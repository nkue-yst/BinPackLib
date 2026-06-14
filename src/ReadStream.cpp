#include "BinPackLib/ReadStream.h"

namespace BinPackLib
{

namespace
{

IReadStream* GetStream(void* userData)
{
    return static_cast<IReadStream*>(userData);
}

size_t ReadCallback(void* userData, void* buffer, size_t size)
{
    auto stream = GetStream(userData);
    if (stream == nullptr)
    {
        return 0;
    }

    return stream->Read(buffer, size);
}

bool SeekCallback(void* userData, uint64_t position)
{
    auto stream = GetStream(userData);
    if (stream == nullptr)
    {
        return false;
    }

    return stream->Seek(position);
}

uint64_t TellCallback(void* userData)
{
    auto stream = GetStream(userData);
    if (stream == nullptr)
    {
        return 0;
    }

    return stream->Tell();
}

uint64_t SizeCallback(void* userData)
{
    auto stream = GetStream(userData);
    if (stream == nullptr)
    {
        return 0;
    }

    return stream->Size();
}

bool IsOpenCallback(void* userData)
{
    auto stream = GetStream(userData);
    if (stream == nullptr)
    {
        return false;
    }

    return stream->IsOpen();
}

}  // unnamed namespace


ReadStreamCallbacks::ReadStreamCallbacks()
    : userData(nullptr)
    , read(nullptr)
    , seek(nullptr)
    , tell(nullptr)
    , size(nullptr)
    , isOpen(nullptr)
{
}

ReadStreamCallbacks CreateReadStreamCallbacks(IReadStream* stream)
{
    auto callbacks = ReadStreamCallbacks();
    callbacks.userData = stream;
    callbacks.read = ReadCallback;
    callbacks.seek = SeekCallback;
    callbacks.tell = TellCallback;
    callbacks.size = SizeCallback;
    callbacks.isOpen = IsOpenCallback;
    return callbacks;
}

}  // namespace BinPackLib
