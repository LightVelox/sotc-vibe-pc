#include "sotc/hle/sce_fileio.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_cd_image.h"
#include "runtime/ps2_memory.h"

#include <cstring>
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

        void sceGetstatHle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            returnTo(ctx);
            const std::string path = guestString(rdram, getRegU32(ctx, 4));
            ps2_syscalls::fioGetstat(rdram, ctx, runtime);
            SOTC_TRACE(File, "sceGetstat(\"" << path << "\") -> " << static_cast<int32_t>(getRegU32(ctx, 2)));
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
            {0x0010C2A8, "sceGetstat", &sceGetstatHle, "fio RPC 12 served from the runtime VFS (ISO/host)"},
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
