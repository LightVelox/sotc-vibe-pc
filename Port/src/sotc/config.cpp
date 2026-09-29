#include "sotc/config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace sotc::config
{
    namespace
    {
        constexpr std::string_view kDefaults =
            "[Settings]\n"
            "PS2X_MEMCARD=1\n"
            "PS2X_INVERT_RIGHT_STICK=xy\n"
            "PS2X_MOUSE_SENSITIVITY=1.0\n"
            "PS2X_MOUSE_VERTICAL_SENSITIVITY=1.5\n"
            "PS2X_GS_GPU=1\n"
            "PS2X_GS_THREAD=1\n"
            "PS2X_MTVU=1\n"
            "PS2X_VU1_RECOMP=1\n"
            "SOTC_VIDEO_MODE=NTSC\n";

        constexpr std::array<std::pair<std::string_view, std::string_view>, 16> kDefaultBindings{{
            {"PS2X_BIND_UP", "UP"},
            {"PS2X_BIND_DOWN", "DOWN"},
            {"PS2X_BIND_LEFT", "LEFT"},
            {"PS2X_BIND_RIGHT", "RIGHT"},
            {"PS2X_BIND_CROSS", "F"},
            {"PS2X_BIND_CIRCLE", "MOUSE_RIGHT"},
            {"PS2X_BIND_SQUARE", "MOUSE_LEFT"},
            {"PS2X_BIND_TRIANGLE", "SPACE"},
            {"PS2X_BIND_L1", "Q"},
            {"PS2X_BIND_R1", "LEFT_SHIFT"},
            {"PS2X_BIND_L2", "LEFT_CTRL"},
            {"PS2X_BIND_R2", "RIGHT_SHIFT"},
            {"PS2X_BIND_START", "ENTER"},
            {"PS2X_BIND_SELECT", "TAB"},
            {"PS2X_BIND_L3", "H"},
            {"PS2X_BIND_R3", "G"},
        }};

        std::string trim(std::string_view value)
        {
            const size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const size_t last = value.find_last_not_of(" \t\r\n");
            return std::string(value.substr(first, last - first + 1));
        }

        bool validKey(std::string_view key)
        {
            if (!key.starts_with("PS2X_") && !key.starts_with("SOTC_"))
                return false;
            return std::all_of(key.begin(), key.end(), [](unsigned char ch)
                               { return std::isupper(ch) || std::isdigit(ch) || ch == '_'; });
        }
    }

    void load(const std::filesystem::path &path)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error) && !error)
        {
            std::ofstream created(path);
            if (created)
            {
                created << kDefaults;
                for (const auto &[key, value] : kDefaultBindings)
                    created << key << '=' << value << '\n';
            }
        }

        std::ifstream input(path);
        if (!input)
        {
            std::cerr << "Cannot read settings file: " << path.string() << '\n';
            return;
        }

        bool inSettings = false;
        std::array<bool, kDefaultBindings.size()> bindingPresent{};
        std::string line;
        size_t lineNumber = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF"))
                line.erase(0, 3);
            const std::string text = trim(line);
            if (text.empty() || text.front() == ';' || text.front() == '#')
                continue;
            if (text.front() == '[' && text.back() == ']')
            {
                inSettings = trim(std::string_view(text).substr(1, text.size() - 2)) == "Settings";
                continue;
            }
            if (!inSettings)
                continue;
            const size_t equals = text.find('=');
            if (equals == std::string::npos)
            {
                std::cerr << "Invalid setting at " << path.string() << ':' << lineNumber << '\n';
                continue;
            }
            const std::string key = trim(std::string_view(text).substr(0, equals));
            std::string value = trim(std::string_view(text).substr(equals + 1));
            if (!validKey(key))
            {
                std::cerr << "Unknown setting at " << path.string() << ':' << lineNumber << '\n';
                continue;
            }
            for (size_t i = 0; i < kDefaultBindings.size(); ++i)
            {
                if (key == kDefaultBindings[i].first)
                    bindingPresent[i] = true;
            }
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (std::getenv(key.c_str()))
                continue;
#if defined(_WIN32)
            if (_putenv_s(key.c_str(), value.c_str()) != 0)
#else
            if (setenv(key.c_str(), value.c_str(), 0) != 0)
#endif
                std::cerr << "Cannot apply setting " << key << " from " << path.string() << '\n';
        }

        if (std::any_of(bindingPresent.begin(), bindingPresent.end(), [](bool present) { return !present; }))
        {
            std::ofstream append(path, std::ios::app);
            if (append)
            {
                append << "\n[Settings]\n";
                for (size_t i = 0; i < kDefaultBindings.size(); ++i)
                {
                    if (!bindingPresent[i])
                        append << kDefaultBindings[i].first << '=' << kDefaultBindings[i].second << '\n';
                }
            }
        }
    }
}
