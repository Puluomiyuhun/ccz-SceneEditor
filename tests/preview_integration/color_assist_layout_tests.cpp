#include "../../cczEditor2/pch.h"
#include "../../cczEditor2/resource.h"
#include <vector>
#include <afxdockablepane.h>
#include <afxframewndex.h>
#include <iostream>
#include <stdexcept>
CWinApp application;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
#include "../../cczEditor2/ColorAssistLayout.h"
CRect localRect(CWnd& dialog,CWnd& control){CRect r;control.GetWindowRect(r);dialog.ScreenToClient(r);return r;}
void run(bool info) {
 struct Template {DLGTEMPLATE d;WORD menu,cls,title;} t{};t.d.style=WS_POPUP|WS_CAPTION|DS_MODALFRAME;t.d.cx=225;t.d.cy=135;
 CDialog dialog;CHECK(dialog.CreateIndirect(&t.d));
 CButton ok,cancel,wrap,mode,color;CStatic label;CEdit bytes,text;
 CHECK(ok.Create(L"确定",WS_CHILD|WS_VISIBLE,CRect(70,155,140,178),&dialog,IDOK));
 CHECK(cancel.Create(L"取消",WS_CHILD|WS_VISIBLE,CRect(160,155,230,178),&dialog,IDCANCEL));
 CHECK(wrap.Create(L"换行辅助",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,CRect(30,120,100,137),&dialog,IDC_WRAP_ENABLE));
 CHECK(label.Create(L"字节上限",WS_CHILD|WS_VISIBLE,CRect(106,121,161,136),&dialog,IDC_WRAP_BYTES_LABEL));
 CHECK(bytes.Create(WS_CHILD|WS_VISIBLE,CRect(167,119,215,141),&dialog,IDC_WRAP_BYTES));
 CHECK(mode.Create(L"普通信息",WS_CHILD|(info?WS_VISIBLE:0),CRect(221,119,293,141),&dialog,IDC_WRAP_MODE));
 CHECK(text.Create(WS_CHILD|WS_VISIBLE,CRect(30,8,295,100),&dialog,IDC_EDIT1));
 CreateColorAssistButton(dialog,color,text);auto handle=color.GetSafeHwnd();CreateColorAssistButton(dialog,color,text);CHECK(handle==color.GetSafeHwnd());
 std::vector<CWnd*> row{&wrap,&label,&bytes};if(info) row.push_back(&mode);row.push_back(&color);
 int left=10000,right=0;CRect previous;
 for(auto* control:row){auto r=localRect(dialog,*control);CHECK(r.top<localRect(dialog,ok).top);if(right)CHECK(previous.right<r.left);previous=r;left=(std::min)(left,int(r.left));right=(std::max)(right,int(r.right));CHECK(control->IsWindowEnabled());}
 CRect client;dialog.GetClientRect(client);CHECK(abs(left+right-client.Width())<=1);
 CHECK(((mode.GetStyle()&WS_VISIBLE)!=0)==info);
 dialog.DestroyWindow();
}

class TestDockPane : public CDockablePane {
 public:int frameworkMenus=0;void OnContextMenu(CWnd* source,CPoint point);
 BOOL OnShowControlBarMenu(CPoint) override {++frameworkMenus;return TRUE;}
};
// PRODUCTION_CONTEXT
class TestFrame : public CFrameWndEx {public:TestFrame()=default;void PostNcDestroy() override{}};
// PRODUCTION_PLAYBACK
void contextTest(){
 TestFrame frame;CHECK(frame.Create(nullptr,L"Dock menu test",WS_OVERLAPPEDWINDOW,CRect(-2000,-2000,-1400,-1500)));
 TestDockPane pane;CHECK(pane.Create(L"preview",&frame,CRect(0,0,400,300),TRUE,105,WS_CHILD|WS_VISIBLE|CBRS_RIGHT));
 CStatic child;CHECK(child.Create(L"child",WS_CHILD,CRect(5,5,40,20),&pane,108));
 CPoint inside(10,10);pane.ClientToScreen(&inside);pane.OnContextMenu(&child,inside);CHECK(pane.frameworkMenus==0);
 pane.OnContextMenu(&child,CPoint(-1,-1));CHECK(pane.frameworkMenus==0);
 CPoint caption(10,-5);pane.ClientToScreen(&caption);pane.OnContextMenu(&pane,caption);CHECK(pane.frameworkMenus==1);
 child.DestroyWindow();pane.DestroyWindow();frame.DestroyWindow();
}
int main(){try{CHECK(AfxWinInit(GetModuleHandleW(nullptr),nullptr,GetCommandLineW(),0));run(false);run(true);contextTest();playbackTest();std::cout<<"COLOR_ASSIST_LAYOUT_PASS: centered row, independent controls, distinct confirmation row, no duplicate button\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;}
