#include "test_framework.h"

#include <ccz/preview/condition_tree.h>
#include <ccz/preview/condition_tree_state.h>
#include <ccz/preview/execution_model.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace ccz::preview;

namespace {

RawReplayNode raw_node(
    const wchar_t* key, std::uint16_t instruction_id, const wchar_t* text,
    std::vector<RawReplayNode> children = {},
    std::vector<std::int32_t> int_data = {}) {
    RawReplayNode node;
    node.logical_key = key;
    node.instruction_id = instruction_id;
    node.command.instruction_id = instruction_id;
    node.command.int_data = std::move(int_data);
    node.command.text = text;
    node.source_text = text;
    node.children = std::move(children);
    return node;
}

InstructionCatalog fixture_catalog() {
    InstructionCatalog catalog;
    catalog.valid = true;
    catalog.names.emplace(static_cast<std::uint16_t>(0x00), L"事件结束");
    catalog.names.emplace(static_cast<std::uint16_t>(0x02), L"内部信息");
    catalog.names.emplace(static_cast<std::uint16_t>(0x03), L"else");
    catalog.names.emplace(static_cast<std::uint16_t>(0x05), L"变量测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x12), L"选择框");
    catalog.names.emplace(static_cast<std::uint16_t>(0x13), L"case");
    catalog.names.emplace(static_cast<std::uint16_t>(20), L"对话");
    catalog.names.emplace(static_cast<std::uint16_t>(0x36), L"武将状态测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x79), L"整型变量测试");
    return catalog;
}

ExecutionNode execution_node(ExecutionCategory category,
                             std::uint16_t instruction_id,
                             const wchar_t* key) {
    ExecutionNode node;
    node.category = category;
    node.instruction_id = instruction_id;
    node.logical_key = key;
    return node;
}

SectionExecutionModel make_model(std::vector<RawReplayNode> nodes) {
    SectionInput input;
    input.section_key = L"fixture|section:0";
    input.nodes = std::move(nodes);
    return build_section_execution_model(input, fixture_catalog(), {});
}

}  // namespace

CCZ_TEST(condition_tree_projection_consumes_nested_execution_model) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"outer", 0x05, L"5:外层条件",
                 {raw_node(L"inner", 0x36, L"36:内层条件",
                           {raw_node(L"inner-body", 20, L"内层正文")})}),
        raw_node(L"else", 0x03, L"3:else",
                 {raw_node(L"false-body", 20, L"假正文")}),
    });

    const ConditionTreeModel projected =
        build_condition_tree_from_execution_model(model);

    CCZ_REQUIRE_EQ(projected.roots.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(projected.roots[0].instruction_id,
                   static_cast<std::uint16_t>(0x05));
    CCZ_REQUIRE_EQ(projected.roots[0].logical_key, LogicalNodeKey(L"outer"));
    CCZ_REQUIRE_EQ(projected.roots[0].predicate_state,
                   PredicateState::DefaultTrue);
    CCZ_REQUIRE_EQ(projected.roots[0].true_children.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(projected.roots[0].true_children[0].instruction_id,
                   static_cast<std::uint16_t>(0x36));
    CCZ_REQUIRE_EQ(projected.roots[0].false_children.size(), std::size_t(0));
}

CCZ_TEST(condition_tree_projection_traverses_predicates_inside_each_case) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"choice", 0x12, L"12:选择框"),
        raw_node(L"case-a", 0x13, L"13:甲",
                 {raw_node(L"case-a-condition", 0x05, L"5:甲条件",
                           {raw_node(L"case-a-body", 20, L"甲正文")})}),
        raw_node(L"case-b", 0x13, L"13:乙",
                 {raw_node(L"case-b-condition", 0x36, L"36:乙条件",
                           {raw_node(L"case-b-body", 20, L"乙正文")})}),
    });

    const ConditionTreeModel projected =
        build_condition_tree_from_execution_model(model);

    CCZ_REQUIRE_EQ(projected.roots.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(projected.roots[0].instruction_id,
                   static_cast<std::uint16_t>(0x05));
    CCZ_REQUIRE_EQ(projected.roots[1].instruction_id,
                   static_cast<std::uint16_t>(0x36));
}

CCZ_TEST(condition_tree_79_overrides_match_canonical_replay_result) {
    const SectionInput input = [] {
        SectionInput result;
        result.section_key = L"fixture|section:0";
        result.nodes = {
            raw_node(L"boundary", 0x00, L"0:边界"),
            raw_node(L"predicate", 0x79, L"79:旧文本",
                     {raw_node(L"true-body", 20, L"20:真")},
                     {4, 600, 2, 0, 10}),
            raw_node(L"else", 0x03, L"3:else",
                     {raw_node(L"false-body", 20, L"20:假")}),
        };
        return result;
    }();

    for (const auto state : {PredicateState::ExplicitTrue,
                             PredicateState::ExplicitFalse,
                             PredicateState::Unsupported}) {
        ReplayOptions options;
        options.predicate_states[L"predicate"] = state;
        const SectionExecutionModel model =
            build_section_execution_model(input, fixture_catalog(), options);
        const ExecutionNode& predicate = model.body[1];
        if (state == PredicateState::ExplicitTrue) {
            CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
                predicate, true, true, options));
            CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(
                predicate, true, false, options));
            CCZ_REQUIRE_EQ(condition_tree_visual_state(
                                predicate, true, false, options),
                           ConditionTreeVisualState::Normal);
        } else if (state == PredicateState::ExplicitFalse) {
            CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(
                predicate, true, true, options));
            CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
                predicate, true, false, options));
            CCZ_REQUIRE_EQ(condition_tree_visual_state(
                                predicate, true, false, options),
                           ConditionTreeVisualState::Normal);
        } else {
            CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(
                predicate, true, true, options));
            CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(
                predicate, true, false, options));
            CCZ_REQUIRE_EQ(condition_tree_visual_state(
                                predicate, true, false, options),
                           ConditionTreeVisualState::Diagnostic);
        }
    }
}

CCZ_TEST(condition_tree_79_labels_and_titles_use_canonical_comparators) {
    const SectionExecutionModel model = make_model({
        raw_node(L"boundary", 0x00, L"0:边界"),
        raw_node(L"less-equal", 0x79, L"79:Var600 < 10",
                 {raw_node(L"less-equal-body", 20, L"20:正文")},
                 {4, 600, 2, 0, 10}),
        raw_node(L"not-equal", 0x79, L"79:Var600 == 10",
                 {raw_node(L"not-equal-body", 20, L"20:正文")},
                 {4, 600, 3, 0, 10}),
    });
    const ConditionTreeModel projected =
        build_condition_tree_from_execution_model(model);

    CCZ_REQUIRE_EQ(projected.roots[0].label,
                   std::wstring(L"Var600 <= 10"));
    CCZ_REQUIRE_EQ(projected.roots[1].label,
                   std::wstring(L"Var600 ≠ 10"));
    const std::wstring less_equal_title =
        condition_tree_node_title(model.body[1]);
    const std::wstring not_equal_title = condition_tree_node_title(model.body[2]);
    CCZ_REQUIRE(less_equal_title.find(L"Var600 <= 10") !=
                std::wstring::npos);
    CCZ_REQUIRE(not_equal_title.find(L"Var600 ≠ 10") !=
                std::wstring::npos);
    CCZ_REQUIRE(less_equal_title.find(L"Var600 < 10") ==
                std::wstring::npos);
    CCZ_REQUIRE(not_equal_title.find(L"Var600 == 10") ==
                std::wstring::npos);
}

CCZ_TEST(condition_tree_actions_and_expansion_use_typed_state) {
    ExecutionNode predicate;
    predicate.logical_key = L"predicate";
    predicate.category = ExecutionCategory::Predicate;
    predicate.predicate_kind = PredicateKind::Boolean;
    predicate.true_children.push_back(ExecutionNode{});
    const ConditionTreeActionState predicate_actions =
        condition_tree_action_state(&predicate);
    CCZ_REQUIRE(predicate_actions.can_set_true);
    CCZ_REQUIRE(predicate_actions.can_set_false);
    CCZ_REQUIRE(predicate_actions.can_restore_predicate);

    ExecutionNode case_node;
    case_node.logical_key = L"choice|case:1";
    case_node.category = ExecutionCategory::Case;
    const ConditionTreeActionState case_actions =
        condition_tree_action_state(&case_node);
    CCZ_REQUIRE(case_actions.can_select_case);
    CCZ_REQUIRE(case_actions.can_restore_choice_auto);

    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
        predicate, true, true));
    CCZ_REQUIRE(!condition_tree_predicate_branch_reachable(
        predicate, true, false));

    ConditionTreeExpansionMap remembered;
    remembered[L"stable-key"] = false;
    CCZ_REQUIRE(!condition_tree_should_expand(
        remembered, L"stable-key", false, false, true));
    CCZ_REQUIRE(condition_tree_should_expand(
        remembered, L"new-key", false, false, true));
}

CCZ_TEST(condition_tree_activation_targets_waiting_integer_predicate) {
    ExecutionNode predicate = execution_node(
        ExecutionCategory::Predicate, 0x79, L"section|integer:1");
    predicate.predicate_kind = PredicateKind::Integer;
    predicate.integer_expression.decoded = true;
    predicate.integer_expression.left =
        {IntegerOperandKind::IntegerVariable, 3000};
    predicate.integer_expression.comparator =
        IntegerComparisonOperator::GreaterEqual;
    predicate.integer_expression.right =
        {IntegerOperandKind::Constant, 100};
    predicate.integer_evaluation = IntegerEvaluationState::True;
    const LogicalNodeKey target = L"section|integer:1|false|dialogue";
    predicate.false_children.push_back(
        execution_node(ExecutionCategory::Ordinary, 0x14, target.c_str()));

    SectionExecutionModel model;
    model.body.push_back(std::move(predicate));
    ReplayOptions options;
    options.simulated_variables.integer_variables[3000] = 100;

    CCZ_REQUIRE(condition_tree_model_can_activate_target(model, target,
                                                         options));
    CCZ_REQUIRE(activate_condition_tree_target(options, model, target));
    CCZ_REQUIRE_EQ(options.simulated_variables.integer_variables.at(3000),
                   99);
}

CCZ_TEST(condition_tree_activation_applies_nested_choice_and_boolean_steps) {
    const LogicalNodeKey choice_key = L"section|choice:1";
    const LogicalNodeKey case_key = L"section|choice:1|case:1";
    const LogicalNodeKey predicate_key =
        L"section|choice:1|case:1|predicate:1";
    const LogicalNodeKey target =
        L"section|choice:1|case:1|predicate:1|false|dialogue";

    ExecutionNode choice =
        execution_node(ExecutionCategory::Choice, 0x12, choice_key.c_str());
    choice.choice_state = ChoiceState::Unselected;
    ExecutionNode case_node =
        execution_node(ExecutionCategory::Case, 0x13, case_key.c_str());
    case_node.choice_state = ChoiceState::Unselected;
    ExecutionNode predicate = execution_node(
        ExecutionCategory::Predicate, 0x05, predicate_key.c_str());
    predicate.predicate_kind = PredicateKind::Boolean;
    predicate.boolean_expression.decoded = true;
    predicate.boolean_expression.false_variables.push_back(600);
    predicate.boolean_evaluation = BooleanEvaluationState::True;
    predicate.false_children.push_back(
        execution_node(ExecutionCategory::Ordinary, 0x14, target.c_str()));
    case_node.ordinary_children.push_back(std::move(predicate));
    choice.cases.push_back(std::move(case_node));

    SectionExecutionModel model;
    model.body.push_back(std::move(choice));
    ReplayOptions options;

    CCZ_REQUIRE(condition_tree_model_can_activate_target(model, target,
                                                         options));
    CCZ_REQUIRE(activate_condition_tree_target(options, model, target));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(choice_key), case_key);
    CCZ_REQUIRE(options.simulated_variables.boolean_variables.at(600));
}

CCZ_TEST(condition_tree_activation_rejects_diagnostic_or_blocked_target) {
    const LogicalNodeKey target = L"section|diagnostic|body";
    ExecutionNode diagnostic = execution_node(
        ExecutionCategory::Diagnostic, 0x7f, L"section|diagnostic");
    diagnostic.diagnostic = L"结构边界错误";
    diagnostic.ordinary_children.push_back(
        execution_node(ExecutionCategory::Ordinary, 0x14, target.c_str()));

    SectionExecutionModel model;
    model.body.push_back(std::move(diagnostic));
    ReplayOptions options;
    CCZ_REQUIRE(!condition_tree_model_can_activate_target(model, target,
                                                          options));
    CCZ_REQUIRE(!activate_condition_tree_target(options, model, target));
}
