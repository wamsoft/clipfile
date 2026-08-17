// ============================================================================
// CLIPWriter — clip::ClipWriter の吉里吉里バインド
//
// 読み側 (CLIP) と別クラスにしてある。clipparse 側で ClipWriter::load が
// ファイル全体をメモリへ読む (mmap しない) 設計になっていて、同じパスへ
// 書き戻せるようにしているため、読み側とライフサイクルが違う。
//
// **CSP が受け付けるための作法は clipparse 側が面倒を見る** (格納型 / MipmapCount /
// チェックサム / サムネイルの世代番号 / CanvasPreview)。書いたファイルは
// CLIPWriter.validate() を通してから CSP で開くこと — ここに引っかかる種類の
// 間違いは寛容なリーダでは読めてしまうのに CSP では落ちたり全面透明になる。
// ============================================================================

#include <ncbind.hpp>
#include "clipclass.h"
#include "clipwrite.h"
#include <string>
#include <vector>

class CLIPWriter : public clip::ClipWriter
{
public:
	CLIPWriter() {}

	/**
	 * 編集元の CLIP ファイルを読む (mmap しないので同じパスへ書き戻せる)。
	 * @param filename ファイル名 (ローカルパスに落ちる必要がある)
	 * @return 成功したら true
	 */
	bool load(ttstr filename) {
		ttstr file = TVPGetPlacedPath(filename);
		if (!file.length()) file = filename;
		ttstr local = TVPGetLocallyAccessibleName(file);
		if (!local.length()) {
			// アーカイブ内などは編集元にできない (書き戻し先を持てないため)。
			TVPThrowExceptionMessage(TJS_W("clip writer needs a local file: %1"), filename);
		}
		std::string path = tjsToUtf8(local);
		return clip::ClipWriter::load(path.c_str());
	}

	/**
	 * 現在の状態でファイルを組み立てて書き出す。
	 * @param filename 保存先 (ローカルパス)
	 * @return 書いたバイト数。失敗で 0
	 */
	tjs_int64 save(ttstr filename) {
		ttstr local = filename;
		TVPGetLocalName(local);
		std::string path = tjsToUtf8(local);
		return (tjs_int64)clip::ClipWriter::save(path.c_str());
	}

	void clearData() { clip::ClipWriter::clear(); }

	/** 直近の失敗理由 */
	ttstr get_error() { return utf8ToTjs(error()); }
	/** 外部チャンク (CHNKExta) の数 */
	int get_external_count() { return (int)externalCount(); }

	/**
	 * レイヤ属性を編集する。attr 辞書のうち指定されたキーだけ変更する。
	 *   %[ name:, opacity:(0..256), visibility:(0/1), composite:, clipping:,
	 *      folder: ]
	 * @param layerId CLIP の Layer.MainId (CLIP.getLayerInfo().layer_id)
	 * @return 成功したら true
	 */
	bool setLayerAttr(int layerId, tTJSVariant attr) {
		if (attr.Type() != tvtObject)
			TVPThrowExceptionMessage(TJS_W("attr must be a dictionary"));
		ncbPropAccessor a(attr);
		clip::ClipWriter::LayerAttr la;
		// name は const char* を保持するので、寿命のある文字列を用意しておく。
		std::string name;
		if (a.HasValue(TJS_W("name"))) {
			name = tjsToUtf8(a.getStrValue(TJS_W("name")));
			la.name = name.c_str();
		}
		if (a.HasValue(TJS_W("opacity")))    la.opacity    = (tjs_int)a.getIntValue(TJS_W("opacity"));
		if (a.HasValue(TJS_W("visibility"))) la.visibility = a.getIntValue(TJS_W("visibility")) ? 1 : 0;
		if (a.HasValue(TJS_W("composite")))  la.composite  = (tjs_int)a.getIntValue(TJS_W("composite"));
		if (a.HasValue(TJS_W("clipping")))   la.clipping   = (tjs_int)a.getIntValue(TJS_W("clipping"));
		if (a.HasValue(TJS_W("folder")))     la.folder     = (tjs_int)a.getIntValue(TJS_W("folder"));
		return clip::ClipWriter::setLayerAttr((int64_t)layerId, la);
	}

	/**
	 * レイヤの画素を Layer オブジェクトの内容で差し替える。
	 * Layer はキャンバス全面 (100% ミップと同じ寸法) である必要がある。
	 * @param layerId CLIP の Layer.MainId
	 * @param layer   画素供給元 Layer
	 * @return 成功したら true
	 */
	bool setPixels(int layerId, tTJSVariant layer) {
		int w, h;
		tjs_uint8 *rgba = readLayerRGBA(layer, w, h);
		if (!rgba) return false;
		bool r = clip::ClipWriter::setPixels((int64_t)layerId, rgba,
		                                     (uint32_t)w, (uint32_t)h);
		delete[] rgba;
		return r;
	}

	/**
	 * 既存レイヤを雛形にレイヤを追加する。
	 * @param copyFrom 雛形にする既存レイヤの Layer.MainId
	 * @param name     新しいレイヤ名
	 * @param layer    画素供給元 Layer (void で全面透明)
	 * @param after    この Layer.MainId の直上に挿す。負値で最上段
	 * @param parent   親フォルダの Layer.MainId。0 でルート直下
	 * @return 新しいレイヤの Layer.MainId。失敗で 0
	 */
	tjs_int64 addLayer(ttstr name, int copyFrom, tTJSVariant layer,
	                   int after, int parent) {
		std::string u8 = tjsToUtf8(name);
		int w = 0, h = 0;
		tjs_uint8 *rgba = 0;
		if (layer.Type() == tvtObject) rgba = readLayerRGBA(layer, w, h);
		int64_t id = clip::ClipWriter::addLayer((int64_t)copyFrom, u8, rgba,
		                                       (uint32_t)w, (uint32_t)h,
		                                       (int64_t)after, (int64_t)parent);
		delete[] rgba;
		return (tjs_int64)id;
	}

	/** レイヤを削除する */
	bool deleteLayer(int layerId) {
		return clip::ClipWriter::deleteLayer((int64_t)layerId);
	}

	/** サムネイルの実体だけ落として世代番号を立てる (CSP が作り直す) */
	bool dropThumbnail(int layerId) {
		return clip::ClipWriter::dropThumbnail((int64_t)layerId);
	}

	/**
	 * 開いた直後に表示される画像 (CanvasPreview) を差し替える。
	 * @param layer 画素供給元 Layer
	 * @return 成功したら true
	 */
	bool setCanvasPreview(tTJSVariant layer) {
		int w, h;
		tjs_uint8 *rgba = readLayerRGBA(layer, w, h);
		if (!rgba) return false;
		bool r = clip::ClipWriter::setCanvasPreview(rgba, (uint32_t)w, (uint32_t)h);
		delete[] rgba;
		return r;
	}

	/**
	 * キャンバスの寸法ごと作り替える (ミップ連鎖を伸縮させる)。
	 * 画素の実体は全部落ちるので、呼び出し側で setPixels で入れ直すこと。
	 * @param width  新しい幅
	 * @param height 新しい高さ
	 * @param dpi    解像度 (0 で据え置き)
	 * @return 成功したら true
	 */
	bool resizeCanvas(int width, int height, double dpi) {
		return clip::ClipWriter::resizeCanvas((uint32_t)width, (uint32_t)height, dpi);
	}

	/** 外部 ID の乱数種を固定する (テストで再現性が要るとき) */
	void setSeed(int seed) {
		setExternalIdSeed((uint64_t)(tjs_uint32)seed);
	}

	/**
	 * static: 参照整合性を検査する。**CSP で開く前に必ず通すこと**。
	 * ミップ段数 / 閉路 / 孤児行 / 格納型 / 消えたレイヤへの参照を見る。
	 * @param filename 検査するファイル
	 * @return 問題点の文字列配列 (空配列なら OK)
	 */
	static tTJSVariant validate(ttstr filename) {
		ttstr local = filename;
		TVPGetLocalName(local);
		std::string path = tjsToUtf8(local);
		std::vector<std::string> problems;
		clip::validate(path.c_str(), problems);
		tTJSVariant result;
		ncbArrayAccessor arr;
		if (arr.IsValid()) {
			for (int i = 0; i < (int)problems.size(); i++)
				arr.SetValue((tjs_int32)i, utf8ToTjs(problems[(size_t)i]));
			result = arr;
		}
		return result;
	}
};

// addLayer(name, copyFrom, layer=void, after=-1, parent=0) /
// resizeCanvas(w, h, dpi=0) の省略引数用 RawCallback。
static tjs_error AddLayer(tTJSVariant *r, tjs_int numparams,
                          tTJSVariant **params, CLIPWriter *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	ttstr name    = *params[0];
	int   copyFrom = (tjs_int)*params[1];
	tTJSVariant layer = numparams > 2 ? *params[2] : tTJSVariant();
	int   after   = numparams > 3 ? (tjs_int)*params[3] : -1;
	int   parent  = numparams > 4 ? (tjs_int)*params[4] : 0;
	tjs_int64 id = instance->addLayer(name, copyFrom, layer, after, parent);
	if (r) *r = (tjs_int64)id;
	return TJS_S_OK;
}

static tjs_error ResizeCanvas(tTJSVariant *r, tjs_int numparams,
                              tTJSVariant **params, CLIPWriter *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	int    w   = (tjs_int)*params[0];
	int    h   = (tjs_int)*params[1];
	double dpi = numparams > 2 ? (double)(tjs_real)*params[2] : 0.0;
	bool ok = instance->resizeCanvas(w, h, dpi);
	if (r) *r = ok;
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(CLIPWriter) {

	Constructor();

	// CLIP の LayerComposite (setLayerAttr の composite に渡す値)。
	// 実測同定された 27 種。詳細は clipparse の docs/CLIP_FORMAT.md §9。
	Variant("composite_normal",        0);
	Variant("composite_darken",        1);
	Variant("composite_multiply",      2);
	Variant("composite_color_burn",    3);
	Variant("composite_linear_burn",   4);
	Variant("composite_subtract",      5);
	Variant("composite_darker_color",  6);
	Variant("composite_lighten",       7);
	Variant("composite_screen",        8);
	Variant("composite_color_dodge",   9);
	Variant("composite_dodge_glow",   10);
	Variant("composite_add",          11);
	Variant("composite_add_glow",     12);
	Variant("composite_lighter_color",13);
	Variant("composite_overlay",      14);
	Variant("composite_soft_light",   15);
	Variant("composite_hard_light",   16);
	Variant("composite_vivid_light",  17);
	Variant("composite_linear_light", 18);
	Variant("composite_pin_light",    19);
	Variant("composite_hard_mix",     20);
	Variant("composite_difference",   21);
	Variant("composite_exclusion",    22);
	Variant("composite_hue",          23);
	Variant("composite_saturation",   24);
	Variant("composite_color",        25);
	Variant("composite_luminosity",   26);
	Variant("composite_pass_through", 30);
	Variant("composite_divide",       36);

	NCB_METHOD(load);
	NCB_METHOD(save);
	NCB_METHOD(clearData);

	Property(TJS_W("error"),          &Class::get_error,          (int)0);
	Property(TJS_W("external_count"), &Class::get_external_count, (int)0);

	NCB_METHOD(setLayerAttr);
	NCB_METHOD(setPixels);
	RawCallback("addLayer", &AddLayer, 0);
	NCB_METHOD(deleteLayer);
	NCB_METHOD(dropThumbnail);
	NCB_METHOD(setCanvasPreview);
	RawCallback("resizeCanvas", &ResizeCanvas, 0);
	NCB_METHOD(setSeed);

	NCB_METHOD(validate);
};
