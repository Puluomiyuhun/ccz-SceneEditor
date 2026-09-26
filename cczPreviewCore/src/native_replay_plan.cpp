#include <ccz/preview/native_replay_plan.h>

#include <cstddef>
#include <utility>

namespace ccz::preview {
namespace {

using ExecutionNodeList = std::vector<ExecutionNode>;

enum class WalkResult : std::uint8_t {
    Continue,
    ReachedTarget,
    WaitingForChoice,
    UnsupportedCondition,
};

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (message.empty() || destination.find(message) != std::wstring::npos) {
        return;
    }
    if (!destination.empty()) {
        destination += L"；";
    }
    destination += message;
}

bool is_native_scene_instruction(std::uint16_t instruction_id) {
    switch (instruction_id) {
        case 0x27:
        case 0x29:
        case 0x2a:
        case 0x2b:
        case 0x2f:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
            return true;
        default:
            return false;
    }
}

bool contains_target(const ExecutionNode& node,
                     const LogicalNodeKey& target_node);

bool contains_target(const ExecutionNodeList& nodes,
                     const LogicalNodeKey& target_node) {
    for (const ExecutionNode& node : nodes) {
        if (contains_target(node, target_node)) {
            return true;
        }
    }
    return false;
}

bool contains_target(const ExecutionNode& node,
                     const LogicalNodeKey& target_node) {
    return node.logical_key == target_node ||
           contains_target(node.ordinary_children, target_node) ||
           contains_target(node.true_children, target_node) ||
           contains_target(node.false_children, target_node) ||
           contains_target(node.cases, target_node);
}

const ExecutionNode* find_node(const ExecutionNodeList& nodes,
                               const LogicalNodeKey& key) {
    for (const ExecutionNode& node : nodes) {
        if (node.logical_key == key) {
            return &node;
        }
        if (const ExecutionNode* found =
                find_node(node.ordinary_children, key);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.true_children, key);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.false_children, key);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.cases, key);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const ExecutionNode* find_model_node(const SectionExecutionModel& model,
                                     const LogicalNodeKey& key) {
    if (const ExecutionNode* found = find_node(model.entry_gates, key);
        found != nullptr) {
        return found;
    }
    return find_node(model.body, key);
}

void append_native_item(NativeReplayPlan& plan, const ExecutionNode& node) {
    if (!is_native_scene_instruction(node.command.instruction_id)) {
        return;
    }

    NativeReplayItem item;
    item.logical_key = node.logical_key;
    item.runtime_handle = node.runtime_handle;
    item.command = node.command;
    plan.items.push_back(std::move(item));
}

PredicateState effective_predicate_state(const ExecutionNode& predicate,
                                         const ReplayOptions& options) {
    const auto override_state =
        options.predicate_states.find(predicate.logical_key);
    return override_state == options.predicate_states.end()
               ? predicate.predicate_state
               : override_state->second;
}

bool evaluate_predicate(const ExecutionNode& predicate,
                        const ReplayOptions& options, bool& result,
                        std::wstring& diagnostic) {
    const PredicateState state =
        effective_predicate_state(predicate, options);
    if (state == PredicateState::Unsupported ||
        predicate.predicate_kind == PredicateKind::Unsupported) {
        diagnostic = L"条件节点的静态回放不支持";
        return false;
    }
    if (state == PredicateState::ExplicitTrue) {
        result = true;
        return true;
    }
    if (state == PredicateState::ExplicitFalse) {
        result = false;
        return true;
    }

    if (predicate.predicate_kind == PredicateKind::Boolean &&
        predicate.boolean_expression.decoded) {
        if (evaluate_boolean_variable_expression(
                predicate.boolean_expression, options.simulated_variables,
                result, &diagnostic)) {
            return true;
        }
        if (diagnostic.empty()) {
            diagnostic = L"条件节点的静态回放不支持";
        }
        return false;
    }

    if (predicate.predicate_kind == PredicateKind::Integer &&
        predicate.integer_expression.decoded) {
        if (evaluate_integer_expression(
                predicate.integer_expression, options.simulated_variables,
                result, &diagnostic)) {
            return true;
        }
        if (diagnostic.empty()) {
            diagnostic = L"条件节点的静态回放不支持";
        }
        return false;
    }

    if (predicate.predicate_kind == PredicateKind::Boolean ||
        predicate.predicate_kind == PredicateKind::Integer) {
        if (predicate.boolean_evaluation == BooleanEvaluationState::True ||
            predicate.integer_evaluation == IntegerEvaluationState::True) {
            result = true;
            return true;
        }
        if (predicate.boolean_evaluation == BooleanEvaluationState::False ||
            predicate.integer_evaluation == IntegerEvaluationState::False) {
            result = false;
            return true;
        }
    }

    // 0x36/0x37 and other condition nodes controlled by the existing
    // condition-tree state use the model's default state when no structured
    // expression is available.
    result = true;
    return true;
}

const ExecutionNode* find_case_by_key(const ExecutionNode& choice,
                                      const LogicalNodeKey& case_key) {
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.logical_key == case_key) {
            return &case_node;
        }
    }
    return nullptr;
}

const ExecutionNode* find_case_with_state(const ExecutionNode& choice,
                                          ChoiceState state) {
    const ExecutionNode* selected_case = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.choice_state != state) {
            continue;
        }
        if (selected_case != nullptr) {
            return nullptr;
        }
        selected_case = &case_node;
    }
    return selected_case;
}

const ExecutionNode* find_unique_target_case(
    const ExecutionNode& choice, const LogicalNodeKey& target_node) {
    const ExecutionNode* selected_case = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (!contains_target(case_node, target_node)) {
            continue;
        }
        if (selected_case != nullptr) {
            return nullptr;
        }
        selected_case = &case_node;
    }
    return selected_case;
}

WalkResult walk_nodes(const ExecutionNodeList& nodes,
                      const LogicalNodeKey& target_node,
                      const ReplayOptions& options, NativeReplayPlan& plan);

WalkResult walk_case(const ExecutionNode& case_node,
                     const LogicalNodeKey& target_node,
                     const ReplayOptions& options, NativeReplayPlan& plan) {
    if (case_node.logical_key == target_node) {
        return WalkResult::ReachedTarget;
    }
    return walk_nodes(case_node.ordinary_children, target_node, options, plan);
}

WalkResult walk_choice(const ExecutionNode& choice,
                       const LogicalNodeKey& target_node,
                       const ReplayOptions& options, NativeReplayPlan& plan) {
    const WalkResult ordinary_result =
        walk_nodes(choice.ordinary_children, target_node, options, plan);
    if (ordinary_result != WalkResult::Continue) {
        return ordinary_result;
    }

    const ExecutionNode* selected_case = nullptr;
    bool selection_was_requested = false;
    const auto selected =
        options.selected_case_by_choice.find(choice.logical_key);
    if (selected != options.selected_case_by_choice.end()) {
        selection_was_requested = true;
        selected_case = find_case_by_key(choice, selected->second);
    } else if (choice.choice_state == ChoiceState::SelectedCase) {
        selection_was_requested = true;
        selected_case = find_case_with_state(choice, ChoiceState::SelectedCase);
    } else if (choice.choice_state == ChoiceState::AutoTarget) {
        selection_was_requested = true;
        selected_case = find_unique_target_case(choice, target_node);
    }

    if (selection_was_requested && selected_case != nullptr) {
        return walk_case(*selected_case, target_node, options, plan);
    }

    const bool target_in_case = contains_target(choice.cases, target_node);
    if (!selection_was_requested || selected_case == nullptr) {
        append_diagnostic(plan.diagnostic, L"包含未决选择支事件");
        if (target_in_case) {
            return WalkResult::WaitingForChoice;
        }
        return WalkResult::Continue;
    }
    return WalkResult::Continue;
}

WalkResult walk_node(const ExecutionNode& node,
                     const LogicalNodeKey& target_node,
                     const ReplayOptions& options, NativeReplayPlan& plan) {
    switch (node.category) {
        case ExecutionCategory::Predicate: {
            bool predicate_value = false;
            std::wstring diagnostic;
            if (!evaluate_predicate(node, options, predicate_value,
                                    diagnostic)) {
                append_diagnostic(plan.diagnostic,
                                  diagnostic.empty()
                                      ? L"条件节点的静态回放不支持"
                                      : diagnostic);
                return WalkResult::UnsupportedCondition;
            }
            const ExecutionNodeList& branch =
                predicate_value ? node.true_children : node.false_children;
            return walk_nodes(branch, target_node, options, plan);
        }
        case ExecutionCategory::Choice:
            return walk_choice(node, target_node, options, plan);
        case ExecutionCategory::IntegerDispatchGroup:
            // This is only a visual grouping.  Preserve source order and let
            // each original 0x79 node apply its own condition semantics.
            return walk_nodes(node.cases, target_node, options, plan);
        case ExecutionCategory::Case:
            // A Case is executable only when reached through its Choice.
            return WalkResult::Continue;
        case ExecutionCategory::Diagnostic:
            append_diagnostic(plan.diagnostic, node.diagnostic);
            return WalkResult::Continue;
        case ExecutionCategory::Ordinary:
        case ExecutionCategory::UnconditionalContainer:
        case ExecutionCategory::Marker:
        case ExecutionCategory::SectionGate:
        case ExecutionCategory::ElseBranch:
        case ExecutionCategory::Trigger:
        default:
            append_native_item(plan, node);
            return walk_nodes(node.ordinary_children, target_node, options,
                              plan);
    }
}

WalkResult walk_nodes(const ExecutionNodeList& nodes,
                      const LogicalNodeKey& target_node,
                      const ReplayOptions& options, NativeReplayPlan& plan) {
    for (const ExecutionNode& node : nodes) {
        // Native scene commands on the selected path are part of the plan.
        // Dialogue/condition nodes remain non-native and therefore contribute
        // no editor instruction, but a selected native command must not be
        // silently dropped at the boundary.
        if (node.logical_key == target_node) {
            append_native_item(plan, node);
            return WalkResult::ReachedTarget;
        }
        const WalkResult result =
            walk_node(node, target_node, options, plan);
        if (result != WalkResult::Continue) {
            return result;
        }
    }
    return WalkResult::Continue;
}

WalkResult walk_nodes_to_end(const ExecutionNodeList& nodes,
                             const ReplayOptions& options,
                             NativeReplayPlan& plan);

WalkResult walk_choice_to_end(const ExecutionNode& choice,
                              const ReplayOptions& options,
                              NativeReplayPlan& plan) {
    const WalkResult ordinary_result =
        walk_nodes_to_end(choice.ordinary_children, options, plan);
    if (ordinary_result != WalkResult::Continue) {
        return ordinary_result;
    }

    const ExecutionNode* selected_case = nullptr;
    const auto selected =
        options.selected_case_by_choice.find(choice.logical_key);
    if (selected != options.selected_case_by_choice.end()) {
        selected_case = find_case_by_key(choice, selected->second);
    } else if (choice.choice_state == ChoiceState::SelectedCase) {
        selected_case = find_case_with_state(choice, ChoiceState::SelectedCase);
    }
    if (selected_case == nullptr) {
        append_diagnostic(plan.diagnostic, L"包含未决选择支事件");
        return WalkResult::WaitingForChoice;
    }
    return walk_nodes_to_end(selected_case->ordinary_children, options, plan);
}

WalkResult walk_node_to_end(const ExecutionNode& node,
                            const ReplayOptions& options,
                            NativeReplayPlan& plan) {
    switch (node.category) {
        case ExecutionCategory::Predicate: {
            bool predicate_value = false;
            std::wstring diagnostic;
            if (!evaluate_predicate(node, options, predicate_value,
                                    diagnostic)) {
                append_diagnostic(plan.diagnostic,
                                  diagnostic.empty()
                                      ? L"条件节点的静态回放不支持"
                                      : diagnostic);
                return WalkResult::UnsupportedCondition;
            }
            const ExecutionNodeList& branch =
                predicate_value ? node.true_children : node.false_children;
            return walk_nodes_to_end(branch, options, plan);
        }
        case ExecutionCategory::Choice:
            return walk_choice_to_end(node, options, plan);
        case ExecutionCategory::IntegerDispatchGroup:
            return walk_nodes_to_end(node.cases, options, plan);
        case ExecutionCategory::Case:
        case ExecutionCategory::Diagnostic:
            if (node.category == ExecutionCategory::Diagnostic) {
                append_diagnostic(plan.diagnostic, node.diagnostic);
            }
            return WalkResult::Continue;
        case ExecutionCategory::Ordinary:
        case ExecutionCategory::UnconditionalContainer:
        case ExecutionCategory::Marker:
        case ExecutionCategory::SectionGate:
        case ExecutionCategory::ElseBranch:
        case ExecutionCategory::Trigger:
        default:
            append_native_item(plan, node);
            return walk_nodes_to_end(node.ordinary_children, options, plan);
    }
}

WalkResult walk_nodes_to_end(const ExecutionNodeList& nodes,
                             const ReplayOptions& options,
                             NativeReplayPlan& plan) {
    for (const ExecutionNode& node : nodes) {
        const WalkResult result = walk_node_to_end(node, options, plan);
        if (result != WalkResult::Continue) {
            return result;
        }
    }
    return WalkResult::Continue;
}

}  // namespace

NativeReplayPlan build_native_replay_plan(
    const SectionExecutionModel& model,
    const LogicalNodeKey& target_node,
    const DialogueProfile& profile,
    const ReplayOptions& options) {
    (void)profile;
    NativeReplayPlan plan;

    if (!model.boundary_complete) {
        append_diagnostic(plan.diagnostic,
                          L"Section 边界不完整，静态回放不可执行");
        return plan;
    }
    if (target_node.empty() || find_model_node(model, target_node) == nullptr) {
        append_diagnostic(plan.diagnostic,
                          L"目标节点不在当前 Section 模型中");
        return plan;
    }

    WalkResult result =
        walk_nodes(model.entry_gates, target_node, options, plan);
    if (result == WalkResult::Continue) {
        result = walk_nodes(model.body, target_node, options, plan);
    }

    switch (result) {
        case WalkResult::ReachedTarget:
            plan.stop_reason = ReplayStopReason::ReachedTarget;
            plan.target_reached = true;
            plan.complete = true;
            break;
        case WalkResult::WaitingForChoice:
            plan.stop_reason = ReplayStopReason::WaitingForChoice;
            plan.target_reached = false;
            plan.complete = false;
            break;
        case WalkResult::UnsupportedCondition:
            plan.stop_reason = ReplayStopReason::UnsupportedCondition;
            plan.target_reached = false;
            plan.complete = false;
            break;
        case WalkResult::Continue:
        default:
            plan.stop_reason = ReplayStopReason::TargetNotFound;
            plan.target_reached = false;
            plan.complete = false;
            append_diagnostic(plan.diagnostic,
                              L"目标节点不在当前 Section 回放路径中");
            break;
    }
    return plan;
}

NativeReplayPlan build_native_replay_plan_to_end(
    const SectionExecutionModel& model,
    const DialogueProfile& profile,
    const ReplayOptions& options) {
    (void)profile;
    NativeReplayPlan plan;
    if (!model.boundary_complete) {
        append_diagnostic(plan.diagnostic,
                          L"Section 边界不完整，静态回放不可执行");
        return plan;
    }

    WalkResult result = walk_nodes_to_end(model.entry_gates, options, plan);
    if (result == WalkResult::Continue) {
        result = walk_nodes_to_end(model.body, options, plan);
    }
    if (result == WalkResult::Continue) {
        plan.stop_reason = ReplayStopReason::ReachedEnd;
        plan.target_reached = true;
        plan.complete = true;
    } else if (result == WalkResult::WaitingForChoice) {
        plan.stop_reason = ReplayStopReason::WaitingForChoice;
    } else if (result == WalkResult::UnsupportedCondition) {
        plan.stop_reason = ReplayStopReason::UnsupportedCondition;
    }
    return plan;
}

}  // namespace ccz::preview
