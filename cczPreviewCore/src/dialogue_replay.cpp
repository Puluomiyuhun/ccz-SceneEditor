#include <ccz/preview/dialogue_replay.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ccz::preview {
namespace {

using ExecutionNodeList = std::vector<ExecutionNode>;

bool is_preview_command(const ExecutionNode& node,
                        const DialogueProfile& profile) {
    return node.command.instruction_id == 0x77 || node.command.instruction_id == 0x78 || node.command.instruction_id == 0x38 || node.command.instruction_id == 0x35 || node.command.instruction_id == profile.face_state_instruction_id ||
           find_dialogue_spec(profile, node.command.instruction_id) != nullptr;
}

std::optional<std::int32_t> decode_rename_target(
    const DialogueCommandView& command) {
    if (command.instruction_id != 0x77 || command.int_data.size() < 5 ||
        command.int_data[0] != 2 || command.int_data[1] != 4054 ||
        command.int_data[2] != 2 || command.int_data[3] != 0 ||
        command.int_data[4] < 0 || command.int_data[4] > 1023) {
        return std::nullopt;
    }
    return command.int_data[4];
}

bool consume_rename_pair(PreviewState& state,
                         const DialogueCommandView& command) {
    if (!state.pending_rename_target.has_value()) {
        return false;
    }

    const bool is_matching_text =
        command.instruction_id == 0x72 && !command.int_data.empty() &&
        command.int_data.front() == 0 && !command.text.empty();
    if (is_matching_text) {
        state.data_name_overrides[*state.pending_rename_target] =
            normalize_dialogue_text(command.text);
    }
    state.pending_rename_target.reset();
    return is_matching_text;
}

void track_rename_command(PreviewState& state,
                          const DialogueCommandView& command) {
    if (consume_rename_pair(state, command)) {
        return;
    }
    if (const auto target = decode_rename_target(command); target.has_value()) {
        state.pending_rename_target = *target;
    }
}

void apply_data_name_override(DialogueState& dialogue,
                              const PreviewState& state) {
    if (dialogue.speaker_label.size() < 3 ||
        dialogue.speaker_label[0] != L'*' ||
        dialogue.speaker_label[1] != L'.') {
        return;
    }
    for (const PersonReference& reference : dialogue.person_references) {
        if (reference.kind != PersonReferenceKind::DataPerson) {
            continue;
        }
        const auto found = state.data_name_overrides.find(reference.value);
        if (found != state.data_name_overrides.end()) {
            dialogue.speaker_label = found->second;
        }
        return;
    }
}

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (!destination.empty()) {
        destination += L"; ";
    }
    destination += message;
}

void append_diagnostic(std::wstring& destination, const wchar_t* message) {
    append_diagnostic(destination, std::wstring(message));
}

void append_node_diagnostic(ReplayResult& result, const ExecutionNode& node) {
    if (!node.diagnostic.empty() &&
        result.diagnostic.find(node.diagnostic) == std::wstring::npos) {
        append_diagnostic(result.diagnostic, node.diagnostic);
    }
}

bool contains_key(const ExecutionNode& node, const LogicalNodeKey& key);

bool contains_key(const ExecutionNodeList& nodes, const LogicalNodeKey& key) {
    for (const ExecutionNode& node : nodes) {
        if (contains_key(node, key)) {
            return true;
        }
    }
    return false;
}

bool contains_key(const ExecutionNode& node, const LogicalNodeKey& key) {
    if (node.logical_key == key) {
        return true;
    }
    return contains_key(node.ordinary_children, key) ||
           contains_key(node.true_children, key) ||
           contains_key(node.false_children, key) ||
           contains_key(node.cases, key);
}

bool model_contains_key(const SectionExecutionModel& model,
                        const LogicalNodeKey& key) {
    return contains_key(model.entry_gates, key) ||
           contains_key(model.body, key);
}

PredicateState effective_predicate_state(const ExecutionNode& predicate,
                                         const ReplayOptions& options) {
    const auto override_state =
        options.predicate_states.find(predicate.logical_key);
    return override_state == options.predicate_states.end()
               ? predicate.predicate_state
               : override_state->second;
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

const ExecutionNode* find_unique_reachable_case(
    const ExecutionNode& choice, const LogicalNodeKey& target_node,
    const ReplayOptions& options) {
    const ExecutionNode* selected_case = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (!execution_case_reaches_target(case_node, target_node, options)) {
            continue;
        }
        if (selected_case != nullptr) {
            return nullptr;
        }
        selected_case = &case_node;
    }
    return selected_case;
}

const ExecutionNode* selected_case_for(const ExecutionNode& choice,
                                       const LogicalNodeKey& target_node,
                                       const ReplayOptions& options,
                                       bool& selection_was_requested) {
    const auto selected =
        options.selected_case_by_choice.find(choice.logical_key);
    if (selected != options.selected_case_by_choice.end()) {
        selection_was_requested = true;
        return find_case_by_key(choice, selected->second);
    }

    if (choice.choice_state == ChoiceState::SelectedCase) {
        selection_was_requested = true;
        return find_case_with_state(choice, ChoiceState::SelectedCase);
    }
    if (choice.choice_state == ChoiceState::AutoTarget) {
        const ExecutionNode* auto_case =
            find_unique_reachable_case(choice, target_node, options);
        selection_was_requested = auto_case != nullptr;
        return auto_case;
    }

    selection_was_requested = false;
    return nullptr;
}

bool target_reachable_in_choice_cases(const ExecutionNode& choice,
                                      const LogicalNodeKey& target_node,
                                      const ReplayOptions& options) {
    for (const ExecutionNode& case_node : choice.cases) {
        if (execution_case_reaches_target(case_node, target_node, options)) {
            return true;
        }
    }
    return false;
}

bool walk_execution_nodes(const ExecutionNodeList& nodes,
                          const LogicalNodeKey& target_node,
                          const DialogueProfile& profile,
                          const ReplayOptions& options,
                          bool continuation_may_reach_target,
                          ReplayResult& result);

void wait_for_choice(const ExecutionNode& choice, ReplayResult& result,
                     const wchar_t* message) {
    result.reason = ReplayStopReason::WaitingForChoice;
    result.waiting_node = choice.logical_key;
    append_diagnostic(result.diagnostic, message);
}

bool walk_case(const ExecutionNode& case_node,
               const LogicalNodeKey& target_node,
               const DialogueProfile& profile,
               const ReplayOptions& options,
               bool continuation_may_reach_target,
               ReplayResult& result) {
    append_node_diagnostic(result, case_node);
    if (case_node.logical_key == target_node) {
        result.reason = ReplayStopReason::ReachedTarget;
        return true;
    }
    return walk_execution_nodes(case_node.ordinary_children, target_node,
                                profile, options,
                                continuation_may_reach_target, result);
}

bool walk_choice(const ExecutionNode& choice,
                 const LogicalNodeKey& target_node,
                 const DialogueProfile& profile,
                 const ReplayOptions& options,
                 bool continuation_may_reach_target,
                 ReplayResult& result) {
    // Choice metadata children are sequential and do not select a Case.
    if (walk_execution_nodes(choice.ordinary_children, target_node, profile,
                             options, continuation_may_reach_target, result)) {
        return true;
    }

    const bool target_in_cases =
        target_reachable_in_choice_cases(choice, target_node, options);
    const bool target_may_follow =
        continuation_may_reach_target || target_in_cases;
    if (choice.cases.empty()) {
        if (!target_may_follow) {
            return false;
        }
        wait_for_choice(choice, result, L"Choice 缺少 Case，无法继续静态回放");
        return true;
    }

    bool selection_was_requested = false;
    const ExecutionNode* selected_case = selected_case_for(
        choice, target_node, options, selection_was_requested);
    if (!selection_was_requested) {
        if (!target_may_follow) {
            return false;
        }
        wait_for_choice(choice, result,
                        L"Choice 尚未选择 Case，无法继续静态回放");
        return true;
    }
    if (selected_case == nullptr) {
        if (!target_may_follow) {
            return false;
        }
        wait_for_choice(choice, result, L"Choice 的 selected Case 不存在");
        return true;
    }

    return walk_case(*selected_case, target_node, profile, options,
                     continuation_may_reach_target, result);
}

bool walk_predicate(const ExecutionNode& predicate,
                    const LogicalNodeKey& target_node,
                    const DialogueProfile& profile,
                    const ReplayOptions& options,
                    bool continuation_may_reach_target,
                    ReplayResult& result) {
    const PredicateState state =
        effective_predicate_state(predicate, options);
    if (predicate.predicate_kind == PredicateKind::Unsupported ||
        state == PredicateState::Unsupported) {
        result.reason = ReplayStopReason::UnsupportedCondition;
        result.waiting_node = predicate.logical_key;
        append_diagnostic(result.diagnostic, L"条件节点的静态回放不支持");
        return true;
    }

    bool predicate_value = state != PredicateState::ExplicitFalse;
    std::wstring evaluation_diagnostic;
    if (state == PredicateState::DefaultTrue) {
        if (predicate.boolean_expression.decoded) {
            if (!evaluate_boolean_variable_expression(
                    predicate.boolean_expression,
                    result.state.simulated, predicate_value,
                    &evaluation_diagnostic)) {
                result.reason = ReplayStopReason::UnsupportedCondition;
                result.waiting_node = predicate.logical_key;
                append_diagnostic(
                    result.diagnostic,
                    evaluation_diagnostic.empty()
                        ? L"条件节点的静态回放不支持"
                        : evaluation_diagnostic);
                return true;
            }
        } else if (predicate.integer_expression.decoded) {
            if (!evaluate_integer_expression(
                    predicate.integer_expression,
                    result.state.simulated, predicate_value,
                    &evaluation_diagnostic)) {
                result.reason = ReplayStopReason::UnsupportedCondition;
                result.waiting_node = predicate.logical_key;
                append_diagnostic(
                    result.diagnostic,
                    evaluation_diagnostic.empty()
                        ? L"条件节点的静态回放不支持"
                        : evaluation_diagnostic);
                return true;
            }
        }
    }

    const ExecutionNodeList& active_children =
        predicate_value ? predicate.true_children : predicate.false_children;
    const ExecutionNodeList& skipped_children =
        predicate_value ? predicate.false_children : predicate.true_children;
    // If the selected target is inside the branch that the current replay
    // state has already ruled out, stop at the predicate.  Continuing through
    // later siblings would incorrectly apply unrelated tail dialogue and was
    // a parity regression in the first internalized walk.
    if (!contains_key(active_children, target_node) &&
        contains_key(skipped_children, target_node)) {
        result.reason = ReplayStopReason::TargetNotFound;
        append_diagnostic(result.diagnostic,
                          L"目标节点不在当前 Section 回放路径中");
        return true;
    }

    if (!predicate_value) {
        if (predicate.false_children.empty()) {
            return false;
        }
        return walk_execution_nodes(predicate.false_children, target_node,
                                    profile, options,
                                    continuation_may_reach_target, result);
    }

    if (predicate.true_children.empty()) {
        return false;
    }
    return walk_execution_nodes(predicate.true_children, target_node, profile,
                                options, continuation_may_reach_target, result);
}

bool walk_execution_node(const ExecutionNode& node,
                         const LogicalNodeKey& target_node,
                         const DialogueProfile& profile,
                         const ReplayOptions& options,
                         bool continuation_may_reach_target,
                         ReplayResult& result) {
    if (node.category == ExecutionCategory::Predicate) {
        return walk_predicate(node, target_node, profile, options,
                              continuation_may_reach_target, result);
    }

    if (node.category == ExecutionCategory::Choice) {
        return walk_choice(node, target_node, profile, options,
                           continuation_may_reach_target, result);
    }

    if (node.category == ExecutionCategory::IntegerDispatchGroup) {
        // This node is only a visual grouping.  Its original 0x79 predicate
        // blocks remain an ordinary sequential run, not an implicit Choice.
        // Walk each original predicate as a sequence member.  Passing the
        // grouped vector to walk_execution_nodes would treat the predicates
        // as ordinary siblings but lose the target-after-member boundary;
        // that made a selected member's dialogue disappear when its 0x79
        // expression evaluated true.
        for (std::size_t index = 0; index < node.cases.size(); ++index) {
            const ExecutionNode& case_node = node.cases[index];
            if (case_node.logical_key == target_node) {
                result.reason = ReplayStopReason::ReachedTarget;
                return true;
            }
            bool target_after_case = continuation_may_reach_target;
            if (!target_after_case) {
                for (std::size_t next = index + 1; next < node.cases.size();
                     ++next) {
                    if (node.cases[next].logical_key == target_node ||
                        execution_node_reaches_target(node.cases[next],
                                                      target_node, options)) {
                        target_after_case = true;
                        break;
                    }
                }
            }
            if (walk_execution_node(case_node, target_node, profile, options,
                                    target_after_case, result)) {
                return true;
            }
        }
        return false;
    }

    if (node.category == ExecutionCategory::Case ||
        node.category == ExecutionCategory::Diagnostic) {
        // A stray/diagnostic node is display-only.  In particular, do not
        // execute an orphan dialogue or portrait hidden below it.
        return false;
    }

    if (node.category == ExecutionCategory::Ordinary ||
        node.category == ExecutionCategory::UnconditionalContainer) {
        apply_dialogue_command(result.state, node.command, profile,
                               &result.diagnostic, true);
        result.last_applied_node = node.logical_key;
    }

    const bool child_result = walk_execution_nodes(
        node.ordinary_children, target_node, profile, options,
        continuation_may_reach_target, result);
    return child_result;
}

bool walk_execution_nodes(const ExecutionNodeList& nodes,
                          const LogicalNodeKey& target_node,
                          const DialogueProfile& profile,
                          const ReplayOptions& options,
                          bool continuation_may_reach_target,
                          ReplayResult& result) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const ExecutionNode& node = nodes[index];
        append_node_diagnostic(result, node);
        if (node.logical_key == target_node) {
            // The editor's native preview and the old DLL both include the
            // selected dialogue/face command itself, then stop before any
            // following node.  Non-preview ordinary commands have no dialogue
            // effect and remain unapplied here.
            if (node.category == ExecutionCategory::Ordinary &&
                is_preview_command(node, profile)) {
                apply_dialogue_command(result.state, node.command, profile,
                                       &result.diagnostic);
                result.last_applied_node = node.logical_key;
            }
            result.reason = ReplayStopReason::ReachedTarget;
            return true;
        }

        const bool target_after =
            continuation_may_reach_target ||
            execution_nodes_reach_target(
                ExecutionNodeList(nodes.begin() + index + 1, nodes.end()),
                target_node, options);
        if (walk_execution_node(node, target_node, profile, options,
                                target_after, result)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void apply_dialogue_command(
    PreviewState& state,
    const DialogueCommandView& command,
    const DialogueProfile& profile,
    std::wstring* diagnostic,
    bool use_last_embedded_dialogue) {
    track_rename_command(state, command);
    if(command.instruction_id==0x77) {
        if(!apply_simulated_arithmetic(state.simulated,command.int_data)) invalidate_simulated_destination(state.simulated,command.int_data);
    }
    if(command.instruction_id==0x78 && !apply_simulated_property(state.simulated,command.int_data,true) && diagnostic) append_diagnostic(*diagnostic,L"属性读写的模拟值未确定；未模拟的头像写入保持原头像");
    if(command.instruction_id==0x38) apply_simulated_avatar(state.simulated,command.int_data);
    if(command.instruction_id==0x35) apply_simulated_appearance(state.simulated,command.int_data,0);


    if (command.instruction_id == profile.face_state_instruction_id) {
        if (command.int_data.empty()) {
            if (diagnostic != nullptr) {
                append_diagnostic(*diagnostic, L"头像状态指令缺少状态字段");
            }
            return;
        }
        const std::int32_t raw_state = command.int_data.front();
        if (raw_state < 0 || raw_state > 0xffff) {
            if (diagnostic != nullptr) {
                append_diagnostic(*diagnostic, L"头像状态字段超出 16 位范围");
            }
            return;
        }
        state.face_state = static_cast<std::uint16_t>(raw_state);
        state.face_state_known = true;
        return;
    }

    const DialogueInstructionSpec* spec =
        find_dialogue_spec(profile, command.instruction_id);
    if (spec == nullptr) {
        return;
    }

    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, profile);
    DialogueState decoded = segments.empty()
                                ? decode_dialogue_command(command, profile)
                                : segments[use_last_embedded_dialogue
                                               ? segments.size() - 1
                                               : std::size_t{0}];
    apply_data_name_override(decoded, state);
    decoded.face_state = state.face_state;
    decoded.face_state_known = state.face_state_known;
    state.dialogue = std::move(decoded);
}


ReplayResult replay_section_to(
    const SectionExecutionModel& model,
    const LogicalNodeKey& target_node,
    const DialogueProfile& profile,
    const ReplayOptions& options) {
    ReplayResult result;
    result.state.simulated=options.simulated_variables;
    for (const std::wstring& diagnostic : model.diagnostics) {
        if (result.diagnostic.find(diagnostic) == std::wstring::npos) {
            append_diagnostic(result.diagnostic, diagnostic);
        }
    }

    if (!model.boundary_complete) {
        append_diagnostic(result.diagnostic,
                          L"Section 边界不完整，静态回放不可执行");
        return result;
    }

    const bool target_after_entry =
        execution_nodes_reach_target(model.body, target_node, options);
    if (!walk_execution_nodes(model.entry_gates, target_node, profile, options,
                              target_after_entry, result) &&
        !walk_execution_nodes(model.body, target_node, profile, options, false,
                              result) &&
        result.reason == ReplayStopReason::TargetNotFound) {
        append_diagnostic(result.diagnostic,
                          model_contains_key(model, target_node)
                              ? L"目标节点不在当前 Section 回放路径中"
                              : L"目标节点不在当前 Section 模型中");
    }
    return result;
}

}  // namespace ccz::preview
