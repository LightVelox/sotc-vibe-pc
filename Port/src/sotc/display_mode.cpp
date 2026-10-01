#include "sotc/display_mode.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_display_options.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <stdexcept>
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
        constexpr uint32_t kOptionWorkCheck = 0x0135FE58u;
        constexpr uint32_t kWidescreenOption = 0x012E5890u;
        constexpr uint32_t kProgressiveOption = 0x012E58A0u;
        constexpr uint32_t kSetScreenWide = 0x0125BBB0u;
        constexpr uint32_t kWideRatio = 0x012939CCu;
        constexpr uint32_t kGsRegister = 0x001C0928u;
        constexpr uint32_t kFloatToFixed = 0x001B9498u;

        uint32_t g_videoMode = 0u;
        bool g_widescreen = true;
        bool g_widescreenActive = false;
        GuestFunction g_originalFloatToFixed = nullptr;
        GuestFunction g_originalSetScreenWide = nullptr;

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

        int32_t scaleUiX(int32_t value)
        {
            const double scale = (4.0 / 3.0) / static_cast<double>(ps2_host_display::aspect());
            return static_cast<int32_t>(std::lround(static_cast<double>(value) * scale));
        }

        void updateProjectionAspect(uint8_t *rdram, R5900Context *, PS2Runtime *)
        {
            if (g_widescreen && !g_widescreenActive)
                SOTC_INFO(Hook, "activating widescreen from the camera projection");
            g_widescreenActive = g_widescreen;
            ps2_host_display::setFitWindow(g_widescreen);
            ps2_host_display::setAspect(4.0f / 3.0f);
            write32(rdram, kWidescreenOption, g_widescreen ? 1u : 0u);
            const float baseRatio = read32(rdram, kProgressiveOption) != 0u ? 0.75f : 1.0f;
            const float ratio = baseRatio * ps2_host_display::aspect() / (4.0f / 3.0f);
            uint32_t bits;
            std::memcpy(&bits, &ratio, sizeof(bits));
            write32(rdram, kWideRatio, bits);
        }

        void selectScreenAspect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            SET_GPR_U32(ctx, 4, g_widescreen ? 1u : 0u);
            g_originalSetScreenWide(rdram, ctx, runtime);
            updateProjectionAspect(rdram, ctx, runtime);
        }

        void correctMenuBackground(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t caller = GPR_U32(ctx, 31);
            if (!g_widescreenActive || caller < 0x014130E0u || caller >= 0x01413330u)
                return;
            const double scale = (16.0 / 9.0) / static_cast<double>(ps2_host_display::aspect());
            for (int reg : {4, 6})
                SET_GPR_S32(ctx, reg, static_cast<int32_t>(std::lround(static_cast<int32_t>(GPR_U32(ctx, reg)) * scale)));
        }

        void correctSpriteRegisters(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            if (!g_widescreenActive)
                return;
            const uint32_t caller = GPR_U32(ctx, 31);
            const bool sprite = caller >= 0x01197C98u && caller < 0x0119AA48u;
            const bool gauge = caller >= 0x01406438u && caller < 0x0140A4ACu;
            if (!sprite && !gauge)
                return;
            const uint32_t reg = GPR_U32(ctx, 4);
            if (reg != 0x4u && reg != 0x5u && reg != 0xCu && reg != 0xDu)
                return;
            const uint64_t value = GPR_U64(ctx, 5);
            const int32_t x = static_cast<int32_t>(value & 0xFFFFu) - 0x8000;
            const uint32_t corrected = static_cast<uint32_t>(scaleUiX(x) + 0x8000);
            SET_GPR_U64(ctx, 5, (value & ~uint64_t{0xFFFFu}) | (corrected & 0xFFFFu));
        }

        void correctWindowSprite(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            if (!g_widescreenActive)
                return;
            SET_GPR_S32(ctx, 4, scaleUiX(static_cast<int32_t>(GPR_U32(ctx, 4))));
            SET_GPR_S32(ctx, 6, scaleUiX(static_cast<int32_t>(GPR_U32(ctx, 6))));
        }

        void correctFontCoordinates(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t caller = GPR_U32(ctx, 31);
            const uint32_t output = GPR_U32(ctx, 4);
            g_originalFloatToFixed(rdram, ctx, runtime);
            if (!g_widescreenActive || caller < 0x01185608u || caller >= 0x01185C48u)
                return;
            for (uint32_t offset : {0u, 8u})
            {
                const int32_t x = static_cast<int32_t>(read32(rdram, output + offset));
                write32(rdram, output + offset, static_cast<uint32_t>(scaleUiX(x)));
            }
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
        if (const char *setting = std::getenv("SOTC_WIDESCREEN"))
        {
            const std::string_view value(setting);
            if (value == "0")
                g_widescreen = false;
            else if (value != "1")
                SOTC_ERROR(Boot, "invalid SOTC_WIDESCREEN='" << value << "'; using adaptive widescreen");
        }
        g_originalFloatToFixed = runtime.lookupFunction(kFloatToFixed);
        g_originalSetScreenWide = runtime.lookupFunction(kSetScreenWide);
        if (!g_originalFloatToFixed || !g_originalSetScreenWide)
        {
            SOTC_ERROR(Hook, "cannot install widescreen: font coordinate function missing");
            throw std::runtime_error("widescreen font hook missing");
        }
        if (!FunctionHooks::instance().observeEntry(kOptionWorkCheck, "ini_widescreen_option",
                                                  [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *) {
                                                      if (GPR_U32(ctx, 4) == 2u)
                                                          write32(rdram, kWidescreenOption, g_widescreen ? 1u : 0u);
                                                  }) ||
            !FunctionHooks::instance().replace(kSetScreenWide, "native_screen_aspect", &selectScreenAspect,
                                              ReplacementStatus::Native, "native projection adapted to the window aspect") ||
            !FunctionHooks::instance().observeEntry(0x0125A568u, "adaptive_camera_update", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125BF70u, "adaptive_clip_screen_update", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125BC10u, "adaptive_wide_ratio", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125BC58u, "adaptive_camera_aspect", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125BD08u, "adaptive_projection", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125BE10u, "adaptive_original_projection", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125C110u, "adaptive_screen_projection", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x0125C400u, "adaptive_clip_projection", &updateProjectionAspect) ||
            !FunctionHooks::instance().observeEntry(0x001C0370u, "adaptive_menu_background", &correctMenuBackground) ||
            !FunctionHooks::instance().observeEntry(kGsRegister, "widescreen_sprite_coordinates", &correctSpriteRegisters) ||
            !FunctionHooks::instance().observeEntry(0x01247740u, "widescreen_window_sprite", &correctWindowSprite) ||
            !FunctionHooks::instance().observeEntry(0x012478B8u, "widescreen_window_sprite_uv", &correctWindowSprite) ||
            !FunctionHooks::instance().observeEntry(0x01247A18u, "widescreen_window_sprite_solid", &correctWindowSprite) ||
            !FunctionHooks::instance().replace(kFloatToFixed, "widescreen_font_coordinates", &correctFontCoordinates,
                                              ReplacementStatus::Native, "preserve font proportions at the window aspect"))
            throw std::runtime_error("cannot install widescreen display hooks");
        SOTC_INFO(Boot, "display aspect: " << (g_widescreen ? "adaptive widescreen" : "4:3"));
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
