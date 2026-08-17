#ifndef __CLIPCLASS_H__
#define __CLIPCLASS_H__

#include <tp_stub.h>
#include "clipfile.h"
#include <map>
#include <vector>

class CLIPStorage;

/**
 * CLIP STUDIO PAINT (.clip) 読み込みクラス。
 *
 * clip::ClipFile を継承して吉里吉里バインドを被せる (psdfile の PSD と同じ
 * 構成)。clipparse 側は「メタ情報 (SQLite) だけ持って、画素は 256x256 ブロック
 * 単位で必要になったときに読む」ので、大きなファイルでも load 自体は軽い。
 */
class CLIP : public clip::ClipFile
{
	friend class CLIPStorage;

public:
	CLIP(iTJSDispatch2 *objthis);
	~CLIP();

	/** インスタンス生成ファクトリ */
	static tjs_error factory(CLIP **result, tjs_int numparams, tTJSVariant **params,
	                         iTJSDispatch2 *objthis);

	/** 生成時の自己オブジェクトを取得 */
	tTJSVariant getSelf();

	/**
	 * CLIP ファイルのロード
	 *
	 * ローカルにあるファイルは mmap で開く (画素は遅延読み)。アーカイブ内など
	 * ローカルパスに落ちない場合はストリームから全体をメモリへ読んでから渡す。
	 * @param filename ファイル名
	 * @return ロードに成功したら true
	 */
	bool load(ttstr filename);

	/**
	 * octet に入った CLIP データのロード。バイト列はこのオブジェクトが保持する。
	 * ファイル名を持たないので clip:// ストレージへは登録しない。
	 * @param data ファイル全体を格納した octet
	 * @return ロードに成功したら true
	 */
	bool loadOctet(tTJSVariant data);

	/** 保持しているデータを明示的に破棄する */
	void clearData();

	static void clearStorageCache();

	// --- 文書レベルのプロパティ ---------------------------------------------
	// キャンバスの実ピクセル寸法 (Canvas.CanvasWidth は CanvasUnit 依存で
	// mm のファイルが実在するため使えない。clipparse が 100% ミップの
	// Attribute から求めた値)。未ロードは -1 / 0。
	int    get_width()       { return isLoaded_ ? (int)canvasWidth()  : -1; }
	int    get_height()      { return isLoaded_ ? (int)canvasHeight() : -1; }
	double get_resolution()  { return isLoaded_ ? canvasResolution()  : 0.0; }
	int    get_layer_count() { return isLoaded_ ? (int)layers().size() : -1; }
	int    get_root_layer()  { return isLoaded_ ? (int)rootLayer()    : -1; }
	bool   get_is_loaded()   { return isLoaded_; }
	/** 直近の失敗理由 (clipparse 側のメッセージ)。無ければ空文字列 */
	ttstr  get_error();

	// --- レイヤ参照系 --------------------------------------------------------

	/** レイヤ名 */
	ttstr getLayerName(int no);

	/**
	 * レイヤ情報の辞書。詳細は manual.tjs 参照。
	 */
	tTJSVariant getLayerInfo(int no);

	/** 最上位レイヤ (ルート直下) のレイヤ番号配列。下から上の順 */
	tTJSVariant getRoots();

	/** フォルダの直接の子のレイヤ番号配列。下から上の順。フォルダ以外は空配列 */
	tTJSVariant getChildren(int no);

	/** LayerId (CLIP の Layer.MainId) からレイヤ番号を引く。無ければ -1 */
	int getLayerIndexById(int layerId);

	/**
	 * 100% ミップの Offscreen 属性。ブロックグリッドや表現色を見たいとき用。
	 * @param no   レイヤ番号
	 * @param mask true でマスク側のミップ連鎖を見る
	 * @return 属性辞書。実体が無ければ void
	 */
	tTJSVariant getLayerAttribute(int no, bool mask);

	// --- 画素取得 ------------------------------------------------------------

	/** レイヤ画像 (マスクをアルファに繰り込んだもの) を Layer へ読み出す */
	void getLayerData(tTJSVariant layer, int no);
	/** レイヤ画像 (マスクを繰り込まない生画像) を Layer へ読み出す */
	void getLayerDataRaw(tTJSVariant layer, int no);
	/** レイヤのマスクのみ (グレー) を Layer へ読み出す */
	void getLayerDataMask(tTJSVariant layer, int no);

	/**
	 * レイヤ画像のうち指定矩形 (キャンバス座標) だけを読み出す。
	 * CLIP は 256x256 タイルなので、重なるタイルしか展開されない
	 * (PSD は行 RLE なので同じことができない = clipparse 固有の口)。
	 * @param layer 読み出し先 Layer
	 * @param no    レイヤ番号
	 * @param x,y,w,h キャンバス座標の矩形
	 * @param mode  image_mode_* (既定 image_mode_masked)
	 */
	void getLayerRegion(tTJSVariant layer, int no, int x, int y, int w, int h, int mode);

	/**
	 * 全レイヤを下から合成した画像を Layer へ読み出す。
	 * clipparse 側で実際に合成する (PSD の getBlend が「保存済みの合成結果を
	 * 読むだけ」なのとは違う)。
	 * @return 成功したら true
	 */
	bool getBlend(tTJSVariant layer);

	/**
	 * ファイルに埋まっている完成画 (CanvasPreview) を PNG の octet で返す。
	 * 等倍とは限らない。無ければ void
	 */
	tTJSVariant getPreviewPNG();

	/**
	 * 全ブロックの構造アサーションを回す (診断用)。
	 * @return 問題があればレポート文字列、問題なければ void
	 */
	tTJSVariant check();

protected:
	iTJSDispatch2 *objthis; ///< 自己オブジェクト情報の参照
	ttstr dname;            ///< ストレージ登録用ベース名
	bool  isLoaded_;        ///< ロード済みか (clip::ClipFile 側にフラグが無いため)
	std::vector<tjs_uint8> owned_; ///< loadOctet / ストリーム経路で保持するバイト列

	/** ロード後の共通処理 (フラグ更新 + ストレージキャッシュ破棄) */
	bool finishLoad(bool ok);

	/** レイヤ番号が適切かどうか判定 (不正なら例外) */
	void checkLayerNo(int no);

	/** clip::Image を吉里吉里 Layer へ流し込む (RGBA → BGRA 変換込み) */
	void storeImage(tTJSVariant layer, const clip::Image &img,
	                int left, int top, int opacity, int type, bool visible,
	                const ttstr &name);

	/** getLayerData* の共通実体 */
	void _getLayerData(tTJSVariant layer, int no, clip::ImageMode mode);

	// ------------------------------------------------------------
	// ストレージ (clip://) 参照用インターフェース
	// ------------------------------------------------------------
protected:
	void addToStorage(const ttstr &filename);
	void removeFromStorage();
	void invalidateStorageCache();

	bool storageStarted; ///< ストレージ用の情報初期化済みフラグ

	static ttstr path_layname(const clip::LayerInfo &lay);
	ttstr pathname(int index) const;
	void  startStorage();

	bool CheckExistentStorage(const ttstr &filename, int *layerIdxRet = 0);
	void GetListAt(const ttstr &pathname, iTVPStorageLister *lister);
	iTJSBinaryStream *openLayerImage(const ttstr &name);

	typedef std::map<int, int> LayerIdIdxMap;   ///< LayerId → レイヤ番号
	LayerIdIdxMap layerIdIdxMap;

	typedef std::map<ttstr, int> NameIdxMap;    ///< レイヤ名 → レイヤ番号
	typedef std::map<ttstr, NameIdxMap> PathMap;///< パス別のレイヤ名一覧
	PathMap pathMap;
};

// clipclass.cpp / clipwriter.cpp / main.cpp で共有する小物 ------------------

/** UTF-8 (clipparse の std::string) → ttstr */
ttstr utf8ToTjs(const std::string &s);
/** ttstr → UTF-8 */
std::string tjsToUtf8(const ttstr &s);
/** CLIP の LayerComposite → 吉里吉里の ltPs* 合成モード */
int clipCompositeToTjsType(int composite);
/** CLIP の LayerComposite → PSD の 4CC (psdfile と同じ表記で見たいとき用) */
ttstr clipCompositeToPsdKey(int composite);
/** 吉里吉里 Layer から RGBA を tight-pack で取り出す (戻り値は呼び出し側で delete[]) */
tjs_uint8 *readLayerRGBA(tTJSVariant &layer, int &width, int &height);

#endif
