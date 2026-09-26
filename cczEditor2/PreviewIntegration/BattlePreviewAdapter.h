#pragma once

#include "EditorPreviewAdapter.h"

#include <ccz/preview/battle_mode_classifier.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

struct BattlePreviewInput {
    ccz::preview::LogicalNodeKey document_key;
    // Prefix of the selected Section in the value-owned document tree.  It
    // deliberately omits the trailing |instruction:<id>, so the parser can
    // include only that Section and its descendants.
    ccz::preview::LogicalNodeKey scope_key;
    std::vector<ccz::preview::RawReplayNode> nodes;
    std::wstring document_path;
    std::uint64_t document_revision{};
};

struct BattlePreviewCapture {
    bool ok{};
    BattlePreviewInput input;
    CapturedEditorTree snapshot;
    ccz::preview::BattleModeDecision mode;
    std::wstring diagnostic;
};

// Build a value-owned, complete-document input for the independent Battle
// route.  The existing Section input remains available in the returned
// snapshot but is not reinterpreted by this adapter.
BattlePreviewCapture BuildBattlePreviewInput(
    const CapturedEditorTree& document_snapshot);

}  // namespace ccz::editor_preview
