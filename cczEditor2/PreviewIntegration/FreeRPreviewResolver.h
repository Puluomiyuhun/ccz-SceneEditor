#pragma once

#include "EditorTreeSnapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

// Scene-pair preview is intentionally bounded to one adjacent two-Scene pair.
// The Scene handles below identify the resource/context owner; matching and
// replay still happen at Section granularity.
enum class ScenePreviewKind {
    Ordinary,
    Deployment,
    FreeR,
};

struct ScenePreviewResolution {
    bool is_free_r{};
    bool resolved{};
    bool has_click_target{};
    int target_person_id{-1};
    ScenePreviewKind kind{ScenePreviewKind::Ordinary};
    std::uintptr_t initial_scene_handle{};
    std::uintptr_t trigger_scene_handle{};
    std::uintptr_t appearance_scene_handle{};
    std::uintptr_t appearance_section_handle{};
    std::uintptr_t appearance_instruction_handle{};
    std::uintptr_t trigger_section_handle{};
    // Direct Sections in the trigger Scene that precede the selected
    // Section.  Free-R is an event tree, but replay remains bounded to this
    // adjacent trigger Scene pair and preserves the source Section order.
    std::vector<std::uintptr_t> trigger_section_handles_before_selected;
    std::wstring diagnostic;
};

using FreeRPreviewResolution = ScenePreviewResolution;

ScenePreviewResolution ResolveScenePreview(
    const CapturedEditorNode& document_root,
    std::uintptr_t trigger_section_handle,
    std::uintptr_t selected_handle);

}  // namespace ccz::editor_preview
