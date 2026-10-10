#pragma once
#include <afxwin.h>
inline void AppendRAutoWriteMenu(CMenu& menu,CMenu& actions) {
 static const wchar_t* names[]={L"普通",L"下跪",L"脸红",L"举手",L"哭",L"伸手",L"作揖",L"盘坐脸红",L"盘坐举手",L"盘坐哭",L"倒下",L"单膝跪地",L"被缚",L"挥剑扬起",L"挥剑劈下",L"活埋",L"起身",L"单手举起",L"未知",L"变量"};
 actions.CreatePopupMenu();int action=AfxGetApp()->GetProfileInt(L"RPreviewAux",L"MoveAction",0);
 actions.AppendMenuW(MF_STRING|(action<0?MF_CHECKED:0),0xed20,L"不补动作");
 for(int i=0;i<20;++i) actions.AppendMenuW(MF_STRING|(action==i?MF_CHECKED:0),0xed21+i,names[i]);
 menu.AppendMenuW(MF_POPUP,reinterpret_cast<UINT_PTR>(actions.m_hMenu),L"移动后补动作");
 menu.AppendMenuW(MF_STRING|(AfxGetApp()->GetProfileInt(L"RPreviewAux",L"Redraw",1)?MF_CHECKED:0),0xed10,L"消失后补重绘");
 menu.AppendMenuW(MF_STRING|(AfxGetApp()->GetProfileInt(L"RPreviewAux",L"AppearRedraw",0)?MF_CHECKED:0),0xed11,L"出现后补重绘");
}
inline bool HandleRAutoWriteMenuCommand(UINT command) {
 if(command==0xed10 || command==0xed11) {
  const wchar_t* key=command==0xed10?L"Redraw":L"AppearRedraw";
  int fallback=command==0xed10?1:0;
  AfxGetApp()->WriteProfileInt(L"RPreviewAux",key,!AfxGetApp()->GetProfileInt(L"RPreviewAux",key,fallback));return true;
 }
 if(command>=0xed20 && command<=0xed34) {
  AfxGetApp()->WriteProfileInt(L"RPreviewAux",L"MoveAction",command==0xed20?-1:command-0xed21);return true;
 }
 return false;
}
