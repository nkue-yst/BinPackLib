#pragma once

#include <cstdint>
#include <string>

namespace BinPackLib
{

enum class FileFlags : uint32_t
{
    None = 0,
    Compressed = 1 << 0,
    Encrypted = 1 << 1
};

FileFlags operator|(FileFlags left, FileFlags right);
FileFlags operator&(FileFlags left, FileFlags right);
FileFlags& operator|=(FileFlags& left, FileFlags right);
bool HasFlag(uint32_t flags, FileFlags flag);
uint32_t ToUInt32(FileFlags flags);

struct FileInfo
{
    FileInfo();

    std::string path;
    uint64_t originalSize;
    uint64_t storedSize;
    uint32_t flags;
    uint32_t crc32;
};

}  // namespace BinPackLib
