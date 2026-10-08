#include "../../cczPreviewCoreTests/tests/test_framework.h"
#include <ccz/preview/native_replay_plan.h>
#include <cassert>
#include <iostream>
using namespace ccz::preview;
ExecutionNode command(const wchar_t* key, unsigned short id, std::vector<std::int32_t> fields={}) {
    ExecutionNode n; n.logical_key=key; n.command.instruction_id=id; n.command.int_data=std::move(fields); return n;
}
SectionExecutionModel section(std::vector<ExecutionNode> nodes) {
    SectionExecutionModel m; m.boundary_complete=true; m.body=std::move(nodes); return m;
}
CCZ_TEST(battle_section_environment_tests_regression) {
    ReplayOptions options; SimulationEnvironment terminal; DialogueProfile profile;
    const auto a=section({command(L"set",0x0b,{7,1}),command(L"integer",0x77,{2,9,2,0,12})});
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(a,profile,options,&terminal).complete);
    CCZ_REQUIRE(terminal.boolean_variables.at(7)); CCZ_REQUIRE(terminal.integer_variables.at(9)==12);
    auto predicate=command(L"check",0x05);
    predicate.category=ExecutionCategory::Predicate; predicate.predicate_kind=PredicateKind::Boolean;
    predicate.boolean_expression.decoded=true; predicate.boolean_expression.true_variables={7};
    predicate.true_children={command(L"move",0x32)};
    const auto b=section({predicate});
    options.simulated_variables=terminal;
    auto plan=build_native_battle_map_plan_to_end(b,profile,options,&terminal);
    CCZ_REQUIRE(plan.complete && plan.items.size()==1 && plan.items.front().logical_key==L"move");
    // Old null-output callers still use their original input, with no writes.
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(a,profile,ReplayOptions{}).complete);
    auto reset=section({command(L"reset",0x0a),command(L"map",0x58)});
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(reset,profile,options,&terminal).complete);
    options.simulated_variables=terminal;
    CCZ_REQUIRE(!build_native_battle_map_plan_to_end(b,profile,options,&terminal).complete);
    auto recover=section({command(L"reset",0x0a),command(L"set",0x0b,{7,1}),predicate});
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(recover,profile,options,&terminal).complete);
    auto unknown=section({command(L"property",0x78,{9,0,1,0}),command(L"map",0x58)});
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(unknown,profile,options,&terminal).complete);
    CCZ_REQUIRE(terminal.unknown_integer_variables.count(9));
    options.simulated_variables=terminal;
    auto integer=command(L"int-check",0x79);
    integer.category=ExecutionCategory::Predicate; integer.predicate_kind=PredicateKind::Integer;
    integer.integer_expression.decoded=true;
    integer.integer_expression.left.kind=IntegerOperandKind::IntegerVariable;
    integer.integer_expression.left.raw_value=9;
    integer.integer_expression.right.kind=IntegerOperandKind::Constant;
    integer.integer_expression.right.raw_value=12;
    CCZ_REQUIRE(!build_native_battle_map_plan_to_end(section({integer}),profile,options,&terminal).complete);
    std::cout << "BATTLE_SECTION_ENVIRONMENT_PASS\n";
}
CCZ_TEST(battle_unresolved_own_status_condition_requires_explicit_choice) {
    auto status=command(L"own-status",0x4d,{0,0,0,0,0,0,0,6,0,2,-1,0,0});
    auto test=command(L"own-test",0x36,{0,0,0,2});test.instruction_id=0x36;test.category=ExecutionCategory::Predicate;test.predicate_kind=PredicateKind::Boolean;
    test.true_children={command(L"fire",0x58)};
    const auto model=section({status,test});ReplayOptions options;options.simulated_variables.battle_unit_identity_available=true;
    options.simulated_variables.known_battle_unit_data_ids={47};SimulationEnvironment terminal;
    auto plan=build_native_battle_map_plan_to_end(model,{},options,&terminal);
    CCZ_REQUIRE(!plan.complete);CCZ_REQUIRE_EQ(plan.unresolved_status_predicates,std::vector<LogicalNodeKey>{L"own-test"});
    options.predicate_states[L"own-test"]=PredicateState::ExplicitTrue;
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(model,{},options,&terminal).complete);
    options.predicate_states[L"own-test"]=PredicateState::ExplicitFalse;
    plan=build_native_battle_map_plan_to_end(model,{},options,&terminal);CCZ_REQUIRE(plan.complete);CCZ_REQUIRE_EQ(plan.items.size(),std::size_t(1));
    options.predicate_states.clear();options.simulated_variables.known_battle_unit_data_ids.insert(0);
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(model,{},options,&terminal).complete);
    // R replay has no battle identity environment and retains its existing manual/default behavior.
    CCZ_REQUIRE(build_native_replay_plan_to_end(model,{},options).complete);
}
CCZ_TEST(battle_unresolved_status_survives_predecessor_environment) {
    ReplayOptions options;options.simulated_variables.battle_unit_identity_available=true;SimulationEnvironment terminal;
    CCZ_REQUIRE(build_native_battle_map_plan_to_end(section({command(L"status",0x4d,{0,0,0,0,0,0,0,6,0,2,-1,0,0})}),{},options,&terminal).complete);
    options.simulated_variables=terminal;auto test=command(L"next-test",0x36,{0});test.instruction_id=0x36;test.category=ExecutionCategory::Predicate;test.predicate_kind=PredicateKind::Boolean;
    auto plan=build_native_battle_map_plan_to_end(section({test}),{},options,&terminal);
    CCZ_REQUIRE(!plan.complete);CCZ_REQUIRE_EQ(plan.unresolved_status_predicates.size(),std::size_t(1));
}
#include <ccz/preview/condition_tree_state.h>
CCZ_TEST(battle_unknown_status_condition_remains_manually_controllable) {
    auto node=command(L"unknown-state",0x36,{0});node.category=ExecutionCategory::Predicate;node.predicate_kind=PredicateKind::Boolean;node.true_children={command(L"fire",0x58)};
    ReplayOptions options;options.unresolved_status_conditions.insert(node.logical_key);
    auto actions=condition_tree_action_state(&node,options);
    CCZ_REQUIRE(actions.can_set_true&&actions.can_set_false);
    CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(node,true,true,options));
    CCZ_REQUIRE(condition_tree_node_title(node,options).find(L"状态未知")!=std::wstring::npos);
    options.predicate_states[node.logical_key]=PredicateState::ExplicitTrue;
    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(node,true,true,options));
}
