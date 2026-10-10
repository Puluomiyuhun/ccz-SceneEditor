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
// PRODUCTION_TEMPLATE_EDITING

#define REQUIRE(x)                                                             \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
ccz::templates::Node Node(int id, int key) {
  ccz::templates::Node n;
  n.id = id;
  n.key = key;
  n.params.resize(id == 5 ? 60 : 20);
  n.label = L"Node";
  return n;
}
ccz::templates::Entry Entry() {
  ccz::templates::Entry e;
  e.uid = L"native-test";
  e.name = L"native";
  e.roots = {Node(0x14, 0), Node(9, 1)};
  e.roots[0].has_text = true;
  e.roots[0].text = std::wstring(15000, L'中');
  return e;
}
HTREEITEM Add(CcczEditor2View &view, HTREEITEM parent, int id,
              const wchar_t *label = L"Node", HTREEITEM after = TVI_LAST) {
  auto h = view.tree.InsertItem(label, 0, 1, parent, after);
  auto d = new ItemData;
  d->id = id;
  d->ord = view.cur_code_ord++;
  view.tree.SetItemData(h, reinterpret_cast<DWORD_PTR>(d));
  return h;
}
int main() {
  try {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_TREEVIEW_CLASSES};
    REQUIRE(InitCommonControlsEx(&init));
    HWND host = CreateWindowW(L"STATIC", L"Native template test",
                              WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE(host);
    CcczEditor2View v;
    v.tree.hwnd =
        CreateWindowW(WC_TREEVIEWW, L"", WS_CHILD | TVS_CHECKBOXES, 0, 0, 600,
                      400, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE(v.tree.hwnd);
    v.code_test[2] = v.code_test[5] = 2;
    v.code_test[3] = 1;
    auto root = v.tree.InsertItem(L"Scenario", 0, 1, TVI_ROOT, TVI_LAST);
    auto scene = Add(v, root, -1, L"Scene");
    auto section = Add(v, scene, -2, L"Section");
    auto info = Add(v, section, 2);
    auto event = Add(v, section, 0);
    auto anchor = Add(v, event, 9);
    auto terminal = Add(v, event, 0);
    v.tree.SelectItem(anchor);
    CString error;
    auto e = Entry();
    int before = v.cur_code_ord;
    REQUIRE(v.InsertTemplate(e, error));
    auto inserted = v.tree.GetSelectedItem();
    REQUIRE(v.tree.GetNextSiblingItem(anchor) != terminal);
    REQUIRE(v.tree.GetNextSiblingItem(inserted) == terminal);
    REQUIRE(v.CanUndoTemplateInsertion());
    auto first = v.tree.GetNextSiblingItem(anchor);
    auto data = reinterpret_cast<ItemData *>(v.tree.GetItemData(first));
    REQUIRE(data->long_char_data_capacity > 15000 &&
            wcslen(data->long_char_data) == 15000);
    REQUIRE(v.InsertTemplate(e, error));
    REQUIRE(v.UndoTemplateInsertion(error));
    REQUIRE(v.tree.GetSelectedItem() == inserted);
    REQUIRE(v.CanUndoTemplateInsertion());
    REQUIRE(v.UndoTemplateInsertion(error));
    REQUIRE(v.tree.GetNextSiblingItem(anchor) == terminal);
    REQUIRE(v.cur_code_ord == before && !v.doc.modified &&
            !v.CanUndoTemplateInsertion());
    std::cout << "[PASS] Native atomic group insertion, final selection, long "
                 "text, continuous undo.\n";
    v.tree.fail_after = 1;
    REQUIRE(!v.InsertTemplate(e, error));
    v.tree.fail_after = -1;
    REQUIRE(v.tree.GetNextSiblingItem(anchor) == terminal &&
            v.cur_code_ord == before && !v.doc.modified);
    std::cout
        << "[PASS] Native insertion failure rolls back the whole group.\n";
    auto jump = Entry();
    jump.roots[1].id = 0x76;
    jump.roots[1].jump_target = 0;
    REQUIRE(v.InsertTemplate(jump, error));
    first = v.tree.GetNextSiblingItem(anchor);
    data = reinterpret_cast<ItemData *>(
        v.tree.GetItemData(v.tree.GetSelectedItem()));
    REQUIRE(data->int_data[0] ==
            reinterpret_cast<ItemData *>(v.tree.GetItemData(first))->ord);
    REQUIRE(v.UndoTemplateInsertion(error));
    std::cout << "[PASS] Native internal jump remaps to newly allocated target "
                 "identity.\n";
    v.tree.SelectItem(terminal);
    REQUIRE(!v.InsertTemplate(e, error));
    v.tree.SelectItem(event);
    REQUIRE(!v.InsertTemplate(e, error));
    v.tree.SelectItem(info);
    REQUIRE(!v.InsertTemplate(e, error));
    v.tree.SelectItem(nullptr);
    REQUIRE(!v.InsertTemplate(e, error));
    REQUIRE(error.value == L"请先选择需要插入新指令的位置");
    std::cout << "[PASS] Native 00 gate, Section layer gate and "
                 "absent-selection diagnostic.\n";
    v.tree.SelectItem(anchor);
    REQUIRE(v.InsertTemplate(e, error));
    ++v.doc.epoch;
    REQUIRE(!v.CanUndoTemplateInsertion());
    REQUIRE(!v.UndoTemplateInsertion(error));
    first = v.tree.GetNextSiblingItem(anchor);
    TemplateDeleteRoot(v.tree, first);
    first = v.tree.GetNextSiblingItem(anchor);
    TemplateDeleteRoot(v.tree, first);
    v.cur_code_ord = before;
    v.doc.modified = false;
    std::cout << "[PASS] Unrelated content mutation invalidates stale template "
                 "undo.\n";
    auto marker = Add(v, event, 1, L"Node", anchor);
    auto nested = Add(v, event, 2, L"Node", marker);
    auto nestedEnd = Add(v, nested, 0);
    v.tree.SelectItem(nested);
    ccz::templates::Entry captured;
    REQUIRE(v.CaptureTemplateSelection(captured, error));
    REQUIRE(captured.roots.size() == 2 && captured.roots[0].id == 1 &&
            captured.roots[1].children.size() == 1);
    v.tree.SelectItem(anchor);
    REQUIRE(v.InsertTemplate(captured, error));
    REQUIRE(v.UndoTemplateInsertion(error));
    std::cout << "[PASS] Nested event capture includes its structural 01 and "
                 "terminator.\n";
    v.checkbox = true;
    v.tree.SetCheck(scene, TRUE);
    v.tree.SetCheck(section, TRUE);
    v.tree.SetCheck(anchor, TRUE);
    REQUIRE(v.CaptureTemplateSelection(captured, error));
    REQUIRE(captured.level == ccz::templates::Level::Scene &&
            captured.roots.size() == 1);
    v.checkbox = false;
    v.tree.SelectItem(scene);
    REQUIRE(v.InsertTemplate(captured, error));
    REQUIRE(v.tree.GetParentItem(v.tree.GetSelectedItem()) == root);
    REQUIRE(v.UndoTemplateInsertion(error));
    std::cout << "[PASS] Checked parent/child de-duplication and whole Scene "
                 "roundtrip.\n";
    v.tree.SelectItem(section);
    REQUIRE(v.CaptureTemplateSelection(captured, error));
    REQUIRE(captured.level == ccz::templates::Level::Section);
    REQUIRE(v.InsertTemplate(captured, error));
    REQUIRE(v.tree.GetParentItem(v.tree.GetSelectedItem()) == scene);
    REQUIRE(v.UndoTemplateInsertion(error));
    std::cout << "[PASS] Whole Section roundtrip and group undo.\n";
    for(const auto& example : std::vector<std::pair<int,std::vector<int>>>{{0x25,{1023,3,4}},{0x26,{999,1,2,7,8}},{0x32,{0,7,0,3,4,1}},{0x55,{0,7,0,3,4,1}},{0x58,{2,0,16,3,4,0,0}},{0x4f,{7,-1,1,0,1,1}},{0x50,{7,6,1,1}}}) {
      auto compact=Add(v,event,example.first,L"Preview command",anchor);
      auto* compactData=reinterpret_cast<ItemData*>(v.tree.GetItemData(compact));compactData->int_data=example.second;
      v.tree.SelectItem(compact);REQUIRE(v.CaptureTemplateSelection(captured,error));
      REQUIRE(captured.roots[0].params.size()==20);
      REQUIRE(std::equal(example.second.begin(),example.second.end(),captured.roots[0].params.begin()));
      v.tree.SelectItem(anchor);REQUIRE(v.InsertTemplate(captured,error));REQUIRE(v.UndoTemplateInsertion(error));
      TemplateDeleteRoot(v.tree,compact);
    }
    std::cout << "[PASS] Preview compact parameters capture, normalize, insert and undo.\n";
    auto external = Entry();
    external.roots[1].id = 0x76;
    external.roots[1].params[0] = 12345;
    ccz::templates::Library library; library.entries.push_back(external);
    auto encoded = ccz::templates::Encode(library);
    auto decoded = ccz::templates::Decode(encoded);
    REQUIRE(decoded.entries[0].roots[1].jump_target == -1);
    v.tree.SelectItem(anchor); externalMode = 0;
    REQUIRE(v.InsertTemplate(external,error));
    REQUIRE(reinterpret_cast<ItemData*>(v.tree.GetItemData(v.tree.GetSelectedItem()))->int_data[0] == 0);
    REQUIRE(v.UndoTemplateInsertion(error));
    externalMode = 1; externalValue = reinterpret_cast<ItemData*>(v.tree.GetItemData(anchor))->ord;
    REQUIRE(v.InsertTemplate(external,error));
    REQUIRE(reinterpret_cast<ItemData*>(v.tree.GetItemData(v.tree.GetSelectedItem()))->int_data[0] == externalValue);
    REQUIRE(v.UndoTemplateInsertion(error));
    externalMode = 2; int beforeOrdinal = v.cur_code_ord;
    REQUIRE(!v.InsertTemplate(external,error)); REQUIRE(v.cur_code_ord == beforeOrdinal);
    externalMode = 0;
    std::cout << "[PASS] External jump persistence, default zero, manual target and cancellation.\n";
    TemplateDeleteRoot(v.tree, scene);
    v.tree.DeleteItem(root);
    DestroyWindow(host);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
