#include "../../cczEditor2/pch.h"
#include "../../cczEditor2/SceneMultiSelectOps.h"
#include "../../cczEditor2/RAutoWriteMenu.h"
#include <map>
#include <vector>
#include <iostream>
#include <stdexcept>
CWinApp application;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct CharacterState {int id,x,y,direction=0;};
struct Doc {BOOL modified=FALSE;BOOL IsModified(){return modified;}void SetModifiedFlag(BOOL v){modified=v;}};
struct Node {int id;std::vector<int> fields;};
struct CcczEditor2View {
 CTreeCtrl tree;Doc doc;HTREEITEM cur_item{};int calls=0,failAt=-1;std::map<HTREEITEM,Node> nodes;
 Doc* GetDocument(){return &doc;}
 HTREEITEM InsertInstructionAt(int id,HTREEITEM after,const int* fields,int count){
  if(++calls==failAt)return nullptr;
  auto item=tree.InsertItem(L"command",TVI_ROOT,after);if(!item)return nullptr;
  nodes[item]={id,{}};if(fields)nodes[item].fields.assign(fields,fields+count);cur_item=item;doc.modified=TRUE;tree.SelectItem(item);return item;
 }
};
CcczEditor2View* active;CcczEditor2View* GetActiveEditorView(){return active;}
class CRScenePreviewPage {
public:
 bool m_bMultiMoveMode=true,m_bPointMoveMode=true;CPoint m_snappedGamePos{20,30},m_multiMoveAnchor{10,10},m_pointMoveCursorPos{20,30};
 int m_multiMoveDirection=1,m_multiMoveInitialDirection=0;std::vector<int> m_multiMoveCharIDs{1,2},m_pointMoveCharIDs{1,2};
 struct {std::vector<CharacterState> characters{{1,10,10,0},{2,12,14,0}};} m_state;
 std::vector<HTREEITEM> undo;std::map<HTREEITEM,std::vector<HTREEITEM>> m_autoWriteGroups;int failures=0;
 void RecordInsertedUndo(const wchar_t*,std::vector<HTREEITEM> handles,HTREEITEM){undo=handles;}
 void RestoreAfterBatchFailure(HTREEITEM item,BOOL modified){++failures;active->cur_item=item;active->tree.SelectItem(item);active->doc.modified=modified;}
 void RemoveInsertedInstruction(HTREEITEM item){auto group=m_autoWriteGroups.find(item);if(group!=m_autoWriteGroups.end()){auto items=group->second;m_autoWriteGroups.erase(group);for(auto i=items.rbegin();i!=items.rend();++i)RemoveInsertedInstruction(*i);return;}active->nodes.erase(item);active->tree.DeleteItem(item);}
 void CommitMultiMove();void CommitPointMove();
 HTREEITEM InsertMoveInstruction(int,int,int,HTREEITEM=nullptr,int=-1,bool=true);
 HTREEITEM InsertDisappearInstruction(int,HTREEITEM=nullptr,bool=true);
 HTREEITEM CompleteAppearWrite(HTREEITEM,HTREEITEM,BOOL);
};
// PRODUCTION_FUNCTIONS
void run(CWnd& host,bool point,bool fail){
 CcczEditor2View view;active=&view;CHECK(view.tree.Create(WS_CHILD,CRect(0,0,400,400),&host,101));
 auto anchor=view.tree.InsertItem(L"anchor");view.cur_item=anchor;
 if(fail)view.failAt=4;
 CRScenePreviewPage page;if(point)page.CommitPointMove();else page.CommitMultiMove();
 if(fail){CHECK(page.failures==1);CHECK(view.nodes.empty());CHECK(view.cur_item==anchor);CHECK(!view.doc.modified);}else{
  CHECK(page.undo.size()==4);std::vector<int> ids;for(auto item=view.tree.GetNextSiblingItem(anchor);item;item=view.tree.GetNextSiblingItem(item))ids.push_back(view.nodes.at(item).id);
  CHECK(ids==std::vector<int>({0x32,0x32,0x34,0x34}));CHECK(view.nodes.at(page.undo[2]).fields==std::vector<int>({1,0}));
 }
 view.tree.DestroyWindow();
}
void appearance(CWnd& host,bool fail){
 CcczEditor2View view;active=&view;CHECK(view.tree.Create(WS_CHILD,CRect(0,0,400,400),&host,101));auto anchor=view.tree.InsertItem(L"anchor");view.cur_item=anchor;
 application.WriteProfileInt(L"RPreviewAux",L"AppearRedraw",1);auto primary=view.InsertInstructionAt(0x30,anchor,nullptr,0);if(fail)view.failAt=2;
 CRScenePreviewPage page;auto result=page.CompleteAppearWrite(primary,anchor,FALSE);
 if(fail){CHECK(!result);CHECK(view.nodes.empty());CHECK(!view.doc.modified);}else{CHECK(view.nodes.at(result).id==0x1e);CHECK(page.m_autoWriteGroups.at(result).size()==2);page.RemoveInsertedInstruction(result);CHECK(view.nodes.empty());}
 view.tree.DestroyWindow();
}
void disappear(CWnd& host) {
 CcczEditor2View view;active=&view;CHECK(view.tree.Create(WS_CHILD,CRect(0,0,400,400),&host,101));
 auto anchor=view.tree.InsertItem(L"anchor");view.cur_item=anchor;CRScenePreviewPage page;
 auto result=page.InsertDisappearInstruction(1,anchor,true);CHECK(view.nodes.at(result).id==0x1e);CHECK(page.undo.size()==1);CHECK(page.m_autoWriteGroups.at(result).size()==2);
 page.RemoveInsertedInstruction(result);CHECK(view.nodes.empty());view.tree.DestroyWindow();
}

void autoMenu() {
 CMenu menu,actions;menu.CreatePopupMenu();AppendRAutoWriteMenu(menu,actions);
 CHECK(menu.GetMenuItemCount()==3);CHECK(actions.GetMenuItemCount()==21);
 CHECK(menu.GetMenuState(0xed10,MF_BYCOMMAND)&MF_CHECKED);
 CHECK(!(menu.GetMenuState(0xed11,MF_BYCOMMAND)&MF_CHECKED));
 CHECK(HandleRAutoWriteMenuCommand(0xed10));CHECK(HandleRAutoWriteMenuCommand(0xed11));CHECK(HandleRAutoWriteMenuCommand(0xed24));
 CMenu second,secondActions;second.CreatePopupMenu();AppendRAutoWriteMenu(second,secondActions);
 CHECK(!(second.GetMenuState(0xed10,MF_BYCOMMAND)&MF_CHECKED));
 CHECK(second.GetMenuState(0xed11,MF_BYCOMMAND)&MF_CHECKED);CHECK(secondActions.GetMenuState(0xed24,MF_BYCOMMAND)&MF_CHECKED);
 CHECK(!HandleRAutoWriteMenuCommand(100));
}
int main(){try{
 CHECK(AfxWinInit(GetModuleHandleW(nullptr),nullptr,GetCommandLineW(),0));
 wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);std::wstring ini=std::wstring(temp)+L"ccz-aux-regression.ini";DeleteFileW(ini.c_str());free((void*)application.m_pszProfileName);application.m_pszProfileName=_wcsdup(ini.c_str());
 CWnd host;CHECK(host.CreateEx(0,AfxRegisterWndClass(0),L"Auxiliary transaction tests",WS_OVERLAPPEDWINDOW,CRect(-2000,-2000,-1500,-1500),nullptr,0));
 disappear(host);run(host,false,false);run(host,true,false);run(host,false,true);run(host,true,true);appearance(host,false);appearance(host,true);application.WriteProfileInt(L"RPreviewAux",L"AppearRedraw",0);autoMenu();
 host.DestroyWindow();DeleteFileW(ini.c_str());std::cout<<"R_AUTO_WRITE_PASS: grouped order/default ordinary/rollback/appearance atomicity\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;}
