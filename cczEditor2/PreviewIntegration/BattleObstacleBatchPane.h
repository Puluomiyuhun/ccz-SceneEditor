#pragma once
#include <afxwin.h>
#include <array>
#include <algorithm>
#include <functional>
#include "BattleMapOperations.h"
class CBattleObstacleBatchPane : public CWnd {
public:
 using Handler=std::function<bool(const ccz::editor_preview::BattleObstacleDraft&,const std::array<bool,5>&)>;
 void Open(CWnd* owner,const ccz::editor_preview::BattleObstacleDraft& value,const std::vector<std::wstring>& terrains,Handler handler) {
  if(!GetSafeHwnd()) {
   auto cls=AfxRegisterWndClass(0,LoadCursor(nullptr,IDC_ARROW),(HBRUSH)(COLOR_BTNFACE+1));
   if(!CreateEx(WS_EX_TOOLWINDOW,cls,L"批量修改障碍参数",WS_POPUP|WS_CAPTION|WS_SYSMENU,CRect(0,0,470,252),owner,0))return;
   font_.CreatePointFont(90,L"Microsoft YaHei");
   SetFont(&font_);
   const wchar_t* names[]={L"类型",L"显示状态",L"地形",L"视点",L"音效"};
   for(int i=0;i<5;++i){checks_[i].Create(names[i],WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,CRect(12,12+i*34,132,38+i*34),this,100+i);combos_[i].Create(WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,CRect(140,12+i*34,438,240+i*34),this,110+i);}
   for(int i=0;i<5;++i){checks_[i].SetFont(&font_);combos_[i].SetFont(&font_);}
   for(auto text:{L"火",L"船",L"火船"})combos_[0].AddString(text);
   for(auto text:{L"显示",L"消失"})combos_[1].AddString(text);
   combos_[2].AddString(L"自动匹配类型");for(const auto& text:terrains)combos_[2].AddString(text.c_str());
   for(auto text:{L"不切换",L"切换"})combos_[3].AddString(text);
   for(auto text:{L"无音效",L"播放音效"})combos_[4].AddString(text);
   apply_.Create(L"应用勾选字段",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(12,186,438,214),this,201);
   apply_.SetFont(&font_);
  }
  value_=value;handler_=std::move(handler);for(auto& check:checks_)check.SetCheck(BST_UNCHECKED);
  combos_[0].SetCurSel(value.type);combos_[1].SetCurSel(value.parameters.visibility);combos_[2].SetCurSel(value.parameters.terrain+1);combos_[3].SetCurSel(value.parameters.viewpoint);combos_[4].SetCurSel(value.parameters.sound);
  CRect rect;owner->GetWindowRect(rect);SetWindowPos(&wndTop,rect.left+40,rect.top+80,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
 }
 void Hide(){if(GetSafeHwnd())ShowWindow(SW_HIDE);handler_={};}
protected:
 BOOL PreTranslateMessage(MSG* message)override{if(message->message==WM_KEYDOWN&&message->wParam==VK_ESCAPE){Hide();return TRUE;}return CWnd::PreTranslateMessage(message);}
 BOOL OnCommand(WPARAM w,LPARAM l)override{
  if(LOWORD(w)!=201)return CWnd::OnCommand(w,l);
  std::array<bool,5> fields{};for(int i=0;i<5;++i)fields[i]=checks_[i].GetCheck()==BST_CHECKED;
  if(!std::any_of(fields.begin(),fields.end(),[](bool b){return b;})){AfxMessageBox(L"请勾选要批量修改的字段");return TRUE;}
  auto value=value_;value.type=combos_[0].GetCurSel();value.parameters={combos_[1].GetCurSel(),combos_[2].GetCurSel()-1,combos_[3].GetCurSel(),combos_[4].GetCurSel()};
  auto handler=handler_;if(handler&&handler(value,fields))Hide();return TRUE;
 }
 LRESULT WindowProc(UINT m,WPARAM w,LPARAM l)override{if(m==WM_CLOSE){Hide();return 0;}return CWnd::WindowProc(m,w,l);}
private:
 CFont font_;
 CButton checks_[5],apply_;CComboBox combos_[5];Handler handler_;ccz::editor_preview::BattleObstacleDraft value_;
};
