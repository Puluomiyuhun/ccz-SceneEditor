#include "PreviewSession.h"

#include <ccz/preview/condition_tree_state.h>
#include <ccz/preview/dialogue_replay.h>
#include <ccz/preview/native_replay_plan.h>
#include <ccz/preview/portrait_overlay.h>
#include <ccz/preview/script_variable_index.h>
#include <ccz/preview/variable_control_state.h>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace ccz::editor_preview {
namespace {

using ccz::preview::ConditionTreeAction;
using ccz::preview::ConditionTreeActionState;
using ccz::preview::ExecutionCategory;
using ccz::preview::ExecutionNode;
using ccz::preview::LogicalNodeKey;
using ccz::preview::NativeReplayItem;
using ccz::preview::NativeReplayPlan;
using ccz::preview::PredicateKind;
using ccz::preview::PredicateState;
using ccz::preview::ReplayOptions;
using ccz::preview::RuntimeNodeHandle;
using ccz::preview::SectionExecutionModel;
using ccz::preview::SimulationEnvironment;

std::optional<std::int32_t> parse_data_speaker_label(
    const std::wstring& label) {
    if (label.size() < 3 || label[0] != L'*' || label[1] != L'.') {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 2; index < label.size(); ++index) {
        const wchar_t character = label[index];
        if (character < L'0' || character > L'9') {
            return std::nullopt;
        }
        value = value * 10u + static_cast<std::uint32_t>(character - L'0');
        if (value > 1023u) {
            return std::nullopt;
        }
    }
    return static_cast<std::int32_t>(value);
}

std::wstring read_data_person_name(
    const std::vector<std::uint8_t>& data,
    const ccz::preview::DialogueProfile& profile,
    std::int32_t person_id) {
    if (person_id < 0 || person_id > 1023 || profile.data_record_stride == 0) {
        return {};
    }
    const std::uint64_t offset =
        static_cast<std::uint64_t>(profile.data_record_base) +
        static_cast<std::uint64_t>(person_id) * profile.data_record_stride;
    if (offset + 12 > data.size()) {
        return {};
    }
    char raw[12]{};
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), 12, raw);
    const int length = MultiByteToWideChar(936, 0, raw, 12, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(936, 0, raw, 12, result.data(), length) != length) {
        return {};
    }
    const std::size_t nul = result.find(L'\0');
    if (nul != std::wstring::npos) {
        result.resize(nul);
    }
    return result;
}

std::int32_t find_person_by_label(
    const std::vector<std::uint8_t>& data,
    const ccz::preview::DialogueProfile& profile,
    const std::wstring& label) {
    if (label.empty()) {
        return -1;
    }
    for (std::int32_t person_id = 0; person_id <= 1023; ++person_id) {
        const std::wstring name =
            read_data_person_name(data, profile, person_id);
        if (name == label) {
            return person_id;
        }
        const std::size_t colon = name.find(L':');
        if (colon != std::wstring::npos && name.substr(colon + 1) == label) {
            return person_id;
        }
    }
    return -1;
}

void append_diagnostic(std::vector<std::wstring>& diagnostics,
                       const std::wstring& value) {
    if (value.empty() ||
        std::find(diagnostics.begin(), diagnostics.end(), value) !=
            diagnostics.end()) {
        return;
    }
    diagnostics.push_back(value);
}

const ExecutionNode* find_node(const std::vector<ExecutionNode>& nodes,
                               const LogicalNodeKey& key) {
    for (const ExecutionNode& node : nodes) {
        if (node.logical_key == key) {
            return &node;
        }
        if (const ExecutionNode* found =
                find_node(node.ordinary_children, key)) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.true_children, key)) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.false_children, key)) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.cases, key)) {
            return found;
        }
    }
    return nullptr;
}

const ExecutionNode* find_model_node(const SectionExecutionModel& model,
                                     const LogicalNodeKey& key) {
    if (const ExecutionNode* found = find_node(model.entry_gates, key)) {
        return found;
    }
    return find_node(model.body, key);
}

const ExecutionNode* find_case_owner(const std::vector<ExecutionNode>& nodes,
                                      const LogicalNodeKey& case_key) {
    for (const ExecutionNode& node : nodes) {
        if (node.category == ExecutionCategory::Choice) {
            for (const ExecutionNode& case_node : node.cases) {
                if (case_node.logical_key == case_key) {
                    return &node;
                }
            }
        }
        if (const ExecutionNode* found =
                find_case_owner(node.ordinary_children, case_key)) {
            return found;
        }
        if (const ExecutionNode* found =
                find_case_owner(node.true_children, case_key)) {
            return found;
        }
        if (const ExecutionNode* found =
                find_case_owner(node.false_children, case_key)) {
            return found;
        }
        if (const ExecutionNode* found = find_case_owner(node.cases, case_key)) {
            return found;
        }
    }
    return nullptr;
}

const ExecutionNode* find_case_owner(const SectionExecutionModel& model,
                                     const LogicalNodeKey& case_key) {
    if (const ExecutionNode* found =
            find_case_owner(model.entry_gates, case_key)) {
        return found;
    }
    return find_case_owner(model.body, case_key);
}

bool native_item_equal(const NativeReplayItem& left,
                       const NativeReplayItem& right) {
    return left.logical_key == right.logical_key &&
           left.runtime_handle == right.runtime_handle &&
           left.command.instruction_id == right.command.instruction_id &&
           left.command.int_data == right.command.int_data &&
           left.command.text == right.command.text;
}

bool native_plan_equal(const NativeReplayPlan& left,
                       const NativeReplayPlan& right) {
    if (left.stop_reason != right.stop_reason ||
        left.target_reached != right.target_reached ||
        left.complete != right.complete ||
        left.diagnostic != right.diagnostic ||
        left.items.size() != right.items.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        if (!native_item_equal(left.items[index], right.items[index])) {
            return false;
        }
    }
    return true;
}

bool variable_index_equal(const ccz::preview::ScriptVariableIndex& left,
                          const ccz::preview::ScriptVariableIndex& right) {
    return left.document_key == right.document_key &&
           left.source_fingerprint == right.source_fingerprint &&
           left.valid == right.valid && left.diagnostics == right.diagnostics;
}

bool frame_equal(const PreviewFrame& left, const PreviewFrame& right) {
    const bool models_equal =
        left.model != nullptr && right.model != nullptr
            ? ccz::preview::execution_model_equal(*left.model, *right.model)
            : left.model == right.model;
    return models_equal && native_plan_equal(left.native_plan, right.native_plan) &&
           ccz::preview::dialogue_overlay_model_equal(left.dialogue,
                                                      right.dialogue) &&
           variable_index_equal(left.variable_index, right.variable_index) &&
           left.active_node == right.active_node &&
           left.diagnostics == right.diagnostics;
}

bool belongs_to_section(const LogicalNodeKey& key,
                        const LogicalNodeKey& section_key) {
    if (section_key.empty()) {
        return false;
    }
    const LogicalNodeKey prefix = section_key + L"|";
    return key == section_key || key.rfind(prefix, 0) == 0;
}

bool is_unqualified_key(const LogicalNodeKey& key) {
    return key.find(L'|') == LogicalNodeKey::npos;
}

struct SectionStateKeys {
    std::set<LogicalNodeKey> predicates;
    std::map<LogicalNodeKey, std::set<LogicalNodeKey>> choices;
};

void collect_section_state_keys(const std::vector<ExecutionNode>& nodes,
                                SectionStateKeys& keys) {
    for (const ExecutionNode& node : nodes) {
        if (node.category == ExecutionCategory::Predicate) {
            keys.predicates.insert(node.logical_key);
        }
        if (node.category == ExecutionCategory::Choice) {
            std::set<LogicalNodeKey>& cases = keys.choices[node.logical_key];
            for (const ExecutionNode& case_node : node.cases) {
                cases.insert(case_node.logical_key);
            }
        }
        collect_section_state_keys(node.ordinary_children, keys);
        collect_section_state_keys(node.true_children, keys);
        collect_section_state_keys(node.false_children, keys);
        collect_section_state_keys(node.cases, keys);
    }
}

bool prune_invalid_section_state(ReplayOptions& options,
                                 const SectionExecutionModel& model,
                                 const LogicalNodeKey& section_key) {
    if (section_key.empty()) {
        return false;
    }
    SectionStateKeys valid;
    collect_section_state_keys(model.entry_gates, valid);
    collect_section_state_keys(model.body, valid);

    // Only keys owned by this Section are candidates for removal.  Qualified
    // keys from another Section remain available when the user switches back;
    // unqualified keys are retained solely for the synthetic test/legacy
    // adapter inputs that predate stable Section-qualified identities.
    bool changed = false;
    for (auto iterator = options.predicate_states.begin();
         iterator != options.predicate_states.end();) {
        const bool is_current_predicate =
            belongs_to_section(iterator->first, section_key) ||
            is_unqualified_key(iterator->first);
        if (is_current_predicate &&
            valid.predicates.find(iterator->first) == valid.predicates.end()) {
            iterator = options.predicate_states.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }

    for (auto iterator = options.selected_case_by_choice.begin();
         iterator != options.selected_case_by_choice.end();) {
        const bool is_current_choice =
            belongs_to_section(iterator->first, section_key) ||
            is_unqualified_key(iterator->first) ||
            valid.choices.find(iterator->first) != valid.choices.end();
        if (!is_current_choice) {
            ++iterator;
            continue;
        }
        const auto choice = valid.choices.find(iterator->first);
        const bool valid_selection =
            choice != valid.choices.end() &&
            choice->second.find(iterator->second) != choice->second.end();
        if (!valid_selection) {
            iterator = options.selected_case_by_choice.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    return changed;
}

class ScopedBoolean {
public:
    explicit ScopedBoolean(bool& value) : value_(value) { value_ = true; }
    ~ScopedBoolean() { value_ = false; }
    ScopedBoolean(const ScopedBoolean&) = delete;
    ScopedBoolean& operator=(const ScopedBoolean&) = delete;

private:
    bool& value_;
};

bool condition_action_enabled(const ConditionTreeActionState& state,
                              ConditionTreeAction action) {
    switch (action) {
        case ConditionTreeAction::SetPredicateTrue:
            return state.can_set_true;
        case ConditionTreeAction::SetPredicateFalse:
            return state.can_set_false;
        case ConditionTreeAction::MakePredicateReachable:
            return state.can_make_reachable;
        case ConditionTreeAction::RestorePredicateDefault:
            return state.can_restore_predicate;
        case ConditionTreeAction::SetIntegerValue:
            return state.can_set_integer;
        case ConditionTreeAction::RestoreIntegerDefault:
            return state.can_restore_integer;
        case ConditionTreeAction::SelectCase:
            return state.can_select_case;
        case ConditionTreeAction::RestoreChoiceAuto:
            return state.can_restore_choice_auto;
    }
    return false;
}

bool apply_condition_action_to_options(
    ReplayOptions& options, const SectionExecutionModel& model,
    const LogicalNodeKey& node_key, ConditionTreeAction action,
    std::int32_t integer_value, std::wstring* diagnostic) {
    const ExecutionNode* selected = find_model_node(model, node_key);
    if (selected == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = L"条件节点不存在：" + node_key;
        }
        return false;
    }

    const ConditionTreeActionState state =
        ccz::preview::condition_tree_action_state(selected, options);
    if (!condition_action_enabled(state, action)) {
        if (diagnostic != nullptr) {
            *diagnostic = L"当前条件节点不支持该操作：" + node_key;
        }
        return false;
    }

    if (selected->category == ExecutionCategory::Predicate) {
        if (selected->predicate_kind == PredicateKind::Integer) {
            if (action == ConditionTreeAction::MakePredicateReachable) {
                return ccz::preview::apply_integer_variable_assignment(
                    options.simulated_variables, selected->integer_expression,
                    true, diagnostic);
            }
            if (action == ConditionTreeAction::SetIntegerValue) {
                auto& values = options.simulated_variables.integer_variables;
                const auto found =
                    values.find(selected->integer_expression.left.raw_value);
                if (integer_value == 0) {
                    if (found == values.end()) {
                        return false;
                    }
                    values.erase(found);
                    return true;
                }
                if (found != values.end() && found->second == integer_value) {
                    return false;
                }
                values[selected->integer_expression.left.raw_value] =
                    integer_value;
                return true;
            }
            if (action == ConditionTreeAction::RestoreIntegerDefault) {
                return options.simulated_variables.integer_variables.erase(
                           selected->integer_expression.left.raw_value) != 0;
            }
            return false;
        }

        if (selected->instruction_id == 0x05 &&
            selected->boolean_expression.decoded) {
            if (action == ConditionTreeAction::MakePredicateReachable) {
                return ccz::preview::apply_boolean_variable_assignment(
                    options.simulated_variables,
                    selected->boolean_expression, true, diagnostic);
            }
            if (action == ConditionTreeAction::SetPredicateTrue ||
                action == ConditionTreeAction::SetPredicateFalse) {
                return ccz::preview::apply_boolean_variable_assignment(
                    options.simulated_variables,
                    selected->boolean_expression,
                    action == ConditionTreeAction::SetPredicateTrue, diagnostic);
            }
            if (action == ConditionTreeAction::RestorePredicateDefault) {
                bool changed = false;
                for (const std::int32_t variable :
                     selected->boolean_expression.true_variables) {
                    auto& value =
                        options.simulated_variables.boolean_variables[variable];
                    changed = value || changed;
                    value = false;
                }
                for (const std::int32_t variable :
                     selected->boolean_expression.false_variables) {
                    auto& value =
                        options.simulated_variables.boolean_variables[variable];
                    changed = value || changed;
                    value = false;
                }
                return changed;
            }
            return false;
        }

        if (action == ConditionTreeAction::SetPredicateTrue) {
            const auto found = options.predicate_states.find(node_key);
            if (found != options.predicate_states.end() &&
                found->second == PredicateState::ExplicitTrue) {
                return false;
            }
            options.predicate_states[node_key] = PredicateState::ExplicitTrue;
            return true;
        }
        if (action == ConditionTreeAction::SetPredicateFalse) {
            const auto found = options.predicate_states.find(node_key);
            if (found != options.predicate_states.end() &&
                found->second == PredicateState::ExplicitFalse) {
                return false;
            }
            options.predicate_states[node_key] = PredicateState::ExplicitFalse;
            return true;
        }
        if (action == ConditionTreeAction::RestorePredicateDefault) {
            return options.predicate_states.erase(node_key) != 0;
        }
        return false;
    }

    if (selected->category == ExecutionCategory::Case &&
        (action == ConditionTreeAction::SelectCase ||
         action == ConditionTreeAction::RestoreChoiceAuto)) {
        const ExecutionNode* owner = find_case_owner(model, node_key);
        if (owner == nullptr) {
            if (diagnostic != nullptr) {
                *diagnostic = L"Case 的 Choice 所有者不存在：" + node_key;
            }
            return false;
        }
        if (action == ConditionTreeAction::SelectCase) {
            const auto found =
                options.selected_case_by_choice.find(owner->logical_key);
            if (found != options.selected_case_by_choice.end() &&
                found->second == node_key) {
                return false;
            }
            options.selected_case_by_choice[owner->logical_key] = node_key;
            return true;
        }
        return options.selected_case_by_choice.erase(owner->logical_key) != 0;
    }
    return false;
}

void append_replay_diagnostics(PreviewFrame& frame,
                               const ccz::preview::ReplayResult& replay) {
    append_diagnostic(frame.diagnostics, replay.diagnostic);
    append_diagnostic(frame.diagnostics, frame.dialogue.diagnostic);
    append_diagnostic(frame.diagnostics, frame.native_plan.diagnostic);
    for (const std::wstring& diagnostic : frame.variable_index.diagnostics) {
        append_diagnostic(frame.diagnostics, diagnostic);
    }
}

}  // namespace

PreviewFrame BuildPreviewFrame(
    const EditorPreviewInput& input, const SectionExecutionModel& model,
    const ReplayOptions& options,
    const ccz::preview::InstructionCatalog& catalog,
    const ccz::preview::DialogueProfile& profile,
    const PreviewAssetsView& assets,
    std::optional<std::size_t> dialogue_segment_index) {
    PreviewFrame frame;
    frame.model = std::make_shared<SectionExecutionModel>(model);
    frame.variable_index = ccz::preview::scan_script_variables(
        input.document_nodes, input.document_key);
    ccz::preview::ReplayResult replay = ccz::preview::replay_section_to(
        model, input.section.selected_target_key, profile, options);
    frame.native_plan = ccz::preview::build_native_replay_plan(
        model, input.section.selected_target_key, profile, options);
    frame.active_node = input.section.selected_target_key;

    if (dialogue_segment_index.has_value()) {
        const ExecutionNode* target =
            find_model_node(model, input.section.selected_target_key);
        if (target != nullptr && target->instruction_id == 0x14) {
            const std::vector<ccz::preview::DialogueState> segments =
                ccz::preview::decode_dialogue_segments(target->command,
                                                       profile);
            if (*dialogue_segment_index < segments.size()) {
                ccz::preview::DialogueState selected =
                    segments[*dialogue_segment_index];
                selected.face_state = replay.state.face_state;
                selected.face_state_known = replay.state.face_state_known;
                replay.state.dialogue = std::move(selected);
            } else if (!replay.diagnostic.empty()) {
                replay.diagnostic += L"；";
                replay.diagnostic += L"对白段索引超出范围";
            } else {
                replay.diagnostic = L"对白段索引超出范围";
            }
        }
    }

    frame.dialogue.visible = replay.state.dialogue.visible;
    frame.dialogue.speaker_label = replay.state.dialogue.speaker_label;
    frame.dialogue.body = replay.state.dialogue.body;
    frame.dialogue.diagnostic = replay.state.dialogue.diagnostic;

    // A target that is present in the typed Section model but is rejected by
    // the currently selected predicate branch is an inactive child event,
    // not a malformed target.  Publish a dedicated overlay state so the UI
    // can show the requested red hint even when the replay has no dialogue
    // left to display.
    const bool target_exists = find_model_node(
        model, input.section.selected_target_key) != nullptr;
    const bool inactive_sub_event = target_exists &&
        replay.reason == ccz::preview::ReplayStopReason::TargetNotFound &&
        replay.diagnostic.find(L"目标节点不在当前 Section 回放路径中") !=
            std::wstring::npos;
    frame.dialogue.inactive_sub_event = inactive_sub_event;
    if (inactive_sub_event) {
        frame.dialogue.visible = false;
        frame.dialogue.show_speaker = false;
        frame.dialogue.show_portrait = false;
        frame.dialogue.speaker_label.clear();
        frame.dialogue.body.clear();
        frame.dialogue.portrait_image.clear();
        frame.dialogue.diagnostic = L"当前子事件未激活";
    }

    const bool dialogue_visible =
        replay.state.dialogue.visible && !inactive_sub_event;
    frame.dialogue.visible = dialogue_visible;
    const ccz::preview::DialogueInstructionSpec* dialogue_spec =
        ccz::preview::find_dialogue_spec(
            profile, replay.state.dialogue.source_instruction_id);
    const bool plain_text_dialogue =
        dialogue_visible && dialogue_spec != nullptr &&
        !dialogue_spec->portrait_required;
    frame.dialogue.show_speaker =
        dialogue_visible && !plain_text_dialogue;
    frame.dialogue.show_portrait =
        dialogue_visible && !plain_text_dialogue;

    if (assets.data_e5 != nullptr && assets.face_e5 != nullptr &&
        assets.face_archive != nullptr && dialogue_visible &&
        frame.dialogue.show_portrait) {
        std::int32_t person_id = -1;
        for (const ccz::preview::PersonReference& person :
             replay.state.dialogue.person_references) {
            if (person.kind == ccz::preview::PersonReferenceKind::DataPerson) {
                person_id = person.value;
                break;
            }
        }
        if (person_id < 0 && !replay.state.dialogue.speaker_label.empty()) {
            person_id = find_person_by_label(
                *assets.data_e5, profile, replay.state.dialogue.speaker_label);
        }
        if (person_id >= 0) {
            const auto override_name =
                replay.state.data_name_overrides.find(person_id);
            if (override_name != replay.state.data_name_overrides.end()) {
                frame.dialogue.speaker_label = override_name->second;
            } else if (const auto token = parse_data_speaker_label(
                           replay.state.dialogue.speaker_label);
                       token.has_value()) {
                const std::wstring name = read_data_person_name(
                    *assets.data_e5, profile, *token);
                if (!name.empty()) {
                    frame.dialogue.speaker_label = name;
                }
            }
            ccz::preview::populate_portrait_overlay(
                frame.dialogue, person_id, replay.state.dialogue.face_state,
                *assets.data_e5, profile, *assets.face_archive, *assets.face_e5);
        }
    }

    append_replay_diagnostics(frame, replay);
    if (assets.diagnostic != nullptr && !assets.diagnostic->empty()) {
        if (!frame.dialogue.diagnostic.empty()) {
            frame.dialogue.diagnostic += L"；";
        }
        frame.dialogue.diagnostic += *assets.diagnostic;
        append_diagnostic(frame.diagnostics, *assets.diagnostic);
    }
    if (!catalog.valid && !catalog.diagnostic.empty()) {
        append_diagnostic(frame.diagnostics, catalog.diagnostic);
    }
    return frame;
}

RefreshResult PreviewSession::Refresh(
    const EditorPreviewInput& input,
    const ccz::preview::InstructionCatalog& catalog,
    const ccz::preview::DialogueProfile& profile,
    const PreviewAssetsView& assets) {
    if (refreshing_) {
        return {RefreshStatus::ReentrantRejected, false,
                L"预览刷新发生重入，已拒绝本次刷新"};
    }
    ScopedBoolean guard(refreshing_);

#if defined(CCZ_PREVIEW_SESSION_TESTING)
    if (refresh_probe_) {
        refresh_probe_();
    }
#endif

    if (input.document_key.empty() || input.section.section_key.empty() ||
        input.section.selected_target_key.empty() ||
        input.section.nodes.empty()) {
        return {RefreshStatus::InvalidInput, false, L"预览输入为空或缺少 Section"};
    }

    ReplayOptions next_options = options_;
    const bool document_changed =
        !document_key_.empty() && document_key_ != input.document_key;
    if (document_changed) {
        next_options = {};
    }

    const ccz::preview::ScriptVariableIndex next_index =
        ccz::preview::scan_script_variables(input.document_nodes,
                                            input.document_key);
    ccz::preview::synchronize_boolean_defaults(
        next_options.simulated_variables, next_index);
    ccz::preview::synchronize_integer_scope(
        next_options.simulated_variables, next_index);

    SectionExecutionModel next_model = ccz::preview::build_section_execution_model(
        input.section, catalog, next_options);
    if (!next_model.boundary_complete) {
        return {RefreshStatus::InvalidInput, false,
                L"Section 缺少顶层 0x00 边界，未发布部分模型"};
    }
    if (find_model_node(next_model, input.section.selected_target_key) ==
        nullptr) {
        return {RefreshStatus::InvalidInput, false,
                L"目标节点不在当前 Section 模型中，未发布部分重放"};
    }

    // A target that exists in the model is valid input even when the native
    // plan cannot reach it because a Choice is unresolved or a condition is
    // unsupported.  Keep that diagnostic frame for the condition/dialogue
    // views; the later native-plan applier must require complete=true.

    const bool section_changed = section_key_ != input.section.section_key;
    bool options_changed = false;
    if (section_changed || document_changed ||
        input.document_revision != document_revision_) {
        options_changed = prune_invalid_section_state(
                              next_options, next_model,
                              input.section.section_key) ||
                          options_changed;
    }
    if (ccz::preview::initialize_default_choice_selections(next_model,
                                                           next_options)) {
        options_changed = true;
    }
    if (options_changed) {
        next_model = ccz::preview::build_section_execution_model(
            input.section, catalog, next_options);
    }

    PreviewFrame next_frame = BuildPreviewFrame(
        input, next_model, next_options, catalog, profile, assets);
    const bool source_changed = document_changed ||
                                input.document_revision != document_revision_;
    const bool changed = source_changed || !frame_equal(frame_, next_frame) ||
                         next_options.predicate_states !=
                             options_.predicate_states ||
                         next_options.selected_case_by_choice !=
                             options_.selected_case_by_choice ||
                         next_options.simulated_variables.boolean_variables !=
                             options_.simulated_variables.boolean_variables ||
                         next_options.simulated_variables.integer_variables !=
                             options_.simulated_variables.integer_variables;

    options_ = std::move(next_options);
    frame_ = std::move(next_frame);
    document_key_ = input.document_key;
    section_key_ = input.section.section_key;
    document_revision_ = input.document_revision;
    if (!changed) {
        return {RefreshStatus::Unchanged, false, {}};
    }
    return {RefreshStatus::Updated, true, {}};
}

bool PreviewSession::ApplyConditionAction(
    const LogicalNodeKey& node_key, ConditionTreeAction action,
    std::int32_t integer_value, std::wstring* diagnostic) {
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    if (frame_.model == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = L"当前没有可操作的预览模型";
        }
        return false;
    }
    ReplayOptions candidate = options_;
    if (!apply_condition_action_to_options(candidate, *frame_.model, node_key,
                                           action, integer_value, diagnostic)) {
        return false;
    }
    options_ = std::move(candidate);
    return true;
}

bool PreviewSession::ActivateTarget(
    const ccz::preview::LogicalNodeKey& target_key,
    std::wstring* diagnostic) {
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    if (frame_.model == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = L"当前没有可操作的预览模型";
        }
        return false;
    }
    ReplayOptions candidate = options_;
    if (!ccz::preview::activate_condition_tree_target(
            candidate, *frame_.model, target_key, diagnostic)) {
        return false;
    }
    options_ = std::move(candidate);
    return true;
}

bool PreviewSession::ApplyVariableAction(
    ccz::preview::VariableControlKind kind, std::int32_t id,
    ccz::preview::VariableControlAction action, std::int32_t integer_value,
    std::wstring* diagnostic) {
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    ReplayOptions candidate = options_;
    if (!ccz::preview::apply_variable_control_action(
            candidate.simulated_variables, kind, id, action, integer_value)) {
        if (diagnostic != nullptr) {
            *diagnostic = L"变量操作未产生状态变化";
        }
        return false;
    }
    options_ = std::move(candidate);
    return true;
}

void PreviewSession::ResetForDocument(const LogicalNodeKey& document_key) {
    options_ = {};
    frame_ = {};
    document_key_ = document_key;
    section_key_.clear();
    document_revision_ = 0;
}

const PreviewFrame& PreviewSession::frame() const noexcept { return frame_; }

const ReplayOptions& PreviewSession::options() const noexcept {
    return options_;
}

#if defined(CCZ_PREVIEW_SESSION_TESTING)
void PreviewSession::SetRefreshProbeForTesting(std::function<void()> probe) {
    refresh_probe_ = std::move(probe);
}
#endif

}  // namespace ccz::editor_preview
