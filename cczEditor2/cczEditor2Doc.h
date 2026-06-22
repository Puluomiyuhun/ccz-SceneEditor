
// cczEditor2Doc.h: CcczEditor2Doc 类的接口
//
#include <vector>
#include <thread>
#include <string>
#pragma once

// 前向声明
namespace httplib { class Server; }
class CcczEditor2View;

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

	// JSON导出功能
	void recur_export_json(HTREEITEM cur, CTreeCtrl& tree, int depth, CFile& f, bool& first_child);
	afx_msg void OnExportJson();

	// 添加指令功能
	// afterOrd: 在ord为多少的指令后面添加（-1表示添加到根节点末尾）
	// instructionId: 新指令的类型ID (0-123)
	// intData: 整数参数数组（对应int_data[20]），最多20个
	// intDataCount: intData数组的实际长度
	// text: 文本参数（对应long_char_data），可为NULL
	bool AddInstruction(int afterOrd, int instructionId, const int* intData, int intDataCount, const wchar_t* text);

	// 辅助函数：在下拉框列表中查找选项的索引
	// listType: 列表类型 ("person"=人物, "item"=物品, "direction"=方向, "job"=兵种,
	//           "magic"=法术, "skill"=特技, "face"=头像, "movie"=动画, "object"=物件,
	//           "terrain"=地形, "camp"=阵营, "gesture"=动作, "weather"=天气, "policy"=策略,
	//           "condition"=条件, "var_kind"=变量类型, "compare"=比较符, "operate"=运算符)
	// name: 要查找的名称
	// 返回值: 索引值（从0开始），未找到返回-1
	int FindComboIndex(const wchar_t* listType, const wchar_t* name);

	// 获取列表项（前N个）
	// listType: 列表类型（同FindComboIndex）
	// count: 要获取的数量
	// 返回值: JSON数组字符串，包含名称列表
	std::wstring GetListItems(const wchar_t* listType, int count);

	// 批量添加指令功能 - 解析JSON数组并递归添加指令（保留层级关系）
	// afterOrd: 在ord为多少的指令后面添加（-1表示添加到根节点末尾）
	// instructionsJson: JSON格式的指令数组字符串，格式如下：
	//   [
	//     {"id": 2, "intData": [0], "text": "注释", "children": [...]},
	//     {"id": 9, "intData": [100]},
	//     ...
	//   ]
	// 每个指令对象可包含: id(必填), intData(可选), text(可选), children(可选,嵌套指令数组)
	// 返回值: 成功添加的指令数量
	int AddInstructionsBatch(int afterOrd, const wchar_t* instructionsJson);

	// HTTP Server 功能
	void StartHttpServer(int port = 8080);
	void StopHttpServer();
	bool IsHttpServerRunning() const;
	afx_msg void OnStartHttpServer();
	afx_msg void OnStopHttpServer();

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

private:
	httplib::Server* m_httpServer = nullptr;
	std::thread* m_serverThread = nullptr;
	int m_serverPort = 8080;
	bool m_serverRunning = false;
	
	// 缓存的 View 指针（用于 HTTP API 访问，避免后台线程调用 MFC 方法）
	CcczEditor2View* m_cachedView = nullptr;
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