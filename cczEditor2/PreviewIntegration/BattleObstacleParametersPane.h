#pragma once
#include <afxwin.h>
#include <functional>
#include "BattleMapOperations.h"
#include "../CMyComboBox.h"

// Same modeless apply/reset interaction as the unit parameter pane.
class CBattleObstacleParametersPane : public CWnd {
public:
 using Handler = std::function<bool(const ccz::editor_preview::BattleObstacleDraft&)>;
 void Open(CWnd* owner, bool defaults, const ccz::editor_preview::BattleObstacleDraft& value,
           const std::vector<std::wstring>& terrains, Handler handler) {
  if (!GetSafeHwnd()) {
   const CString cls=AfxRegisterWndClass(CS_DBLCLKS, LoadCursor(nullptr,IDC_ARROW), (HBRUSH)(COLOR_BTNFACE+1));
   if (!CreateEx(WS_EX_TOOLWINDOW,cls,L"战场障碍参数",WS_POPUP|WS_CAPTION|WS_SYSMENU,CRect(0,0,690,218),owner,0)) { AfxMessageBox(L"无法创建战场障碍参数窗口"); return; }
   font_.CreatePointFont(90,L"Microsoft YaHei");
   const wchar_t* names[]={L"类型",L"显示状态",L"地形",L"视点",L"音效",L"X",L"Y"};
   for(int i=0;i<7;++i) {
    const int row=i<3?0:i<5?1:2;
    const int col=i<3?i:i<5?i-3:i-5;
    const int x=12+col*220,y=14+row*37;
    labels_[i].Create(names[i],WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE,CRect(x,y,x+62,y+25),this);
    labels_[i].SetFont(&font_);
    if(i<5) { combos_[i].Create(WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,CRect(x+64,y,x+206,y+240),this,100+i);combos_[i].SetFont(&font_); }
    else { edits_[i-5].Create(WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,CRect(x+64,y,x+206,y+25),this,100+i);edits_[i-5].SetFont(&font_); }
   }
   for(auto text:{L"火",L"船",L"火船"}) combos_[0].AddString(text);
   for(auto text:{L"显示",L"消失"}) combos_[1].AddString(text);
   combos_[2].AddString(L"自动匹配类型");for(const auto& t:terrains)combos_[2].AddString(t.c_str());
   for(auto text:{L"不切换",L"切换"}) combos_[3].AddString(text);
   for(auto text:{L"无音效",L"播放音效"}) combos_[4].AddString(text);
   apply_.Create(L"应用",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,CRect(12,132,180,159),this,201);
   reset_.Create(L"重置模板",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,CRect(194,132,350,159),this,202);
   status_.Create(L"",WS_CHILD|WS_VISIBLE,CRect(12,166,658,190),this);
   apply_.SetFont(&font_);reset_.SetFont(&font_);status_.SetFont(&font_);
  }
  defaults_=defaults;value_=value;handler_=std::move(handler);
  SetWindowText(defaults?L"战场障碍默认参数":L"战场障碍修改参数");
  combos_[0].EnableWindow(!defaults);edits_[0].EnableWindow(!defaults);edits_[1].EnableWindow(!defaults);
  reset_.ShowWindow(defaults?SW_SHOW:SW_HIDE);apply_.SetWindowText(defaults?L"应用新增默认值":L"应用到此障碍物");
  status_.SetWindowText(defaults?L"仅影响之后添加的障碍物；类型由缩略图选择，坐标由落点确定。":L"修改此障碍物对应的58号指令参数。右上角关闭取消未应用修改。");
  Fill();CRect parent;owner->GetWindowRect(&parent);SetWindowPos(&wndTop,parent.left+(parent.Width()-690)/2,parent.top+(parent.Height()-218)/2,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);SetFocus();
 }
 void Hide(){if(GetSafeHwnd())ShowWindow(SW_HIDE);handler_={};}
protected:
 BOOL OnCommand(WPARAM w,LPARAM l) override {
  const UINT id=LOWORD(w);if(id!=201&&id!=202)return CWnd::OnCommand(w,l);
  auto replacement=value_;
  if(id==202){replacement.parameters={};}
  else {
   replacement.type=combos_[0].GetCurSel();
   replacement.parameters={combos_[1].GetCurSel(),combos_[2].GetCurSel()-1,combos_[3].GetCurSel(),combos_[4].GetCurSel()};
   if(!defaults_){for(int i=0;i<2;++i){CString text;edits_[i].GetWindowText(text);wchar_t* end=nullptr;long value=wcstol(text,&end,10);if(text.IsEmpty()||end==text.GetString()||*end||value<0||value>INT_MAX){AfxMessageBox(L"请填写有效的X/Y整数坐标");return TRUE;}if(i==0)replacement.coordinate.x=(int)value;else replacement.coordinate.y=(int)value;}}
  }
  auto handler=handler_;
  if(handler&&handler(replacement)){value_=replacement;Fill();status_.SetWindowText(L"参数已应用");}return TRUE;
 }
 BOOL PreTranslateMessage(MSG* msg) override {if(msg->message==WM_KEYDOWN&&msg->wParam==VK_ESCAPE){Hide();return TRUE;}return CWnd::PreTranslateMessage(msg);}
 LRESULT WindowProc(UINT msg,WPARAM w,LPARAM l) override {if(msg==WM_CLOSE){Hide();return 0;}return CWnd::WindowProc(msg,w,l);}
private:
 void Fill(){combos_[0].SetCurSel(value_.type);combos_[1].SetCurSel(value_.parameters.visibility);combos_[2].SetCurSel(value_.parameters.terrain+1);combos_[3].SetCurSel(value_.parameters.viewpoint);combos_[4].SetCurSel(value_.parameters.sound);edits_[0].SetWindowText(std::to_wstring(value_.coordinate.x).c_str());edits_[1].SetWindowText(std::to_wstring(value_.coordinate.y).c_str());}
 bool defaults_{};ccz::editor_preview::BattleObstacleDraft value_;Handler handler_;
 CMyComboBox combos_[5];CEdit edits_[2];CStatic labels_[7],status_;CButton apply_,reset_;CFont font_;
};
