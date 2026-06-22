#pragma once

#include <vector>
#include <map>
#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")

struct ItemData;

struct CharacterState {
	int id;        // 人物ID
	int x, y;      // 游戏坐标
	int direction; // 0=北, 1=东, 2=南, 3=西
	int action;    // 动作编号
};

struct SceneState {
	int mapID = -1;
	std::vector<CharacterState> characters;
	void Clear() { mapID = -1; characters.clear(); }
};

class CScenePreviewWnd : public CDockablePane
{
public:
	CScenePreviewWnd() noexcept;
	virtual ~CScenePreviewWnd();

	void UpdatePreview(CTreeCtrl& tree, HTREEITEM hSelectedItem, const wchar_t* modPath);
	void InitCharList(wchar_t(*pPer2)[30]);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSearchChange();
	afx_msg void OnCharListSelChange();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	DECLARE_MESSAGE_MAP()

private:
	SceneState m_state;
	Gdiplus::Bitmap* m_pMapBitmap = nullptr;
	int m_cachedMapID = -1;
	CString m_modPath;
	CString m_debugInfo;

	// 角色形象相关
	bool m_spriteDataLoaded = false;
	WORD m_charImageIDs[1024] = {};
	std::map<int, Gdiplus::Bitmap*> m_spriteCache;

	// 武将列表控件
	CEdit m_editSearch;
	CListBox m_listChars;
	CFont m_listFont;
	wchar_t(*m_pPer2)[30] = nullptr;
	bool m_charListInited = false;
	int m_selectedCharID = -1;

	// 渲染参数(OnPaint计算,点击复用)
	int m_canvasTop = 0;
	int m_offsetX = 0;
	int m_offsetY = 0;
	float m_scale = 1.0f;

	// 右键命中的角色
	int m_hitCharID = -1;

	// 移动模式: 插入移动指令
	bool m_bMoveMode = false;
	int m_moveCharID = -1;
	CPoint m_moveCursorPos = { -1, -1 };

	// 调整位置模式: 角色跟随鼠标,修改已有出现指令
	bool m_bAdjustMode = false;
	int m_adjustCharID = -1;

	// 放置角色模式: 角色形象跟随鼠标
	bool m_bPlaceMode = false;
	CPoint m_placeCursorPos = { -1, -1 };
	int m_placeDir = 2;      // 放置时的朝向(默认朝南)
	int m_placeAction = 0;   // 放置时的动作(默认普通)

	// 记住当前树上下文(用于回溯查找指令)
	CTreeCtrl* m_pTreeCtrl = nullptr;
	HTREEITEM m_hCurItem = nullptr;

	// 网格显示
	bool m_bShowGrid = false;

	// 鼠标跟踪
	CPoint m_mousePos = { -1, -1 };  // 当前鼠标窗口坐标
	bool m_bSuppressContextMenu = false; // 右键取消模式后屏蔽一次菜单

	// Ctrl吸附
	CPoint m_snappedGamePos = { -1, -1 };
	bool m_bSnapped = false;

	void EvaluateToItem(CTreeCtrl& tree, HTREEITEM hItem);
	void ProcessInstruction(ItemData* data);
	bool LoadMapImage(int mapID, const wchar_t* modPath);
	CPoint GameToPixel(int gameX, int gameY);
	CPoint PixelToGame(int screenX, int screenY);
	CPoint SnapToGrid(CPoint gamePos, int excludeCharID);
	void DrawDirectionArrow(Gdiplus::Graphics& g, int cx, int cy, int direction);

	CString GetRootPath(const wchar_t* modPath);
	bool LoadSpriteData(const wchar_t* modPath);
	Gdiplus::Bitmap* GetSpriteBitmap(int charID, const wchar_t* modPath);
	void DrawCharacter(Gdiplus::Graphics& g, const CharacterState& ch, int offsetX, int offsetY, float scale);

	int HitTestCharacter(CPoint screenPt);
	void RefreshCharList();
	void InsertAppearInstruction(int charID, int gameX, int gameY);
	void InsertTurnInstruction(int charID, int direction);
	void InsertActionInstruction(int charID, int action);
	void InsertDisappearInstruction(int charID);
	void InsertMoveInstruction(int charID, int gameX, int gameY);
	HTREEITEM FindAppearInstruction(int charID);
};
