#include "sotc/watchdog.h"
#include "sotc/log.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
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

        void loop(int intervalSeconds)
        {
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

    void startFromEnvironment()
    {
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
