#include "sotc/hle/sce_fileio.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ps2_cd_image.h"
#include "runtime/ps2_memory.h"
#include "runtime/ee_scheduler.h"

#include <cstdlib>

#include <cstring>
#include <mutex>
#include <string>

namespace sotc::hle
{
    namespace
    {
        constexpr int32_t kEnoent = -2;
        constexpr int32_t kEio = -5;
        constexpr int32_t kEnodev = -19;
        constexpr int32_t kErofs = -30;

        std::string guestString(uint8_t *rdram, uint32_t address)
        {
            std::string out;
            for (uint32_t i = 0; i < 1024; ++i)
            {
                const char c = static_cast<char>(rdram[(address + i) & PS2_RAM_MASK]);
                if (c == '\0')
                {
                    break;
                }
                out.push_back(c);
            }
            return out;
        }

        void returnTo(R5900Context *ctx)
        {
            ctx->pc = getRegU32(ctx, 31);
        }

        template <auto Handler>
        void forward(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            returnTo(ctx);
            Handler(rdram, ctx, runtime);
        }

        void logged(const char *name, uint8_t *rdram, R5900Context *ctx, int32_t result)
        {
            SOTC_INFO(File, name << "(\"" << guestString(rdram, getRegU32(ctx, 4)) << "\") -> " << result);
            setReturnS32(ctx, result);
        }

        uint64_t dvdBytesPerSecond()
        {
            static const uint64_t rate = []
            {
                const char *value = std::getenv("SOTC_DVD_RATE");
                return value ? std::strtoull(value, nullptr, 10) : 3500000ull;
            }();
            return rate;
        }

        void sceReadTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t resumePc = getRegU32(ctx, 31);
            returnTo(ctx);
            const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
            ps2_syscalls::fioRead(rdram, ctx, runtime);
            const int32_t result = static_cast<int32_t>(getRegU32(ctx, 2));
            const uint64_t rate = dvdBytesPerSecond();
            if (result <= 0 || rate == 0)
            {
                return;
            }
            constexpr uint64_t kFieldsPerSecond = 50;
            const uint64_t ticks = 1 + (static_cast<uint64_t>(result) * kFieldsPerSecond + rate - 1) / rate;
            SOTC_TRACE(File, "sceRead(fd=" << fd << ") -> " << result << " bytes, blocking " << ticks << " vsync(s)");
            EeScheduler &scheduler = runtime->eeScheduler();
            scheduler.waitVSync(scheduler.currentVSyncTick() + ticks - 1, result, [resumePc](R5900Context &context)
                                { context.pc = resumePc; });
        }

        uint64_t envFields(const char *name, uint64_t fallback)
        {
            const char *value = std::getenv(name);
            return value ? std::strtoull(value, nullptr, 10) : fallback;
        }

        void blockCallerFields(R5900Context *ctx, PS2Runtime *runtime, uint32_t resumePc, uint64_t fields)
        {
            const int32_t result = static_cast<int32_t>(getRegU32(ctx, 2));
            if (result < 0 || fields == 0)
            {
                return;
            }
            EeScheduler &scheduler = runtime->eeScheduler();
            scheduler.waitVSync(scheduler.currentVSyncTick() + fields - 1, result, [resumePc](R5900Context &context)
                                { context.pc = resumePc; });
        }

        void blockCallerOneField(R5900Context *ctx, PS2Runtime *runtime, uint32_t resumePc)
        {
            blockCallerFields(ctx, runtime, resumePc, 1);
        }

        bool isDiscPath(const std::string &path)
        {
            return path.rfind("cdrom", 0) == 0;
        }

        std::mutex g_ttyMutex;
        std::string g_ttyLine[3];

        void writeTty(int fd, const uint8_t *data, size_t size)
        {
            std::lock_guard<std::mutex> lock(g_ttyMutex);
            std::string &line = g_ttyLine[fd];
            for (size_t i = 0; i < size; ++i)
            {
                const char c = static_cast<char>(data[i]);
                if (c == '\n')
                {
                    SOTC_INFO(Game, line);
                    line.clear();
                }
                else if (c != '\r')
                {
                    line.push_back(c);
                }
            }
        }

        template <auto Fallback>
        void writeWithTty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            returnTo(ctx);
            const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
            if (fd == 1 || fd == 2)
            {
                const uint32_t address = getRegU32(ctx, 5);
                const uint32_t size = std::min<uint32_t>(getRegU32(ctx, 6), 64u * 1024u);
                std::string buffer(size, '\0');
                for (uint32_t i = 0; i < size; ++i)
                {
                    buffer[i] = static_cast<char>(rdram[(address + i) & PS2_RAM_MASK]);
                }
                writeTty(fd, reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size());
                setReturnS32(ctx, static_cast<int32_t>(size));
                return;
            }
            Fallback(rdram, ctx, runtime);
        }

        void sceGetstatHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t resumePc = getRegU32(ctx, 31);
            returnTo(ctx);
            const std::string path = guestString(rdram, getRegU32(ctx, 4));
            ps2_syscalls::fioGetstat(rdram, ctx, runtime);
            SOTC_TRACE(File, "sceGetstat(\"" << path << "\") -> " << static_cast<int32_t>(getRegU32(ctx, 2)));
            if (isDiscPath(path))
            {
                blockCallerOneField(ctx, runtime, resumePc);
            }
        }

        void sceOpenTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t resumePc = getRegU32(ctx, 31);
            returnTo(ctx);
            const std::string path = guestString(rdram, getRegU32(ctx, 4));
            ps2_stubs::sceOpen(rdram, ctx, runtime);
            SOTC_TRACE(File, "sceOpen(\"" << path << "\") -> " << static_cast<int32_t>(getRegU32(ctx, 2)));
            if (isDiscPath(path))
            {
                blockCallerOneField(ctx, runtime, resumePc);
            }
        }

        void sceCdSearchFileTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t resumePc = getRegU32(ctx, 31);
            returnTo(ctx);
            ps2_stubs::sceCdSearchFile(rdram, ctx, runtime);
            static const uint64_t searchFields = envFields("SOTC_CD_SEARCH_FIELDS", 3);
            blockCallerFields(ctx, runtime, resumePc, searchFields);
        }

        void sceChstatHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            logged("sceChstat", rdram, ctx, kErofs);
        }

        void sceRenameHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            logged("sceRename", rdram, ctx, kErofs);
        }

        void sceDopenHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            logged("sceDopen", rdram, ctx, kEnoent);
        }

        void sceDreadHle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceDread(" << static_cast<int32_t>(getRegU32(ctx, 4)) << ") -> " << kEio);
            setReturnS32(ctx, kEio);
        }

        void sceDcloseHle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceDclose(" << static_cast<int32_t>(getRegU32(ctx, 4)) << ") -> " << kEio);
            setReturnS32(ctx, kEio);
        }

        void sceUnsupportedDevice(const char *name, uint8_t *rdram, R5900Context *ctx)
        {
            returnTo(ctx);
            logged(name, rdram, ctx, kEnodev);
        }

        void sceIoctl2Hle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceIoctl2(fd=" << static_cast<int32_t>(getRegU32(ctx, 4)) << ", cmd=0x" << std::hex << getRegU32(ctx, 5)
                                             << ") -> " << std::dec << kEnodev);
            setReturnS32(ctx, kEnodev);
        }

        void sceDevctlHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceDevctl(\"" << guestString(rdram, getRegU32(ctx, 4)) << "\", cmd=0x" << std::hex << getRegU32(ctx, 5)
                                            << ") -> " << std::dec << kEnodev);
            setReturnS32(ctx, kEnodev);
        }

        void sceMountHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceMount", rdram, ctx); }
        void sceUmountHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceUmount", rdram, ctx); }
        void sceFormatHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceFormat", rdram, ctx); }
        void sceSyncHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceSync", rdram, ctx); }
        void sceSymlinkHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceSymlink", rdram, ctx); }
        void sceReadlinkHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceReadlink", rdram, ctx); }

        void sceAddDrvHle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceAddDrv(0x" << std::hex << getRegU32(ctx, 4) << ") -> " << std::dec << kEio);
            setReturnS32(ctx, kEio);
        }

        void sceDelDrvHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { sceUnsupportedDevice("sceDelDrv", rdram, ctx); }

        void sceLseek64Hle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_INFO(File, "sceLseek64(fd=" << static_cast<int32_t>(getRegU32(ctx, 4)) << ") -> " << kEio);
            setReturnS32(ctx, kEio);
        }

        void sceFsSetIopBufHle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_TRACE(File, "sceFsSetIopBuf(0x" << std::hex << getRegU32(ctx, 4) << ", 0x" << getRegU32(ctx, 5) << ")");
            setReturnS32(ctx, 0);
        }

        void sceFsSetIopPrioHle(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            returnTo(ctx);
            SOTC_TRACE(File, "sceFsSetIopPrio(" << getRegU32(ctx, 4) << ")");
            setReturnS32(ctx, 0);
        }

        struct Binding
        {
            uint32_t address;
            const char *name;
            GuestFunction function;
            const char *reason;
        };

        const Binding kBindings[] = {
            {0x0010A6C0, "sceOpen", &sceOpenTimed, "fio open via runtime VFS; disc opens block the caller for one field (IOP RPC + seek latency)"},
            {0x00116E88, "sceCdSearchFile", &sceCdSearchFileTimed, "runtime ISO lookup; caller blocks for a modelled seek + directory read (SOTC_CD_SEARCH_FIELDS, default 3)"},
            {0x00111798, "write", &writeWithTty<ps2_syscalls::fioWrite>, "fd 1/2 go to the [GAME] TTY log, others to the runtime VFS"},
            {0x0010AF78, "sceWrite", &writeWithTty<ps2_syscalls::fioWrite>, "fd 1/2 go to the [GAME] TTY log, others to the runtime VFS"},
            {0x0010AD08, "sceRead", &sceReadTimed, "fio read via runtime VFS; caller blocks for modelled DVD transfer time (SOTC_DVD_RATE)"},
            {0x0010C2A8, "sceGetstat", &sceGetstatHle, "fio RPC 12 served from the runtime VFS (ISO/host); disc paths block one field"},
            {0x0010B978, "sceMkdir", &forward<ps2_syscalls::fioMkdir>, "fio via runtime VFS"},
            {0x0010BB30, "sceRmdir", &forward<ps2_syscalls::fioRmdir>, "fio via runtime VFS"},
            {0x0010B958, "sceRemove", &forward<ps2_syscalls::fioRemove>, "fio via runtime VFS"},
            {0x0010C8B8, "sceChdir", &forward<ps2_syscalls::fioChdir>, "fio via runtime VFS"},
            {0x0010C460, "sceChstat", &sceChstatHle, "read-only devices only; returns -EROFS"},
            {0x0010C6B0, "sceRename", &sceRenameHle, "read-only devices only; returns -EROFS"},
            {0x0010BF20, "sceDopen", &sceDopenHle, "directory listing not implemented; returns -ENOENT"},
            {0x0010C150, "sceDread", &sceDreadHle, "directory listing not implemented; returns -EIO"},
            {0x0010BFE8, "sceDclose", &sceDcloseHle, "directory listing not implemented; returns -EIO"},
            {0x0010B5C0, "sceIoctl2", &sceIoctl2Hle, "no device ioctls; returns -ENODEV"},
            {0x0010CF68, "sceDevctl", &sceDevctlHle, "no device controls; returns -ENODEV"},
            {0x0010CA88, "sceMount", &sceMountHle, "returns -ENODEV"},
            {0x0010CD08, "sceUmount", &sceUmountHle, "returns -ENODEV"},
            {0x0010BB50, "sceFormat", &sceFormatHle, "returns -ENODEV"},
            {0x0010C8D8, "sceSync", &sceSyncHle, "returns -ENODEV"},
            {0x0010D210, "sceSymlink", &sceSymlinkHle, "returns -ENODEV"},
            {0x0010D408, "sceReadlink", &sceReadlinkHle, "returns -ENODEV"},
            {0x0010BDD0, "sceAddDrv", &sceAddDrvHle, "IOP device drivers cannot be added; returns -EIO"},
            {0x0010BF00, "sceDelDrv", &sceDelDrvHle, "returns -ENODEV"},
            {0x0010CD28, "sceLseek64", &sceLseek64Hle, "returns -EIO"},
            {0x0010A490, "sceFsSetIopBuf", &sceFsSetIopBufHle, "IOP-side buffering is not modelled"},
            {0x0010A5D0, "sceFsSetIopPrio", &sceFsSetIopPrioHle, "IOP thread priority is not modelled"},
        };
    }

    void installSceFileIo(PS2Runtime &runtime)
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
