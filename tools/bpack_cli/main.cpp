#include "BinPackLib/PackedFile.h"
#include "BinPackLib/PackFiles.h"
#include "BinPackLib/Types.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void PrintUsage()
{
    std::cout
        << "Usage:\n"
        << "  bpack pack output.bpack [--base DIR] [--overwrite] [--compress] inputs...\n"
        << "  bpack list output.bpack\n"
        << "  bpack extract output.bpack virtual/path output/path\n"
        << "  bpack verify output.bpack\n";
}

void AddInputPath(const std::filesystem::path& path, std::vector<std::string>& inputFilePaths)
{
    if (std::filesystem::is_directory(path))
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                inputFilePaths.push_back(entry.path().string());
            }
        }
        return;
    }

    inputFilePaths.push_back(path.string());
}

int RunPack(int argc, char** argv)
{
    if (argc < 4)
    {
        PrintUsage();
        return 1;
    }

    const auto outputPackFilePath = std::string(argv[2]);
    auto options = BinPackLib::PackOptions();
    auto inputFilePaths = std::vector<std::string>();

    for (auto index = 3; index < argc; ++index)
    {
        const auto argument = std::string(argv[index]);
        if (argument == "--base")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "--base requires a directory\n";
                return 1;
            }

            options.baseDirectory = argv[++index];
            continue;
        }

        if (argument == "--overwrite")
        {
            options.overwrite = true;
            continue;
        }

        if (argument == "--compress")
        {
            options.enableCompression = true;
            continue;
        }

        AddInputPath(std::filesystem::path(argument), inputFilePaths);
    }

    const auto result = BinPackLib::PackFiles(inputFilePaths, outputPackFilePath, options);
    if (result.success == false)
    {
        std::cerr << result.errorMessage << '\n';
        return 1;
    }

    std::cout
        << "Packed " << result.fileCount << " files, "
        << result.totalOriginalSize << " bytes original, "
        << result.totalStoredSize << " bytes stored\n";
    return 0;
}

int RunList(int argc, char** argv)
{
    if (argc != 3)
    {
        PrintUsage();
        return 1;
    }

    auto packedFile = BinPackLib::PackedFile();
    if (packedFile.Open(argv[2]) == false)
    {
        std::cerr << "Failed to open pack file\n";
        return 1;
    }

    for (const auto& fileInfo : packedFile.ListFiles())
    {
        std::cout
            << fileInfo.path << '\t'
            << fileInfo.originalSize << '\t'
            << fileInfo.storedSize << '\t'
            << fileInfo.flags << '\n';
    }

    return 0;
}

int RunExtract(int argc, char** argv)
{
    if (argc != 5)
    {
        PrintUsage();
        return 1;
    }

    auto packedFile = BinPackLib::PackedFile();
    if (packedFile.Open(argv[2]) == false)
    {
        std::cerr << "Failed to open pack file\n";
        return 1;
    }

    if (packedFile.ExtractFile(argv[3], argv[4]) == false)
    {
        std::cerr << "Failed to extract file\n";
        return 1;
    }

    return 0;
}

int RunVerify(int argc, char** argv)
{
    if (argc != 3)
    {
        PrintUsage();
        return 1;
    }

    auto packedFile = BinPackLib::PackedFile();
    if (packedFile.Open(argv[2]) == false)
    {
        std::cerr << "Failed to open pack file\n";
        return 1;
    }

    if (packedFile.VerifyAll() == false)
    {
        std::cerr << "Verification failed\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    const auto command = std::string(argv[1]);
    if (command == "pack")
    {
        return RunPack(argc, argv);
    }

    if (command == "list")
    {
        return RunList(argc, argv);
    }

    if (command == "extract")
    {
        return RunExtract(argc, argv);
    }

    if (command == "verify")
    {
        return RunVerify(argc, argv);
    }

    PrintUsage();
    return 1;
}
