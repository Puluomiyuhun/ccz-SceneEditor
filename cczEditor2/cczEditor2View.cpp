
// cczEditor2View.cpp: CcczEditor2View 类的实现
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS 可以在实现预览、缩略图和搜索筛选器句柄的
// ATL 项目中进行定义，并允许与该项目共享文档代码。
#ifndef SHARED_HANDLERS
#include "cczEditor2.h"
#endif

#include "cczEditor2Doc.h"
#include "cczEditor2View.h"

#include "CMyTreeCtrl.h"
#include "LongCharData.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#include "MainFrm.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include "myTool.h"
#include "TextWrapAssist.h"
#include "TextWrapEditSession.h"
#pragma comment(lib, "version.lib")
#include <atlimage.h>

// CcczEditor2View

IMPLEMENT_DYNCREATE(CcczEditor2View, CTreeView)

BEGIN_MESSAGE_MAP(CcczEditor2View, CTreeView)
	// 标准打印命令
	ON_COMMAND(ID_FILE_PRINT, &CTreeView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CTreeView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CcczEditor2View::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()

	ON_NOTIFY_REFLECT(NM_CLICK, OnNMClick)
	ON_NOTIFY_REFLECT(NM_RCLICK, OnNMRClick)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNMDBLClick)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNMCustomdraw)
	//ON_NOTIFY(NM_CUSTOMDRAW, IDC_MY_LIST, OnCustomdrawMyList)

	ON_COMMAND(ID_EDIT_MODIFY, &CcczEditor2View::OnEditModify)
	ON_COMMAND(ID_EDIT_ADD, &CcczEditor2View::OnEditAdd)
	ON_COMMAND(ID_EDIT_ADDI, &CcczEditor2View::OnEditAddi)
	ON_COMMAND(ID_DELETE, &CcczEditor2View::OnEditDelete)
	ON_WM_LBUTTONDOWN()
	ON_WM_KEYDOWN()
	ON_COMMAND(ID_COPY, &CcczEditor2View::OnCopyMsg)
	ON_COMMAND(ID_PASTE, &CcczEditor2View::OnPaste)
	ON_WM_KEYUP()
	ON_COMMAND(ID_CUT, &CcczEditor2View::OnCut)
	ON_COMMAND(ID_EXPAND, &CcczEditor2View::OnExpand)
	ON_COMMAND(ID_JUMP, &CcczEditor2View::OnJump)
	ON_COMMAND(ID_32792, &CcczEditor2View::OnSearchItem)
	ON_COMMAND(ID_32793, &CcczEditor2View::OnSearchItemNext)
	ON_COMMAND(ID_32794, &CcczEditor2View::OnVarList)
	ON_WM_DROPFILES()
	ON_COMMAND(ID_32800, &CcczEditor2View::OnMoveUp)
	ON_COMMAND(ID_32801, &CcczEditor2View::OnMoveDown)
	ON_NOTIFY_REFLECT(TVN_SELCHANGED, &CcczEditor2View::OnTvnSelchanged)
	ON_COMMAND(ID_EDIT_DUPLICATE, &CcczEditor2View::OnEditDuplicate)
	ON_COMMAND(ID_EDIT_BATCH, &CcczEditor2View::OnEditBatch)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

// CcczEditor2View 构造/析构

CcczEditor2View::CcczEditor2View() noexcept
{
	// TODO: 在此处添加构造代码

}

CcczEditor2View::~CcczEditor2View()
{
}

void CcczEditor2View::OnDestroy()
{
	CMainFrame* frame = static_cast<CMainFrame*>(AfxGetMainWnd());
	if (frame != nullptr)
		frame->m_wndScenePreview.EditorTreeDestroyed(
			GetTreeCtrl().GetSafeHwnd(), *GetDocument());
	CTreeView::OnDestroy();
}

void CcczEditor2View::OnActivateView(BOOL activate, CView* activateView,
	CView* deactivateView)
{
	CTreeView::OnActivateView(activate, activateView, deactivateView);
	if (activate && GetSafeHwnd() != nullptr &&
		GetTreeCtrl().GetSafeHwnd() != nullptr && GetDocument() != nullptr) {
		CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
		if (frame != nullptr) {
			cur_item = GetTreeCtrl().GetSelectedItem();
			frame->m_wndScenePreview.ActivateScriptPreview(
				GetTreeCtrl(), cur_item, *GetDocument());
		}
	}
}

BOOL CcczEditor2View::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: 在此处通过修改
	//  CREATESTRUCT cs 来修改窗口类或样式
	night_mode = theApp.custom_night_mode;
	// CTreeView owns the common-control window class and requires lpszClass to
	// remain null until its PreCreateWindow installs that class. Background
	// painting is handled by OnEraseBkgnd, so a custom window class is unnecessary.
	return CTreeView::PreCreateWindow(cs);
}


void CcczEditor2View::OnDraw(CDC* pDC)
{
	CcczEditor2Doc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	pDC->SetBkColor(0);
	// TODO: 在此处为本机数据添加绘制代码
}


void CcczEditor2View::OnInitialUpdate()
{
	if (!m_bBgLoaded)
	{
		//m_bBgLoaded = (m_bgImage.Load(_T("D:\\1.bmp")) == S_OK);
	}
	InitBaseData();
	CTreeView::OnInitialUpdate();
	if (this->GetDocument()->pathName != L"")CreateFileTree();
	else {
		CreateNewTree();
	}
	cur_item = GetTreeCtrl().GetSelectedItem();
	RefreshScenePreview();
}
void CcczEditor2View::ReadString()
{
	wchar_t path[512];
	GetModuleFileName(NULL, path, MAX_PATH);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\')break;
	}
	wchar_t real_path[512];
	wcsncpy_s(real_path, path, pos + 1);
	wchar_t ini_path[512];
	wcscpy_s(ini_path, real_path);
	wcscat_s(ini_path, L"CczString.ini");
	CStdioFile f;
	if (f.Open(ini_path, CStdioFile::modeRead)) {
		char savestring[4000];
		wchar_t res_string[4000];
		wchar_t res[15][200][40];
		int sum[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		for (int i = 0; i < 15; i++) {
			int len;
			for (len = 0; len < 2000; len++) {
				f.Read(savestring + len, 1);
				if (f.GetPosition() == f.GetLength()) {
					len++;
					savestring[len] = 0;
					break;
				}
				if (savestring[len] == '\n') {
					savestring[len] = 0;
					break;
				}
			}
			int wlen = MultiByteToWideChar(936, 0, savestring, len, NULL, 0);
			MultiByteToWideChar(936, 0, savestring, len, res_string, wlen);
			res_string[wlen] = 0;
			int pos = 0;
			for (int j = 0; j < wlen; j++) {
				if (res_string[j] == 0)break;
				if (res_string[j] == ',') {
					res[i][sum[i]][pos] = 0;
					sum[i]++;
					pos = 0;
				}
				else {
					res[i][sum[i]][pos] = res_string[j];
					pos++;
				}
			}
		}
		for (int i = 0; i < sum[0]; i++)wcscpy_s(code[i], res[0][i]);
		for (int i = 0; i < sum[1]; i++)wcscpy_s(face_condition[i], res[1][i]);
		for (int i = 0; i < sum[2]; i++)wcscpy_s(per_condition[i], res[2][i]);
		for (int i = 0; i < sum[3]; i++)wcscpy_s(per_condition_war[i], res[3][i]);
		for (int i = 0; i < sum[4]; i++)wcscpy_s(compare[i], res[4][i]);
		for (int i = 0; i < sum[5]; i++)wcscpy_s(compare2[i], res[5][i]);
		for (int i = 0; i < sum[6]; i++)wcscpy_s(operate[i], res[6][i]);
		for (int i = 0; i < sum[7]; i++)wcscpy_s(operate2[i], res[7][i]);
		for (int i = 0; i < sum[8]; i++)wcscpy_s(join_condition[i], res[8][i]);
		for (int i = 0; i < sum[9]; i++)wcscpy_s(policy[i], res[9][i]);
		for (int i = 0; i < sum[10]; i++)wcscpy_s(var_kind[i], res[10][i]);
		for (int i = 0; i < sum[11]; i++)wcscpy_s(var_kind2[i], res[11][i]);
		for (int i = 0; i < sum[12]; i++)wcscpy_s(all_condition[i], res[12][i]);
		for (int i = 0; i < sum[13]; i++)wcscpy_s(xxcs[i], res[13][i]);
		for (int i = 0; i < 6; i++)wcscpy_s(debuff[i], res[14][i]);
		code_sum = sum[0]; face_condition_sum = sum[1]; per_condition_sum = sum[2];
		per_condition_war_sum = sum[3]; compare_sum = sum[4]; compare2_sum = sum[5];
		operate_sum = sum[6]; operate2_sum = sum[7]; join_condition_sum = sum[8];
		policy_sum = sum[9]; var_kind_sum = sum[10]; var_kind2_sum = sum[11];
		all_condition_sum = sum[12]; xxcs_sum = sum[13];
		f.Close();
	}
}

void CcczEditor2View::ReadDataFromIni()
{
	wchar_t path[512];
	wcscpy_s(path, GetDocument()->pathName);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\') {
			break;
		}
	}
	wchar_t real_path[512];
	wcsncpy_s(real_path, path, pos + 1); 
	wchar_t ini_path[512];
	wcscpy_s(ini_path, real_path);
	wcscat_s(ini_path, L"CczSceneEditor2.ini");
	wchar_t res[5][10];
	if (GetPrivateProfileString(L"Config", L"ItemWeaponSum", L"", res[0], 4, ini_path)) {
		if (set_weapon < 0)set_weapon = CString2Int(CString(res[0]));
	}
	if (GetPrivateProfileString(L"Config", L"ItemDefenseSum", L"", res[1], 4, ini_path)) {
		if (set_armor < 0)set_armor = CString2Int(CString(res[1]));
	}
	if(GetPrivateProfileString(L"Config", L"ItemAssistSum", L"", res[2], 4, ini_path)) {
		if (set_product < 0)set_product = CString2Int(CString(res[2]));
	}
	if (GetPrivateProfileString(L"Config", L"CharLvMax", L"", res[3], 4, ini_path)) {
		if (set_level < 0)set_level = CString2Int(CString(res[3]));
	}
	if (GetPrivateProfileString(L"Config", L"RSMax", L"", res[4], 4, ini_path)) {
		if (set_rs < 0)set_rs = CString2Int(CString(res[4]));
	}
	wchar_t new_path[200];
	GetDocument()->modName = new wchar_t[200];
	if (GetPrivateProfileString(L"Config", L"ExePath", L"", new_path, 200, ini_path)) {
		wcscpy_s((wchar_t*)GetDocument()->modName, 200, new_path);
	}
	else
		wcscpy_s((wchar_t*)GetDocument()->modName, 200, (wchar_t*)GetDocument()->pathName);
}

void CcczEditor2View::ReadDataFromFile()
{
	/*尝试直接读mod文件夹下的文件*/
	wchar_t path[512];
	wcscpy_s(path, GetDocument()->modName);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\') {
			if (path[pos - 1] != 'S' && path[pos - 2] != 'R' 
				&& path[pos - 1] != 's' && path[pos - 2] != 'r')break;
		}
	}
	wchar_t real_path[512];
	wcsncpy_s(real_path, path, pos + 1);

	int version = 64;

	if (!read_exe) {
		CFile f;
		/*读取ekd5.exe*/
		wchar_t exe_path[512];
		wcscpy_s(exe_path, real_path);
		wcscat_s(exe_path, L"Ekd5.exe");
		char per_gbk[30];
		wchar_t per_utf[30];
		/*先获取版本*/
		DWORD dwHandle;
		DWORD dwSize = GetFileVersionInfoSize(exe_path, &dwHandle);
		if (dwSize > 0) {
			LPVOID lpData = new BYTE[dwSize];
			if (GetFileVersionInfo(exe_path, dwHandle, dwSize, lpData)) {
				LPVOID lpBuffer;
				UINT uLen;
				if (VerQueryValue(lpData, TEXT("\\"), &lpBuffer, &uLen)) {
					VS_FIXEDFILEINFO* pFileInfo = (VS_FIXEDFILEINFO*)lpBuffer;
					DWORD dwFileVersionLS = pFileInfo->dwFileVersionLS;
					version = HIWORD(dwFileVersionLS) * 10 + LOWORD(dwFileVersionLS);
				}
			}
			if (version >= 64) {
				if (set_weapon < 0) set_weapon = 70;
				if (set_armor < 0) set_armor = 39;
				if (set_product < 0) set_product = 41;
			}
			if (set_tejibase < 0) {
				if (version == 65)set_tejibase = 0x9E800;
				else if(version == 64)set_tejibase = 0xD0D60;
				else set_tejibase = 0xD0FA0;
			}

			if (f.Open(exe_path, CFile::modeRead)) {
				read_exe = true;
				//f.Seek(0x68F1, CFile::begin);
				f.Seek(0x7CD3, CFile::begin);
				char l;
				f.Read(&l, 1);
				if (set_level < 0) set_level = l;
				for (int i = 0; i < 80; i++)
				{
					strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
					f.Seek(0xD18D0 + i * 9, CFile::begin);
					f.Read(per_gbk, 8);
					int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
					if (wlen >= 8) {
						wlen = 8;
						per_gbk[8] = 0;
					}
					MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
					per_utf[8] = 0;
					wcscat_s(job[i], per_utf);
				}
				if (set_tejibase > 0) {
					for (int i = 0; i < 256; i++)
					{
						strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
						f.Seek(set_tejibase + i * 16, CFile::begin);
						f.Read(per_gbk, 14);
						int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
						if (wlen >= 14) {
							wlen = 14;
							per_gbk[14] = 0;
						}
						MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
						per_utf[14] = 0;
						wcscat_s(teji[i], per_utf);
					}
				}
				f.Close();
			}
		}
	}

	if (!read_e5) {
		CFile f;
		/*读取data.e5*/
		wchar_t data_path[512];
		wcscpy_s(data_path, real_path);
		wcscat_s(data_path, L"Data.e5");
		char per_gbk[30];
		wchar_t per_utf[30];
		if (f.Open(data_path, CFile::modeRead)) {
			read_e5 = true;
			for (int i = 0; i < 1024; i++)
			{
				strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
				f.Seek(0x18C + i * 0x20, CFile::begin);
				f.Read(per_gbk, 12);
				int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
				if (wlen >= 12) {
					wlen = 12;
					per_gbk[12] = 0;
				}
				MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
				per_utf[12] = 0;
				wcscat_s(per1[i], per_utf);
				wcscat_s(per2[i], per_utf);
			}
			for (int i = 0; i < 104; i++)
			{
				strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
				f.Seek(0x818C + i * 0x19, CFile::begin);
				f.Read(per_gbk, 16);
				int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
				if (wlen >= 16) {
					wlen = 16;
					per_gbk[16] = 0;
				}
				MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
				per_utf[16] = 0;
				wcscat_s(item[i], per_utf);
			}
			for (int i = 0; i < 144; i++)
			{
				strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
				f.Seek(0xB204 + i * 0x61, CFile::begin);
				f.Read(per_gbk, 10);
				int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
				if (wlen >= 10) {
					wlen = 10;
					per_gbk[10] = 0;
				}
				MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
				per_utf[10] = 0;
				wcscat_s(meff[i], per_utf);
			}
			f.Close();
		}

		/*读取star.e5*/
		if (version >= 64) {
			wchar_t star_path[512];
			wcscpy_s(star_path, real_path);
			wcscat_s(star_path, L"star.e5");
			char per_gbk[30];
			wchar_t per_utf[30];
			if (f.Open(star_path, CFile::modeRead)) {
				for (int i = 0; i < 151; i++)
				{
					strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
					f.Seek(i * 0x19, CFile::begin);
					f.Read(per_gbk, 16);
					int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
					if (wlen >= 16) {
						wlen = 16;
						per_gbk[16] = 0;
					}
					MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
					per_utf[16] = 0;
					wcscat_s(item[i + 104], per_utf);
				}
				f.Close();
			}
		}

		/*读取item.e5*/
		if (version >= 64) {
			wchar_t star_path[512];
			wcscpy_s(star_path, real_path);
			wcscat_s(star_path, L"item.e5");
			char per_gbk[30];
			wchar_t per_utf[30];
			if (f.Open(star_path, CFile::modeRead)) {
				extend = true;
				for (int i = 0; i < 255; i++)
				{
					strcpy_s(per_gbk, ""); wcscpy_s(per_utf, L"");
					f.Seek(i * 0x19, CFile::begin);
					f.Read(per_gbk, 16);
					int wlen = MultiByteToWideChar(936, 0, per_gbk, -1, NULL, 0);
					if (wlen >= 16) {
						wlen = 16;
						per_gbk[16] = 0;
					}
					MultiByteToWideChar(936, 0, per_gbk, -1, per_utf, wlen);
					per_utf[16] = 0;
					wcscat_s(item[i + 256], per_utf);
				}
				f.Close();
			}
		}
	}
}

void CcczEditor2View::ReadDataFromLocal()
{
	wchar_t path[512];
	GetModuleFileName(NULL, path, MAX_PATH);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\') break;
	}
	wchar_t real_path[512];
	wcsncpy_s(real_path, path, pos + 1);
	wchar_t ini_path[512];
	wcscpy_s(ini_path, real_path);
	wcscat_s(ini_path, L"CczSceneEditor2.ini");
	wchar_t res[5][10];
	if (GetPrivateProfileString(L"Config", L"ItemWeaponSum", L"", res[0], 4, ini_path)) {
		if (set_weapon < 0)set_weapon = CString2Int(CString(res[0]));
	}
	if (GetPrivateProfileString(L"Config", L"ItemDefenseSum", L"", res[1], 4, ini_path)) {
		if (set_armor < 0)set_armor = CString2Int(CString(res[1]));
	}
	if (GetPrivateProfileString(L"Config", L"ItemAssistSum", L"", res[2], 4, ini_path)) {
		if (set_product < 0)set_product = CString2Int(CString(res[2]));
	}
	if (GetPrivateProfileString(L"Config", L"CharLvMax", L"", res[3], 4, ini_path)) {
		if (set_level < 0)set_level = CString2Int(CString(res[3]));
	}
	if (GetPrivateProfileString(L"Config", L"RSMax", L"", res[4], 4, ini_path)) {
		if (set_rs < 0)set_rs = CString2Int(CString(res[4]));
	}
	wchar_t new_path[200];
	GetDocument()->modName = new wchar_t[200];
	if (GetPrivateProfileString(L"Config", L"ExePath", L"", new_path, 200, ini_path)) {
		wcscpy_s((wchar_t*)GetDocument()->modName, 200, new_path);
		if (!read_e5 || !read_exe)ReadDataFromFile();
	}
	else
		wcscpy_s((wchar_t*)GetDocument()->modName, 200, (wchar_t*)GetDocument()->pathName);
}

void CcczEditor2View::ReadDataDefault()
{
	/*如果读不到文件，就只能用默认配置了*/
	if (set_level < 0)set_level = 50;
	if (set_weapon < 0)set_weapon = 70;
	if (set_armor < 0)set_armor = 39;
	if (set_product < 0)set_product = 41;
	if (set_rs < 0)set_rs = 100;
	NormalizeItemSegmentCounts();
}

void CcczEditor2View::NormalizeItemSegmentCounts()
{
	// item[] is a fixed 512-entry table.  Keep the configured segments
	// non-negative and preserve their declared order while ensuring their
	// cumulative ranges cannot run past that table.
	constexpr int kItemTableCapacity = 512;
	set_weapon = max(0, min(set_weapon, kItemTableCapacity));
	set_armor = max(0, min(set_armor, kItemTableCapacity - set_weapon));
	set_product = max(0, min(set_product,
		kItemTableCapacity - set_weapon - set_armor));
}

void CcczEditor2View::InitBaseData()
{
	/*基础信息*/
	wchar_t show[20];
	for (int i = 0; i < 1024; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		wcscpy_s(per1[i], show);
		wcscpy_s(per2[i], show);
	}
	wcscpy_s(per2[1024], L"任何部队");
	wcscpy_s(per2[1025], L"我军或友军");
	wcscpy_s(per2[1026], L"敌军");
	wcscpy_s(per2[1027], L"我军当前人物");

	for (int i = 0; i < 250; i++)
	{
		wcscpy_s(show, L"w");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L" ");
		wcscpy_s(per2[i + 1028], show);
	}

	for (int i = 0; i < 4096; i++)
	{
		wcscpy_s(show, L"v");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L" ");
		wcscpy_s(per1[i + 1024], show);
		wcscpy_s(per2[i + 1028 + 250], show);
	}

	wcscpy_s(per1[5120], L"无");
	wcscpy_s(per2[5374], L"无");

	for (int i = 0; i < 80; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		wcscpy_s(job[i], show);
	}

	for (int i = 0; i < 256; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		wcscpy_s(teji[i], show);
	}

	for (int i = 0; i < 255; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		wcscpy_s(item[i], show);
	}
	wcscpy_s(item[255], L"无");

	for (int i = 0; i < 255; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i + 256).c_str());
		wcscat_s(show, L":");
		wcscpy_s(item[i + 256], show);
	}
	wcscpy_s(item[511], L"无");

	for (int i = 0; i < 256; i++) {
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		wcscpy_s(meff[i], show);
	}

	wcscpy_s(movie[0], L"LOGO.AVI"); wcscpy_s(movie[1], L"OPEN.AVI"); wcscpy_s(movie[2], L"END.AVI"); wcscpy_s(movie[3], L"PRESS.AVI");
	for (int i = 0; i < 124; i++)
	{
		wcscpy_s(movie[i + 4], L"movie");
		wcscat_s(movie[i + 4], std::to_wstring(i + 1).c_str());
	}

	wcscpy_s(object[0], L"火");
	wcscpy_s(object[1], L"船");
	wcscpy_s(object[2], L"起火船");
	wcscpy_s(object[3], L"未知");
	for (int i = 4; i < 128; i++) {
		wcscpy_s(show, L"Gate-");
		wcscat_s(show, std::to_wstring(i * 2 - 7).c_str());
		wcscpy_s(object[i], show);
	}

	ReadString();
	ReadDataFromIni();
	ReadDataFromFile();
	ReadDataFromLocal();
	ReadDataDefault();
}

void CcczEditor2View::CreateRoot(LPWSTR root_name)
{
	CMainFrame* frame = static_cast<CMainFrame*>(AfxGetMainWnd());
	if (frame != nullptr)
		frame->m_wndScenePreview.PrepareForTreeRebuild(*GetDocument());
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	//m_TreeCtrl.ModifyStyle(0, TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_CHECKBOXES);
	m_TreeCtrl.ModifyStyle(TVS_CHECKBOXES, TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS );
	m_TreeCtrl.ModifyStyleEx(0, WS_EX_ACCEPTFILES);
	m_TreeCtrl.SetItemHeight(theApp.custom_height);

	if (theApp.custom_use_font)
		m_TreeCtrl.SetFont(&theApp.custom_fonts);

	HTREEITEM hRoot;
	TV_INSERTSTRUCT TCItem;
	TCItem.hParent = TVI_ROOT;
	TCItem.hInsertAfter = TVI_LAST;
	TCItem.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	TCItem.item.pszText = root_name;
	TCItem.item.lParam = 0;//序号
	TCItem.item.iImage = 0;//正常图标
	TCItem.item.iSelectedImage = 1;//选中时图标
	hRoot = m_TreeCtrl.InsertItem(&TCItem);//返回根项句柄
}

void CcczEditor2View::CreateNewTree()
{
	CreateRoot(L"Scenario");

	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	HTREEITEM hRoot = m_TreeCtrl.GetRootItem();

	CreateScene(hRoot, TVI_LAST);
	
	m_TreeCtrl.Expand(hRoot, TVE_EXPAND);
	m_TreeCtrl.Expand(m_TreeCtrl.GetChildItem(hRoot), TVE_EXPAND);
	m_TreeCtrl.Expand(m_TreeCtrl.GetChildItem(m_TreeCtrl.GetChildItem(hRoot)), TVE_EXPAND);
	GetDocument()->MarkEditorTreeRebuilt();
}

void CcczEditor2View::CreateFileTree()
{
	CreateRoot((LPWSTR)this->GetDocument()->pathName);
	LPCTSTR path = this->GetDocument()->pathName;

	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	HTREEITEM hRoot = m_TreeCtrl.GetRootItem();

	TV_INSERTSTRUCT TCItem;
	TCItem.hParent = TVI_ROOT;
	TCItem.hInsertAfter = TVI_LAST;
	TCItem.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	TCItem.item.pszText = (LPWSTR)path;
	TCItem.item.lParam = 0;//序号
	TCItem.item.iImage = 0;//正常图标
	TCItem.item.iSelectedImage = 1;//选中时图标
	CFile f;
	unsigned char c[100];
	f.Open(path, CFile::modeRead);

	f.Seek(10, CFile::begin);
	f.Read(c, 4); 
	int n = ch2int(c, 4);
	/*找到每一个scene的起始位置*/
	int scene_pos[100]; int scene_sum = 0;
	for (int i = 10; i < n; i += 4)
	{
		f.Seek(i, CFile::begin);
		f.Read(c, 4); scene_pos[scene_sum++] = ch2int(c, 4, false);
	}
	unsigned char sec[65536];
	/*逐个编写每一个scene的内容*/
	for (int i = 0; i < scene_sum; i++)
	{
		TCItem.hParent = hRoot; TCItem.item.pszText = L"Scene";
		HTREEITEM hScene = m_TreeCtrl.InsertItem(&TCItem); 
		m_TreeCtrl.SetItemData(hScene, (DWORD_PTR)InitData(-1));
		/*检查一共有多少个section*/
		f.Seek(scene_pos[i], CFile::begin);
		f.Read(c, 2); int section_sum = ch2int(c, 2);
		/*逐个编写每一个section的内容*/
		for (int j = 0; j < section_sum; j++)
		{
			TCItem.hParent = hScene; TCItem.item.pszText = L"Section";
			HTREEITEM hSection = m_TreeCtrl.InsertItem(&TCItem);
			m_TreeCtrl.SetItemData(hSection, (DWORD_PTR)InitData(-2));
			/*获取section的长度*/
			f.Read(c, 2); int section_length = ch2int(c, 2, false);
			f.Read(sec, section_length);
			/*标记一下当前是不是在section的头部*/
			bool head = true;
			/*标记一下是否出现了子事件设定的指令*/
			bool zsj_flag = false;
			/*标记一下当前的子事件嵌套数量*/
			int zsj_sum = 0;
			/*开始添加指令*/
			int k = 0; 
			TCItem.hParent = hSection;
			while (k < section_length)
			{
				int id = sec[k];
				TCItem.item.pszText = code[id];
				HTREEITEM hItem = m_TreeCtrl.InsertItem(&TCItem);
				if (id == 0x76)jmp_list.push_back(hItem);

				ItemData* data = InitData(id);
				m_TreeCtrl.SetItemData(hItem, (DWORD_PTR)data);

				code_off[cur_code_ord - 1] = f.GetPosition() - section_length + k;
				
				int off = EditData(data, sec + k);
				k += off;
				UpdateShow(hItem);
				/*头部遇到了事件结束标志，表示头部已经结束了，下面开始正文*/
				if (id == 0 && head == true) {
					head = false;
					TCItem.hParent = hItem;
					k += 2;
				}
				/*遇到了子事件设定，做一个标记*/
				if (id == 1)zsj_flag = true;
				/*遇到了标记，开始子事件嵌套*/
				else if (zsj_flag == true)
				{
					if (code_test[id] != 0) {
						zsj_sum++;
						TCItem.hParent = hItem;
						k += 2;
					}
					zsj_flag = false;
				}
				/*子事件结束*/
				if (id == 0 && zsj_sum > 0)
				{
					zsj_sum--;
					TCItem.hParent = m_TreeCtrl.GetParentItem(TCItem.hParent);
				}
				m_TreeCtrl.Expand(hSection, TVE_EXPAND);
			}
			m_TreeCtrl.Expand(hScene, TVE_EXPAND);
		}
	}

	m_TreeCtrl.Expand(hRoot, TVE_EXPAND);
	m_TreeCtrl.Expand(m_TreeCtrl.GetChildItem(hRoot), TVE_EXPAND);
	m_TreeCtrl.Expand(m_TreeCtrl.GetChildItem(m_TreeCtrl.GetChildItem(hRoot)), TVE_EXPAND);

	f.Close();

	for (int i = 0; i < jmp_list.size(); i++)
	{
		ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(jmp_list[i]);
		int dif = data->int_data[0];
		int pos = code_off[data->ord];
		for (int j = 0; j < 200000; j++)
		{
			if (code_off[j] == pos + dif + 8)
			{
				data->int_data[0] = j;
				UpdateShow(jmp_list[i]);
				break;
			}
		}
	}
	jmp_list.clear();
	GetDocument()->MarkEditorTreeRebuilt();
}

int CcczEditor2View::EditData(ItemData* me, unsigned char* c)
{
	unsigned char* begin = c;
	c += 2;
	int long_char_sum = 0;
	int id = me->id;
	bool var = false;    //变量测试专用
	int var_sum = 13;
	if (id == 0x46)var_sum = 11 * 20;
	if (id == 0x47)var_sum = 12 * 80;
	for (int i = 0; i < var_sum; i++)
	{
		int ins = code_instruct[id][i];
		if (id == 0x46) ins = code_instruct[id][i % 11];
		if (id == 0x47) ins = code_instruct[id][i % 12];
		if (ins == -1)break;
		c += 2;
		if (ins == 0x5)
		{
			char tmp[10000];
			bool terminated = false;
			for (int j = 0; j < 10000; j++)
			{
				tmp[j] = *c;
				c++;
				if (tmp[j] == 0) {
					terminated = true;
					break;
				}
			}
			if (!terminated) tmp[9999] = 0;
			int wlen = MultiByteToWideChar(936, 0, tmp, -1, NULL, 0);
			if (wlen > 0) {
				std::vector<wchar_t> decoded(static_cast<std::size_t>(wlen));
				if (MultiByteToWideChar(936, 0, tmp, -1, decoded.data(), wlen) == wlen) {
					const std::size_t storageCapacity = (std::max)(
						ccz::editor::kLongCharDataCapacity,
						static_cast<std::size_t>(wlen));
					ccz::editor::ReplaceLongCharData(
						me->long_char_data,
						me->long_char_data_capacity,
						decoded.data(),
						storageCapacity);
				}
			}
			long_char_sum++;
		}
		else if (ins == 0x35)
		{
			int var_sum = ch2int(c, 2);
			c += 2;
			for (int j = 0; j < var_sum; j++)
			{
				me->int_data[var == true ? (25 + j) : (0 + j)] = ch2int(c, 2);
				c += 2;
			}
			for (int j = (var ? 25 : 0) + var_sum; j < (var ? 25 : 0) + 25; j++) {
				me->int_data[j] = -1;
			}
			var = true;
		}
		else if (ins == 0x4)
		{
			me->int_data[i - long_char_sum] = ch2int(c, 4, 4);
			c += 4;
		}
		else
		{
			me->int_data[i - long_char_sum] = ch2int(c, 2, 2);
			c += 2;
		}
	}
	return c - begin;
}

HTREEITEM CcczEditor2View::FindLast(HTREEITEM me)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	HTREEITEM parent = m_TreeCtrl.GetParentItem(me);
	HTREEITEM after;

	HTREEITEM brother = m_TreeCtrl.GetChildItem(parent);
	if (brother == me) after = TVI_FIRST;
	else {
		while (brother != NULL)
		{
			HTREEITEM tmp = m_TreeCtrl.GetNextItem(brother, TVGN_NEXT);
			if (tmp == me)break;
			brother = tmp;
		}
		after = brother;
	}
	return after;
}

ItemData* CcczEditor2View::InitData(int id)
{
	int length = 20;
	if (id == 70 || id == 71)length = 1000;
	else if (id == 5) length = 60;
	ItemData* item = new ItemData(length); 
	item->id = id;
	item->ord = cur_code_ord++;
	if (id < 0)return item;
	for (int i = 0; i < length; i++)item->int_data[i] = 0;

	for (int i = 0; i < 13; i++)
	{
		if (code_instruct[id][i] == 5)
		{
			ccz::editor::ReplaceLongCharData(
				item->long_char_data,
				item->long_char_data_capacity,
				L"",
				ccz::editor::kLongCharDataCapacity);
		}
	}

	if (id == 5) for (int i = 0; i < 50; i++)item->int_data[i] = -1;
	if (id == 6) for (int i = 2; i < 20; i++)item->int_data[i] = -1;
	if (id == 18 || id == 27)item->int_data[0] = -1;
	if (id == 21)for (int i = 0; i < 2; i++)item->int_data[i] = -1;
	if (id == 48) item->int_data[3] = -1;
	if (id == 50) item->int_data[5] = -1;
	if (id == 51) item->int_data[2] = -1;
	if (id == 70 || id == 71) {
		int off = id - 70;
		for (int i = 0; i < 20 + off * 60; i++) {
			item->int_data[i * (11 + off)] = -1;
			item->int_data[i * (11 + off) + (4 + off)] = -1;
			item->int_data[i * (11 + off) + (8 + off)] = -1;
		}
	}
	if (id == 75)item->int_data[3] = -1;
	if (id == 77)for (int i = 8; i <= 10; i++)item->int_data[i] = -1;
	if (id == 79)item->int_data[2] = -1;
	if (id == 109)item->int_data[6] = -1;
	return item;
}

void CcczEditor2View::UpdateShow(HTREEITEM me)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(me);
	int id = data->id;
	wchar_t show[10000];
	wcscpy_s(show, code[id]);
	wcscat_s(show, L" ");
	if (id == -1)wcscpy_s(show, L"Scene");
	if (id == -2)wcscpy_s(show, L"Section");
	if (id == 2 || id == 20 || (id >= 22 && id <= 26) || id == 105 || id == 122) {
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
		for (int i = 0; i <= lens; i++)
		{
			if (data->long_char_data[i] == L'\n' || i == lens)
			{
				wchar_t tmp[10000];
				wcscpy_s(tmp, L"");
				wcsncpy_s(tmp, data->long_char_data + last, i - last);
				wcscat_s(real, tmp);
				if (i != lens) wcscat_s(real, L"\\n");
				last = i + 1;
			}
		}
		wcscat_s(show, real);
	}
	if (id == 4) wcscat_s(show, data->int_data[0] == 1 ? L"选是" : L"选否");
	if (id == 5)
	{
		for (int i = 0; i < 25; i++) {
			if (data->int_data[i] == -1)
			{
				if (i == 0) wcscat_s(show, L"无");
				break;
			}
			wcscat_s(show, L"Var");
			wcscat_s(show, std::to_wstring(data->int_data[i]).c_str());
			wcscat_s(show, L" ");
		}
		wcscat_s(show, L";");
		for (int i = 25; i < 50; i++) {
			if (data->int_data[i] == -1)
			{
				if (i == 25) wcscat_s(show, L"无");
				break;
			}
			wcscat_s(show, L"Var");
			wcscat_s(show, std::to_wstring(data->int_data[i]).c_str());
			wcscat_s(show, L" ");
		}
	}
	if (id == 6 || id == 74)
	{
		if(id == 6) wcscat_s(show, data->int_data[0] == 0 ? L"false " : L"true ");
		int off = id == 6 ? 0 : 1;
		wcscat_s(show, std::to_wstring(data->int_data[1 - off]).c_str());
		wcscat_s(show, L" ");
		for (int i = 0; i < 10; i++)
		{
			int p = data->int_data[i + 2 - off];
			wchar_t* name = per1[Per1Code2List(p)];
			wcscat_s(show, name);
			wcscat_s(show, L" ");
		}
	}
	if (id == 8) wcscat_s(show, data->int_data[0] == 1 ? L"true" : L"false");
	if (id == 9 || id == 19 || id == 40 || id == 110 || id == 113 || id == 118) wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	if (id == 11)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] == 0 ? L"false " : L"true ");
	}
	if (id == 15)
	{
		wcscat_s(show, L"结局");
		wcscat_s(show, std::to_wstring(data->int_data[0] + 1).c_str());
	}
	if (id == 17)
	{
		int jb = data->int_data[0];
		jb += 1;
		wcscat_s(show, jb % 2 == 0 ? L"R_" : L"S_");
		if (jb < 10) wcscat_s(show, L"0");
		wcscat_s(show, std::to_wstring(jb / 2).c_str());
		wcscat_s(show, L".eex");
	}
	if (id == 18)
	{
		wchar_t real[3000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 3000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[3000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
		wcscat_s(show, L" ");
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
	}
	if (id == 21)
	{
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[10000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
		wcscat_s(show, L" ");
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" "); 
		wchar_t* name2 = per2[Per2Code2List(data->int_data[1])];
		wcscat_s(show, name2);
		wcscat_s(show, L" ");
	}
	if (id == 27)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] == 0 ? L"false" : L"true");
	}
	if (id == 31)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	}
	if (id == 32) {
		if (data->int_data[0] <= 4)
			wcscat_s(show, face_condition[data->int_data[0]]);
		if (data->int_data[0] == 16)
			wcscat_s(show, face_condition[5]);
		if (data->int_data[0] == 32)
			wcscat_s(show, face_condition[6]);
		if (data->int_data[0] == 128)
			wcscat_s(show, face_condition[7]);
		if (data->int_data[0] == 255)
			wcscat_s(show, face_condition[8]);
	}
	if (id == 33)
	{
		wcscat_s(show, L"(");
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[2] == 0 ? L"放火 " : L"恢复 ");
		wcscat_s(show, data->int_data[3] == 0 ? L"视点不切换 " : L"视点切换 ");
		wcscat_s(show, data->int_data[4] == 0 ? L"无音效 " : L"播放音效 ");
	}
	if (id == 34) wcscat_s(show, movie[data->int_data[0]]);
	if (id == 35)
	{
		if (data->int_data[0] < 100) {
			wcscat_s(show, L"SE");
			if (data->int_data[0] < 10) wcscat_s(show, L"0");
			wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
			wcscat_s(show, L" ");
		}
		else if (data->int_data[0] < 200) {
			wcscat_s(show, L"SE_M_");
			if (data->int_data[0] - 100 < 10) wcscat_s(show, L"0");
			wcscat_s(show, std::to_wstring(data->int_data[0] - 100).c_str());
			wcscat_s(show, L" ");
		}
		else {
			wcscat_s(show, L"SE_E_");
			if (data->int_data[0] - 200 < 10) wcscat_s(show, L"0");
			wcscat_s(show, std::to_wstring(data->int_data[0] - 200).c_str());
			wcscat_s(show, L" ");
		}
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	}
	if (id == 36)
	{
		wcscat_s(show, L"Track");
		wcscat_s(show, data->int_data[0] == 255 ? L" 无" : std::to_wstring(data->int_data[0] + 2).c_str());
		wcscat_s(show, L" ");
	}
	if (id == 37 || id == 41 || id == 42)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L")");
	}
	if (id == 38)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L") - (");
		wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		wcscat_s(show, L")");
	}
	if (id == 39)
	{
		if (data->int_data[0] == 0) wcscat_s(show, L"外场景 ");
		if (data->int_data[0] == 1) wcscat_s(show, L"中国地图 ");
		if (data->int_data[0] == 2) wcscat_s(show, L"内场景 ");
		if (data->int_data[0] == 3) wcscat_s(show, L"战场地图 ");
		wcscat_s(show, L" ");
		if (data->int_data[0] == 0)
			wcscat_s(show, std::to_wstring(data->int_data[data->int_data[0] + 1] + 1).c_str());
		else if(data->int_data[0] == 2)
			wcscat_s(show, std::to_wstring(data->int_data[data->int_data[0] + 1] + 41).c_str());
		else
			wcscat_s(show, std::to_wstring(data->int_data[data->int_data[0] + 1]).c_str());
	}
	if (id == 43 || id == 45 || id == 92)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
	}
	if (id == 44)
	{
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[10000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
		wcscat_s(show, L" ");
		if (data->int_data[0] == 1) wcscat_s(show, L"不");
		wcscat_s(show, L"换页 ");
		if (data->int_data[0] == 0) wcscat_s(show, L"不");
		wcscat_s(show, L"换行 ");
		if (data->int_data[0] == 0) wcscat_s(show, L"不");
		wcscat_s(show, L"等待 ");
	}
	if (id == 46 || id == 94 || id == 108)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		name = per2[Per2Code2List(data->int_data[1])];
		wcscat_s(show, name);
		if (id == 46) {
			wcscat_s(show, L" 相邻");
			if (data->int_data[2] == 0) wcscat_s(show, L"(可攻击)");
		}
	}
	if (id == 48)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[3] >= 0 ? dir[data->int_data[3]] : dir[4]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[4] >= 0 ? ges[data->int_data[4]] : ges[20]);
	}
	if (id == 49)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"单人 " : L"区域 ");
		if (data->int_data[0] == 0) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else {
			wcscat_s(show, L"(");
			wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
			wcscat_s(show, L") - (");
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
			wcscat_s(show, L") ");
			wcscat_s(show, zhenying[data->int_data[6]]);
		}
	}
	if (id == 50 || id == 85)
	{
		wcscat_s(show, data->int_data[0] != 1 ? L"data角色 " : L"战场编号 ");
		if (data->int_data[0] != 1) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[5] >= 0 ? dir[data->int_data[5]] : dir[4]);
	}
	if (id == 51)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] >= 0 ? ges[data->int_data[1]] : ges[20]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[2] >= 0 ? dir[data->int_data[2]] : dir[4]);
	}
	if (id == 52)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] >=0 ? ges[data->int_data[1]] : ges[20]);
	}
	if (id == 53 || id == 57 || id == 117)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	}
	if (id == 54)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, per_condition[data->int_data[1]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, compare[data->int_data[3]]);
	}
	if (id == 55)
	{
		if (data->int_data[0] == 0) wcscat_s(show, L"钱");
		else if (data->int_data[0] == 1) wcscat_s(show, L"剧本编号");
		else wcscat_s(show, L"红蓝条");
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, compare[data->int_data[2]]);
	}
	if (id == 56)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, per_condition[data->int_data[1]]);
		wcscat_s(show, L" ");
		wcscat_s(show, operate[data->int_data[2]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	}
	if (id == 58)
	{
		if (data->int_data[0] == 0) wcscat_s(show, L"钱");
		else if (data->int_data[0] == 1) wcscat_s(show, L"剧本编号");
		else wcscat_s(show, L"红蓝条");
		wcscat_s(show, L" ");
		wcscat_s(show, operate[data->int_data[1]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	}
	if (id == 59)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, join_condition[data->int_data[1] == 255 ? 2 : data->int_data[1]]);
		wcscat_s(show, L" ");
		int p = data->int_data[2];
		if (p <= set_level) { wcscat_s(show, L"+"); wcscat_s(show, std::to_wstring(data->int_data[2]).c_str()); }
		else if(p <= set_level * 2) wcscat_s(show, std::to_wstring(set_level - data->int_data[2]).c_str());
		else wcscat_s(show, L"默认等");
		wcscat_s(show, L"级");
	}
	if (id == 60)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, join_condition[data->int_data[1] == 255 ? 2 : data->int_data[1]]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[2] == 0 ? L"!=" : L"=");
	}
	if (id == 61)
	{
		int p = data->int_data[0] >=0 ? data->int_data[0] : 255;
		wcscat_s(show, item[p]);
		p = data->int_data[1];
		if (p <= 0) wcscat_s(show, L" 默认等级 ");
		else {
			wcscat_s(show, L" Lv");
			wcscat_s(show, std::to_wstring(p).c_str());
			wcscat_s(show, L" ");
		}
		wcscat_s(show, data->int_data[2] == 0 ? L"不显示动作 " : L"显示动作 ");
		wchar_t* name = per2[Per2Code2List(data->int_data[3])];
		wcscat_s(show, name);
	}
	if (id == 62 || id == 72)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");

		int p = data->int_data[1];
		if (p <= 0) wcscat_s(show, L"默认装备");
		else if (p == 1) wcscat_s(show, L"卸去装备");
		else wcscat_s(show, item[p - 2]);
		wcscat_s(show, L" ");

		p = data->int_data[2];
		if (p <= 0) wcscat_s(show, L"默认等级 ");
		else {
			wcscat_s(show, L" Lv");
			wcscat_s(show, std::to_wstring(p).c_str());
			wcscat_s(show, L" ");
		}

		p = data->int_data[3];
		if (p <= 0) wcscat_s(show, L"默认装备");
		else if (p == 1) wcscat_s(show, L"卸去装备");
		else wcscat_s(show, item[set_weapon + p - 2]);
		wcscat_s(show, L" ");

		p = data->int_data[4];
		if (p <= 0) wcscat_s(show, L"默认等级 ");
		else {
			wcscat_s(show, L" Lv");
			wcscat_s(show, std::to_wstring(p).c_str());
			wcscat_s(show, L" ");
		}

		p = data->int_data[5];
		if (p <= 0) wcscat_s(show, L"默认装备 ");
		else if (p == 1) wcscat_s(show, L"卸去装备 ");
		else wcscat_s(show, item[set_weapon + set_armor + p - 2]);
	}
	if (id == 63)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, compare[data->int_data[1]]);
	}
	if (id == 64)
	{
		if (data->int_data[0] == 0) wcscat_s(show, L"我军阶段");
		else if (data->int_data[0] == 1) wcscat_s(show, L"友军阶段");
		else wcscat_s(show, L"敌军阶段");
	}
	if (id == 65)
	{
		wcscat_s(show, zhenying[data->int_data[0]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, compare[data->int_data[2]]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[3] == 0 ? L"整个战场" : L"指定区域 ");
		if (data->int_data[3] == 1) {
			wcscat_s(show, L"(");
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
			wcscat_s(show, L") - (");
			wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[7]).c_str());
			wcscat_s(show, L") ");
		}
	}
	if (id == 69)
	{
		for (int i = 0; i < 2; i++)
		{
			wcscat_s(show, L"未知");
			wcscat_s(show, std::to_wstring(data->int_data[i]).c_str());
			wcscat_s(show, L" ");
		}
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L"回合 ");
		int p = data->int_data[3];
		if (p <= set_level) { wcscat_s(show, L"+"); wcscat_s(show, std::to_wstring(data->int_data[3]).c_str()); }
		else if (p <= set_level * 2) wcscat_s(show, std::to_wstring(set_level - data->int_data[3]).c_str());
		else wcscat_s(show, L"默认等");
		wcscat_s(show, L"级 ");
		wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		wcscat_s(show, L" ");
		wchar_t* name = per2[Per2Code2List(data->int_data[5])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
		wcscat_s(show, L" ");
		name = per2[Per2Code2List(data->int_data[7])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, weather[data->int_data[8]]);
		wcscat_s(show, L" ");
		wcscat_s(show, weather2[data->int_data[9]]);
	}
	if (id == 75)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[3] >= 0 ? dir[data->int_data[3]] : dir[4]);
		wcscat_s(show, data->int_data[4] == 0 ? L" 不隐藏" : L" 隐藏");
	}
	if (id == 76)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"data角色 " : L"战场编号 ");
		if (data->int_data[0] == 0) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	}
	if (id == 77)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"data角色" : data->int_data[0] == 0 ? L"战场编号" : L"区域");
		if (data->int_data[0] == 0) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else if (data->int_data[0] == 1) wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		else
		{
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
			wcscat_s(show, L") -");
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
			wcscat_s(show, L") ");
			wcscat_s(show, zhenying[data->int_data[7]]);
		}
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[8] >= 0 ? per_condition_war[data->int_data[8]] : per_condition_war[6]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[9] >= 0 ? changes[data->int_data[9]] : changes[3]);
		wcscat_s(show, L" ");

		int p = data->int_data[10];
		if (p <= 0) wcscat_s(show, L"无状态变更");
		else {
			wcscat_s(show, p < 128 ? L"赋予" : L"取消");
			if (p & 2) { wcscat_s(show, debuff[0]); wcscat_s(show, L"+"); }
			if (p & 4) { wcscat_s(show, debuff[1]); wcscat_s(show, L"+"); }
			if (p & 8) { wcscat_s(show, debuff[2]); wcscat_s(show, L"+"); }
			if (p & 16) { wcscat_s(show, debuff[3]); wcscat_s(show, L"+"); }
			if (p & 32) { wcscat_s(show, debuff[4]); wcscat_s(show, L"+"); }
			if (p & 64) {wcscat_s(show, debuff[5]); wcscat_s(show, L"+");}
			show[wcslen(show) - 1] = L' ';
		}

		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[11]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[12]).c_str());
		wcscat_s(show, L" ");
	}
	if (id == 78)
	{
		wcscat_s(show, data->int_data[0] != 1 ? L"单人 " : L"区域 ");
		if (data->int_data[0] != 1) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
			wcscat_s(show, L" ");
		}
		else
		{
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
			wcscat_s(show, L") -");
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
			wcscat_s(show, L") ");
			wcscat_s(show, zhenying[data->int_data[6]]);
			wcscat_s(show, L" ");
		}
		wcscat_s(show, policy[data->int_data[7]]);
		int p = data->int_data[7];
		wcscat_s(show, L" ");
		if (p == 3 || p == 5)wcscat_s(show, per2[Per2Code2List(data->int_data[8])]);
		if (p == 4 || p == 6) {
			wcscat_s(show, L"(");
			wcscat_s(show, std::to_wstring(data->int_data[9]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[10]).c_str());
			wcscat_s(show, L")");
		}
	}
	if (id == 79)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		name = per2[Per2Code2List(data->int_data[1])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, dir[data->int_data[2] >= 0 ? data->int_data[2] : 4]);
		wcscat_s(show, data->int_data[3] <= 0 ? L" 转向 " : L" 不转向 ");
		wcscat_s(show, data->int_data[4] == 0 ? L" 无延迟 " : L" 动作前延迟 ");
		wcscat_s(show, data->int_data[5] == 0 ? L" 无延迟 " : L" 动作后延迟 ");
	}
	if (id == 80)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, ges_war[data->int_data[1] >= 0 ? data->int_data[1] : 13]);
		wcscat_s(show, data->int_data[2] == 0 ? L" 无延迟 " : L" 动作前延迟 ");
		wcscat_s(show, data->int_data[3] == 0 ? L" 无延迟 " : L" 动作后延迟 ");
	}
	if (id == 82)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, job[data->int_data[1]]);
	}
	if (id == 83)
	{
		wcscat_s(show, data->int_data[0] != 1 ? L"单人 " : L"区域 ");
		if (data->int_data[0] != 1) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else
		{
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
			wcscat_s(show, L") -");
			wcscat_s(show, L" (");
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
			wcscat_s(show, L",");
			wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
			wcscat_s(show, L") ");
			wcscat_s(show, zhenying[data->int_data[6]]);
			wcscat_s(show, L" ");
		}
		wcscat_s(show, data->int_data[7] == 0 ? L"撤退" : L"死亡");
	}
	if (id == 86) wcscat_s(show, weather[data->int_data[0]]);
	if (id == 87) wcscat_s(show, weather2[data->int_data[0]]);
	if (id == 88) {
		wcscat_s(show, object[data->int_data[0]]);
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] == 0 ? L"显示" : L"消失");
		wcscat_s(show, L" ");
		wcscat_s(show, hexz[data->int_data[2]]);
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[5] == 0 ? L"视点不切换 " : L"视点切换 ");
		wcscat_s(show, data->int_data[6] == 0 ? L"无音效 " : L"播放音效 ");
	}
	if (id == 89)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		for (int i = 0; i < 3; i++) {
			int p = data->int_data[1 + i * 2] >= 0 ? data->int_data[1 + i * 2] : 255;
			wcscat_s(show, item[p]);
			p = data->int_data[2 + i * 2];
			if (p <= 0) wcscat_s(show, L" 默认等级 ");
			else {
				wcscat_s(show, L" Lv");
				wcscat_s(show, std::to_wstring(p).c_str());
				wcscat_s(show, L" ");
			}
		}
		wcscat_s(show, data->int_data[7] == 0 ? L"平时" : L"结局");
	}
	if (id == 91)
	{
		wcscat_s(show, L"(");
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L") - (");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[4] == 0 ? L"胜利条件" : L"战斗中");
	}
	if (id == 93)
	{
		wcscat_s(show, operate[data->int_data[0]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	}
	if (id == 96)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"敌方武将 " : L"我方武将 ");
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[10000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
		wcscat_s(show, L" ");
		wcscat_s(show, solo_ges[data->int_data[1]]);
	}
	if (id == 98)wcscat_s(show, data->int_data[0] == 1 ? L"敌方武将" : L"我方武将");
	if (id == 99)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"敌方武将 " : L"我方武将 ");
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[10000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
		wcscat_s(show, data->int_data[1] == 0 ? L" 不延时 " : L" 延时 ");
	}
	if (id == 100)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"敌方武将 " : L"我方武将 ");
		wcscat_s(show, solo_ges[data->int_data[1]]);
	}
	if (id == 101)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"敌方武将 " : L"我方武将 ");
		wcscat_s(show, solo_atk1[data->int_data[1]]);
		wcscat_s(show, data->int_data[2] == 0 ? L" 普通攻击" : L" 致命一击");
	}
	if (id == 102)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"敌方武将 " : L"我方武将 ");
		wcscat_s(show, solo_atk2[data->int_data[1]]);
		wcscat_s(show, data->int_data[2] == 0 ? L" 命中" : L" 未命中");
	}
	if (id == 103 || id == 114 || id == 123)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wchar_t real[10000];
		wcscpy_s(real, L"");
		int last = 0;
		int lens = wcsnlen_s(data->long_char_data, 10000);
		if (lens != 0)
			for (int i = 0; i <= lens; i++)
			{
				if (data->long_char_data[i] == L'\n' || i == lens)
				{
					wchar_t tmp[10000];
					wcscpy_s(tmp, L"");
					wcsncpy_s(tmp, data->long_char_data + last, i - last);
					wcscat_s(real, tmp);
					if (i != lens) wcscat_s(real, L"\\n");
					last = i + 1;
				}
			}
		wcscat_s(show, real);
	}
	if (id == 104)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		name = per2[Per2Code2List(data->int_data[1])];
		wcscat_s(show, name);
		wcscat_s(show, L" Logo-");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	}
	if (id == 107)
	{
		wcscat_s(show, L"(");
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L") ");
		int p = data->int_data[2];
		if (p < 100) {
			wcscat_s(show, L"MEff-");
			wcscat_s(show, std::to_wstring(p + 1).c_str());
		}
		else {
			wcscat_s(show, L"MCall-");
			wcscat_s(show, std::to_wstring(p - 100).c_str());
			wcscat_s(show, L".e5");
		}
		wcscat_s(show, data->int_data[3] == 0 ? L" 不切换视点" : L" 切换视点");
	}
	if (id == 109)
	{
		wcscat_s(show, data->int_data[0] == 0 ? L"data角色 " : L"战场编号 ");
		if (data->int_data[0] == 0) {
			wchar_t* name = per2[Per2Code2List(data->int_data[1])];
			wcscat_s(show, name);
		}
		else wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, per2[Per2Code2List(data->int_data[3])]);
		wcscat_s(show, L" (");
		wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		wcscat_s(show, L",");
		wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
		wcscat_s(show, L") ");
		wcscat_s(show, data->int_data[6] >= 0 ? dir[data->int_data[6]] : dir[4]);
	}
	if (id == 111)
	{
		int p = data->int_data[0] < 255 ? data->int_data[0] : 255;
		wcscat_s(show, item[p]);
	}
	if (id == 112)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		name = per2[Per2Code2List(data->int_data[1])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	}
	if (id == 115)
	{
		wchar_t* name = per2[Per2Code2List(data->int_data[0])];
		wcscat_s(show, name);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, compare[data->int_data[2]]);
		for (int i = 0; i < 5; i++)
		{
			wcscat_s(show, L" ");
			if (data->int_data[i + 3] == 0)wcscat_s(show, L"无");
			else
			{
				if (i == 0)wcscat_s(show, L"武力");
				if (i == 1)wcscat_s(show, L"智力");
				if (i == 2)wcscat_s(show, L"统率");
				if (i == 3)wcscat_s(show, L"敏捷");
				if (i == 4)wcscat_s(show, L"运气");
			}
		}
	}
	if (id == 116) wcscat_s(show, data->int_data[0] == 0 ? L"不允许存档" : L"允许存档");
	if (id == 119)
	{
		wcscat_s(show, var_kind[data->int_data[0]]);
		wcscat_s(show, L" ");
		wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, operate2[data->int_data[2]]);
		wcscat_s(show, L" ");
		wcscat_s(show, var_kind2[data->int_data[3]]);
		wcscat_s(show, L" ");
		if (data->int_data[4] < 0x400000)
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		else
			wcscat_s(show, Int2HexStr(data->int_data[4]));
	}
	if (id == 120)
	{
		wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
		wcscat_s(show, L" ");
		wcscat_s(show, data->int_data[1] == 0 ? L"<==" : L"==>");
		wcscat_s(show, L" ");
		wcscat_s(show, per2[Per2Code2List(data->int_data[2])]);
		wcscat_s(show, L" ");
		wcscat_s(show, all_condition[data->int_data[3]]);
	}
	if(id == 121)
	{
		wcscat_s(show, var_kind2[data->int_data[0]]);
		wcscat_s(show, L" ");
		if (data->int_data[1] < 0x400000)
			wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
		else
			wcscat_s(show, Int2HexStr(data->int_data[1]));
		wcscat_s(show, L" ");
		wcscat_s(show, compare2[data->int_data[2]]);
		wcscat_s(show, L" ");
		wcscat_s(show, var_kind2[data->int_data[3]]);
		wcscat_s(show, L" ");
		if (data->int_data[4] < 0x400000)
			wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
		else
			wcscat_s(show, Int2HexStr(data->int_data[4]));
	}
	wcscat_s(show, L"\0");
	m_TreeCtrl.SetItemText(me, show);
}

HTREEITEM CcczEditor2View::CreateItem(int id, HTREEITEM parent, HTREEITEM hInsertAfter)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	TV_INSERTSTRUCT TCItem;//插入数据项数据结构
	TCItem.hParent = parent;//增加根项
	TCItem.hInsertAfter = hInsertAfter;//在最后项之后
	TCItem.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;//设屏蔽
	TCItem.item.pszText = code[id];

	TCItem.item.lParam = 0;//序号
	TCItem.item.iImage = 0;//正常图标
	TCItem.item.iSelectedImage = 1;//选中时图标
	HTREEITEM hCur = m_TreeCtrl.InsertItem(&TCItem);//返回根项句柄
	m_TreeCtrl.SetItemData(hCur, (DWORD_PTR)InitData(id));

	UpdateShow(hCur);
	return hCur;
}

HTREEITEM CcczEditor2View::InsertInstructionAt(int id, HTREEITEM hAfter, const int* intData, int dataCount)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	if (!hAfter) return nullptr;

	HTREEITEM parent = m_TreeCtrl.GetParentItem(hAfter);
	if (!parent) parent = TVI_ROOT;

	HTREEITEM hNew = CreateItem(id, parent, hAfter);
	if (!hNew) return nullptr;

	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(hNew);
	if (data && intData) {
		int count = min(dataCount, (int)data->int_data.size());
		for (int i = 0; i < count; i++)
			data->int_data[i] = intData[i];
	}

	UpdateShow(hNew);
	m_TreeCtrl.SelectItem(hNew);
	GetDocument()->SetModifiedFlag(TRUE);
	GetDocument()->MarkEditorContentChanged();
	return hNew;
}

HTREEITEM CcczEditor2View::InsertBattleInstructionAt(
	int instructionId, HTREEITEM parent, HTREEITEM hInsertAfter,
	std::vector<int> intData)
{
	// Battle records use editor instruction IDs 0x4B (20 native integers),
	// 0x46/0x47 (1000 native integers each).  Do not route these through the
	// generic 20-field insertion helper, which intentionally has different
	// selection/dirty behavior and a truncating copy contract.
	const std::size_t expectedSize = instructionId == 0x4b ? 20U : 1000U;
	if ((instructionId != 0x4b && instructionId != 0x46 &&
		instructionId != 0x47) || intData.size() != expectedSize || !parent)
		return nullptr;

	// This native tree bridge is reserved for S-battle deployment writeback.
	// Fail closed for unsaved documents and every R/non-S script.
	CcczEditor2Doc* document = GetDocument();
	if (document == nullptr || document->pathName == nullptr)
		return nullptr;
	CString fileName(document->pathName);
	const int lastSeparator = max(fileName.ReverseFind(L'\\'),
		fileName.ReverseFind(L'/'));
	fileName = fileName.Mid(lastSeparator + 1);
	fileName.MakeUpper();
	const bool isBattleScript = fileName.Left(2) == L"S_" ||
		fileName.Left(2) == L"S-";
	if (!isBattleScript)
		return nullptr;

	CTreeCtrl& tree = GetTreeCtrl();
	ItemData* parentData = reinterpret_cast<ItemData*>(tree.GetItemData(parent));
	if (parentData == nullptr)
		return nullptr;
	// Nested S deployment events may be children of a predicate or else
	// instruction. Accept those containers only when their final child is the
	// native 0x00 event terminator.
	HTREEITEM lastChild = tree.GetChildItem(parent);
	if (lastChild == nullptr) return nullptr;
	for (HTREEITEM sibling = tree.GetNextSiblingItem(lastChild);
		sibling != nullptr; sibling = tree.GetNextSiblingItem(sibling)) {
		lastChild = sibling;
	}
	ItemData* terminalData = reinterpret_cast<ItemData*>(
		tree.GetItemData(lastChild));
	if (terminalData == nullptr || terminalData->id != 0 ||
		tree.GetChildItem(lastChild) != nullptr)
		return nullptr;
	// The value batch uses zero for the native tree's TVI_FIRST sentinel.
	// Normalize that representation before sibling validation/insertion.
	if (hInsertAfter == nullptr) hInsertAfter = TVI_FIRST;
	if (hInsertAfter != TVI_FIRST && hInsertAfter != TVI_LAST &&
		tree.GetParentItem(hInsertAfter) != parent)
		return nullptr;

	std::unique_ptr<ItemData> data;
	try {
		data = std::make_unique<ItemData>(static_cast<int>(expectedSize));
		data->id = instructionId;
		data->ord = cur_code_ord;
		data->int_data = std::move(intData);
	}
	catch (...) {
		return nullptr;
	}

	TVINSERTSTRUCT insert{};
	insert.hParent = parent;
	insert.hInsertAfter = hInsertAfter;
	insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE |
		TVIF_SELECTEDIMAGE;
	insert.item.pszText = code[instructionId];
	insert.item.lParam = 0;
	insert.item.iImage = 0;
	insert.item.iSelectedImage = 1;
	HTREEITEM item = tree.InsertItem(&insert);
	if (item == nullptr)
		return nullptr;
	if (!tree.SetItemData(item, reinterpret_cast<DWORD_PTR>(data.get()))) {
		tree.DeleteItem(item);
		return nullptr;
	}

	++cur_code_ord;
	try {
		UpdateShow(item);
	}
	catch (...) {
		tree.DeleteItem(item);
		--cur_code_ord;
		return nullptr;
	}
	data.release();
	return item;
}

HTREEITEM CcczEditor2View::CreateZsj(int id, HTREEITEM parent, HTREEITEM hInsertAfter)
{
	HTREEITEM hCur = CreateItem(1, parent, hInsertAfter);
	hCur = CreateItem(id, parent, hCur);
	UpdateShow(hCur);
	hCur = CreateItem(0, hCur, TVI_LAST);
	return hCur;
}

void CcczEditor2View::CreateScene(HTREEITEM parent, HTREEITEM hInsertAfter)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	TV_INSERTSTRUCT TCItem;
	TCItem.hParent = parent;
	TCItem.hInsertAfter = hInsertAfter;
	TCItem.item.pszText = L"Scene";
	TCItem.item.lParam = 0;//子项序号
	TCItem.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	HTREEITEM hCur = m_TreeCtrl.InsertItem(&TCItem);
	m_TreeCtrl.SetItemData(hCur, (DWORD_PTR)InitData(-1));
	CreateSection(hCur, TVI_LAST);
}

void CcczEditor2View::CreateSection(HTREEITEM parent, HTREEITEM hInsertAfter)
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	TV_INSERTSTRUCT TCItem;
	TCItem.hParent = parent;
	TCItem.hInsertAfter = hInsertAfter;
	TCItem.item.pszText = L"Section";
	TCItem.item.lParam = 0;//子项序号
	TCItem.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	HTREEITEM hCur = m_TreeCtrl.InsertItem(&TCItem);
	m_TreeCtrl.SetItemData(hCur, (DWORD_PTR)InitData(-2));

	CreateItem(2, hCur, TVI_LAST);
	hCur = CreateItem(0, hCur, TVI_LAST);
	CreateItem(0, hCur, TVI_LAST);
}

afx_msg
void CcczEditor2View::OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	NMTVCUSTOMDRAW* pNMCD = (NMTVCUSTOMDRAW*)(pNMHDR);
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	if (CDDS_PREPAINT == pNMCD->nmcd.dwDrawStage)
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if ((CDDS_ITEMPREPAINT) == pNMCD->nmcd.dwDrawStage)
	{
		HTREEITEM me = (HTREEITEM)pNMCD->nmcd.dwItemSpec;
		ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(me);
		CString s = m_TreeCtrl.GetItemText(me);

		if (me == m_TreeCtrl.GetDropHilightItem() || me == m_TreeCtrl.GetSelectedItem()) return;
		if (data == NULL)return;
		if (data->id < 0) {
			if (night_mode) {
				pNMCD->clrText = RGB(255, 255, 255);
				pNMCD->clrTextBk = RGB(30, 30, 30);
			}
			return;
		}

		byte r = theApp.custom_color[data->id][0][0];
		byte g = theApp.custom_color[data->id][0][1];
		byte b = theApp.custom_color[data->id][0][2];
		if (night_mode && r == g && g == b && b == 0) {
			r = 255; g = 255; b = 255;
		}
		pNMCD->clrText = RGB(r, g, b);

		r = theApp.custom_color[data->id][1][0];
		g = theApp.custom_color[data->id][1][1];
		b = theApp.custom_color[data->id][1][2];
		if (night_mode && r == g && g == b && b == 255) {
			r = 30; g = 30; b = 30;
		}
		else if(night_mode){
			r = r / 3 * 2;
			g = g / 3 * 2;
			b = b / 3 * 2;
		}
		pNMCD->clrTextBk = RGB(r, g, b);


		HTREEITEM parent = m_TreeCtrl.GetParentItem(me);
		if ((m_TreeCtrl.GetItemText(parent)[0] != '0' && m_TreeCtrl.GetItemText(parent)[0] != 'S' && s[0] == '0')
			|| m_TreeCtrl.GetChildItem(me) && data->id != 0)
		{
			/*if (data->id != 2) {
				r = theApp.custom_color[123][0][0];
				g = theApp.custom_color[123][0][1];
				b = theApp.custom_color[123][0][2];
				pNMCD->clrText = RGB(r, g, b);
			}*/
			r = theApp.custom_color[123][1][0];
			g = theApp.custom_color[123][1][1];
			b = theApp.custom_color[123][1][2];
			if (night_mode && !(r == g && g == b && b == 255)){
				r = r / 3 * 2;
				g = g / 3 * 2;
				b = b / 3 * 2;
			}
			pNMCD->clrTextBk = RGB(r, g, b);
		}
	}
}

void CcczEditor2View::OnNMClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	fast_total = 0;
	return;
}

void CcczEditor2View::OnNMRClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	CPoint pt;//用于获取CTreeCtrl右击点在CTreeCtrl的坐标 这里主要因为CTreeCtrl的点击测试的坐标点是基于自身坐标系 （HitTest为CTreeCtrl的成员函数）
	CPoint ptSc;//右击菜单的右上角的位置是基于屏幕坐标系 
	UINT  flag;
	GetCursorPos(&pt); //获取当前点击坐标的全局坐标
	ptSc = pt;
	ScreenToClient(&pt);

	MapWindowPoints(&m_TreeCtrl, &pt, 1);//MapWindowPoint  为父类（CDialog）的成员函数,  将坐标系映射为CTreeCtrl的坐标系

	//exit(1);
	HTREEITEM hItem = m_TreeCtrl.HitTest(pt, &flag);
	if (NULL != hItem) {
		cur_item = hItem;
		m_TreeCtrl.Select(hItem, TVGN_CARET);//设置点击节点为当前选中节点
		CMenu m, * mn;
		m.LoadMenu(IDR_POPUP_EDIT);//加载菜单资源
		mn = m.GetSubMenu(0);//获取菜单子项
		mn->TrackPopupMenu(TPM_LEFTALIGN, ptSc.x, ptSc.y, this);    //显示菜单
	}
}

void CcczEditor2View::OnNMDBLClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnEditModify();

	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* item = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	if (item == NULL) return;
	int id = item->id;
	if(id != 0) *pResult = TRUE;
}

// CcczEditor2View 打印


void CcczEditor2View::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CcczEditor2View::OnPreparePrinting(CPrintInfo* pInfo)
{
	// 默认准备
	return DoPreparePrinting(pInfo);
}

void CcczEditor2View::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 添加额外的打印前进行的初始化过程
}

void CcczEditor2View::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 添加打印后进行的清理过程
}

void CcczEditor2View::DrawBackground(CDC& dc)
{
	CRect rect;
	GetClientRect(&rect);

	int imgW = m_bgImage.GetWidth();
	int imgH = m_bgImage.GetHeight();

	// 平铺绘制（全图范围）
	for (int x = 0; x < rect.Width(); x += imgW)
	{
		for (int y = 0; y < rect.Height(); y += imgH)
		{
			m_bgImage.BitBlt(
				dc.GetSafeHdc(),
				x, y,
				min(imgW, rect.Width() - x),
				min(imgH, rect.Height() - y),
				0, 0,
				SRCCOPY
			);
		}
	}
}

void CcczEditor2View::OnPaint()
{
	if (!night_mode && m_bBgLoaded)
	{
		CPaintDC dc(this);
		CRect rect;
		GetClientRect(&rect);

		// 1. 创建内存DC和位图
		CDC memDC;
		memDC.CreateCompatibleDC(&dc);
		CBitmap memBitmap;
		memBitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
		CBitmap* pOldBmp = memDC.SelectObject(&memBitmap);

		// 2. 填充背景色（测试内存DC是否有效）
		memDC.FillSolidRect(rect, RGB(255, 0, 0)); // 红色背景测试
		// 如果红色能显示，说明内存DC有效，问题在图片绘制

		// 3. 绘制背景图片（平铺）
		for (int x = 0; x < rect.Width(); x += m_bgImage.GetWidth())
		{
			for (int y = 0; y < rect.Height(); y += m_bgImage.GetHeight())
			{
				m_bgImage.BitBlt(
					memDC.GetSafeHdc(),
					x, y,
					m_bgImage.GetWidth(),
					m_bgImage.GetHeight(),
					0, 0,
					SRCCOPY
				);
			}
		}

		// 4. 绘制树控件内容
		//DefWindowProc(WM_PAINT, (WPARAM)memDC.GetSafeHdc(), 0);

		// 5. 拷贝到屏幕
		dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

		// 6. 清理
		memDC.SelectObject(pOldBmp);
	}
	else
	{
		CTreeView::OnPaint(); // 默认处理
	}
}

void CcczEditor2View::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	/*ClientToScreen(&point);
	OnContextMenu(this, point);*/
}

void CcczEditor2View::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}


// CcczEditor2View 诊断

#ifdef _DEBUG
void CcczEditor2View::AssertValid() const
{
	CTreeView::AssertValid();
}

void CcczEditor2View::Dump(CDumpContext& dc) const
{
	CTreeView::Dump(dc);
}

CcczEditor2Doc* CcczEditor2View::GetDocument() const // 非调试版本是内联的
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CcczEditor2Doc)));
	return (CcczEditor2Doc*)m_pDocument;
}
#endif //_DEBUG


// CcczEditor2View 消息处理程序


void CcczEditor2View::OnEditModify()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* item = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	if (item == NULL)return;
	cur_data = *(ItemData*)m_TreeCtrl.GetItemData(cur_item);
	int id = item->id;
	cur_code = id;

	if (id == 2 || id == 20 || (id >= 22 && id <= 26) || id == 105 || id == 122) { Dialog_2 d; d.DoModal(); }
	if (id == 4 || id == 8 || id == 98 || id == 116) { Dialog_4 d; d.DoModal(); }
	if (id == 5) { Dialog_5 d; d.DoModal(); }
	if (id == 6 || id == 74) { Dialog_6 d; d.DoModal(); }
	if (id == 9 || id == 19 | id == 40 || id == 110 || id == 113 || id == 118) { Dialog_9 d; d.DoModal(); }
	if (id == 11) { Dialog_11 d; d.DoModal(); }
	if (id == 15) { Dialog_15 d; d.DoModal(); }
	if (id == 17) { Dialog_17 d; d.DoModal(); }
	if (id == 18) { Dialog_18 d; d.DoModal(); }
	if (id == 21) { Dialog_21 d; d.DoModal(); }
	if (id == 27) { Dialog_27 d; d.DoModal(); }
	if (id == 31) { Dialog_31 d; d.DoModal(); }
	if (id == 32) { Dialog_32 d; d.DoModal(); }
	if (id == 33) { Dialog_33 d; d.DoModal(); }
	if (id == 34) { Dialog_34 d; d.DoModal(); }
	if (id == 35) { Dialog_35 d; d.DoModal(); }
	if (id == 36) { Dialog_36 d; d.DoModal(); }
	if (id == 37 || id == 41 || id == 42) { Dialog_37 d; d.DoModal(); }
	if (id == 38) { Dialog_38 d; d.DoModal(); }
	if (id == 39) {
		Dialog_39 d;
		d.DoModal();
	}
	if (id == 43 || id == 45 || id == 92) { Dialog_43 d; d.DoModal(); }
	if (id == 44) { Dialog_44 d; d.DoModal(); }
	if (id == 46 || id == 94 || id == 108) { Dialog_46 d; d.DoModal(); }
	if (id == 48) { Dialog_48 d; d.DoModal(); }
	if (id == 49) { Dialog_49 d; d.DoModal(); }
	if (id == 50 || id == 85) { Dialog_50 d; d.DoModal(); }
	if (id == 51) { Dialog_51 d; d.DoModal(); }
	if (id == 52) { Dialog_52 d; d.DoModal(); }
	if (id == 53 || id == 57 || id == 117) { Dialog_53 d; d.DoModal(); }
	if (id == 54) { Dialog_54 d; d.DoModal(); }
	if (id == 55) { Dialog_55 d; d.DoModal(); }
	if (id == 56) { Dialog_56 d; d.DoModal(); }
	if (id == 58) { Dialog_58 d; d.DoModal(); }
	if (id == 59) { Dialog_59 d; d.DoModal(); }
	if (id == 60) { Dialog_60 d; d.DoModal(); }
	if (id == 61) { Dialog_61 d; d.DoModal(); }
	if (id == 62 || id == 72) { Dialog_62 d; d.DoModal(); }
	if (id == 63) { Dialog_63 d; d.DoModal(); }
	if (id == 64) { Dialog_64 d; d.DoModal(); }
	if (id == 65) { Dialog_65 d; d.DoModal(); }
	if (id == 69) { Dialog_69 d; d.DoModal(); }
	if (id == 70 || id == 71) { Dialog_70 d; d.DoModal(); }
	if (id == 75) { Dialog_75 d; d.DoModal(); }
	if (id == 76) { Dialog_76 d; d.DoModal(); }
	if (id == 77) { Dialog_77 d; d.DoModal(); }
	if (id == 78) { Dialog_78 d; d.DoModal(); }
	if (id == 79) { Dialog_79 d; d.DoModal(); }
	if (id == 80) { Dialog_80 d; d.DoModal(); }
	if (id == 82) { Dialog_82 d; d.DoModal(); }
	if (id == 83) { Dialog_83 d; d.DoModal(); }
	if (id == 86) { Dialog_86 d; d.DoModal(); }
	if (id == 87) { Dialog_86 d; d.DoModal(); }
	if (id == 88) { Dialog_88 d; d.DoModal(); }
	if (id == 89) { Dialog_89 d; d.DoModal(); }
	if (id == 91) { Dialog_91 d; d.DoModal(); }
	if (id == 93) { Dialog_93 d; d.DoModal(); }
	if (id == 96) { Dialog_96 d; d.DoModal(); }
	if (id == 99) { Dialog_99 d; d.DoModal(); }
	if (id == 100) { Dialog_100 d; d.DoModal(); }
	if (id == 101) { Dialog_101 d; d.DoModal(); }
	if (id == 102) { Dialog_102 d; d.DoModal(); }
	if (id == 103) { Dialog_103 d; d.DoModal(); }
	if (id == 104) { Dialog_104 d; d.DoModal(); }
	if (id == 107) { Dialog_107 d; d.DoModal(); }
	if (id == 109) { Dialog_109 d; d.DoModal(); }
	if (id == 111) { Dialog_111 d; d.DoModal(); }
	if (id == 112) { Dialog_112 d; d.DoModal(); }
	if (id == 114 || id == 123) { Dialog_114 d; d.DoModal(); }
	if (id == 115) { Dialog_115 d; d.DoModal(); }
	if (id == 119) { Dialog_119 d; d.DoModal(); }
	if (id == 120) { Dialog_120 d; d.DoModal(); }
	if (id == 121) { Dialog_121 d; d.DoModal(); }
	// Every completed instruction edit goes through the same event-driven
	// preview coordinator, including a cancelled dialog.  The session keeps
	// unchanged model fingerprints from causing unnecessary work.
	RefreshScenePreview();
}


void CcczEditor2View::OnEditAdd()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	if (m_TreeCtrl.GetItemText(cur_item)[0] == L'S') {
		if (m_TreeCtrl.GetItemText(cur_item)[4] != L'a')
		{
			CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
			HTREEITEM parent = m_TreeCtrl.GetParentItem(cur_item);
			HTREEITEM after = FindLast(cur_item);
			if (m_TreeCtrl.GetItemText(cur_item)[4] == L'e') CreateScene(parent, after);
			else CreateSection(parent, after);
		}
	}
	else {
		zishijian = false;
		Dialog_SelectCode d;
		d.DoModal();
	}
}

void CcczEditor2View::OnEditAddi()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	HTREEITEM parent = m_TreeCtrl.GetParentItem(cur_item);
	wchar_t tmp = m_TreeCtrl.GetItemText(parent)[0];
	if (tmp == 'S' || parent == m_TreeCtrl.GetRootItem()) {
		MessageBox(TEXT("不允许在这里创建子事件"), TEXT("创建失败"), MB_ICONERROR);
		return;
	}
	zishijian = true;
	Dialog_SelectCode d;
	d.DoModal();
}

void CcczEditor2View::OnEditDelete()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	std::vector<HTREEITEM> item;
	if (checkbox == false) item.push_back(cur_item);
	else {
		checkbox_selected.clear();
		refreshCheckbox(m_TreeCtrl.GetRootItem(), 1);
		for (int i = 0; i < checkbox_selected.size(); i++)
			item.push_back(checkbox_selected[i]);
	}
	for (int i = 0; i < item.size(); i++) {
		HTREEITEM cur_items = item[i];
		ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur_items);
		if (cur_items == NULL)return;
		/*检查是否要删禁止的指令*/
		if (data == NULL) {
			MessageBox(TEXT("不允许手动删除该指令"), TEXT("删除失败"), MB_ICONERROR);
			return;
		}
		if (data->id == 0) {
			MessageBox(TEXT("不允许手动删除该指令"), TEXT("删除失败"), MB_ICONERROR);
			return;
		}
		/*if (data->id == 2) {
			HTREEITEM p = m_TreeCtrl.GetParentItem(cur_items);
			ItemData* pd = (ItemData*)m_TreeCtrl.GetItemData(p);
			if (pd->id < 0) {
				MessageBox(TEXT("不允许手动删除顶层的2号指令"), TEXT("删除失败"), MB_ICONERROR);
				return;
			}
		}*/
		/*检查是否是独个的scene或section*/
		if (data->id == -1) {
			/*计算scene的个数*/
			int scene_cnt = 0;
			HTREEITEM scene = m_TreeCtrl.GetChildItem(m_TreeCtrl.GetRootItem());
			while (scene != NULL) {
				scene_cnt++;
				scene = m_TreeCtrl.GetNextSiblingItem(scene);
			}
			if (scene_cnt == 1) {
				MessageBox(TEXT("只有一个scene，不能删除"), TEXT("删除失败"), MB_ICONERROR);
				return;
			}
		}
		if (data->id == -2) {
			/*计算scene的个数*/
			int section_cnt = 0;
			HTREEITEM section = m_TreeCtrl.GetChildItem(m_TreeCtrl.GetParentItem(cur_items));
			while (section != NULL) {
				section_cnt++;
				section = m_TreeCtrl.GetNextSiblingItem(section);
			}
			if (section_cnt == 1) {
				MessageBox(TEXT("只有一个section，不能删除"), TEXT("删除失败"), MB_ICONERROR);
				return;
			}
		}
		// The nested ItemData/tree handles below can be destroyed by this
		// operation; invalidate any delayed preview before that boundary.
		CMainFrame* frame = static_cast<CMainFrame*>(AfxGetMainWnd());
		if (frame != nullptr)
			frame->m_wndScenePreview.InvalidatePendingPreviewRefresh(
				*GetDocument());
		/*检查是否是删嵌套*/
		HTREEITEM child = m_TreeCtrl.GetChildItem(cur_items);
		if (child) {
			GetDocument()->recur_close(child, GetTreeCtrl(), true);
			/*检查是否前面有子事件设定，有的话一并删除*/
			HTREEITEM last = FindLast(cur_items);
			if (last != TVI_FIRST) {
				if (((ItemData*)m_TreeCtrl.GetItemData(last))->id == 1)
				{
					ItemData* nestedData = (ItemData*)m_TreeCtrl.GetItemData(last);
					if (nestedData) {
						ccz::editor::ReleaseLongCharData(
							nestedData->long_char_data,
							nestedData->long_char_data_capacity);
						delete nestedData;
					}
					m_TreeCtrl.DeleteItem(last);
				}
			}
		}

		HTREEITEM next = m_TreeCtrl.GetNextSiblingItem(cur_items);
		int id = data->id;

		if (data != NULL) {
			ccz::editor::ReleaseLongCharData(
				data->long_char_data,
				data->long_char_data_capacity);
			delete data;
		}
		m_TreeCtrl.DeleteItem(cur_items);
		GetDocument()->OnModified(TRUE);

		cur_item = m_TreeCtrl.GetSelectedItem();

		/*检查子事件后是否有嵌套，如果有的话继续做删除操作*/
		if (id == 1) {
			cur_items = next;
			OnEditDelete();
		}
	}
}


BEGIN_MESSAGE_MAP(Dialog_SelectCode, CDialogEx)
	ON_BN_CLICKED(IDOK, &Dialog_SelectCode::OnBnClickedOk)
END_MESSAGE_MAP()
BOOL Dialog_SelectCode::OnInitDialog()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CDialogEx::OnInitDialog();
	for (int i = 0; i < pView->code_sum; i++)
		combo1.AddString(pView->code[i]);

	combo1.SetItemHeight(0, 18);
	combo1.SetMinVisibleItems(30);

	combo1.SetCurSel(theApp.last_item_use);

	return TRUE;
}


void Dialog_SelectCode::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	theApp.last_item_use = combo1.GetCurSel();

	if (theApp.search)
	{
		theApp.search_goal = combo1.GetCurSel();
		CDialogEx::OnOK();
		return;
	}

	if (combo1.GetCurSel() < 2)
	{
		CDialogEx::OnOK();
		MessageBox(TEXT("不允许手动添加该指令"), TEXT("创建失败"), MB_ICONERROR);
		return;
	}

	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	HTREEITEM parent = m_TreeCtrl.GetParentItem(pView->cur_item);
	if (((ItemData*)m_TreeCtrl.GetItemData(parent))->id < 0) {
		if (((ItemData*)m_TreeCtrl.GetItemData(pView->cur_item))->id == 2) {
			CDialogEx::OnOK();
			MessageBox(TEXT("不允许在这里添加指令"), TEXT("创建失败"), MB_ICONERROR);
			return;
		}
		if (pView->code_test[combo1.GetCurSel()] < 2 && combo1.GetCurSel() != 0x77 && combo1.GetCurSel() != 0x78 && combo1.GetCurSel() != 0x72) {
			CDialogEx::OnOK();
			MessageBox(TEXT("不允许在顶层添加该指令"), TEXT("创建失败"), MB_ICONERROR);
			return;
		}
		if (pView->zishijian) {
			CDialogEx::OnOK();
			MessageBox(TEXT("不允许在顶层添加子事件"), TEXT("创建失败"), MB_ICONERROR);
			return;
		}
	}
	HTREEITEM last = pView->FindLast(pView->cur_item);
	if (last != TVI_FIRST) {
		if (((ItemData*)m_TreeCtrl.GetItemData(last))->id == 1) {
			CDialogEx::OnOK();
			MessageBox(TEXT("不允许在子事件设定后面添加指令"), TEXT("创建失败"), MB_ICONERROR);
			return;
		}
	}
	if (pView->zishijian && pView->code_test[combo1.GetCurSel()] == 0)
	{
		CDialogEx::OnOK();
		MessageBox(TEXT("该指令类型不可以添加为子事件"), TEXT("创建失败"), MB_ICONERROR);
		return;
	}


	HTREEITEM after = pView->FindLast(pView->cur_item);
	if (pView->zishijian) pView->CreateZsj(combo1.GetCurSel(), parent, after);
	else pView->CreateItem(combo1.GetCurSel(), parent, after);
	// TODO: 在此添加控件通知处理程序代码
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

namespace
{
bool IsHistoryInfoContext(CcczEditor2View* pView)
{
	if (pView == nullptr || pView->cur_item == nullptr)
		return false;

	CTreeCtrl& tree = pView->GetTreeCtrl();
	HTREEITEM previous = tree.GetPrevSiblingItem(pView->cur_item);
	if (previous == nullptr)
		return false;

	ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(previous));
	return data != nullptr && data->id == 0x77 && data->int_data.size() > 1 &&
		data->int_data[1] == 4071;
}

void SetWrapControlVisible(CWnd* dialog, int controlId, bool visible)
{
	if (dialog == nullptr)
		return;
	if (CWnd* control = dialog->GetDlgItem(controlId))
	{
		control->ShowWindow(visible ? SW_SHOW : SW_HIDE);
		control->EnableWindow(visible ? TRUE : FALSE);
	}
}

void ConfigureWrapControls(CWnd* dialog, bool visible, bool showMode)
{
	SetWrapControlVisible(dialog, IDC_WRAP_ENABLE, visible);
	SetWrapControlVisible(dialog, IDC_WRAP_BYTES_LABEL, visible);
	SetWrapControlVisible(dialog, IDC_WRAP_BYTES, visible);
	SetWrapControlVisible(dialog, IDC_WRAP_MODE, visible && showMode);
}

void SetWrapLimitText(CEdit& edit, int value)
{
	CString text;
	text.Format(L"%d", value);
	edit.SetWindowTextW(text);
}

void SetLongTextInputLimit(CEdit& edit)
{
	edit.SetLimitText(static_cast<UINT>(ccz::editor::kLongCharDataMaxLength));
}

bool StoreLongCharDataOrReport(ItemData* data, const CString& text)
{
	if (data == nullptr || data->long_char_data == nullptr ||
		data->long_char_data_capacity == 0 ||
		static_cast<std::size_t>(text.GetLength()) > ccz::editor::kLongCharDataMaxLength ||
		!ccz::editor::StoreLongCharData(
			data->long_char_data,
			data->long_char_data_capacity,
			text.GetString())) {
		AfxMessageBox(
			_T("文本过长，超过编辑器支持的 2999 个 UTF-16 字符上限，未保存。"),
			MB_ICONWARNING);
		return false;
	}
	return true;
}

int Cp936ByteCountForText(std::wstring_view text)
{
	if (text.empty())
		return 0;
	const int length = static_cast<int>((std::min)(
		text.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)())));
	const int bytes = WideCharToMultiByte(
		936, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
	return bytes > 0 ? bytes : length;
}

std::wstring_view ToWideView(const CString& text)
{
	return std::wstring_view(text.GetString(), static_cast<std::size_t>(text.GetLength()));
}

CString ToCString(const std::wstring& text)
{
	return CString(text.data(), static_cast<int>(text.size()));
}

CString ToStorageLineBreaks(const CString& text)
{
	CString result;
	result.Preallocate(text.GetLength());
	for (int i = 0; i < text.GetLength(); ++i)
		if (text[i] != L'\r')
			result += text[i];
	return result;
}

void SetWrapEditText(CEdit& edit, const ccz::text_wrap::EditTextUpdate& update)
{
	CString newText = ToCString(update.state.edit_text);
	CString current;
	edit.GetWindowTextW(current);
	if (newText != current)
	{
		edit.SetWindowTextW(newText);
		edit.SetSel(static_cast<int>(update.selection_start),
			static_cast<int>(update.selection_end));
	}
}

void InitializeWrapEdit(CEdit& edit, ccz::text_wrap::EditTextState& state,
	const CString& source, int limit, bool enabled)
{
	state = ccz::text_wrap::CreateEditTextState(
		ToWideView(source), enabled ? limit : 0, Cp936ByteCountForText,
		ccz::editor::kLongCharDataMaxLength);
	CString editText = ToCString(state.edit_text);
	CString current;
	edit.GetWindowTextW(current);
	if (editText != current)
	{
		edit.SetWindowTextW(editText);
		edit.SetSel(0, 0);
	}
}

void ApplyWrapToEdit(CEdit& edit, ccz::text_wrap::EditTextState& state, int limit)
{
	int selectionStart = 0;
	int selectionEnd = 0;
	edit.GetSel(selectionStart, selectionEnd);
	auto update = ccz::text_wrap::RewrapEditText(
		state, static_cast<std::size_t>((std::max)(0, selectionStart)),
		static_cast<std::size_t>((std::max)(0, selectionEnd)), limit,
		Cp936ByteCountForText, ccz::editor::kLongCharDataMaxLength);
	state = update.state;
	SetWrapEditText(edit, update);
}

void ApplyWrapTextChange(CEdit& edit, ccz::text_wrap::EditTextState& state, int limit)
{
	CString current;
	edit.GetWindowTextW(current);
	int selectionStart = 0;
	int selectionEnd = 0;
	edit.GetSel(selectionStart, selectionEnd);
	auto update = ccz::text_wrap::ApplyEditTextChange(
		state, ToWideView(current),
		static_cast<std::size_t>((std::max)(0, selectionStart)),
		static_cast<std::size_t>((std::max)(0, selectionEnd)), limit,
		Cp936ByteCountForText, ccz::editor::kLongCharDataMaxLength);
	state = update.state;
	SetWrapEditText(edit, update);
}

void AdoptWrapEditText(CEdit& edit, ccz::text_wrap::EditTextState& state)
{
	CString current;
	edit.GetWindowTextW(current);
	state = ccz::text_wrap::AdoptEditTextAsSource(
		ToWideView(current), ccz::editor::kLongCharDataMaxLength);
}
}

BOOL Dialog_2::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());

	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}
	edit1.SetWindowTextW(str);
	SetLongTextInputLimit(edit1);
	SetWindowTextW(pView->code[pView->cur_code]);

	wrap_supported = ccz::text_wrap::IsSupportedCommand(pView->cur_code);
	wrap_history_mode = pView->cur_code == 22 && IsHistoryInfoContext(pView);
	ConfigureWrapControls(this, wrap_supported, pView->cur_code == 22);
	if (wrap_supported)
	{
		wrap_enable.SetCheck(ccz::text_wrap::LoadEnabled(
			pView->cur_code, wrap_history_mode, true) ? BST_CHECKED : BST_UNCHECKED);
		const int limit = ccz::text_wrap::LoadLimit(pView->cur_code, wrap_history_mode);
		SetWrapLimitText(wrap_bytes, limit);
		if (pView->cur_code == 22)
			wrap_mode.SetWindowTextW(wrap_history_mode ? L"历史信息" : L"普通信息");
		wrap_updating = true;
		InitializeWrapEdit(edit1, wrap_state, str, limit,
			wrap_enable.GetCheck() == BST_CHECKED);
		wrap_updating = false;
	}
	return TRUE;
}
BEGIN_MESSAGE_MAP(Dialog_2, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_2::OnBnClickedOk)
	ON_BN_CLICKED(IDC_WRAP_MODE, &Dialog_2::OnBnClickedWrapMode)
	ON_BN_CLICKED(IDC_WRAP_ENABLE, &Dialog_2::OnBnClickedWrapEnable)
	ON_EN_CHANGE(IDC_EDIT1, &Dialog_2::OnEnChangeWrapText)
END_MESSAGE_MAP()


void Dialog_2::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	CString str;
	edit1.GetWindowTextW(str);
	if (wrap_supported)
	{
		const int fallback = ccz::text_wrap::DefaultLimit(pView->cur_code, wrap_history_mode);
		CString limitText;
		wrap_bytes.GetWindowTextW(limitText);
		const int limit = ccz::text_wrap::ParseLimit(limitText, fallback);
		SetWrapLimitText(wrap_bytes, limit);
		const bool wrapEnabled = wrap_enable.GetCheck() == BST_CHECKED;
		if (wrapEnabled)
		{
			wrap_updating = true;
			ApplyWrapTextChange(edit1, wrap_state, limit);
			wrap_updating = false;
			str = ToCString(wrap_state.storage_text);
		}
		else
		{
			AdoptWrapEditText(edit1, wrap_state);
			str = ToStorageLineBreaks(str);
		}
		if (!StoreLongCharDataOrReport(data, str)) return;
		ccz::text_wrap::SaveLimit(pView->cur_code, wrap_history_mode, limit);
		ccz::text_wrap::SaveEnabled(pView->cur_code, wrap_history_mode, wrapEnabled);
	}
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!wrap_supported && !StoreLongCharDataOrReport(data, str)) return;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

void Dialog_2::OnBnClickedWrapMode()
{
	if (!wrap_supported)
		return;

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	if (pView == nullptr || pView->cur_code != 22)
		return;

	wrap_history_mode = !wrap_history_mode;
	const int limit = ccz::text_wrap::LoadLimit(pView->cur_code, wrap_history_mode);
	SetWrapLimitText(wrap_bytes, limit);
	wrap_enable.SetCheck(ccz::text_wrap::LoadEnabled(
		pView->cur_code, wrap_history_mode, true) ? BST_CHECKED : BST_UNCHECKED);
	wrap_mode.SetWindowTextW(wrap_history_mode ? L"历史信息" : L"普通信息");
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapToEdit(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

void Dialog_2::OnBnClickedWrapEnable()
{
	if (!wrap_supported || wrap_updating)
		return;

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	if (pView == nullptr)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(pView->cur_code, wrap_history_mode));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapToEdit(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

void Dialog_2::OnEnChangeWrapText()
{
	if (!wrap_supported || wrap_updating)
		return;

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	if (pView == nullptr)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(pView->cur_code, wrap_history_mode));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapTextChange(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}
BEGIN_MESSAGE_MAP(Dialog_4, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_4::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_4::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	SetWindowTextW(pView->code[pView->cur_code]);
	if (data->id == 98)
		check1.SetWindowTextW(L"敌方武将");

	check1.SetCheck(pView->cur_data.int_data[0]);
	return TRUE;
}
void Dialog_4::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}
BEGIN_MESSAGE_MAP(Dialog_5, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_5::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_5::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	for (int i = 0; i < 25; i++) {
		if (data->int_data[i] == -1)break;
		wcscat_s(show, std::to_wstring(data->int_data[i]).c_str());
		wcscat_s(show, L",");
	}
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	for (int i = 25; i < 50; i++) {
		if (data->int_data[i] == -1)break;
		wcscat_s(show, std::to_wstring(data->int_data[i]).c_str());
		wcscat_s(show, L",");
	}
	edit2.SetWindowTextW(show);
	return 0;
}

void Dialog_5::parser(CString c, int* a)
{
	int num = 0;
	int cnt = 0;
	c.AppendChar(',');
	for (int i = 0; i < c.GetLength(); i++)
	{
		if ((c[i] < '0' || c[i]>'9') && c[i] != ',')
			break;
		if (c[i] != ',')
		{
			num *= 10;
			num += (c[i] - '0');
		}
		else
		{
			if (i > 0)if (c[i - 1] == ',')break;
			if (i == 0)break;
			cnt++;
			a[cnt] = num;
			num = 0;
		}
	}
	a[0] = cnt;
}

void Dialog_5::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int* tru = new int[25];
	int* fal = new int[25];
	CString str;
	edit1.GetWindowTextW(str);
	parser(str, tru);
	//if (tru[0] != 0)exit(1);
	for (int i = 1; i <= tru[0]; i++) data->int_data[i - 1] = tru[i];
	for (int i = tru[0]; i < 25; i++) data->int_data[i] = -1;

	edit2.GetWindowTextW(str);
	parser(str, fal);
	for (int i = 1; i <= fal[0]; i++) data->int_data[i - 1 + 25] = fal[i];
	for (int i = fal[0] + 25; i < 50; i++) data->int_data[i] = -1;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BOOL Dialog_6::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.ShowWindow(data->id == 6);

	int off = data->id == 6 ? 0 : 1;

	check1.SetCheck(data->int_data[0]);
	wchar_t show[10];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1 - off]).c_str());
	edit1.SetWindowTextW(show);
	list1.SetItemHeight(0, 18);
	for (int i = 0; i < 5; i++) list1.AddString(L"强制出场");
	for (int i = 0; i < 5; i++) list1.AddString(L"强制不出场");
	for (int i = 0; i < 10; i++)
		per[i] = data->int_data[i + 2 - off];
	//ComboAddPer(combo1);
	for (int i = 0; i < 5121; i++)combo1.AddString(pView->per1[i]);
	combo1.EnablePersonSearch();

	list1.SetCurSel(0);
	OnLbnSelchangeList1();
	return 0;
}

BEGIN_MESSAGE_MAP(Dialog_6, myDialog)
	ON_LBN_SELCHANGE(IDC_LIST1, &Dialog_6::OnLbnSelchangeList1)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_6::OnCbnSelchangeCombo1)
	ON_BN_CLICKED(IDOK, &Dialog_6::OnBnClickedOk)
END_MESSAGE_MAP()


void Dialog_6::OnLbnSelchangeList1()
{
	int list_line = list1.GetCurSel();
	combo1.SetCurSel(Per1Code2List(per[list_line]));
}


void Dialog_6::OnCbnSelchangeCombo1()
{
	int list_line = list1.GetCurSel();
	int combo_line = combo1.GetCurSel();
	per[list_line] = Per1List2Code(combo_line);
}
void Dialog_6::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	data->int_data[0] = check1.GetCheck();

	int off = data->id == 6 ? 0 : 1;

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1 - off] = num;

	for (int i = 0; i < 10; i++)data->int_data[i + 2 - off] = per[i];

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_9, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_9::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_9::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_9::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_11, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_11::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_11::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);

	combo1.AddString(L"false");
	combo1.AddString(L"true");
	combo1.SetCurSel(data->int_data[1]);
	return 0;
}

void Dialog_11::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;

	data->int_data[1] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}
BEGIN_MESSAGE_MAP(Dialog_15, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_15::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_15::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.AddString(L"结局1");
	combo1.AddString(L"结局2");
	combo1.AddString(L"结局3");
	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_15::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_17, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_17::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_17::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < pView->set_rs; i++) {
		for (int j = 0; j < 2; j++) {
			if (i == 0 && j == 0)continue;
			wchar_t show[100];
			wcscpy_s(show, j == 0 ? L"R_" : L"S_");
			if (i < 10) wcscat_s(show, L"0");
			wcscat_s(show, std::to_wstring(i).c_str());
			wcscat_s(show, L".eex");
			combo1.AddString(show);
		}
	}
	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_17::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_18, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_18::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_18::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);

	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit1.SetWindowTextW(str);
	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	return TRUE;
}

void Dialog_18::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	CString str;
	edit1.GetWindowTextW(str);
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!StoreLongCharDataOrReport(data, str))
		return;

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_21, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_21::OnBnClickedOk)
	ON_BN_CLICKED(IDC_WRAP_ENABLE, &Dialog_21::OnBnClickedWrapEnable)
	ON_EN_CHANGE(IDC_EDIT1, &Dialog_21::OnEnChangeWrapText)
END_MESSAGE_MAP()

BOOL Dialog_21::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit1.SetWindowTextW(str);
	SetLongTextInputLimit(edit1);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	ConfigureWrapControls(this, true, false);
	wrap_enable.SetCheck(ccz::text_wrap::LoadEnabled(21, false, true) ? BST_CHECKED : BST_UNCHECKED);
	const int limit = ccz::text_wrap::LoadLimit(21, false);
	SetWrapLimitText(wrap_bytes, limit);
	wrap_updating = true;
	InitializeWrapEdit(edit1, wrap_state, str, limit,
		wrap_enable.GetCheck() == BST_CHECKED);
	wrap_updating = false;
	return TRUE;
}

void Dialog_21::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	CString str;
	edit1.GetWindowTextW(str);
	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(21, false));
	SetWrapLimitText(wrap_bytes, limit);
	const bool wrapEnabled = wrap_enable.GetCheck() == BST_CHECKED;
	if (wrapEnabled)
	{
		wrap_updating = true;
		ApplyWrapTextChange(edit1, wrap_state, limit);
		wrap_updating = false;
		str = ToCString(wrap_state.storage_text);
	}
	else
	{
		AdoptWrapEditText(edit1, wrap_state);
		str = ToStorageLineBreaks(str);
	}
	if (!StoreLongCharDataOrReport(data, str)) return;
	ccz::text_wrap::SaveLimit(21, false, limit);
	ccz::text_wrap::SaveEnabled(21, false, wrapEnabled);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

void Dialog_21::OnBnClickedWrapEnable()
{
	if (wrap_updating)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(21, false));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapToEdit(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

void Dialog_21::OnEnChangeWrapText()
{
	if (wrap_updating)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(21, false));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapTextChange(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

BEGIN_MESSAGE_MAP(Dialog_27, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_27::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_27::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	combo2.AddString(L"false");
	combo2.AddString(L"true");
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	combo2.SetCurSel(pView->cur_data.int_data[1]);
	return TRUE;
}

void Dialog_27::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	data->int_data[1] = combo2.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_31, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_31::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_31::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit2.SetWindowTextW(show);
	return 0;
}

void Dialog_31::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_32, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_32::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_32::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < pView->face_condition_sum; i++) combo1.AddString(pView->face_condition[i]);
	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_32::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int k[9] = { 0,1,2,3,4,16,32,128,255 };
	data->int_data[0] = k[combo1.GetCurSel()];

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_33, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_33::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_33::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit2.SetWindowTextW(show);

	combo1.SetCurSel(data->int_data[2]);
	check1.SetCheck(data->int_data[3]);
	check2.SetCheck(data->int_data[4]);

	return 0;
}

void Dialog_33::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	data->int_data[2] = combo1.GetCurSel();
	data->int_data[3] = check1.GetCheck();
	data->int_data[4] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_34, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_34::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_34::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 128; i++) combo1.AddString(pView->movie[i]);
	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_34::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_35, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_35::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_35::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	for (int i = 0; i < pView->set_sound[0]; i++) {
		wcscpy_s(show, L"SE");
		if (i < 10) wcscat_s(show, L"0");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L".WAV");
		combo1.AddString(show);
	}
	for (int i = 0; i < pView->set_sound[1]; i++) {
		wcscpy_s(show, L"SE_M_");
		if (i < 10) wcscat_s(show, L"0");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L".WAV");
		combo1.AddString(show);
	}
	for (int i = 0; i < pView->set_sound[2]; i++) {
		wcscpy_s(show, L"SE_E_");
		if (i < 10) wcscat_s(show, L"0");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L".WAV");
		combo1.AddString(show);
	}
	int k = data->int_data[0];
	int p;
	if (k < 100) p = k;
	else if (k < 200) p = pView->set_sound[0] + k - 100;
	else p = pView->set_sound[0] + pView->set_sound[1] + k - 200;
	combo1.SetCurSel(p);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_35::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int k = combo1.GetCurSel();
	int p;
	if (k < pView->set_sound[0]) p = k;
	else if (k < pView->set_sound[0] + pView->set_sound[1]) p = k - pView->set_sound[0] + 100;
	else p = k - pView->set_sound[0] - pView->set_sound[1] + 200;
	data->int_data[0] = p;

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_36, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_36::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_36::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	for (int i = 0; i < pView->set_cd; i++) {
		wcscpy_s(show, L"Track");
		wcscat_s(show, std::to_wstring(i + 2).c_str());
		combo1.AddString(show);
	}
	combo1.AddString(L"无");
	combo1.SetCurSel(data->int_data[0] < 255 ? data->int_data[0] : pView->set_cd);
	return 0;
}

void Dialog_36::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel() == pView->set_cd ? 255 : combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_37, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_37::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_37::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit2.SetWindowTextW(show);
	return 0;
}

void Dialog_37::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_38, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_38::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_38::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit4.SetWindowTextW(show);
	return 0;
}

void Dialog_38::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}




BEGIN_MESSAGE_MAP(Dialog_39, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_39::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_39::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_39::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 4; i++)dat[i] = data->int_data[i + 1];
	dat[0] += 1;
	dat[2] += 41;
	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_39::OnCbnSelchangeCombo1()
{
	int combo_line = combo1.GetCurSel();
	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(dat[combo_line]).c_str());
	edit1.SetWindowTextW(show);
}

void Dialog_39::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	for (int i = 0; i < 4; i++)data->int_data[i + 1] = dat[i];
	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[data->int_data[0] + 1] = num;
	data->int_data[1] -= 1;
	data->int_data[3] -= 41;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_43, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_43::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_43::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	return 0;
}

void Dialog_43::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_44, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_44::OnBnClickedOk)
	ON_BN_CLICKED(IDC_WRAP_ENABLE, &Dialog_44::OnBnClickedWrapEnable)
	ON_EN_CHANGE(IDC_EDIT1, &Dialog_44::OnEnChangeWrapText)
END_MESSAGE_MAP()

BOOL Dialog_44::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit1.SetWindowTextW(str);
	SetLongTextInputLimit(edit1);
	SetWindowTextW(pView->code[pView->cur_code]);

	check1.SetCheck(pView->cur_data.int_data[0]);
	check2.SetCheck(pView->cur_data.int_data[1]);
	check3.SetCheck(pView->cur_data.int_data[2]);

	ConfigureWrapControls(this, true, false);
	wrap_enable.SetCheck(ccz::text_wrap::LoadEnabled(44, false, true) ? BST_CHECKED : BST_UNCHECKED);
	const int limit = ccz::text_wrap::LoadLimit(44, false);
	SetWrapLimitText(wrap_bytes, limit);
	wrap_updating = true;
	InitializeWrapEdit(edit1, wrap_state, str, limit,
		wrap_enable.GetCheck() == BST_CHECKED);
	wrap_updating = false;
	return TRUE;
}

void Dialog_44::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	CString str;
	edit1.GetWindowTextW(str);
	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(44, false));
	SetWrapLimitText(wrap_bytes, limit);
	const bool wrapEnabled = wrap_enable.GetCheck() == BST_CHECKED;
	if (wrapEnabled)
	{
		wrap_updating = true;
		ApplyWrapTextChange(edit1, wrap_state, limit);
		wrap_updating = false;
		str = ToCString(wrap_state.storage_text);
	}
	else
	{
		AdoptWrapEditText(edit1, wrap_state);
		str = ToStorageLineBreaks(str);
	}
	if (!StoreLongCharDataOrReport(data, str)) return;
	ccz::text_wrap::SaveLimit(44, false, limit);
	ccz::text_wrap::SaveEnabled(44, false, wrapEnabled);

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = check2.GetCheck();
	data->int_data[2] = check3.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

void Dialog_44::OnBnClickedWrapEnable()
{
	if (wrap_updating)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(44, false));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapToEdit(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

void Dialog_44::OnEnChangeWrapText()
{
	if (wrap_updating)
		return;

	CString limitText;
	wrap_bytes.GetWindowTextW(limitText);
	const int limit = ccz::text_wrap::ParseLimit(
		limitText, ccz::text_wrap::DefaultLimit(44, false));
	wrap_updating = true;
	if (wrap_enable.GetCheck() == BST_CHECKED)
		ApplyWrapTextChange(edit1, wrap_state, limit);
	else
		AdoptWrapEditText(edit1, wrap_state);
	wrap_updating = false;
}

BEGIN_MESSAGE_MAP(Dialog_46, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_46::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_46::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	if (data->id != 46)check1.ShowWindow(SW_HIDE);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	combo2.EnablePersonSearch();
	int code = data->int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	code = data->int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	check1.SetCheck(data->int_data[2]);
	return 0;
}

void Dialog_46::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	data->int_data[2] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_48, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_48::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_48::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit2.SetWindowTextW(show);

	for (int i = 0; i < 5; i++)combo2.AddString(pView->dir[i]);
	combo2.SetCurSel(data->int_data[3] >= 0 ? data->int_data[3] : 4);
	for (int i = 0; i < 21; i++)combo3.AddString(pView->ges[i]);
	combo3.SetCurSel(data->int_data[4] >= 0 ? data->int_data[4] : 20);
	return 0;
}

void Dialog_48::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	list = combo2.GetCurSel();
	data->int_data[3] = list < 4 ? list : -1;
	list = combo3.GetCurSel();
	data->int_data[4] = list < 20 ? list : -1;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_49, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_49::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_49::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_49::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 7; i++)combo3.AddString(pView->zhenying[i]);
	combo3.SetCurSel(data->int_data[6]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit4.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_49::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(1 - p);
	combo3.EnableWindow(p);
	edit1.EnableWindow(p);
	edit2.EnableWindow(p);
	edit3.EnableWindow(p);
	edit4.EnableWindow(p);
}


void Dialog_49::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;

	data->int_data[6] = combo3.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_50, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_50::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_50::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_50::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0] == 1 ? 1 : 0);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 5; i++)combo3.AddString(pView->dir[i]);
	combo3.SetCurSel(data->int_data[5] >= 0 ? data->int_data[5] : 4);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit3.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_50::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(1 - p);
	edit1.EnableWindow(p);
}


void Dialog_50::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	list = combo3.GetCurSel();
	data->int_data[5] = list < 4 ? list : -1;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_51, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_51::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_51::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 21; i++)combo2.AddString(pView->ges[i]);
	combo2.SetCurSel(data->int_data[1] >= 0 ? data->int_data[1] : 20);
	for (int i = 0; i < 5; i++)combo3.AddString(pView->dir[i]);
	combo3.SetCurSel(data->int_data[2] >= 0 ? data->int_data[2] : 4);
	return 0;
}

void Dialog_51::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	list = combo2.GetCurSel();
	data->int_data[1] = list < 20 ? list : -1;
	list = combo3.GetCurSel();
	data->int_data[2] = list < 4 ? list : -1;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_52, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_52::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_52::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 21; i++)combo2.AddString(pView->ges[i]);
	combo2.SetCurSel(data->int_data[1] < 255 ? data->int_data[1] : 20);
	return 0;
}

void Dialog_52::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	list = combo2.GetCurSel();
	data->int_data[1] = list < 20 ? list : -1;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_53, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_53::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_53::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_53::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_54, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_54::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_54::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < pView->per_condition_sum; i++)combo2.AddString(pView->per_condition[i]);
	combo2.SetCurSel(data->int_data[1]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare_sum; i++)combo3.AddString(pView->compare[i]);
	combo3.SetCurSel(data->int_data[3]);
	return 0;
}

void Dialog_54::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	data->int_data[1] = combo2.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	data->int_data[3] = combo3.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_55, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_55::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_55::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.AddString(L"钱"); combo1.AddString(L"剧本编号"); combo1.AddString(L"红蓝条");
	combo1.SetCurSel(data->int_data[0]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare_sum; i++)combo2.AddString(pView->compare[i]);
	combo2.SetCurSel(data->int_data[2]);
	return 0;
}

void Dialog_55::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	data->int_data[2] = combo2.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_56, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_56::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_56::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < pView->per_condition_sum; i++)combo2.AddString(pView->per_condition[i]);
	combo2.SetCurSel(data->int_data[1]);

	for (int i = 0; i < pView->operate_sum; i++)combo3.AddString(pView->operate[i]);
	combo3.SetCurSel(data->int_data[2]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_56::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	data->int_data[1] = combo2.GetCurSel();

	data->int_data[2] = combo3.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_58, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_58::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_58::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.AddString(L"钱"); combo1.AddString(L"剧本编号"); combo1.AddString(L"红蓝条");
	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < pView->operate_sum; i++)combo2.AddString(pView->operate[i]);
	combo2.SetCurSel(data->int_data[1]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_58::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	data->int_data[1] = combo2.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_59, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_59::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_59::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < pView->join_condition_sum; i++)combo2.AddString(pView->join_condition[i]);
	combo2.SetCurSel(data->int_data[1] == 255 ? 2 : data->int_data[1]);

	wchar_t show[30];
	for (int i = 0; i <= pView->set_level; i++) {
		wcscpy_s(show, L"+");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo3.AddString(show);
	}
	for (int i = 1; i <= pView->set_level; i++) {
		wcscpy_s(show, L"-");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo3.AddString(show);
	}
	combo3.SetCurSel(data->int_data[2] <= 2 * pView->set_level ? data->int_data[2] : 0);
	return 0;
}

void Dialog_59::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	data->int_data[1] = combo2.GetCurSel() == 2 ? 255 : combo2.GetCurSel();
	data->int_data[2] = combo3.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_60, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_60::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_60::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < pView->join_condition_sum; i++)combo2.AddString(pView->join_condition[i]);
	combo2.SetCurSel(data->int_data[1] == 255 ? 2 : data->int_data[1]);
	combo3.SetCurSel(data->int_data[2]);
	return 0;
}

void Dialog_60::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	data->int_data[1] = combo2.GetCurSel() == 2 ? 255 : combo2.GetCurSel();
	data->int_data[2] = combo3.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_61, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_61::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_61::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 256 * (1 + (int)pView->extend); i++)combo1.AddString(pView->item[i]);
	combo1.SetCurSel(data->int_data[0] >= 0 ? data->int_data[0] : 255);
	combo1.EnableItemSearch();

	/*combo2.AddString(L"默认等级");
	wchar_t show[10];
	for (int i = 1; i <= 16; i++) {
		wcscpy_s(show, L"Lv");
		wcscat_s(show, std::to_wstring(i).c_str());
		combo2.AddString(show);
	}
	combo2.SetCurSel(max(data->int_data[1], 0));*/
	if (data->int_data[1] < 0)data->int_data[1] = 0;
	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	check1.SetCheck(data->int_data[2]);

	for (int i = 0; i < 5375; i++)combo3.AddString(pView->per2[i]);
	combo3.EnablePersonSearch();
	int code = pView->cur_data.int_data[3];
	combo3.SetCurSel(Per2Code2List(code));
	return 0;
}

void Dialog_61::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int p = combo1.GetCurSel();
	data->int_data[0] = (p % 256) != 255 ? p : -1;
	CString str;
	edit1.GetWindowTextW(str);
	data->int_data[1] = CString2Int(str);
	data->int_data[2] = check1.GetCheck();
	int list = combo3.GetCurSel();
	data->int_data[3] = Per2List2Code(list);

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_62, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_62::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_62::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	combo2.AddString(L"默认装备"); combo2.AddString(L"卸去装备");
	for (int i = 0; i < pView->set_weapon; i++) combo2.AddString(pView->item[i]);
	combo2.SetCurSel(max(0, data->int_data[1]));
	combo2.EnableItemSegmentSearch(0);

	combo4.AddString(L"默认装备"); combo4.AddString(L"卸去装备");
	for (int i = 0; i < pView->set_armor; i++) combo4.AddString(pView->item[pView->set_weapon + i]);
	combo4.SetCurSel(max(0, data->int_data[3]));
	combo4.EnableItemSegmentSearch(pView->set_weapon);

	combo6.AddString(L"默认装备"); combo6.AddString(L"卸去装备");
	for (int i = 0; i < pView->set_product; i++) combo6.AddString(pView->item[pView->set_weapon + pView->set_armor + i]);
	combo6.SetCurSel(max(0, data->int_data[5]));
	combo6.EnableItemSegmentSearch(pView->set_weapon + pView->set_armor);

	combo3.AddString(L"默认"); combo5.AddString(L"默认");
	wchar_t show[10];
	for (int i = 1; i <= 16; i++) {
		wcscpy_s(show, L"Lv");
		wcscat_s(show, std::to_wstring(i).c_str());
		combo3.AddString(show);
		combo5.AddString(show);
	}
	combo3.SetCurSel(max(0, data->int_data[2]));
	combo5.SetCurSel(max(0, data->int_data[4]));
	return 0;
}

void Dialog_62::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	data->int_data[1] = combo2.GetCurSel();
	int p = combo3.GetCurSel();
	data->int_data[2] = p < 255 ? p : 65535;
	data->int_data[3] = combo4.GetCurSel();
	p = combo5.GetCurSel();
	data->int_data[4] = p < 255 ? p : 65535;
	data->int_data[5] = combo6.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_63, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_63::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_63::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare_sum; i++)combo1.AddString(pView->compare[i]);
	combo1.SetCurSel(data->int_data[1]);
	return 0;
}

void Dialog_63::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;

	data->int_data[1] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_64, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_64::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_64::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_64::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}




BEGIN_MESSAGE_MAP(Dialog_65, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_65::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO4, &Dialog_65::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_65::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 7; i++)combo1.AddString(pView->zhenying[i]);
	combo1.SetCurSel(data->int_data[0]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare_sum; i++)combo2.AddString(pView->compare[i]);
	combo2.SetCurSel(data->int_data[2]);

	combo3.SetCurSel(data->int_data[3]);

	show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
	edit4.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[7]).c_str());
	edit5.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_65::OnCbnSelchangeCombo1()
{
	int p = combo3.GetCurSel();
	edit2.EnableWindow(p);
	edit3.EnableWindow(p);
	edit4.EnableWindow(p);
	edit5.EnableWindow(p);
}


void Dialog_65::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	data->int_data[2] = combo2.GetCurSel();
	data->int_data[3] = combo3.GetCurSel();

	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[6] = num;
	edit5.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[7] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_69, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_69::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_69::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	check2.SetCheck(data->int_data[1]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i <= pView->set_level; i++) {
		wcscpy_s(show, L"+");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo1.AddString(show);
	}
	for (int i = 1; i <= pView->set_level; i++) {
		wcscpy_s(show, L"-");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo1.AddString(show);
	}
	combo1.SetCurSel(data->int_data[3] <= 2 * pView->set_level ? data->int_data[3] : 0);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);

	for (int i = 0; i < 5375; i++) {
		combo2.AddString(pView->per2[i]);
		combo3.AddString(pView->per2[i]);
	}
	combo2.EnablePersonSearch();
	combo3.EnablePersonSearch();
	combo2.SetCurSel(Per2Code2List(pView->cur_data.int_data[5]));

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
	edit3.SetWindowTextW(show);

	combo3.SetCurSel(Per2Code2List(pView->cur_data.int_data[7]));

	for (int i = 0; i < 5; i++)combo4.AddString(pView->weather[i]);
	combo4.SetCurSel(data->int_data[8]);

	for (int i = 0; i < 6; i++)combo5.AddString(pView->weather2[i]);
	combo5.SetCurSel(data->int_data[9]);
	return 0;
}

void Dialog_69::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = check2.GetCheck();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	data->int_data[3] = combo1.GetCurSel();

	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	int list = combo2.GetCurSel();
	data->int_data[5] = Per2List2Code(list);

	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[6] = num;

	list = combo3.GetCurSel();
	data->int_data[7] = Per2List2Code(list);

	data->int_data[8] = combo4.GetCurSel();
	data->int_data[9] = combo5.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_70, myDialog)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_70::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO12, &Dialog_70::OnCbnSelchangeCombo2)
	ON_LBN_SELCHANGE(IDC_LIST1, &Dialog_70::OnLbnSelchangeList1)
	ON_BN_CLICKED(IDOK, &Dialog_70::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_70::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	int id = data->id - 70;

	for (int i = 0; i < (11 + id) * (20 + id * 60); i++)dat[i] = data->int_data[i];

	/*检查一下这是连续的第几个*/
	quan = 0;
	HTREEITEM tmp = pView->cur_item;
	while (true) {
		tmp = pView->FindLast(tmp);
		if (tmp == TVI_FIRST)break;
		ItemData* dat = (ItemData*)m_TreeCtrl.GetItemData(tmp);
		if (dat->id == data->id)quan++;
		else break;
	}

	list1.SetItemHeight(0, 18);
	for (int i = 0; i < 20 + id * 60; i++) {
		wchar_t index[50];
		wcscpy_s(index, L"[");
		wcscat_s(index, std::to_wstring((20 + 40 * id) + i + quan * (20 + id * 60)).c_str());
		wcscat_s(index, L"]");
		wcscat_s(index, pView->per2[Per2Code2List(dat[i * (11 + id)])]);
		list1.AddString(index);
	}

	check1.ShowWindow(id);

	for (int i = 0; i < 5375; i++) {
		combo1.AddString(pView->per2[i]);
		combo6.AddString(pView->per2[i]);
	}
	combo1.EnablePersonSearch();
	combo6.EnablePersonSearch();

	for (int i = 0; i < 5; i++) combo2.AddString(pView->dir[i]);

	wchar_t show[100];
	for (int i = 0; i <= pView->set_level; i++) {
		wcscpy_s(show, L"+");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo3.AddString(show);
	}
	for (int i = 1; i <= pView->set_level; i++) {
		wcscpy_s(show, L"-");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L"级");
		combo3.AddString(show);
	}
	for (int i = 0; i < pView->policy_sum; i++)combo5.AddString(pView->policy[i]);

	list_line = 0;
	list1.SetCurSel(0);
	OnLbnSelchangeList1();
	return 0;
}

void Dialog_70::OnCbnSelchangeCombo1()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	int id = data->id - 70;
	//list1.SetDlgItemTextW(list_line, pView->per2[combo1.GetCurSel()]);
	list1.DeleteString(list_line);
	wchar_t index[50];
	wcscpy_s(index, L"[");
	wcscat_s(index, std::to_wstring(list_line + quan * (20 + id * 60)).c_str());
	wcscat_s(index, L"]");
	wcscat_s(index, pView->per2[combo1.GetCurSel()]);
	list1.InsertString(list_line, index);
	list1.SetCurSel(list_line);
}

void Dialog_70::OnCbnSelchangeCombo2()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	int id = data->id - 70;
	dat[list_line * (11 + id) + 7 + id] = combo5.GetCurSel();
	wchar_t show[100];
	int p = dat[list_line * (11 + id) + 7 + id];
	if (p != 3 && p != 5) {
		static1.ShowWindow(SW_HIDE);
		combo6.ShowWindow(SW_HIDE);
	}
	else {
		static1.ShowWindow(SW_SHOW);
		combo6.ShowWindow(SW_SHOW);
		combo6.SetCurSel(Per2Code2List(dat[list_line * (11 + id) + (8 + id)]));
	}
	if (p != 4 && p != 6) {
		static2.ShowWindow(SW_HIDE);
		edit3.ShowWindow(SW_HIDE);
		edit4.ShowWindow(SW_HIDE);
	}
	else {
		static2.ShowWindow(SW_SHOW);
		edit3.ShowWindow(SW_SHOW);
		edit4.ShowWindow(SW_SHOW);
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(dat[list_line * (11 + id) + (9 + id)]).c_str());
		edit3.SetWindowTextW(show);
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(dat[list_line * (11 + id) + (10 + id)]).c_str());
		edit4.SetWindowTextW(show);
	}
}

void Dialog_70::OnLbnSelchangeList1()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	int id = data->id - 70;

	if (list_line != list1.GetCurSel() || ending == true) {
		dat[list_line * (11 + id)] = Per2List2Code(combo1.GetCurSel());
		dat[list_line * (11 + id) + 1] = check1.GetCheck();
		dat[list_line * (11 + id) + 1 + id] = check2.GetCheck();
		CString str; int num = 0;
		edit1.GetWindowTextW(str);
		num = CString2Int(str);
		dat[list_line * (11 + id) + 2 + id] = num;
		edit2.GetWindowTextW(str);
		num = CString2Int(str);
		dat[list_line * (11 + id) + 3 + id] = num;
		dat[list_line * (11 + id) + 4 + id] = combo2.GetCurSel() < 4 ? combo2.GetCurSel() : -1;
		dat[list_line * (11 + id) + 5 + id] = combo3.GetCurSel();
		dat[list_line * (11 + id) + 6 + id] = combo4.GetCurSel();
		dat[list_line * (11 + id) + 7 + id] = combo5.GetCurSel();
		dat[list_line * (11 + id) + 8 + id] = Per2List2Code(combo6.GetCurSel());
		edit3.GetWindowTextW(str);
		num = CString2Int(str);
		dat[list_line * (11 + id) + 9 + id] = num;
		edit4.GetWindowTextW(str);
		num = CString2Int(str);
		dat[list_line * (11 + id) + 10 + id] = num;
	}

	list_line = list1.GetCurSel();
	combo1.SetCurSel(Per2Code2List(dat[list_line * (11 + id)]));
	check1.SetCheck(dat[list_line * (11 + id) + 1] == 1);
	check2.SetCheck(dat[list_line * (11 + id) + 1 + id] == 1);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(dat[list_line * (11 + id) + 2 + id]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(dat[list_line * (11 + id) + 3 + id]).c_str());
	edit2.SetWindowTextW(show);
	combo2.SetCurSel(dat[list_line * (11 + id) + 4 + id] >= 0 ? dat[list_line * (11 + id) + 4 + id] : 4);
	combo3.SetCurSel(dat[list_line * (11 + id) + 5 + id] >= 0 ? dat[list_line * (11 + id) + 5 + id] : 0);
	combo4.SetCurSel(dat[list_line * (11 + id) + 6 + id] >= 0 ? dat[list_line * (11 + id) + 6 + id] : 2);
	combo5.SetCurSel(dat[list_line * (11 + id) + 7 + id] >= 0 ? dat[list_line * (11 + id) + 7 + id] : 1);

	OnCbnSelchangeCombo2();
}

void Dialog_70::OnBnClickedOk()
{
	ending = true;
	OnLbnSelchangeList1();
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
	int id = data->id - 70;
	for (int i = 0; i < 12 * (20 + id * 60 ); i++) data->int_data[i] = dat[i];

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_75, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_75::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_75::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit3.SetWindowTextW(show);

	for (int i = 0; i < 5; i++)combo1.AddString(pView->dir[i]);
	combo1.SetCurSel(data->int_data[3] >= 0 ? data->int_data[3] : 4);
	
	check1.SetCheck(data->int_data[4]);
	return 0;
}

void Dialog_75::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	int list = combo1.GetCurSel();
	data->int_data[3] = list < 4 ? list : -1;

	data->int_data[4] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_76, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_76::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_76::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_76::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_76::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(1 - p);
	edit1.EnableWindow(p);
}

void Dialog_76::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_77, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_77::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_77::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_77::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit4.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[6]).c_str());
	edit5.SetWindowTextW(show);

	for (int i = 0; i < 7; i++)combo3.AddString(pView->zhenying[i]);
	combo3.SetCurSel(data->int_data[7]);

	for (int i = 0; i < pView->per_condition_war_sum; i++)combo4.AddString(pView->per_condition_war[i]);
	combo4.SetCurSel(data->int_data[8] >= 0 ? data->int_data[8] : 6);

	for (int i = 0; i < 4; i++)combo5.AddString(pView->changes[i]);
	combo5.SetCurSel(data->int_data[9] >= 0 ? data->int_data[9] : 3);

	//for (int i = 0; i < 31; i++)combo6.AddString(pView->debuff[i]);
	check1.SetWindowTextW(pView->debuff[0]);
	check2.SetWindowTextW(pView->debuff[1]);
	check3.SetWindowTextW(pView->debuff[2]);
	check4.SetWindowTextW(pView->debuff[3]);
	check5.SetWindowTextW(pView->debuff[4]);
	check6.SetWindowTextW(pView->debuff[5]);
	int p = data->int_data[10];
	if (p < 0)p = 0;
	combo6.SetCurSel(p < 128 ? 0 : 1);
	check1.SetCheck(p & 2);
	check2.SetCheck(p & 4);
	check3.SetCheck(p & 8);
	check4.SetCheck(p & 16);
	check5.SetCheck(p & 32);
	check6.SetCheck(p & 64);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[11]).c_str());
	edit6.SetWindowTextW(show);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[12]).c_str());
	edit7.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_77::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(p == 0);
	edit1.EnableWindow(p == 1);
	edit2.EnableWindow(p == 2);
	edit3.EnableWindow(p == 2);
	edit4.EnableWindow(p == 2);
	edit5.EnableWindow(p == 2);
	combo3.EnableWindow(p == 2);
}


void Dialog_77::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;
	edit5.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[6] = num;

	data->int_data[7] = combo3.GetCurSel();
	data->int_data[8] = combo4.GetCurSel() < 6 ? combo4.GetCurSel() : -1;
	data->int_data[9] = combo5.GetCurSel() < 3 ? combo5.GetCurSel() : -1;

	//data->int_data[10] = DebuffList2Code(combo6.GetCurSel());
	data->int_data[10] = check1.GetCheck() * 2 + check2.GetCheck() * 4 + check3.GetCheck() * 8 +
		check4.GetCheck() * 16 + check5.GetCheck() * 32 + check6.GetCheck() * 64 + combo6.GetCurSel() * 128;
	if (data->int_data[10] == 0 || data->int_data[10] == 128) data->int_data[10] = -1;

	edit6.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[11] = num;
	edit7.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[12] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_78, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_78::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_78::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO8, &Dialog_78::OnCbnSelchangeCombo8)
END_MESSAGE_MAP()


BOOL Dialog_78::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0] < 255 ? data->int_data[0] : 0);

	for (int i = 0; i < 5375; i++) {
		combo2.AddString(pView->per2[i]);
		combo5.AddString(pView->per2[i]);
	}
	combo2.EnablePersonSearch();
	combo5.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit4.SetWindowTextW(show);

	for (int i = 0; i < 7; i++)combo3.AddString(pView->zhenying[i]);
	combo3.SetCurSel(data->int_data[6]);

	for (int i = 0; i < pView->policy_sum; i++)combo4.AddString(pView->policy[i]);
	combo4.SetCurSel(data->int_data[7]);

	combo5.SetCurSel(data->int_data[8]);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[9]).c_str());
	edit5.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[10]).c_str());
	edit6.SetWindowTextW(show);

	OnCbnSelchangeCombo1();
	OnCbnSelchangeCombo8();
	return 0;
}

void Dialog_78::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(p == 0);
	edit1.EnableWindow(p == 1);
	edit2.EnableWindow(p == 1);
	edit3.EnableWindow(p == 1);
	edit4.EnableWindow(p == 1);
	combo3.EnableWindow(p == 1);
}

void Dialog_78::OnCbnSelchangeCombo8()
{
	int p = combo4.GetCurSel();
	combo5.ShowWindow(p == 3 || p == 5);
	edit5.ShowWindow(p == 4 || p == 6);
	edit6.ShowWindow(p == 4 || p == 6);
	static1.ShowWindow(p == 3 || p == 5);
	static2.ShowWindow(p == 4 || p == 6);
	static3.ShowWindow(p == 4 || p == 6);
}


void Dialog_78::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;

	data->int_data[6] = combo3.GetCurSel();
	data->int_data[7] = combo4.GetCurSel();

	data->int_data[8] = combo5.GetCurSel();

	edit5.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[9] = num;
	edit6.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[10] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_79, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_79::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_79::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = data->int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	code = data->int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 5; i++)combo3.AddString(pView->dir[i]);
	combo3.SetCurSel(data->int_data[2] >= 0 ? data->int_data[2] : 4);

	check1.SetCheck(data->int_data[3] > 0);
	check2.SetCheck(data->int_data[4]);
	check3.SetCheck(data->int_data[5]);
	return 0;
}

void Dialog_79::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	data->int_data[2] = combo3.GetCurSel() < 4 ? combo3.GetCurSel() : -1;

	data->int_data[3] = check1.GetCheck();
	data->int_data[4] = check2.GetCheck();
	data->int_data[5] = check3.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_80, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_80::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_80::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = data->int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 14; i++)combo2.AddString(pView->ges_war[i]);
	combo2.SetCurSel(data->int_data[1] >= 0 ? data->int_data[1] : 13);

	check1.SetCheck(data->int_data[2]);
	check2.SetCheck(data->int_data[3]);
	return 0;
}

void Dialog_80::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	data->int_data[1] = combo2.GetCurSel() < 13 ? combo2.GetCurSel() : -1;

	data->int_data[2] = check1.GetCheck();
	data->int_data[3] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_82, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_82::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_82::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < 80; i++)combo2.AddString(pView->job[i]);
	combo2.SetCurSel(data->int_data[1]);
	return 0;
}

void Dialog_82::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	data->int_data[1] = combo2.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_83, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_83::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_83::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_83::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit4.SetWindowTextW(show);

	for (int i = 0; i < 7; i++)combo3.AddString(pView->zhenying[i]);
	combo3.SetCurSel(data->int_data[6]);

	check1.SetCheck(data->int_data[7]);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_83::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(p == 0);
	edit1.EnableWindow(p == 1);
	edit2.EnableWindow(p == 1);
	edit3.EnableWindow(p == 1);
	edit4.EnableWindow(p == 1);
	combo3.EnableWindow(p == 1);
}


void Dialog_83::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;

	data->int_data[6] = combo3.GetCurSel();
	data->int_data[7] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_86, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_86::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_86::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	if (data->id == 86)
		for (int i = 0; i < 5; i++) combo1.AddString(pView->weather[i]);
	else
		for (int i = 0; i < 6; i++) combo1.AddString(pView->weather2[i]);
	combo1.SetCurSel(data->int_data[0]);
	return 0;
}

void Dialog_86::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_88, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_88::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_88::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 128; i++)combo1.AddString(pView->object[i]);
	combo1.SetCurSel(data->int_data[0]);

	combo2.SetCurSel(data->int_data[1]);

	for (int i = 0; i < 30; i++)combo3.AddString(pView->hexz[i]);
	combo3.SetCurSel(data->int_data[2]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");

	check1.SetCheck(data->int_data[5]);
	check2.SetCheck(data->int_data[6]);
	return 0;
}

void Dialog_88::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();
	data->int_data[1] = combo2.GetCurSel();
	data->int_data[2] = combo3.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	data->int_data[5] = check1.GetCheck();
	data->int_data[6] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_89, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_89::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_89::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < 256 * (1 + (int)pView->extend); i++) {
		combo1.AddString(pView->item[i]);
		combo3.AddString(pView->item[i]);
		combo5.AddString(pView->item[i]);
	}
	combo1.EnableItemSearch();
	combo3.EnableItemSearch();
	combo5.EnableItemSearch();
	combo1.SetCurSel(data->int_data[1] >= 0 ? data->int_data[1] : 255);
	combo3.SetCurSel(data->int_data[3] >= 0 ? data->int_data[3] : 255);
	combo5.SetCurSel(data->int_data[5] >= 0 ? data->int_data[5] : 255);

	bool normalizedInstructionValues = false;
	if (data->int_data[2] < 0) {
		data->int_data[2] = 0;
		normalizedInstructionValues = true;
	}
	if (data->int_data[4] < 0) {
		data->int_data[4] = 0;
		normalizedInstructionValues = true;
	}
	if (data->int_data[6] < 0) {
		data->int_data[6] = 0;
		normalizedInstructionValues = true;
	}
	if (normalizedInstructionValues)
		pView->GetDocument()->MarkEditorContentChanged();
	wcscpy_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit4.SetWindowTextW(show);
	wcscpy_s(show, std::to_wstring(data->int_data[6]).c_str());
	edit6.SetWindowTextW(show);

	/*combo2.AddString(L"默认"); combo4.AddString(L"默认"); combo6.AddString(L"默认");
	for (int i = 1; i <= 16; i++) {
		wcscpy_s(show, L"Lv");
		wcscat_s(show, std::to_wstring(i).c_str());
		combo2.AddString(show); combo4.AddString(show); combo6.AddString(show);
	}
	combo2.SetCurSel(max(0, data->int_data[2]));
	combo4.SetCurSel(max(0, data->int_data[4]));
	combo6.SetCurSel(max(0, data->int_data[6]));*/

	check1.SetCheck(data->int_data[7]);
	return 0;
}

void Dialog_89::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;

	int p = combo1.GetCurSel();
	data->int_data[1] = (combo1.GetCurSel() % 256) != 255 ? combo1.GetCurSel() : -1;
	data->int_data[3] = (combo3.GetCurSel() < 255) != 255 ? combo3.GetCurSel() : -1;
	data->int_data[5] = (combo5.GetCurSel() < 255) != 255 ? combo5.GetCurSel() : -1;

	edit2.GetWindowTextW(str);
	data->int_data[2] = CString2Int(str);
	edit4.GetWindowTextW(str);
	data->int_data[4] = CString2Int(str);
	edit6.GetWindowTextW(str);
	data->int_data[6] = CString2Int(str);

	data->int_data[7] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_91, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_91::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_91::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit3.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[3]).c_str());
	edit4.SetWindowTextW(show);

	check1.SetCheck(data->int_data[4]);
	return 0;
}

void Dialog_91::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit4.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[3] = num;

	data->int_data[4] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_93, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_93::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_93::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < pView->operate_sum; i++)combo1.AddString(pView->operate[i]);
	combo1.SetCurSel(data->int_data[0]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_93::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_96, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_96::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_96::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit1.SetWindowTextW(str);

	for (int i = 0; i < 16; i++)combo1.AddString(pView->solo_ges[i]);
	combo1.SetCurSel(data->int_data[1]);
	return TRUE;
}

void Dialog_96::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str;
	edit1.GetWindowTextW(str);
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!StoreLongCharDataOrReport(data, str))
		return;

	data->int_data[0] = check1.GetCheck();

	data->int_data[1] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_99, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_99::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_99::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	check2.SetCheck(data->int_data[1]);
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit1.SetWindowTextW(str);
	return TRUE;
}

void Dialog_99::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str;
	edit1.GetWindowTextW(str);
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!StoreLongCharDataOrReport(data, str))
		return;

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_100, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_100::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_100::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	for (int i = 0; i < 16; i++)combo1.AddString(pView->solo_ges[i]);
	combo1.SetCurSel(data->int_data[1]);
	return TRUE;
}

void Dialog_100::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = combo1.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_101, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_101::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_101::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	for (int i = 0; i < 5; i++)combo1.AddString(pView->solo_atk1[i]);
	combo1.SetCurSel(data->int_data[1]);
	check2.SetCheck(data->int_data[2]);
	return TRUE;
}

void Dialog_101::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = combo1.GetCurSel();
	data->int_data[2] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_102, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_102::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_102::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	check1.SetCheck(data->int_data[0]);
	for (int i = 0; i < 3; i++)combo1.AddString(pView->solo_atk2[i]);
	combo1.SetCurSel(data->int_data[1]);
	check2.SetCheck(data->int_data[2]);
	return TRUE;
}

void Dialog_102::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = check1.GetCheck();
	data->int_data[1] = combo1.GetCurSel();
	data->int_data[2] = check2.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_103, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_103::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL Dialog_103::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}

	edit2.SetWindowTextW(str);
	return TRUE;
}

void Dialog_103::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	edit2.GetWindowTextW(str);
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!StoreLongCharDataOrReport(data, str))
		return;

	data->int_data[0] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_104, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_104::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_104::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = data->int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	code = data->int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	
	return 0;
}

void Dialog_104::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_107, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_107::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_107::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit2.SetWindowTextW(show);

	for (int i = 0; i < pView->set_fashu; i++) {
		wcscpy_s(show, L"MEff-");
		wcscat_s(show, std::to_wstring(i + 1).c_str());
		combo1.AddString(show);
	}
	for (int i = 0; i < 10; i++) {
		wcscpy_s(show, L"MCall-");
		wcscat_s(show, std::to_wstring(i).c_str()); 
		wcscat_s(show, L".e5");
		combo1.AddString(show);
	}
	combo1.SetCurSel(data->int_data[2] < 100 ? data->int_data[2] : data->int_data[2] - 100 + pView->set_fashu);

	check1.SetCheck(data->int_data[3]);
	return 0;
}

void Dialog_107::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	int list = combo1.GetCurSel();
	data->int_data[2] = list < pView->set_fashu ? list : list - pView->set_fashu + 100;

	data->int_data[3] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_109, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_109::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_109::OnCbnSelchangeCombo1)
END_MESSAGE_MAP()


BOOL Dialog_109::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	combo1.SetCurSel(data->int_data[0]);

	for (int i = 0; i < 5375; i++) {
		combo2.AddString(pView->per2[i]);
		combo3.AddString(pView->per2[i]);
	}
	combo2.EnablePersonSearch();
	combo3.EnablePersonSearch();
	combo2.SetCurSel(Per2Code2List(data->int_data[1]));
	combo3.SetCurSel(Per2Code2List(data->int_data[3]));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[5]).c_str());
	edit3.SetWindowTextW(show);

	for (int i = 0; i < 5; i++)combo4.AddString(pView->dir[i]);
	combo4.SetCurSel(data->int_data[6] >= 0 ? data->int_data[6] : 4);

	check1.SetCheck(data->int_data[7]);

	OnCbnSelchangeCombo1();
	return 0;
}

void Dialog_109::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	combo2.EnableWindow(1 - p);
	edit1.EnableWindow(p);
}


void Dialog_109::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	data->int_data[1] = Per2List2Code(combo2.GetCurSel());

	data->int_data[3] = Per2List2Code(combo3.GetCurSel());

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;
	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;
	edit3.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[5] = num;

	int list = combo4.GetCurSel();
	data->int_data[6] = list < 4 ? list : -1;

	data->int_data[7] = check1.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_111, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_111::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_111::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 256; i++)combo1.AddString(pView->item[i]);
	combo1.EnableItemSearch();
	combo1.SetCurSel(data->int_data[0] < 255 ? data->int_data[0] : 255);
	return 0;
}

void Dialog_111::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int p = combo1.GetCurSel();
	data->int_data[0] = p < 255 ? p : 65535;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_112, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_112::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_112::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	combo2.EnablePersonSearch();
	int code = data->int_data[0];
	combo1.SetCurSel(Per2Code2List(code));
	code = data->int_data[1];
	combo2.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[2]).c_str());
	edit1.SetWindowTextW(show);
	return 0;
}

void Dialog_112::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);
	list = combo2.GetCurSel();
	data->int_data[1] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[2] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_114, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_114::OnBnClickedOk)
	ON_CBN_SELCHANGE(IDC_COMBO1, &Dialog_114::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO3, &Dialog_114::OnCbnSelchangeCombo2)
	ON_BN_CLICKED(IDC_BUTTON1, &Dialog_114::OnBnClickedButton1)
END_MESSAGE_MAP()

BOOL Dialog_114::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);
	CString str = pView->cur_data.long_char_data;
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\n') {
			str.Insert(i, L"\r");
			i++;
		}
	}
	for (int i = 0; i < pView->xxcs_sum; i++) combo1.AddString(pView->xxcs[i]);
	if (data->int_data[0] < pView->xxcs_sum)
		combo1.SetCurSel(data->int_data[0]);
	list1.SetItemHeight(0, 18);

	combo2.SetCurSel(0);
	if (pView->set_tejibase > 0)
		combo2.AddString(L"特技");

	for (int i = 0; i < 1024; i++) list1.AddString(pView->per2[i]);

	edit2.SetWindowTextW(str);
	return TRUE;
}

void Dialog_114::OnCbnSelchangeCombo1()
{
	int p = combo1.GetCurSel();
	edit1.SetWindowTextW(std::to_wstring(p).c_str());
}

void Dialog_114::OnCbnSelchangeCombo2()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	int Count = list1.GetCount();
	for (int i = Count; i >= 0; i--) list1.DeleteString(i);

	if (combo2.GetCurSel() == 0)for (int i = 0; i < 1024; i++) list1.AddString(pView->per2[i]);
	else if (combo2.GetCurSel() == 1)for (int i = 0; i < 255; i++) list1.AddString(pView->item[i]);
	else if (combo2.GetCurSel() == 2)for (int i = 0; i < 144; i++) list1.AddString(pView->meff[i]);
	else if (combo2.GetCurSel() == 3)for (int i = 0; i < 255; i++) list1.AddString(pView->teji[i]);
}

void Dialog_114::OnBnClickedButton1()
{
	int p = combo1.GetCurSel();
	if (p == 0)
		edit2.SetWindowTextW(L"张飞\r\n需要提前用4054指定要修改的角色的data编号");
	else if (p == 1)
		edit2.SetWindowTextW(L"100\r\n3\r\n1\r\n110|662|663|\r\n第一行让武将的血量改变，可以填写正负数或0（ 负数则减少，可以降到0并撤退，0则不改变，正数表示增加HP，正数前不要有“+”号）\r\n第二行表示使用哪一个策略动画\r\n第三行取值为0或1，表示是否切换视点\r\n第四行是一些人物的data号，data号之间用“|”号分隔");
	else if (p == 2)
		edit2.SetWindowTextW(L"100\r\n3\r\n1\r\n1\r\n0,0|0,1|3,4|\r\n第一行让武将的血量改变，可以填写正负数或0（ 负数则减少，可以降到0并撤退，0则不改变，正数表示增加HP，正数前不要有“+”号）\r\n第二行表示使用哪一个策略动画\r\n第三行取值为0或1，表示是否切换视点\r\n第四行取值0-6,表示受影响的阵营\r\n第五行是一些地点的坐标，横坐标和纵坐标之间用\", \"分隔，坐标之间用“ | ”号分隔");
	else if (p == 3)
		edit2.SetWindowTextW(L"100\r\n3\r\n1\r\n1\r\n0,0|30,30|\r\n第一行让武将的血量改变，可以填写正负数或0（ 负数则减少，可以降到0并撤退，0则不改变，正数表示增加HP，正数前不要有“+”号）\r\n第二行表示使用哪一个策略动画\r\n第三行取值为0或1，表示是否切换视点\r\n第四行取值0-6,表示受影响的阵营\r\n第五行是区域的左上角、右下角坐标");
	else if (p == 4)
		edit2.SetWindowTextW(L"26\r\n1\r\n2\r\n第一行为策略编号；第二行为习得时的人物等级；第三行为人物data号\r\n如果要取消已经习得的策略，可以把第二行数字设为一个不可能达到的等级，如254");
	else if (p == 5)
		edit2.SetWindowTextW(L"81\r\n0\r\n1\r\n第一行是技能编号\r\n第二行是特技位置，这一行取值范围是0-3（0-2表示武将的个人天赋，3表示兵种技能）\r\n第三行是武将data号（当第二行取值0-2时）或兵种编号（当第二行取值3时），为1024相当于取消特技。");
	else if (p == 6)
		edit2.SetWindowTextW(L"26\r\n0\r\n1\r\n2\r\n第一行是必杀编号\r\n第二行是必杀位置，这一行取值范围是0-4（一个必杀可以同时分配给五名武将）\r\n第三行是必杀的领悟等级\r\n第四行是武将data号\r\n前提是要开启必杀，同时这个武将的兵种是武将类必杀才会有效果。");
	else if (p == 7)
		edit2.SetWindowTextW(L"10,20\r\n分别是买入商店和卖出商店的说话角色data编号");
	else if (p == 8)
		edit2.SetWindowTextW(L"1\r\n颍川之战\r\n要搭配4051和剧本跳转指令，第一行的1表示在跳转目标的基础上+100，第二行写战役名称");
	else if (p == 9)
		edit2.SetWindowTextW(L"1,2,5,7,12,145\r\n依次输入要展示的data编号，不要强制换行");
	else if (p == 10)
		edit2.SetWindowTextW(L"0\r\n0,15,3,30,10\r\n第一行的0表示专属，第二行的0表示位置（取值范围0-1），15表示特效编号，3表示习得武将，30表示30号道具，10表示效果值为10\r\n1\r\n1,26,1,57,255,1\r\n第一行的1表示套装，第二行的1表示位置（取值范围0-1），26表示特效编号，1、57、255分别三类装备，1表示效果值为1");
	else if (p == 11)
		edit2.SetWindowTextW(L"13,5|15,7\r\n1|60,61\r\n第一行是限定其行动的区域\r\n第二行 0 data 或 1 战场编号\r\n第三行 data或战场编号序列，用逗号分隔，不要强制换行\r\n取消方式：第一行改为 0,0|255,255");
	else if (p == 12)
		edit2.SetWindowTextW(L"39\r\n96,120\r\n3,80\r\n0,0,5,255\r\n10第一行  动态图编号（范围 0-39）\r\n第二行  X,Y        (R直角 S格子)\r\n第三行  光标变手&绘图层次,延时   \r\n        第三行的第一个数字是类似4按钮的写法  如 填写3（二进制的11）表示光标要变手、在人物形象上方绘图 \r\n第四行  0,0,循环次数(0 无限,n次数),是否自动消失（n 不消失，255消失）\r\n第五行  DT中图片的编号\r\n取消动态图  除第一行填编号外，其余数据全部填0");
	else if (p == 13)
		edit2.SetWindowTextW(L"2\r\n0,60\r\n0,4794352,0\r\n4220145\r\n第一行 入栈参数个数\r\n第二行 参数1, 参数2, 参数3...\r\n第三行 eax, ecx, edx\r\n第四行 目标函数的地址\r\n如果是调用没有入栈参数的函数则第一行填0，不输第二行");
	else if (p == 14)
		edit2.SetWindowTextW(L"0,80,0\r\n第一个0是颜色色号，80是透明程度（写0还原），最后的0表示是开局（1则是中途）");
	else if (p == 15)
		edit2.SetWindowTextW(L"0,200016,4,20\r\n第一行：0火1船2火船\r\n第二行：火在内存里的相对位置（火的位置 200016、船的位置 227664、火船的位置 241488）\r\n第三行：火是4帧\r\n第四行：图在U_selsct里的编号\r\n如果要还原，第二行填0，其他数字不变");
	else if (p == 16)
		edit2.SetWindowTextW(L"1\r\n输入data角色编号");
	else if (p == 17)
		edit2.SetWindowTextW(L"6,4|13,11\r\n6,4|13,11\r\n第二行区域内的AI只在第一行的范围内活动\r\n取消方式：第一行改为 0,0|255,255");
	else if (p == 18)
		edit2.SetWindowTextW(L"不需要写东西");
	else if (p == 19)
		edit2.SetWindowTextW(L"200\r\n清楚一个特殊指针变量");
	else if (p == 20)
		edit2.SetWindowTextW(L"该编号暂无含义");
	else if (p == 21)
		edit2.SetWindowTextW(L"0,5,3000\r\n第一个数字0表示获取，5表示战场编号为5的角色，3000表示放入3000号整型变量\r\n1,5,0\r\n第一个数字1表示设置，5表示战场编号为5的角色，0表示设置为未行动");
	else if (p == 22)
		edit2.SetWindowTextW(L"0,1\r\n第一个数字012表示红黄蓝鸟，第二个数字0等同于0F指令\r\n1表示拿鸟，无结局动画，无制作群，不结束游戏\r\n2同1，但是有制作群");
	else if (p == 23)
		edit2.SetWindowTextW(L"1\r\n*0,3\r\n第一个数字n表示取768+n号列传\r\n第二行依次写 要使用该列传的武将的data编号");
	else if (p == 24)
		edit2.SetWindowTextW(L"1,0,2\r\n第一个数字0表示取消1表示设置\r\n第二个数字是角色data编号\r\n语音编号（命名规则  wav\Se_v_xxx.wav）");
	else if (p == 25)
		edit2.SetWindowTextW(L"0\r\n60,100\r\n8,135790,1\r\n21\r\n第一行   编号（取值范围0-13)\r\n第二行   坐标X, Y         （这里采用的是直角坐标系统，左上角为（0，0））\r\n第三行   1、数字的位数   （填写0 显示实际数字，最大8  如12345显示为  00012345）\r\n2、要显示的数字 （可以是个常数，亦可以是整形变量，要填写 * n格式）\r\n3、0 横向排版、1 竖向排版\r\n第四行   图片在U_select里的编号 （图片尺寸上限48 * 640，可以小，但不能更大，同时宽度要为4的整数倍，图为10帧，0 - 9依次上下排列）");
	else if (p == 26)
		edit2.SetWindowTextW(L"100\r\n得到一个0至100-1之间的随机数，并以Dword型保存在整形变量4025中");
	else if (p == 27)
		edit2.SetWindowTextW(L"0\r\n2,4,6,20\r\n第一行 0 清空、1 如开启了功勋共享则功勋进入功勋池 否则同0\r\n第二行 DATA序列");
	else if (p == 28)
		edit2.SetWindowTextW(L"0\r\n140,140\r\n1,100\r\n1\r\n第一行 R插图编号 取值范围 0-7  （最多可以同时使用8个R插图）\r\n第二行 X, Y  （是直角坐标）\r\n第三行光标变手&绘图层次  取值 0（在R人物形象下方绘图），1（在R人物形象上方绘图） \r\n类似4按钮的写法  如 填写3（二进制的11）表示光标要变手、在人物形象上方绘图\r\n第二个数字表示R插图的透明程度  范围0-100，数字越大越清楚 \r\n第四行 图片在Tr.e5里的序号，从0开始计数，但0又表示取消，所以Ts.e5的第一个图其实不能用，随便放一个图凑数即可\r\n取消R插图  除第一行填编号外，其余数据全部填0");
	else if (p == 29)
		edit2.SetWindowTextW(L"pl版专用号");
	else if (p == 30)
		edit2.SetWindowTextW(L"0\r\n一二 & 三四五 & 六七八九\r\n58, 0, 1, 22, 1, 1, 1, 1\r\n68, 166\r\n第一行: R文字编号，取值范围  0-39 \r\n第二行: 文字或指针变量（指针变量应是一段文字的地址），文字部分可用 & 来表示换行， & 要占用1字节\r\n第三行 : 依次为 文字颜色, 描边颜色, 字体, 字号, 粗体, 斜体, 下画, 鼠标悬停时是否变色\r\n第四行 : X  Y （直角坐标）\r\n取消时编号要相同，其余全部填0 ");
	else if (p == 31)
		edit2.SetWindowTextW(L"1,2,14,11\r\n表示要显示部分的 X,Y,W,H，单位为格子\r\n要显示全部战场就全填0 ");
	else if (p == 32)
		edit2.SetWindowTextW(L"39,-1,1,1\r\n1  要操作的动图编号，对应载入动图的编号，二者要相同\r\n2  调整这个图的延时（取值 - 1至255），填 - 1是让指定的动图立即停止播放，也可以和载入时相同，表示不改\r\n3  填写一个数字m，当第二个数字填了 - 1时，就停在这一个显示次数上，填 - 1表示不改\r\n如 填 39, -1, -1, -1 表示停止在当前次数\r\n如 填 39, -1, m, -1 表示停止在指定次数\r\n4  填写一个数字n，让动图只在第m次到第n次之间播放(m应 <= n, 且m和n均不得大于动图的最大显示次数，否则报错)，填 - 1表示不改\r\n如  填 39, 5, 1, 5 动图在第一次显示到第五次显示之间播放，延时5毫秒\r\n使用这个指令让动图停止时，会把当前显示次数以byte型保存在整形变量4038中，以便进一步判断剧情走向");
	else if (p == 33)
		edit2.SetWindowTextW(L"2,16|5,17\r\n1\r\n第一行是一个区域的左上和右下的格子坐标(如果相同则表示一个点) \r\n第二行0 取消可视  1 设为可视");

}

void Dialog_114::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	edit2.GetWindowTextW(str);
	for (int i = 0; i < str.GetLength(); i++)
	{
		if (str[i] == '\r') {
			str.Delete(i);
			i++;
		}
	}
	if (!StoreLongCharDataOrReport(data, str))
		return;

	data->int_data[0] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE); CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_115, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_115::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_115::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < 5375; i++)combo1.AddString(pView->per2[i]);
	combo1.EnablePersonSearch();
	int code = pView->cur_data.int_data[0];
	combo1.SetCurSel(Per2Code2List(code));

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare_sum; i++)combo2.AddString(pView->compare[i]);
	combo2.SetCurSel(data->int_data[2]);

	check1.SetCheck(data->int_data[3]);
	check2.SetCheck(data->int_data[4]);
	check3.SetCheck(data->int_data[5]);
	check4.SetCheck(data->int_data[6]);
	check5.SetCheck(data->int_data[7]);
	return 0;
}

void Dialog_115::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int list = combo1.GetCurSel();
	data->int_data[0] = Per2List2Code(list);

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	data->int_data[2] = combo2.GetCurSel();

	data->int_data[3] = check1.GetCheck();
	data->int_data[4] = check2.GetCheck();
	data->int_data[5] = check3.GetCheck();
	data->int_data[6] = check4.GetCheck();
	data->int_data[7] = check5.GetCheck();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_119, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_119::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_119::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < pView->var_kind_sum; i++)combo1.AddString(pView->var_kind[i]);
	combo1.SetCurSel(data->int_data[0]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->operate2_sum; i++)combo2.AddString(pView->operate2[i]);
	combo2.SetCurSel(data->int_data[2]);
	for (int i = 0; i < pView->var_kind2_sum; i++)combo3.AddString(pView->var_kind2[i]);
	combo3.SetCurSel(data->int_data[3]);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);
	return 0;
}

void Dialog_119::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	data->int_data[2] = combo2.GetCurSel();
	data->int_data[3] = combo3.GetCurSel();

	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_120, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_120::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_120::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
	edit1.SetWindowTextW(show);

	combo1.SetCurSel(data->int_data[1]);

	for (int i = 0; i < 5375; i++)combo2.AddString(pView->per2[i]);
	combo2.EnablePersonSearch();
	int code = pView->cur_data.int_data[2];
	combo2.SetCurSel(Per2Code2List(code));

	for (int i = 0; i < pView->all_condition_sum; i++)combo3.AddString(pView->all_condition[i]);
	combo3.SetCurSel(data->int_data[3]);

	return 0;
}

void Dialog_120::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);


	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[0] = num;

	data->int_data[1] = combo1.GetCurSel();

	int list = combo2.GetCurSel();
	data->int_data[2] = Per2List2Code(list);

	data->int_data[3] = combo3.GetCurSel();

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_121, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_121::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_121::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	for (int i = 0; i < pView->var_kind2_sum; i++)combo1.AddString(pView->var_kind2[i]);
	combo1.SetCurSel(data->int_data[0]);

	wchar_t show[100];
	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
	edit1.SetWindowTextW(show);

	for (int i = 0; i < pView->compare2_sum; i++)combo2.AddString(pView->compare2[i]);
	combo2.SetCurSel(data->int_data[2]);
	for (int i = 0; i < pView->var_kind2_sum; i++)combo3.AddString(pView->var_kind2[i]);
	combo3.SetCurSel(data->int_data[3]);

	wcscpy_s(show, L"");
	wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
	edit2.SetWindowTextW(show);
	return 0;
}

void Dialog_121::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	data->int_data[0] = combo1.GetCurSel();

	CString str; int num = 0;
	edit1.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[1] = num;

	data->int_data[2] = combo2.GetCurSel();
	data->int_data[3] = combo3.GetCurSel();

	edit2.GetWindowTextW(str);
	num = CString2Int(str);
	data->int_data[4] = num;

	pView->UpdateShow(pView->cur_item);
	pView->GetDocument()->OnModified(TRUE);CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_Dup, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_Dup::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_Dup::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	edit1.SetWindowTextW(L"0");
	edit2.SetWindowTextW(L"0");
	edit3.SetWindowTextW(L"0");
	return 0;
}

void Dialog_Dup::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	int res[3];

	CString str;
	edit1.GetWindowTextW(str);
	res[0] = CString2Int(str);
	edit2.GetWindowTextW(str);
	res[1] = CString2Int(str);
	edit3.GetWindowTextW(str);
	res[2] = CString2Int(str);

	if (res[0] > 100)res[0] = 100;
	if (res[1] > 50 || res[1] < 0)res[1] = 0;

	HTREEITEM tmp = pView->cur_item;
	for (int i = 0; i < res[0]; i++) {
		tmp = pView->CreateItem(data->id, m_TreeCtrl.GetParentItem(tmp), tmp);
		ItemData* tmp_data = (ItemData*)m_TreeCtrl.GetItemData(tmp);
		for (int j = 0; j < 20; j++)
			tmp_data->int_data[j] = data->int_data[j];
		tmp_data->int_data[res[1]] = data->int_data[res[1]] + res[2] * (i + 1);
		pView->UpdateShow(tmp);
	}

	pView->cur_item = tmp;
	m_TreeCtrl.SelectItem(tmp);

	pView->GetDocument()->OnModified(TRUE); CDialogEx::OnOK();
}


BEGIN_MESSAGE_MAP(Dialog_Edit, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_Edit::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_Edit::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(pView->code[pView->cur_code]);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);

	edit1.SetWindowTextW(L"0");
	edit2.SetWindowTextW(L"0");
	return 0;
}

void Dialog_Edit::OnBnClickedOk()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	int res[2];
	CString str;
	edit1.GetWindowTextW(str);
	res[0] = CString2Int(str);
	edit2.GetWindowTextW(str);
	res[1] = CString2Int(str);

	int size = pView->checkbox_selected.size();
	ItemData* data;
	for (int i = 0; i < size; i++) {
		data = (ItemData*)m_TreeCtrl.GetItemData(pView->checkbox_selected[i]);
		data->int_data[res[0]] = res[1];
		pView->UpdateShow(pView->checkbox_selected[i]);
	}

	pView->GetDocument()->OnModified(TRUE); CDialogEx::OnOK();
}

BEGIN_MESSAGE_MAP(Dialog_Var, myDialog)
	ON_LBN_DBLCLK(IDC_LIST1, &Dialog_Var::OnDClickList1)
	ON_LBN_DBLCLK(IDC_LIST2, &Dialog_Var::OnDClickList2)
	ON_BN_CLICKED(IDC_BUTTON1, &Dialog_Var::OnBnClickedOk1)
	ON_BN_CLICKED(IDC_BUTTON2, &Dialog_Var::OnBnClickedOk2)
END_MESSAGE_MAP()

void Dialog_Var::recur_SearchVar(HTREEITEM cur, CcczEditor2View* pView)
{
	CTreeCtrl& tree = pView->GetTreeCtrl();
	ItemData* data = (ItemData*)tree.GetItemData(cur);
	if (data) {
		if (data->id == 5) 
		{
			for (int i = 0; i < 50;) 
			{
				int var = data->int_data[i];
				if (var == -1) {
					if (i < 25) { i = 25; continue; }
					else break;
				}
				wchar_t show[60];
				wcscpy_s(show, L"布尔");
				if (var < 10) wcscat_s(show, L"      ");
				else if (var < 100) wcscat_s(show, L"    ");
				else if (var < 1000) wcscat_s(show, L"  ");
				wcscat_s(show, std::to_wstring(var).c_str());
				for (int i = 0; i < 4; i++) wcscat_s(show, L" ");
				wcscat_s(show, L"测试 ");
				wcscat_s(show, i < 25 ? L"true" : L"false");
				for (int i = wcslen(show); i < 50; i++) show[i] = ' ';
				show[50] = (unsigned int)cur % 256;
				show[51] = (unsigned int)cur / 256 % 256;
				show[52] = (unsigned int)cur / 256 / 256 % 256;
				show[53] = (unsigned int)cur / 256 / 256 / 256 % 256;
				show[54] = 0;
				list1.AddString(show);
				i++;
			}
		}
		if (data->id == 11)
		{
			int var = data->int_data[0];
			wchar_t show[60];
			wcscpy_s(show, L"布尔");
			if (var < 10) wcscat_s(show, L"      ");
			else if (var < 100) wcscat_s(show, L"    ");
			else if (var < 1000) wcscat_s(show, L"  ");
			wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
			for (int i = 0; i < 4; i++) wcscat_s(show, L" ");
			wcscat_s(show, L"赋值 ");
			wcscat_s(show, data->int_data[1] ? L"true" : L"false");
			for (int i = wcslen(show); i < 50; i++) show[i] = ' ';
			show[50] = (unsigned int)cur % 256;
			show[51] = (unsigned int)cur / 256 % 256;
			show[52] = (unsigned int)cur / 256 / 256 % 256;
			show[53] = (unsigned int)cur / 256 / 256 / 256 % 256;
			show[54] = 0;
			list1.AddString(show);
		}
		if (data->id == 0x77)
		{
			if (data->int_data[0] != 0) {
				wchar_t show[60];
				wcscpy_s(show, data->int_data[0] == 1 ? L"指针" : L"整型");
				int var = data->int_data[1];
				if (var < 10) wcscat_s(show, L"      ");
				else if (var < 100) wcscat_s(show, L"    ");
				else if (var < 1000) wcscat_s(show, L"  ");
				wcscat_s(show, std::to_wstring(data->int_data[1]).c_str());
				for (int i = 0; i < 4; i++) wcscat_s(show, L" ");
				wcscat_s(show, pView->operate2[data->int_data[2]]);
				if (data->int_data[2] == 2)wcscat_s(show, L" ");
				wcscat_s(show, L"  ");
				wcscat_s(show, pView->var_kind2[data->int_data[3]]);
				wcscat_s(show, L" ");
				wcscat_s(show, std::to_wstring(data->int_data[4]).c_str());
				for (int i = wcslen(show); i < 50; i++) show[i] = ' ';
				show[50] = (unsigned int)cur % 256;
				show[51] = (unsigned int)cur / 256 % 256;
				show[52] = (unsigned int)cur / 256 / 256 % 256;
				show[53] = (unsigned int)cur / 256 / 256 / 256 % 256;
				show[54] = 0;
				list2.AddString(show);
			}
		}
	}

	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL) {
		recur_SearchVar(child, pView);
	}
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL) {
		recur_SearchVar(bro, pView);
	}
}

BOOL Dialog_Var::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	SetWindowTextW(L"变量赋值列表");
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	
	recur_SearchVar(m_TreeCtrl.GetRootItem(), pView);

	if (list1.GetCount() > 0) list1.SetCurSel(0);
	else button1.EnableWindow(FALSE);
	if (list2.GetCount() > 0) list2.SetCurSel(0);
	else button2.EnableWindow(FALSE);
	return 0;
}

void Dialog_Var::OnDClickList1()
{
	OnBnClickedOk1();
}
void Dialog_Var::OnDClickList2()
{
	OnBnClickedOk2();
}

void Dialog_Var::OnBnClickedOk1()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	int sel = list1.GetCurSel();
	if (sel >= 0) {
		wchar_t res[60];
		list1.GetText(sel, res);
		unsigned int addr = 0;
		unsigned int quan = 1;
		for (int i = 0; i < 4; i++) {
			addr += (unsigned char)res[i + 50] * quan;
			quan *= 256;
		}
		try {
			HTREEITEM goal = (HTREEITEM)addr;
			m_TreeCtrl.SelectItem(goal);
			pView->cur_item = goal;
			CMainFrame* pFrame = (CMainFrame*)AfxGetMainWnd();
			wchar_t show[30];
			wcscpy_s(show, L"id:");
			ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
			wcscat_s(show, std::to_wstring(data->ord).c_str());
			pFrame->m_wndStatusBar.SetPaneText(0, show);
		}
		catch (CException* e)
		{
			
		}
	}
	CDialogEx::OnOK();
}

void Dialog_Var::OnBnClickedOk2()
{
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	int sel = list2.GetCurSel();
	if (sel >= 0) {
		wchar_t res[60];
		list2.GetText(sel, res);
		unsigned int addr = 0;
		unsigned int quan = 1;
		for (int i = 0; i < 4; i++) {
			addr += (unsigned char)res[i + 50] * quan;
			quan *= 256;
		}
		HTREEITEM goal = (HTREEITEM)addr;
		m_TreeCtrl.SelectItem(goal);
		pView->cur_item = goal;
		CMainFrame* pFrame = (CMainFrame*)AfxGetMainWnd();
		wchar_t show[30];
		wcscpy_s(show, L"id:");
		ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->cur_item);
		wcscat_s(show, std::to_wstring(data->ord).c_str());
		pFrame->m_wndStatusBar.SetPaneText(0, show);
	}
	CDialogEx::OnOK();
}



BEGIN_MESSAGE_MAP(Dialog_Color, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_Color::OnBnClickedOk)
END_MESSAGE_MAP()


BOOL Dialog_Color::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	for (int i = 0; i < 174; i++) {
		button[i].Create(L"", WS_CHILD | BS_DEFPUSHBUTTON, CRect(10 + i % 16 * 30, 10 + i / 16 * 30, 30, 30), this, 5000 + i);
	}

	return 0;
}

void Dialog_Color::OnBnClickedOk()
{

}


void recur_copy(HTREEITEM cur, ItemData* des, CcczEditor2View* pView, int depth)
{
	CTreeCtrl& tree = pView->GetTreeCtrl();
	ItemData* p = (ItemData*)tree.GetItemData(cur);
	if (p != NULL) {
		des->id = p->id;
		for (int i = 0; i < p->int_data.size(); i++) des->int_data[i] = p->int_data[i];
		if (p->long_char_data != NULL) {
			const std::size_t sourceCapacity = (std::max)(
				ccz::editor::kLongCharDataCapacity,
				p->long_char_data_capacity);
			ccz::editor::ReplaceLongCharData(
				des->long_char_data,
				des->long_char_data_capacity,
				p->long_char_data,
				sourceCapacity);
		}
	}
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL) {
		des->child = pView->InitData(((ItemData*)tree.GetItemData(child))->id);
		recur_copy(child, des->child, pView, depth + 1);
	}
	else des->child = NULL;
	if (depth) {
		HTREEITEM bro = tree.GetNextSiblingItem(cur);
		if (bro != NULL) {
			des->bro = pView->InitData(((ItemData*)tree.GetItemData(bro))->id);
			recur_copy(bro, des->bro, pView, depth + 1);
		}
	}
	else des->bro = NULL;
}

void CcczEditor2View::OnCopyMsg() {
	OnCopy();
}

bool CcczEditor2View::OnCopy()
{
	// TODO: 在此添加命令处理程序代码
	CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
	CcczEditor2Doc* pDoc = pView->GetDocument();
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	if (theApp.condition == 0) theApp.deleteCopy(theApp.copy);
	else for (int i = 0; i < theApp.copys_sum; i++)theApp.deleteCopy(theApp.copys[i]);
	theApp.copys_sum = 0;
	if (!checkbox) {
		theApp.condition = 0;
		if ((ItemData*)m_TreeCtrl.GetItemData(pView->cur_item) == NULL)return false;
		int id = ((ItemData*)m_TreeCtrl.GetItemData(pView->cur_item))->id;
		if (id == 0 || id == 1) {
			MessageBox(TEXT("不可以复制这个指令"), TEXT("创建失败"), MB_ICONERROR);
			theApp.copy = NULL;
			ctrl_on = false;
			return false;
		}
		theApp.copy = InitData(id);
		recur_copy(pView->cur_item, theApp.copy, pView, 0);
	}
	else {
		theApp.condition = 1;
		if (!checkCheckbox()) {
			MessageBox(TEXT("多选的对象非同级对象！"), TEXT("创建失败"), MB_ICONERROR);
			theApp.copy = NULL;
			ctrl_on = false;
			return false;
		}
		theApp.copys_sum = checkbox_selected.size();
		for (int i = 0; i < checkbox_selected.size(); i++) {
			HTREEITEM item = checkbox_selected[i];
			int id = ((ItemData*)m_TreeCtrl.GetItemData(item))->id;
			theApp.copys[i] = InitData(id);
			recur_copy(item, theApp.copys[i], pView, 0);
		}
		//refreshCheckbox(m_TreeCtrl.GetRootItem(), -1);
	}
	return true;
}

void recur_paste(HTREEITEM cur, ItemData* src, CcczEditor2View* pView)
{
	CTreeCtrl& tree = pView->GetTreeCtrl();

	ItemData* p = (ItemData*)tree.GetItemData(cur);
	for (int i = 0; i < src->int_data.size(); i++) p->int_data[i] = src->int_data[i];
	if (src->long_char_data != NULL) {
		const std::size_t sourceCapacity = (std::max)(
			ccz::editor::kLongCharDataCapacity,
			src->long_char_data_capacity);
		ccz::editor::ReplaceLongCharData(
			p->long_char_data,
			p->long_char_data_capacity,
			src->long_char_data,
			sourceCapacity);
	}
	pView->UpdateShow(cur);

	if (src->child != NULL)
	{
		HTREEITEM item = pView->CreateItem(src->child->id, cur, TVI_FIRST);
		recur_paste(item, src->child, pView);
	}
	if (src->bro != NULL)
	{
		HTREEITEM item = pView->CreateItem(src->bro->id, tree.GetParentItem(cur), TVI_LAST);
		recur_paste(item, src->bro, pView);
	}
}

void CcczEditor2View::OnPaste()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();

	if (theApp.condition == 0) {
		if (theApp.copy == NULL)return;
		ItemData* cur_item_data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
		if (theApp.copy->id < 0 && theApp.copy->id != cur_item_data->id) {
			MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
			ctrl_on = false;
			return;
		}
		bool addZSJ = false;
		if (m_TreeCtrl.GetItemText(m_TreeCtrl.GetParentItem(cur_item))[0] == 'S') {
			if (theApp.copy->id >= 0)
				if (code_test[theApp.copy->id] < 2 && theApp.copy->id != 0x77 && theApp.copy->id != 0x78) {
					MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
					ctrl_on = false;
					return;
				}
		}
		else if (m_TreeCtrl.GetItemText(cur_item)[0] == 'S' && theApp.copy->id > 0) {
			MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
			ctrl_on = false;
			return;
		}
		else if (theApp.copy->child != NULL && theApp.copy->id > 0) addZSJ = true;

		HTREEITEM item;
		HTREEITEM last_item = FindLast(cur_item);
		if (last_item != TVI_FIRST) {
			ItemData* last_data = (ItemData*)m_TreeCtrl.GetItemData(last_item);
			if (last_data->id == 1) {
				item = CreateItem(theApp.copy->id, m_TreeCtrl.GetParentItem(cur_item), FindLast(last_item));
				if (addZSJ)CreateItem(1, m_TreeCtrl.GetParentItem(cur_item), FindLast(item));
				recur_paste(item, theApp.copy, this);
			}
			else {
				item = CreateItem(theApp.copy->id, m_TreeCtrl.GetParentItem(cur_item), last_item);
				if (addZSJ)CreateItem(1, m_TreeCtrl.GetParentItem(cur_item), FindLast(item));
				recur_paste(item, theApp.copy, this);
			}
		}
		else {
			item = CreateItem(theApp.copy->id, m_TreeCtrl.GetParentItem(cur_item), last_item);
			if (addZSJ)CreateItem(1, m_TreeCtrl.GetParentItem(cur_item), FindLast(item));
			recur_paste(item, theApp.copy, this);
		}
		cur_item = item;
		m_TreeCtrl.SelectItem(item);
	}
	else {
		if (theApp.copys_sum == 0)return;
		for (int i = 0; i < theApp.copys_sum; i++) {
			ItemData* cur_item_data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
			if (theApp.copys[i]->id < 0 && theApp.copys[i]->id != cur_item_data->id) {
				MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
				ctrl_on = false;
				return;
			}
			else if (m_TreeCtrl.GetItemText(cur_item)[0] == 'S' && theApp.copys[i]->id > 0) {
				MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
				ctrl_on = false;
				return;
			}
			if (m_TreeCtrl.GetChildItem(cur_item) != NULL && cur_item_data->id > 0) {
				MessageBox(TEXT("不可以粘贴到子事件设定的后方"), TEXT("粘贴失败"), MB_ICONERROR);
				ctrl_on = false;
				return;
			}
			if (m_TreeCtrl.GetItemText(m_TreeCtrl.GetParentItem(cur_item))[0] == 'S') {
				if (theApp.copys[i]->id >= 0)
					if (code_test[theApp.copys[i]->id] < 2) {
						MessageBox(TEXT("粘贴目标不匹配"), TEXT("粘贴失败"), MB_ICONERROR);
						ctrl_on = false;
						return;
					}
			}
			else if (theApp.copys[i]->child != NULL && theApp.copys[i]->id > 0) CreateItem(1, m_TreeCtrl.GetParentItem(cur_item), FindLast(cur_item));
			HTREEITEM item = CreateItem(theApp.copys[i]->id, m_TreeCtrl.GetParentItem(cur_item), FindLast(cur_item));
			recur_paste(FindLast(cur_item), theApp.copys[i], this);
			refreshCheckbox(m_TreeCtrl.GetRootItem(), -1);
		}
	}
	GetDocument()->OnModified(TRUE);
}

void CcczEditor2View::OnCut()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	if (OnCopy()) {
		OnEditDelete();
		if (checkbox)
			refreshCheckbox(m_TreeCtrl.GetRootItem(), -1);
	}
}

void CcczEditor2View::refreshCheckbox(HTREEITEM cur, int kind)
{
	CTreeCtrl& tree = GetTreeCtrl();
	if (kind == 0) tree.SetItemState(cur, INDEXTOSTATEIMAGEMASK(0), TVIS_STATEIMAGEMASK);
	else if (kind == -1)tree.SetCheck(cur, FALSE);
	else {
		if (tree.GetCheck(cur)) {
			ItemData* data = (ItemData*)tree.GetItemData(cur);
			if (data != NULL)
				if (data->id != 1 && data->id != 0)checkbox_selected.push_back(cur);
		}
	}
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL)
		refreshCheckbox(child, kind);
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		refreshCheckbox(bro, kind);
}

bool CcczEditor2View::checkCheckbox() {
	CTreeCtrl& tree = GetTreeCtrl();
	checkbox_selected.clear();
	refreshCheckbox(tree.GetRootItem(), 1);
	int size = checkbox_selected.size();
	if (size <= 1)return true;
	for (int i = 1; i < checkbox_selected.size(); i++) {
		if (tree.GetParentItem(checkbox_selected[0]) != tree.GetParentItem(checkbox_selected[i]))
			return false;
	}
	return true;
}

void CcczEditor2View::recurExpand(HTREEITEM cur)
{
	CTreeCtrl& tree = GetTreeCtrl();
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL)
		recurExpand(child);
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		recurExpand(bro);
	tree.Expand(cur, TVE_EXPAND);
}

void CcczEditor2View::OnExpand()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	//recurExpand(m_TreeCtrl.GetRootItem());
	if (m_TreeCtrl.GetChildItem(cur_item) != NULL)
		recurExpand(m_TreeCtrl.GetChildItem(cur_item));
}

HTREEITEM CcczEditor2View::recurSearchOrd(HTREEITEM cur, int ord)
{
	CTreeCtrl& tree = GetTreeCtrl();
	ItemData* data = (ItemData*)tree.GetItemData(cur);
	if (data)
		if (data->ord == ord)return cur;

	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL) {
		HTREEITEM p = recurSearchOrd(child, ord);
		if (p)return p;
	}
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL) {
		HTREEITEM q = recurSearchOrd(bro, ord);
		if (q)return q;
	}
	return NULL;
}

void CcczEditor2View::OnJump()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	if (data->id == 0x76) {
		HTREEITEM res = recurSearchOrd(m_TreeCtrl.GetRootItem(), data->int_data[0]);
		if (res) {
			m_TreeCtrl.SelectItem(res);
			cur_item = res;
			CMainFrame* pFrame = (CMainFrame*)AfxGetMainWnd();
			wchar_t show[30];
			wcscpy_s(show, L"id:");
			wcscat_s(show, std::to_wstring(data->int_data[0]).c_str());
			pFrame->m_wndStatusBar.SetPaneText(0, show);
		}
	}
}

HTREEITEM CcczEditor2View::recurSearchItem(HTREEITEM cur, HTREEITEM ddl, bool &ddly, int id)
{
	CTreeCtrl& tree = GetTreeCtrl();
	ItemData* data = (ItemData*)tree.GetItemData(cur);
	if (data)
		if (data->id == id && ddly == true) {
			return cur;
		}
	if (cur == ddl)ddly = true;

	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL) {
		HTREEITEM p = recurSearchItem(child, ddl, ddly, id);
		if (p)return p;
	}
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL) {
		HTREEITEM q = recurSearchItem(bro, ddl, ddly, id);
		if (q)return q;
	}
	return NULL;
}

void CcczEditor2View::OnSearchItem()
{
	theApp.search = true;
	Dialog_SelectCode d;
	d.DoModal();
	theApp.search = false;
	OnSearchItemNext();
}

void CcczEditor2View::OnSearchItemNext()
{
	// TODO: 在此添加命令处理程序代码
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	//if (cur_item == m_TreeCtrl.GetRootItem())return;
	bool ddly = false;
	HTREEITEM res = recurSearchItem(m_TreeCtrl.GetRootItem(), cur_item, ddly, theApp.search_goal);
	if (res) {
		m_TreeCtrl.SelectItem(res);
		cur_item = res;
		CMainFrame* pFrame = (CMainFrame*)AfxGetMainWnd();
		wchar_t show[30];
		wcscpy_s(show, L"id:");
		wcscat_s(show, std::to_wstring(theApp.search_goal).c_str());
		pFrame->m_wndStatusBar.SetPaneText(0, show);
	}
}


void CcczEditor2View::OnVarList()
{
	Dialog_Var dialog;
	dialog.DoModal();
}


void CcczEditor2View::OnDropFiles(HDROP hDropInfo)
{
	wchar_t szPath[MAX_PATH] = { 0 };
	int cnt = DragQueryFile(hDropInfo, -1, szPath, MAX_PATH);

	for (int i = 0; i < cnt; i++) {
		DragQueryFile(hDropInfo, i, szPath, MAX_PATH);
		AfxGetApp()->OpenDocumentFile(szPath);
	}

	CTreeView::OnDropFiles(hDropInfo);
}


void recur_move(HTREEITEM cur, HTREEITEM src, CcczEditor2View* pView, int depth)
{
	CTreeCtrl& tree = pView->GetTreeCtrl();

	ItemData* p = (ItemData*)tree.GetItemData(cur);
	ItemData* data_src = (ItemData*)tree.GetItemData(src);
	for (int i = 0; i < data_src->int_data.size(); i++) p->int_data[i] = data_src->int_data[i];
	p->ord = data_src->ord;
	if (data_src->long_char_data != NULL) {
		const std::size_t sourceCapacity = (std::max)(
			ccz::editor::kLongCharDataCapacity,
			data_src->long_char_data_capacity);
		ccz::editor::ReplaceLongCharData(
			p->long_char_data,
			p->long_char_data_capacity,
			data_src->long_char_data,
			sourceCapacity);
	}
	pView->UpdateShow(cur);

	if (tree.GetChildItem(src) != NULL)
	{
		ItemData* child_src = (ItemData*)tree.GetItemData(tree.GetChildItem(src));
		HTREEITEM item = pView->CreateItem(child_src->id, cur, TVI_FIRST);
		pView->cur_code_ord--;
		recur_move(item, tree.GetChildItem(src), pView, depth + 1);
	}
	if (tree.GetNextSiblingItem(src) != NULL && depth != 0)
	{
		ItemData* bro_src = (ItemData*)tree.GetItemData(tree.GetNextSiblingItem(src));
		HTREEITEM item = pView->CreateItem(bro_src->id, tree.GetParentItem(cur), TVI_LAST);
		pView->cur_code_ord--;
		recur_move(item, tree.GetNextSiblingItem(src), pView, depth);
	}
}

void CcczEditor2View::OnMoveUp()
{
	if (checkbox) {
		MessageBox(TEXT("多选模式暂不支持移动代码"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	if (!data)return;
	if (data->id == 0 || data->id == 1) {
		MessageBox(TEXT("该指令不允许移动"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	ItemData* datap = (ItemData*)m_TreeCtrl.GetItemData(m_TreeCtrl.GetParentItem(cur_item));
	if (datap != NULL)
		if (datap->id == -2) {
			MessageBox(TEXT("头部的指令不支持移动"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
			return;
		}
	HTREEITEM last1 = FindLast(cur_item);
	if (last1 == TVI_FIRST) {
		MessageBox(TEXT("已经到顶部了，不能再上移了"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	if (m_TreeCtrl.GetChildItem(cur_item) && data->id > 0) {
		last1 = FindLast(last1);
		if (last1 == TVI_FIRST) {
			MessageBox(TEXT("已经到顶部了，不能再上移了"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
			return;
		}
	}
	HTREEITEM last2 = FindLast(last1);
	if (last2 != TVI_FIRST) {
		if (m_TreeCtrl.GetChildItem(last1) && data->id > 0)last2 = FindLast(last2);
	}

	HTREEITEM item = CreateItem(data->id, m_TreeCtrl.GetParentItem(cur_item), last2);
	cur_code_ord--;
	recur_move(item, cur_item, this, 0);
	OnEditDelete();
	data = (ItemData*)m_TreeCtrl.GetItemData(item);
	if (m_TreeCtrl.GetChildItem(item) && data->id > 0) {
		HTREEITEM zsj_item = CreateItem(1, m_TreeCtrl.GetParentItem(item), FindLast(item));
		((ItemData*)m_TreeCtrl.GetItemData(zsj_item))->ord = data->ord - 1;
		cur_code_ord--;
	}
	cur_item = item;
	m_TreeCtrl.SelectItem(cur_item);
}


void CcczEditor2View::OnMoveDown()
{
	if (checkbox) {
		MessageBox(TEXT("多选模式暂不支持移动代码"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur_item);
	if (!data)return;
	if (data->id == 0 || data->id == 1) {
		MessageBox(TEXT("该指令不允许移动"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	ItemData* datap = (ItemData*)m_TreeCtrl.GetItemData(m_TreeCtrl.GetParentItem(cur_item));
	if (datap != NULL)
		if (datap->id == -2) {
			MessageBox(TEXT("头部的指令不支持移动"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
			return;
		}
	HTREEITEM next1 = m_TreeCtrl.GetNextSiblingItem(cur_item);
	if (next1 == NULL) {
		MessageBox(TEXT("已经到尾部了，不能再下移了"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	ItemData* datan = (ItemData*)m_TreeCtrl.GetItemData(next1);
	if (datan->id == 0) {
		MessageBox(TEXT("已经到尾部了，不能再下移了"), TEXT("移动失败"), MB_ICONERROR); ctrl_on = false;
		return;
	}
	ItemData* data2 = (ItemData*)m_TreeCtrl.GetItemData(next1);
	if (data2->id == 1)
		next1 = m_TreeCtrl.GetNextSiblingItem(next1);
	
	HTREEITEM item = CreateItem(data->id, m_TreeCtrl.GetParentItem(cur_item), next1);
	cur_code_ord--;
	recur_move(item, cur_item, this, 0);
	OnEditDelete();
	data = (ItemData*)m_TreeCtrl.GetItemData(item);
	if (m_TreeCtrl.GetChildItem(item) && data->id > 0) {
		HTREEITEM zsj_item = CreateItem(1, m_TreeCtrl.GetParentItem(item), FindLast(item));
		((ItemData*)m_TreeCtrl.GetItemData(zsj_item))->ord = data->ord - 1;
		cur_code_ord--;
	}
	cur_item = item;
	m_TreeCtrl.SelectItem(cur_item);
}


void CcczEditor2View::OnEditDuplicate()
{
	CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
	ItemData* data = (ItemData*)GetTreeCtrl().GetItemData(cur_item);
	if (data == NULL)return;
	int id = data->id;
	if (id == 0x46 || id == 0x47 || id == 5 || id == 0 || id == 1 || id == 2 || m_TreeCtrl.GetChildItem(cur_item) != NULL) {
		MessageBox(TEXT("该指令暂不支持步进复制"), TEXT("批量复制失败"), MB_ICONERROR);
		return;
	}
	Dialog_Dup dialog;
	dialog.DoModal();
}

void CcczEditor2View::OnEditBatch()
{
	CTreeCtrl& tree = GetTreeCtrl();
	if (checkbox == false) {
		MessageBox(TEXT("只有多选模式下才能使用批量编辑"), TEXT("批量编辑失败"), MB_ICONERROR);
		return;
	}
	checkbox_selected.clear();
	refreshCheckbox(tree.GetRootItem(), 1);
	int size = checkbox_selected.size();
	if (size == 0) {
		MessageBox(TEXT("请至少选择一个指令"), TEXT("批量编辑失败"), MB_ICONERROR);
		return;
	}
	ItemData* data = (ItemData*)GetTreeCtrl().GetItemData(checkbox_selected[0]);
	if (size > 1) {
		for (int i = 1; i < size; i++) {
			ItemData* datab = (ItemData*)GetTreeCtrl().GetItemData(checkbox_selected[1]);
			if (data->id != datab->id) {
				MessageBox(TEXT("多选的指令并非同类型"), TEXT("批量编辑失败"), MB_ICONERROR);
				return;
			}
		}
	}
	int id = data->id;
	if (id <= 1 || id == 0x46 || id == 0x47 || id == 5) {
		MessageBox(TEXT("该指令暂不支持批量编辑"), TEXT("批量编辑失败"), MB_ICONERROR);
		return;
	}
	Dialog_Edit dialog;
	dialog.DoModal();
}
void CcczEditor2View::SelectPlaybackItem(HTREEITEM item,
	bool suppress_preview_refresh)
{
	CTreeCtrl& tree = GetTreeCtrl();
	if (item == nullptr || tree.GetSafeHwnd() == nullptr ||
		!::IsWindow(tree.GetSafeHwnd())) {
		return;
	}
	TVITEM itemInfo{};
	itemInfo.mask = TVIF_HANDLE;
	itemInfo.hItem = item;
	if (!TreeView_GetItem(tree.GetSafeHwnd(), &itemInfo)) return;
	if (tree.GetSelectedItem() == item) {
		cur_item = item;
		return;
	}
	m_suppressPreviewRefreshOnce = suppress_preview_refresh;
	tree.SelectItem(item);
	cur_item = item;
	// MFC normally dispatches TVN_SELCHANGED synchronously from SelectItem.
	// Clear the guard here as a safety net for tree implementations that do
	// not emit the notification (or emit it after the call returns).
	m_suppressPreviewRefreshOnce = false;
}

void CcczEditor2View::OnTvnSelchanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	const bool suppress_preview_refresh = m_suppressPreviewRefreshOnce;
	m_suppressPreviewRefreshOnce = false;
	cur_item = GetTreeCtrl().GetSelectedItem();
	ItemData* data = (ItemData*)GetTreeCtrl().GetItemData(cur_item);
	if (data) {
		CMainFrame* pFrame = (CMainFrame*)AfxGetMainWnd();
		wchar_t show[30];
		wcscpy_s(show, L"id:");
		wcscat_s(show, std::to_wstring(data->ord).c_str());
		pFrame->m_wndStatusBar.SetPaneText(0, show);
	}
	// Root/document selections have no ItemData but are valid idle preview
	// input; always route selection changes through the single coordinator.
	if (!suppress_preview_refresh && !m_suppressBattleWritebackPreview) {
		if (!RefreshScenePreview())
			SelectPlaybackItem(pNMTreeView->itemOld.hItem, true);
	}
	*pResult = 0;
}

bool CcczEditor2View::RefreshScenePreview()
{
	CMainFrame* frame = static_cast<CMainFrame*>(AfxGetMainWnd());
	if (frame == nullptr) return true;
	cur_item = GetTreeCtrl().GetSelectedItem();
	const bool accepted = frame->m_wndScenePreview.RequestPreviewRefresh(
		GetTreeCtrl(), cur_item, *GetDocument());
	if (accepted) frame->m_wndScenePreview.InitCharList(per2);
	return accepted;
}

BOOL CcczEditor2View::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case 'S':
		{
			GetDocument()->DoFileSave(); ctrl_on = false;
			return TRUE;
		}
		case 'E':
		{
			if (ctrl_on) OnEditModify(); ctrl_on = false;
			return TRUE;
		}
		case 'I':
		{
			if (ctrl_on) OnEditAdd(); ctrl_on = false;
			return TRUE;
		}
		case 'O':
		{
			if (ctrl_on) OnEditAddi(); ctrl_on = false;
			return TRUE;
		}
		case 'D':
		{
			if (ctrl_on) OnEditDuplicate(); ctrl_on = false;
			return TRUE;
		}
		case 'R':
		{
			if (ctrl_on) OnEditBatch(); ctrl_on = false;
			return TRUE;
		}
		case VK_DELETE:
		{
			OnEditDelete();
			break;
		}
		case VK_UP:
		{
			fast_total = 0;
			if (ctrl_on) OnMoveUp();
			break;
		}
		case VK_DOWN:
		{
			fast_total = 0;
			if (ctrl_on) OnMoveDown();
			break;
		}
		case VK_LEFT:
		{
			GetTreeCtrl().Expand(cur_item, TVE_EXPAND);
			break;
		}
		case VK_RIGHT:
		{
			GetTreeCtrl().Expand(cur_item, TVE_COLLAPSE);
			break;
		}
		case 'X':
		{
			if (ctrl_on) OnCut(); //ctrl_on = false;
			break;
		}
		case 'C':
		{
			if (ctrl_on) OnCopy();
			break;
		}
		case 'V':
		{
			if (ctrl_on) OnPaste();
			break;
		}
		case 'Q':
		{
			if (ctrl_on) OnExpand();
			break;
		}
		case VK_SPACE:
		{
			CTreeCtrl& m_TreeCtrl = GetTreeCtrl();
			checkbox = !checkbox;
			if (checkbox) {
				m_TreeCtrl.ModifyStyle(0, TVS_CHECKBOXES);
				refreshCheckbox(m_TreeCtrl.GetRootItem(), 1);
			}
			else {
				m_TreeCtrl.ModifyStyle(TVS_CHECKBOXES, 0);
				refreshCheckbox(m_TreeCtrl.GetRootItem(), 0);
			}
			break;
		}
		case 'F':
		{
			if (ctrl_on) OnSearchItem(); ctrl_on = false;
			return TRUE;
		}
		case VK_F3:
		{
			OnSearchItemNext();
			break;
		}
		case 'L':
		{
			if (ctrl_on)OnVarList(); ctrl_on = false;
			return TRUE;
		}
		case 'A':
		{
			if (ctrl_on && checkbox) {
				CTreeCtrl& tree = GetTreeCtrl();
				checkbox_selected.clear();
				refreshCheckbox(tree.GetRootItem(), 1);
				int size = checkbox_selected.size();
				if (size == 2) {
					if (tree.GetParentItem(checkbox_selected[0]) == tree.GetParentItem(checkbox_selected[1]))
					{
						HTREEITEM tmp;
						for (tmp = checkbox_selected[0]; tmp != checkbox_selected[1]; tmp = tree.GetNextSiblingItem(tmp)) {
							tree.SetCheck(tmp, 1);
						}
					}
				}
			}
			break;
		}
		case VK_RETURN:
		{
			ctrl_on = false;
			OnEditModify();
			return TRUE;
		}
		case VK_CONTROL:
		{
			ctrl_on = true;
			return TRUE;
		}
		default:
			if (pMsg->wParam >= '0' && pMsg->wParam <= '9') {
				CTreeCtrl& tree = GetTreeCtrl();
				ItemData* data = (ItemData*)tree.GetItemData(cur_item);
				int id = data->id;
				if (code_instruct[id][0] == 4) {
					fast_total = fast_total * 10 + pMsg->wParam - '0';
					data->int_data[0] = fast_total;
					UpdateShow(cur_item);
					GetDocument()->MarkEditorContentChanged();
				}
			}
			else if (pMsg->wParam == VK_BACK) {
				CTreeCtrl& tree = GetTreeCtrl();
				ItemData* data = (ItemData*)tree.GetItemData(cur_item);
				int id = data->id;
				if (code_instruct[id][0] == 4) {
					fast_total = fast_total / 10;
					data->int_data[0] = fast_total;
					UpdateShow(cur_item);
					GetDocument()->MarkEditorContentChanged();
				}
			}
			else fast_total = 0;
			return TRUE;
		}
	}
	if (pMsg->message == WM_KEYUP)
	{
		switch (pMsg->wParam)
		{
			case VK_CONTROL:
			{
				ctrl_on = false;
				return TRUE;
			}
		}
	}
	return CTreeView::PreTranslateMessage(pMsg);
}

BOOL CcczEditor2View::OnEraseBkgnd(CDC* pDC)
{
	if (night_mode) {
		CRect   m_rt;
		GetClientRect(&m_rt);
		CBrush   brush;
		brush.CreateSolidBrush(m_bgcolor);
		pDC->FillRect(&m_rt, &brush);
		return TRUE;

	}
	else if(m_bBgLoaded){
		return TRUE;
	}
	return CTreeView::OnEraseBkgnd(pDC);
}
