#include "sotc/watchdog.h"
#include "sotc/log.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace sotc::watchdog
{
    namespace
    {
        std::atomic<bool> g_running{false};
        std::thread g_thread;
        std::mutex g_symbolMutex;
        bool g_symbolsReady = false;

#if defined(_WIN32)
        HANDLE findThreadByDescription(const wchar_t *description)
        {
            const DWORD pid = GetCurrentProcessId();
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
            {
                return nullptr;
            }
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            HANDLE found = nullptr;
            for (BOOL ok = Thread32First(snapshot, &entry); ok && !found; ok = Thread32Next(snapshot, &entry))
            {
                if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == GetCurrentThreadId())
                {
                    continue;
                }
                HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);
                if (!thread)
                {
                    continue;
                }
                PWSTR name = nullptr;
                if (SUCCEEDED(GetThreadDescription(thread, &name)) && name && wcscmp(name, description) == 0)
                {
                    found = thread;
                }
                if (name)
                {
                    LocalFree(name);
                }
                if (!found)
                {
                    CloseHandle(thread);
                }
            }
            CloseHandle(snapshot);
            return found;
        }

        std::string symbolize(HANDLE process, DWORD64 address)
        {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 511;
            DWORD64 displacement = 0;
            std::ostringstream out;
            if (SymFromAddr(process, address, &displacement, symbol))
            {
                out << symbol->Name << "+0x" << std::hex << displacement;
            }
            else
            {
                out << "0x" << std::hex << address;
            }
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) && line.FileName)
            {
                const char *file = line.FileName;
                for (const char *p = line.FileName; *p; ++p)
                {
                    if (*p == '\\' || *p == '/')
                    {
                        file = p + 1;
                    }
                }
                out << " (" << file << ":" << std::dec << line.LineNumber << ")";
            }
            return out.str();
        }
#endif

        PS2Runtime *g_runtime = nullptr;

#if defined(_WIN32)
        struct WriteWatch
        {
            uint32_t guest = 0;
            uint32_t length = 4;
        };
        std::vector<WriteWatch> g_writeWatches;
        uint8_t *g_watchRam = nullptr;
        std::mutex g_watchMutex;
        std::map<std::pair<DWORD64, int>, uint64_t> g_watchHits;

        LONG CALLBACK writeWatchHandler(EXCEPTION_POINTERS *info)
        {
            if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            CONTEXT *context = info->ContextRecord;
            const DWORD64 status = context->Dr6 & 0xF;
            if (!status)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            context->Dr6 = 0;
            for (int i = 0; i < 4 && i < static_cast<int>(g_writeWatches.size()); ++i)
            {
                if (!(status & (1ull << i)))
                {
                    continue;
                }
                const WriteWatch &watch = g_writeWatches[i];
                uint64_t hits = 0;
                {
                    std::lock_guard<std::mutex> lock(g_watchMutex);
                    hits = ++g_watchHits[{context->Rip, i}];
                }
                uint32_t value = 0;
                std::memcpy(&value, g_watchRam + watch.guest, sizeof(value));
                static const char *const valueFilter = std::getenv("SOTC_WATCH_VALUE");
                static const uint32_t filterValue = valueFilter ? static_cast<uint32_t>(std::stoul(valueFilter, nullptr, 16)) : 0u;
                if (valueFilter ? value != filterValue : (hits > 3 && (hits & (hits - 1)) != 0))
                {
                    continue;
                }
                std::string where;
                {
                    std::lock_guard<std::mutex> lock(g_symbolMutex);
                    if (!g_symbolsReady)
                    {
                        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
                        g_symbolsReady = SymInitialize(GetCurrentProcess(), nullptr, TRUE) == TRUE;
                    }
                    where = symbolize(GetCurrentProcess(), context->Rip);
                    CONTEXT walk = *context;
                    STACKFRAME64 frame{};
                    frame.AddrPC.Offset = walk.Rip;
                    frame.AddrPC.Mode = AddrModeFlat;
                    frame.AddrFrame.Offset = walk.Rbp;
                    frame.AddrFrame.Mode = AddrModeFlat;
                    frame.AddrStack.Offset = walk.Rsp;
                    frame.AddrStack.Mode = AddrModeFlat;
                    for (int depth = 0; depth < 6; ++depth)
                    {
                        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &walk, nullptr,
                                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                            frame.AddrPC.Offset == 0)
                        {
                            break;
                        }
                        if (depth > 0)
                        {
                            where += " <- " + symbolize(GetCurrentProcess(), frame.AddrPC.Offset);
                        }
                    }
                }
                const uint64_t tick = g_runtime ? g_runtime->memory().gs().vsyncTick.load() : 0;
                SOTC_INFO(Ee, "write watch 0x" << std::hex << watch.guest << " = " << value << std::dec << " hit " << hits << " vsync " << tick
                                               << " at " << where);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        void armWriteWatches()
        {
            HANDLE thread = nullptr;
            while (g_running.load() && !(thread = findThreadByDescription(L"GameThread")))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!thread)
            {
                return;
            }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            DWORD64 dr7 = 0;
            DWORD64 *slots[4] = {&context.Dr0, &context.Dr1, &context.Dr2, &context.Dr3};
            for (size_t i = 0; i < g_writeWatches.size() && i < 4; ++i)
            {
                const WriteWatch &watch = g_writeWatches[i];
                *slots[i] = reinterpret_cast<DWORD64>(g_watchRam + watch.guest);
                const DWORD64 len = watch.length == 8 ? 2 : watch.length == 4 ? 3 : watch.length == 2 ? 1 : 0;
                dr7 |= 1ull << (i * 2);
                dr7 |= (1ull | (len << 2)) << (16 + i * 4);
            }
            context.Dr7 = dr7;
            SuspendThread(thread);
            const BOOL ok = SetThreadContext(thread, &context);
            ResumeThread(thread);
            CloseHandle(thread);
            SOTC_INFO(Boot, "write watches armed on GameThread: " << (ok ? "ok" : "failed"));
        }

        void installWriteWatches(const char *spec)
        {
            std::stringstream list{std::string(spec)};
            std::string item;
            while (std::getline(list, item, ',') && g_writeWatches.size() < 4)
            {
                WriteWatch watch;
                const size_t colon = item.find(':');
                watch.guest = static_cast<uint32_t>(std::stoul(item.substr(0, colon), nullptr, 16)) & 0x1FFFFFFu;
                if (colon != std::string::npos)
                {
                    watch.length = static_cast<uint32_t>(std::stoul(item.substr(colon + 1)));
                }
                watch.guest &= ~(watch.length - 1u);
                g_writeWatches.push_back(watch);
            }
            g_watchRam = g_runtime->memory().getRDRAM();
            AddVectoredExceptionHandler(1, writeWatchHandler);
            g_running.store(true);
            std::thread(armWriteWatches).detach();
        }
#endif

        void loop(int intervalSeconds)
        {
            uint64_t lastTick = g_runtime ? g_runtime->memory().gs().vsyncTick.load() : 0;
            auto lastTime = std::chrono::steady_clock::now();
            while (g_running.load())
            {
                for (int i = 0; i < intervalSeconds * 10 && g_running.load(); ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!g_running.load())
                {
                    break;
                }
                const auto frames = captureThreadStack(L"GameThread", 28);
                std::ostringstream message;
                if (g_runtime)
                {
                    const uint64_t tick = g_runtime->memory().gs().vsyncTick.load();
                    const auto now = std::chrono::steady_clock::now();
                    const double seconds = std::chrono::duration<double>(now - lastTime).count();
                    message << "vsync " << tick << " (" << (tick - lastTick) / seconds << "/s); ";
                    lastTick = tick;
                    lastTime = now;
                }
                if (g_runtime && std::getenv("SOTC_WATCHDOG_THREADS"))
                {
                    const EeKernelSnapshot kernel = g_runtime->eeScheduler().snapshot();
                    message << "running=" << kernel.runningThreadId << '\n';
                    for (const auto &t : kernel.threads)
                    {
                        message << "    thread " << t.id << " entry=0x" << std::hex << t.entry << " pc=0x" << t.pc << " ra=0x" << t.ra
                                << " sp=0x" << t.sp << std::dec << " prio=" << t.currentPriority << " status=" << static_cast<int>(t.status)
                                << " wait=" << static_cast<int>(t.waitReason) << ":" << t.waitId << " wakeups=" << t.wakeupCount << '\n';
                    }
                    for (const auto &sema : kernel.semaphores)
                    {
                        if (sema.waiters)
                        {
                            message << "    sema " << sema.id << " count=" << sema.count << "/" << sema.maxCount << " waiters=" << sema.waiters << '\n';
                        }
                    }
                }
                message << "GameThread native stack:";
                for (const auto &frame : frames)
                {
                    message << "\n        " << frame;
                }
                SOTC_INFO(Ee, message.str());
            }
        }
    }

    std::vector<std::string> captureThreadStack(const wchar_t *threadDescription, int maxFrames)
    {
        std::vector<std::string> frames;
#if defined(_WIN32)
        HANDLE process = GetCurrentProcess();
        std::lock_guard<std::mutex> lock(g_symbolMutex);
        if (!g_symbolsReady)
        {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            g_symbolsReady = SymInitialize(process, nullptr, TRUE) == TRUE;
        }
        HANDLE thread = findThreadByDescription(threadDescription);
        if (!thread)
        {
            frames.push_back("<thread not found>");
            return frames;
        }
        if (SuspendThread(thread) == static_cast<DWORD>(-1))
        {
            CloseHandle(thread);
            frames.push_back("<cannot suspend>");
            return frames;
        }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context))
        {
            STACKFRAME64 frame{};
            frame.AddrPC.Offset = context.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
            std::vector<DWORD64> pcs;
            for (int i = 0; i < maxFrames; ++i)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                                 SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                    frame.AddrPC.Offset == 0)
                {
                    break;
                }
                pcs.push_back(frame.AddrPC.Offset);
            }
            ResumeThread(thread);
            for (DWORD64 pc : pcs)
            {
                frames.push_back(symbolize(process, pc));
            }
        }
        else
        {
            ResumeThread(thread);
            frames.push_back("<cannot read context>");
        }
        CloseHandle(thread);
#else
        (void)threadDescription;
        (void)maxFrames;
#endif
        return frames;
    }

    void profileLoop(int delaySeconds, int durationSeconds)
    {
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
#if defined(_WIN32)
        std::wstring threadName = L"GameThread";
        if (const char *name = std::getenv("SOTC_PROFILE_THREAD"))
            threadName.assign(name, name + std::strlen(name));
        HANDLE process = GetCurrentProcess();
        HANDLE thread = findThreadByDescription(threadName.c_str());
        if (!thread)
        {
            SOTC_INFO(Ee, "profile: thread not found");
            return;
        }
        std::vector<std::vector<DWORD64>> stacks;
        {
            std::lock_guard<std::mutex> lock(g_symbolMutex);
            if (!g_symbolsReady)
            {
                SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
                g_symbolsReady = SymInitialize(process, nullptr, TRUE) == TRUE;
            }
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(durationSeconds);
            constexpr size_t kMaxFrames = 48;
            std::array<DWORD64, kMaxFrames> frames{};
            stacks.reserve(static_cast<size_t>(durationSeconds) * 2500u);
            while (std::chrono::steady_clock::now() < end && g_running.load() && stacks.size() < stacks.capacity())
            {
                if (SuspendThread(thread) == static_cast<DWORD>(-1))
                    break;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_FULL;
                size_t depth = 0;
                if (GetThreadContext(thread, &context))
                {
                    while (depth < kMaxFrames && context.Rip != 0)
                    {
                        frames[depth++] = context.Rip;
                        DWORD64 imageBase = 0;
                        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                        if (!function)
                        {
                            context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
                            context.Rsp += 8;
                            continue;
                        }
                        PVOID handlerData = nullptr;
                        DWORD64 establisherFrame = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                                         &establisherFrame, nullptr);
                    }
                }
                ResumeThread(thread);
                if (depth != 0)
                    stacks.emplace_back(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(depth));
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }
        CloseHandle(thread);

        std::map<DWORD64, std::string> names;
        std::map<DWORD64, std::string> lines;
        {
            std::lock_guard<std::mutex> lock(g_symbolMutex);
            for (const auto &stack : stacks)
            {
                for (DWORD64 pc : stack)
                {
                    if (names.count(pc))
                        continue;
                    const std::string full = symbolize(process, pc);
                    names[pc] = full.substr(0, full.find('+'));
                    const size_t paren = full.find(" (");
                    lines[pc] = names[pc] + (paren == std::string::npos ? std::string() : full.substr(paren));
                }
            }
        }
        std::map<std::string, int> self;
        std::map<std::string, int> selfLines;
        std::map<std::string, int> inclusive;
        for (const auto &stack : stacks)
        {
            ++self[names[stack.front()]];
            ++selfLines[lines[stack.front()]];
            std::vector<std::string> seen;
            for (DWORD64 pc : stack)
            {
                const std::string &name = names[pc];
                if (std::find(seen.begin(), seen.end(), name) == seen.end())
                {
                    seen.push_back(name);
                    ++inclusive[name];
                }
            }
        }
        const int samples = static_cast<int>(stacks.size());
        auto report = [&](const char *title, const std::map<std::string, int> &table, size_t count)
        {
            std::vector<std::pair<int, std::string>> sorted;
            for (const auto &[name, hits] : table)
                sorted.emplace_back(hits, name);
            std::sort(sorted.rbegin(), sorted.rend());
            std::ostringstream out;
            out << title << " (" << samples << " samples):";
            for (size_t i = 0; i < sorted.size() && i < count; ++i)
                out << "\n    " << (100.0 * sorted[i].first / std::max(1, samples)) << "%  " << sorted[i].second;
            SOTC_INFO(Ee, out.str());
        };
        report("profile self", self, 30);
        report("profile self lines", selfLines, 30);
        report("profile inclusive", inclusive, 40);
        std::vector<std::pair<int, std::string>> topSelf;
        for (const auto &[name, hits] : self)
            topSelf.emplace_back(hits, name);
        std::sort(topSelf.rbegin(), topSelf.rend());
        for (size_t i = 0; i < topSelf.size() && i < 10; ++i)
        {
            std::map<std::string, int> callers;
            for (const auto &stack : stacks)
            {
                if (names[stack.front()] != topSelf[i].second)
                    continue;
                std::string chain;
                int depth = 0;
                for (size_t f = 1; f < stack.size() && depth < 3; ++f)
                {
                    const std::string &name = names[stack[f]];
                    if (name == topSelf[i].second || name.rfind("Rtl", 0) == 0 || name.rfind("Mtx_", 0) == 0 || name.rfind("std::", 0) == 0 || name.rfind("operator new", 0) == 0 || name == "malloc" || name == "free")
                        continue;
                    chain += (depth ? " <- " : "") + lines[stack[f]];
                    ++depth;
                }
                ++callers[chain];
            }
            report(("callers of " + topSelf[i].second).c_str(), callers, 6);
        }
#else
        (void)durationSeconds;
#endif
    }

    void timelineLoop(std::string path)
    {
        FILE *out = std::fopen(path.c_str(), "w");
        if (!out)
        {
            SOTC_ERROR(Boot, "timeline: cannot open " << path);
            return;
        }
        std::vector<uint32_t> watch;
        if (const char *spec = std::getenv("SOTC_TIMELINE_WATCH"))
        {
            std::stringstream list(spec);
            std::string item;
            while (std::getline(list, item, ','))
                watch.push_back(static_cast<uint32_t>(std::stoul(item, nullptr, 16)));
        }
        const uint8_t *rdram = g_runtime->memory().getRDRAM();
        auto seconds = []()
        { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
        std::fprintf(out, "start %.6f\n", seconds());
        uint64_t last = ~0ull;
        while (g_running.load())
        {
            const uint64_t tick = g_runtime->memory().gs().vsyncTick.load();
            if (tick != last)
            {
                last = tick;
                std::fprintf(out, "%llu %.6f", static_cast<unsigned long long>(tick), seconds());
                for (uint32_t address : watch)
                {
                    uint32_t value = 0;
                    std::memcpy(&value, rdram + (address & 0x1FFFFFFCu), sizeof(value));
                    std::fprintf(out, " %08x", value);
                }
                std::fprintf(out, "\n");
                std::fflush(out);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        std::fclose(out);
    }

    void startFromEnvironment(PS2Runtime *runtime)
    {
        g_runtime = runtime;
#if defined(_WIN32)
        if (const char *watches = std::getenv("SOTC_WATCH_WRITE"))
        {
            installWriteWatches(watches);
        }
#endif
        if (const char *timeline = std::getenv("SOTC_TIMELINE"))
        {
            g_running.store(true);
            std::thread(timelineLoop, std::string(timeline)).detach();
        }
        if (const char *profile = std::getenv("SOTC_PROFILE"))
        {
            std::vector<std::pair<int, int>> windows;
            std::stringstream list{std::string(profile)};
            std::string spec;
            while (std::getline(list, spec, ','))
            {
                const size_t colon = spec.find(':');
                const int delay = colon == std::string::npos ? 0 : std::atoi(spec.substr(0, colon).c_str());
                const int duration = std::atoi(colon == std::string::npos ? spec.c_str() : spec.substr(colon + 1).c_str());
                windows.emplace_back(delay, std::max(1, duration));
                SOTC_INFO(Boot, "profiling for " << duration << "s after " << delay << "s");
            }
            g_running.store(true);
            std::thread([windows]()
                        {
                            const auto start = std::chrono::steady_clock::now();
                            for (const auto &[delay, duration] : windows)
                            {
                                std::this_thread::sleep_until(start + std::chrono::seconds(delay));
                                profileLoop(0, duration);
                            } })
                .detach();
        }
        const char *value = std::getenv("SOTC_WATCHDOG");
        if (!value)
        {
            return;
        }
        const int seconds = std::max(1, std::atoi(value));
        g_running.store(true);
        g_thread = std::thread(loop, seconds);
        SOTC_INFO(Boot, "watchdog: dumping GameThread stack every " << seconds << "s");
    }

    void stop()
    {
        g_running.store(false);
        if (g_thread.joinable())
        {
            g_thread.join();
        }
    }
}
