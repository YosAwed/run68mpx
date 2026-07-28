# Upstream repositories

run68mpxは、共通のrun68を祖先に持つ次の2系統をもとにしています。

| ローカルremote | リポジトリ | 用途 |
| --- | --- | --- |
| `upstream-windows` | `https://github.com/YosAwed/run68Win10VS2022.git` | Windows / Visual Studio系統の修正を参照する |
| `upstream-macos` | `https://github.com/GOROman/run68mac.git` | macOS移植とクロスプラットフォーム修正を参照する |

元リポジトリへの誤pushを防ぐため、この作業コピーでは両remoteのpush URLを無効にしています。新しい公開先は `origin` として別に登録します。

## 統合方針

1. `main` は `GOROman/run68mac` の最新版を基準とする。
2. `YosAwed/run68Win10VS2022` の独自コミットと履歴を保持する。
3. Visual Studioのキャッシュ（`.vs/`、データベース、プリコンパイル済みヘッダー）は取り込まない。
4. Windows版の動作修正は、macOS版ですでに解決済みか確認し、必要な変更だけをテスト付きで移植する。
5. GPL-2.0のライセンスと既存作者の履歴を維持する。

## 更新の確認

```sh
git fetch upstream-macos master
git fetch upstream-windows master
git log --left-right --cherry-pick --oneline \
  upstream-windows/master...upstream-macos/master
```

upstreamは参照用です。変更はrun68mpxのブランチで行い、元リポジトリへ直接pushしません。
