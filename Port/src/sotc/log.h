#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace sotc::log
{
    enum class Category : uint32_t
    {
        Boot = 1u << 0,
        Loader = 1u << 1,
        File = 1u << 2,
        Ee = 1u << 3,
        Iop = 1u << 4,
        Gs = 1u << 5,
        Vu = 1u << 6,
        Spu2 = 1u << 7,
        Input = 1u << 8,
        Game = 1u << 9,
        Hook = 1u << 10,
    };

    enum class Level
    {
        Error,
        Warn,
        Info,
        Trace,
    };

    void configureFromEnvironment();
    void setTraceMask(uint32_t mask);
    bool traceEnabled(Category category);
    void write(Category category, Level level, std::string_view message);
    std::string_view name(Category category);
}

#define SOTC_LOG(cat, lvl, expr)                                                   \
    do                                                                             \
    {                                                                              \
        if ((lvl) != ::sotc::log::Level::Trace || ::sotc::log::traceEnabled(cat)) \
        {                                                                          \
            std::ostringstream sotcLogStream_;                                     \
            sotcLogStream_ << expr;                                                \
            ::sotc::log::write((cat), (lvl), sotcLogStream_.str());                \
        }                                                                          \
    } while (0)

#define SOTC_INFO(cat, expr) SOTC_LOG(::sotc::log::Category::cat, ::sotc::log::Level::Info, expr)
#define SOTC_WARN(cat, expr) SOTC_LOG(::sotc::log::Category::cat, ::sotc::log::Level::Warn, expr)
#define SOTC_ERROR(cat, expr) SOTC_LOG(::sotc::log::Category::cat, ::sotc::log::Level::Error, expr)
#define SOTC_TRACE(cat, expr) SOTC_LOG(::sotc::log::Category::cat, ::sotc::log::Level::Trace, expr)
