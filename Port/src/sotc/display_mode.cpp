#include "sotc/display_mode.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kBootSelectDisplayMode = 0x0138C358u;
        constexpr uint32_t kVideoMode = 0x001DC9D4u;
        constexpr uint32_t kFramebufferResetRequested = 0x0128F6A4u;
        constexpr uint32_t kFramebufferResetTarget = 0x0128F6A8u;
        constexpr uint32_t kDisplayResetCallback = 0x0142ADF0u;

        uint32_t g_videoMode = 0u;

        uint32_t read32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value;
            std::memcpy(&value, rdram + (address & PS2_RAM_MASK), sizeof(value));
            return value;
        }

        void write32(uint8_t *rdram, uint32_t address, uint32_t value)
        {
            std::memcpy(rdram + (address & PS2_RAM_MASK), &value, sizeof(value));
        }

        void selectDisplayMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            SOTC_INFO(Hook, "selecting " << (g_videoMode == 0u ? "NTSC 60 Hz" : "PAL 50 Hz") << " without the display menu");
            if (read32(rdram, kVideoMode) != g_videoMode)
            {
                write32(rdram, kVideoMode, g_videoMode);
                write32(rdram, kFramebufferResetRequested, 1u);
                write32(rdram, kFramebufferResetTarget, kDisplayResetCallback);
            }
            ctx->pc = GPR_U32(ctx, 31);
        }
    }

    void installDisplayMode(PS2Runtime &runtime)
    {
        (void)runtime;
        if (const char *setting = std::getenv("SOTC_VIDEO_MODE"))
        {
            const std::string_view value(setting);
            if (value == "PAL")
                g_videoMode = 1u;
            else if (value != "NTSC")
                SOTC_ERROR(Boot, "invalid SOTC_VIDEO_MODE='" << value << "'; using NTSC");
        }
        if (!FunctionHooks::instance().replace(kBootSelectDisplayMode, "bootSelectDisplayMode", &selectDisplayMode,
                                               ReplacementStatus::Native, "video mode from SOTC_VIDEO_MODE; skip selection menu"))
            SOTC_ERROR(Hook, "failed to bind display mode selection");
    }
}
