#include "sotc/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace sotc::log
{
    namespace
    {
        std::atomic<uint32_t> g_traceMask{0};
        std::mutex g_writeMutex;
        const auto g_start = std::chrono::steady_clock::now();

        struct NamedCategory
        {
            Category category;
            std::string_view name;
        };

        constexpr NamedCategory kCategories[] = {
            {Category::Boot, "BOOT"}, {Category::Loader, "LOADER"}, {Category::File, "FILE"},
            {Category::Ee, "EE"}, {Category::Iop, "IOP"}, {Category::Gs, "GS"},
            {Category::Vu, "VU"}, {Category::Spu2, "SPU2"}, {Category::Input, "INPUT"},
            {Category::Game, "GAME"}, {Category::Hook, "HOOK"},
        };
    }

    std::string_view name(Category category)
    {
        for (const auto &entry : kCategories)
        {
            if (entry.category == category)
            {
                return entry.name;
            }
        }
        return "?";
    }

    void configureFromEnvironment()
    {
        const char *value = std::getenv("SOTC_TRACE");
        if (!value)
        {
            return;
        }
        uint32_t mask = 0;
        std::string_view list(value);
        while (!list.empty())
        {
            const size_t comma = list.find(',');
            const std::string_view token = list.substr(0, comma);
            for (const auto &entry : kCategories)
            {
                if (token == entry.name || token == "ALL")
                {
                    mask |= static_cast<uint32_t>(entry.category);
                }
            }
            list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        }
        g_traceMask.store(mask);
    }

    void setTraceMask(uint32_t mask)
    {
        g_traceMask.store(mask);
    }

    bool traceEnabled(Category category)
    {
        return (g_traceMask.load(std::memory_order_relaxed) & static_cast<uint32_t>(category)) != 0u;
    }

    void write(Category category, Level level, std::string_view message)
    {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
        const char *prefix = level == Level::Error ? " ERROR" : level == Level::Warn ? " WARN" : "";
        std::lock_guard<std::mutex> lock(g_writeMutex);
        std::fprintf(level <= Level::Warn ? stderr : stdout, "[%9.4f][%.*s]%s %.*s\n", seconds,
                     static_cast<int>(name(category).size()), name(category).data(), prefix,
                     static_cast<int>(message.size()), message.data());
        std::fflush(level <= Level::Warn ? stderr : stdout);
    }
}
