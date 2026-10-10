#pragma once
#include <afxwin.h>
#include <memory>
#include <functional>
#include <vector>
#include <map>
struct PageFontSet {
 std::map<HWND,LOGFONT> original;
 std::vector<std::unique_ptr<CFont>> fonts;
};
inline int PreviewUiPointSize(int fallback=9) {
 int value=AfxGetApp()->GetProfileInt(L"PreviewPageFont",L"Scene",0);
 return value>=8 && value<=14?value:fallback;
}
inline void AppendPageFontMenu(CMenu& menu,CMenu& fonts,const wchar_t* key,UINT base) {
 fonts.CreatePopupMenu();int selected=AfxGetApp()->GetProfileInt(L"PreviewPageFont",key,0);
 for(int size=8;size<=14;++size) {CString name;name.Format(L"%d",size);fonts.AppendMenuW(MF_STRING|(selected==size?MF_CHECKED:0),base+size,name);}
 fonts.AppendMenuW(MF_STRING|(selected==0?MF_CHECKED:0),base,L"恢复默认");
 menu.AppendMenuW(MF_POPUP,(UINT_PTR)fonts.m_hMenu,L"页面字号");
}
inline void ApplyPageFont(CWnd& page,const wchar_t* key,std::unique_ptr<PageFontSet>& owned) {
 int size=AfxGetApp()->GetProfileInt(L"PreviewPageFont",key,0);
 if(!owned && size==0) return;
 if(!owned) owned=std::make_unique<PageFontSet>();
 std::vector<std::unique_ptr<CFont>> fonts;
 HDC dc=GetDC(page.GetSafeHwnd());int dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(page.GetSafeHwnd(),dc);
 std::function<void(CWnd*)> visit=[&](CWnd* parent) {
  for(CWnd* child=parent->GetWindow(GW_CHILD);child;child=child->GetWindow(GW_HWNDNEXT)) {
   auto handle=child->GetSafeHwnd();auto found=owned->original.find(handle);
   if(found==owned->original.end()) {
    LOGFONT original{};CFont* old=child->GetFont();
    if(old) old->GetLogFont(&original);else GetObject(GetStockObject(DEFAULT_GUI_FONT),sizeof(original),&original);
    found=owned->original.emplace(handle,original).first;
   }
   LOGFONT requested=found->second;
   if(size>=8 && size<=14) {requested.lfHeight=-MulDiv(size,dpi,72);wcscpy_s(requested.lfFaceName,L"Microsoft YaHei");}
   auto font=std::make_unique<CFont>();
   if(font->CreateFontIndirect(&requested)) {if(size>=8 && size<=14) ::SetPropW(handle,L"CczPageFontOverride",reinterpret_cast<HANDLE>(1));else ::RemovePropW(handle,L"CczPageFontOverride");child->SetFont(font.get(),TRUE);fonts.push_back(std::move(font));}
   visit(child);
  }
 };visit(&page);owned->fonts=std::move(fonts);page.Invalidate();
}
