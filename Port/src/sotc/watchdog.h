#pragma once

class PS2Runtime;

#include <string>
#include <vector>

namespace sotc::watchdog
{
    void startFromEnvironment(PS2Runtime *runtime);
    void stop();
    std::vector<std::string> captureThreadStack(const wchar_t *threadDescription, int maxFrames);
}
