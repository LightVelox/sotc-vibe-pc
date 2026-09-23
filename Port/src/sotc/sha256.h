#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sotc
{
    class Sha256
    {
    public:
        Sha256();
        void update(const void *data, size_t size);
        std::array<uint8_t, 32> finish();
        std::string finishHex();

        static std::string hex(const void *data, size_t size);

    private:
        void block(const uint8_t *chunk);

        std::array<uint32_t, 8> m_state;
        std::array<uint8_t, 64> m_buffer{};
        size_t m_bufferSize = 0;
        uint64_t m_totalBytes = 0;
    };
}
