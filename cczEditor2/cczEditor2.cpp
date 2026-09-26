
// cczEditor2.cpp: 定义应用程序的类行为。
//

#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "cczEditor2.h"
#include "MainFrm.h"

#include "ChildFrm.h"
#include "LongCharData.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#include <string>
#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")

static ULONG_PTR g_gdiplusToken = 0;


// CcczEditor2App

BEGIN_MESSAGE_MAP(CcczEditor2App, CWinAppEx)
	ON_COMMAND(ID_APP_ABOUT, &CcczEditor2App::OnAppAbout)
	// 基于文件的标准文档命令
	ON_COMMAND(ID_FILE_NEW, &CWinAppEx::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CcczEditor2App::OnFileOpen)
	// 标准打印设置命令
	ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
	ON_COMMAND(ID_SAVE_CUSTOM, &CcczEditor2App::OnSaveCustom)
END_MESSAGE_MAP()


// CcczEditor2App 构造

CcczEditor2App::CcczEditor2App() noexcept
{
	m_bHiColorIcons = TRUE;

	m_nAppLook = 0;
	// 支持重新启动管理器
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
#ifdef _MANAGED
	// 如果应用程序是利用公共语言运行时支持(/clr)构建的，则: 
	//     1) 必须有此附加设置，“重新启动管理器”支持才能正常工作。
	//     2) 在您的项目中，您必须按照生成顺序向 System.Windows.Forms 添加引用。
	System::Windows::Forms::Application::SetUnhandledExceptionMode(System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

	// TODO: 将以下应用程序 ID 字符串替换为唯一的 ID 字符串；建议的字符串格式
	//为 CompanyName.ProductName.SubProduct.VersionInformation
	SetAppID(_T("cczEditor2.AppID.NoVersion"));

	// TODO:  在此处添加构造代码，
	// 将所有重要的初始化放置在 InitInstance 中
}

// 唯一的 CcczEditor2App 对象

CcczEditor2App theApp;


// CcczEditor1App 初始化

BOOL CcczEditor2App::InitInstance()
{
	// 初始化GDI+
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

	// 如果一个运行在 Windows XP 上的应用程序清单指定要
	// 使用 ComCtl32.dll 版本 6 或更高版本来启用可视化方式，
	//则需要 InitCommonControlsEx()。  否则，将无法创建窗口。
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// 将它设置为包括所有要在应用程序中使用的
	// 公共控件类。
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinAppEx::InitInstance();


	// 初始化 OLE 库
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	EnableTaskbarInteraction();

	// 使用 RichEdit 控件需要 AfxInitRichEdit2()
	// AfxInitRichEdit2();

	// 标准初始化
	// 如果未使用这些功能并希望减小
	// 最终可执行文件的大小，则应移除下列
	// 不需要的特定初始化例程
	// 更改用于存储设置的注册表项
	// TODO: 应适当修改该字符串，
	// 例如修改为公司或组织名
	SetRegistryKey(_T("应用程序向导生成的本地应用程序"));
	LoadStdProfileSettings(4);  // 加载标准 INI 文件选项(包括 MRU)


	InitContextMenuManager();

	InitKeyboardManager();

	InitTooltipManager();
	CMFCToolTipInfo ttParams;
	ttParams.m_bVislManagerTheme = TRUE;
	theApp.GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
		RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

	// 注册应用程序的文档模板。  文档模板
	// 将用作文档、框架窗口和视图之间的连接
	CMultiDocTemplate* pDocTemplate;
	pDocTemplate = new CMultiDocTemplate(IDR_cczEditor2TYPE,
		RUNTIME_CLASS(CcczEditor2Doc),
		RUNTIME_CLASS(CChildFrame), // 自定义 MDI 子框架
		RUNTIME_CLASS(CcczEditor2View));
	if (!pDocTemplate)
		return FALSE;
	AddDocTemplate(pDocTemplate);

	// 创建主 MDI 框架窗口
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame || !pMainFrame->LoadFrame(IDR_MAINFRAME))
	{
		delete pMainFrame;
		return FALSE;
	}
	m_pMainWnd = pMainFrame;


	// 分析标准 shell 命令、DDE、打开文件操作的命令行
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);



	// 调度在命令行中指定的命令。  如果
	// 用 /RegServer、/Register、/Unregserver 或 /Unregister 启动应用程序，则返回 FALSE。
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;
	// 主窗口已初始化，因此显示它并对其进行更新
	pMainFrame->ShowWindow(m_nCmdShow);
	pMainFrame->UpdateWindow();

	return TRUE;
}

int CcczEditor2App::ExitInstance()
{
	//TODO: 处理可能已添加的附加资源
	AfxOleTerm(FALSE);
	//this->CleanState();
	if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
	return CWinAppEx::ExitInstance();
}

void CcczEditor2App::deleteCopy(ItemData* data)
{
	if (data == NULL)return;
	deleteCopy(data->child);
	deleteCopy(data->bro);

	ccz::editor::ReleaseLongCharData(
		data->long_char_data,
		data->long_char_data_capacity);
	delete data;
}

void CcczEditor2App::OnFileOpen()
{
	CString vctData[50];
	CFileDialog dlg(TRUE, _T("*.eex"), NULL, OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, _T("EEX File(*.eex)|*.eex;*.eex_new||"), NULL);
	dlg.m_ofn.lpstrTitle = _T("选择多个文件");
	CString filename;
	int sum = 0;
	if (dlg.DoModal() == IDOK)
	{
		POSITION fileNamesPosition = dlg.GetStartPosition();
		while (fileNamesPosition != NULL)
		{
			filename = dlg.GetNextPathName(fileNamesPosition);
			AfxGetApp()->OpenDocumentFile(filename);
		}
	}
}


// CcczEditor1App 消息处理程序


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg() noexcept;

	// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持
public:
	DECLARE_MESSAGE_MAP()
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
};

CAboutDlg::CAboutDlg() noexcept : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

// 用于运行对话框的应用程序命令
void CcczEditor2App::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

// CcczEditor1App 自定义加载/保存方法

void CcczEditor2App::PreLoadState()
{
	BOOL bNameValid;
	CString strName;
	bNameValid = strName.LoadString(IDS_EDIT_MENU);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);
	bNameValid = strName.LoadString(IDS_EXPLORER);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}

void CcczEditor2App::LoadCustomState()
{
}

void CcczEditor2App::SaveCustomState()
{
}
BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()


BOOL CAboutDlg::OnEraseBkgnd(CDC* pDC)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值

	return CDialogEx::OnEraseBkgnd(pDC);
}


void CcczEditor2App::OnSaveCustom()
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
	f.Open(ini_path, CStdioFile::modeCreate | CStdioFile::modeWrite);
	wchar_t content[3000];
	wcscpy_s(content, L"");
	for (int i = 0; i < 124; i++)
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 3; k++) {
				wcscat_s(content, std::to_wstring(custom_color[i][j][k]).c_str());
				if (k < 2) wcscat_s(content, L",");
				else wcscat_s(content, L"\n");
			}
	len = WideCharToMultiByte(936, 0, content, -1, NULL, 0, NULL, FALSE);
	char res[3000];
	WideCharToMultiByte(936, 0, content, -1, res, len, NULL, FALSE);
	f.Write(res, len);
	f.Close();

	WritePrivateProfileString(L"Config", L"ItemHeight", std::to_wstring(custom_height).c_str(), ini_path);
	WritePrivateProfileString(L"Config", L"UseFont", theApp.custom_use_font ? L"1" : L"0", ini_path);
	WritePrivateProfileString(L"Config", L"NightMode", theApp.custom_night_mode ? L"1" : L"0", ini_path);

	/*保存字体到注册表*/
	theApp.WriteProfileBinary(_T("CCZEDITOR"), _T("FONT"), (LPBYTE)&theApp.custom_font, sizeof(theApp.custom_font));
}
