#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sotc
{
    struct MouseCameraAngles
    {
        float yaw;
        float pitch;
    };

    inline MouseCameraAngles rotateMouseCamera(float yaw, float pitch, float horizontal, float vertical)
    {
        constexpr float radiansPerPixel = 0.0025f;
        constexpr float pitchLimit = 85.0f * std::numbers::pi_v<float> / 180.0f;
        if (horizontal == 0.0f && vertical == 0.0f)
            return {yaw, pitch};
        return {static_cast<float>(std::remainder(yaw + horizontal * static_cast<double>(radiansPerPixel),
                                                 2.0 * std::numbers::pi)),
                std::clamp(pitch + vertical * radiansPerPixel, -pitchLimit, pitchLimit)};
    }
}
