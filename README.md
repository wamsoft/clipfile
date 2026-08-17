# clipfile

吉里吉里Z (KirikiriZ) 用の CLIP STUDIO PAINT (`.clip`) 読み込み / 編集プラグイン。

CLIP のパース・合成・書き出しは [wamsoft/clipparse](https://github.com/wamsoft/clipparse)
が担い、このリポジトリはそれを submodule で取り込んで TJS2 へ公開する薄いラッパーです。
[psdfile](https://github.com/wamsoft/psdfile) (psdparse のラッパー) と対になる構成で、
`clip://` ストレージや `getLayerData` 系の名前・意味論を psdfile に揃えてあるので、
スクリプト側で PSD と CLIP を似た形で扱えます。

## できること

- `.clip` からレイヤ単位の画像を吉里吉里 `Layer` へ読み出す (`getLayerData` /
  `getLayerDataRaw` / `getLayerDataMask`)
- **重なる 256x256 タイルだけを展開する部分読み** (`getLayerRegion`) — CLIP は画素を
  タイルで持っているので、大きなレイヤの一部を取るのが安い
- レイヤツリー / 属性の参照 (`getLayerInfo` / `getRoots` / `getChildren`)
- 全レイヤの合成 (`getBlend`) — 合成モード 27 種・表現色 4 種・マスク・
  クリッピング・通過フォルダ・調整レイヤ 5 種に対応 (clipparse 側の実装)
- 埋め込みの完成画 (`getPreviewPNG`)
- `clip://` ストレージ経由でレイヤ画像を普通のファイルとしてロード
- 編集と書き出し (`CLIPWriter`) — レイヤ属性 / 画素の差し替え / レイヤ追加・削除 /
  キャンバスのリサイズ。無変更で書き戻すと元ファイルと sha256 が一致します

TJS2 の API 仕様は [manual.tjs](manual.tjs) が正本です。

## clip:// ストレージ

```
clip://CLIPファイル名/root/フォルダ名/.../レイヤ名.bmp
clip://CLIPファイル名/id/LayerId.bmp
```

- CLIP ファイル名はベース名のみ、小文字に正規化されます
- フォルダ名 / レイヤ名も小文字に正規化され、含まれる `/` は `_` に置換されます
- 名前が重複する場合は後にあるものが優先されます
- 画像を持たないレイヤ (フォルダ・調整レイヤ・空レイヤ) は列挙されません

## 使い方

```tjs
Plugins.link("clipfile.dll");

var clip = new CLIP();
if (clip.load("sample.clip")) {
    Debug.message("canvas: " + clip.width + "x" + clip.height);
    for (var i = 0; i < clip.layer_count; i++) {
        var info = clip.getLayerInfo(i);
        Debug.message(info.name + " " + info.width + "x" + info.height);
    }
    var lay = new Layer(window, null);
    clip.getLayerData(lay, 0);
}

// clip:// 経由で普通の画像として読む
var lay2 = new Layer(window, null);
lay2.loadImages("clip://sample.clip/root/背景/空.bmp");
```

編集して書き出す場合:

```tjs
var w = new CLIPWriter();
w.load("in.clip");
w.setLayerAttr(5, %[ opacity: 128, composite: CLIPWriter.composite_multiply ]);
w.save("out.clip");

// CSP で開く前に必ず検査する
var problems = CLIPWriter.validate("out.clip");
if (problems.count) Debug.message(problems.join("\n"));
```

`CLIPWriter` の `opacity` は **CLIP の生値 0..256** です (`CLIP.getLayerInfo()` の
`opacity` は PSD / 吉里吉里 に揃えた 0..255、生値は `opacity_raw`)。

`setPixels` に渡す `Layer` のサイズは **そのレイヤの 100% ミップの寸法** に
合わせる必要があります。これはキャンバスサイズとは限らず (CSP は 256x256 ブロック
境界へ切り上げることがあり、300x400 のキャンバスでも 512x512 になる)、
`CLIP.getLayerAttribute(レイヤ番号)` の `width` / `height` で調べられます。

## Clone

```bash
git clone --recursive <this repo>
# or:
git submodule update --init --recursive
```

## ビルド

要件: MSVC、CMake 3.16+、[vcpkg](https://vcpkg.io/) (`VCPKG_ROOT` 必須)、隣接の
`tp_stub` / `ncbind`、configure 時にネットワーク (clipparse が sqlite3
amalgamation をソースから取得します)。

```powershell
$env:VCPKG_ROOT = 'd:\vcpkg'

# Debug
make PRESET=x64-windows BUILD_TYPE=Debug build
# Release
make PRESET=x64-windows BUILD_TYPE=Release build
```

成果物は `build/x64-windows/{Debug,Release}/clipfile.dll`。

## ライセンス

同梱コンポーネントのライセンスは submodule 側 (clipparse / zlib / SQLite) を参照。
