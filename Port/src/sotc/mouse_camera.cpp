#include "sotc/mouse_camera.h"
#include "sotc/mouse_camera_math.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_input_options.h"
#include "runtime/ps2_memory.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kCameraUpdate = 0x014302F0u;
        constexpr uint32_t kPrepareCamera = 0x01431AB0u;
        constexpr uint32_t kCommitCameraView = 0x014377F8u;
        constexpr uint32_t kSelectCameraAngle = 0x014384D8u;
        constexpr uint32_t kFreeYawReturn = 0x01433048u;
        constexpr uint32_t kFreePitchReturn = 0x0143324Cu;
        constexpr uint32_t kGameDeltaTime = 0x01478FC0u;
        constexpr uint32_t kCameraCommon = 0x1370u;

        GuestFunction g_originalSelectAngle = nullptr;
        ps2_host_input::MouseCameraInput g_mouseInput;
        MouseCameraAngles g_targetAngles{};
        uint32_t g_camera = 0u;
        bool g_targetReady = false;
        bool g_anglesReady = false;
        bool g_freeCameraActive = false;

        float readFloat(const uint8_t *rdram, uint32_t address)
        {
            float value;
            std::memcpy(&value, rdram + (address & PS2_RAM_MASK), sizeof(value));
            return value;
        }

        void writeFloat(uint8_t *rdram, uint32_t address, float value)
        {
            std::memcpy(rdram + (address & PS2_RAM_MASK), &value, sizeof(value));
        }

        void prepareMouseCamera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t common = GPR_U32(ctx, 4);
            uint32_t mode;
            std::memcpy(&mode, rdram + ((g_camera + 0x1360u) & PS2_RAM_MASK), sizeof(mode));
            g_freeCameraActive = g_mouseInput.active && mode == 1u && common == g_camera + kCameraCommon;
            ps2_host_input::setMouseStickEnabled(!g_freeCameraActive);
            if (!g_freeCameraActive)
            {
                g_anglesReady = false;
                return;
            }
            writeFloat(rdram, common + 0xCu, 0.0f);
            writeFloat(rdram, common + 0x10u, 0.0f);
            if (!g_anglesReady)
            {
                const float x = readFloat(rdram, common + 0x140u);
                const float y = readFloat(rdram, common + 0x144u);
                const float z = readFloat(rdram, common + 0x148u);
                const float horizontal = std::hypot(x, z);
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::hypot(horizontal, y) > 0.0001f)
                {
                    g_targetAngles = {std::atan2(x, z), std::atan2(y, horizontal)};
                    g_anglesReady = true;
                }
            }
        }

        void commitMouseCamera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t common = GPR_U32(ctx, 5);
            if (!g_freeCameraActive || !g_targetReady || common != g_camera + kCameraCommon)
            {
                g_anglesReady = false;
                return;
            }
            const float x = std::sin(g_targetAngles.yaw) * std::cos(g_targetAngles.pitch);
            const float y = std::sin(g_targetAngles.pitch);
            const float z = std::cos(g_targetAngles.yaw) * std::cos(g_targetAngles.pitch);
            writeFloat(rdram, common + 0x140u, x);
            writeFloat(rdram, common + 0x144u, y);
            writeFloat(rdram, common + 0x148u, z);
            writeFloat(rdram, common + 0x14Cu, 0.0f);
            writeFloat(rdram, common + 0x150u, g_targetAngles.yaw);
            writeFloat(rdram, common + 0x154u, g_targetAngles.pitch);
            writeFloat(rdram, common + 0x1C4u, 0.0f);
            const float distance = readFloat(rdram, common + 0x158u);
            if (std::isfinite(distance) && distance > 0.0f)
            {
                writeFloat(rdram, common + 0x130u, readFloat(rdram, common + 0x120u) + x * distance);
                writeFloat(rdram, common + 0x134u, readFloat(rdram, common + 0x124u) + y * distance);
                writeFloat(rdram, common + 0x138u, readFloat(rdram, common + 0x128u) + z * distance);
            }
        }

        void selectMouseAngle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t address = GPR_U32(ctx, 4);
            const uint32_t returnPc = GPR_U32(ctx, 31);
            const bool yaw = returnPc == kFreeYawReturn && address == g_camera + kCameraCommon + 0xD0u;
            const bool pitch = returnPc == kFreePitchReturn && address == g_camera + kCameraCommon + 0xD4u;
            if (ctx->pc != kSelectCameraAngle || !g_freeCameraActive || (!yaw && !pitch))
            {
                g_originalSelectAngle(rdram, ctx, runtime);
                return;
            }
            const float current = ctx->f[12];
            if (yaw && !g_targetReady)
            {
                const float elapsed = readFloat(rdram, kGameDeltaTime);
                const float currentPitch = readFloat(rdram, address + 4u);
                if (!std::isfinite(current) || !std::isfinite(currentPitch) || !std::isfinite(elapsed) ||
                    !std::isfinite(g_mouseInput.x) || !std::isfinite(g_mouseInput.y))
                {
                    g_mouseInput.active = false;
                    g_freeCameraActive = false;
                    g_anglesReady = false;
                    ps2_host_input::setMouseStickEnabled(true);
                    g_originalSelectAngle(rdram, ctx, runtime);
                    return;
                }
                if (!g_anglesReady)
                    g_targetAngles = {current, currentPitch};
                if (elapsed > 0.0f)
                    g_targetAngles = rotateMouseCamera(g_targetAngles.yaw, g_targetAngles.pitch, -g_mouseInput.x, g_mouseInput.y);
                g_anglesReady = true;
                g_targetReady = true;
                g_mouseInput.x = 0.0f;
                g_mouseInput.y = 0.0f;
            }
            if (!g_targetReady)
            {
                g_originalSelectAngle(rdram, ctx, runtime);
                return;
            }
            const float target = yaw ? g_targetAngles.yaw : g_targetAngles.pitch;
            std::memcpy(rdram + (address & PS2_RAM_MASK), &target, sizeof(target));
            const float difference = yaw ? std::remainder(target - current, 2.0f * std::numbers::pi_v<float>)
                                         : target - current;
            SET_GPR_U32(ctx, 2, std::abs(difference) >= 0.0001f ? 1u : 0u);
            ctx->pc = returnPc;
        }
    }

    void installMouseCamera(PS2Runtime &runtime)
    {
        if (const char *setting = std::getenv("SOTC_NATIVE_MOUSE_CAMERA"); setting && std::string_view(setting) == "0")
            return;
        g_originalSelectAngle = runtime.lookupFunction(kSelectCameraAngle);
        if (!g_originalSelectAngle)
        {
            SOTC_ERROR(Hook, "cannot install mouse camera: angle selection function missing");
            return;
        }
        if (!FunctionHooks::instance().observeEntry(kCameraUpdate, "camera_mouse_input",
                                                    [](uint8_t *, R5900Context *ctx, PS2Runtime *) {
                                                        const uint32_t camera = GPR_U32(ctx, 4);
                                                        if (camera != g_camera)
                                                            g_anglesReady = false;
                                                        g_camera = camera;
                                                        g_mouseInput = ps2_host_input::consumeMouseCameraInput();
                                                        g_targetReady = false;
                                                        g_freeCameraActive = false;
                                                        if (!g_mouseInput.active)
                                                        {
                                                            g_anglesReady = false;
                                                            ps2_host_input::setMouseStickEnabled(true);
                                                        }
                                                    }))
        {
            SOTC_ERROR(Hook, "cannot install mouse camera input capture");
            return;
        }
        if (!FunctionHooks::instance().observeEntry(kPrepareCamera, "camera_mouse_prepare", &prepareMouseCamera) ||
            !FunctionHooks::instance().observeEntry(kCommitCameraView, "camera_mouse_view", &commitMouseCamera))
        {
            SOTC_ERROR(Hook, "cannot install mouse camera view control");
            return;
        }
        if (!FunctionHooks::instance().replace(kSelectCameraAngle, "camera_mouse_angles", &selectMouseAngle,
                                               ReplacementStatus::Native, "mouse view rotation at free-camera angle selection"))
            SOTC_ERROR(Hook, "cannot install mouse camera angle selection");
    }
}
