#pragma once

#include <ccz/preview/execution_model.h>

#include <cstdint>
#include <map>
#include <string>

namespace ccz::preview {

enum class ConditionTreeAction : std::uint8_t {
    SetPredicateTrue,
    SetPredicateFalse,
    MakePredicateReachable,
    RestorePredicateDefault,
    SetIntegerValue,
    RestoreIntegerDefault,
    SelectCase,
    RestoreChoiceAuto,
};

using ConditionTreeActionHandler = void (*)(
    void* user_data, const LogicalNodeKey& node_key, ConditionTreeAction action,
    std::int32_t integer_value);

struct ConditionTreeActionState {
    bool can_set_true{};
    bool can_set_false{};
    bool can_make_reachable{};
    bool can_restore_predicate{};
    bool can_set_integer{};
    bool can_restore_integer{};
    bool can_select_case{};
    bool can_restore_choice_auto{};
};

enum class ConditionTreeVisualState : std::uint8_t {
    Normal,
    ActivePath,
    Unreachable,
    Diagnostic,
    Unselected,
};

using ConditionTreeExpansionMap = std::map<LogicalNodeKey, bool>;

ConditionTreeActionState condition_tree_action_state(
    const ExecutionNode* selected_node,
    const ReplayOptions& options = {});

ConditionTreeVisualState condition_tree_visual_state(const ExecutionNode& node,
                                                     bool reachable,
                                                     bool active_path,
                                                     const ReplayOptions& options = {});

// The branch projection must use the same typed result as canonical replay.
// ReplayOptions carries explicit predicate overrides and simulated variables;
// both are part of this decision rather than a separate UI-only state.
bool condition_tree_predicate_branch_reachable(const ExecutionNode& node,
                                               bool parent_reachable,
                                               bool true_branch,
                                               const ReplayOptions& options = {});

std::wstring condition_tree_node_title(
    const ExecutionNode& node, const ReplayOptions& options = {});

bool condition_tree_should_expand(const ConditionTreeExpansionMap& remembered,
                                  const LogicalNodeKey& node_key,
                                  bool is_section_root, bool is_entry_root,
                                  bool is_active_path);

// Top-level editor operations retained from the phase2 preview bridge.  The
// activation plan is computed from the same typed execution model used by
// replay; it may select a Choice Case and/or set the predicates that block the
// current target in one transaction.
bool condition_tree_model_has_controls(const SectionExecutionModel& model);

bool condition_tree_model_can_make_node_reachable(
    const SectionExecutionModel& model, const LogicalNodeKey& node_key);

bool condition_tree_model_can_activate_target(
    const SectionExecutionModel& model, const LogicalNodeKey& target_key,
    const ReplayOptions& options = {});

bool activate_condition_tree_target(
    ReplayOptions& options, const SectionExecutionModel& model,
    const LogicalNodeKey& target_key, std::wstring* diagnostic = nullptr,
    const SimulationEnvironment* displayed_variables = nullptr);

}  // namespace ccz::preview
