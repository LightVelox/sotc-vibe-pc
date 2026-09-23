#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sotc
{
    struct DiscCheckResult
    {
        bool ok = false;
        std::string error;
        std::filesystem::path bootElfPath;
    };

    std::filesystem::path findDiscImage(const std::vector<std::filesystem::path> &searchRoots);
    DiscCheckResult verifyDiscAndPrepare(const std::filesystem::path &image, const std::filesystem::path &cacheDir);
}
