#pragma once

#include <cstddef>
#include <cstdint>

namespace BinPackLib::Internal
{

constexpr auto MagicSize = 8;
constexpr auto FormatVersion = uint32_t{1};
constexpr auto CopyBufferSize = size_t{1024 * 1024};

#pragma pack(push, 1)
struct PackFileHeader
{
    char magic[MagicSize];
    uint32_t version;
    uint32_t headerSize;
    uint64_t fileCount;
    uint64_t entryTableOffset;
    uint64_t entryTableSize;
    uint64_t stringTableOffset;
    uint64_t stringTableSize;
    uint64_t dataOffset;
    uint64_t packageSize;
    uint32_t flags;
    uint32_t headerCrc32;
};

struct PackFileEntry
{
    uint64_t pathOffset;
    uint32_t pathLength;
    uint32_t flags;
    uint64_t dataOffset;
    uint64_t originalSize;
    uint64_t storedSize;
    uint32_t crc32;
    uint32_t reserved;
};
#pragma pack(pop)

uint32_t BeginCrc32();
uint32_t UpdateCrc32(uint32_t crc, const uint8_t* data, size_t size);
uint32_t FinishCrc32(uint32_t crc);

}  // namespace BinPackLib::Internal
