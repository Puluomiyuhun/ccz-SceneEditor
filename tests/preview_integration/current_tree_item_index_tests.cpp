#include "CurrentTreeItemIndex.h"

#include <chrono>
#include <iostream>

namespace {
HTREEITEM add(HWND tree, HTREEITEM parent) {
    TVINSERTSTRUCT insert{};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT;
    insert.item.pszText = const_cast<wchar_t*>(L"node");
    return TreeView_InsertItem(tree, &insert);
}
}

int main() {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_TREEVIEW_CLASSES};
    if (!InitCommonControlsEx(&init)) return 1;
    const auto create = [] {
        return CreateWindowEx(0, WC_TREEVIEW, L"", WS_POPUP,
            0, 0, 20, 20, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    };
    const HWND tree = create();
    const HWND other = create();
    if (tree == nullptr || other == nullptr) return 2;
    const auto root = add(tree, TVI_ROOT);
    const auto child = add(tree, root);
    const auto nested = add(tree, child);
    const auto secondRoot = add(tree, TVI_ROOT);
    const auto foreign = add(other, TVI_ROOT);
    {
        const ccz::editor_preview::CurrentTreeItemIndex index(tree);
        if (index.Find(root) != root || index.Find(nested) != nested ||
            index.Find(secondRoot) != secondRoot || index.Find(nullptr) ||
            index.Find(foreign)) return 3;
        TreeView_DeleteItem(tree, nested);
        if (index.Find(nested)) return 4;
    }
    TreeView_DeleteAllItems(tree);
    {
        const ccz::editor_preview::CurrentTreeItemIndex rebuilt(tree);
        if (rebuilt.Find(root) || rebuilt.Find(child)) return 5;
    }
    std::vector<HTREEITEM> nodes;
    for (int i = 0; i < 6000; ++i) nodes.push_back(add(tree, TVI_ROOT));
    const auto start = std::chrono::steady_clock::now();
    const ccz::editor_preview::CurrentTreeItemIndex index(tree);
    for (int i = 0; i < 200; ++i)
        if (index.Find(nodes[i]) != nodes[i]) return 6;
    std::cout << "indexed lookup: nodes=6000, commands=200, ms="
        << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() << '\n';
    DestroyWindow(tree);
    if (index.Find(nodes[0])) return 7;
    DestroyWindow(other);
    std::cout << "CURRENT_TREE_ITEM_INDEX_PASS\n";
}
