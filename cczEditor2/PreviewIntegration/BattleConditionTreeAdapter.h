#pragma once

#include "BattlePreviewAdapter.h"

#include <ccz/preview/execution_model.h>

#include <memory>
#include <string>
#include <vector>

namespace ccz::editor_preview {

struct BattleConditionScope {
    // Stable key for a preview/cache state with the same reachable deployment
    // sources and insertion destination.
    ccz::preview::LogicalNodeKey workspace_key;
    std::vector<ccz::preview::LogicalNodeKey> reachable_deployment_keys;
    ccz::preview::LogicalNodeKey insert_parent_key;
    ccz::preview::LogicalNodeKey insert_before_key;
    std::vector<BattleDeploymentInsertionAnchor> insertion_anchors;
    bool writeback_scope_resolved{};
    bool insertion_anchor_resolved{};
    std::wstring diagnostic;
};

struct BattleConditionTreeBuild {
    bool ok{};
    std::shared_ptr<const ccz::preview::SectionExecutionModel> model;
    BattleConditionScope scope;
    std::wstring diagnostic;
};

// Builds the same typed condition model used by the R condition tree, then
// projects only reachable S deployment sources and their child-event anchor.
// The supplied ReplayOptions are independent of the R preview session.
BattleConditionTreeBuild BuildBattleConditionTree(
    const BattlePreviewCapture& capture,
    const ccz::preview::InstructionCatalog& catalog,
    ccz::preview::ReplayOptions& options);

}  // namespace ccz::editor_preview
