#include "sotc/motion_blur.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"

#include <cstdlib>
#include <string_view>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kScreenBlur = 0x0117FDF8u;
        constexpr uint32_t kCameraBlurReturn = 0x01180F90u;
        GuestFunction g_original = nullptr;

        void screenBlur(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (ctx->pc == kScreenBlur && GPR_U32(ctx, 31) == kCameraBlurReturn)
            {
                ctx->pc = kCameraBlurReturn;
                return;
            }
            g_original(rdram, ctx, runtime);
        }
    }

    void installMotionBlurOption(PS2Runtime &runtime)
    {
        const char *setting = std::getenv("SOTC_DISABLE_MOTION_BLUR");
        if (!setting || std::string_view(setting) != "1")
            return;
        g_original = runtime.lookupFunction(kScreenBlur);
        if (!g_original)
            return;
        FunctionHooks::instance().replace(kScreenBlur, "screenBlur camera pass", screenBlur,
                                         ReplacementStatus::Native, "optional camera-motion blur suppression");
        SOTC_INFO(Hook, "camera-motion screen blur disabled; original camera history remains active");
    }
}
