#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>
#include "test_framework.h"

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace ccz::preview;

namespace {

RawReplayNode raw_node(const wchar_t* key, std::uint16_t instruction_id,
                       const wchar_t* text,
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

SectionInput section(std::vector<RawReplayNode> nodes,
                     const wchar_t* selected_target = L"") {
    SectionInput input;
    input.section_key = L"fixture|section:0";
    input.nodes = std::move(nodes);
    input.selected_target_key = selected_target;
    return input;
}

InstructionCatalog catalog_for(
    std::initializer_list<std::pair<int, const wchar_t*>> entries) {
    InstructionCatalog catalog;
    catalog.valid = true;
    for (const auto& entry : entries) {
        catalog.names.emplace(static_cast<std::uint16_t>(entry.first),
                              entry.second);
    }
    return catalog;
}

bool has_diagnostic(const std::vector<std::wstring>& diagnostics,
                    const wchar_t* fragment) {
    for (const std::wstring& diagnostic : diagnostics) {
        if (diagnostic.find(fragment) != std::wstring::npos) {
            return true;
        }
    }
    return false;
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

const ExecutionNode* find_node(const SectionExecutionModel& model,
                               const LogicalNodeKey& key) {
    if (const ExecutionNode* found = find_node(model.entry_gates, key)) {
        return found;
    }
    return find_node(model.body, key);
}

}  // namespace

CCZ_TEST(execution_model_classifies_recursive_r_nodes_without_marker_heuristic) {
    const RawReplayNode nested_choice = raw_node(
        L"nested-choice", 0x12, L"12:内层选择");
    const RawReplayNode nested_case_one = raw_node(
        L"nested-case-one", 0x13, L"13:内层甲",
        {raw_node(L"nested-predicate", 0x36, L"36:内层条件",
                  {raw_node(L"nested-predicate-body", 0x20, L"20:内层正文")})});
    const RawReplayNode nested_case_two =
        raw_node(L"nested-case-two", 0x13, L"13:内层乙");

    const RawReplayNode outer_choice = raw_node(
        L"choice", 0x12, L"12:外层选择");
    const RawReplayNode outer_case_one = raw_node(
        L"case-one", 0x13, L"13:外层甲",
        {nested_choice, nested_case_one, nested_case_two});
    const RawReplayNode outer_case_two =
        raw_node(L"case-two", 0x13, L"13:外层乙");

    const RawReplayNode predicate = raw_node(
        L"predicate", 0x05, L"5:无需前置标记的条件",
        {raw_node(L"true-body", 0x20, L"20:真正文")});
    const RawReplayNode else_node = raw_node(
        L"else", 0x03, L"3:else",
        {raw_node(L"false-body", 0x20, L"20:假正文")});

    SectionInput input = section(
        {raw_node(L"section-start", 0x00, L"0:Section开始"),
         raw_node(L"ordinary", 0x20, L"20:普通事件"),
         raw_node(L"container", 0x02, L"2:内部信息",
                  {raw_node(L"container-body", 0x20, L"20:容器正文")}),
         predicate,
         raw_node(L"marker", 0x01, L"1:结构标记"),
         else_node,
         raw_node(L"boolean-36", 0x36, L"36:布尔条件"),
         raw_node(L"boolean-37", 0x37, L"37:布尔条件"),
         raw_node(L"integer", 0x79, L"79:变量 >= 1"), outer_choice,
         outer_case_one, outer_case_two,
         raw_node(L"trigger", 0x07, L"7:出战测试"),
         raw_node(L"unknown", 0x7f, L"127:未知指令")});

    const InstructionCatalog catalog = catalog_for(
        {{0x00, L"Section开始"},
         {0x01, L"结构标记"},
         {0x02, L"内部信息"},
         {0x03, L"else"},
         {0x05, L"变量条件"},
         {0x07, L"出战测试"},
         {0x12, L"选择框"},
         {0x13, L"选择分支"},
         {0x20, L"普通事件"},
         {0x36, L"变量条件36"},
         {0x37, L"变量条件37"},
         {0x79, L"整型条件"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE(model.boundary_complete);
    CCZ_REQUIRE_EQ(model.entry_gates.size(), std::size_t(0));
    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(11));
    CCZ_REQUIRE_EQ(execution_node_count(model), std::size_t(22));

    CCZ_REQUIRE_EQ(model.body[0].category, ExecutionCategory::SectionGate);
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[2].category,
                   ExecutionCategory::UnconditionalContainer);
    CCZ_REQUIRE_EQ(model.body[2].ordinary_children.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(model.body[3].category, ExecutionCategory::Predicate);
    CCZ_REQUIRE_EQ(model.body[3].predicate_kind, PredicateKind::Boolean);
    CCZ_REQUIRE_EQ(model.body[3].false_children.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(model.body[3].false_children[0].category,
                   ExecutionCategory::ElseBranch);
    CCZ_REQUIRE_EQ(model.body[3].false_children[0]
                       .ordinary_children[0]
                       .logical_key,
                   LogicalNodeKey(L"false-body"));
    CCZ_REQUIRE_EQ(model.body[4].category, ExecutionCategory::Marker);
    CCZ_REQUIRE_EQ(model.body[5].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[6].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[7].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[7].predicate_kind, PredicateKind::None);

    CCZ_REQUIRE_EQ(model.body[8].category, ExecutionCategory::Choice);
    CCZ_REQUIRE_EQ(model.body[8].cases.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[8].cases[0].category, ExecutionCategory::Case);
    CCZ_REQUIRE_EQ(model.body[8].cases[1].category, ExecutionCategory::Case);
    CCZ_REQUIRE_EQ(model.body[8].cases[0].ordinary_children[0].category,
                   ExecutionCategory::Choice);
    CCZ_REQUIRE_EQ(
        model.body[8].cases[0].ordinary_children[0].cases.size(),
        std::size_t(2));
    CCZ_REQUIRE_EQ(
        model.body[8].cases[0].ordinary_children[0].cases[0]
            .ordinary_children[0]
            .category,
        ExecutionCategory::Predicate);
    const ExecutionNode* nested_predicate =
        find_node(model, LogicalNodeKey(L"nested-predicate"));
    CCZ_REQUIRE(nested_predicate != nullptr);
    CCZ_REQUIRE_EQ(nested_predicate->predicate_kind,
                   PredicateKind::Boolean);

    CCZ_REQUIRE_EQ(model.body[9].category, ExecutionCategory::Trigger);
    CCZ_REQUIRE_EQ(model.body[10].category, ExecutionCategory::Diagnostic);
    CCZ_REQUIRE(!model.body[10].diagnostic.empty());
    CCZ_REQUIRE_EQ(model.body[10].instruction_title,
                   std::wstring(L"0x7F:未知指令"));
    CCZ_REQUIRE_EQ(model.body[1].decoded_parameters,
                   std::wstring(L"20:普通事件"));
}

CCZ_TEST(execution_model_separates_top_level_entry_gates_only) {
    const SectionInput input = section(
        {raw_node(L"entry-trigger", 0x07, L"7:入口出战测试"),
         raw_node(L"entry-trigger-2", 0x2d, L"45:入口点击测试"),
         raw_node(L"boundary", 0x00, L"0:正文边界",
                  {raw_node(L"nested-boundary", 0x00, L"0:嵌套边界",
                             {raw_node(L"nested-body", 0x20, L"20:嵌套正文")})}),
         raw_node(L"body", 0x20, L"20:正文"),
         raw_node(L"later-boundary", 0x00, L"0:后续结构")});
    const InstructionCatalog catalog = catalog_for(
        {{0x00, L"边界"}, {0x07, L"出战测试"}, {0x2d, L"点击测试"},
         {0x20, L"普通"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE(model.boundary_complete);
    CCZ_REQUIRE_EQ(model.entry_gates.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.entry_gates[0].category, ExecutionCategory::Trigger);
    CCZ_REQUIRE_EQ(model.entry_gates[1].category, ExecutionCategory::Trigger);
    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(model.body[0].category, ExecutionCategory::SectionGate);
    CCZ_REQUIRE_EQ(model.body[0].ordinary_children[0].category,
                   ExecutionCategory::SectionGate);
    CCZ_REQUIRE_EQ(model.body[0].ordinary_children[0]
                       .ordinary_children[0]
                       .logical_key,
                   LogicalNodeKey(L"nested-body"));
    CCZ_REQUIRE_EQ(model.body[2].category, ExecutionCategory::SectionGate);
    CCZ_REQUIRE(model.diagnostics.empty());
}

CCZ_TEST(execution_model_classifies_scene_commands_as_ordinary) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"map", 0x27, L"39:背景显示"),
         raw_node(L"actor", 0x30, L"48:人物出现")});
    const InstructionCatalog catalog = catalog_for(
        {{0x00, L"边界"}, {0x27, L"背景显示"}, {0x30, L"人物出现"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[2].category, ExecutionCategory::Ordinary);
}

CCZ_TEST(execution_model_reports_incomplete_section_without_nested_split) {
    const SectionInput input = section(
        {raw_node(L"ordinary", 0x20, L"20:普通",
                  {raw_node(L"nested-boundary", 0x00, L"0:嵌套边界")}),
         raw_node(L"second", 0x20, L"20:第二条")});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x20, L"普通"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE(!model.boundary_complete);
    CCZ_REQUIRE(model.entry_gates.empty());
    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[0].ordinary_children[0].category,
                   ExecutionCategory::SectionGate);
    CCZ_REQUIRE(has_diagnostic(model.diagnostics, L"不完整 Section"));
}

CCZ_TEST(execution_model_does_not_bind_else_across_sections) {
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x03, L"else"}, {0x05, L"条件"}});
    const SectionInput first = section(
        {raw_node(L"boundary-0", 0x00, L"0:边界"),
         raw_node(L"predicate-0", 0x05, L"5:第一 Section 条件")});
    SectionInput second = section(
        {raw_node(L"boundary-1", 0x00, L"0:边界"),
         raw_node(L"else-1", 0x03, L"3:else")});
    second.section_key = L"fixture|section:1";

    const SectionExecutionModel first_model =
        build_section_execution_model(first, catalog, {});
    const SectionExecutionModel second_model =
        build_section_execution_model(second, catalog, {});

    CCZ_REQUIRE(first_model.body[1].false_children.empty());
    CCZ_REQUIRE_EQ(second_model.body[1].category,
                   ExecutionCategory::Diagnostic);
    CCZ_REQUIRE(has_diagnostic(second_model.diagnostics,
                               L"同级前置 Predicate"));
}

CCZ_TEST(execution_model_applies_default_and_explicit_predicate_states) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"default-predicate", 0x05, L"5:默认真",
                  {raw_node(L"default-body", 0x00, L"0:结束")}),
         raw_node(L"false-predicate", 0x36, L"36:显式假",
                  {raw_node(L"false-body", 0x00, L"0:结束")}),
         raw_node(L"true-predicate", 0x05, L"5:显式真",
                  {raw_node(L"true-body", 0x00, L"0:结束")}),
         raw_node(L"unsupported-predicate", 0x37, L"37:不可模拟",
                  {raw_node(L"unsupported-body", 0x00, L"0:结束")})});
    const InstructionCatalog catalog = catalog_for(
        {{0x00, L"边界"}, {0x05, L"布尔"}, {0x36, L"布尔36"},
         {0x37, L"布尔37"}});
    ReplayOptions options;
    options.predicate_states[L"false-predicate"] =
        PredicateState::ExplicitFalse;
    options.predicate_states[L"true-predicate"] =
        PredicateState::ExplicitTrue;
    options.predicate_states[L"unsupported-predicate"] =
        PredicateState::Unsupported;

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);

    CCZ_REQUIRE_EQ(model.body[1].predicate_state,
                   PredicateState::DefaultTrue);
    CCZ_REQUIRE_EQ(model.body[2].predicate_state,
                   PredicateState::ExplicitFalse);
    CCZ_REQUIRE_EQ(model.body[3].predicate_state,
                   PredicateState::ExplicitTrue);
    CCZ_REQUIRE_EQ(model.body[4].predicate_state,
                   PredicateState::Unsupported);
}

CCZ_TEST(execution_model_does_not_treat_unstructured_0x05_as_default_true) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"predicate", 0x05, L"5:缺少结构化变量列表",
                  {raw_node(L"body", 0x20, L"正文")})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x05, L"变量测试"}, {0x20, L"普通"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body[1].boolean_evaluation,
                   BooleanEvaluationState::Unsupported);
    CCZ_REQUIRE(!model.body[1].diagnostic.empty());
}

CCZ_TEST(execution_model_keeps_boolean_var_state_separate_from_integer_value) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"bool-predicate", 0x05, L"5:Var600",
                  {raw_node(L"true-body", 0x20, L"真正文")}),
         raw_node(L"else", 0x03, L"3:else",
                  {raw_node(L"false-body", 0x20, L"假正文")})});
    const InstructionCatalog catalog = catalog_for(
        {{0x00, L"边界"}, {0x03, L"else"}, {0x05, L"变量测试"},
         {0x20, L"普通"}});

    ReplayOptions options;
    options.simulated_variables.boolean_variables[600] = false;
    options.simulated_variables.integer_variables[600] = 1;
    options.predicate_states[L"bool-predicate"] = PredicateState::ExplicitFalse;
    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);

    CCZ_REQUIRE_EQ(model.body[1].predicate_state,
                   PredicateState::ExplicitFalse);
    CCZ_REQUIRE_EQ(model.body[1].integer_evaluation,
                   IntegerEvaluationState::NotEvaluated);
    CCZ_REQUIRE_EQ(options.simulated_variables.boolean_variables.at(600),
                   false);
    CCZ_REQUIRE_EQ(options.simulated_variables.integer_variables.at(600), 1);

    options.simulated_variables.integer_variables[600] = 0;
    const SectionExecutionModel unchanged_boolean_model =
        build_section_execution_model(input, catalog, options);
    CCZ_REQUIRE_EQ(unchanged_boolean_model.body[1].predicate_state,
                   PredicateState::ExplicitFalse);
}

CCZ_TEST(boolean_expression_decodes_true_and_false_slots) {
    DialogueCommandView command;
    command.instruction_id = 0x05;
    command.int_data.assign(50, -1);
    command.int_data[0] = 600;
    command.int_data[1] = 601;
    command.int_data[25] = 1200;

    const BooleanVariableExpression expression =
        decode_boolean_variable_expression(command);

    CCZ_REQUIRE(expression.decoded);
    CCZ_REQUIRE_EQ(expression.true_variables,
                   std::vector<std::int32_t>({600, 601}));
    CCZ_REQUIRE_EQ(expression.false_variables,
                   std::vector<std::int32_t>({1200}));
}

CCZ_TEST(boolean_expression_defaults_all_variables_to_false) {
    DialogueCommandView command;
    command.instruction_id = 0x05;
    command.int_data.assign(50, -1);
    command.int_data[0] = 600;
    command.int_data[25] = 1200;

    const BooleanVariableExpression expression =
        decode_boolean_variable_expression(command);
    SimulationEnvironment environment;
    bool result = false;
    CCZ_REQUIRE(evaluate_boolean_variable_expression(
        expression, environment, result));
    CCZ_REQUIRE(!result);

    environment.boolean_variables[600] = true;
    CCZ_REQUIRE(evaluate_boolean_variable_expression(
        expression, environment, result));
    CCZ_REQUIRE(result);
}

CCZ_TEST(execution_model_groups_only_explicit_integer_dispatch_runs) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"case-1", 0x79, L"79:Var600 == 0x01",
                  {raw_node(L"case-1-body", 0x20, L"20:甲")},
                  {4, 600, 0, 0, 1}),
         raw_node(L"case-2", 0x79, L"Var600 == 2",
                  {raw_node(L"case-2-body", 0x20, L"20:乙")},
                  {4, 600, 0, 0, 2}),
         raw_node(L"case-3", 0x79, L"Var600 == 3",
                  {raw_node(L"case-3-body", 0x20, L"20:丙")},
                  {4, 600, 0, 0, 3})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x20, L"普通"}, {0x79, L"整型"}});
    ReplayOptions options;
    options.simulated_variables.integer_variables[600] = 2;

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[1].category,
                   ExecutionCategory::IntegerDispatchGroup);
    CCZ_REQUIRE(model.body[1].synthetic);
    CCZ_REQUIRE_EQ(model.body[1].cases.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(model.body[1].cases[0].category,
                   ExecutionCategory::Predicate);
    CCZ_REQUIRE_EQ(model.body[1].cases[1].logical_key,
                   LogicalNodeKey(L"case-2"));
    CCZ_REQUIRE_EQ(model.body[1].cases[1].integer_evaluation,
                   IntegerEvaluationState::True);
    CCZ_REQUIRE_EQ(model.body[1].cases[2].true_children[0].logical_key,
                   LogicalNodeKey(L"case-3-body"));
    CCZ_REQUIRE_EQ(execution_node_count(model), std::size_t(8));
}

CCZ_TEST(integer_expression_decodes_structured_int_data_and_defaults_to_zero) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    command.int_data = {4, 600, 0, 0, 0};

    const IntegerComparisonExpression expression =
        decode_integer_expression(command);
    CCZ_REQUIRE(expression.decoded);
    CCZ_REQUIRE_EQ(expression.left.kind, IntegerOperandKind::IntegerVariable);
    CCZ_REQUIRE_EQ(expression.left.raw_value, 600);
    CCZ_REQUIRE_EQ(expression.comparator, IntegerComparisonOperator::Equal);
    CCZ_REQUIRE_EQ(expression.right.kind, IntegerOperandKind::Constant);
    CCZ_REQUIRE_EQ(expression.right.raw_value, 0);

    SimulationEnvironment environment;
    bool result = false;
    CCZ_REQUIRE(evaluate_integer_expression(expression, environment, result));
    CCZ_REQUIRE(result);

    environment.integer_variables[600] = 3;
    CCZ_REQUIRE(evaluate_integer_expression(expression, environment, result));
    CCZ_REQUIRE(!result);
}

CCZ_TEST(integer_expression_decodes_all_confirmed_comparators) {
    const struct Case {
        std::int32_t raw;
        IntegerComparisonOperator expected;
    } cases[] = {
        {0, IntegerComparisonOperator::Equal},
        {1, IntegerComparisonOperator::GreaterEqual},
        {2, IntegerComparisonOperator::LessEqual},
        {3, IntegerComparisonOperator::NotEqual},
    };

    for (const Case& value : cases) {
        DialogueCommandView command;
        command.instruction_id = 0x79;
        command.int_data = {4, 600, value.raw, 0, 10};
        const IntegerComparisonExpression expression =
            decode_integer_expression(command);
        CCZ_REQUIRE(expression.decoded);
        CCZ_REQUIRE_EQ(expression.comparator, value.expected);
    }
}

CCZ_TEST(integer_expression_evaluates_less_equal_and_not_equal) {
    SimulationEnvironment environment;
    DialogueCommandView command;
    command.instruction_id = 0x79;

    command.int_data = {4, 600, 2, 0, 10};
    const IntegerComparisonExpression less_equal =
        decode_integer_expression(command);
    environment.integer_variables[600] = 10;
    bool result = false;
    CCZ_REQUIRE(evaluate_integer_expression(less_equal, environment, result));
    CCZ_REQUIRE(result);
    environment.integer_variables[600] = 11;
    CCZ_REQUIRE(evaluate_integer_expression(less_equal, environment, result));
    CCZ_REQUIRE(!result);

    command.int_data = {4, 600, 3, 0, 11};
    const IntegerComparisonExpression not_equal =
        decode_integer_expression(command);
    environment.integer_variables[600] = 10;
    CCZ_REQUIRE(evaluate_integer_expression(not_equal, environment, result));
    CCZ_REQUIRE(result);
    environment.integer_variables[600] = 11;
    CCZ_REQUIRE(evaluate_integer_expression(not_equal, environment, result));
    CCZ_REQUIRE(!result);
}

CCZ_TEST(integer_assignment_supports_both_operand_directions_and_boundaries) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    SimulationEnvironment environment;
    std::wstring diagnostic;

    command.int_data = {4, 600, 2, 0, 10};
    const IntegerComparisonExpression left_less_equal =
        decode_integer_expression(command);
    environment.integer_variables[600] = 11;
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, left_less_equal, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 10);
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, left_less_equal, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 11);

    command.int_data = {0, 10, 2, 4, 600};
    const IntegerComparisonExpression right_less_equal =
        decode_integer_expression(command);
    environment.integer_variables[600] = 9;
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, right_less_equal, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 10);
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, right_less_equal, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 9);

    command.int_data = {4, 600, 1, 0,
                        (std::numeric_limits<std::int32_t>::min)()};
    const IntegerComparisonExpression impossible_left =
        decode_integer_expression(command);
    const auto before_left = environment.integer_variables[600];
    CCZ_REQUIRE(!apply_integer_variable_assignment(
        environment, impossible_left, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], before_left);
    CCZ_REQUIRE(!diagnostic.empty());

    diagnostic.clear();
    command.int_data = {0, (std::numeric_limits<std::int32_t>::min)(), 2, 4,
                        600};
    const IntegerComparisonExpression impossible_right =
        decode_integer_expression(command);
    const auto before_right = environment.integer_variables[600];
    CCZ_REQUIRE(!apply_integer_variable_assignment(
        environment, impossible_right, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], before_right);
    CCZ_REQUIRE(!diagnostic.empty());
}

CCZ_TEST(integer_not_equal_assignment_supports_left_right_and_symmetric_limits) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    std::wstring diagnostic;
    SimulationEnvironment environment;

    command.int_data = {4, 600, 3, 0, 10};
    const IntegerComparisonExpression left_not_equal =
        decode_integer_expression(command);
    environment.integer_variables[600] = 10;
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, left_not_equal, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 11);
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, left_not_equal, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 10);

    command.int_data = {0, 10, 3, 4, 600};
    const IntegerComparisonExpression right_not_equal =
        decode_integer_expression(command);
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, right_not_equal, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 11);
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, right_not_equal, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 10);

    command.int_data = {4, 600, 3, 0,
                        (std::numeric_limits<std::int32_t>::max)()};
    const IntegerComparisonExpression left_max =
        decode_integer_expression(command);
    environment.integer_variables[600] =
        (std::numeric_limits<std::int32_t>::max)();
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, left_max, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600],
                   (std::numeric_limits<std::int32_t>::max)() - 1);

    command.int_data = {0, (std::numeric_limits<std::int32_t>::min)(), 3, 4,
                        600};
    const IntegerComparisonExpression right_min =
        decode_integer_expression(command);
    environment.integer_variables[600] =
        (std::numeric_limits<std::int32_t>::min)();
    CCZ_REQUIRE(apply_integer_variable_assignment(
        environment, right_min, true, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600],
                   (std::numeric_limits<std::int32_t>::min)() + 1);

    diagnostic.clear();
    command.int_data = {4, 600, 2, 0,
                        (std::numeric_limits<std::int32_t>::max)()};
    const IntegerComparisonExpression left_less_equal_max =
        decode_integer_expression(command);
    environment.integer_variables[600] = 0;
    CCZ_REQUIRE(!apply_integer_variable_assignment(
        environment, left_less_equal_max, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 0);
    CCZ_REQUIRE(!diagnostic.empty());

    diagnostic.clear();
    command.int_data = {0, (std::numeric_limits<std::int32_t>::max)(), 1, 4,
                        600};
    const IntegerComparisonExpression right_greater_equal_max =
        decode_integer_expression(command);
    CCZ_REQUIRE(!apply_integer_variable_assignment(
        environment, right_greater_equal_max, false, &diagnostic));
    CCZ_REQUIRE_EQ(environment.integer_variables[600], 0);
    CCZ_REQUIRE(!diagnostic.empty());
}

CCZ_TEST(integer_expression_does_not_read_boolean_variable_namespace) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    command.int_data = {4, 600, 0, 0, 1};
    const IntegerComparisonExpression expression =
        decode_integer_expression(command);

    SimulationEnvironment environment;
    environment.boolean_variables[600] = true;
    bool result = false;
    CCZ_REQUIRE(evaluate_integer_expression(expression, environment, result));
    CCZ_REQUIRE(!result);
}

CCZ_TEST(integer_expression_supports_structured_comparators_and_right_variables) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    command.int_data = {4, 600, 1, 4, 601};
    const IntegerComparisonExpression greater_equal =
        decode_integer_expression(command);

    SimulationEnvironment environment;
    environment.integer_variables[600] = 3;
    environment.integer_variables[601] = 2;
    bool result = false;
    CCZ_REQUIRE(evaluate_integer_expression(greater_equal, environment, result));
    CCZ_REQUIRE(result);

    command.int_data = {4, 600, 2, 0, 4};
    const IntegerComparisonExpression less_equal =
        decode_integer_expression(command);
    CCZ_REQUIRE_EQ(less_equal.comparator,
                   IntegerComparisonOperator::LessEqual);
    CCZ_REQUIRE(evaluate_integer_expression(less_equal, environment, result));
    CCZ_REQUIRE(result);
}

CCZ_TEST(integer_expression_decodes_not_equal_without_text_fallback) {
    DialogueCommandView command;
    command.instruction_id = 0x79;
    command.text = L"Var600 == 1";
    command.int_data = {4, 600, 3, 0, 1};
    const IntegerComparisonExpression expression =
        decode_integer_expression(command);

    SimulationEnvironment environment;
    environment.integer_variables[600] = 1;
    bool result = false;
    CCZ_REQUIRE(expression.decoded);
    CCZ_REQUIRE_EQ(expression.comparator,
                   IntegerComparisonOperator::NotEqual);
    CCZ_REQUIRE(evaluate_integer_expression(expression, environment, result));
    CCZ_REQUIRE(!result);
}

CCZ_TEST(integer_dispatch_group_requires_structured_data_not_display_text) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"first", 0x79, L"Var600 == 0",
                  {raw_node(L"first-body", 0x20, L"20:甲")}),
         raw_node(L"second", 0x79, L"Var600 == 1",
                  {raw_node(L"second-body", 0x20, L"20:乙")})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x20, L"普通"}, {0x79, L"整型"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Predicate);
    CCZ_REQUIRE_EQ(model.body[2].category, ExecutionCategory::Predicate);
    CCZ_REQUIRE(has_diagnostic(model.diagnostics, L"int_data"));
}

CCZ_TEST(execution_model_keeps_leaf_integer_test_commands_ordinary) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"parseable", 0x79, L"Var600 == 1"),
         raw_node(L"not-equal", 0x79, L"Var600 >= 2"),
         raw_node(L"different-left", 0x79, L"Var601 == 3")});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x79, L"整型"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(4));
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[2].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[3].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(execution_node_count(model), std::size_t(4));
}

CCZ_TEST(execution_model_does_not_treat_leaf_test_command_as_sub_event_gate) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"leaf-test", 0x79, L"79:变量测试")});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x79, L"整型"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Ordinary);
    CCZ_REQUIRE_EQ(model.body[1].predicate_kind, PredicateKind::None);
    CCZ_REQUIRE(model.body[1].true_children.empty());
}

CCZ_TEST(execution_model_does_not_select_first_case_without_valid_state) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"choice", 0x12, L"12:选择"),
         raw_node(L"case-one", 0x13, L"13:甲",
                  {raw_node(L"case-one-body", 0x20, L"20:甲")}),
         raw_node(L"case-two", 0x13, L"13:乙",
                  {raw_node(L"case-two-body", 0x20, L"20:乙")})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x12, L"选择"}, {0x13, L"分支"},
                     {0x20, L"普通"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body[1].choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(model.body[1].cases[0].choice_state,
                   ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(model.body[1].cases[1].choice_state,
                   ChoiceState::Unselected);
}

CCZ_TEST(execution_model_recognizes_cases_inside_choice_marker_children) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(
             L"choice", 0x12, L"12:选择",
             {raw_node(L"case-one-marker", 0x01, L"1:子事件设定"),
              raw_node(L"case-one", 0x13, L"13:甲",
                       {raw_node(L"case-one-body", 0x20, L"20:甲")}),
              raw_node(L"case-two-marker", 0x01, L"1:子事件设定"),
              raw_node(L"case-two", 0x13, L"13:乙",
                       {raw_node(L"case-two-body", 0x20, L"20:乙")})})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x01, L"结构"},
                     {0x12, L"选择"}, {0x13, L"分支"},
                     {0x20, L"普通"}});

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, {});

    CCZ_REQUIRE_EQ(model.body.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[1].category, ExecutionCategory::Choice);
    CCZ_REQUIRE_EQ(model.body[1].cases.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(model.body[1].cases[0].logical_key,
                   LogicalNodeKey(L"case-one"));
    CCZ_REQUIRE_EQ(model.body[1].cases[1].logical_key,
                   LogicalNodeKey(L"case-two"));
    CCZ_REQUIRE(model.body[1].ordinary_children.empty());
    CCZ_REQUIRE(!has_diagnostic(model.diagnostics, L"缺少连续 Case"));
    CCZ_REQUIRE(!has_diagnostic(model.diagnostics, L"Case 未连接"));
}

CCZ_TEST(execution_model_marks_auto_target_and_explicit_case_selection) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"choice", 0x12, L"12:选择"),
         raw_node(L"case-one", 0x13, L"13:甲",
                  {raw_node(L"target", 0x20, L"20:目标")}),
         raw_node(L"case-two", 0x13, L"13:乙")},
        L"target");
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x12, L"选择"}, {0x13, L"分支"},
                     {0x20, L"普通"}});

    const SectionExecutionModel auto_model =
        build_section_execution_model(input, catalog, {});
    CCZ_REQUIRE_EQ(auto_model.body[1].choice_state,
                   ChoiceState::AutoTarget);
    CCZ_REQUIRE_EQ(auto_model.body[1].cases[0].choice_state,
                   ChoiceState::AutoTarget);
    CCZ_REQUIRE_EQ(auto_model.body[1].cases[1].choice_state,
                   ChoiceState::Unselected);

    ReplayOptions options;
    options.selected_case_by_choice[L"choice"] = L"case-two";
    const SectionExecutionModel selected_model =
        build_section_execution_model(input, catalog, options);
    CCZ_REQUIRE_EQ(selected_model.body[1].choice_state,
                   ChoiceState::SelectedCase);
    CCZ_REQUIRE_EQ(selected_model.body[1].cases[0].choice_state,
                   ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(selected_model.body[1].cases[1].choice_state,
                   ChoiceState::SelectedCase);
}

CCZ_TEST(execution_model_79_predicate_overrides_drive_reachability_and_choices) {
    SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"predicate", 0x79, L"79:旧文本",
                  {raw_node(L"true-choice", 0x12, L"12:真选择"),
                   raw_node(L"true-case", 0x13, L"13:真分支",
                            {raw_node(L"true-target", 0x20, L"20:真目标")})}),
         raw_node(L"else", 0x03, L"3:else",
                  {raw_node(L"false-choice", 0x12, L"12:假选择"),
                   raw_node(L"false-case", 0x13, L"13:假分支",
                            {raw_node(L"false-target", 0x20, L"20:假目标")})})},
        L"false-target");
    input.nodes[1].command.int_data = {4, 600, 2, 0, 10};
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x03, L"else"}, {0x20, L"普通"},
                     {0x79, L"整型"}, {0x12, L"选择"},
                     {0x13, L"分支"}});

    ReplayOptions explicitly_false;
    explicitly_false.predicate_states[L"predicate"] =
        PredicateState::ExplicitFalse;
    const SectionExecutionModel false_model =
        build_section_execution_model(input, catalog, explicitly_false);
    CCZ_REQUIRE(execution_nodes_reach_target(
        false_model.body, L"false-target", explicitly_false));
    CCZ_REQUIRE(!execution_nodes_reach_target(
        false_model.body, L"true-target", explicitly_false));
    CCZ_REQUIRE_EQ(find_node(false_model, L"false-choice")->choice_state,
                   ChoiceState::AutoTarget);
    CCZ_REQUIRE_EQ(find_node(false_model, L"true-choice")->choice_state,
                   ChoiceState::Unselected);

    ReplayOptions explicitly_true;
    explicitly_true.predicate_states[L"predicate"] =
        PredicateState::ExplicitTrue;
    SectionInput true_input = input;
    true_input.selected_target_key = L"true-target";
    const SectionExecutionModel true_model =
        build_section_execution_model(true_input, catalog, explicitly_true);
    CCZ_REQUIRE(execution_nodes_reach_target(
        true_model.body, L"true-target", explicitly_true));
    CCZ_REQUIRE(!execution_nodes_reach_target(
        true_model.body, L"false-target", explicitly_true));
    CCZ_REQUIRE_EQ(find_node(true_model, L"true-choice")->choice_state,
                   ChoiceState::AutoTarget);
    CCZ_REQUIRE_EQ(find_node(true_model, L"false-choice")->choice_state,
                   ChoiceState::Unselected);

    ReplayOptions unsupported;
    unsupported.predicate_states[L"predicate"] = PredicateState::Unsupported;
    const SectionExecutionModel unsupported_model =
        build_section_execution_model(input, catalog, unsupported);
    CCZ_REQUIRE(!execution_nodes_reach_target(
        unsupported_model.body, L"true-target", unsupported));
    CCZ_REQUIRE(!execution_nodes_reach_target(
        unsupported_model.body, L"false-target", unsupported));
    CCZ_REQUIRE_EQ(find_node(unsupported_model, L"true-choice")->choice_state,
                   ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(
        find_node(unsupported_model, L"false-choice")->choice_state,
        ChoiceState::Unselected);
}

CCZ_TEST(execution_model_keeps_invalid_choice_selection_diagnostic) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"choice", 0x12, L"12:选择"),
         raw_node(L"case-one", 0x13, L"13:甲")});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x12, L"选择"}, {0x13, L"分支"}});
    ReplayOptions options;
    options.selected_case_by_choice[L"choice"] = L"missing-case";

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);

    CCZ_REQUIRE_EQ(model.body[1].choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE(has_diagnostic(model.diagnostics, L"Case"));
}

CCZ_TEST(execution_model_pruned_nested_choice_is_not_auto_target) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"pruned-predicate", 0x05, L"5:已剪枝条件",
                  {raw_node(L"outer-choice", 0x12, L"12:外层选择"),
                   raw_node(L"outer-case", 0x13, L"13:外层分支",
                            {raw_node(L"inner-choice", 0x12, L"12:内层选择"),
                             raw_node(L"inner-case", 0x13, L"13:内层分支",
                                      {raw_node(L"target", 0x20,
                                                L"剪枝目标")})}),
                   raw_node(L"other-case", 0x13, L"13:其他分支")}),
         raw_node(L"after", 0x20, L"条件之后")},
        L"target");
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x05, L"布尔"}, {0x12, L"选择"},
                     {0x13, L"分支"}, {0x20, L"普通"}});
    ReplayOptions options;
    options.predicate_states[L"pruned-predicate"] =
        PredicateState::ExplicitFalse;

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);
    const ExecutionNode& predicate = model.body[1];
    const ExecutionNode& outer_choice = predicate.true_children[0];
    const ExecutionNode& inner_choice =
        outer_choice.cases[0].ordinary_children[0];

    CCZ_REQUIRE_EQ(outer_choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(inner_choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE(!execution_nodes_reach_target(model.body, L"target",
                                              options));
}

CCZ_TEST(execution_model_fingerprint_ignores_runtime_handles_but_tracks_state) {
    SectionInput first_input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"event", 0x20, L"20:事件")});
    first_input.nodes[0].runtime_handle = 0x1000u;
    first_input.nodes[1].runtime_handle = 0x2000u;
    SectionInput second_input = first_input;
    second_input.nodes[0].runtime_handle = 0x3000u;
    second_input.nodes[1].runtime_handle = 0x4000u;
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x20, L"事件"}});

    const SectionExecutionModel first =
        build_section_execution_model(first_input, catalog, {});
    const SectionExecutionModel same_logic =
        build_section_execution_model(second_input, catalog, {});
    CCZ_REQUIRE(execution_model_equal(first, same_logic));
    CCZ_REQUIRE_EQ(first.model_revision, same_logic.model_revision);

    second_input.nodes[1].source_text = L"20:改名后的事件";
    const SectionExecutionModel changed =
        build_section_execution_model(second_input, catalog, {});
    CCZ_REQUIRE(!execution_model_equal(first, changed));
    CCZ_REQUIRE(first.model_revision != changed.model_revision);

    SectionExecutionModel incomplete = first;
    incomplete.boundary_complete = false;
    CCZ_REQUIRE(!execution_model_equal(first, incomplete));
}

CCZ_TEST(execution_model_exposes_canonical_replay_options_name) {
    const SectionInput input = section(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"predicate", 0x05, L"5:条件",
                  {raw_node(L"predicate-body", 0x00, L"0:结束")})});
    const InstructionCatalog catalog =
        catalog_for({{0x00, L"边界"}, {0x05, L"条件"}});
    ReplayOptions options;
    options.predicate_states[L"predicate"] = PredicateState::ExplicitFalse;

    const SectionExecutionModel model =
        build_section_execution_model(input, catalog, options);

    CCZ_REQUIRE_EQ(model.body[1].predicate_state,
                   PredicateState::ExplicitFalse);
}
