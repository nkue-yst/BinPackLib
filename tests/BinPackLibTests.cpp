#include "BinPackLib/FileSystem.h"
#include "BinPackLib/PackFiles.h"
#include "BinPackLib/PackedFile.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>

namespace
{
#pragma pack(push, 1)
struct TestPackFileHeader
{
    char magic[8];
    uint32_t version;
    uint32_t headerSize;
    uint64_t fileCount;
    uint64_t entryTableOffset;
    uint64_t entryTableSize;
    uint64_t stringTableOffset;
    uint64_t stringTableSize;
    uint64_t dataOffset;
    uint64_t packageSize;
    uint32_t flags;
    uint32_t headerCrc32;
};

struct TestPackFileEntry
{
    uint64_t pathOffset;
    uint32_t pathLength;
    uint32_t flags;
    uint64_t dataOffset;
    uint64_t originalSize;
    uint64_t storedSize;
    uint32_t crc32;
    uint32_t reserved;
};
#pragma pack(pop)

class TestFailure final : public std::exception
{
public:
    explicit TestFailure(std::string message)
        : message_(std::move(message))
    {
    }

    const char* what() const noexcept override
    {
        return this->message_.c_str();
    }

private:
    std::string message_;
};

void Require(bool condition, const std::string& message)
{
    if (condition == false)
    {
        throw TestFailure(message);
    }
}

std::filesystem::path MakeTestRoot()
{
    auto root = std::filesystem::temp_directory_path() / "BinPackLibLibTests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    Require(output.is_open(), "failed to open test file for writing");
    if (bytes.empty() == false)
    {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    auto input = std::ifstream(path, std::ios::binary);
    Require(input.is_open(), "failed to open file for reading");
    input.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(input.tellg());
    input.seekg(0, std::ios::beg);

    auto bytes = std::vector<uint8_t>(size);
    if (bytes.empty() == false)
    {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    return bytes;
}

template <typename T>
T ReadStruct(const std::vector<uint8_t>& bytes, size_t offset)
{
    Require(offset + sizeof(T) <= bytes.size(), "test struct read out of range");
    auto value = T();
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteStruct(std::vector<uint8_t>& bytes, size_t offset, const T& value)
{
    Require(offset + sizeof(T) <= bytes.size(), "test struct write out of range");
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void WriteRawBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    Require(output.is_open(), "failed to open raw test file for writing");
    if (bytes.empty() == false)
    {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

std::filesystem::path CreatePackForCorruptionTest(const std::filesystem::path& root)
{
    const auto inputPath = root / "asset.bin";
    const auto packPath = root / "out.bpack";
    WriteBytes(inputPath, {9, 8, 7, 6});

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = root.string();
    const auto result = BinPackLib::PackFiles({inputPath.string()}, packPath.string(), options);
    Require(result.success, "corruption source pack failed: " + result.errorMessage);
    return packPath;
}

std::vector<uint8_t> ReadStreamInChunks(BinPackLib::IReadStream& stream, size_t chunkSize)
{
    auto result = std::vector<uint8_t>();
    auto buffer = std::vector<uint8_t>(chunkSize);
    while (true)
    {
        const auto readSize = stream.Read(buffer.data(), buffer.size());
        if (readSize == 0)
        {
            break;
        }

        result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(readSize));
    }

    return result;
}

void TestPackSingleFile()
{
    const auto root = MakeTestRoot() / "single";
    const auto inputPath = root / "assets" / "hello.bin";
    const auto packPath = root / "out.bpack";
    const auto bytes = std::vector<uint8_t>{1, 2, 3, 4, 5};
    WriteBytes(inputPath, bytes);

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = (root / "assets").string();
    const auto result = BinPackLib::PackFiles({inputPath.string()}, packPath.string(), options);
    Require(result.success, "single file pack failed: " + result.errorMessage);
    Require(result.fileCount == 1, "single file count mismatch");

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "packed file open failed");
    Require(packedFile.Exists("hello.bin"), "packed file missing virtual path");
    Require(packedFile.ReadAll("hello.bin") == bytes, "packed ReadAll mismatch");
}

void TestPackMultipleAndStream()
{
    const auto root = MakeTestRoot() / "multi";
    const auto base = root / "assets";
    const auto texturePath = base / "textures" / "player.png";
    const auto audioPath = base / "audio" / "bgm.mp3";
    const auto packPath = root / "out.bpack";
    const auto textureBytes = std::vector<uint8_t>{10, 20, 30, 40};
    const auto audioBytes = std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    WriteBytes(texturePath, textureBytes);
    WriteBytes(audioPath, audioBytes);

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = base.string();
    const auto result = BinPackLib::PackFiles(
        {texturePath.string(), audioPath.string()},
        packPath.string(),
        options
    );
    Require(result.success, "multi file pack failed: " + result.errorMessage);

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "multi packed file open failed");
    Require(packedFile.Exists("textures/player.png"), "texture path missing");
    Require(packedFile.Exists("audio/bgm.mp3"), "audio path missing");
    Require(packedFile.GetFileCount() == 2, "packed file count mismatch");
    Require(packedFile.ListFiles().size() == 2, "packed file list size mismatch");
    Require(packedFile.GetFileInfo(size_t{99}).path.empty(), "out of range file info should be empty");

    const auto info = packedFile.GetFileInfo("audio/bgm.mp3");
    Require(info.originalSize == audioBytes.size(), "audio original size mismatch");
    Require(info.storedSize == audioBytes.size(), "audio stored size mismatch");

    auto stream = packedFile.OpenRead("audio/bgm.mp3");
    Require(stream != nullptr, "OpenRead returned null");
    Require(stream->Size() == audioBytes.size(), "stream size mismatch");
    Require(stream->Seek(4), "stream seek failed");

    auto partial = std::vector<uint8_t>(3);
    const auto readSize = stream->Read(partial.data(), partial.size());
    Require(readSize == partial.size(), "partial stream read size mismatch");
    Require(partial == std::vector<uint8_t>({4, 5, 6}), "partial stream read mismatch");

    Require(stream->Seek(0), "stream rewind failed");
    Require(ReadStreamInChunks(*stream, 3) == audioBytes, "chunked stream read mismatch");

    const auto callbacks = BinPackLib::CreateReadStreamCallbacks(stream.get());
    Require(callbacks.isOpen(callbacks.userData), "callback isOpen failed");
    Require(callbacks.size(callbacks.userData) == audioBytes.size(), "callback size mismatch");
    Require(callbacks.seek(callbacks.userData, 2), "callback seek failed");
    Require(callbacks.tell(callbacks.userData) == 2, "callback tell mismatch");

    auto callbackBytes = std::vector<uint8_t>(4);
    const auto callbackReadSize = callbacks.read(callbacks.userData, callbackBytes.data(), callbackBytes.size());
    Require(callbackReadSize == callbackBytes.size(), "callback read size mismatch");
    Require(callbackBytes == std::vector<uint8_t>({2, 3, 4, 5}), "callback read mismatch");

    const auto nullCallbacks = BinPackLib::CreateReadStreamCallbacks(nullptr);
    Require(nullCallbacks.isOpen(nullCallbacks.userData) == false, "null callback isOpen should fail");
    Require(nullCallbacks.size(nullCallbacks.userData) == 0, "null callback size should be zero");
    Require(nullCallbacks.read(nullCallbacks.userData, callbackBytes.data(), callbackBytes.size()) == 0, "null callback read should be zero");
}

void TestEmptyFile()
{
    const auto root = MakeTestRoot() / "empty";
    const auto inputPath = root / "empty.bin";
    const auto packPath = root / "out.bpack";
    WriteBytes(inputPath, {});

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = root.string();
    const auto result = BinPackLib::PackFiles({inputPath.string()}, packPath.string(), options);
    Require(result.success, "empty file pack failed: " + result.errorMessage);

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "empty packed file open failed");
    Require(packedFile.Exists("empty.bin"), "empty file path missing");
    Require(packedFile.GetFileInfo("empty.bin").originalSize == 0, "empty file size mismatch");
    Require(packedFile.ReadAll("empty.bin").empty(), "empty ReadAll should be empty");
}

void TestMissingDuplicateAndOverwriteErrors()
{
    const auto root = MakeTestRoot() / "errors";
    const auto packPath = root / "out.bpack";
    const auto missingPath = root / "missing.bin";

    auto options = BinPackLib::PackOptions();
    auto result = BinPackLib::PackFiles({missingPath.string()}, packPath.string(), options);
    Require(result.success == false, "missing input should fail");

    const auto aPath = root / "a" / "same.bin";
    const auto bPath = root / "b" / "same.bin";
    WriteBytes(aPath, {1});
    WriteBytes(bPath, {2});
    options.preserveDirectoryStructure = false;
    result = BinPackLib::PackFiles({aPath.string(), bPath.string()}, packPath.string(), options);
    Require(result.success == false, "duplicate virtual paths should fail");

    options.preserveDirectoryStructure = true;
    options.baseDirectory = root.string();
    result = BinPackLib::PackFiles({aPath.string()}, packPath.string(), options);
    Require(result.success, "initial overwrite test pack failed");
    result = BinPackLib::PackFiles({aPath.string()}, packPath.string(), options);
    Require(result.success == false, "existing output without overwrite should fail");
}

void TestCorruptPackFails()
{
    const auto root = MakeTestRoot() / "corrupt";
    const auto packPath = root / "bad.bpack";
    WriteBytes(packPath, {'N', 'O', 'T', 'P', 'A', 'C', 'K', 0, 0, 0, 0, 0});

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()) == false, "corrupt pack should fail to open");
}

void TestFileSystems()
{
    const auto root = MakeTestRoot() / "filesystem";
    const auto nativeRoot = root / "native";
    const auto packedRoot = root / "packed";
    const auto nativePath = nativeRoot / "shared.bin";
    const auto packedPath = packedRoot / "shared.bin";
    const auto nativeOnlyPath = nativeRoot / "native_only.bin";
    const auto packPath = root / "out.bpack";
    const auto nativeBytes = std::vector<uint8_t>{1, 1, 1};
    const auto packedBytes = std::vector<uint8_t>{2, 2, 2};
    const auto nativeOnlyBytes = std::vector<uint8_t>{3, 3, 3};
    WriteBytes(nativePath, nativeBytes);
    WriteBytes(nativeOnlyPath, nativeOnlyBytes);
    WriteBytes(packedPath, packedBytes);

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = packedRoot.string();
    auto result = BinPackLib::PackFiles({packedPath.string()}, packPath.string(), options);
    Require(result.success, "filesystem pack failed: " + result.errorMessage);

    auto nativeFileSystem = BinPackLib::NativeFileSystem(nativeRoot.string());
    Require(nativeFileSystem.Exists("shared.bin"), "native filesystem missing shared.bin");
    Require(nativeFileSystem.ReadAll("shared.bin") == nativeBytes, "native filesystem read mismatch");

    auto packedFileSystem = BinPackLib::PackedFileSystem();
    Require(packedFileSystem.Open(packPath.string()), "packed filesystem open failed");
    Require(packedFileSystem.Exists("shared.bin"), "packed filesystem missing shared.bin");
    Require(packedFileSystem.ReadAll("shared.bin") == packedBytes, "packed filesystem read mismatch");

    auto mountFileSystem = BinPackLib::MountFileSystem();
    mountFileSystem.MountNative(nativeRoot.string());
    Require(mountFileSystem.MountPackage(packPath.string()), "mount package failed");
    Require(mountFileSystem.ReadAll("shared.bin") == packedBytes, "mount priority mismatch");
    Require(mountFileSystem.ReadAll("native_only.bin") == nativeOnlyBytes, "mount native fallback mismatch");
}

void TestExtractionAndVerify()
{
    const auto root = MakeTestRoot() / "extract";
    const auto inputPath = root / "assets" / "data.bin";
    const auto packPath = root / "out.bpack";
    const auto extractedPath = root / "out" / "data.bin";
    const auto bytes = std::vector<uint8_t>{5, 4, 3, 2, 1};
    WriteBytes(inputPath, bytes);

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = (root / "assets").string();
    const auto result = BinPackLib::PackFiles({inputPath.string()}, packPath.string(), options);
    Require(result.success, "extract pack failed: " + result.errorMessage);

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "extract packed file open failed");
    Require(packedFile.VerifyFile("data.bin"), "VerifyFile should pass");
    Require(packedFile.VerifyAll(), "VerifyAll should pass");
    Require(packedFile.VerifyFile("missing.bin") == false, "missing VerifyFile should fail");

    Require(packedFile.ExtractFile("data.bin", extractedPath.string()), "ExtractFile failed");
    Require(ReadBytes(extractedPath) == bytes, "ExtractFile bytes mismatch");
    Require(packedFile.ExtractFile("missing.bin", (root / "missing.bin").string()) == false, "missing ExtractFile should fail");

    const auto tempPath = packedFile.ExtractToTempFile("data.bin");
    Require(tempPath.empty() == false, "ExtractToTempFile failed");
    Require(std::filesystem::is_regular_file(std::filesystem::path(tempPath)), "temp file missing");
    Require(ReadBytes(std::filesystem::path(tempPath)) == bytes, "temp extract bytes mismatch");
    std::filesystem::remove(std::filesystem::path(tempPath));
}

void TestCompression()
{
    const auto root = MakeTestRoot() / "compression";
    const auto compressiblePath = root / "assets" / "text.txt";
    const auto randomPath = root / "assets" / "tiny.bin";
    const auto packPath = root / "out.bpack";

    auto compressibleBytes = std::vector<uint8_t>();
    for (auto index = 0; index < 4096; ++index)
    {
        compressibleBytes.push_back(static_cast<uint8_t>('A' + (index % 3)));
    }
    const auto tinyBytes = std::vector<uint8_t>{0, 1, 2, 3, 4};
    WriteBytes(compressiblePath, compressibleBytes);
    WriteBytes(randomPath, tinyBytes);

    auto options = BinPackLib::PackOptions();
    options.baseDirectory = (root / "assets").string();
    options.enableCompression = true;

    const auto result = BinPackLib::PackFiles(
        {compressiblePath.string(), randomPath.string()},
        packPath.string(),
        options
    );
    Require(result.success, "compressed pack failed: " + result.errorMessage);
    Require(result.totalStoredSize < result.totalOriginalSize, "compressed pack should be smaller");

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "compressed packed file open failed");
    Require(packedFile.GetFileCount() == 2, "compressed packed file count mismatch");

    const auto compressedInfo = packedFile.GetFileInfo("text.txt");
    Require(BinPackLib::HasFlag(compressedInfo.flags, BinPackLib::FileFlags::Compressed), "compressible file should be compressed");
    Require(compressedInfo.storedSize < compressedInfo.originalSize, "compressed file stored size should be smaller");
    Require(packedFile.ReadAll("text.txt") == compressibleBytes, "compressed ReadAll mismatch");

    auto stream = packedFile.OpenRead("text.txt");
    Require(stream != nullptr, "compressed OpenRead returned null");
    Require(ReadStreamInChunks(*stream, 127) == compressibleBytes, "compressed stream read mismatch");

    const auto tinyInfo = packedFile.GetFileInfo("tiny.bin");
    Require(BinPackLib::HasFlag(tinyInfo.flags, BinPackLib::FileFlags::Compressed) == false, "tiny file should not use compression");
    Require(packedFile.ReadAll("tiny.bin") == tinyBytes, "tiny file ReadAll mismatch");
    Require(packedFile.VerifyFile("text.txt"), "compressed VerifyFile failed");
    Require(packedFile.VerifyAll(), "compressed VerifyAll failed");
}

void TestVerifyDetectsDataCorruption()
{
    const auto root = MakeTestRoot() / "verify_corrupt";
    const auto packPath = CreatePackForCorruptionTest(root);
    auto bytes = ReadBytes(packPath);
    const auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
    Require(header.dataOffset < bytes.size(), "corruption test data offset invalid");
    bytes[static_cast<size_t>(header.dataOffset)] ^= 0xFF;
    WriteRawBytes(packPath, bytes);

    auto packedFile = BinPackLib::PackedFile();
    Require(packedFile.Open(packPath.string()), "corrupt data pack should still open");
    Require(packedFile.VerifyFile("asset.bin") == false, "VerifyFile should detect data corruption");
    Require(packedFile.VerifyAll() == false, "VerifyAll should detect data corruption");
}

void TestCorruptHeaderValidation()
{
    {
        const auto root = MakeTestRoot() / "corrupt_header_flags";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        header.flags = 1;
        WriteStruct(bytes, 0, header);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "unsupported header flags should fail");
    }

    {
        const auto root = MakeTestRoot() / "corrupt_entry_flags";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        auto entry = ReadStruct<TestPackFileEntry>(bytes, static_cast<size_t>(header.entryTableOffset));
        entry.flags = 1 << 8;
        WriteStruct(bytes, static_cast<size_t>(header.entryTableOffset), entry);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "unsupported entry flags should fail");
    }

    {
        const auto root = MakeTestRoot() / "encrypted_entry_flag";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        auto entry = ReadStruct<TestPackFileEntry>(bytes, static_cast<size_t>(header.entryTableOffset));
        entry.flags = BinPackLib::ToUInt32(BinPackLib::FileFlags::Encrypted);
        WriteStruct(bytes, static_cast<size_t>(header.entryTableOffset), entry);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "encrypted entry flag should fail");
    }

    {
        const auto root = MakeTestRoot() / "corrupt_offset";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        auto entry = ReadStruct<TestPackFileEntry>(bytes, static_cast<size_t>(header.entryTableOffset));
        entry.dataOffset = header.dataOffset - 1;
        WriteStruct(bytes, static_cast<size_t>(header.entryTableOffset), entry);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "data offset before data area should fail");
    }

    {
        const auto root = MakeTestRoot() / "corrupt_size";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        auto entry = ReadStruct<TestPackFileEntry>(bytes, static_cast<size_t>(header.entryTableOffset));
        entry.originalSize = entry.storedSize + 1;
        WriteStruct(bytes, static_cast<size_t>(header.entryTableOffset), entry);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "original/stored size mismatch should fail");
    }

    {
        const auto root = MakeTestRoot() / "corrupt_ordering";
        const auto packPath = CreatePackForCorruptionTest(root);
        auto bytes = ReadBytes(packPath);
        auto header = ReadStruct<TestPackFileHeader>(bytes, 0);
        header.stringTableOffset += 1;
        WriteStruct(bytes, 0, header);
        WriteRawBytes(packPath, bytes);

        auto packedFile = BinPackLib::PackedFile();
        Require(packedFile.Open(packPath.string()) == false, "bad table ordering should fail");
    }
}
}

int main()
{
    try
    {
        TestPackSingleFile();
        TestPackMultipleAndStream();
        TestEmptyFile();
        TestMissingDuplicateAndOverwriteErrors();
        TestCorruptPackFails();
        TestFileSystems();
        TestExtractionAndVerify();
        TestCompression();
        TestVerifyDetectsDataCorruption();
        TestCorruptHeaderValidation();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "BinPackLib tests passed\n";
    return 0;
}
