# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

吉里吉里Z (KirikiriZ) 用の CLIP STUDIO PAINT (`.clip`) 読み込み/編集プラグイン `clipfile.dll`。
CLIP のパース/合成/書き出しコアは [wamsoft/clipparse](https://github.com/wamsoft/clipparse) が持っていて、
clipfile は **clipparse を submodule で取り込んで吉里吉里バインドを行う薄いラッパー**。
[psdfile](https://github.com/wamsoft/psdfile) (psdparse のラッパー) と対になる構成で、
`clip://` ストレージ・`getLayerData` 系・`getBlend` の名前や意味論を psdfile に揃えてある
(スクリプト側で PSD と CLIP を同じコードで扱いやすくするため)。

ユーザー向け TJS2 API は `manual.tjs` が正本。`NCB_REGISTER_CLASS(CLIP)` /
`NCB_REGISTER_CLASS(CLIPWriter)` の登録内容と齟齬が出ないように維持する。

## 構造

```
clipfile/                        ← このリポジトリ
├── CMakeLists.txt               clipfile.dll の build 設定
├── CMakePresets.json            x64-windows (kirikiri Debug/Release)、x86-windows
├── Makefile                     make PRESET=x64-windows BUILD_TYPE=Debug build
├── vcpkg.json                   zlib のみ (sqlite3 は clipparse が FetchContent で取る)
├── manual.tjs                   ★ TJS2 API 正本
├── clipclass.h                  CLIP クラスヘッダ + 共有小物の宣言
├── clipclass.cpp                CLIP クラス本体 (読み + clip:// ストレージ + 変換小物)
├── clipwriter.cpp               CLIPWriter クラス (clip::ClipWriter のバインド)
├── main.cpp                     CLIPStorage (clip:// ストレージ) の登録
└── external/
    └── clipparse/               ★ submodule (wamsoft/clipparse)
```

clipparse 側の C++ ライブラリ、Python バインディング、pytest、CLIP 形式の仕様
(`docs/CLIP_FORMAT.md`)、設計 (`docs/DESIGN.md`)、CSP 実機確認の記録
(`docs/WRITE_TEST*.md`)、CLIP⇄PSD 変換サンプル (`examples/clipconv`) は全部
[submodule 先](https://github.com/wamsoft/clipparse) を見ること。

## Clone 手順

```bash
git clone --recursive <this repo>
# or:
git submodule update --init --recursive
```

## ビルド

要件: MSVC、CMake 3.16+、[vcpkg](https://vcpkg.io/) (`VCPKG_ROOT` 必須)、隣接の
`tp_stub` / `ncbind` (`../tp_stub` / `../ncbind` を既定で見る。`TPSTUB_DIR` で上書き可)、
**configure 時にネットワーク** (clipparse が sqlite3 amalgamation を FetchContent で取る)。

```powershell
$env:VCPKG_ROOT = 'd:\vcpkg'
make PRESET=x64-windows BUILD_TYPE=Debug build
make PRESET=x64-windows BUILD_TYPE=Release build
```

成果物: `build/x64-windows/{Debug,Release}/clipfile.dll`

依存の供給:
- **zlib** — `vcpkg.json` の `zlib` を vcpkg static triplet で供給し、clipparse 側の
  `find_package(ZLIB QUIET)` がそれを拾う (FetchContent フォールバックは発動しない)。
- **sqlite3** — clipparse 側が amalgamation を必ず `FetchContent` で取ってきて
  `sqlite3_static` として同梱ビルドする (`find_package` を試さない)。configure に
  ネットワークが要るのはこれのせい。`SQLITE_ENABLE_DESERIALIZE` 前提で
  `sqlite3_deserialize` を使うので、システムの sqlite3 に差し替えるのは避ける。
- CLI (`clip_cli`) は吉里吉里 build では不要なので `CLIPPARSE_BUILD_CLI=OFF` を
  CMakeLists と CMakePresets の両方で落としてある。

**重要なハマりポイント:**
- clang LSP は tp_stub.h / ncbind.hpp / submodule 先のヘッダを解決できず大量の偽 error を出す。CMake/MSVC ビルドは通るので無視する。
- submodule を update してないと configure 時に `external/clipparse/CMakeLists.txt` が無いと言って fail する。`git submodule update --init --recursive` で修復。

## アーキテクチャ (吉里吉里側のみ)

```
CLIP : public clip::ClipFile        (clipclass.h / clipclass.cpp)
├── load(ttstr)     TVPGetPlacedPath → TVPGetLocallyAccessibleName が通れば
│                   clip::ClipFile::load(localpath) で **mmap 遅延読み**。
│                   通らない (アーカイブ内等) ならストリームから全体を owned_ へ
│                   読んで loadFromMemory。
├── loadOctet(oct)  octet を owned_ にコピーして loadFromMemory
├── clearData()     ストレージ登録解除 + clip::ClipFile::clear() + owned_ 解放
├── getLayerInfo()  → 辞書 (CLIP の生値と psdfile 互換値の両方を入れる)
├── getLayerData/Raw/Mask/Region  → 吉里吉里 Layer へ RGBA→BGRA で流し込む
├── getBlend()      clipparse 側で **実際に合成する** (PSD の getBlend が保存済み
│                   合成結果を読むだけなのとは違う)
└── clip:// ストレージ (CheckExistentStorage / GetListAt / openLayerImage)

CLIPWriter : public clip::ClipWriter   (clipwriter.cpp)
└── load / save / setLayerAttr / setPixels / addLayer / deleteLayer /
    dropThumbnail / setCanvasPreview / resizeCanvas / setSeed / validate(static)

CLIPStorage : iTVPStorageMedia         (main.cpp)
└── psdfile の PSDStorage と同型。clip:// のドメイン部でファイルを引き、
    弱参照マップ + 直近 1 件のキャッシュで CLIP インスタンスを共有する。
```

`clip::ClipFile` には `isLoaded` フラグが無い (`error()` しかない) ので、
CLIP 側で `isLoaded_` を持って管理している。`clip::ClipFile::clear()` は
**virtual ではない**ので、吉里吉里側の後始末は `CLIP::clearData()` に集め、
`load` / `loadOctet` / デストラクタから明示的に呼ぶ。

**`~CLIP()` の `clearData()` 明示呼び出しは外さないこと。** `loadOctet` /
アーカイブ経路では clipparse 側の `MemorySource` と SQLite が `owned_` (CLIP の
メンバ) のバイト列を指しているが、メンバは基底 (`~ClipFile` → `clear()`) より
**先に**破棄される。`clearData()` を先に走らせて縁を切らないと、sqlite の後始末が
解放済みメモリを触りうる。

### 画素の並びと単位の変換

clipparse で気をつける点が 3 つあり、変換は全部 `clipclass.cpp` に閉じている:

- **`clip::Image` は RGBA**、吉里吉里の `mainImageBuffer` は **BGRA**。
  読みは `storeImage()` / `openLayerImage()`、書きは `readLayerRGBA()` で入れ替える。
- **不透明度は CLIP が 0..256**、PSD / 吉里吉里 は 0..255。`getLayerInfo()` は
  正規化済みを `opacity`、生値を `opacity_raw` に入れる。`CLIPWriter.setLayerAttr` の
  `opacity` は **CLIP の生値 (0..256)** をそのまま渡す (manual.tjs に明記済み)。
- **合成モードは CLIP の LayerComposite** (実測同定 27 種)。`clipCompositeToTjsType()`
  が吉里吉里の `ltPs*` へ、`clipCompositeToPsdKey()` が PSD の 4CC へ写す。
  吉里吉里に対応が無いモード (減算 / 暗い色 / ビビッドライト / 色相 / 通過 / 除算 等)
  は `ltPsNormal` に落ちるので、正確に見たいスクリプトは `composite` (生値) を使う。
- **キャンバス寸法は `canvasWidth()/canvasHeight()`** を使う。`Canvas.CanvasWidth` は
  `CanvasUnit` 依存で mm 単位のファイルが実在するため使えない (clipparse が 100%
  ミップの Attribute から実ピクセルを求めている)。
- **レイヤの Offscreen 寸法 ≠ キャンバス寸法**。CSP はレイヤによって 256x256 ブロック
  境界へ切り上げた大きさで画素を持つ (実測: 300x400 のキャンバスで一部レイヤが
  512x512)。`CLIPWriter.setPixels` はこの Offscreen 寸法との一致を要求するので
  (不一致で false + `error` = "pixel size does not match the 100% mipmap")、
  スクリプト側は `CLIP.getLayerAttribute(no).width/height` で調べてから Layer を
  用意する。manual.tjs に手順つきで書いてある。読み側 (`getLayerData`) は
  `bounds` (キャンバス上の矩形) で返るのでこの差は見えない。
- **`ClipWriter::error()` は成功時にクリアされない**。戻り値が false のときだけ
  読むこと (manual.tjs に明記済み)。

### clipparse 固有の口

psdfile に対応物が無く、CLIP だから安く出せるもの:

- `getLayerRegion(layer, no, x, y, w, h, mode)` — CLIP は画素を 256x256 タイルで
  持つので**重なるタイルしか展開しない**部分読み。PSD は行 RLE なので同じことが
  できない。
- `getPreviewPNG()` — ファイルに埋まっている完成画 (`CanvasPreview`) の PNG。
  合成計算が要らないが等倍とは限らない。
- `check()` — 全ブロックの構造アサーション (診断用)。
- `CLIPWriter.validate(filename)` — 参照整合性の検査。**書いたら必ず通す。**

### 書く側の注意

「こちらのリーダでは読めるのに CSP が受け付けない」落とし穴 (格納型 BLOB/TEXT、
`Mipmap.MipmapCount`、`BlockCheckSum`、サムネイルの世代番号、`CanvasPreview`) は
clipparse 側が面倒を見る。参照整合性の崩れだけは `clip::validate` でしか出ないので、
`CLIPWriter.validate()` を通してから CSP で開くことを manual.tjs に明記してある。

`CLIPWriter::load` はアーカイブ内のファイルを受けない (書き戻し先を持てないため、
`TVPGetLocallyAccessibleName` が空なら例外)。読むだけなら `CLIP` 側が
アーカイブでも動く。

## 行儀よく避ける改変

- `manual.tjs` を更新せずに `NCB_REGISTER_CLASS` の登録だけ変更しない (manual.tjs が user-facing 正本)。
- clipparse 側の改修が必要なときは external/clipparse で submodule を編集 → clipparse 側で commit/push → clipfile 側で submodule ref を bump → clipfile commit、の順。clipfile で submodule のファイルを直接編集して放置しない。
- 吉里吉里依存 (tp_stub, ncbind, tjs_char, ttstr) を clipparse 側に滲ませない。submodule の独立性が崩れる。
- 不透明度の 0..256 / 0..255 と RGBA / BGRA の変換を新しい経路で書くときは、
  既存の `clipOpacityToTjs` / `storeImage` / `readLayerRGBA` を通す。片方だけ直すと
  静かに色が入れ替わる / 不透明度が 1 段ずれる。
- psdfile と名前を揃えている API (`getLayerData*` / `getBlend` / `clip://`) の
  意味論を勝手にずらさない。両プラグインを同じスクリプトで扱う前提の設計。

## TJS2 API 変更時の手順

1. `clipclass.cpp` (書き側なら `clipwriter.cpp`) で実装変更
2. 同ファイル末尾の `NCB_REGISTER_CLASS` で公開
   (省略引数があるものは ncbind の `RawCallback`。`NCB_METHOD` は既定値を扱えない)
3. `manual.tjs` に同じシグネチャを追記
4. ビルド (`make PRESET=x64-windows BUILD_TYPE=Debug build`) で smoke 確認
