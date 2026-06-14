#include "BinPackLib/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace BinPackLib
{

namespace
{

class NativeReadStream final : public IReadStream
{
public:
    explicit NativeReadStream(const std::string& filePath)
        : file_()
        , size_(0)
    {
        this->file_.open(filePath, std::ios::binary);
        if (this->file_.is_open())
        {
            this->file_.seekg(0, std::ios::end);
            this->size_ = static_cast<uint64_t>(this->file_.tellg());
            this->file_.seekg(0, std::ios::beg);
        }
    }

    uint64_t Size() const override
    {
        return this->size_;
    }

    uint64_t Tell() const override
    {
        if (this->file_.is_open() == false)
        {
            return 0;
        }

        return static_cast<uint64_t>(this->file_.tellg());
    }

    bool Seek(uint64_t position) override
    {
        if (position > this->size_)
        {
            return false;
        }

        this->file_.clear();
        this->file_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        return this->file_.good();
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

        const auto position = this->Tell();
        if (position >= this->size_)
        {
            return 0;
        }

        const auto remaining = this->size_ - position;
        const auto readSize = std::min<uint64_t>(static_cast<uint64_t>(size), remaining);
        if (readSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            return 0;
        }

        this->file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(readSize));
        const auto actualRead = this->file_.gcount();
        if (actualRead <= 0)
        {
            return 0;
        }

        return static_cast<size_t>(actualRead);
    }

    bool IsOpen() const override
    {
        return this->file_.is_open();
    }

private:
    mutable std::ifstream file_;
    uint64_t size_;
};

std::string NormalizePathText(const std::string& path)
{
    auto result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

}  // unnamed namespace


NativeFileSystem::NativeFileSystem()
    : rootDirectory_()
{
}

NativeFileSystem::NativeFileSystem(const std::string& rootDirectory)
    : rootDirectory_(rootDirectory)
{
}

void NativeFileSystem::SetRootDirectory(const std::string& rootDirectory)
{
    this->rootDirectory_ = rootDirectory;
}

bool NativeFileSystem::Exists(const std::string& path) const
{
    const auto resolvedPath = this->ResolvePath(path);
    return std::filesystem::is_regular_file(std::filesystem::path(resolvedPath));
}

FileInfo NativeFileSystem::GetFileInfo(const std::string& path) const
{
    const auto resolvedPath = this->ResolvePath(path);
    if (std::filesystem::is_regular_file(std::filesystem::path(resolvedPath)) == false)
    {
        return FileInfo();
    }

    auto info = FileInfo();
    info.path = NormalizePathText(path);
    info.originalSize = std::filesystem::file_size(std::filesystem::path(resolvedPath));
    info.storedSize = info.originalSize;
    return info;
}

std::unique_ptr<IReadStream> NativeFileSystem::OpenRead(const std::string& path) const
{
    const auto resolvedPath = this->ResolvePath(path);
    auto stream = std::make_unique<NativeReadStream>(resolvedPath);
    if (stream->IsOpen() == false)
    {
        return nullptr;
    }

    return stream;
}

std::vector<uint8_t> NativeFileSystem::ReadAll(const std::string& path) const
{
    auto stream = this->OpenRead(path);
    if (stream == nullptr)
    {
        return {};
    }

    if (stream->Size() > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return {};
    }

    auto data = std::vector<uint8_t>(static_cast<size_t>(stream->Size()));
    auto offset = size_t{0};
    while (offset < data.size())
    {
        const auto readSize = stream->Read(data.data() + offset, data.size() - offset);
        if (readSize == 0)
        {
            return {};
        }

        offset += readSize;
    }

    return data;
}

std::string NativeFileSystem::ResolvePath(const std::string& path) const
{
    const auto requestedPath = std::filesystem::path(path);
    if (requestedPath.is_absolute() || this->rootDirectory_.empty())
    {
        return requestedPath.lexically_normal().string();
    }

    return (std::filesystem::path(this->rootDirectory_) / requestedPath).lexically_normal().string();
}

PackedFileSystem::PackedFileSystem()
    : packedFile_()
{
}

PackedFileSystem::PackedFileSystem(const std::string& packFilePath)
    : packedFile_()
{
    this->Open(packFilePath);
}

bool PackedFileSystem::Open(const std::string& packFilePath)
{
    return this->packedFile_.Open(packFilePath);
}

void PackedFileSystem::Close()
{
    this->packedFile_.Close();
}

bool PackedFileSystem::Exists(const std::string& path) const
{
    return this->packedFile_.Exists(path);
}

FileInfo PackedFileSystem::GetFileInfo(const std::string& path) const
{
    return this->packedFile_.GetFileInfo(path);
}

std::unique_ptr<IReadStream> PackedFileSystem::OpenRead(const std::string& path) const
{
    return this->packedFile_.OpenRead(path);
}

std::vector<uint8_t> PackedFileSystem::ReadAll(const std::string& path) const
{
    return this->packedFile_.ReadAll(path);
}

MountFileSystem::MountFileSystem()
    : fileSystems_()
{
}

void MountFileSystem::MountNative(const std::string& rootDirectory)
{
    this->fileSystems_.push_back(std::make_unique<NativeFileSystem>(rootDirectory));
}

bool MountFileSystem::MountPackage(const std::string& packFilePath)
{
    auto fileSystem = std::make_unique<PackedFileSystem>();
    if (fileSystem->Open(packFilePath) == false)
    {
        return false;
    }

    this->fileSystems_.push_back(std::move(fileSystem));
    return true;
}

bool MountFileSystem::Exists(const std::string& path) const
{
    for (auto it = this->fileSystems_.rbegin(); it != this->fileSystems_.rend(); ++it)
    {
        if ((*it)->Exists(path))
        {
            return true;
        }
    }

    return false;
}

FileInfo MountFileSystem::GetFileInfo(const std::string& path) const
{
    for (auto it = this->fileSystems_.rbegin(); it != this->fileSystems_.rend(); ++it)
    {
        if ((*it)->Exists(path))
        {
            return (*it)->GetFileInfo(path);
        }
    }

    return FileInfo();
}

std::unique_ptr<IReadStream> MountFileSystem::OpenRead(const std::string& path) const
{
    for (auto it = this->fileSystems_.rbegin(); it != this->fileSystems_.rend(); ++it)
    {
        auto stream = (*it)->OpenRead(path);
        if (stream != nullptr)
        {
            return stream;
        }
    }

    return nullptr;
}

std::vector<uint8_t> MountFileSystem::ReadAll(const std::string& path) const
{
    for (auto it = this->fileSystems_.rbegin(); it != this->fileSystems_.rend(); ++it)
    {
        if ((*it)->Exists(path))
        {
            return (*it)->ReadAll(path);
        }
    }

    return {};
}

}  // namespace BinPackLib
