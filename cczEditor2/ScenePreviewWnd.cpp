#include "pch.h"
#include "resource.h"
#include "ScenePreviewWnd.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"
#include "MainFrm.h"
#include <algorithm>
#include <cmath>

BEGIN_MESSAGE_MAP(CScenePreviewWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_MOUSEWHEEL()
	ON_EN_CHANGE(IDC_SCENE_CHAR_SEARCH, &CScenePreviewWnd::OnSearchChange)
	ON_LBN_SELCHANGE(IDC_SCENE_CHAR_LIST, &CScenePreviewWnd::OnCharListSelChange)
END_MESSAGE_MAP()

CScenePreviewWnd::CScenePreviewWnd() noexcept
{
}

CScenePreviewWnd::~CScenePreviewWnd()
{
	if (m_pMapBitmap) {
		delete m_pMapBitmap;
		m_pMapBitmap = nullptr;
	}
	for (auto& pair : m_spriteCache) {
		delete pair.second;
	}
	m_spriteCache.clear();
}

int CScenePreviewWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rc;
	GetClientRect(&rc);

	m_editSearch.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		CRect(0, 0, rc.Width(), 24), this, IDC_SCENE_CHAR_SEARCH);
	m_editSearch.SetWindowTextW(L"");

	m_listChars.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
		CRect(0, 24, rc.Width(), 120), this, IDC_SCENE_CHAR_LIST);

	m_listFont.CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, _T("Microsoft YaHei"));
	m_editSearch.SetFont(&m_listFont);
	m_listChars.SetFont(&m_listFont);

	return 0;
}

void CScenePreviewWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);

	const int SEARCH_H = 24;
	const int LIST_H = 160;
	m_canvasTop = SEARCH_H + LIST_H;

	if (m_editSearch.GetSafeHwnd())
		m_editSearch.MoveWindow(0, 0, cx, SEARCH_H);
	if (m_listChars.GetSafeHwnd())
		m_listChars.MoveWindow(0, SEARCH_H, cx, LIST_H);

	Invalidate();
}

BOOL CScenePreviewWnd::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

// ========== 坐标转换 ==========

CPoint CScenePreviewWnd::GameToPixel(int X, int Y)
{
	int px = 8 * (X - Y + 42);
	int py = 4 * (X + Y - 50);
	return CPoint(px, py);
}

// ========== 方向箭头绘制 ==========

void CScenePreviewWnd::DrawDirectionArrow(Gdiplus::Graphics& g, int cx, int cy, int direction)
{
	Gdiplus::SolidBrush arrowBrush(Gdiplus::Color(255, 255, 200, 0));
	Gdiplus::Point pts[3];
	int r = 14;

	switch (direction) {
	case 0: // 北
		pts[0] = Gdiplus::Point(cx, cy - r - 6);
		pts[1] = Gdiplus::Point(cx - 4, cy - r);
		pts[2] = Gdiplus::Point(cx + 4, cy - r);
		break;
	case 1: // 东
		pts[0] = Gdiplus::Point(cx + r + 6, cy);
		pts[1] = Gdiplus::Point(cx + r, cy - 4);
		pts[2] = Gdiplus::Point(cx + r, cy + 4);
		break;
	case 2: // 南
		pts[0] = Gdiplus::Point(cx, cy + r + 6);
		pts[1] = Gdiplus::Point(cx - 4, cy + r);
		pts[2] = Gdiplus::Point(cx + 4, cy + r);
		break;
	case 3: // 西
		pts[0] = Gdiplus::Point(cx - r - 6, cy);
		pts[1] = Gdiplus::Point(cx - r, cy - 4);
		pts[2] = Gdiplus::Point(cx - r, cy + 4);
		break;
	default:
		return;
	}
	g.FillPolygon(&arrowBrush, pts, 3);
}

// ========== 推演逻辑 ==========

void CScenePreviewWnd::ProcessInstruction(ItemData* data)
{
	if (!data || data->int_data.empty()) return;

	switch (data->id) {
	case 47: // 0x2F: 清除人物
		m_state.characters.clear();
		break;

	case 39: // 0x27: 背景显示
		if (data->int_data.size() > 3 && data->int_data[0] == 2)
			m_state.mapID = data->int_data[3];
		break;

	case 48: // 0x30: 武将出现 [人物ID, X, Y, 方向, 动作]
		if (data->int_data.size() >= 5) {
			CharacterState cs;
			cs.id = data->int_data[0];
			cs.x = data->int_data[1];
			cs.y = data->int_data[2];
			cs.direction = data->int_data[3];
			cs.action = data->int_data[4];
			auto& chars = m_state.characters;
			chars.erase(std::remove_if(chars.begin(), chars.end(),
				[&](const CharacterState& c) { return c.id == cs.id; }), chars.end());
			chars.push_back(cs);
		}
		break;

	case 49: // 0x31: 武将消失 [模式, 人物ID, ...]
		if (data->int_data.size() >= 2 && data->int_data[0] == 0) {
			int charID = data->int_data[1];
			auto& chars = m_state.characters;
			chars.erase(std::remove_if(chars.begin(), chars.end(),
				[&](const CharacterState& c) { return c.id == charID; }), chars.end());
		}
		break;

	case 50: // 0x32: 武将移动 [?, 人物ID, ?, 目标X, 目标Y, 方向]
		if (data->int_data.size() >= 6) {
			int charID = data->int_data[1];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.x = data->int_data[3];
					c.y = data->int_data[4];
					c.direction = data->int_data[5];
					break;
				}
			}
		}
		break;

	case 51: // 0x33: 武将转向 [人物ID, 动作, 方向, 缺省, 缺省]
		if (data->int_data.size() >= 3) {
			int charID = data->int_data[0];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.action = data->int_data[1];
					c.direction = data->int_data[2];
					break;
				}
			}
		}
		break;

	case 52: // 0x34: 武将动作 [人物ID, 动作, ?, ?, ?]
		if (data->int_data.size() >= 2) {
			int charID = data->int_data[0];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.action = data->int_data[1];
					break;
				}
			}
		}
		break;
	}
}

void CScenePreviewWnd::EvaluateToItem(CTreeCtrl& tree, HTREEITEM hItem)
{
	m_state.Clear();
	if (!hItem) return;

	HTREEITEM hSection = hItem;
	ItemData* pData = (ItemData*)tree.GetItemData(hSection);
	if (pData && (pData->id == -1 || pData->id == -2)) return;

	HTREEITEM hParent = tree.GetParentItem(hItem);
	while (hParent) {
		ItemData* parentData = (ItemData*)tree.GetItemData(hParent);
		if (parentData && parentData->id == -2) {
			hSection = hParent;
			break;
		}
		if (parentData && parentData->id == -1) return;
		hParent = tree.GetParentItem(hParent);
	}

	if (!hParent) return;

	HTREEITEM hCur = tree.GetChildItem(hSection);
	while (hCur) {
		ItemData* data = (ItemData*)tree.GetItemData(hCur);
		if (data) ProcessInstruction(data);
		if (hCur == hItem) break;

		HTREEITEM hChild = tree.GetChildItem(hCur);
		if (hChild) {
			HTREEITEM hSub = hChild;
			while (hSub) {
				ItemData* subData = (ItemData*)tree.GetItemData(hSub);
				if (subData) ProcessInstruction(subData);
				if (hSub == hItem) return;
				hSub = tree.GetNextSiblingItem(hSub);
			}
		}

		hCur = tree.GetNextSiblingItem(hCur);
	}
}

// ========== 路径工具 ==========

CString CScenePreviewWnd::GetRootPath(const wchar_t* modPath)
{
	CString basePath(modPath);
	int len = basePath.GetLength();
	for (int i = len - 1; i >= 0; i--) {
		if (basePath[i] == L'\\' || basePath[i] == L'/') {
			if (i >= 2 && (basePath[i - 1] == L'S' || basePath[i - 1] == L's')
				&& (basePath[i - 2] == L'R' || basePath[i - 2] == L'r')) {
				continue;
			}
			return basePath.Left(i + 1);
		}
	}
	return _T("");
}

// ========== 256色调色板 (从Pmapobj.BMP提取, BGRA格式) ==========
static const BYTE s_spritePalette[1024] = {
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0x63,0x8C,0xFF,0x00, 0x4A,0x6B,0xFF,0x00,
	0x39,0x4A,0xF7,0x00, 0x29,0x31,0xAD,0x00, 0x5A,0xDE,0xFF,0x00, 0x52,0xCE,0xFF,0x00,
	0x5A,0xBD,0xEF,0x00, 0x52,0x9C,0xBD,0x00, 0xFF,0xFF,0xFF,0x00, 0xEE,0xEE,0xEE,0x00,
	0xDD,0xDD,0xDD,0x00, 0xCC,0xCC,0xCC,0x00, 0xBB,0xBB,0xBB,0x00, 0xAA,0xAA,0xAA,0x00,
	0x99,0x99,0x99,0x00, 0x88,0x88,0x88,0x00, 0x77,0x77,0x77,0x00, 0x66,0x66,0x66,0x00,
	0x55,0x55,0x55,0x00, 0x44,0x44,0x44,0x00, 0x22,0x22,0x22,0x00, 0x00,0x00,0x00,0x00,
	0xE1,0xEF,0xFD,0x00, 0xB4,0xD9,0xFD,0x00, 0x96,0xCA,0xFE,0x00, 0x69,0xB3,0xFE,0x00,
	0x2D,0x95,0xFF,0x00, 0x00,0x7F,0xFF,0x00, 0x00,0x6E,0xFF,0x00, 0x00,0x4C,0xFF,0x00,
	0x00,0x00,0xFF,0x00, 0x00,0x00,0xE7,0x00, 0x00,0x00,0xC3,0x00, 0x00,0x00,0xB7,0x00,
	0x00,0x00,0x9E,0x00, 0x00,0x00,0x86,0x00, 0x00,0x00,0x6E,0x00, 0x00,0x00,0x4A,0x00,
	0xFD,0xFD,0xD2,0x00, 0xFD,0xFD,0xB4,0x00, 0xFD,0xFE,0x96,0x00, 0xFD,0xFE,0x69,0x00,
	0xFF,0xFF,0x00,0x00, 0xFF,0xDD,0x00,0x00, 0xFF,0xBB,0x00,0x00, 0xFF,0x99,0x00,0x00,
	0xFF,0x77,0x00,0x00, 0xFF,0x55,0x00,0x00, 0xFF,0x00,0x00,0x00, 0xE7,0x00,0x00,0x00,
	0xC3,0x00,0x00,0x00, 0xAB,0x00,0x00,0x00, 0x7A,0x00,0x00,0x00, 0x56,0x00,0x00,0x00,
	0xE1,0xFD,0xFD,0x00, 0xB4,0xFD,0xFD,0x00, 0x87,0xFE,0xFE,0x00, 0x5A,0xFE,0xFE,0x00,
	0x2D,0xFF,0xFF,0x00, 0x00,0xFF,0xFF,0x00, 0x00,0xED,0xED,0x00, 0x00,0xDB,0xDB,0x00,
	0x00,0xC9,0xC9,0x00, 0x00,0xB2,0xB2,0x00, 0x00,0x9F,0x9F,0x00, 0x00,0x8B,0x8B,0x00,
	0x00,0x78,0x78,0x00, 0x00,0x65,0x65,0x00, 0x00,0x51,0x51,0x00, 0x00,0x3E,0x3E,0x00,
	0xE1,0xFD,0xE1,0x00, 0xC1,0xFD,0xC1,0x00, 0xA1,0xFE,0xA1,0x00, 0x81,0xFE,0x81,0x00,
	0x60,0xFE,0x60,0x00, 0x00,0xFF,0x00,0x00, 0x00,0xED,0x00,0x00, 0x00,0xDB,0x00,0x00,
	0x00,0xC9,0x00,0x00, 0x00,0xB7,0x00,0x00, 0x00,0xA5,0x00,0x00, 0x00,0x92,0x00,0x00,
	0x00,0x80,0x00,0x00, 0x00,0x6E,0x00,0x00, 0x00,0x5C,0x00,0x00, 0x00,0x4A,0x00,0x00,
	0x6E,0x89,0xAE,0x00, 0x5E,0x76,0x98,0x00, 0x48,0x62,0x75,0x00, 0x3F,0x52,0x64,0x00,
	0x36,0x40,0x4B,0x00, 0x1E,0x25,0x2E,0x00, 0xA5,0xCE,0xE7,0x00, 0x88,0xC3,0xEE,0x00,
	0x72,0xAD,0xE1,0x00, 0x73,0xA2,0xD9,0x00, 0x6C,0x9A,0xCE,0x00, 0xBD,0xD6,0xE7,0x00,
	0xB8,0xD1,0xE9,0x00, 0xA5,0xC6,0xE7,0x00, 0x94,0xBD,0xDE,0x00, 0x84,0xB5,0xDE,0x00,
	0xFD,0xE1,0xF7,0x00, 0xE6,0xC1,0xDE,0x00, 0xCE,0xA1,0xC4,0x00, 0xB7,0x81,0xAB,0x00,
	0x9F,0x61,0x92,0x00, 0x88,0x41,0x79,0x00, 0x70,0x21,0x5F,0x00, 0x59,0x01,0x46,0x00,
	0xFD,0xE1,0xFD,0x00, 0xFE,0xA9,0xFE,0x00, 0xFD,0x86,0xFD,0x00, 0xFF,0x6A,0xFF,0x00,
	0xFF,0x00,0xFF,0x00, 0xE8,0x17,0xE8,0x00, 0xD1,0x14,0xD1,0x00, 0xBA,0x12,0xBA,0x00,
	0xCE,0xF0,0xF8,0x00, 0xAB,0xD0,0xD9,0x00, 0x88,0xB0,0xBA,0x00, 0x65,0x90,0x9B,0x00,
	0x55,0x7F,0x8A,0x00, 0x45,0x6F,0x7A,0x00, 0x34,0x5E,0x69,0x00, 0x24,0x4D,0x58,0x00,
	0xF2,0xDE,0xAB,0x00, 0xCD,0xB9,0x85,0x00, 0xA9,0x93,0x5E,0x00, 0x84,0x6E,0x38,0x00,
	0x77,0x63,0x30,0x00, 0x6B,0x57,0x28,0x00, 0x5E,0x4C,0x20,0x00, 0x51,0x40,0x18,0x00,
	0x80,0xC3,0x80,0x00, 0x70,0xB4,0x70,0x00, 0x61,0xA5,0x61,0x00, 0x51,0x96,0x51,0x00,
	0x41,0x88,0x41,0x00, 0x31,0x79,0x31,0x00, 0x22,0x6A,0x22,0x00, 0x12,0x5B,0x12,0x00,
	0x5D,0x92,0xBA,0x00, 0x50,0x84,0xAB,0x00, 0x44,0x76,0x9D,0x00, 0x37,0x68,0x8E,0x00,
	0x2A,0x5B,0x80,0x00, 0x1D,0x4D,0x71,0x00, 0x11,0x3F,0x63,0x00, 0x04,0x31,0x54,0x00,
	0x29,0xC6,0xBD,0x00, 0x29,0xBD,0xAD,0x00, 0x4A,0xAD,0xAD,0x00, 0x31,0xAD,0x94,0x00,
	0x31,0x9C,0x8C,0x00, 0x31,0x94,0x73,0x00, 0x31,0x84,0x73,0x00, 0x31,0x7B,0x63,0x00,
	0xBF,0xA0,0x22,0x00, 0xAE,0x80,0x1A,0x00, 0x9D,0x60,0x11,0x00, 0x8C,0x3F,0x09,0x00,
	0x7B,0x1F,0x00,0x00, 0x02,0x55,0xC2,0x00, 0x36,0xAC,0xFF,0x00, 0x21,0x63,0xE7,0x00,
	0x18,0x42,0xD6,0x00, 0x18,0x42,0xAD,0x00, 0x10,0x31,0x8C,0x00, 0x1F,0x24,0x2A,0x00,
	0x58,0x01,0x44,0x00, 0x00,0x01,0x3A,0x00, 0x00,0x00,0x6F,0x00, 0x0E,0x38,0x1F,0x00,
	0x0F,0x42,0x28,0x00, 0x10,0x4A,0x2D,0x00, 0x05,0x30,0x54,0x00, 0x12,0x3F,0x62,0x00,
	0x13,0x50,0x31,0x00, 0x38,0x3F,0x4A,0x00, 0x13,0x56,0x35,0x00, 0x14,0x53,0x33,0x00,
	0x16,0x5D,0x3A,0x00, 0x15,0x5A,0x37,0x00, 0x19,0x63,0x3E,0x00, 0x17,0x61,0x3D,0x00,
	0x1A,0x65,0x40,0x00, 0x1A,0x68,0x42,0x00, 0x3A,0x5F,0x3C,0x00, 0x1B,0x6C,0x45,0x00,
	0x14,0x63,0x4B,0x00, 0x32,0x50,0x5C,0x00, 0x1E,0x4C,0x71,0x00, 0x60,0x6E,0x44,0x00,
	0x55,0x64,0x3E,0x00, 0x59,0x68,0x42,0x00, 0x60,0x71,0x49,0x00, 0x4E,0x6B,0x4B,0x00,
	0x14,0x69,0x51,0x00, 0x14,0x6D,0x54,0x00, 0x16,0x70,0x57,0x00, 0x1D,0x70,0x47,0x00,
	0x15,0x73,0x59,0x00, 0x2B,0x5A,0x7F,0x00, 0x43,0x71,0x4F,0x00, 0x18,0x75,0x5C,0x00,
	0x37,0x61,0x6A,0x00, 0x19,0x78,0x5D,0x00, 0x6B,0x78,0x47,0x00, 0x63,0x74,0x4C,0x00,
	0x67,0x78,0x51,0x00, 0x56,0x74,0x53,0x00, 0x3D,0x78,0x58,0x00, 0x59,0x6F,0x55,0x00,
	0x63,0x7A,0x5A,0x00, 0x6E,0x7F,0x56,0x00, 0x19,0x7B,0x60,0x00, 0x19,0x81,0x65,0x00,
	0x1B,0x85,0x68,0x00, 0x38,0x67,0x8E,0x00, 0x1B,0x7E,0x63,0x00, 0x30,0x72,0x6D,0x00,
	0x26,0x7D,0x6C,0x00, 0x59,0x76,0x5C,0x00, 0x34,0x7D,0x77,0x00, 0x5B,0x7C,0x6A,0x00,
	0x3A,0x76,0x77,0x00, 0x41,0x7D,0x81,0x00, 0x74,0x86,0x5C,0x00, 0x60,0x82,0x73,0x00,
	0x47,0x75,0x9C,0x00, 0x4D,0x83,0x8B,0x00, 0x55,0x84,0xAA,0x00, 0x53,0x9A,0xBB,0x00,
	0x76,0x9F,0xCA,0x00, 0x74,0xB9,0xE1,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00
};

// 将256色裸像素数据转为ARGB Bitmap，透明色=品红(0xF700FF)
// 宽度固定48，高度由数据量推算；多余字节(头部)自动跳过
static Gdiplus::Bitmap* CreateBitmapFromRaw8bpp(const BYTE* rawData, DWORD dataSize)
{
	const int W = 48;
	DWORD skip = dataSize % W; // 对48取余=头部多余字节
	DWORD pixelBytes = dataSize - skip;
	int H = (int)(pixelBytes / W);
	if (H < 64 || H > 2048) return nullptr;
	rawData += skip;

	Gdiplus::Bitmap* pBmp = new Gdiplus::Bitmap(W, H, PixelFormat32bppARGB);
	if (pBmp->GetLastStatus() != Gdiplus::Ok) { delete pBmp; return nullptr; }

	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, W, H);
	pBmp->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

	// 取左上角第一个像素的色板索引作为透明索引
	BYTE transIdx = rawData[0];

	for (int y = 0; y < H; y++) {
		DWORD* dst = (DWORD*)((BYTE*)bmpData.Scan0 + y * bmpData.Stride);
		const BYTE* src = rawData + y * W;
		for (int x = 0; x < W; x++) {
			BYTE idx = src[x];
			BYTE b = s_spritePalette[idx * 4 + 0];
			BYTE g = s_spritePalette[idx * 4 + 1];
			BYTE r = s_spritePalette[idx * 4 + 2];
			// 品红(B=0xFF,G=0x00,R=0xF7)或与左上角相同色板索引 → 透明
			if ((b == 0xFF && g == 0x00 && r == 0xF7) || idx == transIdx) {
				dst[x] = 0x00000000;
			} else {
				dst[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
			}
		}
	}

	pBmp->UnlockBits(&bmpData);
	return pBmp;
}

// 将Bitmap中与左上角像素相同颜色的像素设为透明
static void ApplyTopLeftTransparency(Gdiplus::Bitmap* pBmp)
{
	if (!pBmp) return;
	int w = (int)pBmp->GetWidth();
	int h = (int)pBmp->GetHeight();
	if (w == 0 || h == 0) return;

	// 读取左上角像素颜色
	Gdiplus::Color topLeft;
	pBmp->GetPixel(0, 0, &topLeft);
	// 如果已经是透明就不用处理
	if (topLeft.GetAlpha() == 0) return;
	BYTE tR = topLeft.GetRed(), tG = topLeft.GetGreen(), tB = topLeft.GetBlue();

	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, w, h);
	pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
		PixelFormat32bppARGB, &bmpData);

	for (int y = 0; y < h; y++) {
		DWORD* row = (DWORD*)((BYTE*)bmpData.Scan0 + y * bmpData.Stride);
		for (int x = 0; x < w; x++) {
			DWORD px = row[x];
			BYTE a = (px >> 24) & 0xFF;
			BYTE r = (px >> 16) & 0xFF;
			BYTE g = (px >> 8) & 0xFF;
			BYTE b = px & 0xFF;
			if (a > 0 && r == tR && g == tG && b == tB) {
				row[x] = 0x00000000;
			}
		}
	}

	pBmp->UnlockBits(&bmpData);
}

// ========== E5 文件加载 ==========

static Gdiplus::Bitmap* LoadBitmapFromE5(const CString& e5FilePath, int imageIndex, CString& debugInfo)
{
	CFile file;
	if (!file.Open(e5FilePath, CFile::modeRead | CFile::shareDenyNone)) {
		debugInfo.Format(_T("open fail: %s"), (LPCTSTR)e5FilePath);
		return nullptr;
	}

	ULONGLONG indexOffset = 0x110 + (ULONGLONG)imageIndex * 12;
	if (indexOffset + 12 > file.GetLength()) {
		debugInfo.Format(_T("index out of range: idx=%d, off=%llu, flen=%llu"), imageIndex, indexOffset, file.GetLength());
		file.Close();
		return nullptr;
	}
	file.Seek(indexOffset, CFile::begin);

	BYTE rawBytes[12] = {};
	file.Read(rawBytes, 12);

	// E5 big-endian: 手动从字节组装
	DWORD imageSize = ((DWORD)rawBytes[0] << 24) | ((DWORD)rawBytes[1] << 16) | ((DWORD)rawBytes[2] << 8) | rawBytes[3];
	DWORD dataOffset = ((DWORD)rawBytes[8] << 24) | ((DWORD)rawBytes[9] << 16) | ((DWORD)rawBytes[10] << 8) | rawBytes[11];

	if (imageSize == 0 || (ULONGLONG)dataOffset + imageSize > file.GetLength()) {
		debugInfo.Format(_T("idx=%d raw=%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X sz=%u off=%u flen=%llu"),
			imageIndex,
			rawBytes[0], rawBytes[1], rawBytes[2], rawBytes[3],
			rawBytes[4], rawBytes[5], rawBytes[6], rawBytes[7],
			rawBytes[8], rawBytes[9], rawBytes[10], rawBytes[11],
			imageSize, dataOffset, file.GetLength());
		file.Close();
		return nullptr;
	}

	file.Seek(dataOffset, CFile::begin);
	BYTE* buffer = new BYTE[imageSize];
	UINT bytesRead = file.Read(buffer, imageSize);
	file.Close();

	if (bytesRead < imageSize) {
		debugInfo.Format(_T("read incomplete: got %u, want %u"), bytesRead, imageSize);
		delete[] buffer;
		return nullptr;
	}

	// 先尝试GDI+标准解码(BMP/PNG/JPG)
	bool isStandard = false;
	if (imageSize >= 2 && buffer[0] == 'B' && buffer[1] == 'M') isStandard = true;
	if (imageSize >= 4 && buffer[0] == 0x89 && buffer[1] == 0x50) isStandard = true;
	if (imageSize >= 2 && buffer[0] == 0xFF && buffer[1] == 0xD8) isStandard = true;

	if (isStandard) {
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, imageSize);
		if (hMem) {
			void* pMem = GlobalLock(hMem);
			memcpy(pMem, buffer, imageSize);
			GlobalUnlock(hMem);
			IStream* pStream = nullptr;
			if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
				Gdiplus::Bitmap* pBmp = Gdiplus::Bitmap::FromStream(pStream);
				pStream->Release();
				if (pBmp && pBmp->GetLastStatus() == Gdiplus::Ok) {
					delete[] buffer;
					ApplyTopLeftTransparency(pBmp);
					return pBmp;
				}
				if (pBmp) delete pBmp;
			} else {
				GlobalFree(hMem);
			}
		}
	}

	// 非标准格式: 尝试作为256色裸像素数据(48x1280)解析
	Gdiplus::Bitmap* pBmp = CreateBitmapFromRaw8bpp(buffer, imageSize);
	delete[] buffer;

	if (pBmp) return pBmp;

	debugInfo.Format(_T("unknown format idx=%d sz=%u"), imageIndex, imageSize);
	return nullptr;
}

bool CScenePreviewWnd::LoadMapImage(int mapID, const wchar_t* modPath)
{
	if (mapID < 0) { m_debugInfo = _T("mapID < 0"); return false; }
	if (mapID == m_cachedMapID && m_pMapBitmap) return true;

	if (m_pMapBitmap) {
		delete m_pMapBitmap;
		m_pMapBitmap = nullptr;
	}
	m_cachedMapID = -1;

	CString rootPath = GetRootPath(modPath);
	if (rootPath.IsEmpty()) { m_debugInfo = _T("path parse fail"); return false; }
	CString e5Path = rootPath + _T("E5\\Mmap.e5");

	m_pMapBitmap = LoadBitmapFromE5(e5Path, mapID, m_debugInfo);
	if (m_pMapBitmap) {
		m_cachedMapID = mapID;
		return true;
	}
	return false;
}

// ========== 角色形象加载 ==========

bool CScenePreviewWnd::LoadSpriteData(const wchar_t* modPath)
{
	if (m_spriteDataLoaded) return true;

	CString rootPath = GetRootPath(modPath);
	if (rootPath.IsEmpty()) return false;

	// 读Ekd5.exe偏移0xE1000处的形象编号表 (1024 * 2字节, 小端序)
	CString exePath = rootPath + _T("Ekd5.exe");
	CFile file;
	if (!file.Open(exePath, CFile::modeRead | CFile::shareDenyNone)) return false;

	ULONGLONG offset = 0xE1000;
	if (offset + 1024 * 2 > file.GetLength()) {
		file.Close();
		return false;
	}
	file.Seek(offset, CFile::begin);
	file.Read(m_charImageIDs, 1024 * 2); // 小端序, 直接读
	file.Close();

	m_spriteDataLoaded = true;
	return true;
}

Gdiplus::Bitmap* CScenePreviewWnd::GetSpriteBitmap(int charID, const wchar_t* modPath)
{
	if (charID < 0 || charID >= 1024) return nullptr;
	if (!LoadSpriteData(modPath)) return nullptr;

	int imageID = m_charImageIDs[charID];
	int frontIdx = imageID * 2;
	int backIdx = imageID * 2 + 1;

	// 确保两张都加载了
	if (m_spriteCache.find(frontIdx) == m_spriteCache.end()) {
		CString rootPath = GetRootPath(modPath);
		CString e5Path = rootPath + _T("Pmapobj.e5");
		CString dbg;
		Gdiplus::Bitmap* pFront = LoadBitmapFromE5(e5Path, frontIdx, dbg);
		m_spriteCache[frontIdx] = pFront;
		if (!pFront) {
			m_debugInfo.Format(_T("char%d img%d front[%d]: %s | %s"),
				charID, imageID, frontIdx, (LPCTSTR)dbg, (LPCTSTR)e5Path);
		}
		Gdiplus::Bitmap* pBack = LoadBitmapFromE5(e5Path, backIdx, dbg);
		m_spriteCache[backIdx] = pBack;
	}

	return nullptr;
}

void CScenePreviewWnd::DrawCharacter(Gdiplus::Graphics& g, const CharacterState& ch,
	int canvasOffX, int canvasOffY, float scale)
{
	if (ch.id < 0 || ch.id >= 1024 || !m_spriteDataLoaded) return;

	int imageID = m_charImageIDs[ch.id];
	int frontIdx = imageID * 2;
	int backIdx = imageID * 2 + 1;

	// 方向决定使用正面/背面以及是否翻转
	// 南(2)=正面, 东(1)=正面翻转, 北(0)=背面, 西(3)=背面翻转
	bool useFront = (ch.direction == 2 || ch.direction == 1);
	bool flipH = (ch.direction == 1 || ch.direction == 3);

	int spriteIdx = useFront ? frontIdx : backIdx;
	auto it = m_spriteCache.find(spriteIdx);
	Gdiplus::Bitmap* pSheet = (it != m_spriteCache.end()) ? it->second : nullptr;

	CPoint gamePx = GameToPixel(ch.x, ch.y);
	int drawX = canvasOffX + (int)(gamePx.x * scale);
	int drawY = canvasOffY + (int)(gamePx.y * scale);

	if (!pSheet) {
		// fallback: draw circle placeholder with debug info
		int radius = (int)(12 * scale);
		if (radius < 6) radius = 6;
		Gdiplus::SolidBrush circleBrush(Gdiplus::Color(180, 30, 100, 220));
		g.FillEllipse(&circleBrush, drawX - radius, drawY - radius, radius * 2, radius * 2);
		Gdiplus::Pen circlePen(Gdiplus::Color(220, 255, 255, 255), 1.5f);
		g.DrawEllipse(&circlePen, drawX - radius, drawY - radius, radius * 2, radius * 2);
		DrawDirectionArrow(g, drawX, drawY, ch.direction);

		CString label;
		label.Format(_T("c%d/i%d"), ch.id, imageID);
		Gdiplus::Font labelFont(L"Microsoft YaHei", 7);
		Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
		Gdiplus::StringFormat sf;
		sf.SetAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::RectF labelRect((float)(drawX - 30), (float)(drawY - radius),
			60.0f, (float)(radius * 2));
		g.DrawString(label, -1, &labelFont, labelRect, &sf, &textBrush);
		return;
	}

	// 计算帧: 动作0=帧0, 动作N(N>=1)=帧N+2
	const int FRAME_W = 48, FRAME_H = 64;
	int frame = (ch.action == 0) ? 0 : (ch.action + 2);
	int maxFrames = (int)pSheet->GetHeight() / FRAME_H;
	if (frame >= maxFrames) frame = 0;

	int srcY = frame * FRAME_H;

	// 绘制尺寸 (缩放)
	int dstW = (int)(FRAME_W * scale);
	int dstH = (int)(FRAME_H * scale);
	// 角色脚底对齐到坐标点
	int dstX = drawX - dstW / 2;
	int dstY = drawY - dstH;

	if (flipH) {
		// 水平翻转: 用矩阵变换
		Gdiplus::Matrix oldMatrix;
		g.GetTransform(&oldMatrix);

		// 翻转: 先平移到中心, 缩放X为-1, 再平移回来
		Gdiplus::Matrix flipMatrix;
		flipMatrix.Translate((float)(dstX + dstW), (float)dstY);
		flipMatrix.Scale(-1.0f, 1.0f);
		flipMatrix.Translate(0, 0);
		g.SetTransform(&flipMatrix);

		g.DrawImage(pSheet,
			Gdiplus::Rect(0, 0, dstW, dstH),
			0, srcY, FRAME_W, FRAME_H,
			Gdiplus::UnitPixel);

		g.SetTransform(&oldMatrix);
	}
	else {
		g.DrawImage(pSheet,
			Gdiplus::Rect(dstX, dstY, dstW, dstH),
			0, srcY, FRAME_W, FRAME_H,
			Gdiplus::UnitPixel);
	}
}

// ========== 更新入口 ==========

void CScenePreviewWnd::UpdatePreview(CTreeCtrl& tree, HTREEITEM hSelectedItem, const wchar_t* modPath)
{
	CString newModPath = modPath ? modPath : L"";

	// 检测mod路径变化 — 切换到不同mod的剧本时清除所有缓存
	CString newRoot = GetRootPath(newModPath);
	CString oldRoot = GetRootPath(m_modPath);
	if (newRoot.CompareNoCase(oldRoot) != 0) {
		// mod根目录变了，清除地图缓存
		if (m_pMapBitmap) {
			delete m_pMapBitmap;
			m_pMapBitmap = nullptr;
		}
		m_cachedMapID = -1;

		// 清除精灵缓存
		for (auto& pair : m_spriteCache) {
			delete pair.second;
		}
		m_spriteCache.clear();
		m_spriteDataLoaded = false;
		memset(m_charImageIDs, 0, sizeof(m_charImageIDs));
	}

	m_modPath = newModPath;
	m_pTreeCtrl = &tree;
	m_hCurItem = hSelectedItem;
	EvaluateToItem(tree, hSelectedItem);

	if (m_state.mapID >= 0 && modPath) {
		int imageIndex = m_state.mapID + 40;
		if (imageIndex != m_cachedMapID) {
			LoadMapImage(imageIndex, modPath);
		}
	}

	// 确保角色形象数据已加载
	if (modPath && !m_spriteDataLoaded) {
		LoadSpriteData(modPath);
	}

	// 预加载当前场景中角色的精灵图
	if (modPath) {
		for (const auto& ch : m_state.characters) {
			GetSpriteBitmap(ch.id, modPath);
		}
	}

	Invalidate();
}

// ========== 渲染 ==========

void CScenePreviewWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect clientRect;
	GetClientRect(&clientRect);

	// 画布区域从控件下方开始
	CRect canvasRect = clientRect;
	canvasRect.top = m_canvasTop;

	if (canvasRect.Width() <= 0 || canvasRect.Height() <= 0) return;

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, canvasRect.Width(), canvasRect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&bmp);

	memDC.FillSolidRect(0, 0, canvasRect.Width(), canvasRect.Height(), RGB(32, 32, 32));

	Gdiplus::Graphics graphics(memDC.m_hDC);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);

	const int MAP_W = 640, MAP_H = 400;
	float scaleX = (float)canvasRect.Width() / MAP_W;
	float scaleY = (float)canvasRect.Height() / MAP_H;
	float scale = min(scaleX, scaleY);

	int canvasW = (int)(MAP_W * scale);
	int canvasH = (int)(MAP_H * scale);
	int offsetX = (canvasRect.Width() - canvasW) / 2;
	int offsetY = (canvasRect.Height() - canvasH) / 2;

	// 保存渲染参数(相对于窗口的实际坐标)
	m_offsetX = offsetX + canvasRect.left;
	m_offsetY = offsetY + canvasRect.top;
	m_scale = scale;

	// 绘制背景地图
	if (m_pMapBitmap) {
		graphics.DrawImage(m_pMapBitmap,
			Gdiplus::Rect(offsetX, offsetY, canvasW, canvasH));
	} else {
		Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 48, 48, 48));
		graphics.FillRectangle(&bgBrush, offsetX, offsetY, canvasW, canvasH);
		Gdiplus::Pen borderPen(Gdiplus::Color(255, 80, 80, 80), 1.0f);
		graphics.DrawRectangle(&borderPen, offsetX, offsetY, canvasW - 1, canvasH - 1);

		if (m_state.mapID >= 0) {
			Gdiplus::Font font(L"Microsoft YaHei", 9);
			Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 200, 150, 150));
			CString msg;
			msg.Format(_T("mapID=%d  %s"), m_state.mapID, (LPCTSTR)m_debugInfo);
			graphics.DrawString(msg, -1, &font,
				Gdiplus::PointF((float)offsetX + 10, (float)offsetY + 10), &textBrush);
		}
	}

	// 绘制等角网格
	if (m_bShowGrid) {
		Gdiplus::Pen gridPen(Gdiplus::Color(80, 200, 200, 200), 1.0f);
		const int G_MIN = 0, G_MAX = 100;
		for (int i = G_MIN; i <= G_MAX; i++) {
			CPoint p1 = GameToPixel(i, G_MIN);
			CPoint p2 = GameToPixel(i, G_MAX);
			graphics.DrawLine(&gridPen,
				offsetX + (int)(p1.x * scale), offsetY + (int)(p1.y * scale),
				offsetX + (int)(p2.x * scale), offsetY + (int)(p2.y * scale));
			p1 = GameToPixel(G_MIN, i);
			p2 = GameToPixel(G_MAX, i);
			graphics.DrawLine(&gridPen,
				offsetX + (int)(p1.x * scale), offsetY + (int)(p1.y * scale),
				offsetX + (int)(p2.x * scale), offsetY + (int)(p2.y * scale));
		}
	}

	// 绘制角色形象
	for (const auto& ch : m_state.characters) {
		DrawCharacter(graphics, ch, offsetX, offsetY, scale);
	}

	// 放置/移动模式: 绘制跟随鼠标的角色预览(半透明)
	{
		int ghostCharID = -1;
		CPoint ghostCursor = { -1, -1 };
		int ghostDir = 2, ghostAction = 0;

		if (m_bPlaceMode && m_selectedCharID >= 0 && m_placeCursorPos.x >= 0) {
			ghostCharID = m_selectedCharID;
			ghostCursor = m_placeCursorPos;
			ghostDir = m_placeDir;
			ghostAction = m_placeAction;
		}
		else if (m_bMoveMode && m_moveCharID >= 0 && m_moveCursorPos.x >= 0) {
			ghostCharID = m_moveCharID;
			ghostCursor = m_moveCursorPos;
			// 移动模式用角色当前的朝向和动作
			for (const auto& ch : m_state.characters) {
				if (ch.id == m_moveCharID) {
					ghostDir = ch.direction;
					ghostAction = ch.action;
					break;
				}
			}
		}

		if (ghostCharID >= 0 && ghostCursor.x >= 0) {
			CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含Ctrl吸附)
			Gdiplus::ColorMatrix cm = {
				1, 0, 0, 0, 0,
				0, 1, 0, 0, 0,
				0, 0, 1, 0, 0,
				0, 0, 0, 0.5f, 0,
				0, 0, 0, 0, 1
			};
			Gdiplus::ImageAttributes ia;
			ia.SetColorMatrix(&cm);

			CPoint gamePxForSnap = GameToPixel(game.x, game.y);
			int snapScreenX = offsetX + (int)(gamePxForSnap.x * scale);
			int snapScreenY = offsetY + (int)(gamePxForSnap.y * scale);

			if (m_spriteDataLoaded && ghostCharID >= 0 && ghostCharID < 1024) {
				int imageID = m_charImageIDs[ghostCharID];
				bool useFront = (ghostDir == 2 || ghostDir == 1);
				bool flipH = (ghostDir == 1 || ghostDir == 3);
				int spriteIdx = useFront ? imageID * 2 : imageID * 2 + 1;
				auto it = m_spriteCache.find(spriteIdx);
				Gdiplus::Bitmap* pSheet = (it != m_spriteCache.end()) ? it->second : nullptr;
				if (pSheet) {
					const int FRAME_W = 48, FRAME_H = 64;
					int frame = (ghostAction == 0) ? 0 : (ghostAction + 2);
					int maxFrames = (int)pSheet->GetHeight() / FRAME_H;
					if (frame >= maxFrames) frame = 0;
					int srcY = frame * FRAME_H;
					int dstW = (int)(FRAME_W * scale);
					int dstH = (int)(FRAME_H * scale);
					int dstX = snapScreenX - dstW / 2;
					int dstY = snapScreenY - dstH;
					if (flipH) {
						Gdiplus::Matrix oldM;
						graphics.GetTransform(&oldM);
						Gdiplus::Matrix flipM;
						flipM.Translate((float)(dstX + dstW), (float)dstY);
						flipM.Scale(-1.0f, 1.0f);
						graphics.SetTransform(&flipM);
						graphics.DrawImage(pSheet,
							Gdiplus::Rect(0, 0, dstW, dstH),
							0, srcY, FRAME_W, FRAME_H,
							Gdiplus::UnitPixel, &ia);
						graphics.SetTransform(&oldM);
					} else {
						graphics.DrawImage(pSheet,
							Gdiplus::Rect(dstX, dstY, dstW, dstH),
							0, srcY, FRAME_W, FRAME_H,
							Gdiplus::UnitPixel, &ia);
					}
				} else {
					int radius = (int)(12 * scale);
					if (radius < 6) radius = 6;
					Gdiplus::SolidBrush ghostBrush(Gdiplus::Color(90, 30, 100, 220));
					graphics.FillEllipse(&ghostBrush, snapScreenX - radius, snapScreenY - radius, radius * 2, radius * 2);
				}
			}

			// 吸附时画十字标记
			if (m_bSnapped) {
				Gdiplus::Pen snapPen(Gdiplus::Color(220, 0, 255, 100), 2.0f);
				int crossSize = (int)(8 * scale);
				if (crossSize < 4) crossSize = 4;
				graphics.DrawLine(&snapPen, snapScreenX - crossSize, snapScreenY,
					snapScreenX + crossSize, snapScreenY);
				graphics.DrawLine(&snapPen, snapScreenX, snapScreenY - crossSize,
					snapScreenX, snapScreenY + crossSize);
			}
		}
	}

	// 状态信息
	{
		static const wchar_t* s_dirNames[] = { L"北", L"东", L"南", L"西" };
		static const wchar_t* s_gesNames[] = {
			L"普通",L"下跪",L"脸红",L"举手",L"哭",L"伸手",L"作揖",
			L"盘坐脸红",L"盘坐举手",L"盘坐哭",L"倒下",L"单膝跪地",L"被缚",
			L"挥剑扬起",L"挥剑劈下",L"活埋",L"起身",L"单手举起",L"未知",L"变量",L"无"
		};

		Gdiplus::Font infoFont(L"Microsoft YaHei", 9);
		Gdiplus::SolidBrush infoBrush(Gdiplus::Color(200, 200, 200, 200));

		CString info;
		info.Format(_T("地图:%d  角色:%d"), m_state.mapID, (int)m_state.characters.size());

		// 鼠标在画布区域内时显示坐标和角色信息
		if (m_mousePos.y >= m_canvasTop) {
			// 拖拽模式下显示吸附后的坐标
			bool inDragMode = m_bAdjustMode || m_bMoveMode || m_bPlaceMode;
			CPoint game = inDragMode ? m_snappedGamePos : PixelToGame(m_mousePos.x, m_mousePos.y);
			CString posStr;
			if (m_bSnapped && inDragMode) {
				posStr.Format(_T("  坐标:(%d,%d) [吸附]"), game.x, game.y);
			} else {
				posStr.Format(_T("  坐标:(%d,%d)"), game.x, game.y);
			}
			info += posStr;

			// 检查鼠标下方的角色
			int hoverIdx = HitTestCharacter(m_mousePos);
			if (hoverIdx >= 0 && hoverIdx < (int)m_state.characters.size()) {
				const auto& ch = m_state.characters[hoverIdx];
				// 找到角色名称
				CString charName;
				if (m_pPer2) {
					charName = m_pPer2[ch.id];
				}
				if (charName.IsEmpty()) {
					charName.Format(_T("ID:%d"), ch.id);
				}

				const wchar_t* dirName = (ch.direction >= 0 && ch.direction < 4) ?
					s_dirNames[ch.direction] : L"?";
				const wchar_t* gesName = (ch.action >= 0 && ch.action < 21) ?
					s_gesNames[ch.action] : L"?";
				CString charInfo;
				charInfo.Format(_T("  [%s] 朝向:%s 动作:%s 位置:(%d,%d)"),
					(LPCTSTR)charName, dirName, gesName, ch.x, ch.y);
				info += charInfo;
			}
		}

		// 拖拽模式下显示当前朝向和动作
		if (m_bPlaceMode && m_selectedCharID >= 0) {
			const wchar_t* dName = (m_placeDir >= 0 && m_placeDir < 4) ?
				s_dirNames[m_placeDir] : L"?";
			const wchar_t* aName = (m_placeAction >= 0 && m_placeAction < 21) ?
				s_gesNames[m_placeAction] : L"?";
			CString placeInfo;
			placeInfo.Format(_T("  放置:朝%s %s (滚轮换向/Shift+滚轮换动作)"), dName, aName);
			info += placeInfo;
		}
		else if ((m_bAdjustMode && m_adjustCharID >= 0) ||
				 (m_bMoveMode && m_moveCharID >= 0)) {
			int targetID = m_bAdjustMode ? m_adjustCharID : m_moveCharID;
			for (const auto& ch : m_state.characters) {
				if (ch.id == targetID) {
					const wchar_t* dName = (ch.direction >= 0 && ch.direction < 4) ?
						s_dirNames[ch.direction] : L"?";
					const wchar_t* aName = (ch.action >= 0 && ch.action < 21) ?
						s_gesNames[ch.action] : L"?";
					CString modeInfo;
					modeInfo.Format(_T("  朝%s %s (滚轮换向/Shift+滚轮换动作)"), dName, aName);
					info += modeInfo;
					break;
				}
			}
		}

		graphics.DrawString(info, -1, &infoFont,
			Gdiplus::PointF(5.0f, 5.0f), &infoBrush);
	}

	dc.BitBlt(canvasRect.left, canvasRect.top, canvasRect.Width(), canvasRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

// ========== 武将列表 ==========

void CScenePreviewWnd::InitCharList(wchar_t(*pPer2)[30])
{
	if (!pPer2) return;
	// 如果per2指针变了(切换到不同View/mod)，需要重新加载列表
	if (m_charListInited && m_pPer2 == pPer2) return;
	m_pPer2 = pPer2;
	m_charListInited = true;
	RefreshCharList();
}

void CScenePreviewWnd::RefreshCharList()
{
	if (!m_pPer2 || !m_listChars.GetSafeHwnd()) return;

	CString filter;
	m_editSearch.GetWindowTextW(filter);
	filter.Trim();

	m_listChars.ResetContent();
	for (int i = 0; i < 1024; i++) {
		if (filter.IsEmpty() || wcsstr(m_pPer2[i], (LPCTSTR)filter) != nullptr) {
			int idx = m_listChars.AddString(m_pPer2[i]);
			m_listChars.SetItemData(idx, (DWORD_PTR)i);
		}
	}
	m_selectedCharID = -1;
}

void CScenePreviewWnd::OnSearchChange()
{
	RefreshCharList();
}

void CScenePreviewWnd::OnCharListSelChange()
{
	int sel = m_listChars.GetCurSel();
	if (sel >= 0) {
		m_selectedCharID = (int)m_listChars.GetItemData(sel);
		// 进入放置模式，角色形象跟随鼠标
		m_bPlaceMode = true;
		m_placeCursorPos = { -1, -1 };
		m_placeDir = 2;      // 默认朝南
		m_placeAction = 0;   // 默认普通动作
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
		// 预加载该角色的精灵图(正面+背面)
		if (!m_modPath.IsEmpty()) {
			GetSpriteBitmap(m_selectedCharID, m_modPath);
		}
	} else {
		m_selectedCharID = -1;
		m_bPlaceMode = false;
	}
}

// ========== 坐标反算 ==========

CPoint CScenePreviewWnd::PixelToGame(int screenX, int screenY)
{
	float cx = (screenX - m_offsetX) / m_scale;
	float cy = (screenY - m_offsetY) / m_scale;
	// px = 8*(X-Y+42), py = 4*(X+Y-50)
	// X-Y = px/8 - 42, X+Y = py/4 + 50
	float xMinusY = cx / 8.0f - 42.0f;
	float xPlusY = cy / 4.0f + 50.0f;
	int gx = (int)roundf((xMinusY + xPlusY) / 2.0f);
	int gy = (int)roundf((xPlusY - xMinusY) / 2.0f);
	return CPoint(gx, gy);
}

CPoint CScenePreviewWnd::SnapToGrid(CPoint gamePos, int excludeCharID)
{
	// 收集所有角色坐标（排除正在操作的角色）
	std::vector<CPoint> pts;
	for (const auto& ch : m_state.characters) {
		if (ch.id == excludeCharID) continue;
		pts.push_back(CPoint(ch.x, ch.y));
	}
	if (pts.empty()) return gamePos;

	// === 第一优先级: 等距延伸候选 ===
	std::vector<CPoint> spacingCands;
	if (pts.size() >= 2) {
		for (int i = 0; i < (int)pts.size(); i++) {
			for (int j = 0; j < (int)pts.size(); j++) {
				if (i == j) continue;
				int dx = pts[j].x - pts[i].x;
				int dy = pts[j].y - pts[i].y;
				if (dx == 0 && dy == 0) continue;

				// 检查i→j之间是否有其他角色（非相邻对则跳过）
				bool hasMiddle = false;
				for (int k = 0; k < (int)pts.size(); k++) {
					if (k == i || k == j) continue;
					int dxik = pts[k].x - pts[i].x;
					int dyik = pts[k].y - pts[i].y;
					if (dx != 0 && dy != 0) {
						if ((long long)dxik * dy == (long long)dyik * dx &&
							dxik * dx > 0 && dyik * dy > 0 &&
							abs(dxik) < abs(dx) && abs(dyik) < abs(dy)) {
							hasMiddle = true; break;
						}
					} else if (dx == 0) {
						if (dxik == 0 && dyik * dy > 0 && abs(dyik) < abs(dy)) {
							hasMiddle = true; break;
						}
					} else {
						if (dyik == 0 && dxik * dx > 0 && abs(dxik) < abs(dx)) {
							hasMiddle = true; break;
						}
					}
				}
				if (hasMiddle) continue;

				// j向同方向延伸1~3步，取最近一个空位
				for (int step = 1; step <= 3; step++) {
					CPoint ext(pts[j].x + dx * step, pts[j].y + dy * step);
					bool occupied = false;
					for (const auto& p : pts) {
						if (p.x == ext.x && p.y == ext.y) { occupied = true; break; }
					}
					if (!occupied) {
						spacingCands.push_back(ext);
						break;
					}
				}
			}
		}
	}

	// 在等距候选中找最近的
	float bestSpacingDist = 1e9f;
	CPoint bestSpacingPt = gamePos;
	for (const auto& c : spacingCands) {
		float fdx = (float)(c.x - gamePos.x);
		float fdy = (float)(c.y - gamePos.y);
		float dist = fdx * fdx + fdy * fdy;
		if (dist < bestSpacingDist) {
			bestSpacingDist = dist;
			bestSpacingPt = c;
		}
	}
	if (bestSpacingDist <= 25.0f) return bestSpacingPt; // 5格距离

	// === 第二优先级: 单轴对齐 ===
	float bestAlignDist = 1e9f;
	CPoint bestAlignPt = gamePos;
	for (const auto& p : pts) {
		float fdx = (float)(p.x - gamePos.x);
		if (fdx * fdx < bestAlignDist) {
			bestAlignDist = fdx * fdx;
			bestAlignPt = CPoint(p.x, gamePos.y);
		}
		float fdy = (float)(p.y - gamePos.y);
		if (fdy * fdy < bestAlignDist) {
			bestAlignDist = fdy * fdy;
			bestAlignPt = CPoint(gamePos.x, p.y);
		}
	}
	if (bestAlignDist <= 9.0f) return bestAlignPt; // 3格距离

	return gamePos;
}

int CScenePreviewWnd::HitTestCharacter(CPoint screenPt)
{
	const int FRAME_W = 48, FRAME_H = 64;
	int bestIdx = -1;
	float bestDist = 1e9f;

	for (int i = 0; i < (int)m_state.characters.size(); i++) {
		const auto& ch = m_state.characters[i];
		CPoint gp = GameToPixel(ch.x, ch.y);
		float sx = m_offsetX + gp.x * m_scale;
		float sy = m_offsetY + gp.y * m_scale;
		float dstW = FRAME_W * m_scale;
		float dstH = FRAME_H * m_scale;
		float left = sx - dstW / 2;
		float top = sy - dstH;
		if (screenPt.x >= left && screenPt.x <= left + dstW &&
			screenPt.y >= top && screenPt.y <= top + dstH) {
			float dx = screenPt.x - sx;
			float dy = screenPt.y - (sy - dstH / 2);
			float dist = dx * dx + dy * dy;
			if (dist < bestDist) {
				bestDist = dist;
				bestIdx = i;
			}
		}
	}
	return bestIdx;
}

// ========== 获取活动View ==========

static CcczEditor2View* GetActiveEditorView()
{
	CMDIFrameWnd* pMain = (CMDIFrameWnd*)AfxGetMainWnd();
	if (!pMain) return nullptr;
	CMDIChildWnd* pChild = pMain->MDIGetActive();
	if (!pChild) return nullptr;
	CView* pView = pChild->GetActiveView();
	if (!pView) return nullptr;
	return dynamic_cast<CcczEditor2View*>(pView);
}

// ========== 点击放置角色 ==========

void CScenePreviewWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (point.y < m_canvasTop) {
		CDockablePane::OnLButtonDown(nFlags, point);
		return;
	}

	// 调整位置模式: 放下角色,修改已有出现指令
	if (m_bAdjustMode && m_adjustCharID >= 0) {
		ReleaseCapture();
		CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)

		// 找到section中该角色的武将出现指令并修改坐标/朝向/动作
		HTREEITEM hAppear = FindAppearInstruction(m_adjustCharID);
		if (hAppear && m_pTreeCtrl) {
			ItemData* data = (ItemData*)m_pTreeCtrl->GetItemData(hAppear);
			if (data && data->id == 48 && (int)data->int_data.size() >= 5) {
				data->int_data[1] = game.x;
				data->int_data[2] = game.y;
				// 如果用户在调整时通过滚轮改了朝向/动作，也写入
				for (const auto& ch : m_state.characters) {
					if (ch.id == m_adjustCharID) {
						data->int_data[3] = ch.direction;
						data->int_data[4] = ch.action;
						break;
					}
				}
				CcczEditor2View* pView = GetActiveEditorView();
				if (pView) {
					pView->UpdateShow(hAppear);
					pView->GetDocument()->SetModifiedFlag(TRUE);
					UpdatePreview(*m_pTreeCtrl, m_hCurItem,
						m_modPath.IsEmpty() ? nullptr : (LPCWSTR)m_modPath);
				}
			}
		}
		m_bAdjustMode = false;
		m_adjustCharID = -1;
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 移动模式: 点击目标位置
	if (m_bMoveMode && m_moveCharID >= 0) {
		ReleaseCapture();
		CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)
		InsertMoveInstruction(m_moveCharID, game.x, game.y);
		m_bMoveMode = false;
		m_moveCharID = -1;
		m_moveCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 放置角色模式
	if (m_bPlaceMode && m_selectedCharID >= 0) {
		ReleaseCapture();
		CcczEditor2View* pView = GetActiveEditorView();
		if (pView) {
			CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)
			int intData[5] = { m_selectedCharID, game.x, game.y, m_placeDir, m_placeAction };
			pView->InsertInstructionAt(48, pView->cur_item, intData, 5);
		}
		m_listChars.SetCurSel(-1);
		m_selectedCharID = -1;
		m_bPlaceMode = false;
		m_placeCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	CDockablePane::OnLButtonDown(nFlags, point);
}

void CScenePreviewWnd::InsertAppearInstruction(int charID, int screenX, int screenY)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;

	CPoint game = PixelToGame(screenX, screenY);
	int intData[5] = { charID, game.x, game.y, 2, 0 };
	pView->InsertInstructionAt(48, pView->cur_item, intData, 5);
}

// ========== 右键菜单 ==========

void CScenePreviewWnd::OnContextMenu(CWnd* pWnd, CPoint point)
{
	// 特殊模式下不弹菜单
	if (m_bPlaceMode || m_bAdjustMode || m_bMoveMode) return;

	// 右键取消模式后屏蔽一次菜单
	if (m_bSuppressContextMenu) {
		m_bSuppressContextMenu = false;
		return;
	}

	CPoint clientPt = point;
	ScreenToClient(&clientPt);

	if (clientPt.y < m_canvasTop) {
		CDockablePane::OnContextMenu(pWnd, point);
		return;
	}

	int hitIdx = HitTestCharacter(clientPt);

	// 空地右键: 显示网格开关
	if (hitIdx < 0) {
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenuW(MF_STRING | (m_bShowGrid ? MF_CHECKED : 0),
			ID_SCENE_TOGGLE_GRID, L"显示网格");
		int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this);
		if (cmd == ID_SCENE_TOGGLE_GRID) {
			m_bShowGrid = !m_bShowGrid;
			Invalidate();
		}
		return;
	}

	m_hitCharID = m_state.characters[hitIdx].id;

	static const wchar_t* dirNames[] = { L"朝北", L"朝东", L"朝南", L"朝西" };
	static const wchar_t* actionNames[] = {
		L"普通", L"下跪", L"脸红", L"举手", L"哭", L"伸手", L"作揖",
		L"盘坐脸红", L"盘坐举手", L"盘坐哭", L"倒下", L"单膝跪地", L"被缚",
		L"挥剑扬起", L"挥剑劈下", L"活埋", L"起身", L"单手举起", L"未知", L"变量"
	};

	CMenu menuDir;
	menuDir.CreatePopupMenu();
	for (int i = 0; i < 4; i++)
		menuDir.AppendMenuW(MF_STRING, ID_SCENE_DIR_0 + i, dirNames[i]);

	CMenu menuAction;
	menuAction.CreatePopupMenu();
	for (int i = 0; i < 20; i++)
		menuAction.AppendMenuW(MF_STRING, ID_SCENE_ACTION_0 + i, actionNames[i]);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenuW(MF_STRING, ID_SCENE_ADJUST, L"调整位置");
	menu.AppendMenuW(MF_SEPARATOR, 0, (LPCTSTR)nullptr);
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuDir.m_hMenu, L"设置朝向");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuAction.m_hMenu, L"设置动作");
	menu.AppendMenuW(MF_STRING, ID_SCENE_MOVE, L"移动到...");
	menu.AppendMenuW(MF_STRING, ID_SCENE_DISAPPEAR, L"消失");

	int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this);
	if (cmd >= ID_SCENE_DIR_0 && cmd <= ID_SCENE_DIR_3) {
		InsertTurnInstruction(m_hitCharID, cmd - ID_SCENE_DIR_0);
	} else if (cmd >= ID_SCENE_ACTION_0 && cmd <= ID_SCENE_ACTION_19) {
		InsertActionInstruction(m_hitCharID, cmd - ID_SCENE_ACTION_0);
	} else if (cmd == ID_SCENE_DISAPPEAR) {
		InsertDisappearInstruction(m_hitCharID);
	} else if (cmd == ID_SCENE_MOVE) {
		m_bMoveMode = true;
		m_moveCharID = m_hitCharID;
		m_moveCursorPos = { -1, -1 };
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	} else if (cmd == ID_SCENE_ADJUST) {
		m_bAdjustMode = true;
		m_adjustCharID = m_hitCharID;
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	}
}

void CScenePreviewWnd::InsertTurnInstruction(int charID, int direction)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	int intData[5] = { charID, 0, direction, 0, 0 };
	pView->InsertInstructionAt(51, pView->cur_item, intData, 5);
}

void CScenePreviewWnd::InsertActionInstruction(int charID, int action)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	int intData[5] = { charID, action, 0, 0, 0 };
	pView->InsertInstructionAt(52, pView->cur_item, intData, 5);
}

void CScenePreviewWnd::InsertDisappearInstruction(int charID)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	// 武将消失: [0]=模式(0=单人), [1]=人物ID
	int intData[7] = { 0, charID, 0, 0, 0, 0, 0 };
	pView->InsertInstructionAt(49, pView->cur_item, intData, 7);
}

void CScenePreviewWnd::InsertMoveInstruction(int charID, int gameX, int gameY)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	// 武将移动: [0]=模式(0=data角色), [1]=人物ID, [2]=0, [3]=目标X, [4]=目标Y, [5]=方向
	int dir = -1;
	for (const auto& ch : m_state.characters) {
		if (ch.id == charID) { dir = ch.direction; break; }
	}
	int intData[6] = { 0, charID, 0, gameX, gameY, dir };
	pView->InsertInstructionAt(50, pView->cur_item, intData, 6);
}

// ========== 调整位置模式 ==========

void CScenePreviewWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	m_mousePos = point;
	bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

	if (m_bAdjustMode && m_adjustCharID >= 0) {
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_adjustCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		for (auto& ch : m_state.characters) {
			if (ch.id == m_adjustCharID) {
				ch.x = game.x;
				ch.y = game.y;
				break;
			}
		}
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	}
	else if (m_bMoveMode && m_moveCharID >= 0) {
		m_moveCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_moveCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPlaceMode && m_selectedCharID >= 0) {
		m_placeCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, -1);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else {
		m_bSnapped = false;
		if (point.y >= m_canvasTop) {
			Invalidate();
		}
	}
	CDockablePane::OnMouseMove(nFlags, point);
}

void CScenePreviewWnd::OnRButtonDown(UINT nFlags, CPoint point)
{
	// 右键取消调整/移动/放置模式，并屏蔽随后的右键菜单
	if (m_bAdjustMode) {
		ReleaseCapture();
		m_bAdjustMode = false;
		m_adjustCharID = -1;
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		if (m_pTreeCtrl && m_hCurItem) {
			EvaluateToItem(*m_pTreeCtrl, m_hCurItem);
			Invalidate();
		}
		return;
	}
	if (m_bMoveMode) {
		ReleaseCapture();
		m_bMoveMode = false;
		m_moveCharID = -1;
		m_moveCursorPos = { -1, -1 };
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bPlaceMode) {
		ReleaseCapture();
		m_bPlaceMode = false;
		m_selectedCharID = -1;
		m_placeCursorPos = { -1, -1 };
		m_listChars.SetCurSel(-1);
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	CDockablePane::OnRButtonDown(nFlags, point);
}

BOOL CScenePreviewWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	bool shiftDown = (nFlags & MK_SHIFT) != 0;
	int delta = (zDelta > 0) ? 1 : -1;

	// 放置模式
	if (m_bPlaceMode && m_selectedCharID >= 0) {
		if (shiftDown) {
			m_placeAction = (m_placeAction + delta + 20) % 20;
		} else {
			m_placeDir = (m_placeDir + delta + 4) % 4;
		}
		Invalidate();
		return TRUE;
	}

	// 调整位置模式
	if (m_bAdjustMode && m_adjustCharID >= 0) {
		for (auto& ch : m_state.characters) {
			if (ch.id == m_adjustCharID) {
				if (shiftDown) {
					ch.action = (ch.action + delta + 20) % 20;
				} else {
					ch.direction = (ch.direction + delta + 4) % 4;
				}
				break;
			}
		}
		Invalidate();
		return TRUE;
	}

	// 移动模式
	if (m_bMoveMode && m_moveCharID >= 0) {
		for (auto& ch : m_state.characters) {
			if (ch.id == m_moveCharID) {
				if (shiftDown) {
					ch.action = (ch.action + delta + 20) % 20;
				} else {
					ch.direction = (ch.direction + delta + 4) % 4;
				}
				break;
			}
		}
		Invalidate();
		return TRUE;
	}

	return CDockablePane::OnMouseWheel(nFlags, zDelta, pt);
}

// 从section头遍历到当前指令为止,找到离当前指令最近的该角色的武将出现指令
HTREEITEM CScenePreviewWnd::FindAppearInstruction(int charID)
{
	if (!m_pTreeCtrl || !m_hCurItem) return nullptr;

	// 找到所属Section: 向上找id==-2的节点
	HTREEITEM hSection = m_hCurItem;
	while (hSection) {
		ItemData* d = (ItemData*)m_pTreeCtrl->GetItemData(hSection);
		if (d && d->id == -2) break;
		hSection = m_pTreeCtrl->GetParentItem(hSection);
	}
	if (!hSection) return nullptr;

	// 与EvaluateToItem相同的遍历方式：遍历直接子节点及其子节点
	HTREEITEM hFound = nullptr;
	HTREEITEM hCur = m_pTreeCtrl->GetChildItem(hSection);
	while (hCur) {
		ItemData* d = (ItemData*)m_pTreeCtrl->GetItemData(hCur);
		if (d && d->id == 48 && (int)d->int_data.size() >= 1 && d->int_data[0] == charID)
			hFound = hCur;
		if (hCur == m_hCurItem) break;

		// 遍历子节点（子事件内部）
		HTREEITEM hSub = m_pTreeCtrl->GetChildItem(hCur);
		while (hSub) {
			ItemData* sd = (ItemData*)m_pTreeCtrl->GetItemData(hSub);
			if (sd && sd->id == 48 && (int)sd->int_data.size() >= 1 && sd->int_data[0] == charID)
				hFound = hSub;
			if (hSub == m_hCurItem) return hFound;
			hSub = m_pTreeCtrl->GetNextSiblingItem(hSub);
		}

		hCur = m_pTreeCtrl->GetNextSiblingItem(hCur);
	}
	return hFound;
}
