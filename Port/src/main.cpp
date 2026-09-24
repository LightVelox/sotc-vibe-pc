#include "ps2_runtime.h"
#include "sotc/function_hooks.h"
#include "sotc/game_disc.h"
#include "sotc/log.h"
#include "sotc/module_guard.h"
#include "sotc/watchdog.h"
#include "sotc/hle/sce_fileio.h"
#include "sotc/hle/sce_cdvd.h"
#include "sotc_layout_generated.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
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

    void printUsage()
    {
        std::cout << "usage: sotc [--iso <path-to-" << sotc::generated::kSerial << ".iso>]\n"
                  << "environment: SOTC_TRACE=BOOT,LOADER,FILE,EE,IOP,GS,VU,SPU2,INPUT,GAME,HOOK|ALL\n";
    }
}

int main(int argc, char *argv[])
{
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

    const std::filesystem::path exeDir = executableDirectory();
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

    try
    {
        PS2Runtime runtime;
        const std::string title = std::string("Shadow of the Colossus (") + sotc::generated::kSerial + ") - native";
        if (!runtime.initialize(title.c_str()))
        {
            SOTC_ERROR(Boot, "runtime initialization failed");
            return 1;
        }
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
        sotc::installCallTracesFromEnvironment(runtime);
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
