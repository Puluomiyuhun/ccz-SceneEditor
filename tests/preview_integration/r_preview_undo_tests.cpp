#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#define _T(x) L##x
struct CString : std::wstring { using std::wstring::wstring; CString(const std::wstring& s):std::wstring(s){} const wchar_t* GetString() const {return c_str();} };
struct ItemData { int id=0; std::vector<int> int_data; wchar_t* long_char_data=nullptr; ItemData* child=nullptr; ItemData* bro=nullptr; };
struct CTreeCtrl {
 HWND window{}; int deleteCalls=0, failDeleteAt=0;
 HWND GetSafeHwnd(){return window;}
 DWORD_PTR GetItemData(HTREEITEM item){TVITEM v{};v.mask=TVIF_PARAM;v.hItem=item;return TreeView_GetItem(window,&v)?v.lParam:0;}
 HTREEITEM GetParentItem(HTREEITEM i){return TreeView_GetParent(window,i);}
 HTREEITEM GetPrevSiblingItem(HTREEITEM i){return TreeView_GetPrevSibling(window,i);}
 bool ItemHasChildren(HTREEITEM i){return TreeView_GetChild(window,i)!=nullptr;}
 CString GetItemText(HTREEITEM i){wchar_t b[256]{};TVITEM v{};v.mask=TVIF_TEXT;v.hItem=i;v.pszText=b;v.cchTextMax=256;TreeView_GetItem(window,&v);return CString(b);}
 HTREEITEM GetSelectedItem(){return TreeView_GetSelection(window);}
 BOOL SelectItem(HTREEITEM i){return TreeView_SelectItem(window,i);}
 BOOL DeleteItem(HTREEITEM i){if(++deleteCalls==failDeleteAt)return FALSE;return TreeView_DeleteItem(window,i);}
 HTREEITEM InsertItem(TVINSERTSTRUCT* i){return TreeView_InsertItem(window,i);}
};
struct Doc {std::uint64_t generation=1; int edits=0; std::uint64_t GetPreviewTreeGeneration(){return generation;} void MarkEditorContentChanged(){++edits;} void SetModifiedFlag(BOOL){} };
struct CcczEditor2View {CTreeCtrl tree;Doc doc;HTREEITEM cur_item{};bool m_suppressBattleWritebackPreview=false,m_suppressPreviewRefreshOnce=false;CTreeCtrl& GetTreeCtrl(){return tree;}Doc* GetDocument(){return &doc;}void UpdateShow(HTREEITEM){} };
CcczEditor2View* active;
CcczEditor2View* GetActiveEditorView(){return active;}
HTREEITEM FindCurrentTreeItem(CTreeCtrl& tree,HTREEITEM target){
 std::vector<HTREEITEM> todo;
 for(auto i=TreeView_GetRoot(tree.window);i;i=TreeView_GetNextSibling(tree.window,i))todo.push_back(i);
 while(!todo.empty()){auto i=todo.back();todo.pop_back();if(i==target)return i;for(auto c=TreeView_GetChild(tree.window,i);c;c=TreeView_GetNextSibling(tree.window,c))todo.push_back(c);}return nullptr;
}
namespace ccz::scene_preview {struct PositionSource {int kind=0,instructionId=30,xIndex=0,yIndex=1;};int position_direction_index(int){return 2;}bool PositionSourceMatchesInstruction(const PositionSource&,const std::vector<int>& v){return v.size()>=3;}}
class CRScenePreviewPage {
public:
 struct CoordinateUndoEntry {HTREEITEM item{};ccz::scene_preview::PositionSource source{};int oldX=0,oldY=0,newX=0,newY=0;bool directionChanged=false;int oldDirection=0,newDirection=0;};
 struct InsertedUndoSnapshot {HTREEITEM item{},parent{};int id=0;std::vector<int> values;CString text;};
 struct PreviewUndoTransaction {enum class Kind{Coordinate,Inserted};Kind kind=Kind::Coordinate;std::wstring label;std::vector<CoordinateUndoEntry> coordinates;std::vector<HTREEITEM> insertedItems;std::vector<InsertedUndoSnapshot> insertedSnapshots;HTREEITEM restoreSelection{};std::uint64_t treeGeneration=0;};
 std::vector<PreviewUndoTransaction> m_previewUndoStack;CTreeCtrl* m_pTreeCtrl{};HTREEITEM m_hCurItem{};std::uint64_t m_previewTreeGeneration=500;int refreshes=0;std::wstring diagnostic;
 void UpdateUndoButton(){} void AppendDiagnostic(const wchar_t* s){diagnostic=s;}
 void RequestPreviewRefreshForCurrentDocument(CTreeCtrl&,HTREEITEM){if(active->m_suppressBattleWritebackPreview)throw std::runtime_error("suppressed final refresh");++refreshes;}
 void RecordCoordinateUndo(const wchar_t*,std::vector<CoordinateUndoEntry>,HTREEITEM);
 void RecordInsertedUndo(const wchar_t*,std::vector<HTREEITEM>,HTREEITEM);
 bool UndoLastPreviewOperation();
};
// PRODUCTION_FUNCTIONS
void check(bool v,const char* name){if(!v)throw std::runtime_error(name);}
int runTests(){
 INITCOMMONCONTROLSEX init{sizeof(init),ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&init);
 HWND parent=CreateWindowW(L"STATIC",L"Undo test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 CcczEditor2View view; active=&view;
 view.tree.window=CreateWindowW(WC_TREEVIEWW,L"",WS_CHILD,0,0,600,400,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
 CRScenePreviewPage page;page.m_pTreeCtrl=&view.tree;
 auto add=[&](int id,std::vector<int> values,HTREEITEM parentItem=nullptr){auto data=new ItemData;data->id=id;data->int_data=values;TVINSERTSTRUCT v{};v.hParent=parentItem;v.hInsertAfter=TVI_LAST;v.item.mask=TVIF_TEXT|TVIF_PARAM;v.item.pszText=const_cast<wchar_t*>(L"instruction");v.item.lParam=reinterpret_cast<LPARAM>(data);return view.tree.InsertItem(&v);};
 auto anchor=add(2,{0,0,0});view.cur_item=anchor;page.m_hCurItem=anchor;
 for(int i=0;i<3;++i){auto item=add(33,{i,0,0},anchor);page.RecordInsertedUndo(L"turn",{item},anchor);}
 check(page.m_previewUndoStack.size()==3,"three records");
 for(int i=0;i<3;++i){check(page.UndoLastPreviewOperation(),"continuous undo");check(page.m_previewUndoStack.size()==2-i,"one record popped");}
 check(!page.UndoLastPreviewOperation(),"empty stack");check(page.refreshes==3,"one refresh each");
 // Mixed insert and coordinate/facing modification.
 auto role=add(30,{1,2,0},anchor);page.RecordInsertedUndo(L"appear",{role},anchor);
 auto data=reinterpret_cast<ItemData*>(view.tree.GetItemData(role));data->int_data={4,5,3};
 CRScenePreviewPage::CoordinateUndoEntry e;e.item=role;e.oldX=1;e.oldY=2;e.newX=4;e.newY=5;e.directionChanged=true;e.oldDirection=0;e.newDirection=3;
 page.RecordCoordinateUndo(L"position",{e},anchor);check(page.UndoLastPreviewOperation(),"coordinate undo");check(data->int_data==std::vector<int>({1,2,0}),"coordinates and facing restored");check(page.UndoLastPreviewOperation(),"earlier insertion undo");
 // Page generation can change without changing the document tree.
 auto turn=add(33,{1,2,3},anchor);page.RecordInsertedUndo(L"turn",{turn},anchor);++page.m_previewTreeGeneration;check(page.UndoLastPreviewOperation(),"page generation independent");
 turn=add(33,{1,2,3},anchor);page.RecordInsertedUndo(L"turn",{turn},anchor);data=reinterpret_cast<ItemData*>(view.tree.GetItemData(turn));data->int_data[0]=99;check(!page.UndoLastPreviewOperation(),"manual edit refused");check(page.m_previewUndoStack.size()==1,"rejected record retained");data->int_data[0]=1;check(page.UndoLastPreviewOperation(),"record usable after restore");
 turn=add(33,{1,2,3},anchor);page.RecordInsertedUndo(L"turn",{turn},anchor);++view.doc.generation;check(!page.UndoLastPreviewOperation(),"tree rebuild refused");--view.doc.generation;check(page.UndoLastPreviewOperation(),"tree generation restored");
 // Fail after two successful removals: restore original sequence and keep the record.
 std::vector<HTREEITEM> group;for(int i=0;i<4;++i)group.push_back(add(33,{i,0,0},anchor));page.RecordInsertedUndo(L"group",group,anchor);
 view.tree.failDeleteAt=view.tree.deleteCalls+3;check(!page.UndoLastPreviewOperation(),"deletion failure refused");check(page.m_previewUndoStack.size()==1,"failure retains transaction");
 int count=0;for(auto i=TreeView_GetChild(view.tree.window,anchor);i;i=TreeView_GetNextSibling(view.tree.window,i)){auto d=reinterpret_cast<ItemData*>(view.tree.GetItemData(i));check(d->int_data[0]==count++,"rollback sibling order");}check(count==4,"rollback restores all nodes");check(!view.m_suppressBattleWritebackPreview,"suppression restored on failure");view.tree.failDeleteAt=0;check(page.UndoLastPreviewOperation(),"retry group undo");check(page.m_previewUndoStack.empty(),"group pops once");
 delete reinterpret_cast<ItemData*>(view.tree.GetItemData(anchor));DestroyWindow(parent);
 std::cout<<"R_UNDO_NATIVE_TREE_PASS: continuous, mixed coordinates/facing, page generation, manual conflict, tree rebuild, group rollback/retry\n";
return 0;
}
int main(){try {return runTests();} catch(const std::exception& e){std::cerr<<"R_UNDO_NATIVE_TREE_FAIL: "<<e.what()<<"\n";return 1;}}
