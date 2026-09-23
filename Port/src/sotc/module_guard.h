#pragma once

#include "ps2_runtime.h"

namespace sotc
{
    void installModuleGuards(PS2Runtime &runtime);
    void installEeFloatingPointMode(PS2Runtime &runtime, uint32_t bootEntry);
    void applyEeFloatingPointMode();
}
