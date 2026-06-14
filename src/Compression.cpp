#include "Compression.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <compressapi.h>
#endif

#include <limits>

namespace BinPackLib::Internal
{

bool IsCompressionSupported()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool CompressBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    output.clear();

#ifdef _WIN32
    if (input.empty())
    {
        return true;
    }

    COMPRESSOR_HANDLE compressor = nullptr;
    if (CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor) == FALSE)
    {
        return false;
    }

    SIZE_T compressedSize = 0;
    auto ok = Compress(
        compressor,
        input.data(),
        input.size(),
        nullptr,
        0,
        &compressedSize
    );
    if (ok == FALSE && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        CloseCompressor(compressor);
        return false;
    }

    output.resize(static_cast<size_t>(compressedSize));
    ok = Compress(
        compressor,
        input.data(),
        input.size(),
        output.data(),
        output.size(),
        &compressedSize
    );
    CloseCompressor(compressor);
    if (ok == FALSE)
    {
        output.clear();
        return false;
    }

    output.resize(static_cast<size_t>(compressedSize));
    return true;
#else
    (void)input;
    return false;
#endif
}

bool DecompressBuffer(
    const std::vector<uint8_t>& input,
    uint64_t originalSize,
    std::vector<uint8_t>& output
)
{
    output.clear();
    if (originalSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return false;
    }

#ifdef _WIN32
    output.resize(static_cast<size_t>(originalSize));
    if (originalSize == 0)
    {
        return input.empty();
    }

    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor) == FALSE)
    {
        return false;
    }

    SIZE_T decompressedSize = 0;
    const auto ok = Decompress(
        decompressor,
        input.data(),
        input.size(),
        output.data(),
        output.size(),
        &decompressedSize
    );
    CloseDecompressor(decompressor);
    if (ok == FALSE)
    {
        output.clear();
        return false;
    }

    if (decompressedSize != static_cast<SIZE_T>(originalSize))
    {
        output.clear();
        return false;
    }

    return true;
#else
    (void)input;
    return false;
#endif
}

bool IsEncryptionSupported()
{
    return false;
}

bool EncryptBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    (void)input;
    output.clear();
    return false;
}

bool DecryptBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    (void)input;
    output.clear();
    return false;
}

}  // namespace BinPackLib::Internal
