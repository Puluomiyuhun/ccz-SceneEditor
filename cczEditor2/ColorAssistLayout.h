#pragma once
#include <afxwin.h>
#include <vector>
#include <algorithm>
#include "resource.h"
inline void CreateColorAssistButton(CDialog& dialog,CButton& button,CEdit& edit) {
 CRect client;dialog.GetClientRect(client);
 CRect anchor;dialog.GetDlgItem(IDC_WRAP_BYTES)->GetWindowRect(anchor);dialog.ScreenToClient(anchor);
 if(!button.GetSafeHwnd()) button.Create(L"换色辅助",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
   CRect(0,anchor.top,88,anchor.bottom),&dialog,0xee01);
 button.SetFont(edit.GetFont());
 std::vector<CWnd*> controls;
 for(int id:{IDC_WRAP_ENABLE,IDC_WRAP_BYTES_LABEL,IDC_WRAP_BYTES,IDC_WRAP_MODE}) {
  auto* control=dialog.GetDlgItem(id);
  if(control && (control->GetStyle()&WS_VISIBLE)) controls.push_back(control);
 }
 controls.push_back(&button);
 std::vector<CRect> bounds;int total=0,gap=6,maxHeight=anchor.Height();
 for(auto* control:controls) {CRect rect;control->GetWindowRect(rect);dialog.ScreenToClient(rect);
  bounds.push_back(rect);total+=rect.Width();maxHeight=(std::max)(maxHeight,rect.Height());}
 total+=gap*static_cast<int>(controls.size()-1);
 // Grow only if the complete row cannot fit. No new copy of the info-mode button.
 if(total+12>client.Width()) {CRect window;dialog.GetWindowRect(window);
  dialog.SetWindowPos(nullptr,0,0,window.Width()+total+12-client.Width(),window.Height(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  dialog.GetClientRect(client);}
 int x=(client.Width()-total)/2;
 for(std::size_t i=0;i<controls.size();++i) {
  controls[i]->MoveWindow(x,anchor.top+(maxHeight-bounds[i].Height())/2,bounds[i].Width(),bounds[i].Height());
  x+=bounds[i].Width()+gap;
 }
 // Keep the distinct confirmation row centered within the possibly wider dialog.
 CRect ok,cancel;dialog.GetDlgItem(IDOK)->GetWindowRect(ok);dialog.ScreenToClient(ok);
 dialog.GetDlgItem(IDCANCEL)->GetWindowRect(cancel);dialog.ScreenToClient(cancel);
 x=(client.Width()-ok.Width()-cancel.Width()-12)/2;
 dialog.GetDlgItem(IDOK)->MoveWindow(x,ok.top,ok.Width(),ok.Height());
 dialog.GetDlgItem(IDCANCEL)->MoveWindow(x+ok.Width()+12,cancel.top,cancel.Width(),cancel.Height());
}
