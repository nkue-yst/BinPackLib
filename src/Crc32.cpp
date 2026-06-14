#include "Internal.h"

namespace BinPackLib::Internal
{

uint32_t BeginCrc32()
{
    return 0xFFFFFFFFU;
}

uint32_t UpdateCrc32(uint32_t crc, const uint8_t* data, size_t size)
{
    auto value = crc;
    for (auto index = size_t{0}; index < size; ++index)
    {
        value ^= data[index];
        for (auto bit = 0; bit < 8; ++bit)
        {
            const auto mask = uint32_t{0} - (value & 1U);
            value = (value >> 1) ^ (0xEDB88320U & mask);
        }
    }

    return value;
}

uint32_t FinishCrc32(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

}  // namespace BinPackLib::Internal
