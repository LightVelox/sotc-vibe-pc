#include "sotc/module_guard.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "sotc/sha256.h"
#include "sotc_layout_generated.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <cstring>
#include <fstream>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace sotc
{
    namespace
    {
        void verifyModule(const generated::ModuleLayout &module, uint8_t *rdram, PS2Runtime *runtime)
        {
            std::vector<uint8_t> text(module.textSize);
            std::memcpy(text.data(), rdram + (module.text & PS2_RAM_MASK), module.textSize);
            const auto begin = std::lower_bound(std::begin(generated::kRuntimeRelocationSites), std::end(generated::kRuntimeRelocationSites), module.text);
            for (auto it = begin; it != std::end(generated::kRuntimeRelocationSites) && *it < module.text + module.textSize; ++it)
            {
                std::memset(text.data() + (*it - module.text), 0, 4);
            }
            const std::string hash = Sha256::hex(text.data(), text.size());
            if (hash == module.maskedTextSha256)
            {
                SOTC_INFO(Loader, "module " << module.name << " entered at 0x" << std::hex << module.entry << "; .text 0x"
                                            << module.text << "+0x" << module.textSize << std::dec
                                            << " matches the recompiled image");
                return;
            }
            SOTC_ERROR(Loader, "module " << module.name << " .text in guest RAM does not match the recompiled image (sha256 "
                                         << hash << ", expected " << module.maskedTextSha256 << ")");
            SOTC_ERROR(Loader, "the original loader placed the module differently than PCSX2 did; recompiled code would be invalid");
            runtime->requestStop();
        }
    }

    void applyEeFloatingPointMode()
    {
        _MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO);
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    }

    void installEeFloatingPointMode(PS2Runtime &runtime, uint32_t bootEntry)
    {
        (void)runtime;
        FunctionHooks::instance().observeEntry(bootEntry, "boot_entry", [](uint8_t *, R5900Context *, PS2Runtime *) {
            applyEeFloatingPointMode();
            SOTC_INFO(Ee, "EE FPU host mode: round toward zero, denormals flushed");
        });
    }

    void installModuleGuards(PS2Runtime &runtime)
    {
        (void)runtime;
        for (const auto &module : generated::kModules)
        {
            const generated::ModuleLayout *layout = &module;
            FunctionHooks::instance().observeEntry(module.entry, std::string(module.name) + "_entry",
                                                   [layout](uint8_t *rdram, R5900Context *, PS2Runtime *rt) {
                                                       verifyModule(*layout, rdram, rt);
                                                   });
        }
    }
}

namespace sotc
{
    void installRamDump(const std::string &spec)
    {
        const size_t colon = spec.find(':');
        if (colon == std::string::npos)
        {
            SOTC_ERROR(Boot, "SOTC_DUMP_RAM_AT must be <hex address>:<file>");
            return;
        }
        const uint32_t address = static_cast<uint32_t>(std::stoul(spec.substr(0, colon), nullptr, 16));
        std::string path = spec.substr(colon + 1);
        uint32_t whenAddress = 0;
        uint32_t whenValue = 0;
        bool conditional = false;
        bool whenNotEqual = false;
        double afterSeconds = 0.0;
        const size_t after = path.find(":after=");
        if (after != std::string::npos)
        {
            afterSeconds = std::stod(path.substr(after + 7));
            path = path.substr(0, after);
        }
        const size_t when = path.find(":when=");
        if (when != std::string::npos)
        {
            const std::string condition = path.substr(when + 6);
            path = path.substr(0, when);
            const size_t eq = condition.find('=');
            whenNotEqual = eq > 0 && condition[eq - 1] == '!';
            whenAddress = static_cast<uint32_t>(std::stoul(condition.substr(0, whenNotEqual ? eq - 1 : eq), nullptr, 16));
            whenValue = static_cast<uint32_t>(std::stoul(condition.substr(eq + 1), nullptr, 16));
            conditional = true;
        }
        auto done = std::make_shared<std::atomic<bool>>(false);
        const auto start = std::chrono::steady_clock::now();
        FunctionHooks::instance().observeEntry(address, "ramdump_" + path, [address, path, done, conditional, whenNotEqual, whenAddress, whenValue, afterSeconds, start](uint8_t *rdram, R5900Context *ctx, PS2Runtime *) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < afterSeconds)
            {
                return;
            }
            if (conditional)
            {
                uint32_t current = 0;
                std::memcpy(&current, rdram + (whenAddress & PS2_RAM_MASK), sizeof(current));
                if ((current != whenValue) != whenNotEqual)
                {
                    return;
                }
            }
            if (done->exchange(true))
            {
                return;
            }
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(rdram), PS2_RAM_SIZE);
            SOTC_INFO(Boot, "guest RAM dumped at entry of 0x" << std::hex << address << " to " << path << " R=0x"
                                                            << static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(ctx->vu0_r))));
        });
    }

    void installRamDumpFromEnvironment()
    {
        const char *value = std::getenv("SOTC_DUMP_RAM_AT");
        if (!value)
        {
            return;
        }
        const std::string all(value);
        size_t begin = 0;
        while (begin < all.size())
        {
            const size_t end = all.find(';', begin);
            const std::string spec = all.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            if (!spec.empty())
            {
                installRamDump(spec);
            }
            if (end == std::string::npos)
            {
                break;
            }
            begin = end + 1;
        }
    }

    void installThreadTraceFromEnvironment()
    {
        if (!std::getenv("SOTC_TRACE_THREADS"))
        {
            return;
        }
        FunctionHooks::instance().observeEntry(0x001066E0, "trace_CreateThread", [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *) {
            const uint32_t param = getRegU32(ctx, 4) & PS2_RAM_MASK;
            uint32_t words[7];
            std::memcpy(words, rdram + param, sizeof(words));
            SOTC_INFO(Ee, "CreateThread(entry=0x" << std::hex << words[1] << " stack=0x" << words[2] << " size=0x" << words[3]
                                                  << " gp=0x" << words[4] << " prio=" << std::dec << words[5] << ") from ra=0x" << std::hex
                                                  << getRegU32(ctx, 31));
        });
        FunctionHooks::instance().observeEntry(0x001073D0, "trace_StartThread", [](uint8_t *, R5900Context *ctx, PS2Runtime *) {
            SOTC_INFO(Ee, "StartThread(id=" << getRegU32(ctx, 4) << ", arg=0x" << std::hex << getRegU32(ctx, 5) << ") from ra=0x"
                                            << getRegU32(ctx, 31) << " sp=0x" << getRegU32(ctx, 29));
        });
        FunctionHooks::instance().observeEntry(0x00106770, "trace_ChangeThreadPriority", [](uint8_t *, R5900Context *ctx, PS2Runtime *) {
            SOTC_INFO(Ee, "ChangeThreadPriority(id=" << static_cast<int32_t>(getRegU32(ctx, 4)) << ", prio=" << getRegU32(ctx, 5)
                                                     << ") from ra=0x" << std::hex << getRegU32(ctx, 31));
        });
    }

    void installCallTracesFromEnvironment(PS2Runtime &runtime)
    {
        (void)runtime;
        installRamDumpFromEnvironment();
        installThreadTraceFromEnvironment();
        const char *value = std::getenv("SOTC_TRACE_CALLS");
        if (!value)
        {
            return;
        }
        std::string list(value);
        size_t start = 0;
        while (start < list.size())
        {
            const size_t comma = list.find(',', start);
            const std::string token = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            start = comma == std::string::npos ? list.size() : comma + 1;
            if (token.empty())
            {
                continue;
            }
            const uint32_t address = static_cast<uint32_t>(std::stoul(token, nullptr, 16));
            auto counter = std::make_shared<std::atomic<uint32_t>>(0u);
            FunctionHooks::instance().observeEntry(address, "trace_" + token, [address, counter](uint8_t *, R5900Context *ctx, PS2Runtime *) {
                const uint32_t n = counter->fetch_add(1);
                if (n < 64u || (n & (n - 1u)) == 0u)
                {
                    SOTC_INFO(Ee, "call #" << n << " 0x" << std::hex << address << " a0=" << getRegU32(ctx, 4) << " a1=" << getRegU32(ctx, 5)
                                          << " a2=" << getRegU32(ctx, 6) << " a3=" << getRegU32(ctx, 7) << " ra=" << getRegU32(ctx, 31)
                                          << " sp=" << getRegU32(ctx, 29) << std::dec << " f12=" << ctx->f[12] << " f13=" << ctx->f[13]
                                          << " f14=" << ctx->f[14] << " f15=" << ctx->f[15] << " R=0x" << std::hex
                                          << static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(ctx->vu0_r))) << std::dec);
                }
            });
        }
    }
}
