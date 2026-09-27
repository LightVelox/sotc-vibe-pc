#include "sotc/hle/libgcc.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <xmmintrin.h>

namespace sotc::hle
{
    namespace
    {
        constexpr uint32_t kFloatdisf = 0x00121D00u;
        constexpr uint32_t kFloatdisfCycles = 1416u;

        std::atomic<uint64_t> g_checked{0u};
        std::atomic<uint64_t> g_mismatches{0u};
        bool g_verify = false;

        float int64ToFloatTruncated(int64_t value)
        {
            const unsigned int csr = _mm_getcsr();
            _mm_setcsr(csr | 0x6000u);
            const float result = _mm_cvtss_f32(_mm_cvtsi64_ss(_mm_setzero_ps(), value));
            _mm_setcsr(csr);
            return result;
        }

        void verifyFloatdisf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, float native)
        {
            const GuestFunction original = FunctionHooks::instance().original(kFloatdisf);
            if (!original)
                return;
            const uint64_t input = GPR_U64(ctx, 4);
            original(rdram, ctx, runtime);
            uint32_t expected = 0u;
            uint32_t actual = 0u;
            std::memcpy(&expected, &ctx->f[0], sizeof(expected));
            std::memcpy(&actual, &native, sizeof(actual));
            const uint64_t checked = ++g_checked;
            const uint32_t distance = expected > actual ? expected - actual : actual - expected;
            if (expected != actual && distance <= 4u && g_mismatches.fetch_add(1u) < 32u)
                SOTC_ERROR(Hook, "__floatdisf(0x" << std::hex << input << ") native 0x" << actual << " guest 0x" << expected);
            if ((checked & (checked - 1u)) == 0u && checked >= 1024u)
                SOTC_INFO(Hook, "__floatdisf verified " << checked << " calls, " << g_mismatches.load() << " mismatches");
        }

        void floatdisf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const float result = int64ToFloatTruncated(static_cast<int64_t>(GPR_U64(ctx, 4)));
            if (g_verify)
            {
                verifyFloatdisf(rdram, ctx, runtime, result);
                return;
            }
            ctx->f[0] = result;
            ctx->pc = GPR_U32(ctx, 31);
            runtime->eeScheduler().accountCycles(kFloatdisfCycles);
        }
    }

    void installLibgcc(PS2Runtime &runtime)
    {
        (void)runtime;
        const char *verify = std::getenv("SOTC_VERIFY_LIBGCC");
        g_verify = verify && *verify && *verify != '0';
        if (!FunctionHooks::instance().replace(kFloatdisf, "__floatdisf", &floatdisf, ReplacementStatus::Native,
                                               "int64 to float rounded toward zero, same result as the soft-double libgcc routine"))
            SOTC_ERROR(Hook, "failed to bind __floatdisf");
    }
}
