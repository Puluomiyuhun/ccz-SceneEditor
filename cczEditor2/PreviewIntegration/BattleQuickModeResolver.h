#pragma once

#include "BattlePreviewAdapter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

struct BattleQuickModeResolution {
    bool resolved{};
    std::uintptr_t section_handle{};
    std::vector<std::uint32_t> section_path;
    BattlePreviewCapture capture;
    std::wstring diagnostic;
};

// Resolve exactly one valid battle-map Section among Scene1's direct Sections.
// Sections in later Scenes are never considered.
BattleQuickModeResolution ResolveScene1BattleSection(
    CapturedEditorTree document_snapshot);

// Rebind a complete fresh document capture to a previously pinned Section.
// Both the runtime handle and source path must still identify the same node.
bool SelectCapturedBattleSection(
    CapturedEditorTree document_snapshot,
    std::uintptr_t section_handle,
    const std::vector<std::uint32_t>& section_path,
    CapturedEditorTree& selected_snapshot,
    std::wstring& diagnostic);

}  // namespace ccz::editor_preview
