#include "BinPackLib/Types.h"

namespace BinPackLib
{

FileFlags operator|(FileFlags left, FileFlags right)
{
    return static_cast<FileFlags>(ToUInt32(left) | ToUInt32(right));
}

FileFlags operator&(FileFlags left, FileFlags right)
{
    return static_cast<FileFlags>(ToUInt32(left) & ToUInt32(right));
}

FileFlags& operator|=(FileFlags& left, FileFlags right)
{
    left = left | right;
    return left;
}

bool HasFlag(uint32_t flags, FileFlags flag)
{
    return (flags & ToUInt32(flag)) != 0;
}

uint32_t ToUInt32(FileFlags flags)
{
    return static_cast<uint32_t>(flags);
}

FileInfo::FileInfo()
    : path()
    , originalSize(0)
    , storedSize(0)
    , flags(0)
    , crc32(0)
{
}

}  // namespace BinPackLib
