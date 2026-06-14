#pragma once

#include <cstdint>
#include <vector>

namespace BinPackLib::Internal
{

bool IsCompressionSupported();
bool CompressBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
bool DecompressBuffer(const std::vector<uint8_t>& input, uint64_t originalSize, std::vector<uint8_t>& output);

bool IsEncryptionSupported();
bool EncryptBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
bool DecryptBuffer(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

}  // namepace BinPackLib::Internal
