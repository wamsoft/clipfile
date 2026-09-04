#include <ncbind.hpp>

#include "clipclass.h"

// -----------------------------------------------------------------------------
// clip:// ストレージ機能 (psdfile の psd:// と同じ規則)
//
//   clip://CLIPファイル名/root/フォルダ名/.../レイヤ名.bmp
//   clip://CLIPファイル名/id/LayerId.bmp
// -----------------------------------------------------------------------------

#define BASENAME TJS_W("clip")

class CLIPStorage : public iTVPStorageMedia, tTVPCompactEventCallbackIntf
{
public:
	CLIPStorage() : refCount(1) {}
	virtual ~CLIPStorage() { clearCache(); }

	/** CLIP オブジェクト参照を追加 */
	void add(ttstr filename, CLIP *clip) {
		clipMap[filename] = clip;
	}

	/** CLIP オブジェクト参照の消去要求 */
	void remove(CLIP *clip) {
		CLIPMap::iterator it = clipMap.begin();
		while (it != clipMap.end()) {
			if (it->second == clip) {
				it = clipMap.erase(it);
			} else {
				it++;
			}
		}
	}

	void clearCache() { cache.Clear(); }

public:
	// -----------------------------------
	// iTVPStorageMedia
	// -----------------------------------

	virtual void TJS_INTF_METHOD AddRef() { refCount++; }

	virtual void TJS_INTF_METHOD Release() {
		if (refCount == 1) delete this;
		else refCount--;
	}

	virtual void TJS_INTF_METHOD GetName(ttstr &name) { name = BASENAME; }

	virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) {}
	virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) {}

	virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) {
		ttstr fname;
		CLIP *clip = getCLIP(name, fname);
		return clip ? clip->CheckExistentStorage(fname) : false;
	}

	virtual iTJSBinaryStream * TJS_INTF_METHOD Open(const ttstr &name, tjs_uint32 flags) {
		if (flags == TJS_BS_READ) {   // 読み込みのみ
			ttstr fname;
			CLIP *clip = getCLIP(name, fname);
			if (clip) {
				iTJSBinaryStream *stream = clip->openLayerImage(fname);
				if (stream) return stream;
			}
		}
		TVPThrowExceptionMessage(TJS_W("%1:cannot open clipfile"), name);
		return NULL;
	}

	virtual void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister *lister) {
		ttstr fname;
		CLIP *clip = getCLIP(name, fname);
		if (clip) clip->GetListAt(fname, lister);
	}

	virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) { name = ""; }

public:
	// -----------------------------------
	// tTVPCompactEventCallbackIntf
	// -----------------------------------
	virtual void TJS_INTF_METHOD OnCompact(tjs_int level) {
		if (level > TVP_COMPACT_LEVEL_MINIMIZE) clearCache();
	}

protected:
	/**
	 * ファイル名に合致した CLIP 情報を取得
	 * @param name  パス全体
	 * @param fname ドメイン以降のファイル名を返す
	 */
	CLIP *getCLIP(ttstr name, ttstr &fname) {
		name.ToLowerCase();
		// ドメイン名とそれ以降を分離
		ttstr dname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = TJS_strchr(p, '/'))) {
			dname = ttstr(p, (tjs_int)(q - p));
			fname = ttstr(q + 1);
		} else {
			TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);
		}

		CLIP *clip = 0;

		// 直近のキャッシュが合致する場合はそれを返す
		if (cache.Type() == tvtObject &&
		    (clip = ncbInstanceAdaptor<CLIP>::GetNativeInstance(cache.AsObjectNoAddRef())) &&
		    clip->dname == dname) {
			return clip;
		}

		// CLIP オブジェクトの弱参照から探す
		CLIPMap::iterator it = clipMap.find(dname);
		if (it != clipMap.end()) {
			cache = it->second->getSelf();
			return it->second;
		}

		// 自分で open してそのままキャッシュとして持つ
		TVPExecuteExpression(TJS_W("new CLIP()"), &cache);
		clip = ncbInstanceAdaptor<CLIP>::GetNativeInstance(cache.AsObjectNoAddRef());
		if (clip) {
			if (clip->load(dname)) return clip;
			clearCache();
		}
		return 0;
	}

private:
	tjs_uint refCount;

	// CLIP オブジェクトのキャッシュ参照
	tTJSVariant cache;

	// CLIP オブジェクトの弱参照
	typedef std::map<ttstr, CLIP *> CLIPMap;
	CLIPMap clipMap;
};

static CLIPStorage *clipStorage = 0;

// 弱参照追加
void
CLIP::addToStorage(const ttstr &filename)
{
	// 登録用ベース名を生成
	const tjs_char *p = filename.c_str();
	const tjs_char *q;
	if ((q = TJS_strrchr(p, '/'))) {
		dname = ttstr(q + 1);
	} else {
		dname = filename;
	}
	dname.ToLowerCase();
	if (clipStorage != NULL) clipStorage->add(dname, this);
}

// 弱参照解除
void
CLIP::removeFromStorage()
{
	if (clipStorage != NULL) clipStorage->remove(this);
}

void
CLIP::clearStorageCache()
{
	if (clipStorage != NULL) clipStorage->clearCache();
}

// --------------------------------------------------------------------

void initStorage()
{
	if (clipStorage == NULL) {
		clipStorage = new CLIPStorage();
		TVPRegisterStorageMedia(clipStorage);
		TVPAddCompactEventHook((tTVPCompactEventCallbackIntf *)clipStorage);
	}
}

void doneStorage()
{
	if (clipStorage != NULL) {
		TVPRemoveCompactEventHook((tTVPCompactEventCallbackIntf *)clipStorage);
		TVPUnregisterStorageMedia(clipStorage);
		clipStorage->Release();
		clipStorage = NULL;
	}
}

NCB_PRE_REGIST_CALLBACK(initStorage);
NCB_POST_UNREGIST_CALLBACK(doneStorage);

// 静的リンクする第三者コンポーネント (clipparse / SQLite) のライセンスを
// 本体収集機構へ登録 (LicensesGen.cpp = licenses/manifest.json から生成)
extern void RegisterClipFileLicenses();
NCB_PRE_REGIST_CALLBACK(RegisterClipFileLicenses);
