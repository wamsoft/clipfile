#include <ncbind.hpp>
#include "clipclass.h"
#include <string>
#include <vector>

#define BMPEXT TJS_W(".bmp")

// ============================================================================
// 共有小物
// ============================================================================

// UTF-8 (clipparse は名前を UTF-8 の std::string で持つ) → ttstr。
// CLIP は PSD と違い pascal 名 / luni 名の二重化が無く、常に UTF-8 の 1 本。
ttstr utf8ToTjs(const std::string &s)
{
	static_assert(sizeof(tjs_char) == sizeof(char16_t),
	              "tjs_char must be 16-bit UTF-16 code unit");
	std::vector<tjs_char> out;
	out.reserve(s.size());
	size_t i = 0, n = s.size();
	while (i < n) {
		tjs_uint8 c = (tjs_uint8)s[i];
		tjs_uint32 cp;
		int extra;
		if (c < 0x80)        { cp = c;        extra = 0; }
		else if (c < 0xc0)   { i++; continue; }            // 不正な継続バイトは捨てる
		else if (c < 0xe0)   { cp = c & 0x1f; extra = 1; }
		else if (c < 0xf0)   { cp = c & 0x0f; extra = 2; }
		else                 { cp = c & 0x07; extra = 3; }
		if (i + (size_t)extra >= n) break;
		for (int k = 1; k <= extra; k++)
			cp = (cp << 6) | ((tjs_uint8)s[i + k] & 0x3f);
		i += (size_t)extra + 1;
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out.push_back((tjs_char)(0xd800 + (cp >> 10)));
			out.push_back((tjs_char)(0xdc00 + (cp & 0x3ff)));
		} else {
			out.push_back((tjs_char)cp);
		}
	}
	if (out.empty()) return ttstr();
	return ttstr(&out[0], (tjs_int)out.size());
}

std::string tjsToUtf8(const ttstr &s)
{
	std::string out;
	const tjs_char *p = s.c_str();
	tjs_int n = s.length();
	for (tjs_int i = 0; i < n; i++) {
		tjs_uint32 cp = (tjs_uint16)p[i];
		if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < n) {
			tjs_uint32 lo = (tjs_uint16)p[i + 1];
			if (lo >= 0xdc00 && lo <= 0xdfff) {
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				i++;
			}
		}
		if (cp < 0x80) {
			out.push_back((char)cp);
		} else if (cp < 0x800) {
			out.push_back((char)(0xc0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else if (cp < 0x10000) {
			out.push_back((char)(0xe0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else {
			out.push_back((char)(0xf0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
	}
	return out;
}

// CLIP の LayerComposite (実測同定された 27 種。clipparse の
// docs/CLIP_FORMAT.md §9) → 吉里吉里の ltPs* 合成モード。
// 吉里吉里に対応が無いモードは ltPsNormal に落とす (生の値は
// getLayerInfo().composite で見られる)。
int clipCompositeToTjsType(int composite)
{
	switch (composite) {
	case 0:  return ltPsNormal;          // 通常
	case 1:  return ltPsDarken;          // 比較(暗)
	case 2:  return ltPsMultiplicative;  // 乗算
	case 3:  return ltPsColorBurn;       // 焼き込みカラー
	case 4:  return ltPsSubtractive;     // 焼き込み(リニア)
	case 7:  return ltPsLighten;         // 比較(明)
	case 8:  return ltPsScreen;          // スクリーン
	case 9:  return ltPsColorDodge;      // 覆い焼きカラー
	case 10: return ltPsColorDodge;      // 覆い焼き(発光) — 近似
	case 11: return ltPsAdditive;        // 加算
	case 12: return ltPsAdditive;        // 加算(発光) — 近似
	case 14: return ltPsOverlay;         // オーバーレイ
	case 15: return ltPsSoftLight;       // ソフトライト
	case 16: return ltPsHardLight;       // ハードライト
	case 21: return ltPsDifference;      // 差の絶対値
	case 22: return ltPsExclusion;       // 除外
	// 5=減算 6=暗い色 13=明るい色 17=ビビッドライト 18=リニアライト
	// 19=ピンライト 20=ハードミックス 23=色相 24=彩度 25=カラー 26=輝度
	// 30=通過 36=除算 は吉里吉里に対応が無い
	default: return ltPsNormal;
	}
}

// CLIP の LayerComposite → PSD の 4CC 文字列。psdfile の
// getLayerInfo().blend_mode_key と同じ表記で扱いたいとき用。
// 10 と 12 は PSD に対応が無いので近い 9 / 11 相当へ落ちる。
ttstr clipCompositeToPsdKey(int composite)
{
	static const struct { int clip; const tjs_char *key; } tbl[] = {
		{  0, TJS_W("norm") }, {  1, TJS_W("dark") }, {  2, TJS_W("mul ") },
		{  3, TJS_W("idiv") }, {  4, TJS_W("lbrn") }, {  5, TJS_W("fsub") },
		{  6, TJS_W("dkCl") }, {  7, TJS_W("lite") }, {  8, TJS_W("scrn") },
		{  9, TJS_W("div ") }, { 10, TJS_W("div ") }, { 11, TJS_W("lddg") },
		{ 12, TJS_W("lddg") }, { 13, TJS_W("lgCl") }, { 14, TJS_W("over") },
		{ 15, TJS_W("sLit") }, { 16, TJS_W("hLit") }, { 17, TJS_W("vLit") },
		{ 18, TJS_W("lLit") }, { 19, TJS_W("pLit") }, { 20, TJS_W("hMix") },
		{ 21, TJS_W("diff") }, { 22, TJS_W("smud") }, { 23, TJS_W("hue ") },
		{ 24, TJS_W("sat ") }, { 25, TJS_W("colr") }, { 26, TJS_W("lum ") },
		{ 30, TJS_W("pass") }, { 36, TJS_W("fdiv") },
	};
	for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
		if (tbl[i].clip == composite) return ttstr(tbl[i].key);
	return ttstr(TJS_W("norm"));
}

// CLIP の不透明度は 0..256、吉里吉里 / PSD は 0..255。
static int clipOpacityToTjs(int opacity)
{
	int v = opacity * 255 / 256;
	return v > 255 ? 255 : (v < 0 ? 0 : v);
}

// 吉里吉里 Layer の mainImageBuffer (BGRA) を RGBA へ並べ替えて tight-pack で
// 取り出す (clipparse の Image は RGBA)。戻り値は呼び出し側で delete[]。
tjs_uint8 *readLayerRGBA(tTJSVariant &layer, int &width, int &height)
{
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	ncbPropAccessor obj(layer);
	width  = (int)obj.GetValue(TJS_W("imageWidth"),  ncbTypedefs::Tag<tjs_int>());
	height = (int)obj.GetValue(TJS_W("imageHeight"), ncbTypedefs::Tag<tjs_int>());
	if (width <= 0 || height <= 0) return 0;
	const tjs_uint8 *src = (const tjs_uint8 *)(tjs_intptr_t)
		obj.GetValue(TJS_W("mainImageBuffer"), ncbTypedefs::Tag<tjs_intptr_t>());
	tjs_int pitch = (tjs_int)obj.GetValue(TJS_W("mainImageBufferPitch"),
	                                      ncbTypedefs::Tag<tjs_int>());
	if (!src) return 0;
	tjs_uint8 *buf = new tjs_uint8[(size_t)width * height * 4];
	for (int y = 0; y < height; y++) {
		const tjs_uint8 *s = src + (tjs_intptr_t)y * pitch;
		tjs_uint8 *d = buf + (size_t)y * width * 4;
		for (int x = 0; x < width; x++, s += 4, d += 4) {
			d[0] = s[2];  // R ← B
			d[1] = s[1];  // G
			d[2] = s[0];  // B ← R
			d[3] = s[3];  // A
		}
	}
	return buf;
}

// ============================================================================
// CLIP
// ============================================================================

CLIP::CLIP(iTJSDispatch2 *objthis)
	: objthis(objthis)
	, isLoaded_(false)
	, storageStarted(false)
{
}

CLIP::~CLIP()
{
	// clearData() をここで呼ぶ必要がある。loadFromMemory 経路では clipparse 側の
	// MemorySource / SQLite が owned_ のバイト列を指しているが、メンバ (owned_) は
	// 基底 (~ClipFile → clear()) より先に破棄されるので、そのままだと sqlite の
	// 後始末が解放済みメモリを触りうる。clear() を先に走らせて縁を切る。
	clearData();
}

tjs_error
CLIP::factory(CLIP **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis)
{
	*result = new CLIP(objthis);
	return S_OK;
}

tTJSVariant
CLIP::getSelf()
{
	return tTJSVariant(objthis, objthis);
}

ttstr
CLIP::get_error()
{
	return utf8ToTjs(error());
}

bool
CLIP::finishLoad(bool ok)
{
	isLoaded_ = ok;
	invalidateStorageCache();
	if (!ok) owned_.clear();
	return ok;
}

bool
CLIP::load(ttstr filename)
{
	clearData();

	ttstr file = TVPGetPlacedPath(filename);
	if (!file.length()) return false;

	// ローカルに実体があるなら mmap 経路を使う (clipparse の遅延読みが最大限
	// 効く。ファイル全体をメモリに載せない)。
	ttstr local = TVPGetLocallyAccessibleName(file);
	if (local.length()) {
		std::string path = tjsToUtf8(local);
		if (finishLoad(clip::ClipFile::load(path.c_str()))) {
			addToStorage(filename);
			return true;
		}
		return false;
	}

	// アーカイブ内などローカルパスに落ちない場合は、ストリームから全体を
	// メモリへ読んで loadFromMemory へ渡す。loadFromMemory はバイト列を
	// 所有しないので owned_ で保持する。
	iTJSBinaryStream *stream = TVPCreateStream(file, TJS_BS_READ);
	if (!stream) return false;
	bool ok = false;
	try {
		tjs_uint64 size = stream->GetSize();
		if (size > 0 && size <= (tjs_uint64)0x7fffffff) {
			owned_.resize((size_t)size);
			stream->Seek(0, TJS_BS_SEEK_SET);
			if (stream->Read(&owned_[0], (tjs_uint)size) == (tjs_uint)size)
				ok = clip::ClipFile::loadFromMemory(&owned_[0], (int64_t)size);
		}
	} catch (...) {
		stream->Destruct();
		throw;
	}
	stream->Destruct();
	if (finishLoad(ok)) {
		addToStorage(filename);
		return true;
	}
	return false;
}

bool
CLIP::loadOctet(tTJSVariant data)
{
	if (data.Type() != tvtOctet) TVPThrowExceptionMessage(TJS_W("not octet"));
	tTJSVariantOctet *oct = data.AsOctetNoAddRef();
	if (!oct) return false;
	clearData();
	tjs_uint len = oct->GetLength();
	if (!len) return false;
	// loadFromMemory は所有しないので、こちら側にコピーを持つ。
	owned_.assign(oct->GetData(), oct->GetData() + len);
	// ファイル名を持たないので clip:// への登録はしない。
	return finishLoad(clip::ClipFile::loadFromMemory(&owned_[0], (int64_t)len));
}

void
CLIP::clearData()
{
	removeFromStorage();
	invalidateStorageCache();
	isLoaded_ = false;
	clip::ClipFile::clear();
	owned_.clear();
}

void
CLIP::checkLayerNo(int no)
{
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	if (no < 0 || no >= (int)layers().size())
		TVPThrowExceptionMessage(TJS_W("not such layer"));
}

ttstr
CLIP::getLayerName(int no)
{
	checkLayerNo(no);
	return utf8ToTjs(layers()[(size_t)no].name);
}

tTJSVariant
CLIP::getLayerInfo(int no)
{
	checkLayerNo(no);
	const clip::LayerInfo &lay = layers()[(size_t)no];
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;

	dict.SetValue(TJS_W("left"),   lay.bounds.x);
	dict.SetValue(TJS_W("top"),    lay.bounds.y);
	dict.SetValue(TJS_W("right"),  lay.bounds.x + lay.bounds.w);
	dict.SetValue(TJS_W("bottom"), lay.bounds.y + lay.bounds.h);
	dict.SetValue(TJS_W("width"),  lay.bounds.w);
	dict.SetValue(TJS_W("height"), lay.bounds.h);

	dict.SetValue(TJS_W("name"),     utf8ToTjs(lay.name));
	dict.SetValue(TJS_W("layer_id"), (tjs_int)lay.mainId);
	dict.SetValue(TJS_W("visible"),  lay.visibility != 0);
	// CLIP の不透明度は 0..256。PSD / 吉里吉里 と揃えた 0..255 を opacity に、
	// 生の値を opacity_raw に入れる。
	dict.SetValue(TJS_W("opacity"),     clipOpacityToTjs((int)lay.opacity));
	dict.SetValue(TJS_W("opacity_raw"), (tjs_int)lay.opacity);
	dict.SetValue(TJS_W("clipping"),    lay.clipping ? 1 : 0);

	// 合成モード: composite が CLIP の生値、type が吉里吉里の ltPs*、
	// blend_mode_key が PSD 相当の 4CC (psdfile と見比べるとき用)。
	dict.SetValue(TJS_W("composite"),      (tjs_int)lay.composite);
	dict.SetValue(TJS_W("type"),           clipCompositeToTjsType((int)lay.composite));
	dict.SetValue(TJS_W("blend_mode_key"), clipCompositeToPsdKey((int)lay.composite));

	dict.SetValue(TJS_W("is_group"),  lay.isGroup);
	dict.SetValue(TJS_W("is_filter"), lay.isFilter);
	dict.SetValue(TJS_W("is_text"),   lay.isText);
	dict.SetValue(TJS_W("has_mask"),  lay.hasMask);
	// folder は bit0=フォルダ / bit4=折り畳み の生値。
	dict.SetValue(TJS_W("folder"),    (tjs_int)lay.folder);
	dict.SetValue(TJS_W("collapsed"), (lay.folder & 16) != 0);
	// type(LayerType) の生値。bit12(4096)=調整レイヤ / bit1(2)=マスク有。
	dict.SetValue(TJS_W("layer_type_raw"), (tjs_int)lay.type);

	dict.SetValue(TJS_W("parent_index"), lay.parent);
	ncbArrayAccessor kids;
	if (kids.IsValid()) {
		for (int i = 0; i < (int)lay.children.size(); i++)
			kids.SetValue((tjs_int32)i, lay.children[(size_t)i]);
		dict.SetValue(TJS_W("children"), kids.GetDispatch());
	}

	result = dict;
	return result;
}

tTJSVariant
CLIP::getRoots()
{
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		const std::vector<int> &r = roots();
		for (int i = 0; i < (int)r.size(); i++)
			arr.SetValue((tjs_int32)i, r[(size_t)i]);
		result = arr;
	}
	return result;
}

tTJSVariant
CLIP::getChildren(int no)
{
	checkLayerNo(no);
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		const std::vector<int> &c = layers()[(size_t)no].children;
		for (int i = 0; i < (int)c.size(); i++)
			arr.SetValue((tjs_int32)i, c[(size_t)i]);
		result = arr;
	}
	return result;
}

int
CLIP::getLayerIndexById(int layerId)
{
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	return layerIndex((int64_t)layerId);
}

tTJSVariant
CLIP::getLayerAttribute(int no, bool mask)
{
	checkLayerNo(no);
	tTJSVariant result;
	int64_t off = topOffscreen(layers()[(size_t)no].mainId, mask);
	if (!off) return result;
	const clip::OffscreenAttr *a = attribute(off);
	if (!a) return result;
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;
	dict.SetValue(TJS_W("width"),        (tjs_int)a->width);
	dict.SetValue(TJS_W("height"),       (tjs_int)a->height);
	dict.SetValue(TJS_W("cols"),         (tjs_int)a->cols);
	dict.SetValue(TJS_W("rows"),         (tjs_int)a->rows);
	dict.SetValue(TJS_W("color_mode"),   (tjs_int)a->colorMode);
	dict.SetValue(TJS_W("num_channels"), (tjs_int)a->numChannels);
	dict.SetValue(TJS_W("bit_depth"),    (tjs_int)a->bitDepth);
	dict.SetValue(TJS_W("plane_bytes"),  (tjs_int)a->planeBytes);
	dict.SetValue(TJS_W("block_width"),  (tjs_int)a->blockWidth);
	dict.SetValue(TJS_W("block_height"), (tjs_int)a->blockHeight);
	dict.SetValue(TJS_W("has_init_color"), a->hasInitColor);
	dict.SetValue(TJS_W("init_color"),   tTJSVariant((tjs_int64)a->initColor));
	dict.SetValue(TJS_W("block_count"),  (tjs_int)a->blockSizes.size());
	result = dict;
	return result;
}

// clip::Image (RGBA8 ストレートアルファ) を吉里吉里 Layer へ流し込む。
void
CLIP::storeImage(tTJSVariant layer, const clip::Image &img,
                 int left, int top, int opacity, int type, bool visible,
                 const ttstr &name)
{
	ncbPropAccessor obj(layer);
	int w = (int)img.width, h = (int)img.height;
	obj.SetValue(TJS_W("left"),   left);
	obj.SetValue(TJS_W("top"),    top);
	obj.SetValue(TJS_W("opacity"), opacity);
	obj.SetValue(TJS_W("type"),    type);
	obj.SetValue(TJS_W("visible"), visible);
	obj.SetValue(TJS_W("width"),   w);
	obj.SetValue(TJS_W("height"),  h);
	obj.SetValue(TJS_W("imageLeft"),   0);
	obj.SetValue(TJS_W("imageTop"),    0);
	obj.SetValue(TJS_W("imageWidth"),  w);
	obj.SetValue(TJS_W("imageHeight"), h);
	obj.SetValue(TJS_W("name"), name);

	tjs_uint8 *buffer = (tjs_uint8 *)(tjs_intptr_t)
		obj.GetValue(TJS_W("mainImageBufferForWrite"), ncbTypedefs::Tag<tjs_intptr_t>());
	tjs_int pitch = (tjs_int)obj.GetValue(TJS_W("mainImageBufferPitch"),
	                                      ncbTypedefs::Tag<tjs_int>());
	if (!buffer) return;
	// RGBA → BGRA。
	for (int y = 0; y < h; y++) {
		const tjs_uint8 *s = &img.rgba[(size_t)y * w * 4];
		tjs_uint8 *d = buffer + (tjs_intptr_t)y * pitch;
		for (int x = 0; x < w; x++, s += 4, d += 4) {
			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
			d[3] = s[3];
		}
	}
}

void
CLIP::_getLayerData(tTJSVariant layer, int no, clip::ImageMode mode)
{
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	checkLayerNo(no);
	const clip::LayerInfo &lay = layers()[(size_t)no];

	clip::Image img;
	if (!layerImage(no, mode, img) || img.width == 0 || img.height == 0) {
		// フォルダ / 調整レイヤ / マスクを持たないレイヤの mask 取得などは
		// 空 (0x0) になる。吉里吉里 Layer は 0 サイズにできないので何もしない。
		return;
	}
	storeImage(layer, img, lay.bounds.x, lay.bounds.y,
	           mode == clip::IMAGE_MODE_MASK ? 255 : clipOpacityToTjs((int)lay.opacity),
	           mode == clip::IMAGE_MODE_MASK ? ltPsNormal
	                                         : clipCompositeToTjsType((int)lay.composite),
	           lay.visibility != 0, utf8ToTjs(lay.name));
}

void CLIP::getLayerData(tTJSVariant layer, int no)    { _getLayerData(layer, no, clip::IMAGE_MODE_MASKED); }
void CLIP::getLayerDataRaw(tTJSVariant layer, int no) { _getLayerData(layer, no, clip::IMAGE_MODE_IMAGE); }
void CLIP::getLayerDataMask(tTJSVariant layer, int no){ _getLayerData(layer, no, clip::IMAGE_MODE_MASK); }

void
CLIP::getLayerRegion(tTJSVariant layer, int no, int x, int y, int w, int h, int mode)
{
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	checkLayerNo(no);
	if (w <= 0 || h <= 0) TVPThrowExceptionMessage(TJS_W("invalid region size"));
	const clip::LayerInfo &lay = layers()[(size_t)no];

	clip::Rect r;
	r.x = x; r.y = y; r.w = w; r.h = h;
	clip::Image img;
	if (!layerRegion(no, r, (clip::ImageMode)mode, img)
	    || img.width == 0 || img.height == 0) {
		return;
	}
	storeImage(layer, img, x, y,
	           mode == clip::IMAGE_MODE_MASK ? 255 : clipOpacityToTjs((int)lay.opacity),
	           mode == clip::IMAGE_MODE_MASK ? ltPsNormal
	                                         : clipCompositeToTjsType((int)lay.composite),
	           lay.visibility != 0, utf8ToTjs(lay.name));
}

bool
CLIP::getBlend(tTJSVariant layer)
{
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	clip::Image img;
	if (!mergedImage(img) || img.width == 0 || img.height == 0) return false;
	storeImage(layer, img, 0, 0, 255, ltPsNormal, true, ttstr());
	return true;
}

tTJSVariant
CLIP::getPreviewPNG()
{
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	std::vector<tjs_uint8> png;
	int w = 0, h = 0;
	if (!previewPng(png, w, h) || png.empty()) return result;
	return tTJSVariant(&png[0], (tjs_uint)png.size());
}

tTJSVariant
CLIP::check()
{
	if (!isLoaded_) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	std::string report;
	if (checkAll(&report)) return result;   // 問題なしは void
	return tTJSVariant(utf8ToTjs(report));
}

// ============================================================================
// clip:// ストレージ用のレイヤ名 / パス管理
//
// psdfile の psd:// と同じ規則:
//   clip://ファイル名/root/フォルダ名/.../レイヤ名.bmp
//   clip://ファイル名/id/LayerId.bmp
// 名前は小文字に正規化し、含まれる "/" は "_" に置換する。
// ============================================================================

ttstr
CLIP::path_layname(const clip::LayerInfo &lay)
{
	ttstr ret = utf8ToTjs(lay.name);
	ttstr from = "/";
	ttstr to   = "_";
	ret.Replace(from, to, true);
	ret.ToLowerCase();
	return ret;
}

ttstr
CLIP::pathname(int index) const
{
	// 親を root までたどってパスを組む。
	ttstr name = "";
	int p = layers()[(size_t)index].parent;
	while (p >= 0) {
		name = path_layname(layers()[(size_t)p]) + "/" + name;
		p = layers()[(size_t)p].parent;
	}
	return ttstr("root/") + name;
}

void
CLIP::startStorage()
{
	if (storageStarted) return;
	storageStarted = true;
	// レイヤ検索用の情報を生成。psdfile と同じく後ろにあるものを優先させる
	// ため逆順に走査する。フォルダ / 調整レイヤは画像を持たないので除く。
	int count = (int)layers().size();
	for (int i = count - 1; i >= 0; i--) {
		const clip::LayerInfo &lay = layers()[(size_t)i];
		if (lay.isGroup || lay.isFilter) continue;
		if (lay.bounds.empty()) continue;
		pathMap[pathname(i)][path_layname(lay)] = i;
		layerIdIdxMap[(int)lay.mainId] = i;
	}
}

void
CLIP::invalidateStorageCache()
{
	layerIdIdxMap.clear();
	pathMap.clear();
	storageStarted = false;
}

static bool checkAllNum(const tjs_char *p)
{
	if (*p == '\0') return false;
	while (*p != '\0') {
		if (!(*p >= '0' && *p <= '9')) return false;
		p++;
	}
	return true;
}

bool
CLIP::CheckExistentStorage(const ttstr &filename, int *layerIdxRet)
{
	if (!isLoaded_) return false;
	startStorage();

	const tjs_char *p = filename.c_str();

	if (TJS_strncmp(p, TJS_W("id/"), 3) == 0) {
		p += 3;
		const tjs_char *q;
		if (!(q = TJS_strrchr(p, '/'))
		    && ((q = TJS_strchr(p, '.')) && (TJS_strcmp(q, BMPEXT) == 0))) {
			ttstr name = ttstr(p, (tjs_int)(q - p));
			q = name.c_str();
			if (checkAllNum(q)) {
				int id = TJS_atoi(q);
				LayerIdIdxMap::const_iterator n = layerIdIdxMap.find(id);
				if (n != layerIdIdxMap.end()) {
					if (layerIdxRet) *layerIdxRet = n->second;
					return true;
				}
			}
		}
		return false;
	}

	// パスとファイル名を分離
	ttstr pname, fname;
	const tjs_char *q;
	if ((q = TJS_strrchr(p, '/'))) {
		pname = ttstr(p, (tjs_int)(q - p + 1));
		fname = ttstr(q + 1);
	} else {
		return false;
	}

	// 拡張子分離
	ttstr basename;
	p = fname.c_str();
	if ((q = TJS_strchr(p, '.')) && (TJS_strcmp(q, BMPEXT) == 0)) {
		basename = ttstr(p, (tjs_int)(q - p));
	} else {
		return false;
	}

	PathMap::const_iterator n = pathMap.find(pname);
	if (n != pathMap.end()) {
		const NameIdxMap &names = n->second;
		NameIdxMap::const_iterator m = names.find(basename);
		if (m != names.end()) {
			if (layerIdxRet) *layerIdxRet = m->second;
			return true;
		}
	}
	return false;
}

void
CLIP::GetListAt(const ttstr &pathname, iTVPStorageLister *lister)
{
	if (!isLoaded_) return;
	startStorage();

	if (pathname == "id/") {
		LayerIdIdxMap::const_iterator it = layerIdIdxMap.begin();
		while (it != layerIdIdxMap.end()) {
			lister->Add(ttstr(it->first) + BMPEXT);
			it++;
		}
		return;
	}

	PathMap::const_iterator n = pathMap.find(pathname);
	if (n != pathMap.end()) {
		const NameIdxMap &names = n->second;
		NameIdxMap::const_iterator it = names.begin();
		while (it != names.end()) {
			lister->Add(it->first + BMPEXT);
			it++;
		}
	}
}

/*
 * メモリ上の BMP データを返す iTJSBinaryStream 実装 (psdfile と同じ)
 */
namespace {
class CLIPMemoryStream : public iTJSBinaryStream {
	tjs_uint8 *buf;
	tjs_uint64 size;
	tjs_uint64 pos;
public:
	CLIPMemoryStream(tjs_uint8 *buf, tjs_uint64 size) : buf(buf), size(size), pos(0) {}
	virtual ~CLIPMemoryStream() { delete[] buf; }

	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) {
		tjs_int64 newpos;
		switch (whence) {
		case TJS_BS_SEEK_SET: newpos = offset; break;
		case TJS_BS_SEEK_CUR: newpos = (tjs_int64)pos + offset; break;
		case TJS_BS_SEEK_END: newpos = (tjs_int64)size + offset; break;
		default: return pos;
		}
		if (newpos < 0) newpos = 0;
		if ((tjs_uint64)newpos > size) newpos = (tjs_int64)size;
		pos = (tjs_uint64)newpos;
		return pos;
	}
	virtual tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) {
		tjs_uint64 avail = size - pos;
		if ((tjs_uint64)read_size > avail) read_size = (tjs_uint)avail;
		if (read_size) memcpy(buffer, buf + pos, read_size);
		pos += read_size;
		return read_size;
	}
	virtual tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) { return 0; }
	virtual void TJS_INTF_METHOD SetEndOfStorage() {}
	virtual tjs_uint64 TJS_INTF_METHOD GetSize() { return size; }
};
}

#if !defined(_WIN32)
typedef struct tagBITMAPFILEHEADER
{
	tjs_uint16	bfType;
	tjs_uint32	bfSize;
	tjs_uint16	bfReserved1;
	tjs_uint16	bfReserved2;
	tjs_uint32	bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#endif

#ifndef BI_RGB
	#define BI_RGB 0
#endif

iTJSBinaryStream *
CLIP::openLayerImage(const ttstr &name)
{
	int layerIdx;
	if (!CheckExistentStorage(name, &layerIdx)) return 0;
	if (layerIdx >= (int)layers().size()) return 0;
	const clip::LayerInfo &lay = layers()[(size_t)layerIdx];
	if (lay.isGroup || lay.isFilter || lay.bounds.empty()) return 0;

	clip::Image img;
	if (!layerImage(layerIdx, clip::IMAGE_MODE_MASKED, img)) return 0;
	if (img.width == 0 || img.height == 0) return 0;

	int width  = (int)img.width;
	int height = (int)img.height;
	int pitch  = width * 4;

	int hsize = sizeof(BITMAPFILEHEADER);
	int isize = hsize + sizeof(BITMAPINFOHEADER);
	int size  = isize + pitch * height;

	tjs_uint8 *p = new tjs_uint8[size];
	BITMAPFILEHEADER bfh;
	bfh.bfType      = 'B' + ('M' << 8);
	bfh.bfSize      = size;
	bfh.bfReserved1 = 0;
	bfh.bfReserved2 = 0;
	bfh.bfOffBits   = isize;
	memcpy(p, &bfh, sizeof bfh);

	BITMAPINFOHEADER bih;
	bih.biSize          = sizeof(bih);
	bih.biWidth         = width;
	bih.biHeight        = height;   // 正 = ボトムアップ
	bih.biPlanes        = 1;
	bih.biBitCount      = 32;
	bih.biCompression   = BI_RGB;
	bih.biSizeImage     = 0;
	bih.biXPelsPerMeter = 0;
	bih.biYPelsPerMeter = 0;
	bih.biClrUsed       = 0;
	bih.biClrImportant  = 0;
	memcpy(p + hsize, &bih, sizeof bih);

	// ボトムアップ BMP なので最終行から詰める。あわせて RGBA → BGRA。
	for (int y = 0; y < height; y++) {
		const tjs_uint8 *s = &img.rgba[(size_t)y * width * 4];
		tjs_uint8 *d = p + isize + (size_t)(height - 1 - y) * pitch;
		for (int x = 0; x < width; x++, s += 4, d += 4) {
			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
			d[3] = s[3];
		}
	}
	return new CLIPMemoryStream(p, (tjs_uint64)size);
}

// ============================================================================
// NCB 登録
// ============================================================================

// getLayerRegion(layer, no, x, y, w, h, mode=image_mode_masked) の
// 省略引数用 RawCallback (NCB_METHOD は既定値を扱えない)。
static tjs_error GetLayerRegion(tTJSVariant *r, tjs_int numparams,
                                tTJSVariant **params, CLIP *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 6) return TJS_E_BADPARAMCOUNT;
	tTJSVariant layer = *params[0];
	int no = (tjs_int)*params[1];
	int x  = (tjs_int)*params[2];
	int y  = (tjs_int)*params[3];
	int w  = (tjs_int)*params[4];
	int h  = (tjs_int)*params[5];
	int mode = numparams > 6 ? (tjs_int)*params[6] : (int)clip::IMAGE_MODE_MASKED;
	instance->getLayerRegion(layer, no, x, y, w, h, mode);
	return TJS_S_OK;
}

// getLayerAttribute(no, mask=false) の省略引数用 RawCallback。
static tjs_error GetLayerAttribute(tTJSVariant *r, tjs_int numparams,
                                   tTJSVariant **params, CLIP *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	int  no   = (tjs_int)*params[0];
	bool mask = numparams > 1 ? ((tjs_int)*params[1] != 0) : false;
	tTJSVariant v = instance->getLayerAttribute(no, mask);
	if (r) *r = v;
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(CLIP) {

	Factory(&ClassT::factory);

	// 画素取得モード (clip::ImageMode と同値)
	Variant("image_mode_image",  (int)clip::IMAGE_MODE_IMAGE);
	Variant("image_mode_mask",   (int)clip::IMAGE_MODE_MASK);
	Variant("image_mode_masked", (int)clip::IMAGE_MODE_MASKED);

	NCB_METHOD(load);
	NCB_METHOD(loadOctet);
	NCB_METHOD(clearData);

	Property(TJS_W("width"),       &Class::get_width,       (int)0);
	Property(TJS_W("height"),      &Class::get_height,      (int)0);
	Property(TJS_W("resolution"),  &Class::get_resolution,  (int)0);
	Property(TJS_W("layer_count"), &Class::get_layer_count, (int)0);
	Property(TJS_W("root_layer"),  &Class::get_root_layer,  (int)0);
	Property(TJS_W("is_loaded"),   &Class::get_is_loaded,   (int)0);
	Property(TJS_W("error"),       &Class::get_error,       (int)0);

	NCB_METHOD(getLayerName);
	NCB_METHOD(getLayerInfo);
	NCB_METHOD(getRoots);
	NCB_METHOD(getChildren);
	NCB_METHOD(getLayerIndexById);
	RawCallback("getLayerAttribute", &GetLayerAttribute, 0);

	NCB_METHOD(getLayerData);
	NCB_METHOD(getLayerDataRaw);
	NCB_METHOD(getLayerDataMask);
	RawCallback("getLayerRegion", &GetLayerRegion, 0);
	NCB_METHOD(getBlend);
	NCB_METHOD(getPreviewPNG);
	NCB_METHOD(check);

	NCB_METHOD(clearStorageCache);
};
