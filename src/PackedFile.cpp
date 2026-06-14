#include "BinPackLib/PackedFile.h"

#include "Compression.h"
#include "Internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

namespace BinPackLib
{

namespace
{

std::string NormalizeVirtualPath(const std::string& path)
{
    auto result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    while (result.starts_with("./"))
    {
        result.erase(0, 2);
    }

    return result;
}

bool IsRangeValid(uint64_t offset, uint64_t size, uint64_t packageSize)
{
    if (offset > packageSize)
    {
        return false;
    }

    if (size > packageSize - offset)
    {
        return false;
    }

    return true;
}

bool CanConvertToSizeT(uint64_t value)
{
    return value <= static_cast<uint64_t>(std::numeric_limits<size_t>::max());
}

bool CanConvertToStreamSize(uint64_t value)
{
    return value <= static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max());
}

std::string MakeTempFilePath(const std::string& virtualPath)
{
    auto sanitizedName = NormalizeVirtualPath(virtualPath);
    const auto slash = sanitizedName.find_last_of('/');
    if (slash != std::string::npos)
    {
        sanitizedName = sanitizedName.substr(slash + 1);
    }

    if (sanitizedName.empty())
    {
        sanitizedName = "extract.bin";
    }

    auto randomDevice = std::random_device();
    const auto seed = static_cast<uint64_t>(randomDevice());
    const auto ticks = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );

    const auto fileName = "BinPackLib_" + std::to_string(ticks) + "_" + std::to_string(seed) + "_" + sanitizedName;
    return (std::filesystem::temp_directory_path() / fileName).string();
}

class PackedReadStream final : public IReadStream
{
public:
    PackedReadStream(const std::string& packFilePath, uint64_t offset, uint64_t size)
        : file_()
        , baseOffset_(offset)
        , size_(size)
        , position_(0)
    {
        this->file_.open(packFilePath, std::ios::binary);
    }

    uint64_t Size() const override
    {
        return this->size_;
    }

    uint64_t Tell() const override
    {
        return this->position_;
    }

    bool Seek(uint64_t position) override
    {
        if (position > this->size_)
        {
            return false;
        }

        this->position_ = position;
        return true;
    }

    size_t Read(void* buffer, size_t size) override
    {
        if (this->IsOpen() == false)
        {
            return 0;
        }

        if (buffer == nullptr)
        {
            return 0;
        }

        if (this->position_ >= this->size_)
        {
            return 0;
        }

        const auto remaining = this->size_ - this->position_;
        const auto readSize = std::min<uint64_t>(static_cast<uint64_t>(size), remaining);
        if (readSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            return 0;
        }

        this->file_.clear();
        this->file_.seekg(static_cast<std::streamoff>(this->baseOffset_ + this->position_), std::ios::beg);
        if (this->file_.good() == false)
        {
            return 0;
        }

        this->file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(readSize));
        const auto actualRead = this->file_.gcount();
        if (actualRead <= 0)
        {
            return 0;
        }

        this->position_ += static_cast<uint64_t>(actualRead);
        return static_cast<size_t>(actualRead);
    }

    bool IsOpen() const override
    {
        return this->file_.is_open();
    }

private:
    std::ifstream file_;
    uint64_t baseOffset_;
    uint64_t size_;
    uint64_t position_;
};


class MemoryReadStream final : public IReadStream
{
public:
    explicit MemoryReadStream(std::vector<uint8_t> data)
        : data_(std::move(data))
        , position_(0)
        , isOpen_(true)
    {
    }

    uint64_t Size() const override
    {
        return static_cast<uint64_t>(this->data_.size());
    }

    uint64_t Tell() const override
    {
        return this->position_;
    }

    bool Seek(uint64_t position) override
    {
        if (position > this->Size())
        {
            return false;
        }

        this->position_ = position;
        return true;
    }

    size_t Read(void* buffer, size_t size) override
    {
        if (this->IsOpen() == false)
        {
            return 0;
        }

        if (buffer == nullptr)
        {
            return 0;
        }

        if (this->position_ >= this->Size())
        {
            return 0;
        }

        const auto remaining = this->Size() - this->position_;
        const auto readSize = std::min<uint64_t>(static_cast<uint64_t>(size), remaining);
        std::memcpy(
            buffer,
            this->data_.data() + static_cast<size_t>(this->position_),
            static_cast<size_t>(readSize)
        );
        this->position_ += readSize;
        return static_cast<size_t>(readSize);
    }

    bool IsOpen() const override
    {
        return this->isOpen_;
    }

private:
    std::vector<uint8_t> data_;
    uint64_t position_;
    bool isOpen_;
};

}  // unnamed namespace


PackedFile::Entry::Entry()
    : fileInfo()
    , dataOffset(0)
{
}

PackedFile::PackedFile()
    : packFilePath_()
    , packageSize_(0)
    , isOpen_(false)
    , entries_()
    , pathToEntryIndex_()
{
}

PackedFile::~PackedFile() = default;

bool PackedFile::Open(const std::string& packFilePath)
{
    this->Close();

    auto input = std::ifstream(packFilePath, std::ios::binary);
    if (input.is_open() == false)
    {
        return false;
    }

    const auto packageSize = std::filesystem::file_size(std::filesystem::path(packFilePath));
    if (packageSize < sizeof(Internal::PackFileHeader))
    {
        return false;
    }

    auto header = Internal::PackFileHeader();
    input.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    if (input.good() == false)
    {
        return false;
    }

    if (std::memcmp(header.magic, "BPACK01", 7) != 0)
    {
        return false;
    }

    if (header.version != Internal::FormatVersion)
    {
        return false;
    }

    if (header.headerSize != sizeof(Internal::PackFileHeader))
    {
        return false;
    }

    if (header.flags != 0)
    {
        return false;
    }

    if (header.packageSize > packageSize)
    {
        return false;
    }

    if (header.entryTableOffset != sizeof(Internal::PackFileHeader))
    {
        return false;
    }

    if (header.stringTableOffset != header.entryTableOffset + header.entryTableSize)
    {
        return false;
    }

    if (header.dataOffset < header.stringTableOffset + header.stringTableSize)
    {
        return false;
    }

    if (header.dataOffset > header.packageSize)
    {
        return false;
    }

    if (header.fileCount > std::numeric_limits<uint64_t>::max() / sizeof(Internal::PackFileEntry))
    {
        return false;
    }

    if (header.entryTableSize != header.fileCount * sizeof(Internal::PackFileEntry))
    {
        return false;
    }

    if (CanConvertToSizeT(header.fileCount) == false)
    {
        return false;
    }

    if (CanConvertToSizeT(header.stringTableSize) == false)
    {
        return false;
    }

    if (CanConvertToStreamSize(header.entryTableSize) == false)
    {
        return false;
    }

    if (CanConvertToStreamSize(header.stringTableSize) == false)
    {
        return false;
    }

    if (IsRangeValid(header.entryTableOffset, header.entryTableSize, header.packageSize) == false)
    {
        return false;
    }

    if (IsRangeValid(header.stringTableOffset, header.stringTableSize, header.packageSize) == false)
    {
        return false;
    }

    auto fileEntries = std::vector<Internal::PackFileEntry>(static_cast<size_t>(header.fileCount));
    input.seekg(static_cast<std::streamoff>(header.entryTableOffset), std::ios::beg);
    input.read(
        reinterpret_cast<char*>(fileEntries.data()),
        static_cast<std::streamsize>(fileEntries.size() * sizeof(Internal::PackFileEntry))
    );
    if (input.good() == false && fileEntries.empty() == false)
    {
        return false;
    }

    auto stringTable = std::string(static_cast<size_t>(header.stringTableSize), '\0');
    input.seekg(static_cast<std::streamoff>(header.stringTableOffset), std::ios::beg);
    input.read(stringTable.data(), static_cast<std::streamsize>(stringTable.size()));
    if (input.good() == false && stringTable.empty() == false)
    {
        return false;
    }

    auto entries = std::vector<Entry>();
    auto pathToEntryIndex = std::unordered_map<std::string, size_t>();
    entries.reserve(fileEntries.size());

    for (const auto& fileEntry : fileEntries)
    {
        if (IsRangeValid(fileEntry.pathOffset, fileEntry.pathLength, header.stringTableSize) == false)
        {
            return false;
        }

        if (IsRangeValid(fileEntry.dataOffset, fileEntry.storedSize, header.packageSize) == false)
        {
            return false;
        }

        if (fileEntry.dataOffset < header.dataOffset)
        {
            return false;
        }

        const auto supportedFlags = ToUInt32(FileFlags::Compressed) | ToUInt32(FileFlags::Encrypted);
        if ((fileEntry.flags & ~supportedFlags) != 0)
        {
            return false;
        }

        if (HasFlag(fileEntry.flags, FileFlags::Encrypted))
        {
            return false;
        }

        if (HasFlag(fileEntry.flags, FileFlags::Compressed) == false && fileEntry.originalSize != fileEntry.storedSize)
        {
            return false;
        }

        auto entry = Entry();
        entry.fileInfo.path = stringTable.substr(
            static_cast<size_t>(fileEntry.pathOffset),
            static_cast<size_t>(fileEntry.pathLength)
        );
        entry.fileInfo.originalSize = fileEntry.originalSize;
        entry.fileInfo.storedSize = fileEntry.storedSize;
        entry.fileInfo.flags = fileEntry.flags;
        entry.fileInfo.crc32 = fileEntry.crc32;
        entry.dataOffset = fileEntry.dataOffset;

        if (pathToEntryIndex.emplace(entry.fileInfo.path, entries.size()).second == false)
        {
            return false;
        }

        entries.push_back(entry);
    }

    this->packFilePath_ = packFilePath;
    this->packageSize_ = header.packageSize;
    this->entries_ = std::move(entries);
    this->pathToEntryIndex_ = std::move(pathToEntryIndex);
    this->isOpen_ = true;
    return true;
}

void PackedFile::Close()
{
    this->packFilePath_.clear();
    this->packageSize_ = 0;
    this->isOpen_ = false;
    this->entries_.clear();
    this->pathToEntryIndex_.clear();
}

bool PackedFile::IsOpen() const
{
    return this->isOpen_;
}

size_t PackedFile::GetFileCount() const
{
    return this->entries_.size();
}

bool PackedFile::Exists(const std::string& virtualPath) const
{
    if (this->isOpen_ == false)
    {
        return false;
    }

    const auto normalizedPath = NormalizeVirtualPath(virtualPath);
    return this->pathToEntryIndex_.find(normalizedPath) != this->pathToEntryIndex_.end();
}

FileInfo PackedFile::GetFileInfo(const std::string& virtualPath) const
{
    if (this->isOpen_ == false)
    {
        return FileInfo();
    }

    const auto normalizedPath = NormalizeVirtualPath(virtualPath);
    const auto it = this->pathToEntryIndex_.find(normalizedPath);
    if (it == this->pathToEntryIndex_.end())
    {
        return FileInfo();
    }

    return this->entries_[it->second].fileInfo;
}

FileInfo PackedFile::GetFileInfo(size_t index) const
{
    if (index >= this->entries_.size())
    {
        return FileInfo();
    }

    return this->entries_[index].fileInfo;
}

std::vector<FileInfo> PackedFile::ListFiles() const
{
    auto result = std::vector<FileInfo>();
    result.reserve(this->entries_.size());
    for (const auto& entry : this->entries_)
    {
        result.push_back(entry.fileInfo);
    }

    return result;
}

std::unique_ptr<IReadStream> PackedFile::OpenRead(const std::string& virtualPath) const
{
    if (this->isOpen_ == false)
    {
        return nullptr;
    }

    const auto normalizedPath = NormalizeVirtualPath(virtualPath);
    const auto it = this->pathToEntryIndex_.find(normalizedPath);
    if (it == this->pathToEntryIndex_.end())
    {
        return nullptr;
    }

    const auto& entry = this->entries_[it->second];
    if (HasFlag(entry.fileInfo.flags, FileFlags::Compressed))
    {
        auto data = this->ReadAll(normalizedPath);
        if (data.empty() && entry.fileInfo.originalSize != 0)
        {
            return nullptr;
        }

        return std::make_unique<MemoryReadStream>(std::move(data));
    }

    auto stream = std::make_unique<PackedReadStream>(
        this->packFilePath_,
        entry.dataOffset,
        entry.fileInfo.storedSize
    );
    if (stream->IsOpen() == false)
    {
        return nullptr;
    }

    return stream;
}

std::vector<uint8_t> PackedFile::ReadAll(const std::string& virtualPath) const
{
    if (this->isOpen_ == false)
    {
        return {};
    }

    const auto normalizedPath = NormalizeVirtualPath(virtualPath);
    const auto it = this->pathToEntryIndex_.find(normalizedPath);
    if (it == this->pathToEntryIndex_.end())
    {
        return {};
    }

    const auto& entry = this->entries_[it->second];
    if (entry.fileInfo.storedSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return {};
    }

    auto stream = std::make_unique<PackedReadStream>(
        this->packFilePath_,
        entry.dataOffset,
        entry.fileInfo.storedSize
    );
    if (stream->IsOpen() == false)
    {
        return {};
    }

    auto storedData = std::vector<uint8_t>(static_cast<size_t>(entry.fileInfo.storedSize));
    auto offset = size_t{0};
    while (offset < storedData.size())
    {
        const auto readSize = stream->Read(storedData.data() + offset, storedData.size() - offset);
        if (readSize == 0)
        {
            return {};
        }

        offset += readSize;
    }

    if (HasFlag(entry.fileInfo.flags, FileFlags::Compressed) == false)
    {
        return storedData;
    }

    auto decompressedData = std::vector<uint8_t>();
    if (Internal::DecompressBuffer(storedData, entry.fileInfo.originalSize, decompressedData) == false)
    {
        return {};
    }

    return decompressedData;
}

bool PackedFile::ExtractFile(const std::string& virtualPath, const std::string& outputFilePath) const
{
    if (outputFilePath.empty())
    {
        return false;
    }

    auto stream = this->OpenRead(virtualPath);
    if (stream == nullptr)
    {
        return false;
    }

    const auto outputPath = std::filesystem::path(outputFilePath);
    if (outputPath.has_parent_path())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    auto output = std::ofstream(outputPath, std::ios::binary | std::ios::trunc);
    if (output.is_open() == false)
    {
        return false;
    }

    auto buffer = std::vector<uint8_t>(Internal::CopyBufferSize);
    while (stream->Tell() < stream->Size())
    {
        const auto readSize = stream->Read(buffer.data(), buffer.size());
        if (readSize == 0)
        {
            return false;
        }

        output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(readSize));
        if (output.good() == false)
        {
            return false;
        }
    }

    output.close();
    return output.good();
}

std::string PackedFile::ExtractToTempFile(const std::string& virtualPath) const
{
    const auto tempPath = MakeTempFilePath(virtualPath);
    if (this->ExtractFile(virtualPath, tempPath) == false)
    {
        return {};
    }

    return tempPath;
}

bool PackedFile::VerifyFile(const std::string& virtualPath) const
{
    if (this->isOpen_ == false)
    {
        return false;
    }

    const auto normalizedPath = NormalizeVirtualPath(virtualPath);
    const auto it = this->pathToEntryIndex_.find(normalizedPath);
    if (it == this->pathToEntryIndex_.end())
    {
        return false;
    }

    const auto& entry = this->entries_[it->second];
    auto stream = this->OpenRead(normalizedPath);
    if (stream == nullptr)
    {
        return false;
    }

    auto crc = Internal::BeginCrc32();
    auto buffer = std::vector<uint8_t>(Internal::CopyBufferSize);
    while (stream->Tell() < stream->Size())
    {
        const auto readSize = stream->Read(buffer.data(), buffer.size());
        if (readSize == 0)
        {
            return false;
        }

        crc = Internal::UpdateCrc32(crc, buffer.data(), readSize);
    }

    return Internal::FinishCrc32(crc) == entry.fileInfo.crc32;
}

bool PackedFile::VerifyAll() const
{
    if (this->isOpen_ == false)
    {
        return false;
    }

    for (const auto& entry : this->entries_)
    {
        if (this->VerifyFile(entry.fileInfo.path) == false)
        {
            return false;
        }
    }

    return true;
}

}  // BinPackLib
