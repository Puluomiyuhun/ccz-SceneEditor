#include <ccz/preview/execution_model.h>

#include <ccz/preview/dialogue_replay.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ccz::preview {
namespace {

using ExecutionNodeList = std::vector<ExecutionNode>;
const std::size_t kNoIndex = (std::numeric_limits<std::size_t>::max)();

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (!destination.empty()) {
        destination += L"；";
    }
    destination += message;
}

std::wstring join_path(const std::vector<std::uint32_t>& path) {
    if (path.empty()) {
        return L"root";
    }

    std::wstring result;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index != 0) {
            result += L'/';
        }
        result += std::to_wstring(path[index]);
    }
    return result;
}

bool is_predicate_id(std::uint16_t instruction_id) {
    return instruction_id == 0x05 || instruction_id == 0x36 ||
           instruction_id == 0x37 || instruction_id == 0x79;
}

ExecutionCategory category_for(std::uint16_t instruction_id,
                                bool catalog_contains_id) {
    switch (instruction_id) {
        case 0x00:
            return ExecutionCategory::SectionGate;
        case 0x01:
            return ExecutionCategory::Marker;
        case 0x02:
            return ExecutionCategory::UnconditionalContainer;
        case 0x03:
            return ExecutionCategory::ElseBranch;
        case 0x05:
        case 0x36:
        case 0x37:
        case 0x79:
            return ExecutionCategory::Predicate;
        case 0x07:
        case 0x2d:
            return ExecutionCategory::Trigger;
        case 0x12:
            return ExecutionCategory::Choice;
        case 0x13:
            return ExecutionCategory::Case;
        default:
            return catalog_contains_id ? ExecutionCategory::Ordinary
                                        : ExecutionCategory::Diagnostic;
    }
}

PredicateKind predicate_kind_for(std::uint16_t instruction_id) {
    if (instruction_id == 0x79) {
        return PredicateKind::Integer;
    }
    if (instruction_id == 0x05 || instruction_id == 0x36 ||
        instruction_id == 0x37) {
        return PredicateKind::Boolean;
    }
    return PredicateKind::None;
}

bool is_marker_or_structure(const ExecutionNode& node) {
    return node.category == ExecutionCategory::Marker ||
           node.category == ExecutionCategory::SectionGate;
}

ExecutionNode make_diagnostic_node(const LogicalNodeKey& source_key,
                                   std::uint16_t instruction_id,
                                   const std::wstring& message) {
    ExecutionNode result{};
    result.logical_key = source_key + L"|diagnostic";
    result.instruction_id = instruction_id;
    result.instruction_title = L"诊断";
    result.decoded_parameters = message;
    result.command.instruction_id = instruction_id;
    result.command.text = message;
    result.category = ExecutionCategory::Diagnostic;
    result.synthetic = true;
    result.diagnostic = message;
    return result;
}

bool same_integer_operand(const IntegerExpressionOperand& left,
                          const IntegerExpressionOperand& right) {
    return left.kind == right.kind && left.raw_value == right.raw_value;
}

bool is_integer_dispatch_member(const ExecutionNode& node) {
    return node.category == ExecutionCategory::Predicate &&
           node.instruction_id == 0x79 && node.integer_expression.decoded &&
           node.integer_expression.comparator ==
               IntegerComparisonOperator::Equal &&
           node.integer_expression.right.kind == IntegerOperandKind::Constant;
}

ExecutionNodeList classify_sequence(
    const std::vector<RawReplayNode>& raw_nodes,
    const InstructionCatalog& catalog,
    const ReplayOptions& options);

ExecutionNode make_execution_node(const RawReplayNode& raw,
                                  const InstructionCatalog& catalog,
                                  const ReplayOptions& options,
                                  bool classify_children = true) {
    ExecutionNode result{};
    result.logical_key = raw.logical_key;
    result.runtime_handle = raw.runtime_handle;
    result.instruction_id = raw.instruction_id;
    result.instruction_title = instruction_name(catalog, raw.instruction_id);
    result.decoded_parameters = raw.source_text;
    result.command = raw.command;
    if (result.command.instruction_id == 0 && raw.instruction_id != 0) {
        result.command.instruction_id = raw.instruction_id;
    }

    const bool catalog_contains_id =
        catalog.names.find(raw.instruction_id) != catalog.names.end();
    result.category = category_for(raw.instruction_id, catalog_contains_id);
    result.predicate_kind = predicate_kind_for(raw.instruction_id);

    // A test opcode is a conditional sub-event gate only when the editor
    // actually gives it a child block.  The same opcodes can also appear as
    // ordinary leaf commands (R_03/R_13 contain such 0x79 examples); treating
    // those leaves as predicates invents a branch that does not exist.
    if (result.category == ExecutionCategory::Predicate &&
        raw.children.empty()) {
        const auto state = options.predicate_states.find(result.logical_key);
        const bool explicitly_unsupported =
            state != options.predicate_states.end() &&
            state->second == PredicateState::Unsupported;
        if (!explicitly_unsupported) {
            result.category = ExecutionCategory::Ordinary;
            result.predicate_kind = PredicateKind::None;
        }
    }

    if (result.category == ExecutionCategory::Predicate &&
        result.instruction_id == 0x79) {
        result.integer_expression =
            decode_integer_expression(result.command);
        if (!result.integer_expression.decoded) {
            append_diagnostic(result.diagnostic,
                              result.integer_expression.diagnostic.empty()
                                  ? L"0x79 缺少结构化 int_data"
                                  : result.integer_expression.diagnostic);
            result.integer_evaluation = IntegerEvaluationState::Unsupported;
        } else {
            result.integer_value_available = read_simulated_operand(options.simulated_variables,
                static_cast<int>(result.integer_expression.left.kind), result.integer_expression.left.raw_value,
                result.simulated_integer_value);
            bool evaluated = false;
            std::wstring diagnostic;
            if (evaluate_integer_expression(result.integer_expression, options.simulated_variables,
                                            evaluated, &diagnostic)) {
                result.integer_evaluation =
                    evaluated ? IntegerEvaluationState::True
                              : IntegerEvaluationState::False;
            } else {
                result.integer_evaluation =
                    IntegerEvaluationState::Unsupported;
                append_diagnostic(result.diagnostic,
                                  diagnostic.empty()
                                      ? L"0x79 表达式暂不支持模拟"
                                      : diagnostic);
            }
        }
    }
    if (result.category == ExecutionCategory::Predicate &&
        result.instruction_id == 0x05) {
        result.boolean_expression =
            decode_boolean_variable_expression(result.command);
        if (!result.boolean_expression.decoded) {
            append_diagnostic(
                result.diagnostic,
                result.boolean_expression.diagnostic.empty()
                    ? L"0x05 缺少有效结构化 int_data"
                    : result.boolean_expression.diagnostic);
            result.boolean_evaluation = BooleanEvaluationState::Unsupported;
        } else {
            bool evaluated = false;
            std::wstring diagnostic;
            if (evaluate_boolean_variable_expression(
                    result.boolean_expression,
                    options.simulated_variables, evaluated, &diagnostic)) {
                result.boolean_evaluation =
                    evaluated ? BooleanEvaluationState::True
                              : BooleanEvaluationState::False;
            } else {
                result.boolean_evaluation =
                    BooleanEvaluationState::Unsupported;
                append_diagnostic(
                    result.diagnostic,
                    diagnostic.empty() ? L"0x05 表达式暂不支持模拟"
                                       : diagnostic);
            }
        }
    }
    if (!catalog_contains_id) {
        append_diagnostic(result.diagnostic,
                          L"指令目录缺少 " + result.instruction_title);
    }
    if (result.category == ExecutionCategory::Diagnostic) {
        append_diagnostic(result.diagnostic,
                          L"未注册的指令，按诊断节点保留");
    }

    if (!classify_children) {
        return result;
    }

    if (result.category == ExecutionCategory::Predicate) {
        const auto state = options.predicate_states.find(result.logical_key);
        result.predicate_state = state == options.predicate_states.end()
                                     ? PredicateState::DefaultTrue
                                     : state->second;
        ExecutionNodeList child_nodes =
            classify_sequence(raw.children, catalog, options);
        result.true_children = std::move(child_nodes);
    } else if (result.category != ExecutionCategory::Choice) {
        ExecutionNodeList child_nodes =
            classify_sequence(raw.children, catalog, options);
        result.ordinary_children.swap(child_nodes);
    }
    return result;
}

ExecutionNode make_case_node(const RawReplayNode& raw,
                             const InstructionCatalog& catalog,
                             const ReplayOptions& options) {
    ExecutionNode case_node = make_execution_node(raw, catalog, options);
    case_node.category = ExecutionCategory::Case;
    case_node.predicate_kind = PredicateKind::None;
    case_node.predicate_state = PredicateState::DefaultTrue;
    return case_node;
}

ExecutionNodeList group_integer_dispatch_runs(ExecutionNodeList nodes) {
    ExecutionNodeList result;
    result.reserve(nodes.size());

    std::size_t index = 0;
    while (index < nodes.size()) {
        ExecutionNode& first_node = nodes[index];
        if (first_node.category != ExecutionCategory::Predicate ||
            first_node.instruction_id != 0x79) {
            result.push_back(std::move(first_node));
            ++index;
            continue;
        }

        if (!is_integer_dispatch_member(first_node) ||
            !first_node.false_children.empty()) {
            result.push_back(std::move(first_node));
            ++index;
            continue;
        }

        const IntegerComparisonExpression first_expression =
            first_node.integer_expression;
        std::vector<ExecutionNode> members;
        members.push_back(std::move(first_node));
        std::set<std::int64_t> constants;
        constants.insert(first_expression.right.raw_value);
        std::size_t next = index + 1;
        while (next < nodes.size() &&
               is_integer_dispatch_member(nodes[next])) {
            const IntegerComparisonExpression& expression =
                nodes[next].integer_expression;
            if (!same_integer_operand(expression.left,
                                      first_expression.left) ||
                !nodes[next].false_children.empty() ||
                !constants.insert(expression.right.raw_value).second) {
                break;
            }
            members.push_back(std::move(nodes[next]));
            ++next;
        }

        if (members.size() < 2) {
            result.push_back(std::move(members.front()));
            ++index;
            continue;
        }

        ExecutionNode group;
        group.logical_key = members.front().logical_key + L"|integer-dispatch";
        group.instruction_id = 0x79;
        group.instruction_title = L"整型值分派";
        group.decoded_parameters = members.front().decoded_parameters;
        group.command = members.front().command;
        group.category = ExecutionCategory::IntegerDispatchGroup;
        group.synthetic = true;
        group.cases = std::move(members);
        result.push_back(std::move(group));
        index = next;
    }
    return result;
}

ExecutionNodeList classify_sequence(
    const std::vector<RawReplayNode>& raw_nodes,
    const InstructionCatalog& catalog,
    const ReplayOptions& options) {
    struct SequenceEntry {
        const RawReplayNode* node{};
        bool marker_wrapper{};
    };

    std::vector<SequenceEntry> sequence;
    const auto append_transparent_markers =
        [&](const auto& self, const RawReplayNode& raw) -> void {
            const bool marker_wrapper =
                raw.instruction_id == 0x01 && !raw.children.empty();
            sequence.push_back({&raw, marker_wrapper});
            if (marker_wrapper) {
                for (const RawReplayNode& child : raw.children) {
                    self(self, child);
                }
            }
        };
    for (const RawReplayNode& raw : raw_nodes) {
        append_transparent_markers(append_transparent_markers, raw);
    }

    ExecutionNodeList classified;
    classified.reserve(sequence.size());

    std::size_t pending_predicate = kNoIndex;
    bool can_bind_else = false;

    std::size_t index = 0;
    while (index < sequence.size()) {
        const SequenceEntry& entry = sequence[index];
        const RawReplayNode& raw = *entry.node;
        if (raw.instruction_id == 0x12) {
            ExecutionNode choice = make_execution_node(raw, catalog, options);
            choice.cases.clear();

            std::vector<RawReplayNode> ordinary_children;
            ordinary_children.reserve(raw.children.size());
            for (std::size_t child_index = 0;
                 child_index < raw.children.size(); ++child_index) {
                const RawReplayNode& child = raw.children[child_index];
                if (child.instruction_id == 0x13) {
                    choice.cases.push_back(
                        make_case_node(child, catalog, options));
                    continue;
                }
                if (child.instruction_id == 0x01 &&
                    child_index + 1 < raw.children.size() &&
                    raw.children[child_index + 1].instruction_id == 0x13) {
                    continue;
                }
                ordinary_children.push_back(child);
            }
            choice.ordinary_children =
                classify_sequence(ordinary_children, catalog, options);

            std::size_t next = index + 1;
            while (next < sequence.size()) {
                if (sequence[next].node->instruction_id == 0x01 &&
                    next + 1 < sequence.size() &&
                    sequence[next + 1].node->instruction_id == 0x13) {
                    choice.ordinary_children.push_back(make_execution_node(
                        *sequence[next].node, catalog, options, false));
                    ++next;
                }
                if (sequence[next].node->instruction_id != 0x13) break;
                choice.cases.push_back(
                    make_case_node(*sequence[next].node, catalog, options));
                ++next;
            }

            if (choice.cases.empty()) {
                const std::wstring message = L"Choice 缺少连续 Case";
                append_diagnostic(choice.diagnostic, message);
                choice.ordinary_children.push_back(make_diagnostic_node(
                    choice.logical_key, choice.instruction_id, message));
            }

            classified.push_back(std::move(choice));
            pending_predicate = kNoIndex;
            can_bind_else = false;
            index = next;
            continue;
        }

        ExecutionNode node = make_execution_node(
            raw, catalog, options, !entry.marker_wrapper);
        if (raw.instruction_id == 0x13) {
            node.category = ExecutionCategory::Case;
            node.predicate_kind = PredicateKind::None;
            node.predicate_state = PredicateState::DefaultTrue;
            append_diagnostic(node.diagnostic, L"Case 未连接到同级 Choice");
            classified.push_back(std::move(node));
            pending_predicate = kNoIndex;
            can_bind_else = false;
            ++index;
            continue;
        }

        if (raw.instruction_id == 0x03) {
            if (pending_predicate != kNoIndex && can_bind_else &&
                pending_predicate < classified.size() &&
                classified[pending_predicate].category ==
                    ExecutionCategory::Predicate &&
                classified[pending_predicate].false_children.empty()) {
                classified[pending_predicate].false_children.push_back(
                    std::move(node));
                pending_predicate = kNoIndex;
                can_bind_else = false;
                ++index;
                continue;
            }

            node.category = ExecutionCategory::Diagnostic;
            append_diagnostic(node.diagnostic,
                              L"else 未能绑定到同级前置 Predicate");
            classified.push_back(std::move(node));
            pending_predicate = kNoIndex;
            can_bind_else = false;
            ++index;
            continue;
        }

        const bool predicate = node.category == ExecutionCategory::Predicate;
        classified.push_back(std::move(node));
        if (predicate) {
            pending_predicate = classified.size() - 1;
            can_bind_else = true;
        } else if (!is_marker_or_structure(classified.back())) {
            pending_predicate = kNoIndex;
            can_bind_else = false;
        }
        ++index;
    }

    return group_integer_dispatch_runs(std::move(classified));
}

PredicateState effective_predicate_state_for(
    const ExecutionNode& predicate, const ReplayOptions& options) {
    const auto override_state =
        options.predicate_states.find(predicate.logical_key);
    return override_state == options.predicate_states.end()
               ? predicate.predicate_state
               : override_state->second;
}

bool integer_predicate_is_false_for(const ExecutionNode& predicate,
                                    const ReplayOptions& options,
                                    bool& supported) {
    supported = false;
    if (predicate.predicate_kind != PredicateKind::Integer) {
        return false;
    }
    const PredicateState state =
        effective_predicate_state_for(predicate, options);
    if (state == PredicateState::Unsupported) {
        return false;
    }
    if (state == PredicateState::ExplicitTrue) {
        supported = true;
        return false;
    }
    if (state == PredicateState::ExplicitFalse) {
        supported = true;
        return true;
    }
    bool value = false;
    supported = evaluate_integer_expression(
        predicate.integer_expression, options.simulated_variables, value,
        nullptr);
    return supported && !value;
}

bool boolean_predicate_is_false_for(const ExecutionNode& predicate,
                                    const ReplayOptions& options,
                                    bool& supported) {
    supported = false;
    if (predicate.instruction_id != 0x05) {
        return false;
    }
    const PredicateState state =
        effective_predicate_state_for(predicate, options);
    if (state == PredicateState::Unsupported) {
        return false;
    }
    if (state == PredicateState::ExplicitTrue) {
        supported = true;
        return false;
    }
    if (state == PredicateState::ExplicitFalse) {
        supported = true;
        return true;
    }
    if (!predicate.boolean_expression.decoded) {
        return false;
    }
    bool value = false;
    supported = evaluate_boolean_variable_expression(
        predicate.boolean_expression, options.simulated_variables, value,
        nullptr);
    return supported && !value;
}

bool predicate_is_false_for(const ExecutionNode& predicate,
                            const ReplayOptions& options,
                            bool& supported) {
    if (predicate.predicate_kind == PredicateKind::Integer) {
        return integer_predicate_is_false_for(predicate, options, supported);
    }
    if (predicate.instruction_id == 0x05) {
        return boolean_predicate_is_false_for(predicate, options, supported);
    }
    supported = predicate.predicate_kind != PredicateKind::Unsupported &&
                effective_predicate_state_for(predicate, options) !=
                    PredicateState::Unsupported;
    return supported &&
           effective_predicate_state_for(predicate, options) ==
               PredicateState::ExplicitFalse;
}

bool predicate_terminates_replay_path(const ExecutionNode& node,
                                      const ReplayOptions& options) {
    if (node.category != ExecutionCategory::Predicate) {
        return false;
    }
    if (node.predicate_kind == PredicateKind::Integer) {
        bool supported = false;
        (void)predicate_is_false_for(node, options, supported);
        return !supported;
    }
    if (node.instruction_id == 0x05) {
        bool supported = false;
        (void)predicate_is_false_for(node, options, supported);
        return !supported;
    }
    return node.predicate_kind == PredicateKind::Unsupported ||
           effective_predicate_state_for(node, options) ==
               PredicateState::Unsupported;
}

const ExecutionNode* find_case_by_key_for(
    const ExecutionNode& choice, const LogicalNodeKey& case_key) {
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.logical_key == case_key) {
            return &case_node;
        }
    }
    return nullptr;
}

const ExecutionNode* find_case_with_state_for(const ExecutionNode& choice,
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

enum class TargetReachability : std::uint8_t {
    NotFound,
    Found,
    Terminated,
};

TargetReachability execution_node_reaches_target_impl(
    const ExecutionNode& node, const LogicalNodeKey& target_node,
    const ReplayOptions& options);

TargetReachability execution_nodes_reach_target_impl(
    const ExecutionNodeList& nodes, const LogicalNodeKey& target_node,
    const ReplayOptions& options) {
    for (const ExecutionNode& node : nodes) {
        const TargetReachability reachability =
            execution_node_reaches_target_impl(node, target_node, options);
        if (reachability == TargetReachability::Found) {
            return TargetReachability::Found;
        }
        if (reachability == TargetReachability::Terminated ||
            predicate_terminates_replay_path(node, options)) {
            return TargetReachability::Terminated;
        }
    }
    return TargetReachability::NotFound;
}

TargetReachability execution_case_reaches_target_impl(
    const ExecutionNode& case_node, const LogicalNodeKey& target_node,
    const ReplayOptions& options) {
    if (case_node.logical_key == target_node) {
        return TargetReachability::Found;
    }
    return execution_nodes_reach_target_impl(case_node.ordinary_children,
                                              target_node, options);
}

TargetReachability execution_node_reaches_target_impl(
    const ExecutionNode& node, const LogicalNodeKey& target_node,
    const ReplayOptions& options) {
    if (node.logical_key == target_node) {
        return TargetReachability::Found;
    }

    switch (node.category) {
        case ExecutionCategory::Predicate: {
            if (predicate_terminates_replay_path(node, options)) {
                return TargetReachability::Terminated;
            }
            bool supported = false;
            const bool is_false =
                predicate_is_false_for(node, options, supported);
            if (!supported) {
                return TargetReachability::Terminated;
            }
            const ExecutionNodeList& branch =
                is_false ? node.false_children : node.true_children;
            return execution_nodes_reach_target_impl(branch, target_node,
                                                      options);
        }
        case ExecutionCategory::Choice: {
            const TargetReachability ordinary_reachability =
                execution_nodes_reach_target_impl(node.ordinary_children,
                                                  target_node, options);
            if (ordinary_reachability != TargetReachability::NotFound) {
                return ordinary_reachability;
            }

            const auto selected =
                options.selected_case_by_choice.find(node.logical_key);
            if (selected != options.selected_case_by_choice.end()) {
                const ExecutionNode* selected_case =
                    find_case_by_key_for(node, selected->second);
                return selected_case == nullptr
                           ? TargetReachability::NotFound
                           : execution_case_reaches_target_impl(
                                 *selected_case, target_node, options);
            }

            if (node.choice_state == ChoiceState::SelectedCase) {
                const ExecutionNode* selected_case =
                    find_case_with_state_for(node, ChoiceState::SelectedCase);
                return selected_case == nullptr
                           ? TargetReachability::NotFound
                           : execution_case_reaches_target_impl(
                                 *selected_case, target_node, options);
            }

            if (node.choice_state == ChoiceState::AutoTarget) {
                std::size_t target_case_count = 0;
                bool terminated = false;
                for (const ExecutionNode& case_node : node.cases) {
                    const TargetReachability case_reachability =
                        execution_case_reaches_target_impl(
                            case_node, target_node, options);
                    if (case_reachability == TargetReachability::Found) {
                        ++target_case_count;
                    } else if (case_reachability ==
                               TargetReachability::Terminated) {
                        terminated = true;
                    }
                }
                if (target_case_count == 1) {
                    return TargetReachability::Found;
                }
                return terminated ? TargetReachability::Terminated
                                  : TargetReachability::NotFound;
            }

            bool terminated = false;
            for (const ExecutionNode& case_node : node.cases) {
                const TargetReachability case_reachability =
                    execution_case_reaches_target_impl(case_node, target_node,
                                                       options);
                if (case_reachability == TargetReachability::Found) {
                    return TargetReachability::Found;
                }
                if (case_reachability == TargetReachability::Terminated) {
                    terminated = true;
                }
            }
            return terminated ? TargetReachability::Terminated
                              : TargetReachability::NotFound;
        }
        case ExecutionCategory::Case:
        case ExecutionCategory::Diagnostic:
            // Cases are entered only through their parent Choice.  Diagnostic
            // nodes (and their children) are display-only as well.
            return TargetReachability::NotFound;
        case ExecutionCategory::IntegerDispatchGroup:
            {
                bool terminated = false;
                for (const ExecutionNode& case_node : node.cases) {
                    if (case_node.logical_key == target_node) {
                        return TargetReachability::Found;
                    }
                    const TargetReachability reachability =
                        execution_node_reaches_target_impl(case_node,
                                                          target_node, options);
                    if (reachability == TargetReachability::Found) {
                        return TargetReachability::Found;
                    }
                    if (reachability == TargetReachability::Terminated) {
                        terminated = true;
                    }
                }
                return terminated ? TargetReachability::Terminated
                                  : TargetReachability::NotFound;
            }
        default:
            return execution_nodes_reach_target_impl(node.ordinary_children,
                                                     target_node, options);
    }
}

void reset_choice_states(ExecutionNodeList& nodes) {
    for (ExecutionNode& node : nodes) {
        node.choice_state = ChoiceState::Unselected;
        reset_choice_states(node.ordinary_children);
        reset_choice_states(node.true_children);
        reset_choice_states(node.false_children);
        reset_choice_states(node.cases);
    }
}

void apply_choice_states(ExecutionNodeList& nodes,
                         const ReplayOptions& options,
                         const LogicalNodeKey& selected_target,
                         bool path_reachable,
                         bool case_path = false) {
    bool sequence_reachable = path_reachable;
    for (ExecutionNode& node : nodes) {
        const bool node_reachable = sequence_reachable;

        if (node.category == ExecutionCategory::Predicate) {
            bool supported = false;
            const bool is_false =
                predicate_is_false_for(node, options, supported);
            const bool true_reachable = node_reachable && supported && !is_false;
            const bool false_reachable = node_reachable && supported && is_false;

            apply_choice_states(node.ordinary_children, options,
                                selected_target, false);
            apply_choice_states(node.true_children, options, selected_target,
                                true_reachable);
            apply_choice_states(node.false_children, options, selected_target,
                                false_reachable);
            apply_choice_states(node.cases, options, selected_target, false);
            node.choice_state = ChoiceState::Unselected;
            if (predicate_terminates_replay_path(node, options)) {
                sequence_reachable = false;
            }
            continue;
        }

        if (node.category == ExecutionCategory::Diagnostic ||
            node.category == ExecutionCategory::Case) {
            const bool case_children_reachable =
                node_reachable && case_path &&
                node.category == ExecutionCategory::Case;
            node.choice_state = ChoiceState::Unselected;
            apply_choice_states(node.ordinary_children, options,
                                selected_target, case_children_reachable);
            apply_choice_states(node.true_children, options, selected_target,
                                false);
            apply_choice_states(node.false_children, options, selected_target,
                                false);
            apply_choice_states(node.cases, options, selected_target, false);
            continue;
        }

        if (node.category == ExecutionCategory::Choice) {
            // Recurse first so nested choices are available when this Choice
            // is evaluated.  A potential Case is still a valid analysis path
            // until this Choice has an explicit/automatic selection.
            apply_choice_states(node.ordinary_children, options,
                                selected_target, node_reachable);
            apply_choice_states(node.true_children, options, selected_target,
                                node_reachable);
            apply_choice_states(node.false_children, options, selected_target,
                                node_reachable);
            apply_choice_states(node.cases, options, selected_target,
                                node_reachable, true);

            node.choice_state = ChoiceState::Unselected;
            if (!node_reachable || node.cases.empty()) {
                reset_choice_states(node.cases);
                continue;
            }

            const auto apply_case_children =
                [&](ExecutionNode& case_node, bool case_reachable) {
                    apply_choice_states(case_node.ordinary_children, options,
                                        selected_target, case_reachable);
                    apply_choice_states(case_node.true_children, options,
                                        selected_target, false);
                    apply_choice_states(case_node.false_children, options,
                                        selected_target, false);
                    apply_choice_states(case_node.cases, options,
                                        selected_target, false);
                };

            const auto selected =
                options.selected_case_by_choice.find(node.logical_key);
            if (selected != options.selected_case_by_choice.end()) {
                ExecutionNode* selected_case = nullptr;
                for (ExecutionNode& case_node : node.cases) {
                    if (case_node.logical_key == selected->second) {
                        selected_case = &case_node;
                        break;
                    }
                }
                if (selected_case == nullptr) {
                    const std::wstring message =
                        L"Choice 的 selected Case 不存在";
                    append_diagnostic(node.diagnostic, message);
                    reset_choice_states(node.cases);
                } else {
                    node.choice_state = ChoiceState::SelectedCase;
                    for (ExecutionNode& case_node : node.cases) {
                        case_node.choice_state =
                            &case_node == selected_case
                                ? ChoiceState::SelectedCase
                                : ChoiceState::Unselected;
                        apply_case_children(case_node,
                                            &case_node == selected_case);
                    }
                }
                continue;
            }

            std::size_t target_case_count = 0;
            ExecutionNode* target_case = nullptr;
            bool target_path_terminated = false;
            if (!selected_target.empty()) {
                for (ExecutionNode& case_node : node.cases) {
                    const TargetReachability case_reachability =
                        execution_case_reaches_target_impl(
                            case_node, selected_target, options);
                    if (case_reachability == TargetReachability::Found) {
                        ++target_case_count;
                        target_case = &case_node;
                    } else if (case_reachability ==
                               TargetReachability::Terminated) {
                        target_path_terminated = true;
                    }
                }
            }
            if (target_case_count == 1 && !target_path_terminated) {
                node.choice_state = ChoiceState::AutoTarget;
                for (ExecutionNode& case_node : node.cases) {
                    case_node.choice_state =
                        &case_node == target_case ? ChoiceState::AutoTarget
                                                   : ChoiceState::Unselected;
                    apply_case_children(case_node,
                                        &case_node == target_case);
                }
            } else {
                reset_choice_states(node.cases);
            }
            continue;
        }

        if (node.category == ExecutionCategory::IntegerDispatchGroup) {
            node.choice_state = ChoiceState::Unselected;
            apply_choice_states(node.ordinary_children, options,
                                selected_target, node_reachable);
            apply_choice_states(node.true_children, options, selected_target,
                                node_reachable);
            apply_choice_states(node.false_children, options, selected_target,
                                node_reachable);
            apply_choice_states(node.cases, options, selected_target,
                                node_reachable);
            continue;
        }

        node.choice_state = ChoiceState::Unselected;
        apply_choice_states(node.ordinary_children, options, selected_target,
                            node_reachable);
        apply_choice_states(node.true_children, options, selected_target,
                            node_reachable);
        apply_choice_states(node.false_children, options, selected_target,
                            node_reachable);
        apply_choice_states(node.cases, options, selected_target, false);
    }
}

void collect_node_diagnostics(const ExecutionNodeList& nodes,
                             std::vector<std::wstring>& diagnostics) {
    for (const ExecutionNode& node : nodes) {
        if (!node.diagnostic.empty()) {
            diagnostics.push_back(node.logical_key + L"：" + node.diagnostic);
        }
        collect_node_diagnostics(node.ordinary_children, diagnostics);
        collect_node_diagnostics(node.true_children, diagnostics);
        collect_node_diagnostics(node.false_children, diagnostics);
        collect_node_diagnostics(node.cases, diagnostics);
    }
}

class ModelHasher {
public:
    void add_byte(std::uint8_t value) {
        hash_ ^= static_cast<std::uint64_t>(value);
        hash_ *= 1099511628211ull;
    }

    void add_u64(std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            add_byte(static_cast<std::uint8_t>(value & 0xffu));
            value >>= 8;
        }
    }

    void add_string(const std::wstring& value) {
        add_u64(static_cast<std::uint64_t>(value.size()));
        for (const wchar_t character : value) {
            add_u64(static_cast<std::uint64_t>(character));
        }
    }

    std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_{1469598103934665603ull};
};

void hash_nodes(ModelHasher& hasher, const ExecutionNodeList& nodes);

void hash_node(ModelHasher& hasher, const ExecutionNode& node) {
    hasher.add_string(node.logical_key);
    hasher.add_u64(node.instruction_id);
    hasher.add_string(node.instruction_title);
    hasher.add_string(node.decoded_parameters);
    hasher.add_u64(node.command.instruction_id);
    hasher.add_u64(static_cast<std::uint64_t>(node.command.int_data.size()));
    for (const std::int32_t value : node.command.int_data) {
        hasher.add_u64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
    }
    hasher.add_string(node.command.text);
    hasher.add_byte(static_cast<std::uint8_t>(node.category));
    hasher.add_byte(static_cast<std::uint8_t>(node.predicate_kind));
    hasher.add_byte(static_cast<std::uint8_t>(node.predicate_state));
    hasher.add_byte(node.boolean_expression.decoded ? 1 : 0);
    hasher.add_u64(static_cast<std::uint64_t>(
        node.boolean_expression.true_variables.size()));
    for (const std::int32_t value : node.boolean_expression.true_variables) {
        hasher.add_u64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
    }
    hasher.add_u64(static_cast<std::uint64_t>(
        node.boolean_expression.false_variables.size()));
    for (const std::int32_t value : node.boolean_expression.false_variables) {
        hasher.add_u64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
    }
    hasher.add_string(node.boolean_expression.diagnostic);
    hasher.add_byte(static_cast<std::uint8_t>(node.boolean_evaluation));
    hasher.add_byte(node.integer_expression.decoded ? 1 : 0);
    hasher.add_byte(static_cast<std::uint8_t>(
        node.integer_expression.left.kind));
    hasher.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(node.integer_expression.left.raw_value)));
    hasher.add_byte(static_cast<std::uint8_t>(
        node.integer_expression.comparator));
    hasher.add_byte(static_cast<std::uint8_t>(
        node.integer_expression.right.kind));
    hasher.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(node.integer_expression.right.raw_value)));
    hasher.add_string(node.integer_expression.diagnostic);
    hasher.add_byte(static_cast<std::uint8_t>(node.integer_evaluation));
    hasher.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(node.simulated_integer_value)));
    hasher.add_byte(node.integer_value_available ? 1 : 0);
    hasher.add_byte(static_cast<std::uint8_t>(node.choice_state));
    hasher.add_byte(node.synthetic ? 1 : 0);
    hasher.add_string(node.diagnostic);
    hash_nodes(hasher, node.ordinary_children);
    hash_nodes(hasher, node.true_children);
    hash_nodes(hasher, node.false_children);
    hash_nodes(hasher, node.cases);
}

void hash_nodes(ModelHasher& hasher, const ExecutionNodeList& nodes) {
    hasher.add_u64(static_cast<std::uint64_t>(nodes.size()));
    for (const ExecutionNode& node : nodes) {
        hash_node(hasher, node);
    }
}

std::uint64_t model_revision_for(const SectionExecutionModel& model) {
    ModelHasher hasher;
    hasher.add_string(model.section_key);
    hasher.add_byte(model.boundary_complete ? 1 : 0);
    hash_nodes(hasher, model.entry_gates);
    hash_nodes(hasher, model.body);
    hasher.add_string(model.selected_target_key);
    hasher.add_u64(static_cast<std::uint64_t>(model.diagnostics.size()));
    for (const std::wstring& diagnostic : model.diagnostics) {
        hasher.add_string(diagnostic);
    }
    return hasher.value();
}

bool node_equal(const ExecutionNode& left, const ExecutionNode& right);

bool node_list_equal(const ExecutionNodeList& left,
                     const ExecutionNodeList& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!node_equal(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool node_equal(const ExecutionNode& left, const ExecutionNode& right) {
    return left.logical_key == right.logical_key &&
           left.instruction_id == right.instruction_id &&
           left.instruction_title == right.instruction_title &&
           left.decoded_parameters == right.decoded_parameters &&
           left.command.instruction_id == right.command.instruction_id &&
           left.command.int_data == right.command.int_data &&
           left.command.text == right.command.text &&
           left.category == right.category &&
           left.predicate_kind == right.predicate_kind &&
           left.predicate_state == right.predicate_state &&
           left.boolean_expression.decoded ==
               right.boolean_expression.decoded &&
           left.boolean_expression.true_variables ==
               right.boolean_expression.true_variables &&
           left.boolean_expression.false_variables ==
               right.boolean_expression.false_variables &&
           left.boolean_expression.diagnostic ==
               right.boolean_expression.diagnostic &&
           left.boolean_evaluation == right.boolean_evaluation &&
           left.integer_expression.decoded ==
               right.integer_expression.decoded &&
           left.integer_expression.left.kind ==
               right.integer_expression.left.kind &&
           left.integer_expression.left.raw_value ==
               right.integer_expression.left.raw_value &&
           left.integer_expression.comparator ==
               right.integer_expression.comparator &&
           left.integer_expression.right.kind ==
               right.integer_expression.right.kind &&
           left.integer_expression.right.raw_value ==
               right.integer_expression.right.raw_value &&
           left.integer_expression.diagnostic ==
               right.integer_expression.diagnostic &&
            left.integer_evaluation == right.integer_evaluation &&
            left.simulated_integer_value == right.simulated_integer_value &&
            left.integer_value_available == right.integer_value_available &&
           left.choice_state == right.choice_state &&
           left.synthetic == right.synthetic &&
           left.diagnostic == right.diagnostic &&
           node_list_equal(left.ordinary_children, right.ordinary_children) &&
           node_list_equal(left.true_children, right.true_children) &&
           node_list_equal(left.false_children, right.false_children) &&
           node_list_equal(left.cases, right.cases);
}

std::size_t count_nodes(const ExecutionNodeList& nodes) {
    std::size_t count = 0;
    for (const ExecutionNode& node : nodes) {
        ++count;
        count += count_nodes(node.ordinary_children);
        count += count_nodes(node.true_children);
        count += count_nodes(node.false_children);
        count += count_nodes(node.cases);
    }
    return count;
}

}  // namespace

BooleanVariableExpression decode_boolean_variable_expression(
    const DialogueCommandView& command) {
    BooleanVariableExpression result;
    if (command.instruction_id != 0x05) {
        result.diagnostic = L"不是 0x05 变量测试指令";
        return result;
    }
    if (command.int_data.size() < 50) {
        result.diagnostic = L"0x05 int_data 字段不足 50 个";
        return result;
    }

    const auto decode_slot_range = [&](std::size_t begin,
                                       std::size_t end,
                                       std::vector<std::int32_t>& target) {
        for (std::size_t index = begin; index < end; ++index) {
            const std::int32_t value = command.int_data[index];
            if (value == -1) {
                break;
            }
            if (value < 0) {
                result.diagnostic = L"0x05 变量列表包含非法负数";
                return false;
            }
            target.push_back(value);
        }
        return true;
    };

    if (!decode_slot_range(0, 25, result.true_variables) ||
        !decode_slot_range(25, 50, result.false_variables)) {
        result.true_variables.clear();
        result.false_variables.clear();
        return result;
    }
    result.decoded = true;
    return result;
}

bool evaluate_boolean_variable_expression(
    const BooleanVariableExpression& expression,
    const SimulationEnvironment& environment, bool& result,
    std::wstring* diagnostic) {
    if (!expression.decoded) {
        if (diagnostic != nullptr) {
            *diagnostic = expression.diagnostic.empty()
                              ? L"布尔表达式未解码"
                              : expression.diagnostic;
        }
        return false;
    }

    if (environment.boolean_values_invalidated) {
        for (const auto* variables : {&expression.true_variables, &expression.false_variables})
            for (auto variable : *variables) if (!environment.known_boolean_assignments.count(variable)) {
                if (diagnostic) *diagnostic=L"布尔变量的模拟值未确定";
                return false;
            }
    }

    for (const std::int32_t variable : expression.true_variables) {
        if (std::find(expression.false_variables.begin(),
                      expression.false_variables.end(), variable) !=
            expression.false_variables.end()) {
            if (diagnostic != nullptr) {
                *diagnostic = L"0x05 变量同时位于真/假列表：Var" +
                              std::to_wstring(variable);
            }
            return false;
        }
    }

    for (const std::int32_t variable : expression.true_variables) {
        const auto found = environment.boolean_variables.find(variable);
        if (found == environment.boolean_variables.end() || !found->second) {
            result = false;
            return true;
        }
    }
    for (const std::int32_t variable : expression.false_variables) {
        const auto found = environment.boolean_variables.find(variable);
        if (found != environment.boolean_variables.end() && found->second) {
            result = false;
            return true;
        }
    }
    result = true;
    return true;
}

bool apply_boolean_variable_assignment(
    SimulationEnvironment& environment,
    const BooleanVariableExpression& expression, bool desired_result,
    std::wstring* diagnostic) {
    if (!expression.decoded) {
        if (diagnostic != nullptr) {
            *diagnostic = expression.diagnostic.empty()
                              ? L"布尔表达式未解码"
                              : expression.diagnostic;
        }
        return false;
    }

    SimulationEnvironment candidate = environment;
    for (const std::int32_t variable : expression.true_variables) {
        if (std::find(expression.false_variables.begin(),
                      expression.false_variables.end(), variable) !=
            expression.false_variables.end()) {
            if (diagnostic != nullptr) {
                *diagnostic = L"0x05 变量同时位于真/假列表：Var" +
                              std::to_wstring(variable);
            }
            return false;
        }
        candidate.boolean_variables[variable] = desired_result;
    }
    for (const std::int32_t variable : expression.false_variables) {
        candidate.boolean_variables[variable] = !desired_result;
    }
    if (candidate.boolean_variables == environment.boolean_variables) {
        return false;
    }
    environment.boolean_variables.swap(candidate.boolean_variables);
    return true;
}

bool apply_integer_variable_assignment(
    SimulationEnvironment& environment,
    const IntegerComparisonExpression& expression, bool desired_result,
    std::wstring* diagnostic) {
    if (!expression.decoded) {
        if (diagnostic != nullptr) {
            *diagnostic = expression.diagnostic.empty()
                              ? L"整型表达式未解码"
                              : expression.diagnostic;
        }
        return false;
    }
    bool current_result = false;
    if (evaluate_integer_expression(expression, environment, current_result,
                                    diagnostic) &&
        current_result == desired_result) {
        return false;
    }

    const auto read_operand = [&](const IntegerExpressionOperand& operand,
                                  std::int64_t& value) {
        std::int32_t resolved;
        if (!read_simulated_operand(environment, static_cast<int>(operand.kind), operand.raw_value, resolved)) {
            if (diagnostic) *diagnostic = L"0x79 比较操作数的模拟值未确定";
            return false;
        }
        value = resolved;
        return true;
    };

    const IntegerExpressionOperand* target = nullptr;
    const IntegerExpressionOperand* other = nullptr;
    bool target_is_left = false;
    std::uint32_t target_address;
    if (resolve_simulated_assignment_address(environment, expression.left, target_address)) {
        target = &expression.left;
        other = &expression.right;
        target_is_left = true;
    } else if (resolve_simulated_assignment_address(environment, expression.right, target_address)) {
        target = &expression.right;
        other = &expression.left;
    }
    if (target == nullptr || other == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = L"0x79 没有可写整型变量操作数";
        }
        return false;
    }

    std::int64_t other_value = 0;
    if (!read_operand(*other, other_value)) {
        return false;
    }

    const auto checked_increment = [](std::int32_t value,
                                      std::int32_t& result) {
        if (value == (std::numeric_limits<std::int32_t>::max)()) {
            return false;
        }
        result = value + 1;
        return true;
    };
    const auto checked_decrement = [](std::int32_t value,
                                      std::int32_t& result) {
        if (value == (std::numeric_limits<std::int32_t>::min)()) {
            return false;
        }
        result = value - 1;
        return true;
    };
    const auto checked_different = [&](std::int32_t value,
                                       std::int32_t& result) {
        return value == (std::numeric_limits<std::int32_t>::max)()
                   ? checked_decrement(value, result)
                   : checked_increment(value, result);
    };

    const auto other_as_int32 = [&]() {
        return static_cast<std::int32_t>(other_value);
    };
    std::int32_t target_value = 0;
    bool target_value_valid = true;
    if (target_is_left) {
        switch (expression.comparator) {
            case IntegerComparisonOperator::Equal:
                if (desired_result) {
                    target_value = other_as_int32();
                } else {
                    target_value_valid =
                        checked_different(other_as_int32(), target_value);
                }
                break;
            case IntegerComparisonOperator::GreaterEqual:
                target_value = other_as_int32();
                if (!desired_result) {
                    target_value_valid =
                        checked_decrement(other_as_int32(), target_value);
                }
                break;
            case IntegerComparisonOperator::LessEqual:
                target_value = other_as_int32();
                if (!desired_result) {
                    target_value_valid =
                        checked_increment(other_as_int32(), target_value);
                }
                break;
            case IntegerComparisonOperator::NotEqual:
                if (desired_result) {
                    target_value_valid =
                        checked_different(other_as_int32(), target_value);
                } else {
                    target_value = other_as_int32();
                }
                break;
            case IntegerComparisonOperator::Unsupported:
            default:
                if (diagnostic != nullptr) {
                    *diagnostic = L"0x79 比较符不支持快速调整";
                }
                return false;
        }
    } else {
        std::int64_t left_value = 0;
        if (!read_operand(expression.left, left_value)) {
            return false;
        }
        switch (expression.comparator) {
            case IntegerComparisonOperator::Equal:
                if (desired_result) {
                    target_value = static_cast<std::int32_t>(left_value);
                } else {
                    target_value_valid = checked_different(
                        static_cast<std::int32_t>(left_value), target_value);
                }
                break;
            case IntegerComparisonOperator::GreaterEqual:
                target_value = static_cast<std::int32_t>(left_value);
                if (!desired_result) {
                    target_value_valid = checked_increment(
                        static_cast<std::int32_t>(left_value), target_value);
                }
                break;
            case IntegerComparisonOperator::LessEqual:
                target_value = static_cast<std::int32_t>(left_value);
                if (!desired_result) {
                    target_value_valid = checked_decrement(
                        static_cast<std::int32_t>(left_value), target_value);
                }
                break;
            case IntegerComparisonOperator::NotEqual:
                if (desired_result) {
                    target_value_valid = checked_different(
                        static_cast<std::int32_t>(left_value), target_value);
                } else {
                    target_value = static_cast<std::int32_t>(left_value);
                }
                break;
            case IntegerComparisonOperator::Unsupported:
            default:
                if (diagnostic != nullptr) {
                    *diagnostic = L"0x79 比较符不支持快速调整";
                }
                return false;
        }
    }

    if (!target_value_valid) {
        if (diagnostic != nullptr) {
            *diagnostic = L"0x79 快速调整在整型边界上无可行结果";
        }
        return false;
    }
    auto candidate = environment;
    write_simulated_address(candidate, target_address, target_value);
    bool verified = false;
    if (!evaluate_integer_expression(expression, candidate, verified, diagnostic) || verified != desired_result) {
        if (diagnostic) *diagnostic = L"0x79 操作数关联导致该比较无法快速调整";
        return false;
    }
    if (target->kind == IntegerOperandKind::IntegerVariable && target_value == 0)
        candidate.integer_variables.erase(target->raw_value);
    environment = std::move(candidate);
    if (diagnostic) diagnostic->clear();
    return true;
}

IntegerComparisonExpression decode_integer_expression(
    const DialogueCommandView& command) {
    IntegerComparisonExpression result;
    if (command.instruction_id != 0x79) {
        result.diagnostic = L"不是 0x79 变量测试指令";
        return result;
    }
    if (command.int_data.size() < 5) {
        result.diagnostic = L"0x79 int_data 字段不足 5 个";
        return result;
    }

    const auto operand_kind = [](std::int32_t raw) {
        switch (raw) {
            case 0:
                return IntegerOperandKind::Constant;
            case 1:
                return IntegerOperandKind::PointerValue;
            case 2:
                return IntegerOperandKind::PointerVariable;
            case 3:
                return IntegerOperandKind::PointerAddress;
            case 4:
                return IntegerOperandKind::IntegerVariable;
            case 5:
                return IntegerOperandKind::IntegerVariableAddress;
            default:
                return IntegerOperandKind::Unsupported;
        }
    };
    const auto comparator_kind = [](std::int32_t raw) {
        switch (raw) {
            case 0:
                return IntegerComparisonOperator::Equal;
            case 1:
                return IntegerComparisonOperator::GreaterEqual;
            case 2:
                return IntegerComparisonOperator::LessEqual;
            case 3:
                return IntegerComparisonOperator::NotEqual;
            default:
                return IntegerComparisonOperator::Unsupported;
        }
    };

    result.decoded = true;
    result.left = {operand_kind(command.int_data[0]), command.int_data[1]};
    result.comparator = comparator_kind(command.int_data[2]);
    result.right = {operand_kind(command.int_data[3]), command.int_data[4]};
    if (result.left.kind == IntegerOperandKind::Unsupported ||
        result.right.kind == IntegerOperandKind::Unsupported) {
        result.diagnostic = L"0x79 使用了未知操作数类型";
    } else if (result.comparator == IntegerComparisonOperator::Unsupported) {
        result.diagnostic = L"0x79 使用了未知比较符";
    }
    return result;
}

std::wstring format_integer_expression(
    const IntegerComparisonExpression& expression) {
    if (!expression.decoded) {
        return std::wstring();
    }

    const auto format_operand = [](const IntegerExpressionOperand& operand) {
        switch (operand.kind) {
            case IntegerOperandKind::Constant:
                return std::to_wstring(operand.raw_value);
            case IntegerOperandKind::PointerValue:
                return L"PtrValue(" + std::to_wstring(operand.raw_value) +
                       L")";
            case IntegerOperandKind::PointerVariable:
                return L"PtrVar(" + std::to_wstring(operand.raw_value) +
                       L")";
            case IntegerOperandKind::PointerAddress:
                return L"PtrAddr(" + std::to_wstring(operand.raw_value) +
                       L")";
            case IntegerOperandKind::IntegerVariable:
                return L"Var" + std::to_wstring(operand.raw_value);
            case IntegerOperandKind::IntegerVariableAddress:
                return L"VarAddr(" + std::to_wstring(operand.raw_value) +
                       L")";
            case IntegerOperandKind::Unsupported:
            default:
                return std::wstring(L"?");
        }
    };
    const auto format_comparator = [](IntegerComparisonOperator comparator) {
        switch (comparator) {
            case IntegerComparisonOperator::Equal:
                return std::wstring(L"==");
            case IntegerComparisonOperator::GreaterEqual:
                return std::wstring(L">=");
            case IntegerComparisonOperator::LessEqual:
                return std::wstring(L"<=");
            case IntegerComparisonOperator::NotEqual:
                // The editor's compare2 table displays NotEqual as the single
                // glyph U+2260 (CczString.ini: "≠").  Keep this separate
                // from the C++ expression spelling used by the evaluator.
                return std::wstring(L"\u2260");
            case IntegerComparisonOperator::Unsupported:
            default:
                return std::wstring(L"?");
        }
    };

    return format_operand(expression.left) + L" " +
           format_comparator(expression.comparator) + L" " +
           format_operand(expression.right);
}

bool evaluate_integer_expression(const IntegerComparisonExpression& expression,
                                 const SimulationEnvironment& environment,
                                 bool& result,
                                 std::wstring* diagnostic) {
    if (!expression.decoded) {
        if (diagnostic != nullptr) {
            *diagnostic = expression.diagnostic.empty()
                              ? L"整型表达式未解码"
                              : expression.diagnostic;
        }
        return false;
    }

    const auto read_operand = [&](const IntegerExpressionOperand& operand,
                                  std::int64_t& value) {
        std::int32_t resolved;
        if (read_simulated_operand(environment, static_cast<int>(operand.kind), operand.raw_value, resolved)) {
            value=resolved;return true;
        }
        if(diagnostic) *diagnostic=L"变量或指针目标的模拟值未确定";
        return false;
    };

    std::int64_t left = 0;
    std::int64_t right = 0;
    if (!read_operand(expression.left, left) ||
        !read_operand(expression.right, right)) {
        return false;
    }

    switch (expression.comparator) {
        case IntegerComparisonOperator::Equal:
            result = left == right;
            return true;
        case IntegerComparisonOperator::GreaterEqual:
            result = left >= right;
            return true;
        case IntegerComparisonOperator::LessEqual:
            result = left <= right;
            return true;
        case IntegerComparisonOperator::NotEqual:
            result = left != right;
            return true;
        case IntegerComparisonOperator::Unsupported:
        default:
            if (diagnostic != nullptr) {
                *diagnostic = expression.diagnostic.empty()
                                  ? L"0x79 比较符不支持值模拟"
                                  : expression.diagnostic;
            }
            return false;
    }
}

LogicalNodeKey make_section_key(
    const std::wstring& document_path,
    const std::vector<std::uint32_t>& section_path) {
    return document_path + L"|section:" + join_path(section_path);
}

LogicalNodeKey make_logical_node_key(
    const LogicalNodeKey& section_key,
    const std::vector<std::uint32_t>& child_path,
    std::uint16_t instruction_id) {
    return section_key + L"|node:" + join_path(child_path) +
           L"|instruction:" + std::to_wstring(instruction_id);
}

bool execution_node_reaches_target(const ExecutionNode& node,
                                   const LogicalNodeKey& target_node,
                                   const ReplayOptions& options) {
    return execution_node_reaches_target_impl(node, target_node, options) ==
           TargetReachability::Found;
}

bool execution_case_reaches_target(const ExecutionNode& case_node,
                                   const LogicalNodeKey& target_node,
                                   const ReplayOptions& options) {
    return execution_case_reaches_target_impl(case_node, target_node, options) ==
           TargetReachability::Found;
}

bool execution_nodes_reach_target(
    const std::vector<ExecutionNode>& nodes,
    const LogicalNodeKey& target_node,
    const ReplayOptions& options) {
    return execution_nodes_reach_target_impl(nodes, target_node, options) ==
           TargetReachability::Found;
}

SectionExecutionModel build_section_execution_model(
    const SectionInput& input, const InstructionCatalog& catalog,
    const ReplayOptions& options) {
    SectionExecutionModel model;
    model.section_key = input.section_key;
    model.selected_target_key = input.selected_target_key;

    std::size_t boundary = kNoIndex;
    for (std::size_t index = 0; index < input.nodes.size(); ++index) {
        if (input.nodes[index].instruction_id == 0x00) {
            boundary = index;
            break;
        }
    }

    if (boundary == kNoIndex) {
        model.diagnostics.push_back(L"不完整 Section：缺少顶层 0x00 边界");
        model.body = classify_sequence(input.nodes, catalog, options);
    } else {
        model.boundary_complete = true;
        const std::vector<RawReplayNode> entry_nodes(
            input.nodes.begin(), input.nodes.begin() + boundary);
        const std::vector<RawReplayNode> body_nodes(
            input.nodes.begin() + boundary, input.nodes.end());
        model.entry_gates = classify_sequence(entry_nodes, catalog, options);
        model.body = classify_sequence(body_nodes, catalog, options);
    }

    if (!catalog.valid && !catalog.diagnostic.empty()) {
        model.diagnostics.push_back(L"指令目录诊断：" + catalog.diagnostic);
    }
    apply_choice_states(model.entry_gates, options, model.selected_target_key,
                        true);
    apply_choice_states(model.body, options, model.selected_target_key, true);
    collect_node_diagnostics(model.entry_gates, model.diagnostics);
    collect_node_diagnostics(model.body, model.diagnostics);
    model.model_revision = model_revision_for(model);
    return model;
}

bool initialize_default_choice_selections(
    const SectionExecutionModel& model, ReplayOptions& options) {
    bool changed = false;
    std::function<void(const std::vector<ExecutionNode>&)> visit_nodes;
    visit_nodes = [&](const std::vector<ExecutionNode>& nodes) {
        for (const ExecutionNode& node : nodes) {
            if (node.category == ExecutionCategory::Choice &&
                !node.cases.empty() &&
                options.selected_case_by_choice.find(node.logical_key) ==
                    options.selected_case_by_choice.end()) {
                options.selected_case_by_choice.emplace(
                    node.logical_key, node.cases.front().logical_key);
                changed = true;
            }
            visit_nodes(node.ordinary_children);
            visit_nodes(node.true_children);
            visit_nodes(node.false_children);
            visit_nodes(node.cases);
        }
    };

    visit_nodes(model.entry_gates);
    visit_nodes(model.body);
    return changed;
}

bool execution_model_equal(const SectionExecutionModel& left,
                           const SectionExecutionModel& right) {
    return left.section_key == right.section_key &&
           left.boundary_complete == right.boundary_complete &&
           left.selected_target_key == right.selected_target_key &&
           left.model_revision == right.model_revision &&
           left.diagnostics == right.diagnostics &&
           node_list_equal(left.entry_gates, right.entry_gates) &&
           node_list_equal(left.body, right.body);
}

std::size_t execution_node_count(const SectionExecutionModel& model) {
    return count_nodes(model.entry_gates) + count_nodes(model.body);
}

}  // namespace ccz::preview
