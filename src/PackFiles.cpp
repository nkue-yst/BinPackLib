#include "BinPackLib/PackFiles.h"
#include "BinPackLib/Types.h"

#include "Compression.h"
#include "Internal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace BinPackLib
{

namespace
{

struct PendingFile
{
    std::filesystem::path inputPath;
    std::string virtualPath;
    uint64_t size;
    uint64_t storedSize;
    uint32_t flags;
    uint32_t crc32;
    uint64_t dataOffset;
    std::vector<uint8_t> storedData;
};

uint64_t AlignUp(uint64_t value, uint32_t alignment)
{
    if (alignment <= 1)
    {
        return value;
    }

    const auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }

    return value + (alignment - remainder);
}

std::string ToVirtualPathString(const std::filesystem::path& path)
{
    auto result = path.generic_string();
    while (result.starts_with("./"))
    {
        result.erase(0, 2);
    }

    return result;
}

std::string MakeVirtualPath(
    const std::filesystem::path& inputPath,
    const PackOptions& options
)
{
    auto virtualPath = std::filesystem::path();

    if (options.preserveDirectoryStructure == false)
    {
        virtualPath = inputPath.filename();
        return ToVirtualPathString(virtualPath);
    }

    if (options.baseDirectory.empty() == false)
    {
        auto basePath = std::filesystem::absolute(std::filesystem::path(options.baseDirectory));
        auto absoluteInput = std::filesystem::absolute(inputPath);
        virtualPath = absoluteInput.lexically_relative(basePath);
        const auto relativeText = virtualPath.generic_string();
        if (virtualPath.empty() || relativeText.starts_with(".."))
        {
            virtualPath = inputPath.filename();
        }
    }
    else if (inputPath.is_absolute())
    {
        virtualPath = inputPath.filename();
    }
    else
    {
        virtualPath = inputPath.lexically_normal();
    }

    return ToVirtualPathString(virtualPath);
}

bool WritePadding(std::ofstream& output, uint64_t currentOffset, uint64_t alignedOffset)
{
    if (alignedOffset < currentOffset)
    {
        return false;
    }

    auto paddingSize = alignedOffset - currentOffset;
    const auto zeros = std::array<char, 256>{};
    while (paddingSize > 0)
    {
        const auto writeSize = std::min<uint64_t>(paddingSize, zeros.size());
        output.write(zeros.data(), static_cast<std::streamsize>(writeSize));
        if (output.good() == false)
        {
            return false;
        }

        paddingSize -= writeSize;
    }

    return true;
}

bool WriteEntriesAndStrings(
    std::ofstream& output,
    const Internal::PackFileHeader& header,
    const std::vector<Internal::PackFileEntry>& entries,
    const std::string& stringTable
)
{
    output.seekp(static_cast<std::streamoff>(header.entryTableOffset), std::ios::beg);
    output.write(
        reinterpret_cast<const char*>(entries.data()),
        static_cast<std::streamsize>(entries.size() * sizeof(Internal::PackFileEntry))
    );
    output.write(stringTable.data(), static_cast<std::streamsize>(stringTable.size()));
    return output.good();
}

bool ReadAllBytes(const std::filesystem::path& path, std::vector<uint8_t>& data)
{
    data.clear();
    const auto size = std::filesystem::file_size(path);
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return false;
    }

    auto input = std::ifstream(path, std::ios::binary);
    if (input.is_open() == false)
    {
        return false;
    }

    data.resize(static_cast<size_t>(size));
    if (data.empty())
    {
        return true;
    }

    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return input.good();
}

uint32_t CalculateCrc32(const std::vector<uint8_t>& data)
{
    auto crc = Internal::BeginCrc32();
    if (data.empty() == false)
    {
        crc = Internal::UpdateCrc32(crc, data.data(), data.size());
    }

    return Internal::FinishCrc32(crc);
}

}  // unnamed namespace

PackOptions::PackOptions()
    : baseDirectory()
    , preserveDirectoryStructure(true)
    , overwrite(false)
    , alignment(16)
    , enableCompression(false)
{
}

PackResult::PackResult()
    : success(false)
    , errorMessage()
    , fileCount(0)
    , totalOriginalSize(0)
    , totalStoredSize(0)
{
}

bool PackFiles(
    const std::vector<std::string>& inputFilePaths,
    const std::string& outputPackFilePath
)
{
    const auto options = PackOptions();
    const auto result = PackFiles(inputFilePaths, outputPackFilePath, options);
    return result.success;
}

PackResult PackFiles(
    const std::vector<std::string>& inputFilePaths,
    const std::string& outputPackFilePath,
    const PackOptions& options
)
{
    auto result = PackResult();

#ifndef _WIN32
    if (options.enableCompression)
    {
        result.errorMessage = "Compression is not supported on this platform.";
        return result;
    }
#endif

    if (options.enableCompression && Internal::IsCompressionSupported() == false)
    {
        result.errorMessage = "Compression is not supported by this implementation.";
        return result;
    }

    if (inputFilePaths.empty())
    {
        result.errorMessage = "No input files were specified.";
        return result;
    }

    if (outputPackFilePath.empty())
    {
        result.errorMessage = "Output pack file path is empty.";
        return result;
    }

    const auto outputPath = std::filesystem::path(outputPackFilePath);
    if (std::filesystem::exists(outputPath) && options.overwrite == false)
    {
        result.errorMessage = "Output pack file already exists.";
        return result;
    }

    auto pendingFiles = std::vector<PendingFile>();
    auto virtualPaths = std::unordered_set<std::string>();

    for (const auto& inputFilePath : inputFilePaths)
    {
        const auto inputPath = std::filesystem::path(inputFilePath);
        if (std::filesystem::exists(inputPath) == false)
        {
            result.errorMessage = "Input file does not exist: " + inputFilePath;
            return result;
        }

        if (std::filesystem::is_regular_file(inputPath) == false)
        {
            result.errorMessage = "Input path is not a regular file: " + inputFilePath;
            return result;
        }

        const auto size = std::filesystem::file_size(inputPath);
        const auto virtualPath = MakeVirtualPath(inputPath, options);
        if (virtualPath.empty())
        {
            result.errorMessage = "Failed to create virtual path: " + inputFilePath;
            return result;
        }

        if (virtualPaths.insert(virtualPath).second == false)
        {
            result.errorMessage = "Duplicate virtual path: " + virtualPath;
            return result;
        }

        auto pendingFile = PendingFile();
        pendingFile.inputPath = inputPath;
        pendingFile.virtualPath = virtualPath;
        pendingFile.size = size;
        pendingFile.storedSize = size;
        pendingFile.flags = 0;
        pendingFile.crc32 = 0;
        pendingFile.dataOffset = 0;

        if (options.enableCompression)
        {
            auto originalData = std::vector<uint8_t>();
            if (ReadAllBytes(inputPath, originalData) == false)
            {
                result.errorMessage = "Failed to read input file for compression: " + inputFilePath;
                return result;
            }

            pendingFile.crc32 = CalculateCrc32(originalData);

            auto compressedData = std::vector<uint8_t>();
            if (Internal::CompressBuffer(originalData, compressedData) == false)
            {
                result.errorMessage = "Failed to compress input file: " + inputFilePath;
                return result;
            }

            if (compressedData.size() < originalData.size())
            {
                pendingFile.storedData = std::move(compressedData);
                pendingFile.storedSize = static_cast<uint64_t>(pendingFile.storedData.size());
                pendingFile.flags = ToUInt32(FileFlags::Compressed);
            }
        }

        pendingFiles.push_back(pendingFile);
    }

    auto stringTable = std::string();
    auto entries = std::vector<Internal::PackFileEntry>();
    entries.reserve(pendingFiles.size());

    auto currentOffset = uint64_t{sizeof(Internal::PackFileHeader)};
    const auto entryTableOffset = currentOffset;
    const auto entryTableSize = uint64_t{sizeof(Internal::PackFileEntry)} * pendingFiles.size();
    currentOffset += entryTableSize;

    const auto stringTableOffset = currentOffset;
    for (const auto& pendingFile : pendingFiles)
    {
        const auto pathOffset = stringTable.size();
        stringTable += pendingFile.virtualPath;

        auto entry = Internal::PackFileEntry();
        entry.pathOffset = static_cast<uint64_t>(pathOffset);
        entry.pathLength = static_cast<uint32_t>(pendingFile.virtualPath.size());
        entry.flags = pendingFile.flags;
        entry.dataOffset = 0;
        entry.originalSize = pendingFile.size;
        entry.storedSize = pendingFile.storedSize;
        entry.crc32 = pendingFile.crc32;
        entry.reserved = 0;
        entries.push_back(entry);
    }

    const auto stringTableSize = static_cast<uint64_t>(stringTable.size());
    currentOffset += stringTableSize;

    auto dataOffset = AlignUp(currentOffset, options.alignment);
    currentOffset = dataOffset;

    for (auto index = size_t{0}; index < pendingFiles.size(); ++index)
    {
        currentOffset = AlignUp(currentOffset, options.alignment);
        pendingFiles[index].dataOffset = currentOffset;
        entries[index].dataOffset = currentOffset;
        currentOffset += pendingFiles[index].storedSize;
        result.totalOriginalSize += pendingFiles[index].size;
        result.totalStoredSize += pendingFiles[index].storedSize;
    }

    auto header = Internal::PackFileHeader();
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, "BPACK01", 7);
    header.version = Internal::FormatVersion;
    header.headerSize = static_cast<uint32_t>(sizeof(Internal::PackFileHeader));
    header.fileCount = static_cast<uint64_t>(pendingFiles.size());
    header.entryTableOffset = entryTableOffset;
    header.entryTableSize = entryTableSize;
    header.stringTableOffset = stringTableOffset;
    header.stringTableSize = stringTableSize;
    header.dataOffset = dataOffset;
    header.packageSize = currentOffset;
    header.flags = 0;
    header.headerCrc32 = 0;

    if (outputPath.has_parent_path())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    auto output = std::ofstream(outputPath, std::ios::binary | std::ios::trunc);
    if (output.is_open() == false)
    {
        result.errorMessage = "Failed to open output pack file.";
        return result;
    }

    output.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    if (WriteEntriesAndStrings(output, header, entries, stringTable) == false)
    {
        result.errorMessage = "Failed to write pack header.";
        return result;
    }

    auto writeOffset = header.stringTableOffset + header.stringTableSize;
    if (WritePadding(output, writeOffset, dataOffset) == false)
    {
        result.errorMessage = "Failed to write pack alignment padding.";
        return result;
    }

    auto buffer = std::vector<uint8_t>(Internal::CopyBufferSize);
    for (auto index = size_t{0}; index < pendingFiles.size(); ++index)
    {
        const auto alignedOffset = pendingFiles[index].dataOffset;
        const auto actualOffset = static_cast<uint64_t>(output.tellp());
        if (WritePadding(output, actualOffset, alignedOffset) == false)
        {
            result.errorMessage = "Failed to write file alignment padding.";
            return result;
        }

        if (pendingFiles[index].storedData.empty() == false)
        {
            output.write(
                reinterpret_cast<const char*>(pendingFiles[index].storedData.data()),
                static_cast<std::streamsize>(pendingFiles[index].storedData.size())
            );
            if (output.good() == false)
            {
                result.errorMessage = "Failed to write compressed file data.";
                return result;
            }
            entries[index].crc32 = pendingFiles[index].crc32;
            continue;
        }

        auto input = std::ifstream(pendingFiles[index].inputPath, std::ios::binary);
        if (input.is_open() == false)
        {
            result.errorMessage = "Failed to open input file.";
            return result;
        }

        auto crc = Internal::BeginCrc32();
        while (input.good())
        {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto readSize = input.gcount();
            if (readSize <= 0)
            {
                break;
            }

            output.write(reinterpret_cast<const char*>(buffer.data()), readSize);
            if (output.good() == false)
            {
                result.errorMessage = "Failed to write file data.";
                return result;
            }

            crc = Internal::UpdateCrc32(crc, buffer.data(), static_cast<size_t>(readSize));
        }

        if (input.bad())
        {
            result.errorMessage = "Failed to read input file.";
            return result;
        }

        entries[index].crc32 = Internal::FinishCrc32(crc);
    }

    header.packageSize = static_cast<uint64_t>(output.tellp());
    output.seekp(0, std::ios::beg);
    output.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    if (WriteEntriesAndStrings(output, header, entries, stringTable) == false)
    {
        result.errorMessage = "Failed to rewrite pack header.";
        return result;
    }

    output.close();
    if (output.good() == false)
    {
        result.errorMessage = "Failed to close output pack file.";
        return result;
    }

    result.success = true;
    result.fileCount = pendingFiles.size();
    return result;
}

}  // namespace BinPackLib
