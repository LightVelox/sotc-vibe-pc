#include "sotc/watchdog.h"
#include "sotc/log.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <sstream>
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
        std::map<std::string, int> self;
        std::map<std::string, int> inclusive;
        int samples = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(durationSeconds);
        while (std::chrono::steady_clock::now() < end && g_running.load())
        {
            const auto frames = captureThreadStack(L"GameThread", 40);
            if (!frames.empty())
            {
                ++samples;
                auto strip = [](const std::string &f) { return f.substr(0, f.find('+')); };
                ++self[strip(frames.front())];
                std::vector<std::string> seen;
                for (const auto &f : frames)
                {
                    const std::string name = strip(f);
                    if (std::find(seen.begin(), seen.end(), name) == seen.end())
                    {
                        seen.push_back(name);
                        ++inclusive[name];
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        auto report = [&](const char *title, const std::map<std::string, int> &table)
        {
            std::vector<std::pair<int, std::string>> sorted;
            for (const auto &[name, count] : table)
            {
                sorted.emplace_back(count, name);
            }
            std::sort(sorted.rbegin(), sorted.rend());
            std::ostringstream out;
            out << title << " (" << samples << " samples):";
            for (size_t i = 0; i < sorted.size() && i < 25; ++i)
            {
                out << "\n    " << (100.0 * sorted[i].first / std::max(1, samples)) << "%  " << sorted[i].second;
            }
            SOTC_INFO(Ee, out.str());
        };
        report("profile self", self);
        report("profile inclusive", inclusive);
    }

    void startFromEnvironment(PS2Runtime *runtime)
    {
        g_runtime = runtime;
        if (const char *profile = std::getenv("SOTC_PROFILE"))
        {
            const std::string spec(profile);
            const size_t colon = spec.find(':');
            const int delay = colon == std::string::npos ? 0 : std::atoi(spec.substr(0, colon).c_str());
            const int duration = std::atoi(colon == std::string::npos ? spec.c_str() : spec.substr(colon + 1).c_str());
            g_running.store(true);
            std::thread(profileLoop, delay, std::max(1, duration)).detach();
            SOTC_INFO(Boot, "profiling GameThread for " << duration << "s after " << delay << "s");
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
