#include <ccz/preview/playback_timeline.h>
#include <ccz/preview/dialogue_model.h>

#include <algorithm>
#include <set>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace ccz::preview {
namespace {

using ExecutionNodeList = std::vector<ExecutionNode>;

enum class WalkResult : std::uint8_t {
    Continue,
    ReachedTarget,
    Unresolved,
    Unsupported,
};

struct WalkState {
    const LogicalNodeKey& target;
    PlaybackRange range;
    ReplayOptions options;
    PlaybackTimeline& timeline;
    bool target_seen{};
    std::size_t target_index{};
    std::size_t target_end_index{};
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

bool contains_target(const ExecutionNode& node,
                     const LogicalNodeKey& target);

bool contains_target(const ExecutionNodeList& nodes,
                     const LogicalNodeKey& target) {
    for (const ExecutionNode& node : nodes) {
        if (contains_target(node, target)) {
            return true;
        }
    }
    return false;
}

bool contains_target(const ExecutionNode& node,
                     const LogicalNodeKey& target) {
    return node.logical_key == target ||
           contains_target(node.ordinary_children, target) ||
           contains_target(node.true_children, target) ||
           contains_target(node.false_children, target) ||
           contains_target(node.cases, target);
}

const ExecutionNode* find_node(const ExecutionNodeList& nodes,
                               const LogicalNodeKey& target) {
    for (const ExecutionNode& node : nodes) {
        if (node.logical_key == target) {
            return &node;
        }
        if (const ExecutionNode* found =
                find_node(node.ordinary_children, target);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found =
                find_node(node.true_children, target);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found =
                find_node(node.false_children, target);
            found != nullptr) {
            return found;
        }
        if (const ExecutionNode* found = find_node(node.cases, target);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const ExecutionNode* find_model_node(const SectionExecutionModel& model,
                                     const LogicalNodeKey& target) {
    if (const ExecutionNode* found = find_node(model.entry_gates, target);
        found != nullptr) {
        return found;
    }
    return find_node(model.body, target);
}

bool is_structural(const ExecutionNode& node) {
    switch (node.category) {
        case ExecutionCategory::Marker:
        case ExecutionCategory::SectionGate:
        case ExecutionCategory::ElseBranch:
        case ExecutionCategory::Case:
        case ExecutionCategory::Choice:
        case ExecutionCategory::IntegerDispatchGroup:
            return true;
        default:
            return false;
    }
}

PlaybackSegmentKind segment_kind_for(const ExecutionNode& node) {
    switch (node.instruction_id) {
        case 0x32:
            return PlaybackSegmentKind::CharacterMove;
        case 0x2a:
            return PlaybackSegmentKind::PortraitMove;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x1c:
        case 0x2c:
        case 0x33:
        case 0x34:
        case 0x63:
        case 0x69:
        case 0x7a:
            return PlaybackSegmentKind::Hold;
        default:
            return PlaybackSegmentKind::Instant;
    }
}

std::size_t append_event(const ExecutionNode& node, WalkState& state,
                        bool force) {
    if (!force && is_structural(node)) {
        return (std::numeric_limits<std::size_t>::max)();
    }

    if(node.instruction_id==0x77 && !apply_simulated_arithmetic(state.options.simulated_variables,node.command.int_data)) invalidate_simulated_destination(state.options.simulated_variables,node.command.int_data);
    if(node.instruction_id==0x38) apply_simulated_avatar(state.options.simulated_variables,node.command.int_data);
    if(node.instruction_id==0x78) apply_simulated_property(state.options.simulated_variables,node.command.int_data);
    const std::size_t first_index = state.timeline.events.size();
    const std::vector<DialogueSegment> dialogue_segments =
        node.instruction_id == 0x14
            ? split_dialogue_segments(node.command.text)
            : std::vector<DialogueSegment>{};
    const std::size_t segment_count = node.instruction_id == 0x7a ? decode_dialogue_segments(node.command,DialogueProfile{}).size() : node.instruction_id == 0x14 ? dialogue_segments.size() : 0;
    const std::size_t event_count =
        (node.instruction_id == 0x14 || node.instruction_id == 0x7a) ? (std::max)(std::size_t{1}, segment_count)
                                    : std::size_t{1};
    for (std::size_t segment_index = 0; segment_index < event_count;
         ++segment_index) {
        PlaybackEvent event;
        event.logical_key = node.logical_key;
        event.runtime_handle = node.runtime_handle;
        event.command = node.command;
        event.kind = segment_kind_for(node);
        event.duration_units =
            event.kind == PlaybackSegmentKind::Instant ? 0 : 1;
        if (node.instruction_id == 0x14 || node.instruction_id == 0x7a) {
            event.dialogue_segment_index = segment_index;
            event.dialogue_segment_count = event_count;
        }
        state.timeline.events.push_back(std::move(event));
    }
    return first_index;
}

PredicateState effective_predicate_state(const ExecutionNode& predicate,
                                         const ReplayOptions& options) {
    const auto found = options.predicate_states.find(predicate.logical_key);
    return found == options.predicate_states.end()
               ? predicate.predicate_state
               : found->second;
}

bool evaluate_predicate(const ExecutionNode& predicate,
                        const ReplayOptions& options, bool& result,
                        std::wstring& diagnostic) {
    const PredicateState state =
        effective_predicate_state(predicate, options);
    if (state == PredicateState::Unsupported ||
        predicate.predicate_kind == PredicateKind::Unsupported) {
        diagnostic = L"条件节点的动态回放不支持";
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
    }
    if (predicate.predicate_kind == PredicateKind::Integer &&
        predicate.integer_expression.decoded) {
        if (evaluate_integer_expression(
                predicate.integer_expression, options.simulated_variables,
                result, &diagnostic)) {
            return true;
        }
    }
    if (predicate.predicate_kind == PredicateKind::Boolean &&
        predicate.boolean_evaluation == BooleanEvaluationState::True) {
        result = true;
        return true;
    }
    if (predicate.predicate_kind == PredicateKind::Boolean &&
        predicate.boolean_evaluation == BooleanEvaluationState::False) {
        result = false;
        return true;
    }
    if (predicate.predicate_kind == PredicateKind::Integer &&
        predicate.integer_evaluation == IntegerEvaluationState::True) {
        result = true;
        return true;
    }
    if (predicate.predicate_kind == PredicateKind::Integer &&
        predicate.integer_evaluation == IntegerEvaluationState::False) {
        result = false;
        return true;
    }
    if (diagnostic.empty()) {
        diagnostic = L"条件节点的动态回放不支持";
    }
    return false;
}

const ExecutionNode* find_case_by_key(const ExecutionNode& choice,
                                      const LogicalNodeKey& key) {
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.logical_key == key) {
            return &case_node;
        }
    }
    return nullptr;
}

const ExecutionNode* find_case_with_state(const ExecutionNode& choice,
                                          ChoiceState state) {
    const ExecutionNode* selected = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (case_node.choice_state != state) {
            continue;
        }
        if (selected != nullptr) {
            return nullptr;
        }
        selected = &case_node;
    }
    return selected;
}

const ExecutionNode* find_unique_target_case(
    const ExecutionNode& choice, const LogicalNodeKey& target) {
    const ExecutionNode* selected = nullptr;
    for (const ExecutionNode& case_node : choice.cases) {
        if (!contains_target(case_node, target)) {
            continue;
        }
        if (selected != nullptr) {
            return nullptr;
        }
        selected = &case_node;
    }
    return selected;
}

WalkResult walk_nodes(const ExecutionNodeList& nodes, WalkState& state);

WalkResult walk_case(const ExecutionNode& case_node, WalkState& state) {
    if (case_node.logical_key == state.target && !state.target_seen) {
        state.target_seen = true;
        state.target_index = append_event(case_node, state, true);
        state.target_end_index = state.timeline.events.size();
        if (state.range == PlaybackRange::StartToTarget) {
            return WalkResult::ReachedTarget;
        }
    }
    return walk_nodes(case_node.ordinary_children, state);
}

WalkResult walk_choice(const ExecutionNode& choice, WalkState& state) {
    const WalkResult ordinary = walk_nodes(choice.ordinary_children, state);
    if (ordinary != WalkResult::Continue) {
        return ordinary;
    }

    const ExecutionNode* selected_case = nullptr;
    const auto selected =
        state.options.selected_case_by_choice.find(choice.logical_key);
    if (selected != state.options.selected_case_by_choice.end()) {
        selected_case = find_case_by_key(choice, selected->second);
    } else if (choice.choice_state == ChoiceState::SelectedCase) {
        selected_case = find_case_with_state(choice, ChoiceState::SelectedCase);
    } else if (choice.choice_state == ChoiceState::AutoTarget) {
        selected_case = find_unique_target_case(choice, state.target);
    }

    if (selected_case == nullptr) {
        append_diagnostic(state.timeline.diagnostic, L"包含未决选择支事件");
        return WalkResult::Unresolved;
    }
    return walk_case(*selected_case, state);
}

WalkResult walk_node(const ExecutionNode& node, WalkState& state) {
    if (node.logical_key == state.target && !state.target_seen) {
        state.target_seen = true;
        state.target_index = append_event(node, state, true);
        state.target_end_index = state.timeline.events.size();
        if (state.range == PlaybackRange::StartToTarget) {
            return WalkResult::ReachedTarget;
        }
    } else if (!is_structural(node)) {
        append_event(node, state, false);
    }

    switch (node.category) {
        case ExecutionCategory::Predicate: {
            bool predicate_value = false;
            std::wstring diagnostic;
            if (!evaluate_predicate(node, state.options, predicate_value,
                                    diagnostic)) {
                append_diagnostic(state.timeline.diagnostic,
                                  diagnostic.empty()
                                      ? L"条件节点的动态回放不支持"
                                      : diagnostic);
                return WalkResult::Unsupported;
            }
            const ExecutionNodeList& branch = predicate_value
                                                  ? node.true_children
                                                  : node.false_children;
            return walk_nodes(branch, state);
        }
        case ExecutionCategory::Choice:
            return walk_choice(node, state);
        case ExecutionCategory::IntegerDispatchGroup:
            return walk_nodes(node.cases, state);
        case ExecutionCategory::Case:
            return WalkResult::Continue;
        case ExecutionCategory::Diagnostic:
            append_diagnostic(state.timeline.diagnostic, node.diagnostic);
            return walk_nodes(node.ordinary_children, state);
        default:
            return walk_nodes(node.ordinary_children, state);
    }
}

WalkResult walk_nodes(const ExecutionNodeList& nodes, WalkState& state) {
    for (const ExecutionNode& node : nodes) {
        const WalkResult result = walk_node(node, state);
        if (result != WalkResult::Continue) {
            return result;
        }
    }
    return WalkResult::Continue;
}

}  // namespace

PlaybackTimeline group_consecutive_character_moves(const PlaybackTimeline& source) {
    if(!source.complete || source.events.empty() || source.range_begin>=source.range_end ||
       source.range_end>source.events.size() || source.target_index>=source.events.size() ||
       source.target_end_index<=source.target_index || source.target_end_index>source.events.size()) return source;
    PlaybackTimeline result=source;result.events.clear();
    std::vector<std::size_t> mapped(source.events.size()+1);
    for(std::size_t i=0;i<source.events.size();) {
        std::size_t end=i+1;
        const auto isMove=[](const PlaybackEvent& event) {
            return event.kind==PlaybackSegmentKind::CharacterMove &&
                event.command.int_data.size()>=6 && event.command.int_data[0]==0;
        };
        if(i>=source.range_begin && i<source.range_end && isMove(source.events[i])) {
            std::set<int> people{source.events[i].command.int_data[1]};
            while(end<source.range_end && isMove(source.events[end]) &&
                  people.insert(source.events[end].command.int_data[1]).second) ++end;
        }
        // The last instruction reconstructs the final pose of every member.
        result.events.push_back(source.events[end-1]);
        for(std::size_t j=i;j<end;++j) mapped[j]=result.events.size()-1;
        i=end;
    }
    mapped[source.events.size()]=result.events.size();
    result.target_index=mapped[source.target_index];
    result.target_end_index=mapped[source.target_end_index-1]+1;
    result.range_begin=mapped[source.range_begin];
    result.range_end=mapped[source.range_end-1]+1;
    return result;
}

PlaybackTimeline build_playback_timeline(
    const SectionExecutionModel& model, const LogicalNodeKey& target_node,
    PlaybackRange range, const ReplayOptions& options) {
    PlaybackTimeline timeline;
    if (!model.boundary_complete) {
        timeline.diagnostic = L"Section 边界不完整，动态回放不可执行";
        return timeline;
    }
    if (target_node.empty() || find_model_node(model, target_node) == nullptr) {
        timeline.diagnostic = L"目标节点不在当前 Section 模型中";
        return timeline;
    }

    // Always try to build the complete reachable Section path first.  The
    // player uses a range slice of this path, while the UI progress remains
    // anchored to the Section origin for both playback directions.
    PlaybackTimeline full;
    WalkState full_state{target_node, PlaybackRange::TargetToEnd, options, full};
    WalkResult result = walk_nodes(model.entry_gates, full_state);
    if (result == WalkResult::Continue) {
        result = walk_nodes(model.body, full_state);
    }

    if (result == WalkResult::Continue && full_state.target_seen) {
        full.complete = true;
        full.target_index = full_state.target_index;
        full.target_end_index = full_state.target_end_index;
        full.range_begin = range == PlaybackRange::TargetToEnd
                               ? full.target_index
                               : std::size_t{0};
        full.range_end = range == PlaybackRange::TargetToEnd
                             ? full.events.size()
                             : full.target_end_index;
    } else if (range == PlaybackRange::StartToTarget) {
        // Preserve the old useful behavior when the selected target itself is
        // reachable but an unsupported/unresolved tail prevents discovering a
        // later endpoint.  In that degraded case the selected target is the
        // best-known reachable endpoint.
        PlaybackTimeline prefix;
        WalkState prefix_state{target_node, PlaybackRange::StartToTarget,
                               options, prefix};
        WalkResult prefix_result = walk_nodes(model.entry_gates, prefix_state);
        if (prefix_result == WalkResult::Continue) {
            prefix_result = walk_nodes(model.body, prefix_state);
        }
        if (prefix_result == WalkResult::ReachedTarget &&
            prefix_state.target_seen) {
            prefix.complete = true;
            prefix.target_index = prefix_state.target_index;
            prefix.target_end_index = prefix_state.target_end_index;
            prefix.range_begin = 0;
            prefix.range_end = prefix.events.size();
            return prefix;
        }
        append_diagnostic(full.diagnostic, prefix.diagnostic);
    }

    if (!full.complete) {
        if (!full_state.target_seen) {
            append_diagnostic(full.diagnostic,
                              L"目标节点不在当前 Section 动态回放路径中");
        }
        full.events.clear();
        full.target_index = 0;
        full.target_end_index = 0;
        full.range_begin = 0;
        full.range_end = 0;
        return full;
    }

    if (full.target_index >= full.events.size() ||
        full.target_end_index <= full.target_index ||
        full.target_end_index > full.events.size() ||
        full.range_begin >= full.range_end ||
        full.range_end > full.events.size()) {
        full.complete = false;
        full.events.clear();
        full.target_index = 0;
        full.target_end_index = 0;
        full.range_begin = 0;
        full.range_end = 0;
        append_diagnostic(full.diagnostic, L"动态回放目标索引无效");
        return full;
    }
    if (full.target_index > 0) {
        full.pre_target_key =
            full.events[full.target_index - 1].logical_key;
    }
    return full;
}

}  // namespace ccz::preview
