#include "sotc/hle/sce_cdvd.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"

#include <cmath>
#include <cstdlib>
#include <mutex>

namespace sotc::hle
{
    namespace
    {
        constexpr uint32_t kSectorSize = 2048;

        struct DriveModel
        {
            std::mutex mutex;
            uint64_t busyUntilCycle = 0;
            bool pending = false;
            uint32_t nextSequentialLsn = 0xFFFFFFFFu;
            uint64_t reads = 0;
        };

        DriveModel g_drive;

        double envDouble(const char *name, double fallback)
        {
            const char *value = std::getenv(name);
            return value ? std::atof(value) : fallback;
        }

        double bytesPerSecond()
        {
            static const double rate = envDouble("SOTC_DVD_RATE", 3500000.0);
            return rate;
        }

        uint64_t secondsToCycles(double seconds)
        {
            return static_cast<uint64_t>(std::llround(seconds * static_cast<double>(EeScheduler::kEeClockHz)));
        }

        uint64_t seekCycles()
        {
            static const uint64_t cycles = secondsToCycles(envDouble("SOTC_DVD_SEEK_MS", 20.0) / 1000.0);
            return cycles;
        }

        void returnTo(R5900Context *ctx)
        {
            ctx->pc = getRegU32(ctx, 31);
        }

        template <auto Original>
        void sceCdReadTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            returnTo(ctx);
            const uint32_t lsn = getRegU32(ctx, 4);
            const uint32_t sectors = getRegU32(ctx, 5);
            const uint32_t buffer = getRegU32(ctx, 6);
            Original(rdram, ctx, runtime);
            const int32_t result = static_cast<int32_t>(getRegU32(ctx, 2));
            if (result == 0 || bytesPerSecond() <= 0.0)
            {
                return;
            }
            const uint64_t now = runtime->eeScheduler().currentCycle();
            std::lock_guard<std::mutex> lock(g_drive.mutex);
            uint64_t start = std::max(now, g_drive.busyUntilCycle);
            if (lsn != g_drive.nextSequentialLsn)
            {
                start += seekCycles();
            }
            g_drive.busyUntilCycle = start + secondsToCycles(static_cast<double>(sectors) * kSectorSize / bytesPerSecond());
            g_drive.pending = true;
            g_drive.nextSequentialLsn = lsn + sectors;
            ++g_drive.reads;
            SOTC_TRACE(File, "sceCdRead(lsn=0x" << std::hex << lsn << ", sectors=0x" << sectors << ", buf=0x" << buffer << std::dec << ") completes at cycle "
                                                << g_drive.busyUntilCycle << " (now " << now << ")");
        }

        template <auto Original>
        void sceCdSyncTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t resumePc = getRegU32(ctx, 31);
            returnTo(ctx);
            const uint32_t mode = getRegU32(ctx, 4);
            Original(rdram, ctx, runtime);
            EeScheduler &scheduler = runtime->eeScheduler();
            const uint64_t now = scheduler.currentCycle();
            uint64_t target = 0;
            {
                std::lock_guard<std::mutex> lock(g_drive.mutex);
                if (!g_drive.pending)
                {
                    setReturnS32(ctx, 0);
                    return;
                }
                const bool busy = g_drive.busyUntilCycle > now;
                if (mode != 0 || !busy)
                {
                    if (!busy)
                    {
                        g_drive.pending = false;
                    }
                    setReturnS32(ctx, busy ? 1 : 0);
                    return;
                }
                g_drive.pending = false;
                target = g_drive.busyUntilCycle;
            }
            scheduler.waitUntilCycle(target, 0, [resumePc](R5900Context &context)
                                     { context.pc = resumePc; });
        }

        struct Binding
        {
            uint32_t address;
            const char *name;
            GuestFunction function;
            const char *reason;
        };

        const Binding kBindings[] = {
            {0x001DB688, "sceCdRead", &sceCdReadTimed<ps2_stubs::sceCdRead>,
             "runtime ISO read; drive busy for modelled seek + transfer time in EE cycles (SOTC_DVD_RATE, SOTC_DVD_SEEK_MS)"},
            {0x001DB868, "sceCdReadIOPm", &sceCdReadTimed<ps2_stubs::sceCdReadIOPm>,
             "runtime ISO read into IOP RAM; same drive model as sceCdRead"},
            {0x00117098, "sceCdSync", &sceCdSyncTimed<ps2_stubs::sceCdSync>,
             "mode 0 blocks the caller until the EE cycle the modelled read completes at; mode 1 polls"},
            {0x00117138, "sceCdSyncS", &sceCdSyncTimed<ps2_stubs::sceCdSyncS>,
             "same drive model as sceCdSync"},
        };
    }

    void installSceCdvd(PS2Runtime &runtime)
    {
        (void)runtime;
        for (const auto &binding : kBindings)
        {
            if (!FunctionHooks::instance().replace(binding.address, binding.name, binding.function, ReplacementStatus::Hle, binding.reason))
            {
                SOTC_ERROR(Hook, "failed to bind " << binding.name);
            }
        }
    }
}
