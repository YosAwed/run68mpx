# run68mpx

Human68k の実行ファイル（`.x` / `.r`）を macOS などのターミナルで実行する CUI エミュレータです。

このリポジトリは、次の2系統の run68 を土台として、現行macOSで保守・改良を続けるために作成しました。

- [YosAwed/run68Win10VS2022](https://github.com/YosAwed/run68Win10VS2022) — Windows 10 / Visual Studio 2022向けの修正版
- [GOROman/run68mac](https://github.com/GOROman/run68mac) — macOS、Linux、MSYS、Emscripten向けの移植版

現在のソースは、macOS対応と近年のコンパイラ修正を含む `GOROman/run68mac` の最新版を基準にしています。Windows版の独自変更は履歴を保持し、内容を確認しながら段階的に取り込みます。詳しくは [doc/upstreams.md](doc/upstreams.md) を参照してください。

## GOROman/run68macからの主な改良

[GOROman/run68mac](https://github.com/GOROman/run68mac) が実現したmacOS／Linux移植とCMake対応を土台に、run68mpxでは次の改良を加えています。

### MC68000コアの精度

- 32ビット演算をMC68000と同じラップアラウンドとして扱い、ホストC言語の符号付きオーバーフローへの依存を削減
- 加算・減算・比較・条件判定のCCR（X/N/Z/V/C）計算を修正
- 実効アドレス計算、絶対ショートの符号拡張、A7のバイト単位増減、PC相対／インデックスアドレスを修正
- BRA／BSR／Bcc／DBcc／Scc、ADDX／SUBX、ABCD／SBCD／NBCD、MULU／MULS、DIVU／DIVS、MOVEM、シフト／ローテートなどの境界条件とフラグ動作を修正
- Fライン算術の32ビット境界値、除算、浮動小数点変換を安全化

### 例外とスタック

- 通常例外用の共通6バイトフレームと、アドレスエラー用のMC68000形式14バイトフレームを実装
- USP／SSPのスタックバンク切り替えとRTEによる復帰を実装
- 不正命令、ゼロ除算、TRAPV、特権違反、A／Fライン、TRAP、アドレスエラーを例外ベクタへ接続
- 命令途中の奇数アドレスへのワード／ロングアクセスを中断し、ベクタ3のハンドラへ移行

### メモリと実行ファイルローダ

- 24ビットアドレスのラップ、ビッグエンディアンアクセス、アラインメント、確保領域境界の検査を強化
- Xファイルのヘッダ、コード／データ／BSSサイズ、ロード上限、エントリアドレスを検証
- 通常形式と拡張形式のリロケーション、非ゼロのリンクベース、BSS初期化に対応
- 壊れたヘッダ、範囲外セクション、奇数リロケーション先などをロード前に拒否

### macOS／POSIX対応

- DOSCALL `FILES`／`NFILES` のワイルドカード列挙、属性、更新日時、ファイルサイズ、継続検索を実装
- 64ビットMacで検索ハンドルを安全に管理し、検索終了時にHuman68k互換のエラーコード `-18` を返すよう修正
- DOS形式の日時とPOSIXの更新日時を相互変換し、`FILEDATE`の取得・設定に対応
- コンソール入力、ファイルI/O、文字列処理で、バッファ境界やホストAPIのエラー処理を強化

### CLI向けDOSCALL／IOCSCALL

- DOSCALLの`PUTCHAR`、`KEYSNS`、`KFLUSH`、`KEYCTRL`の先読み、`CURDRV`をPOSIX端末で動作するよう修正
- `GETDATE`／`SETDATE`と`GETTIME`／`SETTIME`／`SETTIM2`を仮想RTCへ接続。ゲストから日時を変更してもMac本体のシステム時計は変更しない
- `SETENV`／`GETENV`でゲスト環境ブロックを更新・参照。`GETSS`、`WAIT`、`SETPDB`、`MAKETMP`、`FATCHK`、`S_MALLOC`／`S_MFREE`をCLI向けに接続（`S_PROCESS`によるサブメモリ管理は未対応）
- `-S size`で実行時スタックサイズ（KB）を指定可能（既定64KB）
- IOCSの`B_KEYINP`、`B_KEYSNS`、`B_SFTSNS`、`KEY_INIT`を標準入力へ接続し、通常キーのX68000スキャンコードを返す
- `B_CURON`、`B_CUROFF`、`B_UP`、`B_DOWN`、`B_RIGHT`、`B_LEFT`、`B_CLR_ST`、`B_ERA_ST`、`B_INS`、`B_DEL`をANSIエスケープシーケンスで実装
- `DATEBCD`、`DATESET`、`TIMEBCD`、`TIMESET`、`DATECNV`、`TIMECNV`を実装し、既存の`DATEGET`／`TIMEGET`のBCD形式と月計算を修正
- `ONTIME`を単調時計によるエミュレータ起動後の1/100秒カウンタとして実装
- `B_MEMSTR`、`B_BPOKE`、`B_WPOKE`、`B_LPOKE`、`B_MEMSET`を実装し、アドレスレジスタと転送カウンタもIOCS仕様に従って更新
- `DMAMOVE`の固定／増加／減少アドレスと両方向転送に対応し、ゲストメモリ境界検査を経由するよう安全化
- 実験的なYM2151（OPM）I/O、Timer A/B、IOCS `OPMSET`／`OPMSNS`／`OPMINTST`を実装し、MusashiコアからWAVへ出力
- MSM6258 4-bit ADPCMデコードとIOCS `ADPCMOUT`／`ADPCMSNS`／`ADPCMMOD`を実装し、OPM出力へミックス
- PCM8互換の`TRAP #2` HLEと8チャンネルADPCMミキサーを実装し、EX-PDXの音量・周波数・パン・一時停止／再開に対応

VRAM、物理ディスク、シリアル、マウスなど、上記以外の実機ハードウェアを必要とするIOCSCALLは、このCLI対応の対象外です。

### ビルドと品質確認

- Apple Silicon／Intel macOSおよびLinuxを対象にしたCI構成へ更新
- CTestによる24系統の回帰テストを追加し、CPU命令、例外、メモリ、ローダ、スタックレイアウト、DOSファイル検索、CLI向けDOS／IOCS、Musashiバックエンド、OPM／ADPCM／PCM8音声を検証
- AddressSanitizer／UndefinedBehaviorSanitizerを有効にできる `RUN68_ENABLE_SANITIZERS` オプションを追加
- コンパイラ警告を強化し、現在のバージョン表示を `0.10.0` に更新

これらの変更は元の作者・移植者の成果を置き換えるものではなく、既存の互換性を維持しながら精度とmacOS上の安全性を高めることを目的としています。

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

実際のXファイル群を従来コアとMusashiで比較する場合:

```sh
python3 scripts/compare_cpu_backends.py \
  --run68 build/run68 \
  --samples /path/to/x-files \
  --include-r \
  --exclude-file scripts/corpus_exclude.example.txt \
  --json-out /tmp/corpus.json \
  --csv-out /tmp/corpus.csv \
  --fail-list /tmp/corpus-fail.txt \
  --timeout 5
```

各サンプルを短い一時パスへコピーして両バックエンドで実行し、終了状態、
標準出力、標準エラーを比較します。DIFFは exit_code / stdout / stderr /
timeout_mismatch などに分類され、JSON/CSVへ出力できます。入力待ちや
ハードウェア待ちのプログラムは指定秒数で打ち切り、`--exclude-file`で除外できます。

## 使い方

```sh
./build/run68 program.x [引数...]
```

スタックサイズを変更する場合（単位はKB、既定は64）:

```sh
./build/run68 -S 128 program.x
```

MPUバックエンドは、従来コアが既定です。実験的なMusashiバックエンドは
次のように選択できます。

```sh
./build/run68 --cpu=musashi program.x [引数...]
```

macOSのデフォルト音声デバイスでYM2151／MSM6258／PCM8をリアルタイム再生する場合:

```sh
./build/run68 --cpu=musashi --audio=live program.x [引数...]
```

OPM／ADPCM／PCM8のミックス出力をWAVへ保存することもできます。

```sh
./build/run68 --cpu=musashi --audio=wav:output.wav program.x [引数...]
```

音声は62.5 kHz、16-bitステレオです。リアルタイム出力は現在macOSに対応しています。

`tests/HAS.X`と`tests/hlk.r`がある場合、MXDRV用CLIランチャーをビルドできます。

```sh
cmake --build build --target mxplay
./build/run68 --cpu=musashi --audio=live \
  ./build/MXPLAY.X tests/mxdrv.x tests/BOM_01.MDX
```

ランチャーはMXDRV.XをDOSCALL `EXEC`で子プロセスとして起動し、`KEEPPR`で
常駐した後、MDXをMXDRV転送形式へ整形して`TRAP #4`の`LOADMML`と`M_PLAY`を
呼びます。キーが押されるまで演奏を続け、1キー入力を受けると`M_END`で停止します。
MDXにPDX名が埋め込まれている場合はMDXと同じディレクトリから検索し、拡張子が
省略されていれば`.PDX`を補って`LOADPCM`へ転送します。標準の96音色PDXによる
MSM6258単音再生に加え、EX-PDXによるPCM8の最大8音多重再生に対応します。

`--audio=live`または`--audio=wav:...`を指定すると、run68mpxは`TRAP #2`ベクタに
`PCM8`常駐シグネチャを公開し、MusashiのTRAP HLEからホスト側ミキサーへ接続します。
そのため、MXDRVでPCM8曲を再生する際に別途`PCM8.X`を常駐させる必要はありません。
現在の内蔵PCM8はMXDRVのPDXで使われる4-bit ADPCMレートに対応し、PCM8派生ドライバの
8-bit／16-bitリニアPCMモードは対象外です。

MusashiモードでもDOSCALL（`0xFFxx`）、FLOAT（`0xFExx`）、IOCSCALL
（`TRAP #15`）はrun68mpxのホスト実装へ接続されます。現在は互換性比較を
優先して1命令ごとに既存のレジスタ状態と同期するため、速度は今後の最適化
対象です。

標準出力と標準エラー出力のShift-JIS文字列は、対応環境ではUTF-8へ変換されます。

## ライセンス

GNU General Public License version 2（GPL-2.0）です。詳細は [LICENCE](LICENCE) を参照してください。

元プロジェクトと各移植・修正の作者、コントリビューターに感謝します。
YM2151エミュレーションにはAaron Giles氏のBSD 3-Clauseライセンスの
[ymfm](https://github.com/aaronsgiles/ymfm)を使用しています。

実験的MPUバックエンドには Karl Stenerud による
[Musashi](https://github.com/kstenerud/Musashi) を使用しています。取り込んだ
コミットとライセンスについては
[`third_party/musashi/UPSTREAM.md`](third_party/musashi/UPSTREAM.md) を参照してください。
ymfmの取り込み元については
[`third_party/ymfm/UPSTREAM.md`](third_party/ymfm/UPSTREAM.md) を参照してください。
