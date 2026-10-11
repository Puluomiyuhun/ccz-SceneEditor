#pragma once
#include <ccz/preview/text_color.h>
#include <memory>
#include "ColorAssistLayout.h"
#include "ColorAssistFavorites.h"
namespace {
class ColorAssistDialog : public CDialog {
 CButton colors[256],favorite_buttons[12],more,reset,ok,cancel,preview;CEdit code;bool expanded=false;
 std::vector<std::uint32_t> palette=ccz::preview::standard_text_palette();
 std::array<int,12> common=ccz::editor_preview::kDefaultColorFavorites;
 std::wstring favorites_path;
 bool choosing_favorite=false;
public:
 int selected=0x28;
 INT_PTR ChooseFavorite(CWnd* owner,int initial) {
  selected=initial;expanded=true;choosing_favorite=true;return Run(owner);
 }
 INT_PTR Run(CWnd* owner) {
  struct Layout {DLGTEMPLATE d;WORD menu,cls,title;} t{};
  t.d.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.d.cx=280;t.d.cy=310;
  auto* frame=DYNAMIC_DOWNCAST(CMDIFrameWnd,AfxGetMainWnd());auto* child=frame?frame->MDIGetActive():nullptr;
  auto* document=child?child->GetActiveDocument():nullptr;
  if(document) {
   CString path=document->GetPathName();int separator=path.ReverseFind(L'\\');
   if(separator>=0) {
    path=path.Left(separator);int parent=path.ReverseFind(L'\\');
    if(parent>=0 && path.Mid(parent+1).CompareNoCase(L"RS")==0)path=path.Left(parent);
    CFile file;if(file.Open(path+L"\\Ekd5.exe",CFile::modeRead|CFile::shareDenyNone) && file.GetLength()<64ull*1024*1024) {
     std::vector<std::uint8_t> bytes((std::size_t)file.GetLength());if(file.Read(bytes.data(),(UINT)bytes.size())==bytes.size()) {
      auto found=ccz::preview::extract_text_palette(bytes);if(!found.empty())palette=std::move(found);
     }
    }
   }
  }
  favorites_path=ccz::editor_preview::ColorFavoritesIniPath();
  common=ccz::editor_preview::LoadColorFavorites(favorites_path);
  InitModalIndirect(&t.d,owner);return DoModal();
 }
 void Layout() {
  for(int i=0;i<256;++i) colors[i].ShowWindow(SW_HIDE);
  for(auto& button:favorite_buttons)button.ShowWindow(SW_HIDE);
  int width=expanded?416:320;
  int rows=expanded?16:2;
  for(int n=0;n<(expanded?256:12);++n) {
   int cols=expanded?16:6;int cell=expanded?24:48;
   auto& button=expanded?colors[n]:favorite_buttons[n];
   button.MoveWindow(12+n%cols*cell,12+n/cols*24,cell-2,22);button.ShowWindow(SW_SHOW);
  }
  int y=16+rows*24;
  code.MoveWindow(12,y,60,26);more.MoveWindow(80,y,100,26);reset.MoveWindow(188,y,110,26);
  preview.MoveWindow(12,y+36,104,28);ok.MoveWindow(width-184,y+36,80,28);cancel.MoveWindow(width-96,y+36,80,28);
  CRect window,client;GetWindowRect(window);GetClientRect(client);
  SetWindowPos(nullptr,0,0,width+window.Width()-client.Width(),y+76+window.Height()-client.Height(),SWP_NOMOVE|SWP_NOZORDER);
 }
 BOOL OnInitDialog() override {
  CDialog::OnInitDialog();SetWindowTextW(choosing_favorite?L"选择常用颜色":L"换色辅助");
  for(int i=0;i<256;++i) {CString label;label.Format(L"%02X",i);colors[i].Create(label,WS_CHILD|BS_OWNERDRAW,CRect(0,0,1,1),this,1000+i);}
  for(int i=0;i<12;++i)favorite_buttons[i].Create(L"",WS_CHILD|BS_OWNERDRAW,CRect(0,0,1,1),this,3000+i);
  code.Create(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_UPPERCASE|ES_AUTOHSCROLL|WS_TABSTOP,CRect(),this,2000);code.SetLimitText(2);
  CString value;value.Format(L"%02X",selected);code.SetWindowTextW(value);
  more.Create(L"更多颜色",WS_CHILD|WS_VISIBLE|WS_TABSTOP,CRect(),this,2001);
  reset.Create(L"恢复默认色",WS_CHILD|WS_VISIBLE|WS_TABSTOP,CRect(),this,2002);
  if(choosing_favorite) {more.SetWindowTextW(L"收起色板");reset.ShowWindow(SW_HIDE);}
  ok.Create(L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,CRect(),this,IDOK);
  preview.Create(L"预览",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,CRect(),this,2003);
  cancel.Create(L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,CRect(),this,IDCANCEL);
  CFont* font=CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT));
  for(CWnd* child=GetWindow(GW_CHILD);child;child=child->GetWindow(GW_HWNDNEXT))child->SetFont(font);
  Layout();CenterWindow();return TRUE;
 }
 BOOL OnCommand(WPARAM w,LPARAM l) override {
  UINT id=LOWORD(w);
  if(id>=1000 && id<1256) {selected=id-1000;CString value;value.Format(L"%02X",selected);code.SetWindowTextW(value);return TRUE;}
  if(id>=3000 && id<3012) {selected=common[id-3000];CString value;value.Format(L"%02X",selected);code.SetWindowTextW(value);return TRUE;}
  if(id==2000 && HIWORD(w)==EN_CHANGE) {
   CString value;code.GetWindowTextW(value);wchar_t* end=nullptr;auto number=wcstol(value,&end,16);
   if(value.GetLength()==2 && end && !*end && number>=0 && number<=255) {selected=(int)number;for(auto& color:colors)color.Invalidate(FALSE);for(auto& favorite:favorite_buttons)favorite.Invalidate(FALSE);if(preview.GetSafeHwnd())preview.Invalidate(FALSE);}
   return TRUE;
  }
  if(id==2001) {expanded=!expanded;more.SetWindowTextW(expanded?L"收起色板":L"更多颜色");Layout();return TRUE;}
  if(id==2002) {selected=-1;EndDialog(IDOK);return TRUE;}
  return CDialog::OnCommand(w,l);
 }
 void OnOK() override {
  CString value;code.GetWindowTextW(value);wchar_t* end=nullptr;
  auto number=wcstol(value,&end,16);
  if(value.GetLength()!=2 || !end || *end || number<0 || number>255) {MessageBox(L"请输入两位十六进制颜色编号");return;}
  selected=(int)number;CDialog::OnOK();
 }
 BOOL OnWndMsg(UINT message,WPARAM w,LPARAM l,LRESULT* result) override {
  if(message==WM_CONTEXTMENU && !choosing_favorite) {
   auto* target=CWnd::FromHandlePermanent(reinterpret_cast<HWND>(w));
   const int id=target?target->GetDlgCtrlID():-1;
   if(id>=3000 && id<3012 && !expanded) {
     CPoint point(GET_X_LPARAM(l),GET_Y_LPARAM(l));
     if(point.x==-1 && point.y==-1) {CRect rect;target->GetWindowRect(rect);point=rect.CenterPoint();}
     CMenu menu;menu.CreatePopupMenu();
     menu.AppendMenuW(MF_STRING,1,L"更换这个常用颜色...");
     menu.AppendMenuW(MF_STRING,2,L"恢复默认常用颜色");
     const UINT action=menu.TrackPopupMenu(TPM_RETURNCMD|TPM_NONOTIFY,
                                           point.x,point.y,this);
     if(action==1) {
      const auto index=static_cast<std::size_t>(id-3000);
      ColorAssistDialog picker;
      if(picker.ChooseFavorite(this,common[index])==IDOK) {
       auto updated=common;updated[index]=picker.selected;
       if(!ccz::editor_preview::SaveColorFavorites(favorites_path,updated))
        MessageBox(L"常用颜色保存失败，请检查用户配置目录。");
       else {common=updated;Layout();}
      }
     } else if(action==2) {
      const auto defaults=ccz::editor_preview::kDefaultColorFavorites;
      if(!ccz::editor_preview::SaveColorFavorites(favorites_path,defaults))
       MessageBox(L"常用颜色保存失败，请检查用户配置目录。");
      else {common=defaults;Layout();}
     }
     if(result)*result=0;
     return TRUE;
   }
  }
  if(message==WM_DRAWITEM) {DrawColorItem(reinterpret_cast<LPDRAWITEMSTRUCT>(l));if(result)*result=TRUE;return TRUE;}
  return CDialog::OnWndMsg(message,w,l,result);
 }
 void DrawColorItem(LPDRAWITEMSTRUCT d) {
  const bool favorite=d->CtlID>=3000 && d->CtlID<3012;
  if(d->CtlID!=2003 && !favorite && (d->CtlID<1000 || d->CtlID>=1256)) return;
  int color=d->CtlID==2003?(std::max)(0,selected):(favorite?common[d->CtlID-3000]:d->CtlID-1000);
  auto rgb=palette[color];
  CDC dc;dc.Attach(d->hDC);CRect r(d->rcItem);dc.FillSolidRect(r,RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));
  if(d->CtlID!=2003 && color==selected) {dc.Draw3dRect(r,RGB(255,255,255),RGB(0,0,0));r.DeflateRect(2,2);dc.Draw3dRect(r,RGB(0,0,0),RGB(255,255,255));}
  dc.SetBkMode(TRANSPARENT);int light=((rgb>>16)&255)*3+((rgb>>8)&255)*6+(rgb&255);
  dc.SetTextColor(light>1300?RGB(0,0,0):RGB(255,255,255));CString text;if(d->CtlID==2003)text=L"文字预览";else text.Format(L"%02X",color);dc.DrawText(text,r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);dc.Detach();
 }
};
inline void ApplyColorAssist(CEdit& edit) {
 int a,b;edit.GetSel(a,b);CString text;edit.GetWindowTextW(text);
 if(a==b) {
  int open=text.Left(a+1).ReverseFind(L'[');int close=open>=0?text.Find(L']',open):-1;
  if(open>=0 && close>=a && text.Mid(open,2)==L"[C") {a=open;b=close+1;}
 }
 if(a==b) {edit.MessageBox(L"请先选中需要换色的文字");return;}
 CString selection=text.Mid(a,b-a);
 // Reject a selection that cuts through an existing color span.
 bool inside=false;int spanStart=-1;
 for(int i=0;i<text.GetLength();++i) {
  if(i+3<text.GetLength() && text.Mid(i,2)==L"[C" && iswxdigit(text[i+2]) && iswxdigit(text[i+3])) {inside=true;spanStart=i;i+=3;continue;}
  if(text[i]==L']' && inside) {
   if((a>spanStart && a<=i) || (b>spanStart && b<=i)) {edit.MessageBox(L"请选择完整的颜色标记，或将光标放在颜色标记内进行修改");return;}
   inside=false;
  }
 }
 if(inside && b>spanStart) {edit.MessageBox(L"请选择完整的颜色标记或正文");return;}
 ColorAssistDialog dialog;
 if(selection.GetLength()>=4 && selection.Left(2)==L"[C") dialog.selected=(int)wcstol(selection.Mid(2,2),nullptr,16);
 if(dialog.Run(edit.GetParent())!=IDOK) return;
 std::wstring plain;
 for(const auto& run:ccz::preview::parse_color_text(std::wstring(selection.GetString()))) plain+=run.text;
 // An unmatched boundary must never modify surrounding text.
 if(selection.Find(L"[C")>=0 && selection.Find(L']')<0) {edit.MessageBox(L"请选择完整的颜色标记或正文");return;}
 CString replacement;
 if(dialog.selected<0) replacement=plain.c_str();
 else replacement.Format(L"[C%02X%s]",dialog.selected,plain.c_str());
 edit.SetSel(a,b);edit.ReplaceSel(replacement,TRUE);
}
}
