
#include "pch.h"
#include "framework.h"

#include "PropertiesWnd.h"
#include "Resource.h"
#include "MainFrm.h"
#include "cczEditor2.h"
#include <string>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CResourceViewBar

CPropertiesWnd::CPropertiesWnd() noexcept
{
	m_nComboHeight = 0;
}

CPropertiesWnd::~CPropertiesWnd()
{
}

BEGIN_MESSAGE_MAP(CPropertiesWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_COMMAND(ID_EXPAND_ALL, OnExpandAllProperties)
	ON_UPDATE_COMMAND_UI(ID_EXPAND_ALL, OnUpdateExpandAllProperties)
	ON_COMMAND(ID_SORTPROPERTIES, OnSortProperties)
	ON_UPDATE_COMMAND_UI(ID_SORTPROPERTIES, OnUpdateSortProperties)
	ON_COMMAND(ID_PROPERTIES1, OnProperties1)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES1, OnUpdateProperties1)
	ON_COMMAND(ID_PROPERTIES2, OnProperties2)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES2, OnUpdateProperties2)
	ON_WM_SETFOCUS()
	ON_WM_SETTINGCHANGE()

	ON_REGISTERED_MESSAGE(AFX_WM_PROPERTY_CHANGED, OnPropertyChanged)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CResourceViewBar 消息处理程序

void CPropertiesWnd::AdjustLayout()
{
	if (GetSafeHwnd () == nullptr || (AfxGetMainWnd() != nullptr && AfxGetMainWnd()->IsIconic()))
	{
		return;
	}

	CRect rectClient;
	GetClientRect(rectClient);

	int cyTlb = m_wndToolBar.CalcFixedLayout(FALSE, TRUE).cy;

	m_wndObjectCombo.SetWindowPos(nullptr, rectClient.left, rectClient.top, rectClient.Width(), m_nComboHeight, SWP_NOACTIVATE | SWP_NOZORDER);
	m_wndToolBar.SetWindowPos(nullptr, rectClient.left, rectClient.top + m_nComboHeight, rectClient.Width(), cyTlb, SWP_NOACTIVATE | SWP_NOZORDER);
	m_wndPropList.SetWindowPos(nullptr, rectClient.left, rectClient.top + m_nComboHeight + cyTlb, rectClient.Width(), rectClient.Height() -(m_nComboHeight+cyTlb), SWP_NOACTIVATE | SWP_NOZORDER);
}

int CPropertiesWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	// 创建组合: 
	const DWORD dwViewStyle = WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER | CBS_SORT | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

	if (!m_wndObjectCombo.Create(dwViewStyle, rectDummy, this, 1))
	{
		TRACE0("未能创建属性组合 \n");
		return -1;      // 未能创建
	}

	m_wndObjectCombo.AddString(_T("应用程序"));
	m_wndObjectCombo.AddString(_T("属性窗口"));
	m_wndObjectCombo.SetCurSel(0);

	CRect rectCombo;
	m_wndObjectCombo.GetClientRect (&rectCombo);

	m_nComboHeight = rectCombo.Height();

	if (!m_wndPropList.Create(WS_VISIBLE | WS_CHILD, rectDummy, this, 2))
	{
		TRACE0("未能创建属性网格\n");
		return -1;      // 未能创建
	}

	InitPropList();

	m_wndToolBar.Create(this, AFX_DEFAULT_TOOLBAR_STYLE, IDR_PROPERTIES);
	m_wndToolBar.LoadToolBar(IDR_PROPERTIES, 0, 0, TRUE /* 已锁定*/);
	m_wndToolBar.CleanUpLockedImages();
	m_wndToolBar.LoadBitmap(theApp.m_bHiColorIcons ? IDB_PROPERTIES_HC : IDR_PROPERTIES, 0, 0, TRUE /* 锁定*/);

	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() | CBRS_TOOLTIPS | CBRS_FLYBY);
	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() & ~(CBRS_GRIPPER | CBRS_SIZE_DYNAMIC | CBRS_BORDER_TOP | CBRS_BORDER_BOTTOM | CBRS_BORDER_LEFT | CBRS_BORDER_RIGHT));
	m_wndToolBar.SetOwner(this);

	// 所有命令将通过此控件路由，而不是通过主框架路由: 
	m_wndToolBar.SetRouteCommandsViaFrame(FALSE);

	AdjustLayout();
	return 0;
}

void CPropertiesWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	AdjustLayout();
}

void CPropertiesWnd::OnExpandAllProperties()
{
	m_wndPropList.ExpandAll();
}

void CPropertiesWnd::OnUpdateExpandAllProperties(CCmdUI* /* pCmdUI */)
{
}

void CPropertiesWnd::OnSortProperties()
{
	m_wndPropList.SetAlphabeticMode(!m_wndPropList.IsAlphabeticMode());
}

void CPropertiesWnd::OnUpdateSortProperties(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_wndPropList.IsAlphabeticMode());
}

void CPropertiesWnd::OnProperties1()
{
	// TODO: 在此处添加命令处理程序代码
}

void CPropertiesWnd::OnUpdateProperties1(CCmdUI* /*pCmdUI*/)
{
	// TODO: 在此处添加命令更新 UI 处理程序代码
}

void CPropertiesWnd::OnProperties2()
{
	// TODO: 在此处添加命令处理程序代码
}

void CPropertiesWnd::OnUpdateProperties2(CCmdUI* /*pCmdUI*/)
{
	// TODO: 在此处添加命令更新 UI 处理程序代码
}

int CString2Int2(CString str)
{
	if (str.GetLength() == 0)return 0;
	int sum = 0;
	for (int i = 0; i < str.GetLength(); i++)
	{
		sum *= 10;
		sum += str[i] - '0';
	}
	return sum;
}

void CPropertiesWnd::InitPropList()
{
	wchar_t path[512];
	GetModuleFileName(NULL, path, MAX_PATH);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\')break;
	}
	wchar_t real_path[512]; wcsncpy_s(real_path, path, pos + 1);
	wchar_t ini_path[512]; wcscpy_s(ini_path, real_path); wcscat_s(ini_path, L"CczCustom.ini");
	CStdioFile f;
	if (f.Open(ini_path, CStdioFile::modeRead)) {
		char savestring[20];
		wchar_t res_string[20];
		for (int i = 0; i < 248; i++) {
			int len;
			for (len = 0; len < 16; len++) {
				f.Read(savestring + len, 1);
				if (f.GetPosition() == f.GetLength()) {
					len++;
					break;
				}
				if (savestring[len] == '\n') {
					savestring[len] = 0;
					break;
				}
			}
			MultiByteToWideChar(936, 0, savestring, -1, res_string, len);
			int rgb_pos = 0; int sums = 0;
			for (int j = 0; j <= len; j++) {
				if (res_string[j] == 0)break;
				if (res_string[j] == ',' || j == len) {
					theApp.custom_color[i / 2][i % 2][rgb_pos] = sums;
					sums = 0;
					rgb_pos++;
				}
				else {
					sums *= 10;
					sums += res_string[j] - '0';
				}
			}
		}
		f.Close();
	}
	else {
		for (int i = 0; i < 124; i++)
			for (int j = 0; j < 2; j++)
				for (int k = 0; k < 3; k++)
					theApp.custom_color[i][j][k] = j * 255;
		theApp.custom_color[1][0][0] = 255;
		theApp.custom_color[2][0][0] = 50;
		theApp.custom_color[2][0][1] = 188;
		theApp.custom_color[2][0][2] = 50;
		theApp.custom_color[119][1][0] = 255;
		theApp.custom_color[119][1][1] = 255;
		theApp.custom_color[120][1][0] = 255;
		theApp.custom_color[124][1][1] = 255;
		theApp.custom_color[124][1][2] = 255;
	}
	wchar_t res[10];
	if (GetPrivateProfileString(L"Config", L"ItemHeight", L"", res, 4, ini_path)) {
		theApp.custom_height = CString2Int2(CString(res));
	}
	else theApp.custom_height = 20;

	if (GetPrivateProfileString(L"Config", L"UseFont", L"", res, 4, ini_path)) {
		theApp.custom_use_font = res[0] == '1' ? true : false;
	}

	if (GetPrivateProfileString(L"Config", L"NightMode", L"", res, 4, ini_path)) {
		theApp.custom_night_mode = res[0] == '1' ? true : false;
	}

	/*从注册表中读取字体*/
	LPLOGFONT p = NULL;
	UINT nLen = sizeof(LOGFONT);
	if (theApp.GetProfileBinary(_T("CCZEDITOR"), _T("FONT"), (LPBYTE*)&p, &nLen)) {
		theApp.custom_font = *LPLOGFONT(p);
		theApp.custom_fonts.DeleteObject();
		theApp.custom_fonts.CreatePointFontIndirect(&theApp.custom_font, 0);
	}
	else theApp.custom_use_font = false;

	SetPropListFont();

	m_wndPropList.EnableHeaderCtrl(FALSE);
	m_wndPropList.EnableDescriptionArea();
	m_wndPropList.SetVSDotNetLook();
	m_wndPropList.MarkModifiedProperties();
	
	CMFCPropertyGridProperty* pGroup2 = new CMFCPropertyGridProperty(_T("个性化"));

	m_wndPropList.AddProperty(pGroup2);

	CMFCPropertyGridColorProperty* pColorProp;
	for (int i = 0; i < 124; i++) {
		wchar_t show[50] = { 0 };
		if (i < 123) wcscpy_s(show, code[i]);
		else wcscpy_s(show, L"嵌套");
		pColorProp = new CMFCPropertyGridColorProperty(show, RGB(theApp.custom_color[i][0][0], theApp.custom_color[i][0][1], theApp.custom_color[i][0][2]), nullptr, _T("指定指令的字体颜色"));
		pColorProp->EnableOtherButton(_T("其他..."));
		pColorProp->EnableAutomaticButton(_T("默认"), ::GetSysColor(0));
		pGroup2->AddSubItem(pColorProp);
		pColorProp = new CMFCPropertyGridColorProperty(L"", RGB(theApp.custom_color[i][1][0], theApp.custom_color[i][1][1], theApp.custom_color[i][1][2]), nullptr, _T("指定指令的背景颜色"));
		pColorProp->EnableOtherButton(_T("其他..."));
		pColorProp->EnableAutomaticButton(_T("默认"), ::GetSysColor(0));
		pGroup2->AddSubItem(pColorProp);
	}
	pGroup2->AddSubItem(new CMFCPropertyGridProperty(_T("行间距"), std::to_wstring(theApp.custom_height).c_str(), _T("指令间的间距，每台电脑的合适间距都不同")));


	/*CFont* font = CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT));
	font->GetLogFont(&theApp.custom_font);

	_tcscpy_s(theApp.custom_font.lfFaceName, _T("宋体, Arial"));*/

	pGroup2->AddSubItem(new CMFCPropertyGridFontProperty(_T("字体"), theApp.custom_font, CF_EFFECTS | CF_SCREENFONTS, _T("指定窗口的默认字体")));
	pGroup2->AddSubItem(new CMFCPropertyGridProperty(_T("自定义字体"), (_variant_t)theApp.custom_use_font, _T("选择False则使用默认字体，选择True则使用自定义字体")));
	pGroup2->AddSubItem(new CMFCPropertyGridProperty(_T("夜间模式"), (_variant_t)theApp.custom_night_mode, _T("选择True则使用夜间模式")));


	/*CMFCPropertyGridProperty* pGroup3 = new CMFCPropertyGridProperty(_T("错误修复"));

	m_wndPropList.AddProperty(pGroup3);
	pGroup3->AddSubItem(new CMFCPropertyGridProperty(_T("错误section"), L"0", _T("如果是第i个scene的第j个section超额了，请输入i*100+j")));*/
	/*
	CMFCPropertyGridProperty* pGroup1 = new CMFCPropertyGridProperty(_T("外观"));

	pGroup1->AddSubItem(new CMFCPropertyGridProperty(_T("三维外观"), (_variant_t) false, _T("指定窗口的字体不使用粗体，并且控件将使用三维边框")));

	CMFCPropertyGridProperty* pProp = new CMFCPropertyGridProperty(_T("边框"), _T("对话框外框"), _T("其中之一: “无”、“细”、“可调整大小”或“对话框外框”"));
	pProp->AddOption(_T("无"));
	pProp->AddOption(_T("细"));
	pProp->AddOption(_T("可调整大小"));
	pProp->AddOption(_T("对话框外框"));
	pProp->AllowEdit(FALSE);

	pGroup1->AddSubItem(pProp);
	pGroup1->AddSubItem(new CMFCPropertyGridProperty(_T("标题"), (_variant_t) _T("关于"), _T("指定窗口标题栏中显示的文本")));

	m_wndPropList.AddProperty(pGroup1);

	CMFCPropertyGridProperty* pSize = new CMFCPropertyGridProperty(_T("窗口大小"), 0, TRUE);

	pProp = new CMFCPropertyGridProperty(_T("高度"), (_variant_t) 250l, _T("指定窗口的高度"));
	pProp->EnableSpinControl(TRUE, 50, 300);
	pSize->AddSubItem(pProp);

	pProp = new CMFCPropertyGridProperty( _T("宽度"), (_variant_t) 150l, _T("指定窗口的宽度"));
	pProp->EnableSpinControl(TRUE, 50, 200);
	pSize->AddSubItem(pProp);

	m_wndPropList.AddProperty(pSize);

	CMFCPropertyGridProperty* pGroup2 = new CMFCPropertyGridProperty(_T("字体"));

	LOGFONT lf;
	CFont* font = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	font->GetLogFont(&lf);

	_tcscpy_s(lf.lfFaceName, _T("宋体, Arial"));

	pGroup2->AddSubItem(new CMFCPropertyGridFontProperty(_T("字体"), lf, CF_EFFECTS | CF_SCREENFONTS, _T("指定窗口的默认字体")));
	pGroup2->AddSubItem(new CMFCPropertyGridProperty(_T("使用系统字体"), (_variant_t) true, _T("指定窗口使用“MS Shell Dlg”字体")));

	m_wndPropList.AddProperty(pGroup2);

	CMFCPropertyGridProperty* pGroup3 = new CMFCPropertyGridProperty(_T("杂项"));
	pProp = new CMFCPropertyGridProperty(_T("(名称)"), _T("应用程序"));
	pProp->Enable(FALSE);
	pGroup3->AddSubItem(pProp);

	CMFCPropertyGridColorProperty* pColorProp = new CMFCPropertyGridColorProperty(_T("窗口颜色"), RGB(210, 192, 254), nullptr, _T("指定默认的窗口颜色"));
	pColorProp->EnableOtherButton(_T("其他..."));
	pColorProp->EnableAutomaticButton(_T("默认"), ::GetSysColor(COLOR_3DFACE));
	pGroup3->AddSubItem(pColorProp);

	static const TCHAR szFilter[] = _T("图标文件(*.ico)|*.ico|所有文件(*.*)|*.*||");
	pGroup3->AddSubItem(new CMFCPropertyGridFileProperty(_T("图标"), TRUE, _T(""), _T("ico"), 0, szFilter, _T("指定窗口图标")));

	pGroup3->AddSubItem(new CMFCPropertyGridFileProperty(_T("文件夹"), _T("c:\\")));

	m_wndPropList.AddProperty(pGroup3);

	CMFCPropertyGridProperty* pGroup4 = new CMFCPropertyGridProperty(_T("层次结构"));

	CMFCPropertyGridProperty* pGroup41 = new CMFCPropertyGridProperty(_T("第一个子级"));
	pGroup4->AddSubItem(pGroup41);

	CMFCPropertyGridProperty* pGroup411 = new CMFCPropertyGridProperty(_T("第二个子级"));
	pGroup41->AddSubItem(pGroup411);

	pGroup411->AddSubItem(new CMFCPropertyGridProperty(_T("项 1"), (_variant_t) _T("值 1"), _T("此为说明")));
	pGroup411->AddSubItem(new CMFCPropertyGridProperty(_T("项 2"), (_variant_t) _T("值 2"), _T("此为说明")));
	pGroup411->AddSubItem(new CMFCPropertyGridProperty(_T("项 3"), (_variant_t) _T("值 3"), _T("此为说明")));

	pGroup4->Expand(FALSE);
	m_wndPropList.AddProperty(pGroup4);
	*/
}

void CPropertiesWnd::OnSetFocus(CWnd* pOldWnd)
{
	CDockablePane::OnSetFocus(pOldWnd);
	m_wndPropList.SetFocus();
}

void CPropertiesWnd::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CDockablePane::OnSettingChange(uFlags, lpszSection);
	SetPropListFont();
}

LRESULT CPropertiesWnd::OnPropertyChanged(WPARAM wParam, LPARAM lParam)
{
	CMFCPropertyGridProperty* pChangedPropertyItem = (CMFCPropertyGridProperty*)lParam;

	if (!pChangedPropertyItem) return 0;

	CMFCPropertyGridProperty* p;
	for (int i = 0; i < 124; i++)
	{
		for (int j = 0; j < 2; j++) {
			p = m_wndPropList.GetProperty(0)->GetSubItem(i * 2 + j);
			DWORD col = (DWORD)p->GetValue().intVal;
			byte r = col % 256;
			byte g = col / 256 % 256;
			byte b = col / 256 / 256 % 256;
			theApp.custom_color[i][j][0] = r;
			theApp.custom_color[i][j][1] = g;
			theApp.custom_color[i][j][2] = b;
		}
	}
	p = m_wndPropList.GetProperty(0)->GetSubItem(124 * 2);
	CString val = (CString)p->GetValue();
	theApp.custom_height = CString2Int2(val);

	CMFCPropertyGridFontProperty* q;
	q = (CMFCPropertyGridFontProperty*)m_wndPropList.GetProperty(0)->GetSubItem(124 * 2 + 1);
	theApp.custom_font = *q->GetLogFont();
	theApp.custom_fonts.DeleteObject();
	theApp.custom_fonts.CreatePointFontIndirect(&theApp.custom_font, 0);

	p = m_wndPropList.GetProperty(0)->GetSubItem(124 * 2 + 2);
	val = (CString)p->GetValue();
	theApp.custom_use_font = val[0] == '0' ? false : true;

	p = m_wndPropList.GetProperty(0)->GetSubItem(124 * 2 + 3);
	val = (CString)p->GetValue();
	theApp.custom_night_mode = val[0] == '0' ? false : true;
	
	/*改变字体*/
	/*CFont font;
	font.CreatePointFontIndirect(&theApp.custom_font, 0);
	POSITION PosDocTemplate = theApp.GetFirstDocTemplatePosition();
	if (PosDocTemplate)
	{
		CDocTemplate* pDocTemplate = theApp.GetNextDocTemplate(PosDocTemplate);

		POSITION PosDoc = pDocTemplate->GetFirstDocPosition();
		while (PosDoc)
		{
			CDocument* pDoc = pDocTemplate->GetNextDoc(PosDoc);

			POSITION PosView = pDoc->GetFirstViewPosition();
			CView* pView = (CView*)pDoc->GetNextView(PosView);
			
			CTreeCtrl& tree = ((CcczEditor2View*)pView)->GetTreeCtrl();
			tree.SetFont(&font);
		}
	}*/

	return 0;
}

void CPropertiesWnd::SetPropListFont()
{
	::DeleteObject(m_fntPropList.Detach());

	LOGFONT lf;
	afxGlobalData.fontRegular.GetLogFont(&lf);

	NONCLIENTMETRICS info;
	info.cbSize = sizeof(info);

	afxGlobalData.GetNonClientMetrics(info);

	lf.lfHeight = info.lfMenuFont.lfHeight;
	lf.lfWeight = info.lfMenuFont.lfWeight;
	lf.lfItalic = info.lfMenuFont.lfItalic;

	m_fntPropList.CreateFontIndirect(&lf);

	m_wndPropList.SetFont(&m_fntPropList);
	m_wndObjectCombo.SetFont(&m_fntPropList);
}
