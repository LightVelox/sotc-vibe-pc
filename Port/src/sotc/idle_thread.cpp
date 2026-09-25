#include "sotc/idle_thread.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"

#include <cstdlib>
#include <cstring>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kIosRecvMsg = 0x001ABED0u;
        constexpr uint32_t kIdleLoopReturn = 0x001A808Cu;

        GuestFunction g_iosRecvMsg = nullptr;

        void iosRecvMsgIdleSkip(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (ctx->pc == kIosRecvMsg && getRegU32(ctx, 31) == kIdleLoopReturn)
            {
                runtime->eeScheduler().skipIdleCycles();
            }
            g_iosRecvMsg(rdram, ctx, runtime);
        }
    }

    void installIdleThreadSkip(PS2Runtime &runtime)
    {
        const char *setting = std::getenv("SOTC_IDLE_SKIP");
        if (setting && std::strcmp(setting, "0") == 0)
        {
            SOTC_INFO(Hook, "idle thread skip disabled (SOTC_IDLE_SKIP=0)");
            return;
        }
        g_iosRecvMsg = runtime.lookupFunction(kIosRecvMsg);
        if (!g_iosRecvMsg ||
            !FunctionHooks::instance().replace(kIosRecvMsg, "iosRecvMsg", &iosRecvMsgIdleSkip, ReplacementStatus::Observed,
                                               "idle thread sub_001A8068 polls it forever; that call site skips EE time to the next event"))
        {
            SOTC_ERROR(Hook, "failed to bind the idle thread skip");
        }
    }
}
