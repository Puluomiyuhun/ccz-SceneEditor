#include "pch.h"

#include "EditorPreviewAdapter.h"
#include "../resource.h"
#include "../cczEditor2Doc.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ccz::editor_preview {
namespace {

constexpr std::size_t kMaximumTreeDepth = 128;
constexpr std::size_t kMaximumCapturedNodes = 100000;

struct CaptureBudget {
    std::size_t node_count{};
};

struct CaptureContext {
    CTreeCtrl& tree;
    CaptureBudget& budget;
    std::unordered_set<HTREEITEM> visited;
    std::wstring diagnostic;
};

void fail(CaptureContext& context, const wchar_t* message) {
    if (context.diagnostic.empty()) {
        context.diagnostic = message;
    }
}

bool SiblingIndex(CTreeCtrl& tree, HTREEITEM item, std::uint32_t& index,
                  std::wstring& diagnostic) {
    if (item == nullptr) {
        diagnostic = L"树节点句柄为空";
        return false;
    }
    HTREEITEM sibling = tree.GetParentItem(item) == nullptr
                            ? tree.GetRootItem()
                            : tree.GetChildItem(tree.GetParentItem(item));
    index = 0;
    std::unordered_set<HTREEITEM> visited;
    while (sibling != nullptr) {
        if (!visited.insert(sibling).second) {
            diagnostic = L"树节点兄弟链存在循环";
            return false;
        }
        if (sibling == item) {
            return true;
        }
        ++index;
        sibling = tree.GetNextSiblingItem(sibling);
    }
    diagnostic = L"树节点不在预期的兄弟链中";
    return false;
}

bool CaptureNode(CaptureContext& context, HTREEITEM item, std::size_t depth,
                 CapturedEditorNode& destination,
                 bool container_node = false) {
    if (item == nullptr) {
        fail(context, L"树节点句柄为空");
        return false;
    }
    if (depth > kMaximumTreeDepth) {
        fail(context, L"剧本树深度超出回放安全上限");
        return false;
    }
    if (context.budget.node_count >= kMaximumCapturedNodes) {
        fail(context, L"剧本树节点数量超出回放安全上限");
        return false;
    }
    ++context.budget.node_count;
    if (!context.visited.insert(item).second) {
        fail(context, L"剧本树遍历检测到重复节点");
        return false;
    }

    destination = {};
    destination.runtime_handle = reinterpret_cast<std::uintptr_t>(item);
    destination.display_text = context.tree.GetItemText(item).GetString();

    // The document root is a TreeView-only container.  CreateRoot() sets its
    // lParam to zero, so it has no ItemData and must never be interpreted as
    // an editor instruction.  Keep the container in the owned snapshot so
    // document-wide indexing can still walk its children, using the same
    // invalid instruction marker as the old bridge's container path.
    if (container_node) {
        destination.instruction_id = -1;
    } else {
        auto* data = reinterpret_cast<ItemData*>(context.tree.GetItemData(item));
        if (data == nullptr) {
            fail(context, L"树节点 ItemData 为空");
            return false;
        }
        destination.instruction_id = data->id;
        destination.ordinal = data->ord;
        destination.int_data = data->int_data;
        destination.long_text =
            CopyLongText(data->long_char_data, data->long_char_data_capacity);
    }

    for (HTREEITEM child = context.tree.GetChildItem(item); child != nullptr;
         child = context.tree.GetNextSiblingItem(child)) {
        CapturedEditorNode captured_child;
        if (!CaptureNode(context, child, depth + 1, captured_child)) {
            destination = {};
            return false;
        }
        destination.children.push_back(std::move(captured_child));
    }
    return true;
}

bool BuildSectionPath(CTreeCtrl& tree, HTREEITEM root, HTREEITEM section,
                      std::vector<std::uint32_t>& path,
                      std::wstring& diagnostic) {
    std::vector<std::uint32_t> reversed;
    HTREEITEM current = section;
    std::unordered_set<HTREEITEM> visited;
    while (current != root) {
        if (!visited.insert(current).second) {
            diagnostic = L"树节点父级链存在循环";
            return false;
        }
        HTREEITEM parent = tree.GetParentItem(current);
        if (parent == nullptr) {
            diagnostic = L"Section 不在当前剧本文档根节点下";
            return false;
        }
        std::uint32_t index = 0;
        if (!SiblingIndex(tree, current, index, diagnostic)) {
            return false;
        }
        reversed.push_back(index);
        current = parent;
    }
    std::reverse(reversed.begin(), reversed.end());
    path = std::move(reversed);
    return !path.empty();
}

bool FindSection(CTreeCtrl& tree, HTREEITEM selected, HTREEITEM& section,
                 std::wstring& diagnostic) {
    std::unordered_set<HTREEITEM> visited;
    HTREEITEM current = selected;
    while (current != nullptr) {
        if (!visited.insert(current).second) {
            diagnostic = L"选中节点父级链存在循环";
            return false;
        }
        auto* data = reinterpret_cast<ItemData*>(tree.GetItemData(current));
        if (data == nullptr) {
            // The document root is a container with lParam == 0.  Reaching
            // it means the selection is outside every Section, which is a
            // normal idle state rather than a malformed instruction node.
            if (tree.GetParentItem(current) == nullptr) {
                section = nullptr;
                return true;
            }
            diagnostic = L"无法读取选中节点 ItemData";
            return false;
        }
        if (data->id == -2) {
            section = current;
            return true;
        }
        current = tree.GetParentItem(current);
    }
    section = nullptr;
    return true;
}

bool BuildSelectedPath(CTreeCtrl& tree, HTREEITEM boundary,
                       HTREEITEM selected,
                       std::vector<std::uint32_t>& path,
                       std::wstring& diagnostic) {
    if (selected == boundary) {
        path.clear();
        return true;
    }

    std::vector<std::uint32_t> reversed;
    std::unordered_set<HTREEITEM> visited;
    HTREEITEM current = selected;
    while (current != boundary) {
        if (current == nullptr || !visited.insert(current).second) {
            diagnostic = L"选中节点不在 Section 边界下";
            return false;
        }
        HTREEITEM parent = tree.GetParentItem(current);
        if (parent == nullptr) {
            diagnostic = L"选中节点不在 Section 边界下";
            return false;
        }
        std::uint32_t index = 0;
        if (!SiblingIndex(tree, current, index, diagnostic)) {
            return false;
        }
        reversed.push_back(index);
        current = parent;
    }
    std::reverse(reversed.begin(), reversed.end());
    path = std::move(reversed);
    return true;
}

bool CaptureDocumentRoot(CTreeCtrl& tree, HTREEITEM root,
                         CaptureBudget& capture_budget,
                         CapturedEditorNode& document_root,
                         std::wstring& diagnostic) {
    CaptureContext context{tree, capture_budget};
    if (!CaptureNode(context, root, 0, document_root, true)) {
        diagnostic = context.diagnostic;
        return false;
    }
    return true;
}

}  // namespace

bool CaptureEditorDocumentSnapshot(
    CTreeCtrl& tree, const std::wstring& document_path,
    CapturedEditorTree& snapshot, std::wstring& diagnostic) {
    snapshot = {};
    snapshot.document_path = document_path;
    diagnostic.clear();
    snapshot.selected_handle = reinterpret_cast<std::uintptr_t>(
        tree.GetSelectedItem());
    const HTREEITEM root = tree.GetRootItem();
    if (root == nullptr) {
        diagnostic = L"剧本文档树为空";
        return false;
    }

    CaptureBudget capture_budget;
    CapturedEditorNode document_root;
    if (!CaptureDocumentRoot(tree, root, capture_budget, document_root,
                             diagnostic)) {
        return false;
    }
    snapshot.document_roots.push_back(std::move(document_root));
    return true;
}

bool CaptureEditorPinnedSectionSnapshot(
    CTreeCtrl& tree, EditorTreeItemHandle section_item,
    const std::vector<std::uint32_t>& expected_section_path,
    const std::wstring& document_path, CapturedEditorTree& snapshot,
    std::wstring& diagnostic) {
    snapshot = {};
    snapshot.document_path = document_path;
    diagnostic.clear();
    if (section_item == nullptr || tree.GetSafeHwnd() == nullptr) {
        diagnostic = L"快速模式绑定的 Scene1 Section 句柄无效";
        return false;
    }
    const HTREEITEM root = tree.GetRootItem();
    auto* data = reinterpret_cast<ItemData*>(tree.GetItemData(section_item));
    if (root == nullptr || data == nullptr || data->id != -2) {
        diagnostic = L"快速模式绑定节点已不再是剧本 Section";
        return false;
    }

    std::vector<std::uint32_t> actual_path;
    if (!BuildSectionPath(tree, root, section_item, actual_path, diagnostic))
        return false;
    if (actual_path != expected_section_path) {
        diagnostic = L"快速模式绑定的 Scene1 Section 路径已变化";
        return false;
    }

    CaptureBudget capture_budget;
    CaptureContext context{tree, capture_budget};
    if (!CaptureNode(context, section_item, 0, snapshot.section_root)) {
        diagnostic = context.diagnostic.empty()
            ? L"快速模式无法读取固定的 Scene1 Section"
            : context.diagnostic;
        snapshot = {};
        return false;
    }
    snapshot.section_root.instruction_id = -2;
    snapshot.section_path = std::move(actual_path);
    return true;
}

bool CaptureEditorTreeSnapshot(
    CTreeCtrl& tree, EditorTreeItemHandle selected_item,
    const std::wstring& document_path, CapturedEditorTree& snapshot,
    std::wstring& diagnostic) {
    snapshot = {};
    snapshot.document_path = document_path;
    HTREEITEM selected = selected_item != nullptr
                             ? selected_item
                             : tree.GetSelectedItem();
    snapshot.selected_handle = reinterpret_cast<std::uintptr_t>(selected);
    if (selected == nullptr) {
        return true;
    }

    const HTREEITEM root = tree.GetRootItem();
    if (root == nullptr) {
        diagnostic = L"剧本文档树为空";
        return false;
    }

    HTREEITEM section = nullptr;
    if (!FindSection(tree, selected, section, diagnostic)) {
        return false;
    }
    if (section == nullptr) {
        return true;
    }

    if (!BuildSectionPath(tree, root, section, snapshot.section_path,
                          diagnostic)) {
        diagnostic = diagnostic.empty() ? L"无法生成 Section 逻辑路径"
                                        : diagnostic;
        return false;
    }

    CaptureBudget capture_budget;
    CapturedEditorNode document_root;
    if (!CaptureDocumentRoot(tree, root, capture_budget, document_root,
                             diagnostic)) {
        return false;
    }
    snapshot.document_roots.push_back(std::move(document_root));

    CaptureContext section_context{tree, capture_budget};
    // Keep the Section itself as a value-owned container.  Its direct
    // children are an ordered sequence of entry instructions followed by the
    // first top-level 0x00 boundary and its body.  Do not assume that the
    // boundary is the first child; real scripts commonly have 0x02/condition
    // entries before it.
    if (!CaptureNode(section_context, section, 0, snapshot.section_root,
                     true)) {
        diagnostic = section_context.diagnostic;
        return false;
    }
    snapshot.section_root.instruction_id = -2;

    if (selected == section) {
        HTREEITEM boundary = nullptr;
        for (HTREEITEM child = tree.GetChildItem(section); child != nullptr;
             child = tree.GetNextSiblingItem(child)) {
            auto* data = reinterpret_cast<ItemData*>(tree.GetItemData(child));
            if (data != nullptr && data->id == 0) {
                boundary = child;
                break;
            }
        }
        // The adapter will reject a Section without a boundary.  Leave the
        // path empty here so special-scene document capture can still proceed
        // far enough to report its own classification/diagnostic.
        if (boundary != nullptr) {
            selected = boundary;
        }
    }
    if (selected == section) {
        snapshot.selected_path.clear();
    } else if (!BuildSelectedPath(tree, section, selected,
                                  snapshot.selected_path,
                            diagnostic)) {
        return false;
    }

    return true;
}

EditorPreviewCapture CaptureEditorPreviewInput(
    CTreeCtrl& tree, EditorTreeItemHandle selected_item,
    const std::wstring& document_path) {
    EditorPreviewCapture result;
    CapturedEditorTree snapshot;
    if (!CaptureEditorTreeSnapshot(tree, selected_item, document_path,
                                   snapshot, result.diagnostic)) {
        return result;
    }
    if (snapshot.section_path.empty()) {
        result.ok = true;
        result.idle = true;
        result.snapshot = std::move(snapshot);
        return result;
    }
    EditorPreviewCapture converted = BuildEditorPreviewInput(snapshot);
    converted.snapshot = std::move(snapshot);
    return converted;
}

}  // namespace ccz::editor_preview
