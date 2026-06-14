#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "BinPackLib/ReadStream.h"
#include "BinPackLib/Types.h"

namespace BinPackLib
{

class PackedFile
{
public:
    PackedFile();
    ~PackedFile();

    bool Open(const std::string& packFilePath);
    void Close();

    bool IsOpen() const;
    size_t GetFileCount() const;
    bool Exists(const std::string& virtualPath) const;
    FileInfo GetFileInfo(const std::string& virtualPath) const;
    FileInfo GetFileInfo(size_t index) const;
    std::vector<FileInfo> ListFiles() const;
    std::unique_ptr<IReadStream> OpenRead(const std::string& virtualPath) const;
    std::vector<uint8_t> ReadAll(const std::string& virtualPath) const;
    bool ExtractFile(const std::string& virtualPath, const std::string& outputFilePath) const;
    std::string ExtractToTempFile(const std::string& virtualPath) const;
    bool VerifyFile(const std::string& virtualPath) const;
    bool VerifyAll() const;

private:
    struct Entry
    {
        Entry();

        FileInfo fileInfo;
        uint64_t dataOffset;
    };

    std::string packFilePath_;
    uint64_t packageSize_;
    bool isOpen_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> pathToEntryIndex_;
};

}  // namespace BinPackLib
