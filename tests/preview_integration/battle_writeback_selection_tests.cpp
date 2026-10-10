#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <vector>
#include <iostream>

namespace ccz::editor_preview {
enum class BattleWritebackChangeKind { UpdateInstruction, InsertInstruction };
struct Change { BattleWritebackChangeKind kind; std::uintptr_t item_handle; };
struct BattleWritebackNativeBatch { std::vector<Change> changes; };
}
struct CTreeCtrl {
    HWND window;
    DWORD_PTR GetItemData(HTREEITEM item) {
        TVITEM data{}; data.mask = TVIF_PARAM; data.hItem = item;
        return TreeView_GetItem(window, &data) ? data.lParam : 0;
    }
    BOOL EnsureVisible(HTREEITEM item) { return TreeView_EnsureVisible(window, item); }
    BOOL SelectItem(HTREEITEM item) { return TreeView_SelectItem(window, item); }
    HTREEITEM GetSelectedItem() { return TreeView_GetSelection(window); }
};
struct CcczEditor2View {
    CTreeCtrl tree;
    HTREEITEM cur_item{};
    CTreeCtrl& GetTreeCtrl() { return tree; }
};

// PRODUCTION_HELPER

int main() {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_TREEVIEW_CLASSES};
    if (!InitCommonControlsEx(&init)) return 1;
    HWND parent = CreateWindowW(L"STATIC", L"Test", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND window = CreateWindowW(WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE,
        0, 0, 600, 400, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent || !window) return 2;
    const auto insert = [window](HTREEITEM parentItem) {
        TVINSERTSTRUCTW data{}; data.hParent = parentItem; data.hInsertAfter = TVI_LAST;
        data.item.mask = TVIF_TEXT | TVIF_PARAM;
        data.item.pszText = const_cast<wchar_t*>(L"Instruction"); data.item.lParam = 1;
        return TreeView_InsertItem(window, &data);
    };
    CcczEditor2View view{{window}};
    const auto root = insert(TVI_ROOT);
    const auto anchor = insert(root);
    const auto first = insert(root);
    const auto last = insert(root);
    TreeView_Expand(window, root, TVE_EXPAND);
    view.tree.SelectItem(anchor);
    ccz::editor_preview::BattleWritebackNativeBatch batch;
    batch.changes = {{ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction, 0},
                     {ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction, 0}};
    if (view.tree.EnsureVisible(last) != FALSE) return 3;
    if (!RevealNativeWritebackInstruction(view, batch, reinterpret_cast<std::uintptr_t>(last)) ||
        view.tree.GetSelectedItem() != last || view.cur_item != last) return 4;
    std::cout << "[PASS] Already-visible final inserted instruction replaces the old anchor selection.\n";
    const auto group = insert(root);
    const auto nested = insert(group);
    view.tree.SelectItem(anchor);
    TreeView_Expand(window, group, TVE_COLLAPSE);
    if (!RevealNativeWritebackInstruction(view, batch, reinterpret_cast<std::uintptr_t>(nested)) ||
        view.tree.GetSelectedItem() != nested || view.cur_item != nested) return 5;
    std::cout << "[PASS] Final inserted instruction inside a collapsed event is expanded and selected.\n";
    if (RevealNativeWritebackInstruction(view, batch, 0) ||
        view.tree.GetSelectedItem() != nested) return 6;
    std::cout << "[PASS] Missing insert handle does not change selection.\n";
    batch.changes = {{ccz::editor_preview::BattleWritebackChangeKind::UpdateInstruction,
                      reinterpret_cast<std::uintptr_t>(first)}};
    if (!RevealNativeWritebackInstruction(view, batch, 0) ||
        view.tree.GetSelectedItem() != first) return 7;
    std::cout << "[PASS] Ordinary deployment writeback still selects the updated instruction.\n";
    DestroyWindow(parent);
}
