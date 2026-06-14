# BinPackLib
BinPackLib は、PNG / MP3 / MP4 などの複数のバイナリファイルを 1 つのパックファイルにまとめ、必要なファイルだけを読み出すための C++ ライブラリです。

読み込み時に常駐するのは、固定長ヘッダー、ファイルエントリ一覧、文字列テーブル、パス検索用インデックスです。非圧縮ファイルの本体データは、`OpenRead()` や `ReadAll()` が呼ばれたタイミングで必要な範囲だけ読み込みます。

## 主な機能
- 複数ファイルをパックファイルにパッキング
- 仮想パスによるファイル検索
- ファイル一覧取得
- `ReadAll()` によるメモリ読み込み
- `IReadStream` によるストリーム読み込み
- 通常ファイルとパック内ファイルを同じ API で扱う `IFileSystem`
- ファイル単位圧縮
- CRC32 検証
- 一時ファイル展開、指定パスへの展開
- C-style callback adapter
- `BinPack.exe` CLI

## 必要環境
- C++20
- CMake 3.20 以上
- Visual Studio 2022 / MSBuild
- Windows では圧縮機能のために `Cabinet.lib` をリンクします

## ビルド
```powershell
$ cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
$ MSBuild build\BinPackLib.sln /p:Configuration=Debug /p:Platform=x64 /m
$ ctest --test-dir build -C Debug --output-on-failure
```

生成物:
- `build/Debug/binpack.lib`
- `build/Debug/BinPack.exe`
- `build/Debug/BinPackLibTests.exe`

## CMake オプション
| Option | Default | 内容 |
| --- | --- | --- |
| `BINPACKLIB_BUILD_TESTS` | `ON` | テスト実行ファイルと CTest 登録を有効にします |
| `BINPACKLIB_BUILD_TOOLS` | `ON` | `BinPack.exe` CLI をビルドします |

例:
```powershell
$ cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DBINARY_PACK_BUILD_TESTS=OFF
```

## 利用方法
利用側プロジェクトでは、`include` をインクルードパスに追加し、`binpack.lib` をリンクしてください。Windows で圧縮済み pack を扱う場合は `Cabinet.lib` もリンクしてください。

```cpp
#include "BinPackLib/PackFiles.h"
#include "BinPackLib/PackedFile.h"

#include <iostream>
#include <vector>

int main()
{
    auto options = BinPackLib::PackOptions();
    options.baseDirectory = "assets";
    options.overwrite = true;
    options.enableCompression = true;

    const auto result = BinPackLib::PackFiles(
        {
            "assets/textures/player.png",
            "assets/audio/bgm.mp3"
        },
        "game_assets.bpack",
        options
    );

    if (result.success == false)
    {
        std::cerr << result.errorMessage << "\n";
        return 1;
    }

    auto packedFile = BinPackLib::PackedFile();
    if (packedFile.Open("game_assets.bpack") == false)
    {
        return 1;
    }

    const auto data = packedFile.ReadAll("textures/player.png");
    std::cout << "player.png size = " << data.size() << "\n";
    return 0;
}
```

## パック作成 API
### PackOptions
```cpp
struct PackOptions
{
    std::string baseDirectory;
    bool preserveDirectoryStructure;
    bool overwrite;
    uint32_t alignment;
    bool enableCompression;
};
```

既定値:
| メンバ | 既定値 | 内容 |
| --- | --- | --- |
| `baseDirectory` | 空文字列 | 仮想パス生成の基準ディレクトリ |
| `preserveDirectoryStructure` | `true` | ディレクトリ構造を仮想パスに残す |
| `overwrite` | `false` | 出力 pack が存在する場合に上書きするか |
| `alignment` | `16` | データ領域のアラインメント |
| `enableCompression` | `false` | ファイル単位圧縮の有効化 |

`baseDirectory` を指定すると、入力ファイルはそのディレクトリからの相対パスとして pack に保存されます。

```cpp
auto options = BinPackLib::PackOptions();
options.baseDirectory = "C:/Project/Assets";

BinPackLib::PackFiles(
    { "C:/Project/Assets/textures/player.png" },
    "assets.bpack",
    options
);

// pack 内の仮想パスは textures/player.png
```

`preserveDirectoryStructure == false` の場合はファイル名だけを保存します。同じ仮想パスが発生した場合、パック作成は失敗します。

### PackResult
```cpp
struct PackResult
{
    bool success;
    std::string errorMessage;
    size_t fileCount;
    uint64_t totalOriginalSize;
    uint64_t totalStoredSize;
};
```

`success == false` の場合、`errorMessage` に失敗理由が入ります。

### PackFiles
```cpp
bool PackFiles(
    const std::vector<std::string>& inputFilePaths,
    const std::string& outputPackFilePath
);

PackResult PackFiles(
    const std::vector<std::string>& inputFilePaths,
    const std::string& outputPackFilePath,
    const PackOptions& options
);
```

簡易版は成功/失敗だけを返します。詳細なエラーや統計情報が必要な場合は `PackResult` 版を使ってください。

## 読み込み API
### PackedFile
```cpp
class PackedFile
{
public:
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
};
```

### ファイル一覧
```cpp
auto packedFile = BinPackLib::PackedFile();
packedFile.Open("assets.bpack");

for (const auto& fileInfo : packedFile.ListFiles())
{
    std::cout
        << fileInfo.path << " "
        << fileInfo.originalSize << " "
        << fileInfo.storedSize << " "
        << fileInfo.flags << "\n";
}
```

### ReadAll
小さな画像や、サードパーティがメモリバッファを要求する場合に使います。

```cpp
const auto data = packedFile.ReadAll("textures/player.png");
if (data.empty())
{
    // ファイルが空か、読み込み失敗の可能性があります。
}
```

空ファイルも空 vector を返すため、存在確認が必要な場合は `Exists()` や `GetFileInfo()` と併用してください。

### OpenRead
大きな音声・動画など、ストリームとして読みたい場合に使います。

```cpp
auto stream = packedFile.OpenRead("audio/bgm.mp3");
if (stream == nullptr)
{
    return;
}

std::vector<uint8_t> buffer(4096);
while (stream->Tell() < stream->Size())
{
    const auto readSize = stream->Read(buffer.data(), buffer.size());
    if (readSize == 0)
    {
        break;
    }

    // buffer[0..readSize) を処理
}
```

非圧縮ファイルは pack ファイル上の対象範囲だけを読みます。圧縮ファイルは対象ファイル 1 個分を展開したメモリストリームを返します。

### 展開
```cpp
packedFile.ExtractFile("movie/opening.mp4", "cache/movie/opening.mp4");

const auto tempPath = packedFile.ExtractToTempFile("movie/opening.mp4");
if (tempPath.empty() == false)
{
    // 実ファイルパスしか受け取れないライブラリに渡す
}
```

`ExtractToTempFile()` が作成したファイルは自動削除されません。不要になったタイミングで呼び出し側が削除してください。

### CRC 検証
```cpp
if (packedFile.VerifyAll() == false)
{
    std::cerr << "pack file is corrupted\n";
}

if (packedFile.VerifyFile("textures/player.png"))
{
    std::cout << "OK\n";
}
```

圧縮ファイルは展開後のデータで検証します。

## FileInfo と FileFlags
```cpp
struct FileInfo
{
    std::string path;
    uint64_t originalSize;
    uint64_t storedSize;
    uint32_t flags;
    uint32_t crc32;
};
```

```cpp
enum class FileFlags : uint32_t
{
    None = 0,
    Compressed = 1 << 0,
    Encrypted = 1 << 1
};
```

圧縮されているか確認する例:
```cpp
const auto info = packedFile.GetFileInfo("textures/player.png");
if (BinPackLib::HasFlag(info.flags, BinPackLib::FileFlags::Compressed))
{
    std::cout << "compressed\n";
}
```

`Encrypted` flag は将来拡張用です。現時点では暗号化処理は実装されておらず、`Encrypted` flag を持つ pack は読み込みに失敗します。

## IReadStream と callback adapter
```cpp
class IReadStream
{
public:
    virtual uint64_t Size() const = 0;
    virtual uint64_t Tell() const = 0;
    virtual bool Seek(uint64_t position) = 0;
    virtual size_t Read(void* buffer, size_t size) = 0;
    virtual bool IsOpen() const = 0;
};
```

C-style callback が必要なライブラリには `CreateReadStreamCallbacks()` を使えます。

```cpp
auto stream = packedFile.OpenRead("audio/bgm.mp3");
auto callbacks = BinPackLib::CreateReadStreamCallbacks(stream.get());

callbacks.seek(callbacks.userData, 128);
auto position = callbacks.tell(callbacks.userData);
std::vector<uint8_t> buffer(1024);
auto readSize = callbacks.read(callbacks.userData, buffer.data(), buffer.size());
```

callback は `IReadStream` を所有しません。呼び出し側が stream の寿命を維持してください。

## 仮想ファイルシステム API
`IFileSystem` を使うと、通常ファイルと pack 内ファイルを同じ API で扱えます。

```cpp
auto native = BinPackLib::NativeFileSystem("assets");
auto packed = BinPackLib::PackedFileSystem("assets.bpack");

auto data1 = native.ReadAll("textures/player.png");
auto data2 = packed.ReadAll("textures/player.png");
```

`MountFileSystem` は複数のファイルシステムを重ねます。後から mount したものが優先されます。

```cpp
auto fileSystem = BinPackLib::MountFileSystem();
fileSystem.MountNative("assets");
fileSystem.MountPackage("patch.bpack");

// patch.bpack に同名ファイルがあればそちらが優先されます。
auto data = fileSystem.ReadAll("textures/player.png");
```

## BinPack CLI
### pack
```powershell
build\Debug\BinPack.exe pack assets.bpack --base assets --overwrite assets\textures\player.png assets\audio\bgm.mp3
```

ディレクトリを渡すと再帰的に通常ファイルを収集します。

```powershell
build\Debug\BinPack.exe pack assets.bpack --base assets --overwrite assets
```

圧縮を有効にする場合:
```powershell
build\Debug\BinPack.exe pack assets.bpack --base assets --overwrite --compress assets
```

### list
```powershell
build\Debug\BinPack.exe list assets.bpack
```

出力形式:
```text
virtual/path<TAB>originalSize<TAB>storedSize<TAB>flags
```

### extract
```powershell
build\Debug\BinPack.exe extract assets.bpack textures/player.png out/player.png
```

### verify
```powershell
build\Debug\BinPack.exe verify assets.bpack
```

検証成功時は `OK` を出力して exit code `0` を返します。失敗時は非 `0` を返します。

## 圧縮仕様
`PackOptions::enableCompression` または CLI の `--compress` でファイル単位圧縮を試します。

- Windows Compression API を使用
- アルゴリズムは `COMPRESS_ALGORITHM_XPRESS_HUFF`
- 圧縮後サイズが元サイズより小さい場合だけ圧縮データを保存
- 圧縮しても小さくならない場合は非圧縮で保存
- `FileInfo::originalSize` は元サイズ
- `FileInfo::storedSize` は pack 内に保存されたサイズ
- CRC32 は展開後の元データ基準

非 Windows 環境では、現時点の圧縮実装は未対応です。

## 制限事項
- 暗号化処理は未実装です。
- `Encrypted` flag を持つ pack は読み込みに失敗します。
- 圧縮ファイルの `OpenRead()` は対象ファイル全体を展開してメモリに保持します。
- 任意のサードパーティライブラリに対して、pack 内ファイルを OS 上の通常パスとして直接見せる機能はありません。
- pack 全体圧縮は行いません。
- pack フォーマットは little-endian 前提です。
- 例外を基本 API の失敗通知には使いません。

## テスト
```powershell
$ ctest --test-dir build -C Debug --output-on-failure
```
