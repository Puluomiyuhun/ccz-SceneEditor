#include "BattleConditionTreeAdapter.h"

#include <ccz/preview/condition_tree_state.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

namespace ccz::editor_preview {
namespace {

using ccz::preview::LogicalNodeKey;
using ccz::preview::RawReplayNode;
using ccz::preview::ReplayOptions;
using ccz::preview::SectionExecutionModel;

constexpr std::uint16_t kContainerInstruction = 0xffff;

bool key_in_scope(const LogicalNodeKey& key, const LogicalNodeKey& scope) {
    if (key.empty() || scope.empty()) return false;
    if (key == scope) return true;
    if (key.size() <= scope.size() ||
        key.compare(0, scope.size(), scope) != 0) {
        return false;
    }
    // Native editor keys extend the section's node path with '/'. Synthetic
    // and legacy keys use '|'. Both are structural boundaries after a scope.
    const wchar_t separator = key[scope.size()];
    return separator == L'|' || separator == L'/';
}

const RawReplayNode* find_section_node(
    const std::vector<RawReplayNode>& nodes, std::uintptr_t section_handle) {
    for (const RawReplayNode& node : nodes) {
        if (node.runtime_handle == section_handle &&
            node.instruction_id == kContainerInstruction) {
            return &node;
        }
        if (const RawReplayNode* found =
                find_section_node(node.children, section_handle)) {
            return found;
        }
    }
    return nullptr;
}

bool is_deployment(std::uint16_t instruction_id) {
    return instruction_id == 0x4b || instruction_id == 0x46 ||
        instruction_id == 0x47;
}

bool reaches_key(const SectionExecutionModel& model,
                 const LogicalNodeKey& key, const ReplayOptions& options) {
    return ccz::preview::execution_nodes_reach_target(
               model.entry_gates, key, options) ||
        ccz::preview::execution_nodes_reach_target(
               model.body, key, options);
}

struct NodeLocation {
    const RawReplayNode* node{};
    std::vector<const RawReplayNode*> ancestors;
};

void collect_locations(const RawReplayNode& node,
                       std::vector<const RawReplayNode*>& ancestors,
                       std::vector<NodeLocation>& output) {
    output.push_back({&node, ancestors});
    ancestors.push_back(&node);
    for (const RawReplayNode& child : node.children) {
        collect_locations(child, ancestors, output);
    }
    ancestors.pop_back();
}

bool has_final_terminator(const RawReplayNode& node) {
    if (node.children.empty()) return false;
    const RawReplayNode& terminal = node.children.back();
    return terminal.instruction_id == 0 && terminal.children.empty() &&
        !node.logical_key.empty() && !terminal.logical_key.empty();
}

bool execution_node_has_deployment(const ccz::preview::ExecutionNode& node) {
    if (is_deployment(node.instruction_id)) return true;
    const auto contains = [](const auto& children, const auto& self) {
        return std::any_of(children.begin(), children.end(),
            [&self](const auto& child) { return self(child); });
    };
    return contains(node.ordinary_children, execution_node_has_deployment) ||
        contains(node.true_children, execution_node_has_deployment) ||
        contains(node.false_children, execution_node_has_deployment) ||
        contains(node.cases, execution_node_has_deployment);
}

bool has_unresolved_deployment_path(
    const std::vector<ccz::preview::ExecutionNode>& nodes,
    const ReplayOptions& options) {
    using namespace ccz::preview;
    for (const ExecutionNode& node : nodes) {
        const auto predicate_override =
            options.predicate_states.find(node.logical_key);
        const bool explicitly_unsupported =
            predicate_override != options.predicate_states.end() &&
            predicate_override->second == PredicateState::Unsupported;
        const bool unsupported_predicate =
            node.category == ExecutionCategory::Predicate &&
            (node.predicate_kind == PredicateKind::Unsupported ||
             explicitly_unsupported ||
             (node.predicate_kind == PredicateKind::Integer &&
              node.integer_evaluation == IntegerEvaluationState::Unsupported) ||
             ((node.instruction_id == 0x05) &&
              node.boolean_evaluation == BooleanEvaluationState::Unsupported));
        const bool unresolved_choice =
            node.category == ExecutionCategory::Choice &&
            node.choice_state == ChoiceState::Unselected;
        if ((unsupported_predicate || unresolved_choice ||
             node.category == ExecutionCategory::Diagnostic) &&
            execution_node_has_deployment(node)) {
            return true;
        }
        if (has_unresolved_deployment_path(node.ordinary_children, options) ||
            has_unresolved_deployment_path(node.true_children, options) ||
            has_unresolved_deployment_path(node.false_children, options) ||
            has_unresolved_deployment_path(node.cases, options)) {
            return true;
        }
    }
    return false;
}

std::wstring make_workspace_key(
    const LogicalNodeKey& section_key,
    const std::vector<LogicalNodeKey>& deployment_keys,
    const LogicalNodeKey& insert_parent_key,
    const LogicalNodeKey& insert_before_key,
    const std::vector<BattleDeploymentInsertionAnchor>& insertion_anchors,
    bool anchor_resolved) {
    std::wostringstream key;
    key << L"S-battle-scope|" << section_key.size() << L':' << section_key
        << L'|' << (anchor_resolved ? L"anchor:" : L"no-anchor:");
    for (const LogicalNodeKey& source_key : deployment_keys) {
        key << source_key.size() << L':' << source_key << L';';
    }
    if (anchor_resolved) {
        key << L"|parent:" << insert_parent_key.size() << L':'
            << insert_parent_key << L"|before:" << insert_before_key.size()
            << L':' << insert_before_key;
    }
    for (const auto& anchor : insertion_anchors) {
        key << L"|type:" << anchor.instruction_id << L':'
            << (anchor.resolved ? L"resolved:" : L"unresolved:");
        if (anchor.resolved) {
            key << anchor.parent_key.size() << L':' << anchor.parent_key
                << L'|' << anchor.before_key.size() << L':'
                << anchor.before_key;
        } else {
            key << anchor.diagnostic.size() << L':' << anchor.diagnostic;
        }
    }
    return key.str();
}

struct AnchorCandidate {
    std::uint16_t target_instruction_id{};
    std::uint16_t instruction_id{};
    LogicalNodeKey parent_key;
    LogicalNodeKey before_key;
    std::size_t depth{};
    std::uintptr_t parent_handle{};
    std::uintptr_t before_handle{};
};

void collect_anchor_candidates(
    const RawReplayNode& node, std::size_t depth,
    const SectionExecutionModel& model, const ReplayOptions& options,
    std::vector<AnchorCandidate>& candidates) {
    if (has_final_terminator(node)) {
        const RawReplayNode& terminal = node.children.back();
        if (reaches_key(model, terminal.logical_key, options)) {
            candidates.push_back({0, node.instruction_id, node.logical_key,
                                  terminal.logical_key, depth,
                                  node.runtime_handle,
                                  terminal.runtime_handle});
        }
    }
    for (const RawReplayNode& child : node.children) {
        collect_anchor_candidates(child, depth + 1, model, options,
                                  candidates);
    }
}

}  // namespace

BattleConditionTreeBuild BuildBattleConditionTree(
    const BattlePreviewCapture& capture,
    const ccz::preview::InstructionCatalog& catalog,
    ReplayOptions& options) {
    BattleConditionTreeBuild result;
    if (!capture.ok || capture.mode.mode != ccz::preview::PreviewMode::Battle ||
        capture.input.scope_key.empty() ||
        capture.snapshot.section_root.runtime_handle == 0) {
        result.diagnostic = capture.diagnostic.empty()
            ? L"S 条件树无法建立：战场 Section 捕获不完整"
            : capture.diagnostic;
        return result;
    }

    const RawReplayNode* raw_section = find_section_node(
        capture.input.nodes, capture.snapshot.section_root.runtime_handle);
    if (raw_section == nullptr ||
        !key_in_scope(raw_section->logical_key, capture.input.scope_key)) {
        result.diagnostic = L"S 条件树无法建立：找不到固定 Section 的原生树节点";
        return result;
    }

    ccz::preview::SectionInput input;
    input.section_key = capture.input.scope_key;
    input.nodes = raw_section->children;
    ccz::preview::SectionExecutionModel model =
        ccz::preview::build_section_execution_model(input, catalog, options);
    if (!model.boundary_complete) {
        result.diagnostic = L"S 条件树无法建立：固定 Section 缺少顶层 0x00 边界";
        return result;
    }
    if (ccz::preview::initialize_default_choice_selections(model, options)) {
        model = ccz::preview::build_section_execution_model(
            input, catalog, options);
    }
    result.model = std::make_shared<SectionExecutionModel>(std::move(model));

    std::vector<NodeLocation> locations;
    std::vector<const RawReplayNode*> ancestors;
    for (const RawReplayNode& child : raw_section->children) {
        collect_locations(child, ancestors, locations);
    }

    std::set<LogicalNodeKey> deployment_keys;
    std::vector<AnchorCandidate> deployment_anchors;
    for (const NodeLocation& location : locations) {
        const RawReplayNode& node = *location.node;
        if (!is_deployment(node.instruction_id) ||
            !key_in_scope(node.logical_key, capture.input.scope_key)) {
            continue;
        }
        if (!deployment_keys.insert(node.logical_key).second) {
            result.scope.writeback_scope_resolved = false;
            result.scope.diagnostic =
                L"S 条件树无法唯一映射重复的战场出场指令键";
            continue;
        }
        if (!reaches_key(*result.model, node.logical_key, options)) continue;

        result.scope.reachable_deployment_keys.push_back(node.logical_key);
        result.scope.reachable_deployment_handles.push_back(
            node.runtime_handle);
        for (auto ancestor = location.ancestors.rbegin();
             ancestor != location.ancestors.rend(); ++ancestor) {
            if (has_final_terminator(**ancestor)) {
                const RawReplayNode& terminal = (**ancestor).children.back();
                deployment_anchors.push_back({node.instruction_id,
                    (**ancestor).instruction_id, (**ancestor).logical_key,
                    terminal.logical_key, location.ancestors.size(),
                    (**ancestor).runtime_handle, terminal.runtime_handle});
                break;
            }
        }
    }

    std::sort(result.scope.reachable_deployment_keys.begin(),
              result.scope.reachable_deployment_keys.end());
    result.scope.reachable_deployment_keys.erase(
        std::unique(result.scope.reachable_deployment_keys.begin(),
                    result.scope.reachable_deployment_keys.end()),
        result.scope.reachable_deployment_keys.end());

    const bool unresolved = has_unresolved_deployment_path(
        result.model->entry_gates, options) ||
        has_unresolved_deployment_path(result.model->body, options);
    result.scope.writeback_scope_resolved =
        result.scope.diagnostic.empty() && !unresolved;
    if (unresolved && result.scope.diagnostic.empty()) {
        result.scope.diagnostic =
            L"S 战场存在包含出场指令但无法模拟的条件分支，写回预检已关闭";
    }

    if (!deployment_anchors.empty()) {
        const AnchorCandidate& common_candidate = deployment_anchors.front();
        const bool common_anchor = std::all_of(
            deployment_anchors.begin(), deployment_anchors.end(),
            [&common_candidate](const AnchorCandidate& candidate) {
                return candidate.parent_key == common_candidate.parent_key &&
                       candidate.before_key == common_candidate.before_key;
            });
        for (const std::uint16_t instruction_id : {
                 std::uint16_t(0x4b), std::uint16_t(0x46),
                 std::uint16_t(0x47)}) {
            const auto first = std::find_if(deployment_anchors.begin(),
                deployment_anchors.end(), [instruction_id](
                    const AnchorCandidate& candidate) {
                    return candidate.target_instruction_id == instruction_id;
            });
            BattleDeploymentInsertionAnchor anchor;
            anchor.instruction_id = instruction_id;
            if (first == deployment_anchors.end()) {
                anchor.resolved = common_anchor;
                if (common_anchor) {
                    anchor.parent_key = common_candidate.parent_key;
                    anchor.before_key = common_candidate.before_key;
                    anchor.parent_handle = common_candidate.parent_handle;
                    anchor.before_handle = common_candidate.before_handle;
                } else {
                    anchor.diagnostic = std::wstring(L"当前可达出场指令没有共同的子事件锚点，无法安全插入缺少的 ") +
                        (instruction_id == 0x4b ? L"0x4B" :
                         instruction_id == 0x46 ? L"0x46" : L"0x47") +
                        L" 类型指令";
                }
            } else {
                const bool unique = std::all_of(deployment_anchors.begin(),
                    deployment_anchors.end(),
                    [&](const AnchorCandidate& candidate) {
                        return candidate.target_instruction_id != instruction_id ||
                            (candidate.parent_key == first->parent_key &&
                             candidate.before_key == first->before_key);
                    });
                anchor.resolved = unique;
                if (unique) {
                    anchor.parent_key = first->parent_key;
                    anchor.before_key = first->before_key;
                    anchor.parent_handle = first->parent_handle;
                    anchor.before_handle = first->before_handle;
                } else {
                    anchor.diagnostic = std::wstring(L"当前可达分支中的 ") +
                        (instruction_id == 0x4b ? L"0x4B" :
                         instruction_id == 0x46 ? L"0x46" : L"0x47") +
                        L" 出场指令属于多个子事件，无法唯一选择新增目标";
                }
            }
            result.scope.insertion_anchors.push_back(std::move(anchor));
        }

        const bool all_resolved = !result.scope.insertion_anchors.empty() &&
            std::all_of(result.scope.insertion_anchors.begin(),
                result.scope.insertion_anchors.end(),
                [](const BattleDeploymentInsertionAnchor& anchor) {
                    return anchor.resolved;
                });
        if (all_resolved) {
            const auto& first = result.scope.insertion_anchors.front();
            const bool one_shared_anchor = std::all_of(
                result.scope.insertion_anchors.begin(),
                result.scope.insertion_anchors.end(),
                [&first](const BattleDeploymentInsertionAnchor& anchor) {
                    return anchor.parent_key == first.parent_key &&
                           anchor.before_key == first.before_key;
                });
            if (one_shared_anchor) {
                result.scope.insert_parent_key = first.parent_key;
                result.scope.insert_before_key = first.before_key;
                result.scope.insertion_anchor_resolved = true;
            }
        }
    } else {
        const bool has_controls =
            ccz::preview::condition_tree_model_has_controls(*result.model);
        std::vector<AnchorCandidate> candidates;
        collect_anchor_candidates(*raw_section, 0, *result.model, options,
                                  candidates);
        if (has_controls) {
            // The Section's ordinary event is not a safe fallback when a
            // selected condition branch has no child event of its own.
                candidates.erase(std::remove_if(candidates.begin(),
                candidates.end(), [](const AnchorCandidate& candidate) {
                    return candidate.instruction_id == 0;
                }), candidates.end());
        }
        if (!candidates.empty()) {
            const std::size_t deepest = std::max_element(
                candidates.begin(), candidates.end(),
                [](const AnchorCandidate& left,
                   const AnchorCandidate& right) {
                    return left.depth < right.depth;
                })->depth;
            candidates.erase(std::remove_if(candidates.begin(),
                candidates.end(), [deepest](const AnchorCandidate& item) {
                    return item.depth != deepest;
                }), candidates.end());
            const bool unique = std::all_of(candidates.begin(),
                candidates.end(), [&](const AnchorCandidate& candidate) {
                    return candidate.parent_key ==
                               candidates.front().parent_key &&
                           candidate.before_key ==
                               candidates.front().before_key;
                });
            if (unique) {
                result.scope.insert_parent_key =
                    candidates.front().parent_key;
                result.scope.insert_before_key =
                    candidates.front().before_key;
                result.scope.insertion_anchors.clear();
                result.scope.insertion_anchor_resolved = true;
            }
        }
        if (!result.scope.insertion_anchor_resolved &&
            result.scope.diagnostic.empty()) {
            result.scope.diagnostic = !candidates.empty()
                ? L"当前可达分支存在多个同级子事件，新增单位没有唯一写入目标"
                : has_controls
                    ? L"当前条件结果没有可写入的子事件；临时布置会保留，但不能写回新增指令"
                    : L"当前 Section 没有可唯一定位的 0x00 子事件终止锚点";
        }
    }

    if (!result.scope.insertion_anchor_resolved &&
        result.scope.insertion_anchors.empty() &&
        !result.scope.reachable_deployment_keys.empty() &&
        result.scope.diagnostic.empty()) {
        result.scope.diagnostic =
            L"当前可达出场指令没有唯一的末尾 0x00 子事件锚点";
    }

    if (result.scope.insertion_anchor_resolved) {
        if (!key_in_scope(result.scope.insert_parent_key,
                          capture.input.scope_key) ||
            !key_in_scope(result.scope.insert_before_key,
                          capture.input.scope_key)) {
            result.scope.insert_parent_key.clear();
            result.scope.insert_before_key.clear();
            result.scope.insertion_anchor_resolved = false;
            result.scope.diagnostic =
                L"S 条件树插入锚点不在固定 Section 内，新增指令写回已关闭";
        }
    }
    for (BattleDeploymentInsertionAnchor& anchor :
         result.scope.insertion_anchors) {
        if (!anchor.resolved) continue;
        if (!key_in_scope(anchor.parent_key, capture.input.scope_key) ||
            !key_in_scope(anchor.before_key, capture.input.scope_key)) {
            anchor.parent_key.clear();
            anchor.before_key.clear();
            anchor.resolved = false;
            anchor.diagnostic =
                std::wstring(L"S 条件树的 ") +
                (anchor.instruction_id == 0x4b ? L"0x4B" :
                 anchor.instruction_id == 0x46 ? L"0x46" : L"0x47") +
                L" 插入锚点不在固定 Section 内";
        }
    }

    result.scope.workspace_key = make_workspace_key(
        capture.input.scope_key,
        result.scope.reachable_deployment_keys,
        result.scope.insert_parent_key, result.scope.insert_before_key,
        result.scope.insertion_anchors,
        result.scope.insertion_anchor_resolved);
    result.ok = true;
    return result;
}

}  // namespace ccz::editor_preview
