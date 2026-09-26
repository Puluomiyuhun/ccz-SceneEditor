#pragma once

#include <ccz/preview/execution_model.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_MFC_VER)
class CTreeCtrl;
#endif

namespace ccz::editor_preview {

struct CapturedEditorNode {
    std::uintptr_t runtime_handle{};
    int instruction_id{};
    int ordinal{};
    std::vector<int> int_data;
    std::wstring long_text;
    std::wstring display_text;
    std::vector<CapturedEditorNode> children;
};

struct CapturedEditorTree {
    std::wstring document_path;
    std::vector<std::uint32_t> section_path;
    std::vector<std::uint32_t> selected_path;
    // Synthetic value-owned Section container (-2).  Its children preserve
    // the native Section's complete direct-child order, including entry
    // instructions before the first top-level 0x00 boundary.
    CapturedEditorNode section_root;
    std::vector<CapturedEditorNode> document_roots;
};

struct EditorPreviewInput {
    ccz::preview::LogicalNodeKey document_key;
    ccz::preview::SectionInput section;
    std::vector<ccz::preview::RawReplayNode> document_nodes;
    std::uint64_t document_revision{};
};

struct EditorPreviewCapture {
    bool ok{};
    bool idle{};
    EditorPreviewInput input;
    // The value-owned document snapshot is retained so special-scene replay
    // can build a second Section model without walking live TreeView items.
    CapturedEditorTree snapshot;
    std::wstring diagnostic;
};

// Copy at most capacity UTF-16 code units.  The editor's ItemData owns the
// source buffer only for the current TreeView generation; no pointer escapes
// this value-owned snapshot.
std::wstring CopyLongText(const wchar_t* data, std::size_t capacity);

#if defined(_MFC_VER)
using EditorTreeItemHandle = HTREEITEM;

EditorPreviewCapture CaptureEditorPreviewInput(
    ::CTreeCtrl& tree, EditorTreeItemHandle selected_item,
    const std::wstring& document_path);

// Capture the value-owned document/Section snapshot without converting it to
// the replay-core representation.  The native editor preview uses this same
// bounded snapshot to resolve free-R Section pairs, so the lookup never walks
// raw TreeView nodes after the capture boundary.
bool CaptureEditorTreeSnapshot(
    ::CTreeCtrl& tree, EditorTreeItemHandle selected_item,
    const std::wstring& document_path, CapturedEditorTree& snapshot,
    std::wstring& diagnostic);
#endif

}  // namespace ccz::editor_preview
