#pragma once

#include <string>
#include <vector>

namespace sotc::watchdog
{
    void startFromEnvironment();
    void stop();
    std::vector<std::string> captureThreadStack(const wchar_t *threadDescription, int maxFrames);
}
