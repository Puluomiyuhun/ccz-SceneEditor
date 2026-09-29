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
	// Content-only revision for the currently captured Section. Quick mode uses
	// this to follow edits in its pinned Scene1 source without reacting to other
	// Sections in the same document.
	std::uint64_t section_revision{};
};

struct BattlePreviewCapture {
    bool ok{};
    BattlePreviewInput input;
    CapturedEditorTree snapshot;
    ccz::preview::BattleModeDecision mode;
    std::wstring diagnostic;
};

// S-only insertion target selected from the active condition-tree projection.
// Each deployment instruction type can live in a different child event.
struct BattleDeploymentInsertionAnchor {
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey parent_key;
    ccz::preview::LogicalNodeKey before_key;
    bool resolved{};
    std::wstring diagnostic;
};

// Build a value-owned, complete-document input for the independent Battle
// route.  The existing Section input remains available in the returned
// snapshot but is not reinterpreted by this adapter.
BattlePreviewCapture BuildBattlePreviewInput(
    CapturedEditorTree document_snapshot);

// Content-only revision shared by full preview capture and the quick-mode
// pinned-Section fast path.
std::uint64_t ComputeBattleSectionRevision(
    const std::wstring& document_path,
    const CapturedEditorNode& section_root);

}  // namespace ccz::editor_preview
