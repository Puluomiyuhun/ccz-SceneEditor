
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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif
#include "cczEditor2View.h"
#include "MainFrm.h"

// CcczEditor2Doc

IMPLEMENT_DYNCREATE(CcczEditor2Doc, CDocument)

BEGIN_MESSAGE_MAP(CcczEditor2Doc, CDocument)
	ON_BN_CLICKED(ID_EXPORT_TXT, &CcczEditor2Doc::OnExportTxt)
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
						int wlen = WideCharToMultiByte(CP_OEMCP, 0, tmp, -1, NULL, 0, NULL, FALSE);
						WideCharToMultiByte(CP_OEMCP, 0, tmp, -1, tmp2, wlen, NULL, FALSE);
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
		int len = WideCharToMultiByte(CP_OEMCP, 0, tree.GetItemText(cur), -1, NULL, 0, NULL, FALSE);
		WideCharToMultiByte(CP_OEMCP, 0, tree.GetItemText(cur), -1, tmp, len, NULL, FALSE);
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