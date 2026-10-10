#pragma once

#include <windows.h>
#include <commctrl.h>
#include <unordered_set>
#include <vector>

namespace ccz::editor_preview {

// Local to one synchronous replay transaction. Never retain this index across
// a message dispatch or a TreeView rebuild.
class CurrentTreeItemIndex {
public:
    explicit CurrentTreeItemIndex(HWND tree) : tree_(tree) {
        if (tree_ == nullptr || !::IsWindow(tree_)) return;
        std::vector<HTREEITEM> pending;
        for (auto root = TreeView_GetRoot(tree_); root != nullptr;
             root = TreeView_GetNextSibling(tree_, root)) {
            pending.push_back(root);
        }
        while (!pending.empty()) {
            const auto item = pending.back();
            pending.pop_back();
            items_.insert(item);
            for (auto child = TreeView_GetChild(tree_, item); child != nullptr;
                 child = TreeView_GetNextSibling(tree_, child)) {
                pending.push_back(child);
            }
        }
    }

    HTREEITEM Find(HTREEITEM expected) const {
        if (expected == nullptr || items_.find(expected) == items_.end() ||
            !::IsWindow(tree_)) return nullptr;
        TVITEM item{};
        item.mask = TVIF_HANDLE;
        item.hItem = expected;
        return TreeView_GetItem(tree_, &item) ? expected : nullptr;
    }

private:
    HWND tree_{};
    std::unordered_set<HTREEITEM> items_;
};

} // namespace ccz::editor_preview
