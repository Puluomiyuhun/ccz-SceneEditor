#pragma once

#include <ccz/preview/execution_model.h>

#include <string>
#include <vector>

namespace ccz::preview {

// Battle preview is intentionally a separate mode.  Ordinary R documents
// may contain instruction ids that are also meaningful to the battle editor,
// so the classifier must never infer Battle from an instruction id alone.
enum class PreviewMode : std::uint8_t {
    Ordinary,
    Deployment,
    FreeR,
    Battle,
    Unresolved,
};

struct BattleModeDecision {
    PreviewMode mode{PreviewMode::Unresolved};
    bool prefix_candidate{};
    bool marker_valid{};
    std::wstring diagnostic;
};

// Classify an entire value-owned document snapshot.  S_ documents are only
// Battle candidates; at least one recursively visible Battle marker (0x44,
// 0x46, 0x47 or 0x4B) is required before the mode becomes Battle.
BattleModeDecision classify_preview_mode(
    const std::wstring& document_path,
    const std::vector<RawReplayNode>& document_nodes);

bool contains_battle_marker(const std::vector<RawReplayNode>& nodes);

}  // namespace ccz::preview
