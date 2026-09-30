#include "ps2_vu1_impl.inl"
#include "runtime/gs/gs_frontend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")

void registerGeneratedVu1Programs();

namespace
{
    using A = VU1InterpreterAccess;

    struct Sampler
    {
        std::atomic<bool> running{false};
        std::atomic<bool> active{false};
        std::unordered_map<uint64_t, uint64_t> hits;
        uint64_t samples = 0;
        std::thread thread;

        std::vector<uint64_t> rips;

        void start(HANDLE target)
        {
            rips.reserve(1u << 22);
            running = true;
            thread = std::thread([this, target]()
                                 {
                                     timeBeginPeriod(1);
                                     while (running.load())
                                     {
                                         Sleep(1);
                                         if (!active.load())
                                             continue;
                                         if (SuspendThread(target) == static_cast<DWORD>(-1))
                                             continue;
                                         CONTEXT context{};
                                         context.ContextFlags = CONTEXT_CONTROL;
                                         const bool ok = GetThreadContext(target, &context) != 0;
                                         ResumeThread(target);
                                         if (ok && rips.size() < rips.capacity())
                                             rips.push_back(context.Rip);
                                     }
                                     timeEndPeriod(1); });
        }

        void stop()
        {
            running = false;
            thread.join();
            for (const uint64_t rip : rips)
                ++hits[rip];
            samples = rips.size();
        }

        void report()
        {
            HANDLE process = GetCurrentProcess();
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(process, nullptr, TRUE);
            std::map<std::string, uint64_t> functions;
            std::map<std::string, uint64_t> lines;
            for (const auto &[address, count] : hits)
            {
                alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512];
                auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = 511;
                DWORD64 displacement = 0;
                std::string name = "?";
                if (SymFromAddr(process, address, &displacement, symbol))
                    name = displacement > 0x10000u ? std::string("far+") + symbol->Name : std::string(symbol->Name);
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;
                std::string where = name;
                if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                {
                    std::string file = line.FileName;
                    const size_t slash = file.find_last_of("\\/");
                    where += " (" + file.substr(slash == std::string::npos ? 0 : slash + 1) + ":" + std::to_string(line.LineNumber) + ")";
                }
                functions[name] += count;
                lines[where] += count;
                if (name == "_NLG_Return2" && count * 200u > samples)
                    std::printf("  raw %llx +%llx count %llu\n", static_cast<unsigned long long>(address),
                                static_cast<unsigned long long>(displacement), static_cast<unsigned long long>(count));
            }
            const auto top = [&](const char *title, const std::map<std::string, uint64_t> &table, size_t limit)
            {
                std::vector<std::pair<uint64_t, std::string>> sorted;
                for (const auto &[key, count] : table)
                    sorted.emplace_back(count, key);
                std::sort(sorted.rbegin(), sorted.rend());
                std::printf("%s (%llu samples):\n", title, static_cast<unsigned long long>(samples));
                for (size_t i = 0; i < std::min(limit, sorted.size()); ++i)
                    std::printf("  %6.2f%%  %s\n", 100.0 * sorted[i].first / std::max<uint64_t>(samples, 1u), sorted[i].second.c_str());
            };
            top("functions", functions, 25);
            top("lines", lines, 60);
        }
    };

    struct Record
    {
        uint32_t maxCycles = 0;
        uint64_t hash = 0;
        uint64_t tick = 0;
        A::TraceState state;
        size_t diff = 0;
    };

    std::vector<uint8_t> readFile(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    template <typename T>
    T readAt(const std::vector<uint8_t> &buffer, size_t &pos)
    {
        T value{};
        std::memcpy(&value, buffer.data() + pos, sizeof(T));
        pos += sizeof(T);
        return value;
    }

    void applyDiff(const std::vector<uint8_t> &buffer, size_t pos, uint8_t *mem)
    {
        for (;;)
        {
            const uint16_t index = readAt<uint16_t>(buffer, pos);
            const uint16_t count = readAt<uint16_t>(buffer, pos);
            if (index == 0xFFFFu && count == 0u)
                return;
            std::memcpy(mem + index * 16u, buffer.data() + pos, count * 16u);
            pos += count * 16u;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: sotc_vu1_bench <trace.bin> <image-dir> [--verify] [--interp] [--repeat N] [--cpu-cycles]\n");
        return 2;
    }
    const std::string tracePath = argv[1];
    const std::string imageDir = argv[2];
    bool verify = false;
    bool interp = false;
    bool profile = false;
    bool cpuCycles = false;
    uint32_t onlyPc = ~0u;
    int repeat = 1;
    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--verify")
            verify = true;
        else if (arg == "--interp")
            interp = true;
        else if (arg == "--profile")
            profile = true;
        else if (arg == "--cpu-cycles")
            cpuCycles = true;
        else if (arg == "--only" && i + 1 < argc)
            onlyPc = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 16));
        else if (arg == "--repeat" && i + 1 < argc)
            repeat = std::atoi(argv[++i]);
    }
    registerGeneratedVu1Programs();
    if (std::getenv("BENCH_RZ"))
        _mm_setcsr(_mm_getcsr() | 0x6000u);

    const std::vector<uint8_t> buffer = readFile(tracePath);
    std::vector<Record> records;
    for (size_t pos = 0; pos + 24u <= buffer.size();)
    {
        if (readAt<uint32_t>(buffer, pos) != 0x52543156u)
        {
            std::fprintf(stderr, "bad record at %zu\n", pos - 4u);
            return 1;
        }
        Record record;
        record.maxCycles = readAt<uint32_t>(buffer, pos);
        record.hash = readAt<uint64_t>(buffer, pos);
        record.tick = readAt<uint64_t>(buffer, pos);
        record.state = readAt<A::TraceState>(buffer, pos);
        record.diff = pos;
        for (;;)
        {
            const uint16_t index = readAt<uint16_t>(buffer, pos);
            const uint16_t count = readAt<uint16_t>(buffer, pos);
            if (index == 0xFFFFu && count == 0u)
                break;
            pos += count * 16u;
        }
        records.push_back(record);
    }
    if (records.empty())
    {
        std::fprintf(stderr, "no records\n");
        return 1;
    }

    std::map<uint64_t, std::vector<uint8_t>> images;
    std::map<uint64_t, A::CompiledProgram> programs;
    for (const Record &record : records)
    {
        if (images.count(record.hash))
            continue;
        char name[64];
        std::snprintf(name, sizeof(name), "/vu1_%016" PRIx64 ".bin", record.hash);
        images[record.hash] = readFile(imageDir + name);
        programs[record.hash] = findCompiledVu1ProgramByHash(record.hash);
        std::printf("image %016" PRIx64 ": %s, %s\n", record.hash, images[record.hash].size() == 0x4000u ? "loaded" : "MISSING",
                    programs[record.hash] ? "compiled" : "not compiled");
    }

    auto vu = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
    GS gs;
    std::vector<uint8_t> current(0x4000u, 0u);
    std::vector<uint8_t> work(0x4000u, 0u);
    vu1_impl::Vu1PacketSink sink;
    sink.forward = false;
    std::map<uint64_t, double> imageSeconds;
    std::map<std::pair<uint64_t, uint32_t>, std::pair<double, uint64_t>> entryStats;
    std::map<std::pair<uint64_t, uint32_t>, uint64_t> entryRuns;
    double total = 0.0;
    uint64_t mismatches = 0;
    uint64_t runs = 0;
    uint64_t vuCycles = 0;
    uint64_t threadCycles = 0;
    VU1State lastRegs{};
    bool haveLast = false;
    Sampler sampler;
    HANDLE self = nullptr;
    if (profile)
    {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
        sampler.start(self);
    }
    for (int pass = 0; pass < repeat; ++pass)
    {
        std::fill(current.begin(), current.end(), 0u);
        for (const Record &record : records)
        {
            applyDiff(buffer, record.diff, current.data());
            std::vector<uint8_t> &image = images[record.hash];
            if (image.size() != 0x4000u)
                continue;
            if (onlyPc != ~0u && record.state.state.pc != onlyPc)
                continue;
            std::memcpy(work.data(), current.data(), work.size());
            A::traceRestore(*vu, record.state);
            if (verify || !haveLast || std::memcmp(lastRegs.vf, record.state.state.vf, sizeof(lastRegs.vf)) != 0 ||
                std::memcmp(lastRegs.acc, record.state.state.acc, sizeof(lastRegs.acc)) != 0)
                vu1NormShadow.valid = false;
            const A::CompiledProgram compiled = interp ? nullptr : programs[record.hash];
            ++runs;
            if (verify)
            {
                if (!A::runVerifiedWith(*vu, image.data(), 0x4000u, work.data(), 0x4000u, gs, nullptr, record.maxCycles, compiled, false))
                    ++mismatches;
                continue;
            }
            vu1_impl::packetSink = &sink;
            const uint64_t cycleBefore = record.state.cycle;
            sampler.active = true;
            ULONG64 threadBefore = 0;
            if (cpuCycles && !QueryThreadCycleTime(GetCurrentThread(), &threadBefore))
                return 1;
            const auto start = std::chrono::steady_clock::now();
            A::runLoopWith(*vu, image.data(), 0x4000u, work.data(), 0x4000u, gs, nullptr, record.maxCycles, compiled);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            ULONG64 threadAfter = 0;
            if (cpuCycles && !QueryThreadCycleTime(GetCurrentThread(), &threadAfter))
                return 1;
            threadCycles += threadAfter - threadBefore;
            sampler.active = false;
            vu1_impl::packetSink = nullptr;
            sink.packets.clear();
            total += seconds;
            imageSeconds[record.hash] += seconds;
            vuCycles += vu->state().cycles - cycleBefore;
            lastRegs = vu->state();
            haveLast = true;
            auto &entry = entryStats[{record.hash, record.state.state.pc}];
            entry.first += seconds;
            entry.second += vu->state().cycles - cycleBefore;
            ++entryRuns[{record.hash, record.state.state.pc}];
        }
    }
    if (profile)
    {
        sampler.stop();
        sampler.report();
    }
    const double fields = static_cast<double>(records.back().tick - records.front().tick + 1u);
    std::printf("%zu records over %.0f fields, %llu runs\n", records.size(), fields, static_cast<unsigned long long>(runs));
    if (verify)
    {
        std::printf("verify: %llu mismatches\n", static_cast<unsigned long long>(mismatches));
        return mismatches == 0u ? 0 : 1;
    }
    std::printf("total %.3f ms, %.3f ms per field, %.2f M VU cycles per field, %.2f ns per VU cycle\n", total * 1000.0 / repeat,
                total * 1000.0 / repeat / fields, vuCycles / 1e6 / repeat / fields, total * 1e9 / std::max<uint64_t>(vuCycles, 1u));
    if (cpuCycles)
        std::printf("CPU %.3f M thread cycles per field\n", threadCycles / 1e6 / repeat / fields);
    for (const auto &[hash, seconds] : imageSeconds)
        std::printf("  %016" PRIx64 " %.3f ms per field\n", hash, seconds * 1000.0 / repeat / fields);
    std::vector<std::pair<double, std::pair<uint64_t, uint32_t>>> entries;
    for (const auto &[key, value] : entryStats)
        entries.emplace_back(value.first, key);
    std::sort(entries.rbegin(), entries.rend());
    for (size_t i = 0; i < std::min<size_t>(entries.size(), 15u); ++i)
    {
        const auto &key = entries[i].second;
        const auto &value = entryStats[key];
        std::printf("  entry %016" PRIx64 ":%04x %.3f ms/field, %.2f M cycles/field, %llu runs/field, %.2f ns/cycle\n", key.first, key.second,
                    value.first * 1000.0 / repeat / fields, value.second / 1e6 / repeat / fields,
                    static_cast<unsigned long long>(entryRuns[key] / repeat / static_cast<uint64_t>(fields)),
                    value.first * 1e9 / std::max<uint64_t>(value.second, 1u));
    }
    return 0;
}
