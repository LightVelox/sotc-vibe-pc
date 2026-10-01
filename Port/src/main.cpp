#include "runtime/ps2_async_vif.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_input_options.h"
#include "runtime/ps2_save_state.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "sotc/function_hooks.h"
#include "sotc/config.h"
#include "sotc/debug_script.h"
#include "sotc/display_mode.h"
#include "sotc/game_disc.h"
#include "sotc/idle_thread.h"
#include "sotc/log.h"
#include "sotc/module_guard.h"
#include "sotc/mouse_camera.h"
#include "sotc/window_icon.h"
#include "sotc/watchdog.h"
#include "sotc/hle/sce_fileio.h"
#include "sotc/hle/sce_cdvd.h"
#include "sotc/hle/libgcc.h"
#include "sotc_layout_generated.h"

#if SOTC_HAS_VU1_PROGRAMS
void registerGeneratedVu1Programs();
#endif

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#endif

namespace
{
    std::filesystem::path executableDirectory()
    {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return std::filesystem::path(buffer).parent_path();
        }
#endif
        return std::filesystem::current_path();
    }

#if defined(_WIN32)
    void printStackTrace()
    {
        void *frames[48];
        const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, nullptr, TRUE);
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 256];
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        for (USHORT i = 0; i < count; ++i)
        {
            std::memset(storage, 0, sizeof(storage));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            const bool haveSymbol = SymFromAddr(process, address, &displacement, symbol) != FALSE;
            const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;
            std::fprintf(stderr, "[crash]   #%u %s+0x%llx %s:%lu\n", static_cast<unsigned>(i), haveSymbol ? symbol->Name : "?",
                         static_cast<unsigned long long>(displacement), haveLine ? line.FileName : "", haveLine ? line.LineNumber : 0ul);
        }
        std::fflush(stderr);
    }

    void installCrashReporter()
    {
        std::set_terminate([]()
                           {
                               std::fprintf(stderr, "[crash] std::terminate on thread %lu\n", GetCurrentThreadId());
                               if (const std::exception_ptr ep = std::current_exception())
                               {
                                   try
                                   {
                                       std::rethrow_exception(ep);
                                   }
                                   catch (const std::exception &e)
                                   {
                                       std::fprintf(stderr, "[crash] exception: %s\n", e.what());
                                   }
                                   catch (...)
                                   {
                                       std::fprintf(stderr, "[crash] non-std exception\n");
                                   }
                               }
                               printStackTrace();
                               std::_Exit(3); });
        std::signal(SIGABRT, [](int)
                    {
                        std::fprintf(stderr, "[crash] abort on thread %lu\n", GetCurrentThreadId());
                        printStackTrace();
                        std::_Exit(3); });
    }
#endif

    void printUsage()
    {
        std::cout << "usage: sotc [--iso <path-to-" << sotc::generated::kSerial << ".iso>]\n"
                  << "settings: sotc.ini beside the executable (created on first launch)\n"
                  << "environment: SOTC_TRACE=BOOT,LOADER,FILE,EE,IOP,GS,VU,SPU2,INPUT,GAME,HOOK|ALL\n";
    }
}

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    installCrashReporter();
#endif
    const std::filesystem::path exeDir = executableDirectory();
    sotc::config::load(exeDir / "sotc.ini");
    sotc::log::configureFromEnvironment();
    std::filesystem::path iso;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--iso" || arg == "-iso") && i + 1 < argc)
        {
            iso = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
        else if (iso.empty() && arg.rfind("-", 0) != 0)
        {
            iso = arg;
        }
    }

    if (iso.empty())
    {
        if (const char *env = std::getenv("SOTC_ISO"))
        {
            iso = env;
        }
    }
    if (iso.empty())
    {
        iso = sotc::findDiscImage({exeDir / "Game", exeDir.parent_path() / "Game", exeDir.parent_path().parent_path() / "Game",
                                   std::filesystem::current_path() / "Game", std::filesystem::current_path()});
    }
    if (iso.empty())
    {
        SOTC_ERROR(Boot, "no disc image found. Pass --iso <path> to your own " << sotc::generated::kSerial << " disc image.");
        printUsage();
        return 2;
    }

    const std::filesystem::path cacheDir = exeDir / "game_data";
    SOTC_INFO(Boot, "disc image: " << iso.string());
    const sotc::DiscCheckResult disc = sotc::verifyDiscAndPrepare(iso, cacheDir);
    if (!disc.ok)
    {
        SOTC_ERROR(Boot, disc.error);
        return 3;
    }

    PS2Runtime::IoPaths paths;
    paths.elfPath = disc.bootElfPath;
    paths.elfDirectory = cacheDir;
    paths.hostRoot = cacheDir;
    paths.cdRoot = cacheDir;
    paths.mcRoot = exeDir / "memcards" / "mc0";
    paths.cdImage = std::filesystem::absolute(iso);
    std::error_code error;
    std::filesystem::create_directories(paths.mcRoot, error);
    ps2x::savestate::setQuickSlotPath(exeDir / "states" / "quick.state");

    GSThreadedBackend::SetEnabledByDefault(true);
    GSThreadedBackend::SetGpuByDefault(true);
    GSGpuBackend::SetAsyncPresentDefault(true);
    ps2x::asyncvif::setDefaultEnabled(true);
    ps2x::asyncvif::setHostPacingDefault(true);
    EeScheduler::setHostPacingDefault(true);
    ps2_host_input::setRightStickInvertDefault(ps2_host_input::kInvertRightX | ps2_host_input::kInvertRightY);
    ps2_host_input::setMouseCameraEnabled(true);

    try
    {
        PS2Runtime runtime;
        if (!runtime.initialize("Shadow of the Colossus"))
        {
            SOTC_ERROR(Boot, "runtime initialization failed");
            return 1;
        }
#if defined(_WIN32)
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC iconResource = FindResourceW(module, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(10));
        if (iconResource)
        {
            const HGLOBAL iconData = LoadResource(module, iconResource);
            const void *iconBytes = iconData ? LockResource(iconData) : nullptr;
            if (iconBytes)
            {
                sotc::applyWindowIcon(static_cast<const unsigned char *>(iconBytes),
                                      static_cast<int>(SizeofResource(module, iconResource)));
            }
        }
#endif
        if (!runtime.loadELF(disc.bootElfPath.string()))
        {
            SOTC_ERROR(Boot, "failed to load " << disc.bootElfPath.string());
            return 1;
        }
        PS2Runtime::setIoPaths(paths);

        const char *missingPolicy = std::getenv("SOTC_MISSING_FUNCTION");
        runtime.setMissingFunctionPolicy(missingPolicy && std::string(missingPolicy) == "continue"
                                             ? PS2Runtime::MissingFunctionPolicy::ContinueToTarget
                                             : PS2Runtime::MissingFunctionPolicy::Stop);
        sotc::FunctionHooks::instance().attach(runtime);
        sotc::installEeFloatingPointMode(runtime, runtime.cpu().pc);
        sotc::installModuleGuards(runtime);
        sotc::hle::installSceFileIo(runtime);
        sotc::hle::installSceCdvd(runtime);
        sotc::hle::installLibgcc(runtime);
        sotc::installIdleThreadSkip(runtime);
        sotc::installDisplayMode(runtime);
        sotc::installMouseCamera(runtime);
#if SOTC_HAS_VU1_PROGRAMS
        registerGeneratedVu1Programs();
#endif
        sotc::installCallTracesFromEnvironment(runtime);
        sotc::installDebugScriptFromEnvironment(runtime);
        sotc::FunctionHooks::instance().logBindings();

        sotc::watchdog::startFromEnvironment(&runtime);
        SOTC_INFO(Boot, "starting guest at 0x" << std::hex << runtime.cpu().pc);
        runtime.run();
        SOTC_INFO(Boot, "runtime exited");
    }
    catch (const std::exception &e)
    {
        SOTC_ERROR(Boot, "fatal: " << e.what());
        std::cout.flush();
        std::_Exit(1);
    }
    std::cout.flush();
    std::_Exit(0);
}
