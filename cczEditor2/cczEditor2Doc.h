
// cczEditor2Doc.h: CcczEditor2Doc 类的接口
//
#include <vector>
#pragma once

struct ItemData
{
	int id;
	int ord;
	std::vector<int> int_data;
	wchar_t* long_char_data = NULL;
	ItemData* child = NULL;
	ItemData* bro = NULL;

	ItemData():int_data(20,0){}
	ItemData(int length) : int_data(length, 0) {}
};

class CcczEditor2Doc : public CDocument
{
protected: // 仅从序列化创建
	CcczEditor2Doc() noexcept;
	DECLARE_DYNCREATE(CcczEditor2Doc)

// 特性
public:
	LPCTSTR pathName = L"";
	LPCTSTR modName = L"";
	LPCTSTR title = L"";
	// 操作
public:

// 重写
public:
	virtual BOOL OnNewDocument();
	virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
	void recur_refresh(HTREEITEM cur, CTreeCtrl& tree);
	virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
	void recur_close(HTREEITEM cur, CTreeCtrl& tree, bool del);
	virtual void OnCloseDocument();
	//void recur_check();
	virtual void Serialize(CArchive& ar);
	virtual BOOL SaveModified();
	void OnModified(BOOL m);
	bool modified = false;
	bool can_close = false;

	void recur_export(HTREEITEM cur, CTreeCtrl& tree, int depth, CFile &f);
	afx_msg void OnExportTxt();

#ifdef SHARED_HANDLERS
	virtual void InitializeSearchContent();
	virtual void OnDrawThumbnail(CDC& dc, LPRECT lprcBounds);
#endif // SHARED_HANDLERS

// 实现
public:
	virtual ~CcczEditor2Doc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// 生成的消息映射函数
protected:
	DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
	// 用于为搜索处理程序设置搜索内容的 Helper 函数
	void SetSearchContent(const CString& value);
#endif // SHARED_HANDLERS
};

class Dialog_Exit :
	public CDialogEx
{
public:
	Dialog_Exit() noexcept : CDialogEx(IDD_DIALOGEXIT) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOGEXIT };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDOK, button1);
		DDX_Control(pDX, ID_BUTTON1, button2);
	}
public:
	CcczEditor2Doc* cur_doc = NULL;;
	CButton button1;
	CButton button2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk1();
	afx_msg void OnBnClickedOk2();
};