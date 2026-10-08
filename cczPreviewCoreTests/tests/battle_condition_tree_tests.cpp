#include "test_framework.h"

#include <ccz/preview/condition_tree_state.h>

#include "../../cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.h"

#include <utility>
#include <vector>

namespace {

using namespace ccz::editor_preview;
using namespace ccz::preview;

RawReplayNode node(std::uint16_t id, std::wstring key,
                   std::vector<RawReplayNode> children = {},
                   std::vector<std::int32_t> values = {}) {
    RawReplayNode result;
    result.logical_key = std::move(key);
    result.instruction_id = id;
    result.command.instruction_id = id;
    result.command.int_data = std::move(values);
    result.children = std::move(children);
    return result;
}

InstructionCatalog catalog() {
    InstructionCatalog result;
    result.valid = true;
    for (const std::uint16_t id : {std::uint16_t(0), std::uint16_t(2),
             std::uint16_t(0x05),
             std::uint16_t(3), std::uint16_t(0x12), std::uint16_t(0x13),
             std::uint16_t(0x4b),
             std::uint16_t(0x36), std::uint16_t(0x46), std::uint16_t(0x47),
             std::uint16_t(75)}) {
        result.names.emplace(id, L"test instruction");
    }
    return result;
}

BattlePreviewCapture capture(std::vector<RawReplayNode> section_children) {
    BattlePreviewCapture result;
    result.ok = true;
    result.mode.mode = PreviewMode::Battle;
    result.input.scope_key = L"S_12|section:0";
    result.input.document_path = L"S_12.eex";
    RawReplayNode section;
    section.instruction_id = 0xffff;
    section.runtime_handle = 42;
    section.logical_key = result.input.scope_key + L"|instruction:65535";
    section.children = std::move(section_children);
    result.input.nodes.push_back(section);
    result.snapshot.section_root.runtime_handle = 42;
    result.snapshot.document_path = result.input.document_path;
    return result;
}

const std::wstring kRootEvent = L"S_12|section:0|node:0|instruction:0";
const std::wstring kPredicate = kRootEvent + L"|node:0|instruction:54";
const std::wstring kElse = kRootEvent + L"|node:1|instruction:3";
const std::wstring kTrueDeployment = kPredicate + L"|node:0|instruction:70";
const std::wstring kFalseDeployment = kElse + L"|node:0|instruction:71";

}  // namespace

CCZ_TEST(battle_condition_tree_resolves_true_and_else_sources_and_anchors) {
    auto value = capture({
        node(0, kRootEvent, {
            node(0x36, kPredicate, {
                node(0x46, kTrueDeployment, {}, std::vector<std::int32_t>(11, 0)),
                node(0, kPredicate + L"|node:1|instruction:0"),
            }),
            node(3, kElse, {
                node(0x47, kFalseDeployment, {}, std::vector<std::int32_t>(12, 0)),
                node(0, kElse + L"|node:1|instruction:0"),
            }),
            node(0, kRootEvent + L"|node:2|instruction:0"),
        }),
    });

    ReplayOptions options;
    const auto true_scope = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(true_scope.ok);
    CCZ_REQUIRE_EQ(true_scope.scope.reachable_deployment_keys.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(true_scope.scope.reachable_deployment_keys[0], kTrueDeployment);
    CCZ_REQUIRE(true_scope.scope.insertion_anchor_resolved);
    CCZ_REQUIRE_EQ(true_scope.scope.insert_parent_key, kPredicate);
    CCZ_REQUIRE_EQ(true_scope.scope.insert_before_key,
                   kPredicate + L"|node:1|instruction:0");

    options.predicate_states[kPredicate] = PredicateState::ExplicitFalse;
    const auto false_scope = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(false_scope.ok);
    CCZ_REQUIRE_EQ(false_scope.scope.reachable_deployment_keys.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(false_scope.scope.reachable_deployment_keys[0], kFalseDeployment);
    CCZ_REQUIRE_EQ(false_scope.scope.insert_parent_key, kElse);
    CCZ_REQUIRE_EQ(false_scope.scope.insert_before_key,
                   kElse + L"|node:1|instruction:0");
    CCZ_REQUIRE(false_scope.scope.workspace_key != true_scope.scope.workspace_key);
}

CCZ_TEST(battle_condition_tree_accepts_native_editor_node_path_keys) {
    const std::wstring section_key = L"S_10.eex|document|node:0/0";
    const std::wstring root_event = section_key + L"/0|instruction:0";
    const std::wstring deployment = root_event + L"/0|instruction:70";
    const std::wstring terminal = root_event + L"/1|instruction:0";
    auto value = capture({node(0, root_event, {
        node(0x46, deployment, {}, std::vector<std::int32_t>(11, 0)),
        node(0, terminal),
    })});
    value.input.scope_key = section_key;
    value.input.nodes.front().logical_key = section_key + L"|instruction:65535";

    ReplayOptions options;
    const auto resolved = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(resolved.ok);
    CCZ_REQUIRE_EQ(resolved.scope.reachable_deployment_keys,
                   std::vector<LogicalNodeKey>{deployment});
    CCZ_REQUIRE(resolved.scope.writeback_scope_resolved);
    CCZ_REQUIRE(resolved.scope.insertion_anchor_resolved);
    CCZ_REQUIRE_EQ(resolved.scope.insert_parent_key, root_event);
    CCZ_REQUIRE_EQ(resolved.scope.insert_before_key, terminal);
}

CCZ_TEST(battle_condition_tree_gives_all_deployment_kinds_a_shared_anchor) {
    const std::wstring deployment =
        kRootEvent + L"|node:0|instruction:70";
    const std::wstring terminal =
        kRootEvent + L"|node:1|instruction:0";
    auto value = capture({node(0, kRootEvent, {
        node(0x46, deployment, {}, std::vector<std::int32_t>(11, 0)),
        node(0, terminal),
    })});

    ReplayOptions options;
    const auto resolved = BuildBattleConditionTree(
        value, catalog(), options);
    CCZ_REQUIRE(resolved.ok);
    CCZ_REQUIRE(resolved.scope.insertion_anchor_resolved);
    CCZ_REQUIRE_EQ(resolved.scope.insertion_anchors.size(), std::size_t(3));
    for (const std::uint16_t instruction_id : {
             std::uint16_t(0x4b), std::uint16_t(0x46),
             std::uint16_t(0x47)}) {
        const auto anchor = std::find_if(
            resolved.scope.insertion_anchors.begin(),
            resolved.scope.insertion_anchors.end(),
            [instruction_id](const auto& candidate) {
                return candidate.instruction_id == instruction_id;
            });
        CCZ_REQUIRE(anchor != resolved.scope.insertion_anchors.end());
        CCZ_REQUIRE(anchor->resolved);
        CCZ_REQUIRE_EQ(anchor->parent_key, kRootEvent);
        CCZ_REQUIRE_EQ(anchor->before_key, terminal);
    }
}

CCZ_TEST(battle_condition_tree_resolves_sample_style_0x05_deployments) {
    const std::wstring boolean_predicate =
        kRootEvent + L"|node:0|instruction:5";
    const std::wstring true_deployment =
        boolean_predicate + L"|node:0|instruction:70";
    const std::wstring false_deployment =
        kElse + L"|node:0|instruction:71";
    std::vector<std::int32_t> boolean_fields(50, -1);
    // S_12's 0x05 stores its variable lists in two 25-slot regions. The
    // representative sample has no true-variable requirement and requires
    // Var1310 to be false for the predicate branch.
    boolean_fields[25] = 1310;
    auto value = capture({node(0, kRootEvent, {
        node(0x05, boolean_predicate, {
            node(0x46, true_deployment, {}, std::vector<std::int32_t>(11, 0)),
            node(0, boolean_predicate + L"|node:1|instruction:0"),
        }, std::move(boolean_fields)),
        node(3, kElse, {
            node(0x47, false_deployment, {}, std::vector<std::int32_t>(12, 0)),
            node(0, kElse + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:2|instruction:0"),
    })});

    ReplayOptions options;
    const auto true_scope = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(true_scope.ok);
    CCZ_REQUIRE(true_scope.scope.writeback_scope_resolved);
    CCZ_REQUIRE(true_scope.scope.insertion_anchor_resolved);
    CCZ_REQUIRE_EQ(true_scope.scope.reachable_deployment_keys,
                   std::vector<LogicalNodeKey>{true_deployment});

    options.predicate_states[boolean_predicate] =
        PredicateState::ExplicitFalse;
    const auto false_scope = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(false_scope.ok);
    CCZ_REQUIRE(false_scope.scope.writeback_scope_resolved);
    CCZ_REQUIRE_EQ(false_scope.scope.reachable_deployment_keys,
                   std::vector<LogicalNodeKey>{false_deployment});
}

CCZ_TEST(battle_condition_tree_keeps_selected_target_for_activation) {
    const std::wstring predicate_key =
        kRootEvent + L"|node:0|instruction:5";
    const std::wstring target_key =
        predicate_key + L"|node:0|instruction:71";
    std::vector<std::int32_t> boolean_fields(50, -1);
    boolean_fields[0] = 700;
    auto value = capture({node(0, kRootEvent, {
        node(0x05, predicate_key, {
            node(0x47, target_key, {}, std::vector<std::int32_t>(12, 0)),
            node(0, predicate_key + L"|node:1|instruction:0"),
        }, std::move(boolean_fields)),
        node(3, kElse, {
            node(0x46, kFalseDeployment, {}, std::vector<std::int32_t>(11, 0)),
            node(0, kElse + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:2|instruction:0"),
    })});
    value.selected_target_key = target_key;

    ReplayOptions options;
    options.simulated_variables.boolean_variables[700] = false;
    const auto built = BuildBattleConditionTree(value, catalog(), options);

    CCZ_REQUIRE(built.ok);
    CCZ_REQUIRE(built.model != nullptr);
    CCZ_REQUIRE_EQ(built.model->selected_target_key, target_key);
    CCZ_REQUIRE(!execution_nodes_reach_target(
        built.model->body, target_key, options));
    CCZ_REQUIRE(condition_tree_model_can_activate_target(
        *built.model, target_key, options));
}

CCZ_TEST(battle_condition_tree_can_activate_nested_0x4b_subevent_target) {
    const std::wstring predicate_key =
        kRootEvent + L"|node:0|instruction:5";
    const std::wstring container_key =
        predicate_key + L"|node:1|instruction:2";
    const std::wstring target_key =
        container_key + L"|node:0|instruction:75";
    std::vector<std::int32_t> boolean_fields(50, -1);
    boolean_fields[0] = 700;
    auto value = capture({node(0, kRootEvent, {
        node(0x05, predicate_key, {
            node(0x01, predicate_key + L"|node:0|instruction:1"),
            node(0x02, container_key, {
                node(0x4b, target_key, {}, std::vector<std::int32_t>(20, 0)),
                node(0, container_key + L"|node:1|instruction:0"),
            }),
            node(0, predicate_key + L"|node:2|instruction:0"),
        }, std::move(boolean_fields)),
        node(3, kElse, {
            node(0x46, kFalseDeployment, {}, std::vector<std::int32_t>(11, 0)),
            node(0, kElse + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:2|instruction:0"),
    })});
    value.selected_target_key = target_key;

    ReplayOptions options;
    options.simulated_variables.boolean_variables[700] = false;
    const auto built = BuildBattleConditionTree(value, catalog(), options);

    CCZ_REQUIRE(built.ok);
    CCZ_REQUIRE(built.model != nullptr);
    CCZ_REQUIRE_EQ(built.model->selected_target_key, target_key);
    CCZ_REQUIRE(condition_tree_model_can_activate_target(
        *built.model, target_key, options));
}

CCZ_TEST(battle_condition_tree_does_not_merge_distinct_deployment_kind_anchors) {
    const std::wstring boolean_predicate =
        kRootEvent + L"|node:0|instruction:5";
    const std::wstring true_deployment =
        boolean_predicate + L"|node:0|instruction:70";
    const std::wstring else_event = kRootEvent + L"|node:1|instruction:3";
    const std::wstring wrapper = kRootEvent + L"|node:2|instruction:2";
    const std::wstring four_b_deployment =
        wrapper + L"|node:0|instruction:75";
    const std::wstring false_deployment =
        else_event + L"|node:0|instruction:71";
    std::vector<std::int32_t> boolean_fields(50, -1);
    boolean_fields[25] = 1310;
    auto value = capture({node(0, kRootEvent, {
        node(0x05, boolean_predicate, {
            node(0x46, true_deployment, {}, std::vector<std::int32_t>(11, 0)),
            node(0, boolean_predicate + L"|node:1|instruction:0"),
        }, std::move(boolean_fields)),
        node(3, else_event, {
            node(0x47, false_deployment, {}, std::vector<std::int32_t>(12, 0)),
            node(0, else_event + L"|node:1|instruction:0"),
        }),
        node(2, wrapper, {
            node(0x4b, four_b_deployment, {}, std::vector<std::int32_t>(20, 0)),
            node(0, wrapper + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:3|instruction:0"),
    })});

    ReplayOptions options;
    const auto resolved = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(resolved.ok);
    CCZ_REQUIRE(resolved.scope.writeback_scope_resolved);
    CCZ_REQUIRE_EQ(resolved.scope.reachable_deployment_keys.size(), std::size_t(2));
    // S_12 has reachable 0x46 and 0x4B rows in different child events. Their
    // distinct destinations are routable by instruction kind, not ambiguous.
    CCZ_REQUIRE(resolved.scope.diagnostic.empty());
    const auto four_b_anchor = std::find_if(
        resolved.scope.insertion_anchors.begin(),
        resolved.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x4b; });
    const auto friend_anchor = std::find_if(
        resolved.scope.insertion_anchors.begin(),
        resolved.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x46; });
    CCZ_REQUIRE(four_b_anchor != resolved.scope.insertion_anchors.end());
    CCZ_REQUIRE(four_b_anchor->resolved);
    CCZ_REQUIRE_EQ(four_b_anchor->parent_key, wrapper);
    CCZ_REQUIRE(friend_anchor != resolved.scope.insertion_anchors.end());
    CCZ_REQUIRE(friend_anchor->resolved);
    CCZ_REQUIRE_EQ(friend_anchor->parent_key, boolean_predicate);

    options.predicate_states[boolean_predicate] = PredicateState::ExplicitFalse;
    const auto false_scope = BuildBattleConditionTree(value, catalog(), options);
    CCZ_REQUIRE(false_scope.ok);
    const auto enemy_anchor = std::find_if(
        false_scope.scope.insertion_anchors.begin(),
        false_scope.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x47; });
    const auto false_four_b_anchor = std::find_if(
        false_scope.scope.insertion_anchors.begin(),
        false_scope.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x4b; });
    CCZ_REQUIRE(enemy_anchor != false_scope.scope.insertion_anchors.end());
    CCZ_REQUIRE(enemy_anchor->resolved);
    CCZ_REQUIRE_EQ(enemy_anchor->parent_key, else_event);
    CCZ_REQUIRE(false_four_b_anchor !=
                false_scope.scope.insertion_anchors.end());
    CCZ_REQUIRE_EQ(false_four_b_anchor->parent_key, wrapper);
}

CCZ_TEST(battle_condition_tree_choice_and_unconditional_scopes_are_canonical) {
    const std::wstring choice = kRootEvent + L"|node:0|instruction:18";
    const std::wstring case_a = choice + L"|node:0|instruction:19";
    const std::wstring case_b = choice + L"|node:1|instruction:19";
    const std::wstring deployment_a = case_a + L"|node:0|instruction:75";
    const std::wstring deployment_b = case_b + L"|node:0|instruction:75";
    auto choice_value = capture({node(0, kRootEvent, {
        node(0x12, choice, {
            node(0x13, case_a, {node(0x4b, deployment_a, {}, {303, 7, 8, 2, 1}),
                              node(0, case_a + L"|node:1|instruction:0")}),
            node(0x13, case_b, {node(0x4b, deployment_b, {}, {304, 9, 10, 3, 0}),
                              node(0, case_b + L"|node:1|instruction:0")}),
        }),
        node(0, kRootEvent + L"|node:1|instruction:0"),
    })});

    ReplayOptions options;
    options.selected_case_by_choice[choice] = case_b;
    const auto selected = BuildBattleConditionTree(choice_value, catalog(), options);
    CCZ_REQUIRE(selected.ok);
    CCZ_REQUIRE_EQ(selected.scope.reachable_deployment_keys.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(selected.scope.reachable_deployment_keys[0], deployment_b);
    CCZ_REQUIRE_EQ(selected.scope.insert_parent_key, case_b);

    const std::wstring wrapper = kRootEvent + L"|node:0|instruction:2";
    const std::wstring wrapped_deployment = wrapper + L"|node:0|instruction:70";
    auto unconditional_value = capture({node(0, kRootEvent, {
        node(2, wrapper, {node(0x46, wrapped_deployment, {},
                               std::vector<std::int32_t>(11, 0)),
                          node(0, wrapper + L"|node:1|instruction:0")}),
        node(0, kRootEvent + L"|node:1|instruction:0"),
    })});
    options = {};
    const auto unconditional = BuildBattleConditionTree(
        unconditional_value, catalog(), options);
    CCZ_REQUIRE(unconditional.ok);
    CCZ_REQUIRE(!condition_tree_model_has_controls(*unconditional.model));
    CCZ_REQUIRE_EQ(unconditional.scope.reachable_deployment_keys.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(unconditional.scope.insert_parent_key, wrapper);
}

CCZ_TEST(battle_condition_tree_unsupported_paths_and_same_type_ambiguous_anchors_fail_closed) {
    auto unsupported_value = capture({node(0, kRootEvent, {
        node(0x36, kPredicate, {
            node(0x46, kTrueDeployment, {}, std::vector<std::int32_t>(11, 0)),
            node(0, kPredicate + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:1|instruction:0"),
    })});
    ReplayOptions options;
    options.predicate_states[kPredicate] = PredicateState::Unsupported;
    const auto unsupported = BuildBattleConditionTree(
        unsupported_value, catalog(), options);
    CCZ_REQUIRE(unsupported.ok);
    CCZ_REQUIRE(unsupported.scope.reachable_deployment_keys.empty());
    CCZ_REQUIRE(!unsupported.scope.writeback_scope_resolved);
    CCZ_REQUIRE(!unsupported.scope.diagnostic.empty());

    const std::wstring branch_a = kPredicate + L"|node:0|instruction:2";
    const std::wstring branch_b = kPredicate + L"|node:1|instruction:2";
    auto ambiguous_value = capture({node(0, kRootEvent, {
        node(0x36, kPredicate, {
            node(2, branch_a, {
                node(0x46, branch_a + L"|node:0|instruction:70", {},
                     std::vector<std::int32_t>(11, 0)),
                node(0, branch_a + L"|node:1|instruction:0"),
            }),
            node(2, branch_b, {
                node(0x47, branch_b + L"|node:0|instruction:71", {},
                     std::vector<std::int32_t>(12, 0)),
                node(0, branch_b + L"|node:1|instruction:0"),
            }),
            node(0, kPredicate + L"|node:2|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:1|instruction:0"),
    })});
    options = {};
    const auto ambiguous = BuildBattleConditionTree(
        ambiguous_value, catalog(), options);
    CCZ_REQUIRE(ambiguous.ok);
    CCZ_REQUIRE_EQ(ambiguous.scope.reachable_deployment_keys.size(), std::size_t(2));
    const auto separate_friend_anchor = std::find_if(
        ambiguous.scope.insertion_anchors.begin(),
        ambiguous.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x46; });
    const auto separate_enemy_anchor = std::find_if(
        ambiguous.scope.insertion_anchors.begin(),
        ambiguous.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x47; });
    CCZ_REQUIRE(separate_friend_anchor != ambiguous.scope.insertion_anchors.end());
    CCZ_REQUIRE(separate_friend_anchor->resolved);
    CCZ_REQUIRE_EQ(separate_friend_anchor->parent_key, branch_a);
    CCZ_REQUIRE(separate_enemy_anchor != ambiguous.scope.insertion_anchors.end());
    CCZ_REQUIRE(separate_enemy_anchor->resolved);
    CCZ_REQUIRE_EQ(separate_enemy_anchor->parent_key, branch_b);

    const std::wstring same_kind_a = kRootEvent + L"|node:0|instruction:2";
    const std::wstring same_kind_b = kRootEvent + L"|node:1|instruction:2";
    auto same_kind_value = capture({node(0, kRootEvent, {
        node(2, same_kind_a, {
            node(0x46, same_kind_a + L"|node:0|instruction:70", {},
                 std::vector<std::int32_t>(11, 0)),
            node(0, same_kind_a + L"|node:1|instruction:0"),
        }),
        node(2, same_kind_b, {
            node(0x46, same_kind_b + L"|node:0|instruction:71", {},
                 std::vector<std::int32_t>(11, 0)),
            node(0, same_kind_b + L"|node:1|instruction:0"),
        }),
        node(0, kRootEvent + L"|node:2|instruction:0"),
    })});
    const auto same_kind = BuildBattleConditionTree(
        same_kind_value, catalog(), options);
    CCZ_REQUIRE(same_kind.ok);
    CCZ_REQUIRE(same_kind.scope.writeback_scope_resolved);
    CCZ_REQUIRE_EQ(same_kind.scope.reachable_deployment_keys.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(same_kind.scope.insertion_anchors.size(), std::size_t(3));
    const auto ambiguous_friend_anchor = std::find_if(
        same_kind.scope.insertion_anchors.begin(),
        same_kind.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x46; });
    CCZ_REQUIRE(ambiguous_friend_anchor !=
                same_kind.scope.insertion_anchors.end());
    CCZ_REQUIRE(!ambiguous_friend_anchor->resolved);
    CCZ_REQUIRE(!ambiguous_friend_anchor->diagnostic.empty());
    const auto unresolved_missing_kind = std::find_if(
        same_kind.scope.insertion_anchors.begin(),
        same_kind.scope.insertion_anchors.end(),
        [](const auto& anchor) { return anchor.instruction_id == 0x4b; });
    CCZ_REQUIRE(unresolved_missing_kind !=
                same_kind.scope.insertion_anchors.end());
    CCZ_REQUIRE(!unresolved_missing_kind->resolved);
    CCZ_REQUIRE(!unresolved_missing_kind->diagnostic.empty());
}
