#include <ccz/preview/condition_tree_state.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace ccz::preview {
namespace {

std::wstring category_text(const ExecutionNode& node) {
    switch (node.category) {
        case ExecutionCategory::Ordinary:
            return L"事件";
        case ExecutionCategory::Marker:
            return L"结构";
        case ExecutionCategory::UnconditionalContainer:
            return L"无条件";
        case ExecutionCategory::Predicate:
            switch (node.predicate_kind) {
                case PredicateKind::Boolean:
                    return L"条件·布尔";
                case PredicateKind::Integer:
                    return L"条件·整型";
                case PredicateKind::Unsupported:
                    return L"条件·不可模拟";
                case PredicateKind::None:
                default:
                    return L"条件";
            }
        case ExecutionCategory::ElseBranch:
            return L"分支·假";
        case ExecutionCategory::Choice:
            return L"选择";
        case ExecutionCategory::Case:
            return L"Case";
        case ExecutionCategory::IntegerDispatchGroup:
            return L"整型分派";
        case ExecutionCategory::SectionGate:
            return L"区域";
        case ExecutionCategory::Trigger:
            return L"触发";
        case ExecutionCategory::Diagnostic:
        default:
            return L"诊断";
    }
}

std::wstring instruction_id_text(std::uint16_t instruction_id) {
    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex << std::setw(2)
           << std::setfill(L'0') << instruction_id;
    return stream.str();
}

PredicateState effective_predicate_state_for_ui(
    const ExecutionNode& node, const ReplayOptions& options) {
    const auto override_state =
        options.predicate_states.find(node.logical_key);
    const auto state = override_state == options.predicate_states.end()
        ? node.predicate_state : override_state->second;
    if (options.unresolved_status_conditions.count(node.logical_key) &&
        state != PredicateState::ExplicitTrue && state != PredicateState::ExplicitFalse)
        return PredicateState::Unsupported;
    return state;
}

bool evaluate_predicate_for_ui(const ExecutionNode& node,
                               const ReplayOptions& options, bool& result) {
    const PredicateState state =
        effective_predicate_state_for_ui(node, options);
    if (state == PredicateState::Unsupported ||
        node.predicate_kind == PredicateKind::Unsupported) {
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
    if (node.predicate_kind == PredicateKind::Integer &&
        node.integer_expression.decoded) {
        return evaluate_integer_expression(node.integer_expression,
                                           options.simulated_variables, result,
                                           nullptr);
    }
    if (node.instruction_id == 0x05 && node.boolean_expression.decoded) {
        return evaluate_boolean_variable_expression(
            node.boolean_expression, options.simulated_variables, result,
            nullptr);
    }
    if (node.integer_evaluation == IntegerEvaluationState::True ||
        node.boolean_evaluation == BooleanEvaluationState::True) {
        result = true;
        return true;
    }
    if (node.integer_evaluation == IntegerEvaluationState::False ||
        node.boolean_evaluation == BooleanEvaluationState::False) {
        result = false;
        return true;
    }
    return true;
}

std::wstring state_text(const ExecutionNode& node,
                        const ReplayOptions& options) {
    switch (node.category) {
        case ExecutionCategory::Predicate:
            {
                const PredicateState state =
                    effective_predicate_state_for_ui(node, options);
                if (state == PredicateState::Unsupported) {
                    return options.unresolved_status_conditions.count(node.logical_key)
                        ? L"状态未知，请指定结果" : L"不可模拟";
                }
                if (state == PredicateState::ExplicitTrue) {
                    return L"已设真";
                }
                if (state == PredicateState::ExplicitFalse) {
                    return L"已设假";
                }
            }
            if (node.instruction_id == 0x05 &&
                node.boolean_expression.decoded) {
                bool result = false;
                if (!evaluate_predicate_for_ui(node, options, result)) {
                    return L"不可模拟";
                }
                return result ? L"自动结果：真" : L"自动结果：假";
            }
            if (node.predicate_kind == PredicateKind::Integer) {
                bool predicate_result = false;
                if (!evaluate_predicate_for_ui(node, options,
                                               predicate_result)) {
                    return L"不可模拟";
                }
                if (!node.integer_value_available) {
                    return L"整型值未解析";
                }
                std::wstring text = L"整型值 " +
                                    std::to_wstring(node.simulated_integer_value);
                text += predicate_result ? L" -> 真" : L" -> 假";
                return text;
            }
            switch (effective_predicate_state_for_ui(node, options)) {
                case PredicateState::ExplicitTrue:
                    return L"已设真";
                case PredicateState::ExplicitFalse:
                    return L"已设假";
                case PredicateState::Unsupported:
                    return L"不可模拟";
                case PredicateState::DefaultTrue:
                default:
                    return L"默认真";
            }
        case ExecutionCategory::Choice:
            switch (node.choice_state) {
                case ChoiceState::SelectedCase:
                    return L"已选分支";
                case ChoiceState::AutoTarget:
                    return L"自动目标";
                case ChoiceState::Unselected:
                default:
                    return L"需要选择分支";
            }
        case ExecutionCategory::Case:
            switch (node.choice_state) {
                case ChoiceState::SelectedCase:
                    return L"当前分支";
                case ChoiceState::AutoTarget:
                    return L"自动分支";
                case ChoiceState::Unselected:
                default:
                    return L"未选分支";
            }
        case ExecutionCategory::Diagnostic:
            return L"诊断";
        default:
            return std::wstring();
    }
}

bool has_known_id_prefix(const std::wstring& title,
                         std::uint16_t instruction_id) {
    const std::wstring prefix = instruction_id_text(instruction_id) + L":";
    return title.size() >= prefix.size() &&
           title.compare(0, prefix.size(), prefix) == 0;
}

std::wstring instruction_name_part(const std::wstring& title,
                                   std::uint16_t instruction_id) {
    const std::wstring prefix = instruction_id_text(instruction_id) + L":";
    if (has_known_id_prefix(title, instruction_id)) {
        return title.substr(prefix.size());
    }
    return title;
}

std::wstring decimal_id_text(std::uint16_t instruction_id) {
    return std::to_wstring(instruction_id);
}

std::wstring bare_hex_id_text(std::uint16_t instruction_id) {
    const std::wstring prefixed = instruction_id_text(instruction_id);
    return prefixed.substr(2);
}

std::wstring parameters_without_repeated_header(const ExecutionNode& node) {
    if (node.instruction_id == 0x79) {
        return node.integer_expression.decoded
                   ? format_integer_expression(node.integer_expression)
                   : L"0x79 参数不可解码";
    }
    if (node.decoded_parameters.empty()) {
        return std::wstring();
    }

    const std::wstring name =
        instruction_name_part(node.instruction_title, node.instruction_id);
    std::vector<std::wstring> prefixes;
    prefixes.push_back(instruction_id_text(node.instruction_id) + L":" + name);
    prefixes.push_back(decimal_id_text(node.instruction_id) + L":" + name);
    const std::wstring bare_hex = bare_hex_id_text(node.instruction_id);
    prefixes.push_back(bare_hex + L":" + name);
    if (bare_hex.size() > 1 && bare_hex.front() == L'0') {
        prefixes.push_back(bare_hex.substr(1) + L":" + name);
    }
    if (has_known_id_prefix(node.instruction_title, node.instruction_id)) {
        prefixes.push_back(node.instruction_title);
    }

    std::size_t start = 0;
    while (start < node.decoded_parameters.size() &&
           std::iswspace(node.decoded_parameters[start]) != 0) {
        ++start;
    }
    const std::wstring_view parameters(node.decoded_parameters.data() + start,
                                       node.decoded_parameters.size() - start);
    for (const std::wstring& prefix : prefixes) {
        if (parameters.size() < prefix.size() ||
            parameters.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        if (parameters.size() != prefix.size() &&
            std::iswspace(parameters[prefix.size()]) == 0) {
            continue;
        }
        std::size_t parameter_start = prefix.size();
        while (parameter_start < parameters.size() &&
               std::iswspace(parameters[parameter_start]) != 0) {
            ++parameter_start;
        }
        return std::wstring(parameters.substr(parameter_start));
    }
    return node.decoded_parameters;
}

}  // namespace

ConditionTreeActionState condition_tree_action_state(
    const ExecutionNode* selected_node, const ReplayOptions& options) {
    ConditionTreeActionState result;
    if (selected_node == nullptr) {
        return result;
    }
    if (!selected_node->diagnostic.empty()) {
        return result;
    }

    if (selected_node->category == ExecutionCategory::Predicate &&
        selected_node->predicate_kind == PredicateKind::Integer) {
        const IntegerComparisonExpression& expression =
            selected_node->integer_expression;
        bool predicate_result = false;
        const bool editable =
            expression.decoded &&
            expression.left.kind == IntegerOperandKind::IntegerVariable &&
            expression.comparator != IntegerComparisonOperator::Unsupported &&
            effective_predicate_state_for_ui(*selected_node, options) !=
                PredicateState::Unsupported &&
            evaluate_predicate_for_ui(*selected_node, options,
                                      predicate_result);
        result.can_set_integer = editable;
        result.can_restore_integer = editable;
        result.can_make_reachable = editable;
    } else if (selected_node->category == ExecutionCategory::Predicate &&
               selected_node->instruction_id == 0x05 &&
               selected_node->boolean_expression.decoded) {
        bool conflict = false;
        for (const std::int32_t variable :
             selected_node->boolean_expression.true_variables) {
            if (std::find(selected_node->boolean_expression.false_variables.begin(),
                          selected_node->boolean_expression.false_variables.end(),
                          variable) !=
                selected_node->boolean_expression.false_variables.end()) {
                conflict = true;
                break;
            }
        }
        bool predicate_result = false;
        if (!conflict &&
            evaluate_predicate_for_ui(*selected_node, options,
                                      predicate_result)) {
            result.can_set_true = true;
            result.can_set_false = true;
            result.can_restore_predicate = true;
            result.can_make_reachable = true;
        }
    } else if (selected_node->category == ExecutionCategory::Predicate &&
               selected_node->predicate_kind != PredicateKind::Unsupported &&
               (effective_predicate_state_for_ui(*selected_node, options) != PredicateState::Unsupported ||
                options.unresolved_status_conditions.count(selected_node->logical_key))) {
        // A missing branch is still a meaningful outcome: selecting that
        // outcome means the branch is skipped and replay continues with the
        // following sibling.  In particular, a predicate with only a true
        // branch must allow an explicit false state even though it has no
        // else node.
        const bool has_branch = !selected_node->true_children.empty() ||
                                !selected_node->false_children.empty();
        result.can_set_true = has_branch;
        result.can_set_false = has_branch;
        result.can_restore_predicate = true;
    } else if (selected_node->category == ExecutionCategory::Case &&
               !selected_node->logical_key.empty() &&
               selected_node->diagnostic.empty()) {
        result.can_select_case = true;
        result.can_restore_choice_auto = true;
    }
    return result;
}

bool condition_tree_predicate_branch_reachable(const ExecutionNode& node,
                                               bool parent_reachable,
                                               bool true_branch,
                                               const ReplayOptions& options) {
    if (!parent_reachable || node.category != ExecutionCategory::Predicate) {
        return false;
    }

    const PredicateState state = effective_predicate_state_for_ui(node, options);
    if (state == PredicateState::Unsupported) {
        return false;
    }
    if (state == PredicateState::ExplicitTrue) {
        return true_branch;
    }
    if (state == PredicateState::ExplicitFalse) {
        return !true_branch;
    }

    if (node.predicate_kind == PredicateKind::Integer) {
        if (node.integer_expression.decoded) {
            bool result = false;
            if (evaluate_integer_expression(
                    node.integer_expression, options.simulated_variables,
                    result, nullptr)) {
                return true_branch ? result : !result;
            }
            return false;
        }
        if (node.integer_evaluation == IntegerEvaluationState::True) {
            return true_branch;
        }
        if (node.integer_evaluation == IntegerEvaluationState::False) {
            return !true_branch;
        }
        return false;
    }

    // Structured 0x05 conditions are evaluated by the replay model from the
    // typed int_data expression and the simulated boolean environment.  The
    // legacy predicate_state is only an editor override for other predicate
    // kinds; consulting it here made the tree show the opposite branch.
    if (node.instruction_id == 0x05) {
        if (node.boolean_expression.decoded) {
            bool result = false;
            if (evaluate_boolean_variable_expression(
                    node.boolean_expression, options.simulated_variables,
                    result, nullptr)) {
                return true_branch ? result : !result;
            }
            return false;
        }
        if (node.boolean_evaluation == BooleanEvaluationState::True) {
            return true_branch;
        }
        if (node.boolean_evaluation == BooleanEvaluationState::False) {
            return !true_branch;
        }
        return false;
    }

    if (true_branch) {
        return node.predicate_state != PredicateState::ExplicitFalse &&
               node.predicate_state != PredicateState::Unsupported &&
               node.predicate_kind != PredicateKind::Unsupported;
    }
    return node.predicate_state == PredicateState::ExplicitFalse;
}

ConditionTreeVisualState condition_tree_visual_state(const ExecutionNode& node,
                                                     bool reachable,
                                                     bool active_path,
                                                     const ReplayOptions& options) {
    if (!reachable) {
        return ConditionTreeVisualState::Unreachable;
    }
    if (node.category == ExecutionCategory::Diagnostic ||
        !node.diagnostic.empty() ||
        node.predicate_kind == PredicateKind::Unsupported ||
        effective_predicate_state_for_ui(node, options) ==
            PredicateState::Unsupported) {
        return ConditionTreeVisualState::Diagnostic;
    }
    if (node.category == ExecutionCategory::Predicate) {
        bool predicate_result = false;
        if (!evaluate_predicate_for_ui(node, options, predicate_result)) {
            return ConditionTreeVisualState::Diagnostic;
        }
    }
    if ((node.category == ExecutionCategory::Choice ||
         node.category == ExecutionCategory::Case) &&
        node.choice_state == ChoiceState::Unselected) {
        return ConditionTreeVisualState::Unselected;
    }
    return active_path ? ConditionTreeVisualState::ActivePath
                       : ConditionTreeVisualState::Normal;
}

std::wstring condition_tree_node_title(const ExecutionNode& node,
                                       const ReplayOptions& options) {
    std::wstring title = L"[" + category_text(node) + L"] ";
    if (has_known_id_prefix(node.instruction_title, node.instruction_id)) {
        title += node.instruction_title;
    } else {
        title += instruction_id_text(node.instruction_id) + L":" +
                 (node.instruction_title.empty() ? L"未知指令"
                                                  : node.instruction_title);
    }
    const std::wstring parameters = parameters_without_repeated_header(node);
    if (!parameters.empty()) {
        title += L" " + parameters;
    }
    const std::wstring status = state_text(node, options);
    if (!status.empty()) {
        title += L" [" + status + L"]";
    }
    if (!node.diagnostic.empty()) {
        title += L" [诊断: " + node.diagnostic + L"]";
    }
    return title;
}

bool condition_tree_should_expand(const ConditionTreeExpansionMap& remembered,
                                  const LogicalNodeKey& node_key,
                                  bool is_section_root, bool is_entry_root,
                                  bool is_active_path) {
    const auto found = remembered.find(node_key);
    if (found != remembered.end()) {
        return found->second;
    }
    return is_section_root || is_entry_root || is_active_path;
}

namespace {

using ExecutionNodeList = std::vector<ExecutionNode>;

const ExecutionNode* find_execution_node(const ExecutionNodeList& nodes,
                                         const LogicalNodeKey& node_key) {
    for (const ExecutionNode& node : nodes) {
        if (node.logical_key == node_key) {
            return &node;
        }
        if (const auto* found =
                find_execution_node(node.ordinary_children, node_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found =
                find_execution_node(node.true_children, node_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found =
                find_execution_node(node.false_children, node_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found = find_execution_node(node.cases, node_key);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const ExecutionNode* find_case_owner(const ExecutionNodeList& nodes,
                                     const LogicalNodeKey& case_key) {
    for (const ExecutionNode& node : nodes) {
        if (node.category == ExecutionCategory::Choice) {
            for (const ExecutionNode& case_node : node.cases) {
                if (case_node.logical_key == case_key) {
                    return &node;
                }
            }
        }
        if (const auto* found =
                find_case_owner(node.ordinary_children, case_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found = find_case_owner(node.true_children, case_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found = find_case_owner(node.false_children, case_key);
            found != nullptr) {
            return found;
        }
        if (const auto* found = find_case_owner(node.cases, case_key);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool contains_execution_node(const ExecutionNodeList& nodes,
                             const LogicalNodeKey& node_key) {
    return find_execution_node(nodes, node_key) != nullptr;
}

bool current_predicate_result(const ExecutionNode& node,
                              const ReplayOptions& options, bool& result) {
    const PredicateState state =
        effective_predicate_state_for_ui(node, options);
    if (state == PredicateState::Unsupported ||
        node.predicate_kind == PredicateKind::Unsupported) {
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
    if (node.predicate_kind == PredicateKind::Integer &&
        node.integer_expression.decoded) {
        return evaluate_integer_expression(node.integer_expression,
                                           options.simulated_variables, result,
                                           nullptr);
    }
    if (node.instruction_id == 0x05 && node.boolean_expression.decoded) {
        return evaluate_boolean_variable_expression(
            node.boolean_expression, options.simulated_variables, result,
            nullptr);
    }
    if (node.integer_evaluation == IntegerEvaluationState::True ||
        node.boolean_evaluation == BooleanEvaluationState::True) {
        result = true;
        return true;
    }
    if (node.integer_evaluation == IntegerEvaluationState::False ||
        node.boolean_evaluation == BooleanEvaluationState::False) {
        result = false;
        return true;
    }
    result = true;
    return true;
}

bool selected_case_is_valid(const ExecutionNode& choice) {
    const ExecutionNode* selected = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.choice_state != ChoiceState::SelectedCase) {
            continue;
        }
        if (selected != nullptr) {
            return false;
        }
        selected = &case_node;
    }
    return selected != nullptr;
}

bool target_is_in_diagnostic_subtree(const ExecutionNodeList& nodes,
                                     const LogicalNodeKey& target_key,
                                     bool diagnostic_ancestor);

bool target_is_in_diagnostic_subtree(const ExecutionNode& node,
                                     const LogicalNodeKey& target_key,
                                     bool diagnostic_ancestor) {
    const bool diagnostic_here =
        diagnostic_ancestor || node.category == ExecutionCategory::Diagnostic ||
        !node.diagnostic.empty();
    if (diagnostic_here && node.logical_key == target_key) {
        return true;
    }
    return target_is_in_diagnostic_subtree(node.ordinary_children, target_key,
                                           diagnostic_here) ||
           target_is_in_diagnostic_subtree(node.true_children, target_key,
                                           diagnostic_here) ||
           target_is_in_diagnostic_subtree(node.false_children, target_key,
                                           diagnostic_here) ||
           target_is_in_diagnostic_subtree(node.cases, target_key,
                                           diagnostic_here);
}

bool target_is_in_diagnostic_subtree(const ExecutionNodeList& nodes,
                                     const LogicalNodeKey& target_key,
                                     bool diagnostic_ancestor = false) {
    for (const ExecutionNode& node : nodes) {
        if (target_is_in_diagnostic_subtree(node, target_key,
                                            diagnostic_ancestor)) {
            return true;
        }
    }
    return false;
}

bool sequence_blocks_continuation(const ExecutionNodeList& nodes,
                                  const ReplayOptions& options) {
    for (const ExecutionNode& node : nodes) {
        if (node.category == ExecutionCategory::Diagnostic ||
            !node.diagnostic.empty()) {
            continue;
        }
        if (node.category == ExecutionCategory::Predicate) {
            bool result = false;
            if (!current_predicate_result(node, options, result)) {
                return true;
            }
            if (sequence_blocks_continuation(
                    result ? node.true_children : node.false_children,
                    options)) {
                return true;
            }
            continue;
        }
        if (node.category == ExecutionCategory::Choice) {
            if (sequence_blocks_continuation(node.ordinary_children,
                                              options)) {
                return true;
            }
            if (!selected_case_is_valid(node)) {
                return true;
            }
            for (const ExecutionNode& case_node : node.cases) {
                if (case_node.choice_state == ChoiceState::SelectedCase &&
                    sequence_blocks_continuation(case_node.ordinary_children,
                                                 options)) {
                    return true;
                }
            }
            continue;
        }
        if (node.category == ExecutionCategory::IntegerDispatchGroup) {
            if (sequence_blocks_continuation(node.cases, options)) {
                return true;
            }
            continue;
        }
        if (sequence_blocks_continuation(node.ordinary_children, options)) {
            return true;
        }
    }
    return false;
}

struct TargetActivationStep {
    LogicalNodeKey node_key;
    bool select_case{};
    bool predicate_result{};
};

struct TargetActivationPlan {
    std::vector<TargetActivationStep> steps;
};

std::optional<TargetActivationPlan> find_activation_in_nodes(
    const ExecutionNodeList& nodes, const LogicalNodeKey& target_key,
    const ReplayOptions& options);

std::optional<TargetActivationPlan> find_activation_in_node(
    const ExecutionNode& node, const LogicalNodeKey& target_key,
    const ReplayOptions& options) {
    if (node.logical_key == target_key) {
        return TargetActivationPlan{};
    }
    if (node.category == ExecutionCategory::Diagnostic ||
        !node.diagnostic.empty()) {
        return std::nullopt;
    }

    if (node.category == ExecutionCategory::Predicate) {
        if (contains_execution_node(node.true_children, target_key)) {
            bool current = false;
            if (!current_predicate_result(node, options, current)) {
                return std::nullopt;
            }
            TargetActivationPlan plan;
            if (!current) {
                plan.steps.push_back({node.logical_key, false, true});
            }
            if (auto nested = find_activation_in_nodes(
                    node.true_children, target_key, options);
                nested.has_value()) {
                plan.steps.insert(plan.steps.end(), nested->steps.begin(),
                                  nested->steps.end());
                return plan;
            }
            return std::nullopt;
        }
        if (contains_execution_node(node.false_children, target_key)) {
            bool current = false;
            if (!current_predicate_result(node, options, current)) {
                return std::nullopt;
            }
            TargetActivationPlan plan;
            if (current) {
                plan.steps.push_back({node.logical_key, false, false});
            }
            if (auto nested = find_activation_in_nodes(
                    node.false_children, target_key, options);
                nested.has_value()) {
                plan.steps.insert(plan.steps.end(), nested->steps.begin(),
                                  nested->steps.end());
                return plan;
            }
            return std::nullopt;
        }
        return find_activation_in_nodes(node.ordinary_children, target_key,
                                        options);
    }

    if (node.category == ExecutionCategory::Choice) {
        if (contains_execution_node(node.ordinary_children, target_key)) {
            return find_activation_in_nodes(node.ordinary_children, target_key,
                                            options);
        }
        for (const ExecutionNode& case_node : node.cases) {
            if (!contains_execution_node(ExecutionNodeList{case_node},
                                         target_key)) {
                continue;
            }
            if (case_node.choice_state == ChoiceState::Unselected) {
                TargetActivationPlan plan;
                plan.steps.push_back({case_node.logical_key, true, false});
                if (auto nested = find_activation_in_nodes(
                        case_node.ordinary_children, target_key, options);
                    nested.has_value()) {
                    plan.steps.insert(plan.steps.end(), nested->steps.begin(),
                                      nested->steps.end());
                    return plan;
                }
                if (case_node.logical_key == target_key) {
                    return plan;
                }
                return std::nullopt;
            }
            if (case_node.logical_key == target_key) {
                return std::nullopt;
            }
            return find_activation_in_nodes(case_node.ordinary_children,
                                            target_key, options);
        }
        return std::nullopt;
    }

    if (node.category == ExecutionCategory::Case) {
        return find_activation_in_nodes(node.ordinary_children, target_key,
                                        options);
    }
    if (node.category == ExecutionCategory::IntegerDispatchGroup) {
        return find_activation_in_nodes(node.cases, target_key, options);
    }

    if (auto found = find_activation_in_nodes(node.ordinary_children,
                                              target_key, options);
        found.has_value()) {
        return found;
    }
    if (auto found = find_activation_in_nodes(node.true_children, target_key,
                                              options);
        found.has_value()) {
        return found;
    }
    if (auto found = find_activation_in_nodes(node.false_children, target_key,
                                              options);
        found.has_value()) {
        return found;
    }
    return find_activation_in_nodes(node.cases, target_key, options);
}

std::optional<TargetActivationPlan> find_activation_in_nodes(
    const ExecutionNodeList& nodes, const LogicalNodeKey& target_key,
    const ReplayOptions& options) {
    if (target_is_in_diagnostic_subtree(nodes, target_key)) {
        return std::nullopt;
    }
    for (const ExecutionNode& node : nodes) {
        if (node.logical_key == target_key ||
            contains_execution_node(node.ordinary_children, target_key) ||
            contains_execution_node(node.true_children, target_key) ||
            contains_execution_node(node.false_children, target_key) ||
            contains_execution_node(node.cases, target_key)) {
            return find_activation_in_node(node, target_key, options);
        }
        if (sequence_blocks_continuation({node}, options)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool model_nodes_have_controls(const ExecutionNodeList& nodes) {
    for (const ExecutionNode& node : nodes) {
        const ConditionTreeActionState state =
            condition_tree_action_state(&node, {});
        // Reachability is deliberately represented by the separate
        // "激活子事件" entry, matching the old DLL's three-button contract.
        if (state.can_set_true || state.can_set_false ||
            state.can_restore_predicate || state.can_set_integer ||
            state.can_restore_integer || state.can_select_case ||
            state.can_restore_choice_auto) {
            return true;
        }
        if (model_nodes_have_controls(node.ordinary_children) ||
            model_nodes_have_controls(node.true_children) ||
            model_nodes_have_controls(node.false_children) ||
            model_nodes_have_controls(node.cases)) {
            return true;
        }
    }
    return false;
}

bool set_activation_step(ReplayOptions& options,
                         const SectionExecutionModel& model,
                         const TargetActivationStep& step,
                         std::wstring* diagnostic) {
    if (step.select_case) {
        const ExecutionNode* owner = find_case_owner(model.entry_gates,
                                                     step.node_key);
        if (owner == nullptr) {
            owner = find_case_owner(model.body, step.node_key);
        }
        if (owner == nullptr) {
            if (diagnostic != nullptr) {
                *diagnostic = L"激活目标的 Choice 所有者不存在";
            }
            return false;
        }
        const auto found = options.selected_case_by_choice.find(
            owner->logical_key);
        if (found != options.selected_case_by_choice.end() &&
            found->second == step.node_key) {
            return false;
        }
        options.selected_case_by_choice[owner->logical_key] = step.node_key;
        return true;
    }

    const ExecutionNode* predicate = find_execution_node(model.entry_gates,
                                                         step.node_key);
    if (predicate == nullptr) {
        predicate = find_execution_node(model.body, step.node_key);
    }
    if (predicate == nullptr ||
        predicate->category != ExecutionCategory::Predicate) {
        if (diagnostic != nullptr) {
            *diagnostic = L"激活目标的条件节点不存在";
        }
        return false;
    }
    if (predicate->predicate_kind == PredicateKind::Integer) {
        return apply_integer_variable_assignment(
            options.simulated_variables, predicate->integer_expression,
            step.predicate_result, diagnostic);
    }
    if (predicate->instruction_id == 0x05 &&
        predicate->boolean_expression.decoded) {
        return apply_boolean_variable_assignment(
            options.simulated_variables, predicate->boolean_expression,
            step.predicate_result, diagnostic);
    }
    if (predicate->predicate_kind == PredicateKind::Unsupported) {
        if (diagnostic != nullptr) {
            *diagnostic = L"激活目标包含不可模拟条件";
        }
        return false;
    }
    const PredicateState desired =
        step.predicate_result ? PredicateState::ExplicitTrue
                              : PredicateState::ExplicitFalse;
    const auto found = options.predicate_states.find(step.node_key);
    if (found != options.predicate_states.end() && found->second == desired) {
        return false;
    }
    options.predicate_states[step.node_key] = desired;
    return true;
}

}  // namespace

bool condition_tree_model_has_controls(const SectionExecutionModel& model) {
    return model_nodes_have_controls(model.entry_gates) ||
           model_nodes_have_controls(model.body);
}

bool condition_tree_model_can_make_node_reachable(
    const SectionExecutionModel& model, const LogicalNodeKey& node_key) {
    if (node_key.empty()) {
        return false;
    }
    const ExecutionNode* selected = find_execution_node(model.entry_gates,
                                                        node_key);
    if (selected == nullptr) {
        selected = find_execution_node(model.body, node_key);
    }
    return selected != nullptr &&
           condition_tree_action_state(selected, {}).can_make_reachable;
}

bool condition_tree_model_can_activate_target(
    const SectionExecutionModel& model, const LogicalNodeKey& target_key,
    const ReplayOptions& options) {
    if (target_key.empty()) {
        return false;
    }
    const auto in = [&](const ExecutionNodeList& nodes) {
        const auto plan = find_activation_in_nodes(nodes, target_key, options);
        return plan.has_value() && !plan->steps.empty();
    };
    return in(model.entry_gates) || in(model.body);
}

bool activate_condition_tree_target(
    ReplayOptions& options, const SectionExecutionModel& model,
    const LogicalNodeKey& target_key, std::wstring* diagnostic) {
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    if (target_key.empty()) {
        return false;
    }
    std::optional<TargetActivationPlan> plan =
        find_activation_in_nodes(model.entry_gates, target_key, options);
    if (!plan.has_value()) {
        plan = find_activation_in_nodes(model.body, target_key, options);
    }
    if (!plan.has_value() || plan->steps.empty()) {
        return false;
    }
    bool changed = false;
    for (const TargetActivationStep& step : plan->steps) {
        const bool step_changed =
            set_activation_step(options, model, step, diagnostic);
        if (!step_changed && diagnostic != nullptr && !diagnostic->empty()) {
            return false;
        }
        changed = step_changed || changed;
    }
    return changed;
}

}  // namespace ccz::preview
