#pragma once
#include "JumpTransfer.h"
#include <functional>
namespace {
class TransferTargetDialog : public CDialog {
  CEdit edit; CStatic label; CButton ok, cancel;
  const std::set<int>& ids;
  int originalTarget;
public:
  int value = 0;
  explicit TransferTargetDialog(const std::set<int>& valid, int original) : ids(valid), originalTarget(original) {}
  INT_PTR Run(CWnd* owner) {
    struct Layout { DLGTEMPLATE dialog; WORD menu, windowClass, title; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    layout.dialog.cx = 200; layout.dialog.cy = 75;
    InitModalIndirect(&layout.dialog, owner);
    return DoModal();
  }
  BOOL OnInitDialog() override {
    CDialog::OnInitDialog(); CString title; title.Format(L"修改跳转目标（原目标 %d）",originalTarget); SetWindowTextW(title);
    CRect r; GetClientRect(&r);
    label.Create(L"目标 ID", WS_CHILD|WS_VISIBLE, CRect(12,15,80,40), this);
    edit.Create(WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,
                CRect(85,12,r.Width()-12,40), this, 100);
    ok.Create(L"确定", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
              CRect(r.Width()-176,r.Height()-38,r.Width()-96,r.Height()-10),this,IDOK);
    cancel.Create(L"取消", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
                  CRect(r.Width()-88,r.Height()-38,r.Width()-8,r.Height()-10),this,IDCANCEL);
    CFont* font = AfxGetMainWnd()->GetFont();
    if (!font) font = CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT));
    label.SetFont(font); edit.SetFont(font); ok.SetFont(font); cancel.SetFont(font);
    edit.SetWindowTextW(L"0"); edit.SetSel(0,-1); edit.SetFocus(); CenterWindow(); return FALSE;
  }
  void OnOK() override {
    CString text; edit.GetWindowTextW(text);
    if (!ccz::editor::ParseTransferTarget(std::wstring(text.GetString()),ids,value)) {
      MessageBox(L"目标 ID 不存在",L"修改跳转目标",MB_OK|MB_ICONINFORMATION);
      edit.SetFocus(); edit.SetSel(0,-1); return;
    }
    CDialog::OnOK();
  }
};
bool ResolveTransferExternal(CcczEditor2View& view, const std::set<int>& targets,
                             bool templateInsertion, std::map<int,int>& result) {
  if (targets.empty()) return true;
  int answer = view.MessageBox(
    L"存在无条件跳转指令指向复制/模板范围外，请选择是否修改跳转目标",
    L"无条件跳转", MB_YESNOCANCEL|MB_ICONQUESTION);
  if (answer == IDCANCEL) return false;
  std::set<int> ids;
  auto& tree = view.GetTreeCtrl();
  std::function<void(HTREEITEM)> visit = [&](HTREEITEM item) {
    for (;item;item=tree.GetNextSiblingItem(item)) {
      auto data = reinterpret_cast<ItemData*>(tree.GetItemData(item));
      if (data && data->id >= 0) ids.insert(data->ord);
      visit(tree.GetChildItem(item));
    }
  };
  visit(tree.GetRootItem());
  for (int target : targets) {
    int value = templateInsertion ? 0 : target;
    if (answer == IDYES) {
      TransferTargetDialog dialog(ids,target);
      if (dialog.Run(&view) != IDOK) return false;
      value = dialog.value;
    }
    result.emplace(target,value);
  }
  return true;
}
}
