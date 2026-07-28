# run68mpx

Human68k の実行ファイル（`.x` / `.r`）を macOS などのターミナルで実行する CUI エミュレータです。

このリポジトリは、次の2系統の run68 を土台として、現行macOSで保守・改良を続けるために作成しました。

- [YosAwed/run68Win10VS2022](https://github.com/YosAwed/run68Win10VS2022) — Windows 10 / Visual Studio 2022向けの修正版
- [GOROman/run68mac](https://github.com/GOROman/run68mac) — macOS、Linux、MSYS、Emscripten向けの移植版

現在のソースは、macOS対応と近年のコンパイラ修正を含む `GOROman/run68mac` の最新版を基準にしています。Windows版の独自変更は履歴を保持し、内容を確認しながら段階的に取り込みます。詳しくは [doc/upstreams.md](doc/upstreams.md) を参照してください。

## 対応環境

- macOS（Apple Silicon / Intel）
- Linux
- Windows（MSYS / MinGW）
- Emscripten（既存の実験的対応）

主な開発対象はmacOSです。

## ビルド

CMake 3.13以降が必要です。macOSでCMakeを未導入の場合はHomebrewでインストールできます。

```sh
brew install cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

生成された実行ファイルは `build/run68` です。

Xcodeプロジェクトを生成する場合:

```sh
cmake -S . -B build-xcode -G Xcode
open build-xcode/run68.xcodeproj
```

## テスト

CPU命令、メモリアクセス、Xファイルローダ、macOS互換層の回帰テストをCTestで実行できます。

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

AddressSanitizerとUndefinedBehaviorSanitizerを使う場合:

```sh
cmake -S . -B build-sanitize -DBUILD_TESTING=ON -DRUN68_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

## 使い方

```sh
./build/run68 program.x [引数...]
```

標準出力と標準エラー出力のShift-JIS文字列は、対応環境ではUTF-8へ変換されます。

## ライセンス

GNU General Public License version 2（GPL-2.0）です。詳細は [LICENCE](LICENCE) を参照してください。

元プロジェクトと各移植・修正の作者、コントリビューターに感謝します。
