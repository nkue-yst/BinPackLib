#pragma once

#include <cstddef>
#include <cstdint>

namespace BinPackLib
{

class IReadStream
{
public:
    virtual ~IReadStream() = default;

    virtual uint64_t Size() const = 0;
    virtual uint64_t Tell() const = 0;
    virtual bool Seek(uint64_t position) = 0;
    virtual size_t Read(void* buffer, size_t size) = 0;
    virtual bool IsOpen() const = 0;
};


struct ReadStreamCallbacks
{
    ReadStreamCallbacks();

    void* userData;
    size_t (*read)(void* userData, void* buffer, size_t size);
    bool (*seek)(void* userData, uint64_t position);
    uint64_t (*tell)(void* userData);
    uint64_t (*size)(void* userData);
    bool (*isOpen)(void* userData);
};

ReadStreamCallbacks CreateReadStreamCallbacks(IReadStream* stream);

}  // namespace BinPackLib
