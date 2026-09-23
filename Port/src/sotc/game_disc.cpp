#include "sotc/game_disc.h"
#include "sotc/log.h"
#include "sotc/sha256.h"
#include "sotc_layout_generated.h"
#include "runtime/ps2_cd_image.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace sotc
{
    namespace
    {
        bool readWholeFile(const PS2CdImage &image, std::string_view ps2Path, std::vector<uint8_t> &out)
        {
            PS2CdImage::Entry entry{};
            if (!image.find(ps2Path, entry) || entry.directory)
            {
                return false;
            }
            out.resize(static_cast<size_t>(entry.size));
            return image.read(static_cast<uint64_t>(entry.lba) * PS2CdImage::kSectorSize, out.data(), out.size());
        }

        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    std::filesystem::path findDiscImage(const std::vector<std::filesystem::path> &searchRoots)
    {
        for (const auto &root : searchRoots)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(root, error))
            {
                continue;
            }
            for (const auto &entry : std::filesystem::directory_iterator(root, error))
            {
                if (!entry.is_regular_file(error))
                {
                    continue;
                }
                const std::string ext = lower(entry.path().extension().string());
                if (ext == ".iso" && entry.file_size(error) == generated::kDiscSize)
                {
                    return entry.path();
                }
            }
        }
        return {};
    }

    DiscCheckResult verifyDiscAndPrepare(const std::filesystem::path &imagePath, const std::filesystem::path &cacheDir)
    {
        DiscCheckResult result;
        PS2CdImage &image = PS2CdImage::instance();
        if (!image.open(imagePath))
        {
            result.error = "cannot mount " + imagePath.string() + " as an ISO9660 disc image";
            return result;
        }

        std::vector<uint8_t> systemCnf;
        if (!readWholeFile(image, "SYSTEM.CNF", systemCnf))
        {
            result.error = "SYSTEM.CNF not found; this is not a PS2 game disc";
            return result;
        }
        const std::string cnf(systemCnf.begin(), systemCnf.end());
        if (cnf.find(generated::kBootElfPath) == std::string::npos)
        {
            result.error = std::string("SYSTEM.CNF does not boot ") + generated::kBootElfPath + "; expected " +
                           generated::kSerial + " v" + generated::kVersion;
            return result;
        }

        std::vector<uint8_t> bootElf;
        if (!readWholeFile(image, generated::kBootElfPath, bootElf))
        {
            result.error = std::string("boot executable ") + generated::kBootElfPath + " missing";
            return result;
        }
        const std::string bootHash = Sha256::hex(bootElf.data(), bootElf.size());
        if (bootHash != generated::kBootElfSha256)
        {
            result.error = std::string("boot executable hash mismatch (") + bootHash + "); this build supports only " +
                           generated::kSerial + " v" + generated::kVersion;
            return result;
        }
        SOTC_INFO(Boot, generated::kBootElfPath << " sha256 " << bootHash << " (matches " << generated::kSerial << " v"
                                                << generated::kVersion << ")");

        for (const auto &module : generated::kModuleFiles)
        {
            std::vector<uint8_t> data;
            if (!readWholeFile(image, module.path, data))
            {
                result.error = std::string("module ") + module.path + " missing from disc";
                return result;
            }
            const std::string hash = Sha256::hex(data.data(), data.size());
            if (hash != module.sha256)
            {
                result.error = std::string("module ") + module.path + " hash mismatch (" + hash + ")";
                return result;
            }
            SOTC_INFO(Boot, module.path << " sha256 " << hash);
        }

        std::error_code error;
        std::filesystem::create_directories(cacheDir, error);
        result.bootElfPath = cacheDir / generated::kBootElfPath;
        std::ofstream out(result.bootElfPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bootElf.data()), static_cast<std::streamsize>(bootElf.size()));
        if (!out)
        {
            result.error = "cannot write " + result.bootElfPath.string();
            return result;
        }
        result.ok = true;
        return result;
    }
}
