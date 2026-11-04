#include "sotc/native_effects.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_types.h"

#include <array>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kGsReg = 0x001C0928u;
        constexpr uint32_t kGsRegEnd = 0x001BE8F8u;
        struct EffectRange
        {
            uint32_t begin, end;
            GSPostEffect effect;
            const char *setting;
            bool enabled = false;
        };
        std::array<EffectRange, 7> g_effects{{
            {0x0117CA50u, 0x0117D6F8u, GSPostEffect::DepthOfField, "SOTC_NATIVE_DOF"},
            {0x0117F270u, 0x0117F754u, GSPostEffect::FeedbackBlur, "SOTC_NATIVE_FEEDBACK_BLUR"},
            {0x0117FDF8u, 0x0118077Cu, GSPostEffect::ScreenBlur, "SOTC_NATIVE_SCREEN_BLUR"},
            {0x01193A78u, 0x011952E0u, GSPostEffect::SolarFlare, "SOTC_NATIVE_SOLAR_FLARE"},
            {0x011952E0u, 0x01196CF0u, GSPostEffect::SolarFlare, "SOTC_NATIVE_SOLAR_FLARE"},
            {0x011A1BA8u, 0x011A1EFCu, GSPostEffect::ZFog, "SOTC_NATIVE_ZFOG"},
            {0x0117F900u, 0x0117FA7Cu, GSPostEffect::FrontFog, "SOTC_NATIVE_FRONT_FOG"},
        }};
        GuestFunction g_writeRegister = nullptr;
        std::unordered_map<int, GSPostEffect> g_packets;

        bool enabled(const char *key, bool fallback)
        {
            if (const char *value = std::getenv(key))
                return std::string_view(value) == "1";
            return fallback;
        }

        void annotate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, GSPostEffect effect)
        {
            // gsReg is a leaf packet writer. Append the annotation through the same writer, then
            // restore all guest registers so its caller sees the original invocation unchanged.
            const R5900Context saved = *ctx;
            ctx->pc = kGsReg;
            SET_GPR_U32(ctx, 4, GS_REG_HOST_EFFECT);
            SET_GPR_U64(ctx, 5, static_cast<uint32_t>(effect));
            g_writeRegister(rdram, ctx, runtime);
            *ctx = saved;
        }
    }

    void installNativeEffects(PS2Runtime &runtime)
    {
        const bool all = enabled("SOTC_NATIVE_EFFECTS", false);
        const bool brightness = enabled("SOTC_NATIVE_BRIGHTNESS", all);
        bool any = brightness;
        for (auto &effect : g_effects)
        {
            effect.enabled = enabled(effect.setting, all);
            any |= effect.enabled;
        }
        if (!any)
            return;
        g_writeRegister = FunctionHooks::instance().original(kGsReg);
        if (!g_writeRegister)
            g_writeRegister = runtime.lookupFunction(kGsReg);
        if (!g_writeRegister)
            return;

        FunctionHooks::instance().observeEntry(kGsReg, "gsReg native effects",
            [brightness](uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
            {
                const uint32_t caller = GPR_U32(ctx, 31);
                if (brightness && caller == 0x0117F8A0u && GPR_U32(ctx, 4) == GS_REG_XYZF2)
                    SET_GPR_U32(ctx, 4, GS_REG_HOST_BRIGHTNESS);
                for (const auto &effect : g_effects)
                {
                    if (!effect.enabled || caller < effect.begin || caller >= effect.end)
                        continue;
                    auto &packet = g_packets[runtime->eeScheduler().currentThreadId()];
                    if (packet != effect.effect)
                    {
                        annotate(rdram, ctx, runtime, effect.effect);
                        packet = effect.effect;
                    }
                    break;
                }
            });
        FunctionHooks::instance().observeEntry(kGsRegEnd, "gsRegEnd native effects",
            [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
            {
                const auto it = g_packets.find(runtime->eeScheduler().currentThreadId());
                if (it != g_packets.end() && it->second != GSPostEffect::None)
                {
                    annotate(rdram, ctx, runtime, GSPostEffect::None);
                    it->second = GSPostEffect::None;
                }
            });
        if (brightness)
            SOTC_INFO(Hook, "native screenBrightness enabled; other GS backends retain the original draw");
        for (const auto &effect : g_effects)
            if (effect.enabled)
                SOTC_INFO(Hook, effect.setting << " enabled at 0x" << std::hex << effect.begin);
    }
}
