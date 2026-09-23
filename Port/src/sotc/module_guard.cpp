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
    void installCallTracesFromEnvironment(PS2Runtime &runtime)
    {
        (void)runtime;
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
