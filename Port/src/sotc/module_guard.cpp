#include "sotc/module_guard.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "sotc/sha256.h"
#include "sotc_layout_generated.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <atomic>
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
    void installRamDumpFromEnvironment()
    {
        const char *value = std::getenv("SOTC_DUMP_RAM_AT");
        if (!value)
        {
            return;
        }
        const std::string spec(value);
        const size_t colon = spec.find(':');
        if (colon == std::string::npos)
        {
            SOTC_ERROR(Boot, "SOTC_DUMP_RAM_AT must be <hex address>:<file>");
            return;
        }
        const uint32_t address = static_cast<uint32_t>(std::stoul(spec.substr(0, colon), nullptr, 16));
        const std::string path = spec.substr(colon + 1);
        auto done = std::make_shared<std::atomic<bool>>(false);
        FunctionHooks::instance().observeEntry(address, "ramdump", [address, path, done](uint8_t *rdram, R5900Context *, PS2Runtime *) {
            if (done->exchange(true))
            {
                return;
            }
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(rdram), PS2_RAM_SIZE);
            SOTC_INFO(Boot, "guest RAM dumped at entry of 0x" << std::hex << address << " to " << path);
        });
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
                                          << " sp=" << getRegU32(ctx, 29));
                }
            });
        }
    }
}
