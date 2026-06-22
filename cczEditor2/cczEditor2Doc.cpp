
// cczEditor2Doc.cpp: CcczEditor2Doc 类的实现
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS 可以在实现预览、缩略图和搜索筛选器句柄的
// ATL 项目中进行定义，并允许与该项目共享文档代码。
#ifndef SHARED_HANDLERS
#include "cczEditor2.h"
#endif

#include "cczEditor2Doc.h"

#include <propkey.h>
#include <string>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#include "cczEditor2View.h"
#include "MainFrm.h"

// HTTP Server 相关
// #define CPPHTTPLIB_OPENSSL_SUPPORT  // 禁用 OpenSSL，使用纯 HTTP
#include "httplib.h"
#include "json.hpp"
using json = nlohmann::json;

// CcczEditor2Doc

IMPLEMENT_DYNCREATE(CcczEditor2Doc, CDocument)

BEGIN_MESSAGE_MAP(CcczEditor2Doc, CDocument)
	ON_BN_CLICKED(ID_EXPORT_TXT, &CcczEditor2Doc::OnExportTxt)
	ON_COMMAND(ID_EXPORT_JSON, &CcczEditor2Doc::OnExportJson)
	ON_COMMAND(ID_START_HTTP_SERVER, &CcczEditor2Doc::OnStartHttpServer)
	ON_COMMAND(ID_STOP_HTTP_SERVER, &CcczEditor2Doc::OnStopHttpServer)
END_MESSAGE_MAP()


// CcczEditor2Doc 构造/析构

CcczEditor2Doc::CcczEditor2Doc() noexcept
{
	// TODO: 在此添加一次性构造代码

}

CcczEditor2Doc::~CcczEditor2Doc()
{
}

BOOL CcczEditor2Doc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;
	
	title = new wchar_t[512];
	wcscpy_s((wchar_t*)title, 512, this->GetTitle());

	return TRUE;
}
byte k[4];
byte* int2ch(int p, int num)
{
	unsigned int tmp = p;
	if (p < 0 && num == 2) tmp += 65536;
	else if (p < 0 && num == 4) tmp += 4294967296;
	for (int i = 0; i < num; i++)
	{
		k[i] = tmp % 256;
		tmp /= 256;
	}
	return k;
}

BOOL CcczEditor2Doc::OnOpenDocument(LPCTSTR lpszPathName)
{
	pathName = new wchar_t[512];
	wcscpy_s((wchar_t*)pathName, 512, (wchar_t *)lpszPathName);
	if (!CDocument::OnOpenDocument(lpszPathName))
		return FALSE;

	wchar_t path[512];
	wcscpy_s(path, lpszPathName);
	int len = wcsnlen_s(path, 512); int pos;
	for (pos = len - 1; pos >= 0; pos--) {
		if (path[pos] == '/' || path[pos] == '\\') break;
	}
	wchar_t real_path[512];
	wcsncpy_s(real_path, path + pos + 1, len - pos - 1);

	title = new wchar_t[512];
	wcscpy_s((wchar_t*)title, 512, real_path);
	this->SetTitle(title);
	return TRUE;
}

void CcczEditor2Doc::recur_refresh(HTREEITEM cur, CTreeCtrl& tree)
{
	POSITION vp = GetFirstViewPosition();
	CcczEditor2View* pView = (CcczEditor2View*)GetNextView(vp);
	ItemData* p = (ItemData*)tree.GetItemData(cur);
	if (p != NULL) {
		pView->new_ord[p->ord] = pView->cur_code_ord;
		p->ord = pView->cur_code_ord++;
		if (p->id == 0x76) pView->jmp_list.push_back(cur);
	}
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL)
		recur_refresh(child, tree);
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		recur_refresh(bro, tree);
}

BOOL CcczEditor2Doc::OnSaveDocument(LPCTSTR lpszPathName)
{
	/*if (wcslen(lpszPathName) == 0) {
		TCHAR szFilter[] = _T("曹操传剧本文件(*.eex)|*.eex||");
		// 构造打开文件对话框   
		CFileDialog fileDlg(FALSE, _T("eex"), NULL, 0, szFilter, NULL);
		// 显示打开文件对话框   
		if (IDOK == fileDlg.DoModal())
			wcscpy_s((wchar_t*)lpszPathName, 128, fileDlg.GetPathName());
		else return TRUE;
	}*/
	// MFC自动恢复会传入AppData\Local临时路径,跳过自定义保存逻辑
	CString savePath(lpszPathName);
	if (savePath.Find(L"\\AppData\\Local\\") != -1) {
		return CDocument::OnSaveDocument(lpszPathName);
	}
	pathName = new wchar_t[512];
	wcscpy_s((wchar_t*)pathName, 512, lpszPathName);
	if (!CDocument::OnSaveDocument(lpszPathName))
		return FALSE;
	POSITION p = GetFirstViewPosition();
	CcczEditor2View* pView = (CcczEditor2View*)GetNextView(p);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	CFile f;
	/*写入头文件*/
	byte c[100] = { 0x45,0x45,0x58,0,1,2,0,0,0,0 };
	f.Open(pathName, CFile::modeCreate | CFile::modeWrite);
	f.Write(c, 10);

	/*计算scene的个数*/
	HTREEITEM top = m_TreeCtrl.GetRootItem();
	int scene_cnt = 0;
	HTREEITEM scene[500];
	scene[0] = m_TreeCtrl.GetChildItem(top);
	while (scene[scene_cnt] != NULL) {
		scene_cnt++;
		f.Write(c, 4);
		scene[scene_cnt] = m_TreeCtrl.GetNextSiblingItem(scene[scene_cnt - 1]);
	}

	/*开始写入每一个scene的内容*/
	for (int i = 0; i < scene_cnt; i++)
	{
		int pos = f.GetPosition();
		f.Seek(10 + i * 4, CFile::begin);
		f.Write(int2ch(pos, 4), 4);
		f.Seek(pos, CFile::begin);

		/*计算section的个数*/
		int section_cnt = 0;
		HTREEITEM section[500];
		section[0] = m_TreeCtrl.GetChildItem(scene[i]);
		while (section[section_cnt] != NULL) {
			section_cnt++;
			section[section_cnt] = m_TreeCtrl.GetNextSiblingItem(section[section_cnt - 1]);
		}

		/*写入section的个数*/
		f.Write(int2ch(section_cnt, 2), 2);
		/*写入每一个section的内容*/
		for (int j = 0; j < section_cnt; j++)
		{
			int sec_pos[100];            //每一层嵌套区的起点
			int qiantao = 1;             //嵌套的层数
			sec_pos[0] = f.GetPosition();
			f.Write(int2ch(0, 2), 2);    //写入section的总长度
			bool flag = false;
			/*下面开始遍历指令*/
			HTREEITEM item = m_TreeCtrl.GetChildItem(section[j]);
			ItemData* data;
			while (true)
			{
				data = (ItemData*)m_TreeCtrl.GetItemData(item);
				int id = data->id;

				pView->code_off[data->ord] = f.GetPosition();
				if (id == 0x76)
				{
					pView->jmp_pos.push_back(f.GetPosition());
					pView->jmp_goal.push_back(data->int_data[0]);
				}

				int long_char_sum = 0;
				bool var = false;
				int var_sum = 13;
				if (id == 0x46)var_sum = 11 * 20;
				if (id == 0x47)var_sum = 12 * 80;
				f.Write(int2ch(id, 2), 2);
				for (int k = 0; k < var_sum; k++)
				{
					int ins = pView->code_instruct[id][k];
					if (id == 0x46) ins = pView->code_instruct[id][k % 11];
					if (id == 0x47) ins = pView->code_instruct[id][k % 12];
					if (ins == -1)break;
					f.Write(int2ch(ins, 2), 2);
					if (ins == 0x5)
					{
						wchar_t tmp[10000];
						char tmp2[10000];
						for (int j = 0; j < 10000; j++)
						{
							tmp[j] = data->long_char_data[j];
							if (tmp[j] == 0)break;
						}
						int wlen = WideCharToMultiByte(936, 0, tmp, -1, NULL, 0, NULL, FALSE);
						WideCharToMultiByte(936, 0, tmp, -1, tmp2, wlen, NULL, FALSE);
						f.Write(tmp2, wlen);
						long_char_sum++;
					}
					else if (ins == 0x35)
					{
						int sum = 0;
						for (int i = 0; i < 25; i++) {
							if (data->int_data[i + (var ? 25 : 0)] != -1)sum++;
							else break;
						}
						f.Write(int2ch(sum, 2), 2);
						for (int i = 0; i < sum; i++)f.Write(int2ch(data->int_data[i + (var ? 25 : 0)], 2), 2);
						var = true;
					}
					else if (ins == 0x4) f.Write(int2ch(data->int_data[k - long_char_sum], 4), 4);
					else f.Write(int2ch(data->int_data[k - long_char_sum], 2), 2);
				}
				/*检查一下该节点是否有子节点*/
				HTREEITEM child = m_TreeCtrl.GetChildItem(item);
				if (child != NULL)
				{
					/*继续叠加嵌套*/
					item = child;
					sec_pos[qiantao++] = f.GetPosition();
					f.Write(int2ch(0, 2), 2);
				}
				else
				{
					HTREEITEM tmp = m_TreeCtrl.GetNextSiblingItem(item);
					while (tmp == NULL)
					{
						int pos = f.GetPosition();
						qiantao--;
						f.Seek(sec_pos[qiantao], 0);
						f.Write(int2ch(pos - sec_pos[qiantao] - 2, 2), 2);
						f.Seek(pos, 0);
						tmp = m_TreeCtrl.GetParentItem(item);
						if (tmp == section[j]) {
							flag = true;
							break;
						}
						item = tmp;
						tmp = m_TreeCtrl.GetNextSiblingItem(item);
					}
					item = tmp;
				}
				if (flag)break;
			}
		}
	}

	/*更新写入的无条件跳转*/
	for (int i = 0; i < pView->jmp_pos.size(); i++)
	{
		f.Seek(pView->jmp_pos[i] + 4, CFile::begin);
		f.Write(int2ch(pView->code_off[pView->jmp_goal[i]] - pView->jmp_pos[i] - 8, 4), 4);
	}
	pView->jmp_pos.clear();
	pView->jmp_goal.clear();

	/*更新全部指令，并准备更新无条件跳转*/
	HTREEITEM root = m_TreeCtrl.GetRootItem();
	pView->jmp_list.clear();
	pView->cur_code_ord = 0;
	recur_refresh(root, m_TreeCtrl);

	for (int i = 0; i < pView->jmp_list.size(); i++)
	{
		ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(pView->jmp_list[i]);
		data->int_data[0] = pView->new_ord[data->int_data[0]];
		pView->UpdateShow(pView->jmp_list[i]);
	}
	pView->jmp_list.clear();

	f.Close();
	OnModified(FALSE);

	return TRUE;
}

void CcczEditor2Doc::recur_close(HTREEITEM cur,CTreeCtrl & tree, bool del)
{
	ItemData* p = (ItemData*)tree.GetItemData(cur);
	if (p != NULL) {
		if (p->long_char_data != NULL)
			delete p->long_char_data;
		delete p;
	}
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL)
		recur_close(child, tree, del);
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		recur_close(bro, tree, del);
	if (del)tree.DeleteItem(cur);
}

void CcczEditor2Doc::OnCloseDocument()
{
	POSITION p = GetFirstViewPosition();
	CcczEditor2View* pView = (CcczEditor2View*)GetNextView(p);
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
	if (m_TreeCtrl) {
		HTREEITEM root = m_TreeCtrl.GetRootItem();
		recur_close(root, m_TreeCtrl, false);
	}
	CDocument::OnCloseDocument();
}

// CcczEditor2Doc 序列化

void CcczEditor2Doc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		
	}
	else
	{

	}
}

void CcczEditor2Doc::recur_export(HTREEITEM cur, CTreeCtrl& tree, int depth, CFile& f)
{
	POSITION vp = GetFirstViewPosition();
	CcczEditor2View* pView = (CcczEditor2View*)GetNextView(vp);
	if (cur != NULL) {
		for (int i = 0; i < depth; i++) f.Write("\t", 1);
		char tmp[3000];
		int len = WideCharToMultiByte(936, 0, tree.GetItemText(cur), -1, NULL, 0, NULL, FALSE);
		WideCharToMultiByte(936, 0, tree.GetItemText(cur), -1, tmp, len, NULL, FALSE);
		f.Write(tmp, len);
		f.Write("\n", 1);
	}
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL)
		recur_export(child, tree, depth + 1, f);
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		recur_export(bro, tree, depth, f);
}

void CcczEditor2Doc::OnExportTxt()
{
	TCHAR szFilter[] = _T("文本文件(*.txt)|*.ini;*.txt||");
	// 构造打开文件对话框   
	CFileDialog fileDlg(FALSE, _T("txt"), NULL, 0, szFilter, NULL);
	CString strFilePath;
	// 显示打开文件对话框   
	if (IDOK == fileDlg.DoModal())
	{
		CFile f;
		f.Open(fileDlg.GetPathName(), CFile::modeCreate | CFile::modeWrite);

		CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
		CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();
		recur_export(m_TreeCtrl.GetRootItem(), m_TreeCtrl, 0, f);

		f.Close();
	}
}

#ifdef SHARED_HANDLERS

// 缩略图的支持
void CcczEditor2Doc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// 修改此代码以绘制文档数据
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// 搜索处理程序的支持
void CcczEditor2Doc::InitializeSearchContent()
{
	CString strSearchContent;
	// 从文档数据设置搜索内容。
	// 内容部分应由“;”分隔

	// 例如:     strSearchContent = _T("point;rectangle;circle;ole object;")；
	SetSearchContent(strSearchContent);
}

void CcczEditor2Doc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl *pChunk = nullptr;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != nullptr)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CcczEditor2Doc 诊断

#ifdef _DEBUG
void CcczEditor2Doc::AssertValid() const
{
	CDocument::AssertValid();
}

void CcczEditor2Doc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CcczEditor2Doc 命令


BOOL CcczEditor2Doc::SaveModified()
{
	// TODO: 在此添加专用代码和/或调用基类
	if (modified == false)
		return CDocument::SaveModified();
	else {
		Dialog_Exit dialog;
		dialog.cur_doc = this;
		dialog.DoModal();
		if (can_close)return CDocument::SaveModified();
		else return FALSE;
	}
}

void CcczEditor2Doc::OnModified(BOOL m)
{
	modified = m;
	wchar_t show[100];
	wcscpy_s(show, title);
	if (m == TRUE)wcscat_s(show, L" *");
	this->SetTitle(show);
}


BEGIN_MESSAGE_MAP(Dialog_Exit, myDialog)
	ON_BN_CLICKED(IDOK, &Dialog_Exit::OnBnClickedOk1)
	ON_BN_CLICKED(ID_BUTTON1, &Dialog_Exit::OnBnClickedOk2)
END_MESSAGE_MAP()

BOOL Dialog_Exit::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowTextW(cur_doc->title);
	return TRUE;
}

void Dialog_Exit::OnBnClickedOk1()
{
	if (cur_doc->pathName != L"") {
		cur_doc->OnSaveDocument(cur_doc->pathName);
		cur_doc->can_close = true;
	}
	else {
		//MessageBox(TEXT("新建的剧本文件需要手动保存后才能关闭"), TEXT("关闭失败"), MB_ICONERROR);
		//doc->can_close = false;
		TCHAR szFilter[] = _T("曹操传剧本文件(*.eex)|*.eex||")_T("曹操传新剧本文件(*.eex_new)|*.eex_new||");
		// 构造打开文件对话框   
		CFileDialog fileDlg(FALSE, _T("eex"), NULL, 0, szFilter, this);
		CString strFilePath;
		// 显示打开文件对话框   
		if (IDOK == fileDlg.DoModal())
		{
			cur_doc->OnSaveDocument(fileDlg.GetPathName());
			cur_doc->can_close = true;
		}
	}
	CDialogEx::OnOK();
}

void Dialog_Exit::OnBnClickedOk2()
{
	cur_doc->can_close = true;
	CDialogEx::OnOK();
}

// JSON导出辅助函数：将宽字符串转换为UTF-8并写入文件，同时转义特殊字符
static void WriteJsonString(CFile& f, const wchar_t* str)
{
	f.Write("\"", 1);
	if (str != NULL) {
		int len = wcslen(str);
		for (int i = 0; i < len; i++) {
			wchar_t c = str[i];
			if (c == L'"') {
				f.Write("\\\"", 2);
			}
			else if (c == L'\\') {
				f.Write("\\\\", 2);
			}
			else if (c == L'\n') {
				f.Write("\\n", 2);
			}
			else if (c == L'\r') {
				f.Write("\\r", 2);
			}
			else if (c == L'\t') {
				f.Write("\\t", 2);
			}
			else {
				char utf8[4] = { 0 };
				int utf8len = WideCharToMultiByte(65001, 0, &c, 1, utf8, 4, NULL, FALSE);
				f.Write(utf8, utf8len);
			}
		}
	}
	f.Write("\"", 1);
}

// 递归导出JSON
void CcczEditor2Doc::recur_export_json(HTREEITEM cur, CTreeCtrl& tree, int depth, CFile& f, bool& first_child)
{
	if (cur == NULL) return;

	POSITION vp = GetFirstViewPosition();
	CcczEditor2View* pView = (CcczEditor2View*)GetNextView(vp);
	ItemData* data = (ItemData*)tree.GetItemData(cur);

	if (data == NULL) {
		// 处理兄弟节点
		HTREEITEM bro = tree.GetNextSiblingItem(cur);
		if (bro != NULL)
			recur_export_json(bro, tree, depth, f, first_child);
		return;
	}

	int id = data->id;

	// 获取指令名称
	CString itemText = tree.GetItemText(cur);

	// 如果不是第一个子节点，添加逗号
	if (!first_child) {
		f.Write(",", 1);
	}
	first_child = false;

	// 写入缩进
	f.Write("\r\n", 2);
	for (int i = 0; i < depth; i++) f.Write("  ", 2);

	// 开始对象
	f.Write("{", 1);

	// 写入指令ID
	f.Write("\r\n", 2);
	for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
	f.Write("\"id\": ", 6);
	char idStr[20];
	sprintf_s(idStr, "%d", id);
	f.Write(idStr, strlen(idStr));

	// 写入指令名称（从code数组获取）
	f.Write(",", 1);
	f.Write("\r\n", 2);
	for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
	f.Write("\"name\": ", 8);
	if (id >= 0 && id < 124) {
		WriteJsonString(f, pView->code[id]);
	}
	else if (id == -1) {
		WriteJsonString(f, L"Scene");
	}
	else if (id == -2) {
		WriteJsonString(f, L"Section");
	}
	else {
		WriteJsonString(f, L"Unknown");
	}

	// 写入序号
	f.Write(",", 1);
	f.Write("\r\n", 2);
	for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
	f.Write("\"ord\": ", 7);
	char ordStr[20];
	sprintf_s(ordStr, "%d", data->ord);
	f.Write(ordStr, strlen(ordStr));

	// 写入整数数据（只写入有效的部分）
	bool has_int_data = false;
	int int_count = 0;
	for (int i = 0; i < (int)data->int_data.size(); i++) {
		if (data->int_data[i] != 0 || i < 5) { // 至少保留前5个，或者非零值
			int_count = i + 1;
			has_int_data = true;
		}
	}

	if (has_int_data && int_count > 0) {
		f.Write(",", 1);
		f.Write("\r\n", 2);
		for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
		f.Write("\"int_data\": [", 13);
		for (int i = 0; i < int_count; i++) {
			if (i > 0) f.Write(", ", 2);
			char valStr[20];
			sprintf_s(valStr, "%d", data->int_data[i]);
			f.Write(valStr, strlen(valStr));
		}
		f.Write("]", 1);
	}

	// 写入文本数据
	if (data->long_char_data != NULL && wcslen(data->long_char_data) > 0) {
		f.Write(",", 1);
		f.Write("\r\n", 2);
		for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
		f.Write("\"text\": ", 8);
		WriteJsonString(f, data->long_char_data);
	}

	// 处理子节点
	HTREEITEM child = tree.GetChildItem(cur);
	if (child != NULL) {
		f.Write(",", 1);
		f.Write("\r\n", 2);
		for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
		f.Write("\"children\": [", 13);
		bool first_sub = true;
		recur_export_json(child, tree, depth + 2, f, first_sub);
		f.Write("\r\n", 2);
		for (int i = 0; i < depth + 1; i++) f.Write("  ", 2);
		f.Write("]", 1);
	}

	// 结束对象
	f.Write("\r\n", 2);
	for (int i = 0; i < depth; i++) f.Write("  ", 2);
	f.Write("}", 1);

	// 处理兄弟节点
	HTREEITEM bro = tree.GetNextSiblingItem(cur);
	if (bro != NULL)
		recur_export_json(bro, tree, depth, f, first_child);
}

void CcczEditor2Doc::OnExportJson()
{
	TCHAR szFilter[] = _T("JSON文件(*.json)|*.json||");
	CFileDialog fileDlg(FALSE, _T("json"), NULL, 0, szFilter, NULL);
	CString strFilePath;
	if (IDOK == fileDlg.DoModal())
	{
		CFile f;
		f.Open(fileDlg.GetPathName(), CFile::modeCreate | CFile::modeWrite);

		// 写入UTF-8 BOM
		unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		f.Write(bom, 3);

		CcczEditor2View* pView = (CcczEditor2View*)(((CMainFrame*)AfxGetMainWnd())->MDIGetActive()->GetActiveView());
		CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

		// 写入JSON开头
		f.Write("{\r\n", 3);
		f.Write("  \"version\": \"1.0\",\r\n", 20);
		
		// 写入文件名信息
		f.Write("  \"filename\": ", 14);
		WriteJsonString(f, this->title);
		f.Write(",\r\n", 3);

		f.Write("  \"instructions\": [", 19);

		// 递归导出所有指令
		bool first = true;
		HTREEITEM root = m_TreeCtrl.GetRootItem();
		HTREEITEM child = m_TreeCtrl.GetChildItem(root);
		recur_export_json(child, m_TreeCtrl, 2, f, first);

		// 写入JSON结尾
		f.Write("\r\n  ]\r\n}\r\n", 10);

		f.Close();

		AfxMessageBox(L"JSON导出成功！", MB_OK | MB_ICONINFORMATION);
	}
}

// 辅助函数：在下拉框列表中查找选项的索引
int CcczEditor2Doc::FindComboIndex(const wchar_t* listType, const wchar_t* name)
{
	// 尝试重新获取 View 指针（如果缓存的失效了）
	CcczEditor2View* pView = m_cachedView;
	if (pView == nullptr) {
		POSITION vp = GetFirstViewPosition();
		if (vp != NULL) {
			pView = (CcczEditor2View*)GetNextView(vp);
			m_cachedView = pView;
		}
	}

	if (pView == nullptr) {
		return -1;
	}

	if (wcscmp(listType, L"person") == 0) {
		for (int i = 0; i < 5400; i++) {
			if (pView->per1[i][0] != L'\0') {
				// 精确匹配
				if (wcscmp(pView->per1[i], name) == 0 || wcscmp(pView->per2[i], name) == 0)
					return i;
				// 模糊匹配：检查名称部分（跳过"序号:"前缀）
				const wchar_t* colonPos = wcschr(pView->per1[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
				colonPos = wcschr(pView->per2[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
			}
		}
	}
	else if (wcscmp(listType, L"item") == 0) {
		for (int i = 0; i < 512; i++) {
			if (pView->item[i][0] != L'\0') {
				if (wcscmp(pView->item[i], name) == 0)
					return i;
				const wchar_t* colonPos = wcschr(pView->item[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
			}
		}
	}
	else if (wcscmp(listType, L"direction") == 0) {
		for (int i = 0; i < 5; i++) {
			if (wcscmp(pView->dir[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"job") == 0) {
		for (int i = 0; i < 80; i++) {
			if (pView->job[i][0] != L'\0') {
				if (wcscmp(pView->job[i], name) == 0)
					return i;
				const wchar_t* colonPos = wcschr(pView->job[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
			}
		}
	}
	else if (wcscmp(listType, L"magic") == 0) {
		for (int i = 0; i < 256; i++) {
			if (pView->meff[i][0] != L'\0') {
				if (wcscmp(pView->meff[i], name) == 0)
					return i;
				const wchar_t* colonPos = wcschr(pView->meff[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
			}
		}
	}
	else if (wcscmp(listType, L"skill") == 0) {
		for (int i = 0; i < 256; i++) {
			if (pView->teji[i][0] != L'\0') {
				if (wcscmp(pView->teji[i], name) == 0)
					return i;
				const wchar_t* colonPos = wcschr(pView->teji[i], L':');
				if (colonPos && wcscmp(colonPos + 1, name) == 0)
					return i;
			}
		}
	}
	else if (wcscmp(listType, L"face") == 0) {
		for (int i = 0; i < pView->face_condition_sum; i++) {
			if (wcscmp(pView->face_condition[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"movie") == 0) {
		for (int i = 0; i < 128; i++) {
			if (wcscmp(pView->movie[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"object") == 0) {
		for (int i = 0; i < 128; i++) {
			if (wcscmp(pView->object[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"terrain") == 0) {
		for (int i = 0; i < 30; i++) {
			if (wcscmp(pView->hexz[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"camp") == 0) {
		for (int i = 0; i < 7; i++) {
			if (wcscmp(pView->zhenying[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"gesture") == 0) {
		for (int i = 0; i < 21; i++) {
			if (wcscmp(pView->ges[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"weather") == 0) {
		for (int i = 0; i < 5; i++) {
			if (wcscmp(pView->weather[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"policy") == 0) {
		for (int i = 0; i < pView->policy_sum; i++) {
			if (wcscmp(pView->policy[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"condition") == 0) {
		for (int i = 0; i < pView->all_condition_sum; i++) {
			if (wcscmp(pView->all_condition[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"var_kind") == 0) {
		for (int i = 0; i < pView->var_kind_sum; i++) {
			if (wcscmp(pView->var_kind[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"compare") == 0) {
		for (int i = 0; i < pView->compare_sum; i++) {
			if (wcscmp(pView->compare[i], name) == 0)
				return i;
		}
	}
	else if (wcscmp(listType, L"operate") == 0) {
		for (int i = 0; i < pView->operate_sum; i++) {
			if (wcscmp(pView->operate[i], name) == 0)
				return i;
		}
	}

	return -1; // 未找到或未知类型
}

// UTF-8 转宽字符串
static std::wstring Utf8ToWide(const std::string& utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
	if (len <= 0) return L"";
	std::wstring result(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
	return result;
}

// 宽字符串转 UTF-8
static std::string WideToUtf8(const std::wstring& wide)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
	if (len <= 0) return "";
	std::string result(len, 0);
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], len, NULL, NULL);
	return result;
}

// 获取列表项（前N个）
std::wstring CcczEditor2Doc::GetListItems(const wchar_t* listType, int count)
{
	// 尝试重新获取 View 指针（如果缓存的失效了）
	CcczEditor2View* pView = m_cachedView;
	if (pView == nullptr) {
		POSITION vp = GetFirstViewPosition();
		if (vp != NULL) {
			pView = (CcczEditor2View*)GetNextView(vp);
			m_cachedView = pView;
		}
	}
	if (pView == nullptr) {
		return L"[]";
	}

	json items = json::array();

	if (wcscmp(listType, L"person") == 0) {
		int limit = min(count, 5400);
		for (int i = 0; i < limit; i++) {
			if (pView->per1[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->per1[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"item") == 0) {
		int limit = min(count, 512);
		for (int i = 0; i < limit; i++) {
			if (pView->item[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->item[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"direction") == 0) {
		int limit = min(count, 5);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->dir[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"job") == 0) {
		int limit = min(count, 80);
		for (int i = 0; i < limit; i++) {
			if (pView->job[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->job[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"magic") == 0) {
		int limit = min(count, 256);
		for (int i = 0; i < limit; i++) {
			if (pView->meff[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->meff[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"skill") == 0) {
		int limit = min(count, 256);
		for (int i = 0; i < limit; i++) {
			if (pView->teji[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->teji[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"face") == 0) {
		int limit = min(count, pView->face_condition_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->face_condition[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"movie") == 0) {
		int limit = min(count, 128);
		for (int i = 0; i < limit; i++) {
			if (pView->movie[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->movie[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"object") == 0) {
		int limit = min(count, 128);
		for (int i = 0; i < limit; i++) {
			if (pView->object[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->object[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"terrain") == 0) {
		int limit = min(count, 30);
		for (int i = 0; i < limit; i++) {
			if (pView->hexz[i][0] != L'\0') {
				std::string nameUtf8 = WideToUtf8(pView->hexz[i]);
				items.push_back(nameUtf8);
			}
		}
	}
	else if (wcscmp(listType, L"camp") == 0) {
		int limit = min(count, 7);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->zhenying[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"gesture") == 0) {
		int limit = min(count, 21);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->ges[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"weather") == 0) {
		int limit = min(count, 5);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->weather[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"policy") == 0) {
		int limit = min(count, pView->policy_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->policy[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"condition") == 0) {
		int limit = min(count, pView->all_condition_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->all_condition[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"var_kind") == 0) {
		int limit = min(count, pView->var_kind_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->var_kind[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"compare") == 0) {
		int limit = min(count, pView->compare_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->compare[i]);
			items.push_back(nameUtf8);
		}
	}
	else if (wcscmp(listType, L"operate") == 0) {
		int limit = min(count, pView->operate_sum);
		for (int i = 0; i < limit; i++) {
			std::string nameUtf8 = WideToUtf8(pView->operate[i]);
			items.push_back(nameUtf8);
		}
	}

	std::string utf8Result = items.dump();
	return Utf8ToWide(utf8Result);
}

bool CcczEditor2Doc::AddInstruction(int afterOrd, int instructionId, const int* intData, int intDataCount, const wchar_t* text)
{
	// 尝试重新获取 View 指针（如果缓存的失效了）
	CcczEditor2View* pView = m_cachedView;
	if (pView == nullptr) {
		POSITION vp = GetFirstViewPosition();
		if (vp != NULL) {
			pView = (CcczEditor2View*)GetNextView(vp);
			m_cachedView = pView;
		}
	}
	if (pView == nullptr) {
		AfxMessageBox(L"无法获取视图，请确保已打开剧本文件", MB_OK | MB_ICONERROR);
		return false;
	}
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	// 特殊处理：Scene (-1) 和 Section (-2) - 只创建节点，不自动创建子指令
	if (instructionId == -1) {
		// 创建 Scene 节点（不自动创建子 Section）
		HTREEITEM parentItem = NULL;
		HTREEITEM insertAfterPos = TVI_LAST;
		
		if (afterOrd == -1) {
			parentItem = m_TreeCtrl.GetRootItem();
		} else {
			std::vector<HTREEITEM> stack;
			stack.push_back(m_TreeCtrl.GetRootItem());
			while (!stack.empty()) {
				HTREEITEM cur = stack.back();
				stack.pop_back();
				ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur);
				if (data && data->ord == afterOrd) {
					parentItem = m_TreeCtrl.GetParentItem(cur);
					insertAfterPos = cur;
					break;
				}
				HTREEITEM child = m_TreeCtrl.GetChildItem(cur);
				while (child) {
					stack.push_back(child);
					child = m_TreeCtrl.GetNextSiblingItem(child);
				}
			}
			if (parentItem == NULL) {
				AfxMessageBox(L"找不到指定ord的指令", MB_OK | MB_ICONERROR);
				return false;
			}
		}
		
		TVINSERTSTRUCT tvInsert;
		tvInsert.hParent = parentItem;
		tvInsert.hInsertAfter = insertAfterPos;
		tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		tvInsert.item.pszText = L"Scene";
		tvInsert.item.lParam = 0;
		HTREEITEM sceneItem = m_TreeCtrl.InsertItem(&tvInsert);
		m_TreeCtrl.SetItemData(sceneItem, (DWORD_PTR)pView->InitData(-1));
		
		this->OnModified(TRUE);
		return true;
	}
	else if (instructionId == -2) {
		// 创建 Section 节点（不自动创建子指令）
		HTREEITEM parentItem = NULL;
		HTREEITEM insertAfterPos = TVI_LAST;
		
		if (afterOrd == -1) {
			parentItem = m_TreeCtrl.GetRootItem();
		} else {
			std::vector<HTREEITEM> stack;
			stack.push_back(m_TreeCtrl.GetRootItem());
			while (!stack.empty()) {
				HTREEITEM cur = stack.back();
				stack.pop_back();
				ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur);
				if (data && data->ord == afterOrd) {
					parentItem = m_TreeCtrl.GetParentItem(cur);
					insertAfterPos = cur;
					break;
				}
				HTREEITEM child = m_TreeCtrl.GetChildItem(cur);
				while (child) {
					stack.push_back(child);
					child = m_TreeCtrl.GetNextSiblingItem(child);
				}
			}
			if (parentItem == NULL) {
				AfxMessageBox(L"找不到指定ord的指令", MB_OK | MB_ICONERROR);
				return false;
			}
		}
		
		TVINSERTSTRUCT tvInsert;
		tvInsert.hParent = parentItem;
		tvInsert.hInsertAfter = insertAfterPos;
		tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		tvInsert.item.pszText = L"Section";
		tvInsert.item.lParam = 0;
		HTREEITEM sectionItem = m_TreeCtrl.InsertItem(&tvInsert);
		m_TreeCtrl.SetItemData(sectionItem, (DWORD_PTR)pView->InitData(-2));
		
		this->OnModified(TRUE);
		return true;
	}

	// 验证指令ID
	if (instructionId < 0 || instructionId > 123) {
		AfxMessageBox(L"无效的指令ID", MB_OK | MB_ICONERROR);
		return false;
	}

	// 查找目标位置（afterOrd对应的节点）
	HTREEITEM targetItem = NULL;
	HTREEITEM parentItem = NULL;

	if (afterOrd == -1) {
		// 添加到根节点末尾
		targetItem = m_TreeCtrl.GetRootItem();
		parentItem = targetItem;
	}
	else {
		// 遍历树查找ord匹配的节点
		std::vector<HTREEITEM> stack;
		stack.push_back(m_TreeCtrl.GetRootItem());
		while (!stack.empty()) {
			HTREEITEM cur = stack.back();
			stack.pop_back();

			ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur);
			if (data && data->ord == afterOrd) {
				targetItem = cur;
				parentItem = m_TreeCtrl.GetParentItem(cur);
				break;
			}

			// 添加子节点到栈
			HTREEITEM child = m_TreeCtrl.GetChildItem(cur);
			while (child) {
				stack.push_back(child);
				child = m_TreeCtrl.GetNextSiblingItem(child);
			}
		}

		if (targetItem == NULL) {
			AfxMessageBox(L"找不到指定ord的指令", MB_OK | MB_ICONERROR);
			return false;
		}
	}

	// 创建新指令数据
	ItemData* newData = pView->InitData(instructionId);
	newData->id = instructionId;

	// 复制整数参数
	if (intData != NULL && intDataCount > 0) {
		int count = min(intDataCount, 20);
		for (int i = 0; i < count; i++) {
			newData->int_data[i] = intData[i];
		}
	}

	// 复制文本参数
	if (text != NULL && wcslen(text) > 0) {
		if (newData->long_char_data != NULL) {
			delete[] newData->long_char_data;
		}
		int len = (int)wcslen(text) + 1;
		newData->long_char_data = new wchar_t[len];
		wcscpy_s(newData->long_char_data, len, text);
	}

	// 分配新的ord
	pView->cur_code_ord++;
	newData->ord = pView->cur_code_ord;

	// 插入到树中（在targetItem后面）
	CTreeCtrl& tree = m_TreeCtrl;
	TVINSERTSTRUCT tvInsert;
	tvInsert.hParent = parentItem;
	tvInsert.hInsertAfter = targetItem;
	tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM;
	
	// 获取指令显示文本
	wchar_t showText[300];
	if (instructionId >= 0 && instructionId < 124) {
		wcscpy_s(showText, pView->code[instructionId]);
	}
	else {
		wcscpy_s(showText, L"Unknown");
	}

	tvInsert.item.pszText = showText;
	tvInsert.item.lParam = (LPARAM)newData;

	HTREEITEM newItem = tree.InsertItem(&tvInsert);
	tree.SetItemData(newItem, (DWORD_PTR)newData);

	// 更新显示
	pView->UpdateShow(newItem);

	// 标记文档已修改
	this->OnModified(TRUE);

	return true;
}

// 递归辅助函数：解析单个指令JSON对象并添加到树中
// 返回成功添加的指令数量（包括子指令）
static int RecurAddInstructionFromJson(CcczEditor2View* pView, CTreeCtrl& tree, HTREEITEM parentItem, HTREEITEM insertAfter, const json& instrJson)
{
	int count = 0;

	try {
		// 获取指令ID（必填）
		if (!instrJson.contains("id") || !instrJson["id"].is_number_integer()) {
			return 0;
		}
		int instructionId = instrJson["id"].get<int>();

		// 特殊处理：Scene (-1) 和 Section (-2) - 只创建节点，不自动创建子指令
		if (instructionId == -1) {
			// 创建 Scene 节点（不自动创建子 Section）
			TVINSERTSTRUCT tvInsert;
			tvInsert.hParent = parentItem;
			tvInsert.hInsertAfter = insertAfter ? insertAfter : TVI_LAST;
			tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
			tvInsert.item.pszText = L"Scene";
			tvInsert.item.lParam = 0;
			HTREEITEM sceneItem = tree.InsertItem(&tvInsert);
			tree.SetItemData(sceneItem, (DWORD_PTR)pView->InitData(-1));
			
			count = 1;

			// 处理 Scene 的子指令（如果有）- 添加到 Scene 节点的子节点列表
			if (instrJson.contains("children") && instrJson["children"].is_array()) {
				HTREEITEM childInsertAfter = NULL;
				for (const auto& childJson : instrJson["children"]) {
					int childCount = RecurAddInstructionFromJson(pView, tree, sceneItem, childInsertAfter, childJson);
					count += childCount;
					if (childCount > 0) {
						childInsertAfter = tree.GetChildItem(sceneItem);
						HTREEITEM lastC = childInsertAfter;
						while (lastC) {
							childInsertAfter = lastC;
							lastC = tree.GetNextSiblingItem(lastC);
						}
					}
				}
			}
			return count;
		}
		else if (instructionId == -2) {
			// 创建 Section 节点（不自动创建子指令）
			TVINSERTSTRUCT tvInsert;
			tvInsert.hParent = parentItem;
			tvInsert.hInsertAfter = insertAfter ? insertAfter : TVI_LAST;
			tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
			tvInsert.item.pszText = L"Section";
			tvInsert.item.lParam = 0;
			HTREEITEM sectionItem = tree.InsertItem(&tvInsert);
			tree.SetItemData(sectionItem, (DWORD_PTR)pView->InitData(-2));
			
			count = 1;

			// 处理 Section 的子指令（如果有）- 添加到 Section 节点的子节点列表
			if (instrJson.contains("children") && instrJson["children"].is_array()) {
				HTREEITEM childInsertAfter = NULL;
				for (const auto& childJson : instrJson["children"]) {
					int childCount = RecurAddInstructionFromJson(pView, tree, sectionItem, childInsertAfter, childJson);
					count += childCount;
					if (childCount > 0) {
						childInsertAfter = tree.GetChildItem(sectionItem);
						HTREEITEM lastC = childInsertAfter;
						while (lastC) {
							childInsertAfter = lastC;
							lastC = tree.GetNextSiblingItem(lastC);
						}
					}
				}
			}
			return count;
		}

		// 验证指令ID
		if (instructionId < 0 || instructionId > 123) {
			return 0;
		}

		// 创建新指令数据
		ItemData* newData = pView->InitData(instructionId);
		newData->id = instructionId;

		// 复制整数参数（可选）
		if (instrJson.contains("intData") && instrJson["intData"].is_array()) {
			auto intArray = instrJson["intData"];
			int len = min((int)intArray.size(), 20);
			for (int i = 0; i < len; i++) {
				if (intArray[i].is_number_integer()) {
					newData->int_data[i] = intArray[i].get<int>();
				}
			}
		}

		// 复制文本参数（可选）
		if (instrJson.contains("text") && instrJson["text"].is_string()) {
			std::wstring text = Utf8ToWide(instrJson["text"].get<std::string>());
			if (!text.empty()) {
				if (newData->long_char_data != NULL) {
					delete[] newData->long_char_data;
				}
				int len = (int)text.length() + 1;
				newData->long_char_data = new wchar_t[len];
				wcscpy_s(newData->long_char_data, len, text.c_str());
			}
		}

		// 分配新的ord
		pView->cur_code_ord++;
		newData->ord = pView->cur_code_ord;

		// 插入到树中
		TVINSERTSTRUCT tvInsert;
		tvInsert.hParent = parentItem;
		tvInsert.hInsertAfter = insertAfter;
		tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM;

		wchar_t showText[300];
		if (instructionId >= 0 && instructionId < 124) {
			wcscpy_s(showText, pView->code[instructionId]);
		}
		else {
			wcscpy_s(showText, L"Unknown");
		}

		tvInsert.item.pszText = showText;
		tvInsert.item.lParam = (LPARAM)newData;

		HTREEITEM newItem = tree.InsertItem(&tvInsert);
		tree.SetItemData(newItem, (DWORD_PTR)newData);

		// 更新显示
		pView->UpdateShow(newItem);

		count = 1; // 当前指令计数

		// 递归处理子指令（如果有children字段）
		if (instrJson.contains("children") && instrJson["children"].is_array()) {
			HTREEITEM childInsertAfter = NULL; // 第一个子节点插入到parent=newItem, after=NULL(末尾)
			for (const auto& childJson : instrJson["children"]) {
				int childCount = RecurAddInstructionFromJson(pView, tree, newItem, childInsertAfter, childJson);
				count += childCount;
				if (childCount > 0) {
					// 后续子节点插入到上一个子节点后面
					childInsertAfter = tree.GetChildItem(newItem);
					HTREEITEM lastChild = childInsertAfter;
					while (lastChild) {
						childInsertAfter = lastChild;
						lastChild = tree.GetNextSiblingItem(lastChild);
					}
				}
			}
		}
	}
	catch (...) {
		// JSON解析错误，跳过该指令
		return 0;
	}

	return count;
}

// 批量添加指令功能 - 解析JSON数组并递归添加指令（保留层级关系）
int CcczEditor2Doc::AddInstructionsBatch(int afterOrd, const wchar_t* instructionsJson)
{
	if (instructionsJson == NULL || wcslen(instructionsJson) == 0) {
		AfxMessageBox(L"JSON数据为空", MB_OK | MB_ICONERROR);
		return 0;
	}

	// 尝试重新获取 View 指针（如果缓存的失效了）
	CcczEditor2View* pView = m_cachedView;
	if (pView == nullptr) {
		POSITION vp = GetFirstViewPosition();
		if (vp != NULL) {
			pView = (CcczEditor2View*)GetNextView(vp);
			m_cachedView = pView;
		}
	}
	if (pView == nullptr) {
		AfxMessageBox(L"无法获取视图，请确保已打开剧本文件", MB_OK | MB_ICONERROR);
		return 0;
	}
	CTreeCtrl& m_TreeCtrl = pView->GetTreeCtrl();

	// 查找目标位置（afterOrd对应的节点）
	HTREEITEM targetItem = NULL;
	HTREEITEM parentItem = NULL;

	if (afterOrd == -1) {
		// 添加到根节点末尾
		targetItem = m_TreeCtrl.GetRootItem();
		parentItem = targetItem;
	}
	else {
		// 遍历树查找ord匹配的节点
		std::vector<HTREEITEM> stack;
		stack.push_back(m_TreeCtrl.GetRootItem());
		while (!stack.empty()) {
			HTREEITEM cur = stack.back();
			stack.pop_back();

			ItemData* data = (ItemData*)m_TreeCtrl.GetItemData(cur);
			if (data && data->ord == afterOrd) {
				targetItem = cur;
				parentItem = m_TreeCtrl.GetParentItem(cur);
				break;
			}

			// 添加子节点到栈
			HTREEITEM child = m_TreeCtrl.GetChildItem(cur);
			while (child) {
				stack.push_back(child);
				child = m_TreeCtrl.GetNextSiblingItem(child);
			}
		}

		if (targetItem == NULL) {
			AfxMessageBox(L"找不到指定ord的指令", MB_OK | MB_ICONERROR);
			return 0;
		}
	}

	// 解析JSON
	std::string utf8Json = WideToUtf8(instructionsJson);
	json jsonArray;
	try {
		jsonArray = json::parse(utf8Json);
	}
	catch (const json::exception& e) {
		std::string errMsg = "JSON解析失败: " + std::string(e.what());
		AfxMessageBox(Utf8ToWide(errMsg).c_str(), MB_OK | MB_ICONERROR);
		return 0;
	}

	// 验证是否为数组
	if (!jsonArray.is_array()) {
		AfxMessageBox(L"JSON格式错误：必须是数组", MB_OK | MB_ICONERROR);
		return 0;
	}

	// 逐个添加指令
	int totalCount = 0;
	HTREEITEM insertAfter = targetItem;

	for (const auto& instrJson : jsonArray) {
		int count = RecurAddInstructionFromJson(pView, m_TreeCtrl, parentItem, insertAfter, instrJson);
		totalCount += count;

		// 更新insertAfter为刚添加的最后一个同级节点
		if (count > 0) {
			insertAfter = m_TreeCtrl.GetChildItem(parentItem);
			HTREEITEM lastSibling = insertAfter;
			while (lastSibling) {
				insertAfter = lastSibling;
				lastSibling = m_TreeCtrl.GetNextSiblingItem(lastSibling);
			}
		}
	}

	// 标记文档已修改
	if (totalCount > 0) {
		this->OnModified(TRUE);
	}

	return totalCount;
}

// ==================== HTTP Server 实现 ====================

void CcczEditor2Doc::StartHttpServer(int port)
{
	if (m_serverRunning) {
		AfxMessageBox(L"HTTP服务器已在运行中", MB_OK | MB_ICONINFORMATION);
		return;
	}

	// 缓存 View 指针（在主线程中获取，避免后台线程调用 MFC 方法）
	m_cachedView = nullptr;
	POSITION vp = GetFirstViewPosition();
	if (vp != NULL) {
		m_cachedView = (CcczEditor2View*)GetNextView(vp);
	}

	m_serverPort = port;
	m_httpServer = new httplib::Server();

	// 设置 CORS 头，允许跨域请求
	m_httpServer->set_default_headers({
		{"Access-Control-Allow-Origin", "*"},
		{"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
		{"Access-Control-Allow-Headers", "Content-Type"},
		{"Access-Control-Allow-Credentials", "true"}
	});

	// 处理 OPTIONS 预检请求
	m_httpServer->Options("/.*", [](const httplib::Request&, httplib::Response& res) {
		res.status = 204;
	});

	// GET /api/status - 获取服务器状态
	m_httpServer->Get("/api/status", [&](const httplib::Request&, httplib::Response& res) {
		json j;
		j["status"] = "running";
		j["port"] = m_serverPort;
		
		// 使用缓存的 View 数据
		if (m_cachedView) {
			j["view_available"] = true;
			// 检查前几个人物名称
			json persons = json::array();
			for (int i = 0; i < 10; i++) {
				if (m_cachedView->per1[i][0] != L'\0') {
					std::string nameUtf8 = WideToUtf8(m_cachedView->per1[i]);
					persons.push_back(nameUtf8);
				}
			}
			j["sample_persons"] = persons;
			j["person_count"] = persons.size();
		} else {
			j["view_available"] = false;
		}
		
		res.set_content(j.dump(), "application/json");
	});

	// POST /api/add_instruction - 添加指令
	m_httpServer->Post("/api/add_instruction", [&](const httplib::Request& req, httplib::Response& res) {
		try {
			json j = json::parse(req.body);

			int afterOrd = j.value("afterOrd", -1);
			int instructionId = j.value("instructionId", 0);
			std::vector<int> intData = j.value("intData", std::vector<int>());
			std::string textUtf8 = j.value("text", "");
			std::wstring text = Utf8ToWide(textUtf8);

			bool success = AddInstruction(afterOrd, instructionId,
				intData.empty() ? nullptr : intData.data(),
				(int)intData.size(),
				text.empty() ? nullptr : text.c_str());

			json response;
			response["success"] = success;
			response["message"] = success ? "指令添加成功" : "指令添加失败";
			res.set_content(response.dump(), "application/json");
		}
		catch (const std::exception& e) {
			json response;
			response["success"] = false;
			response["message"] = std::string("解析错误: ") + e.what();
			res.status = 400;
			res.set_content(response.dump(), "application/json");
		}
	});

	// POST /api/find_combo_index - 查找下拉框索引
	m_httpServer->Post("/api/find_combo_index", [&](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type");

		if (req.method == "OPTIONS") {
			res.status = 204;
			return;
		}

		try {
			json j = json::parse(req.body);

			std::string listTypeUtf8 = j.value("listType", "");
			std::string nameUtf8 = j.value("name", "");
			std::wstring listType = Utf8ToWide(listTypeUtf8);
			std::wstring name = Utf8ToWide(nameUtf8);

			int index = FindComboIndex(listType.c_str(), name.c_str());

			json response;
			response["index"] = index;
			response["found"] = (index >= 0);
			res.set_content(response.dump(), "application/json; charset=utf-8");
		}
		catch (const std::exception& e) {
			json response;
			response["index"] = -1;
			response["found"] = false;
			response["message"] = std::string("解析错误: ") + e.what();
			res.status = 400;
			res.set_content(response.dump(), "application/json; charset=utf-8");
		}
	});

	// POST /api/get_list_items - 获取列表项（前N个）
	m_httpServer->Post("/api/get_list_items", [&](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type");

		if (req.method == "OPTIONS") {
			res.status = 204;
			return;
		}

		try {
			json j = json::parse(req.body);

			std::string listTypeUtf8 = j.value("listType", "");
			int count = j.value("count", 10);
			std::wstring listType = Utf8ToWide(listTypeUtf8);

			std::wstring itemsJson = GetListItems(listType.c_str(), count);
			std::string itemsUtf8 = WideToUtf8(itemsJson);

			json response;
			response["items"] = json::parse(itemsUtf8);
			response["count"] = count;
			response["listType"] = listTypeUtf8;
			res.set_content(response.dump(), "application/json; charset=utf-8");
		}
		catch (const std::exception& e) {
			json response;
			response["items"] = json::array();
			response["message"] = std::string("解析错误: ") + e.what();
			res.status = 400;
			res.set_content(response.dump(), "application/json; charset=utf-8");
		}
	});

	// POST /api/add_instructions_batch - 批量添加指令（保留层级关系）
	m_httpServer->Post("/api/add_instructions_batch", [&](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type");

		if (req.method == "OPTIONS") {
			res.status = 204;
			return;
		}

		try {
			json j = json::parse(req.body);

			int afterOrd = j.value("afterOrd", -1);
			
			// 支持两种格式：instructions 可以是 JSON 数组对象或 JSON 字符串
			std::string instructionsJsonUtf8;
			if (j.contains("instructions") && j["instructions"].is_array()) {
				// 如果是数组对象，直接序列化
				instructionsJsonUtf8 = j["instructions"].dump();
			} else {
				// 如果是字符串，直接使用
				instructionsJsonUtf8 = j.value("instructions", "[]");
			}

			// 将JSON数组字符串转换为宽字符串
			std::wstring instructionsJson = Utf8ToWide(instructionsJsonUtf8);

			// 调用批量添加函数
			int count = AddInstructionsBatch(afterOrd, instructionsJson.c_str());

			json response;
			if (count > 0) {
				response["success"] = true;
				response["message"] = "批量添加成功";
				response["count"] = count;
			}
			else {
				response["success"] = false;
				response["message"] = "批量添加失败，请检查JSON格式和指令ID";
				response["count"] = 0;
			}
			res.set_content(response.dump(), "application/json");
		}
		catch (const std::exception& e) {
			json response;
			response["success"] = false;
			response["message"] = std::string("解析错误: ") + e.what();
			response["count"] = 0;
			res.status = 400;
			res.set_content(response.dump(), "application/json");
		}
	});

	// ==================== MCP (Model Context Protocol) Streamable HTTP ====================
	// POST /mcp - JSON-RPC 2.0 endpoint for MCP protocol
	m_httpServer->Post("/mcp", [&](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Content-Type", "application/json");
		try {
			json rpc = json::parse(req.body);
			std::string method = rpc.value("method", "");
			auto id = rpc.contains("id") ? rpc["id"] : json(nullptr);

			if (method == "initialize") {
				json result;
				result["protocolVersion"] = "2024-11-05";
				result["capabilities"]["tools"] = json::object();
				result["serverInfo"]["name"] = "cczEditor2";
				result["serverInfo"]["version"] = "0.24";
				json resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["result"] = result;
				res.set_content(resp.dump(), "application/json");
			}
			else if (method == "notifications/initialized") {
				// Notification, no response needed
				res.status = 204;
			}
			else if (method == "tools/list") {
				json tools = json::array();

				// Tool: get_tree_structure
				tools.push_back({
					{"name", "get_tree_structure"},
					{"description", "获取剧本指令树结构。返回带层级的指令列表。可指定深度限制和起始ord。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"startOrd", {{"type", "integer"}, {"description", "起始节点的ord值，-1表示从根开始"}}},
							{"maxDepth", {{"type", "integer"}, {"description", "最大递归深度，默认3"}}},
							{"maxItems", {{"type", "integer"}, {"description", "最多返回项数，默认200"}}}
						}}
					}}
				});

				// Tool: get_instruction
				tools.push_back({
					{"name", "get_instruction"},
					{"description", "获取指定ord的指令详细信息，包含id、intData、text等。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"ord", {{"type", "integer"}, {"description", "指令的ord值"}}}
						}},
						{"required", json::array({"ord"})}
					}}
				});

				// Tool: add_instruction
				tools.push_back({
					{"name", "add_instruction"},
					{"description", "在指定位置后添加一条新指令。instructionId对照表: 0x30=武将出现, 0x31=武将消失, 0x32=武将移动, 0x33=武将转向, 0x14=对话, 0x44=战斗初始化, 0x47=敌军出场设定, 等等。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"afterOrd", {{"type", "integer"}, {"description", "在此ord后面插入，-1表示末尾"}}},
							{"instructionId", {{"type", "integer"}, {"description", "指令类型ID (0-123)"}}},
							{"intData", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "整数参数数组(最多20个)"}}},
							{"text", {{"type", "string"}, {"description", "文本参数(对话、注释等)"}}}
						}},
						{"required", json::array({"afterOrd", "instructionId"})}
					}}
				});

				// Tool: add_instructions_batch
				tools.push_back({
					{"name", "add_instructions_batch"},
					{"description", "批量添加指令（保留层级关系）。每条指令格式: {id, intData?, text?, children?[]}"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"afterOrd", {{"type", "integer"}, {"description", "在此ord后面插入"}}},
							{"instructions", {{"type", "array"}, {"description", "指令数组，支持children嵌套"}}}
						}},
						{"required", json::array({"afterOrd", "instructions"})}
					}}
				});

				// Tool: modify_instruction
				tools.push_back({
					{"name", "modify_instruction"},
					{"description", "修改已有指令的参数。可修改intData和text。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"ord", {{"type", "integer"}, {"description", "要修改的指令ord"}}},
							{"intData", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "新的整数参数(全量替换)"}}},
							{"text", {{"type", "string"}, {"description", "新的文本内容"}}}
						}},
						{"required", json::array({"ord"})}
					}}
				});

				// Tool: delete_instruction
				tools.push_back({
					{"name", "delete_instruction"},
					{"description", "删除指定ord的指令（及其所有子节点）。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"ord", {{"type", "integer"}, {"description", "要删除的指令ord"}}}
						}},
						{"required", json::array({"ord"})}
					}}
				});

				// Tool: find_combo_index
				tools.push_back({
					{"name", "find_combo_index"},
					{"description", "在下拉框列表中查找名称对应的索引。listType: person/item/direction/job/magic/skill/face/movie/object/terrain/camp/gesture/weather/policy/condition"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"listType", {{"type", "string"}, {"description", "列表类型"}}},
							{"name", {{"type", "string"}, {"description", "要查找的名称"}}}
						}},
						{"required", json::array({"listType", "name"})}
					}}
				});

				// Tool: get_list_items
				tools.push_back({
					{"name", "get_list_items"},
					{"description", "获取指定列表的前N个项目名称。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"listType", {{"type", "string"}, {"description", "列表类型(person/item/job/magic/skill等)"}}},
							{"count", {{"type", "integer"}, {"description", "获取数量，默认50"}}}
						}},
						{"required", json::array({"listType"})}
					}}
				});

				// Tool: search_instructions
				tools.push_back({
					{"name", "search_instructions"},
					{"description", "搜索指令。可按类型ID、文本内容、intData值搜索。"},
					{"inputSchema", {
						{"type", "object"},
						{"properties", {
							{"instructionId", {{"type", "integer"}, {"description", "按指令类型ID过滤，-1不过滤"}}},
							{"text", {{"type", "string"}, {"description", "按文本内容模糊匹配"}}},
							{"intDataIndex", {{"type", "integer"}, {"description", "检查intData的哪个索引"}}},
							{"intDataValue", {{"type", "integer"}, {"description", "该索引位置的值"}}},
							{"maxResults", {{"type", "integer"}, {"description", "最多返回数量，默认50"}}}
						}}
					}}
				});

				// Tool: get_current_selection
				tools.push_back({
					{"name", "get_current_selection"},
					{"description", "获取当前选中的指令信息。"},
					{"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
				});

				json result;
				result["tools"] = tools;
				json resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["result"] = result;
				res.set_content(resp.dump(), "application/json");
			}
			else if (method == "tools/call") {
				json params = rpc.value("params", json::object());
				std::string toolName = params.value("name", "");
				json args = params.value("arguments", json::object());

				json toolResult;
				bool isError = false;

				// 获取View
				CcczEditor2View* pView = m_cachedView;
				if (!pView) {
					POSITION vp = GetFirstViewPosition();
					if (vp) pView = (CcczEditor2View*)GetNextView(vp);
					m_cachedView = pView;
				}
				if (!pView) {
					toolResult["text"] = "错误: 无法获取视图，请先打开剧本文件";
					isError = true;
				}
				else if (toolName == "get_tree_structure") {
					int startOrd = args.value("startOrd", -1);
					int maxDepth = args.value("maxDepth", 3);
					int maxItems = args.value("maxItems", 200);

					CTreeCtrl& tree = pView->GetTreeCtrl();
					HTREEITEM startItem = tree.GetRootItem();

					if (startOrd >= 0) {
						startItem = pView->recurSearchOrd(tree.GetRootItem(), startOrd);
						if (!startItem) {
							toolResult["text"] = "错误: 找不到指定ord的节点";
							isError = true;
						}
					}

					if (!isError) {
						json items = json::array();
						int count = 0;

						// 递归收集树结构的lambda
						std::function<void(HTREEITEM, int)> collectTree = [&](HTREEITEM item, int depth) {
							if (!item || count >= maxItems || depth > maxDepth) return;
							while (item && count < maxItems) {
								ItemData* data = (ItemData*)tree.GetItemData(item);
								if (data) {
									json node;
									node["ord"] = data->ord;
									node["id"] = data->id;

									// 显示文本
									CString itemText = tree.GetItemText(item);
									node["display"] = WideToUtf8(std::wstring(itemText));

									// 是否有子节点
									bool hasChildren = (tree.GetChildItem(item) != NULL);
									node["hasChildren"] = hasChildren;

									// depth信息
									node["depth"] = depth;

									items.push_back(node);
									count++;

									// 递归子节点
									if (hasChildren && depth < maxDepth) {
										collectTree(tree.GetChildItem(item), depth + 1);
									}
								}
								item = tree.GetNextSiblingItem(item);
							}
						};

						if (startOrd >= 0) {
							// 从指定节点开始，包含自身和子节点
							ItemData* data = (ItemData*)tree.GetItemData(startItem);
							if (data) {
								json node;
								node["ord"] = data->ord;
								node["id"] = data->id;
								CString itemText = tree.GetItemText(startItem);
								node["display"] = WideToUtf8(std::wstring(itemText));
								node["hasChildren"] = (tree.GetChildItem(startItem) != NULL);
								node["depth"] = 0;
								items.push_back(node);
								count++;
							}
							collectTree(tree.GetChildItem(startItem), 1);
						}
						else {
							collectTree(tree.GetRootItem(), 0);
						}

						json out;
						out["items"] = items;
						out["totalReturned"] = count;
						out["truncated"] = (count >= maxItems);
						toolResult["text"] = out.dump(2);
					}
				}
				else if (toolName == "get_instruction") {
					int ord = args.value("ord", -1);
					CTreeCtrl& tree = pView->GetTreeCtrl();
					HTREEITEM item = pView->recurSearchOrd(tree.GetRootItem(), ord);

					if (!item) {
						toolResult["text"] = "错误: 找不到ord=" + std::to_string(ord) + "的指令";
						isError = true;
					}
					else {
						ItemData* data = (ItemData*)tree.GetItemData(item);
						json out;
						out["ord"] = data->ord;
						out["id"] = data->id;

						// 指令类型名称
						if (data->id >= 0 && data->id < 124) {
							out["typeName"] = WideToUtf8(std::wstring(pView->code[data->id]));
						}
						else if (data->id == -1) { out["typeName"] = "Scene"; }
						else if (data->id == -2) { out["typeName"] = "Section"; }

						// intData
						json intArr = json::array();
						for (int i = 0; i < (int)data->int_data.size(); i++) {
							intArr.push_back(data->int_data[i]);
						}
						out["intData"] = intArr;

						// text
						if (data->long_char_data && wcslen(data->long_char_data) > 0) {
							out["text"] = WideToUtf8(std::wstring(data->long_char_data));
						}

						// 显示文本
						CString displayText = tree.GetItemText(item);
						out["display"] = WideToUtf8(std::wstring(displayText));

						// 父子关系
						HTREEITEM parent = tree.GetParentItem(item);
						if (parent) {
							ItemData* pData = (ItemData*)tree.GetItemData(parent);
							if (pData) out["parentOrd"] = pData->ord;
						}
						out["hasChildren"] = (tree.GetChildItem(item) != NULL);

						toolResult["text"] = out.dump(2);
					}
				}
				else if (toolName == "add_instruction") {
					int afterOrd = args.value("afterOrd", -1);
					int instructionId = args.value("instructionId", 0);
					std::vector<int> intData = args.value("intData", std::vector<int>());
					std::string textUtf8 = args.value("text", "");
					std::wstring text = Utf8ToWide(textUtf8);

					bool success = AddInstruction(afterOrd, instructionId,
						intData.empty() ? nullptr : intData.data(),
						(int)intData.size(),
						text.empty() ? nullptr : text.c_str());

					json out;
					out["success"] = success;
					out["message"] = success ? "指令添加成功" : "指令添加失败";
					toolResult["text"] = out.dump();
				}
				else if (toolName == "add_instructions_batch") {
					int afterOrd = args.value("afterOrd", -1);
					std::string instructionsUtf8;
					if (args.contains("instructions") && args["instructions"].is_array()) {
						instructionsUtf8 = args["instructions"].dump();
					}
					else {
						instructionsUtf8 = args.value("instructions", "[]");
					}
					std::wstring instructionsJson = Utf8ToWide(instructionsUtf8);
					int cnt = AddInstructionsBatch(afterOrd, instructionsJson.c_str());

					json out;
					out["success"] = (cnt > 0);
					out["count"] = cnt;
					toolResult["text"] = out.dump();
				}
				else if (toolName == "modify_instruction") {
					int ord = args.value("ord", -1);
					CTreeCtrl& tree = pView->GetTreeCtrl();
					HTREEITEM item = pView->recurSearchOrd(tree.GetRootItem(), ord);

					if (!item) {
						toolResult["text"] = "错误: 找不到ord=" + std::to_string(ord) + "的指令";
						isError = true;
					}
					else {
						ItemData* data = (ItemData*)tree.GetItemData(item);

						if (args.contains("intData") && args["intData"].is_array()) {
							auto newData = args["intData"].get<std::vector<int>>();
							int cnt = min((int)newData.size(), (int)data->int_data.size());
							for (int i = 0; i < cnt; i++)
								data->int_data[i] = newData[i];
						}
						if (args.contains("text")) {
							std::string textUtf8 = args["text"].get<std::string>();
							std::wstring text = Utf8ToWide(textUtf8);
							if (data->long_char_data) delete[] data->long_char_data;
							int len = (int)text.size() + 1;
							data->long_char_data = new wchar_t[len];
							wcscpy_s(data->long_char_data, len, text.c_str());
						}

						pView->UpdateShow(item);
						this->OnModified(TRUE);

						json out;
						out["success"] = true;
						out["message"] = "指令修改成功";
						toolResult["text"] = out.dump();
					}
				}
				else if (toolName == "delete_instruction") {
					int ord = args.value("ord", -1);
					CTreeCtrl& tree = pView->GetTreeCtrl();
					HTREEITEM item = pView->recurSearchOrd(tree.GetRootItem(), ord);

					if (!item) {
						toolResult["text"] = "错误: 找不到ord=" + std::to_string(ord) + "的指令";
						isError = true;
					}
					else {
						// 递归删除子节点的ItemData
						std::function<void(HTREEITEM)> recurDelete = [&](HTREEITEM h) {
							HTREEITEM child = tree.GetChildItem(h);
							while (child) {
								HTREEITEM next = tree.GetNextSiblingItem(child);
								recurDelete(child);
								child = next;
							}
							ItemData* d = (ItemData*)tree.GetItemData(h);
							if (d) {
								if (d->long_char_data) delete[] d->long_char_data;
								delete d;
							}
						};
						recurDelete(item);
						tree.DeleteItem(item);
						this->OnModified(TRUE);

						json out;
						out["success"] = true;
						out["message"] = "指令删除成功";
						toolResult["text"] = out.dump();
					}
				}
				else if (toolName == "find_combo_index") {
					std::string listTypeUtf8 = args.value("listType", "");
					std::string nameUtf8 = args.value("name", "");
					std::wstring listType = Utf8ToWide(listTypeUtf8);
					std::wstring name = Utf8ToWide(nameUtf8);
					int index = FindComboIndex(listType.c_str(), name.c_str());

					json out;
					out["index"] = index;
					out["found"] = (index >= 0);
					toolResult["text"] = out.dump();
				}
				else if (toolName == "get_list_items") {
					std::string listTypeUtf8 = args.value("listType", "");
					int count = args.value("count", 50);
					std::wstring listType = Utf8ToWide(listTypeUtf8);
					std::wstring itemsJson = GetListItems(listType.c_str(), count);
					std::string itemsUtf8 = WideToUtf8(itemsJson);

					json out;
					out["items"] = json::parse(itemsUtf8);
					out["listType"] = listTypeUtf8;
					toolResult["text"] = out.dump(2);
				}
				else if (toolName == "search_instructions") {
					int searchId = args.value("instructionId", -1);
					std::string searchText = args.value("text", "");
					std::wstring searchTextW = Utf8ToWide(searchText);
					int intIdx = args.value("intDataIndex", -1);
					int intVal = args.value("intDataValue", 0);
					int maxResults = args.value("maxResults", 50);

					CTreeCtrl& tree = pView->GetTreeCtrl();
					json results = json::array();
					int found = 0;

					std::function<void(HTREEITEM)> searchTree = [&](HTREEITEM item) {
						while (item && found < maxResults) {
							ItemData* data = (ItemData*)tree.GetItemData(item);
							if (data) {
								bool match = true;
								if (searchId >= 0 && data->id != searchId) match = false;
								if (match && !searchText.empty()) {
									if (!data->long_char_data || wcsstr(data->long_char_data, searchTextW.c_str()) == nullptr)
										match = false;
								}
								if (match && intIdx >= 0 && intIdx < (int)data->int_data.size()) {
									if (data->int_data[intIdx] != intVal) match = false;
								}
								if (match) {
									json node;
									node["ord"] = data->ord;
									node["id"] = data->id;
									CString txt = tree.GetItemText(item);
									node["display"] = WideToUtf8(std::wstring(txt));
									if (data->long_char_data && wcslen(data->long_char_data) > 0)
										node["text"] = WideToUtf8(std::wstring(data->long_char_data));
									results.push_back(node);
									found++;
								}
							}
							HTREEITEM child = tree.GetChildItem(item);
							if (child) searchTree(child);
							item = tree.GetNextSiblingItem(item);
						}
					};
					searchTree(tree.GetRootItem());

					json out;
					out["results"] = results;
					out["count"] = found;
					toolResult["text"] = out.dump(2);
				}
				else if (toolName == "get_current_selection") {
					CTreeCtrl& tree = pView->GetTreeCtrl();
					HTREEITEM sel = tree.GetSelectedItem();
					if (!sel) {
						toolResult["text"] = "{\"selected\":false}";
					}
					else {
						ItemData* data = (ItemData*)tree.GetItemData(sel);
						json out;
						out["selected"] = true;
						out["ord"] = data->ord;
						out["id"] = data->id;
						CString txt = tree.GetItemText(sel);
						out["display"] = WideToUtf8(std::wstring(txt));
						if (data->id >= 0 && data->id < 124)
							out["typeName"] = WideToUtf8(std::wstring(pView->code[data->id]));
						toolResult["text"] = out.dump(2);
					}
				}
				else {
					toolResult["text"] = "未知工具: " + toolName;
					isError = true;
				}

				// 构建JSON-RPC响应
				json resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				json result;
				result["content"] = json::array();
				result["content"].push_back({
					{"type", "text"},
					{"text", toolResult.value("text", "")}
				});
				if (isError) result["isError"] = true;
				resp["result"] = result;
				res.set_content(resp.dump(), "application/json");
			}
			else {
				// Unknown method
				json resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["error"] = {{"code", -32601}, {"message", "Method not found: " + method}};
				res.set_content(resp.dump(), "application/json");
			}
		}
		catch (const std::exception& e) {
			json resp;
			resp["jsonrpc"] = "2.0";
			resp["id"] = nullptr;
			resp["error"] = {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}};
			res.status = 400;
			res.set_content(resp.dump(), "application/json");
		}
	});

	// 在后台线程中启动服务器
	m_serverThread = new std::thread([this]() {
		m_httpServer->listen("0.0.0.0", m_serverPort);
	});

	m_serverRunning = true;

	CString msg;
	msg.Format(L"HTTP服务器已启动，端口: %d\nMCP端点: http://localhost:%d/mcp", port, port);
	AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
}

void CcczEditor2Doc::StopHttpServer()
{
	if (!m_serverRunning) {
		AfxMessageBox(L"HTTP服务器未运行", MB_OK | MB_ICONINFORMATION);
		return;
	}

	if (m_httpServer) {
		m_httpServer->stop();
	}

	if (m_serverThread) {
		m_serverThread->join();
		delete m_serverThread;
		m_serverThread = nullptr;
	}

	delete m_httpServer;
	m_httpServer = nullptr;
	m_serverRunning = false;

	AfxMessageBox(L"HTTP服务器已停止", MB_OK | MB_ICONINFORMATION);
}

bool CcczEditor2Doc::IsHttpServerRunning() const
{
	return m_serverRunning;
}

void CcczEditor2Doc::OnStartHttpServer()
{
	StartHttpServer(8080);
}

void CcczEditor2Doc::OnStopHttpServer()
{
	StopHttpServer();
}