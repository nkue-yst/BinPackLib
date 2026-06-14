#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BinPackLib/PackedFile.h"
#include "BinPackLib/ReadStream.h"
#include "BinPackLib/Types.h"

namespace BinPackLib
{

class IFileSystem
{
public:
    virtual ~IFileSystem() = default;

    virtual bool Exists(const std::string& path) const = 0;
    virtual FileInfo GetFileInfo(const std::string& path) const = 0;
    virtual std::unique_ptr<IReadStream> OpenRead(const std::string& path) const = 0;
    virtual std::vector<uint8_t> ReadAll(const std::string& path) const = 0;
};

class NativeFileSystem final : public IFileSystem
{
public:
    NativeFileSystem();
    explicit NativeFileSystem(const std::string& rootDirectory);

    void SetRootDirectory(const std::string& rootDirectory);

    bool Exists(const std::string& path) const override;
    FileInfo GetFileInfo(const std::string& path) const override;
    std::unique_ptr<IReadStream> OpenRead(const std::string& path) const override;
    std::vector<uint8_t> ReadAll(const std::string& path) const override;

private:
    std::string ResolvePath(const std::string& path) const;

    std::string rootDirectory_;
};


class PackedFileSystem final : public IFileSystem
{
public:
    PackedFileSystem();
    explicit PackedFileSystem(const std::string& packFilePath);

    bool Open(const std::string& packFilePath);
    void Close();

    bool Exists(const std::string& path) const override;
    FileInfo GetFileInfo(const std::string& path) const override;
    std::unique_ptr<IReadStream> OpenRead(const std::string& path) const override;
    std::vector<uint8_t> ReadAll(const std::string& path) const override;

private:
    PackedFile packedFile_;
};


class MountFileSystem final : public IFileSystem
{
public:
    MountFileSystem();

    void MountNative(const std::string& rootDirectory);
    bool MountPackage(const std::string& packFilePath);

    bool Exists(const std::string& path) const override;
    FileInfo GetFileInfo(const std::string& path) const override;
    std::unique_ptr<IReadStream> OpenRead(const std::string& path) const override;
    std::vector<uint8_t> ReadAll(const std::string& path) const override;

private:
    std::vector<std::unique_ptr<IFileSystem>> fileSystems_;
};

}  // namespace BinPackLib
