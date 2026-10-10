#pragma once

#include "EditorTreeSnapshot.h"

namespace ccz::editor_preview {

EditorPreviewCapture BuildEditorPreviewInput(
    const CapturedEditorTree& snapshot);

EditorPreviewCapture BuildEditorPreviewInputForSection(
    const CapturedEditorTree& document_snapshot,
    std::uintptr_t section_handle,
    std::uintptr_t selected_handle);

// Build a Section model whose reachable native commands can be replayed to
// its end.  The first top-level 0x00 is used only as a stable synthetic
// target; the end-plan builder does not stop at that target.
EditorPreviewCapture BuildEditorPreviewInputForSectionToEnd(
    const CapturedEditorTree& document_snapshot,
    std::uintptr_t section_handle);

}  // namespace ccz::editor_preview
