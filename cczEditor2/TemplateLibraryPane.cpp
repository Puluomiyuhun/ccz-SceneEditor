#include "pch.h"
#include <functional>
#include "PreviewPageFont.h"
#include "resource.h"
#include "TemplateLibraryPane.h"
#include "MainFrm.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"
#include "IndexedComboSearch.h"

namespace {
class MetadataDialog : public CDialog {
public:
  CString name, notes;
  INT_PTR Show(CWnd *owner) {
    struct T {
      DLGTEMPLATE d;
      WORD menu, cls, title;
    };
    T t{};
    t.d.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    t.d.cx = 280;
    t.d.cy = 140;
    InitModalIndirect(&t.d, owner);
    return DoModal();
  }

protected:
  BOOL OnInitDialog() override {
    CDialog::OnInitDialog();
    SetWindowText(L"模板名称与备注");
    CRect r;
    GetClientRect(&r);
    font_.CreatePointFont(90, L"Microsoft YaHei");
    label1_.Create(L"名称", WS_CHILD | WS_VISIBLE, CRect(12, 14, 58, 36), this);
    name_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                     ES_AUTOHSCROLL,
                 CRect(64, 10, r.right - 12, 38), this, 301);
    name_.SetLimitText(256);
    name_.SetWindowText(name);
    label2_.Create(L"备注", WS_CHILD | WS_VISIBLE, CRect(12, 48, 58, 70), this);
    notes_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                      ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                  CRect(64, 46, r.right - 12, r.bottom - 50), this, 302);
    notes_.SetLimitText(4096);
    notes_.SetWindowText(notes);
    ok_.Create(
        L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        CRect(r.right - 190, r.bottom - 38, r.right - 104, r.bottom - 10), this,
        IDOK);
    cancel_.Create(
        L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        CRect(r.right - 98, r.bottom - 38, r.right - 12, r.bottom - 10), this,
        IDCANCEL);
    for (CWnd *w : {(CWnd *)&label1_, (CWnd *)&label2_, (CWnd *)&name_,
                    (CWnd *)&notes_, (CWnd *)&ok_, (CWnd *)&cancel_})
      w->SetFont(&font_);
    name_.SetFocus();
    return FALSE;
  }
  void OnOK() override {
    name_.GetWindowText(name);
    name.Trim();
    notes_.GetWindowText(notes);
    if (name.IsEmpty()) {
      AfxMessageBox(L"请填写模板名称");
      return;
    }
    CDialog::OnOK();
  }

private:
  CEdit name_, notes_;
  CStatic label1_, label2_;
  CButton ok_, cancel_;
  CFont font_;
};
void FillPreview(CTreeCtrl &tree,
                 const std::vector<ccz::templates::Node> &nodes,
                 HTREEITEM parent) {
  for (auto &n : nodes) {
    auto item = tree.InsertItem(n.label.empty() ? L"指令" : n.label.c_str(),
                                parent, TVI_LAST);
    FillPreview(tree, n.children, item);
    if (parent == TVI_ROOT)
      tree.Expand(item, TVE_EXPAND);
  }
}
} // namespace
BEGIN_MESSAGE_MAP(CTemplateLibraryPane, CDockablePane)
ON_WM_CREATE()
ON_WM_SIZE()
ON_WM_TIMER()
ON_WM_DESTROY()
ON_WM_CONTEXTMENU()
ON_WM_PAINT()
ON_EN_CHANGE(201, OnSearch)
ON_NOTIFY(LVN_ITEMCHANGED, 202, OnSelected)
ON_BN_CLICKED(204, OnInsert)
ON_BN_CLICKED(205, OnEdit)
ON_BN_CLICKED(206, OnDelete)
ON_BN_CLICKED(207, OnUndo)
END_MESSAGE_MAP()
void CTemplateLibraryPane::OnPaint() {
  CPaintDC dc(this);
  CRect client; GetClientRect(&client);
  dc.FillSolidRect(client, ::GetSysColor(COLOR_BTNFACE));
}
void CTemplateLibraryPane::OnContextMenu(CWnd* source, CPoint point) {
  if (source != &table_ && source != &preview_) return;
  if (source == &table_ && point.x != -1) {
    CPoint local = point; table_.ScreenToClient(&local);
    LVHITTESTINFO hit{}; hit.pt = local;
    const int row = table_.HitTest(&hit);
    if (row >= 0) table_.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  }
  if (point.x == -1) { CRect rect; source->GetWindowRect(rect); point = rect.TopLeft(); }
  CMenu menu; menu.CreatePopupMenu();
  CMenu fonts;AppendPageFontMenu(menu,fonts,L"Templates",0xec00);
  const UINT flags = MF_STRING | (Selected() ? 0 : MF_GRAYED);
  menu.AppendMenu(flags, 1, L"插入");
  menu.AppendMenu(flags | (diagnostic_.empty() ? 0 : MF_GRAYED), 2, L"修改");
  auto* view = ActiveView();
  menu.AppendMenu(MF_STRING | (view && view->CanUndoTemplateInsertion() ? 0 : MF_GRAYED), 3, L"撤回");
  const UINT action = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this);
  if(action==0xec00 || (action>=0xec08 && action<=0xec0e)) {
    AfxGetApp()->WriteProfileInt(L"PreviewPageFont",L"Templates",action==0xec00?0:action-0xec00);
    ApplyPageFont(*this,L"Templates",page_font_);CRect r;GetClientRect(r);OnSize(SIZE_RESTORED,r.Width(),r.Height());return;
  }

  if (action == 1) OnInsert(); else if (action == 2) OnEdit(); else if (action == 3) OnUndo();
}
int CTemplateLibraryPane::OnCreate(LPCREATESTRUCT cs) {
  if (CDockablePane::OnCreate(cs) == -1)
    return -1;
  font_.CreatePointFont(90, L"Microsoft YaHei");
  search_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                     ES_AUTOHSCROLL,
                 CRect(), this, 201);
  search_.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"检索模板名称 / 备注");
  table_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
                    LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                CRect(), this, 202);
  table_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                          LVS_EX_DOUBLEBUFFER);
  table_.InsertColumn(0, L"序号", LVCFMT_LEFT, 48);
  table_.InsertColumn(1, L"层级", LVCFMT_LEFT, 65);
  table_.InsertColumn(2, L"名称", LVCFMT_LEFT, 135);
  table_.InsertColumn(3, L"备注", LVCFMT_LEFT, 170);
  preview_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                      TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT,
                  CRect(), this, 203);
  insert_.Create(L"插入模板", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(), this,
                 204);
  edit_.Create(L"修改信息", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(), this,
               205);
  remove_.Create(L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(), this,
                 206);
  undo_.Create(L"撤回插入", WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(), this,
               207);
  status_.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(), this, 208);
  for (CWnd *w :
       {(CWnd *)&search_, (CWnd *)&table_, (CWnd *)&preview_, (CWnd *)&insert_,
        (CWnd *)&edit_, (CWnd *)&remove_, (CWnd *)&undo_, (CWnd *)&status_})
    w->SetFont(&font_);
  Reload(false);
  Fill();
  ApplyPageFont(*this,L"Templates",page_font_);
  SetTimer(1, 1500, nullptr);
  return 0;
}
void CTemplateLibraryPane::OnSize(UINT type, int cx, int cy) {
  CDockablePane::OnSize(type, cx, cy);
  if (!table_.GetSafeHwnd())
    return;
  int width = (std::max)(0, cx - 16), height = (std::max)(0, cy - 128);
  int tableHeight = height / 2;
  search_.MoveWindow(8, 8, width, 26);
  table_.MoveWindow(8, 40, width, tableHeight);
  preview_.MoveWindow(8, 46 + tableHeight, width, height - tableHeight);
  int y = (std::max)(46, cy - 72);
  int buttonWidth = (std::max)(0, (width - 9) / 4);
  CButton *buttons[] = {&insert_, &edit_, &remove_, &undo_};
  for (int i = 0; i < 4; ++i)
    buttons[i]->MoveWindow(8 + i * (buttonWidth + 3), y, buttonWidth, 28);
  status_.MoveWindow(8, y + 34, width, 34);
  table_.SetColumnWidth(2, (std::max)(100, (width - 119) / 2));
  table_.SetColumnWidth(
      3, (std::max)(100, width - 119 - table_.GetColumnWidth(2)));
}
CcczEditor2View *CTemplateLibraryPane::ActiveView() {
  auto frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
  auto child = frame ? frame->MDIGetActive() : nullptr;
  return child ? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView())
               : nullptr;
}
const ccz::templates::Entry *CTemplateLibraryPane::Selected() const {
  int row = table_.GetNextItem(-1, LVNI_SELECTED);
  if (row < 0 || row >= (int)visible_ids_.size())
    return nullptr;
  for (auto &e : library_.entries)
    if (e.uid == visible_ids_[row])
      return &e;
  return nullptr;
}
void CTemplateLibraryPane::Reload(bool report) {
  std::pair<std::uint64_t, std::uint64_t> stamp{};
  try {
    stamp = store_.Stamp();
  } catch (...) {
  }
  if (!report && stamp_known_ && stamp == file_stamp_)
    return;
  ccz::templates::Library next;
  std::wstring message;
  bool loaded = store_.Load(next, message);
  if (loaded) {
    file_stamp_ = stamp;
    stamp_known_ = true;
  }
  bool changed = loaded && (next.revision != library_.revision ||
                            next.entries.size() != library_.entries.size());
  if (loaded)
    library_ = std::move(next);
  if (changed)
    Fill();
  diagnostic_ = message;
  status_.SetWindowText(message.empty()
                            ? L"模板全局保存 · 插入到当前选中位置之后"
                            : message.c_str());
  if (report && !message.empty())
    AfxMessageBox(message.c_str(), MB_ICONWARNING);
}
void CTemplateLibraryPane::Fill() {
  std::wstring selected;
  auto old = Selected();
  if (old)
    selected = old->uid;
  CString query;
  search_.GetWindowText(query);
  filling_ = true;
  table_.DeleteAllItems();
  visible_ids_.clear();
  int selection = -1;
  for (std::size_t i = 0; i < library_.entries.size(); ++i) {
    auto &e = library_.entries[i];
    if (!ccz::editor::ContainsInsensitive(e.name, (LPCWSTR)query) &&
        !ccz::editor::ContainsInsensitive(e.notes, (LPCWSTR)query))
      continue;
    int row = table_.GetItemCount();
    CString ordinal;
    ordinal.Format(L"%u", (unsigned)i + 1);
    table_.InsertItem(row, ordinal);
    table_.SetItemText(row, 1, ccz::templates::LevelName(e.level));
    table_.SetItemText(row, 2, e.name.c_str());
    table_.SetItemText(row, 3, e.notes.c_str());
    visible_ids_.push_back(e.uid);
    if (e.uid == selected)
      selection = row;
  }
  if (selection < 0 && table_.GetItemCount())
    selection = 0;
  if (selection >= 0)
    table_.SetItemState(selection, LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
  filling_ = false;
  LRESULT ignored;
  OnSelected(nullptr, &ignored);
}
void CTemplateLibraryPane::OnSelected(NMHDR *, LRESULT *result) {
  *result = 0;
  if (filling_)
    return;
  preview_.DeleteAllItems();
  auto entry = Selected();
  if (entry)
    FillPreview(preview_, entry->roots, TVI_ROOT);
  insert_.EnableWindow(entry != nullptr);
  edit_.EnableWindow(entry != nullptr);
  remove_.EnableWindow(entry != nullptr);
}
void CTemplateLibraryPane::OnSearch() {
  if (table_.GetSafeHwnd())
    Fill();
}
void CTemplateLibraryPane::OnTimer(UINT_PTR id) {
  if (id == 1) {
    if (IsWindowVisible())
      Reload(false);
    auto view = ActiveView();
    undo_.EnableWindow(view && view->CanUndoTemplateInsertion());
  }
  CDockablePane::OnTimer(id);
}
void CTemplateLibraryPane::OnDestroy() {
  KillTimer(1);
  CDockablePane::OnDestroy();
}
void CTemplateLibraryPane::SaveSelection(CcczEditor2View &view) {
  ccz::templates::Entry entry;
  CString error;
  if (!view.CaptureTemplateSelection(entry, error)) {
    AfxMessageBox(error, MB_ICONWARNING);
    return;
  }
  MetadataDialog dialog;
  if (dialog.Show(this) != IDOK)
    return;
  try {
    entry.uid = ccz::templates::NewIdentity();
  } catch (...) {
    AfxMessageBox(L"无法生成模板编号，请重试", MB_ICONERROR);
    return;
  }
  entry.name = (LPCWSTR)dialog.name;
  entry.notes = (LPCWSTR)dialog.notes;
  std::wstring diagnostic;
  if (!store_.Save(entry, false, library_, diagnostic)) {
    AfxMessageBox(diagnostic.c_str(), MB_ICONERROR);
    Reload(false);
    return;
  }
  search_.SetWindowText(L"");
  Fill();
  for (int row = 0; row < (int)visible_ids_.size(); ++row)
    if (visible_ids_[row] == entry.uid)
      table_.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
  ShowPane(TRUE, FALSE, TRUE);
  status_.SetWindowText(L"模板已保存");
}
void CTemplateLibraryPane::OnInsert() {
  Reload(false);
  auto entry = Selected();
  if (!entry)
    return;
  auto copy = *entry;
  auto view = ActiveView();
  if (!view) {
    AfxMessageBox(L"请先选择需要插入新指令的位置");
    return;
  }
  CString error;
  if (!view->InsertTemplate(copy, error))
    AfxMessageBox(error, MB_ICONWARNING);
  undo_.EnableWindow(view->CanUndoTemplateInsertion());
}
void CTemplateLibraryPane::OnEdit() {
  auto selected = Selected();
  if (!selected)
    return;
  auto copy = *selected;
  MetadataDialog dialog;
  dialog.name = copy.name.c_str();
  dialog.notes = copy.notes.c_str();
  if (dialog.Show(this) != IDOK)
    return;
  copy.name = (LPCWSTR)dialog.name;
  copy.notes = (LPCWSTR)dialog.notes;
  std::wstring diagnostic;
  if (!store_.Save(copy, true, library_, diagnostic))
    AfxMessageBox(diagnostic.c_str(), MB_ICONERROR);
  Fill();
}
void CTemplateLibraryPane::OnDelete() {
  auto selected = Selected();
  if (!selected)
    return;
  auto copy = *selected;
  if (AfxMessageBox(L"删除选中的模板？", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  std::wstring diagnostic;
  if (!store_.Remove(copy, library_, diagnostic))
    AfxMessageBox(diagnostic.c_str(), MB_ICONERROR);
  Fill();
}
void CTemplateLibraryPane::OnUndo() {
  auto view = ActiveView();
  if (!view)
    return;
  CString error;
  if (!view->UndoTemplateInsertion(error))
    AfxMessageBox(error, MB_ICONWARNING);
  undo_.EnableWindow(view->CanUndoTemplateInsertion());
}
void CTemplateLibraryPane::OnUpdateCmdUI(CFrameWnd *, BOOL) {
  if (!table_.GetSafeHwnd())
    return;
  const bool selected = Selected() != nullptr;
  insert_.EnableWindow(selected);
  edit_.EnableWindow(selected && diagnostic_.empty());
  remove_.EnableWindow(selected && diagnostic_.empty());
  auto view = ActiveView();
  undo_.EnableWindow(view && view->CanUndoTemplateInsertion());
}
