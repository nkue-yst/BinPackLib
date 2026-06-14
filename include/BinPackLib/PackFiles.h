#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BinPackLib
{

struct PackOptions
{
    PackOptions();

    std::string baseDirectory;
    bool preserveDirectoryStructure;
    bool overwrite;
    uint32_t alignment;
    bool enableCompression;
};


struct PackResult
{
    PackResult();

    bool success;
    std::string errorMessage;
    size_t fileCount;
    uint64_t totalOriginalSize;
    uint64_t totalStoredSize;
};


bool PackFiles(const std::vector<std::string>& inputFilePaths, const std::string& outputPackFilePath);
PackResult PackFiles(const std::vector<std::string>& inputFilePaths, const std::string& outputPackFilePath, const PackOptions& options);

}  // namespace BinPackLib
