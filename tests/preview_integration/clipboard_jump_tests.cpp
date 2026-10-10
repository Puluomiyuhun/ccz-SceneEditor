#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <memory>
#include <map>
#include <set>
#include <functional>
#include <iostream>
#include "../../cczEditor2/TemplateLibrary/TemplateModel.h"
#include "../../cczEditor2/LongCharData.h"
#include "../../cczEditor2/PreviewIntegration/PreviewWritebackGate.h"
using std::max;
using std::min;
class CString {
public:
  std::wstring value;
  CString() = default;
  CString(const wchar_t *s) : value(s) {}
  CString &operator=(const wchar_t *s) {
    value = s;
    return *this;
  }
  wchar_t operator[](int i) const { return value.at(i); }
  operator LPCWSTR() const { return value.c_str(); }
};
struct ItemData {
  int id = 0, ord = 0;
  std::vector<int> int_data = std::vector<int>(20);
  wchar_t *long_char_data = nullptr;
  std::size_t long_char_data_capacity = 0;
  ItemData *child = nullptr;
  ItemData *bro = nullptr;
};
struct TestDoc {
  std::uint64_t epoch = 1, generation = 1;
  bool modified = false;
  std::uint64_t GetPreviewContentEpoch() const { return epoch; }
  std::uint64_t GetPreviewTreeGeneration() const { return generation; }
  void OnModified(BOOL v) {
    modified = v != FALSE;
    if (v)
      ++epoch;
  }
  void MarkEditorContentChanged() { ++epoch; }
  void SetModifiedFlag(BOOL){};
};
struct CTreeCtrl {
  HWND hwnd = nullptr;
  int fail_after = -1;
  HTREEITEM GetRootItem() { return TreeView_GetRoot(hwnd); }
  HTREEITEM GetChildItem(HTREEITEM n) { return TreeView_GetChild(hwnd, n); }
  HTREEITEM GetParentItem(HTREEITEM n) { return TreeView_GetParent(hwnd, n); }
  HTREEITEM GetNextSiblingItem(HTREEITEM n) {
    return TreeView_GetNextSibling(hwnd, n);
  }
  HTREEITEM GetPrevSiblingItem(HTREEITEM n) {
    return TreeView_GetPrevSibling(hwnd, n);
  }
  HTREEITEM GetSelectedItem() { return TreeView_GetSelection(hwnd); }
  DWORD_PTR GetItemData(HTREEITEM n) {
    TVITEMW t{};
    t.mask = TVIF_PARAM;
    t.hItem = n;
    return TreeView_GetItem(hwnd, &t) ? t.lParam : 0;
  }
  BOOL SetItemData(HTREEITEM n, DWORD_PTR data) {
    TVITEMW t{};
    t.mask = TVIF_PARAM;
    t.hItem = n;
    t.lParam = data;
    return TreeView_SetItem(hwnd, &t);
  }
  CString GetItemText(HTREEITEM n) {
    wchar_t text[20000]{};
    TVITEMW t{};
    t.mask = TVIF_TEXT;
    t.hItem = n;
    t.pszText = text;
    t.cchTextMax = 20000;
    TreeView_GetItem(hwnd, &t);
    return CString(text);
  }
  BOOL GetCheck(HTREEITEM n) { return TreeView_GetCheckState(hwnd, n); }
  void SetCheck(HTREEITEM n, BOOL v) { TreeView_SetCheckState(hwnd, n, v); }
  HTREEITEM InsertItem(const wchar_t *label, int, int, HTREEITEM parent,
                       HTREEITEM after) {
    if (fail_after == 0)
      return nullptr;
    if (fail_after > 0)
      --fail_after;
    TVINSERTSTRUCTW t{};
    t.hParent = parent;
    t.hInsertAfter = after;
    t.item.mask = TVIF_TEXT;
    t.item.pszText = const_cast<wchar_t *>(label);
    return TreeView_InsertItem(hwnd, &t);
  }
  BOOL DeleteItem(HTREEITEM n) { return TreeView_DeleteItem(hwnd, n); }
  BOOL SelectItem(HTREEITEM n) { return TreeView_SelectItem(hwnd, n); }
  void Expand(HTREEITEM n, UINT mode) { TreeView_Expand(hwnd, n, mode); }
  void EnsureVisible(HTREEITEM n) { TreeView_EnsureVisible(hwnd, n); }
  void SetRedraw(BOOL v) { SendMessageW(hwnd, WM_SETREDRAW, v, 0); }
  void Invalidate() { InvalidateRect(hwnd, nullptr, TRUE); }
};
struct CcczEditor2View {
  CTreeCtrl tree;
  TestDoc doc;
  int cur_code_ord = 0, code_sum = 124;
  unsigned char code_test[124]{};
  bool checkbox = false, m_suppressBattleWritebackPreview = false;
  HTREEITEM cur_item = nullptr;
  struct TemplateInsertionUndo {
    std::vector<HTREEITEM> roots;
    HTREEITEM anchor = nullptr;
    std::uint64_t epoch = 0, generation = 0;
    int ordinal_before = 0;
    bool modified_before = false;
  };
  std::vector<TemplateInsertionUndo> m_templateUndo;
  CTreeCtrl &GetTreeCtrl() { return tree; }
  TestDoc *GetDocument() const { return const_cast<TestDoc *>(&doc); }
  void refreshCheckbox(HTREEITEM n, int) {
    for (; n; n = tree.GetNextSiblingItem(n)) {
      tree.SetCheck(n, FALSE);
      refreshCheckbox(tree.GetChildItem(n), -1);
    }
  }
  bool RefreshScenePreview() { return true; }
  bool CaptureTemplateSelection(ccz::templates::Entry &, CString &);
  bool ctrl_on = false;
  int updates = 0;
  void UpdateShow(HTREEITEM) { ++updates; }
  int MessageBox(const wchar_t*, const wchar_t*, UINT) { return IDOK; }
  HTREEITEM FindLast(HTREEITEM n) { return n; }
  HTREEITEM CreateItem(int id, HTREEITEM parent, HTREEITEM after) {
    auto item = tree.InsertItem(L"Instruction",0,1,parent,after);
    auto data = new ItemData; data->id = id; data->ord = cur_code_ord++;
    tree.SetItemData(item,reinterpret_cast<DWORD_PTR>(data)); return item;
  }
  void OnPaste();
  bool InsertTemplate(const ccz::templates::Entry &, CString &);
  bool CanUndoTemplateInsertion() const;
  bool UndoTemplateInsertion(CString &);
};
static int externalMode = 0;
static int externalValue = 0;
static int externalPrompts = 0;
bool ResolveTransferExternal(CcczEditor2View&, const std::set<int>& ids, bool isTemplate,
                              std::map<int,int>& out) {
  if (ids.empty()) return true;
  ++externalPrompts;
  if (externalMode == 2) return false;
  for (int id : ids) out[id] = externalMode == 1 ? externalValue : (isTemplate ? 0 : id);
  return true;
}

struct Clipboard { int condition=1,copys_sum=0; ItemData* copy=nullptr; ItemData* copys[1000]{}; } theApp;
#include "../../cczEditor2/JumpTransfer.h"
// PRODUCTION_CLIPBOARD
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
int main() {
 try {
  int parsed = -1; std::set<int> valid{0,4,123};
  CHECK(ccz::editor::ParseTransferTarget(L"123",valid,parsed) && parsed==123);
  CHECK(!ccz::editor::ParseTransferTarget(L"999",valid,parsed));
  CHECK(!ccz::editor::ParseTransferTarget(L"",valid,parsed));
  CHECK(!ccz::editor::ParseTransferTarget(L"-1",valid,parsed));
  CHECK(!ccz::editor::ParseTransferTarget(L"123x",valid,parsed));
  CHECK(!ccz::editor::ParseTransferTarget(L"999999999999999999999",valid,parsed));
  INITCOMMONCONTROLSEX init{sizeof(init),ICC_TREEVIEW_CLASSES}; InitCommonControlsEx(&init);
  HWND host=CreateWindowW(L"STATIC",L"test",WS_OVERLAPPEDWINDOW,0,0,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  CcczEditor2View v; v.tree.hwnd=CreateWindowW(WC_TREEVIEWW,L"",WS_CHILD,0,0,600,400,host,nullptr,GetModuleHandleW(nullptr),nullptr);
  auto root=v.CreateItem(-3,nullptr,TVI_LAST);
  auto scene=v.CreateItem(-1,root,TVI_LAST);
  auto section=v.CreateItem(-2,scene,TVI_LAST);
  auto event=v.CreateItem(5,section,TVI_LAST);
  auto anchor=v.CreateItem(9,event,TVI_LAST); v.cur_item=anchor;
  ItemData forward,target,backward,outside;
  forward.id=backward.id=outside.id=0x76; target.id=9;
  forward.ord=100;target.ord=101;backward.ord=102;outside.ord=103;
  forward.int_data[0]=101;backward.int_data[0]=100;outside.int_data[0]=999;
  theApp.copys_sum=4;theApp.copys[0]=&forward;theApp.copys[1]=&target;theApp.copys[2]=&backward;theApp.copys[3]=&outside;
  int start=v.cur_code_ord; v.OnPaste();
  std::map<int,ItemData*> inserted;
  for(auto h=v.tree.GetChildItem(event);h;h=v.tree.GetNextSiblingItem(h)) {
    auto d=(ItemData*)v.tree.GetItemData(h);inserted[d->ord]=d;
  }
  CHECK(inserted.at(start)->int_data[0]==start+1);
  CHECK(inserted.at(start+2)->int_data[0]==start);
  CHECK(inserted.at(start+3)->int_data[0]==999);
  int second=v.cur_code_ord;v.OnPaste();
  for(auto h=v.tree.GetChildItem(event);h;h=v.tree.GetNextSiblingItem(h)) {auto d=(ItemData*)v.tree.GetItemData(h);inserted[d->ord]=d;}
  CHECK(inserted.at(second)->int_data[0]==second+1);CHECK(inserted.at(second+2)->int_data[0]==second);
  externalMode=1;externalValue=4;int third=v.cur_code_ord;v.OnPaste();
  for(auto h=v.tree.GetChildItem(event);h;h=v.tree.GetNextSiblingItem(h)) {auto d=(ItemData*)v.tree.GetItemData(h);inserted[d->ord]=d;}
  CHECK(inserted.at(third+3)->int_data[0]==4);
  externalMode=2;int before=v.cur_code_ord;v.OnPaste();CHECK(v.cur_code_ord==before);
  externalMode=0;theApp.condition=0;theApp.copy=&forward;forward.child=&target;
  v.OnPaste(); auto copyRoot=v.cur_item;auto copyChild=v.tree.GetChildItem(copyRoot);
  CHECK(((ItemData*)v.tree.GetItemData(copyRoot))->int_data[0]==((ItemData*)v.tree.GetItemData(copyChild))->ord);
  std::cout<<"[PASS] Production clipboard forward/backward, repeated groups, external preserve/manual/cancel, nested single paste.\n";
  DestroyWindow(host);return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
