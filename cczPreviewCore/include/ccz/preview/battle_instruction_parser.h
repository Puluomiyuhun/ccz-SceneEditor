#pragma once

#include <ccz/preview/battle_preview.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

// The parser receives the value-owned document tree produced by the editor
// adapter.  scope_root_key is mandatory: a battle document may contain many
// scenes, but a preview must never fill the current state by scanning them all.
struct BattleParseOptions {
    bool require_complete_field_table{true};
    bool reject_unknown_instruction{true};
    LogicalNodeKey scope_root_key;
    // When enabled, only these S deployment instructions contribute units.
    // Map and initialization records remain Section-scoped.  An empty list is
    // a valid branch with no reachable deployments.
    bool filter_deployment_nodes{};
    std::vector<LogicalNodeKey> reachable_deployment_keys;
};

struct BattleParseResult {
    // ok means the selected scope and record boundaries were parsed without
    // structural failure.  It does not grant permission to edit unresolved
    // unit fields; fixed-record page numbers are assigned in scoped source
    // order, while full group/order validation remains a writeback concern.
    // No parser output grants writeback permission.
    bool ok{};
    bool field_table_complete{};
    BattlePreviewState state;
    std::vector<std::wstring> diagnostics;
};

BattleParseResult parse_battle_instructions(
    const std::wstring& document_path,
    const std::vector<RawReplayNode>& document_nodes,
    const BattleParseOptions& options);

}  // namespace ccz::preview
