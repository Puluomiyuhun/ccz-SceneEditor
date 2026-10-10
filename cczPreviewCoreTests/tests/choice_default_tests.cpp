#include "test_framework.h"

#include <ccz/preview/execution_model.h>

#include <string>
#include <utility>

using namespace ccz::preview;

namespace {

ExecutionNode node(const wchar_t* key, ExecutionCategory category) {
    ExecutionNode result;
    result.logical_key = key;
    result.category = category;
    return result;
}

SectionExecutionModel nested_choice_model() {
    ExecutionNode outer = node(L"outer", ExecutionCategory::Choice);
    ExecutionNode outer_first = node(L"outer-first", ExecutionCategory::Case);
    ExecutionNode outer_second = node(L"outer-second", ExecutionCategory::Case);

    ExecutionNode inner = node(L"inner", ExecutionCategory::Choice);
    inner.cases.push_back(node(L"inner-first", ExecutionCategory::Case));
    inner.cases.push_back(node(L"inner-second", ExecutionCategory::Case));
    outer_first.ordinary_children.push_back(std::move(inner));

    outer.cases.push_back(std::move(outer_first));
    outer.cases.push_back(std::move(outer_second));

    SectionExecutionModel model;
    model.body.push_back(std::move(outer));
    return model;
}

}  // namespace

CCZ_TEST(choice_defaults_recurse_without_overwriting_explicit_choices) {
    SectionExecutionModel model = nested_choice_model();
    ReplayOptions options;

    CCZ_REQUIRE(initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"outer"),
                   LogicalNodeKey(L"outer-first"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"inner"),
                   LogicalNodeKey(L"inner-first"));
    CCZ_REQUIRE(!initialize_default_choice_selections(model, options));

    options.selected_case_by_choice[L"outer"] = L"missing-case";
    CCZ_REQUIRE(!initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"outer"),
                   LogicalNodeKey(L"missing-case"));
}

CCZ_TEST(choice_defaults_preserve_explicit_invalid_nested_choice) {
    SectionExecutionModel model = nested_choice_model();
    ReplayOptions options;
    options.selected_case_by_choice[L"outer"] = L"invalid-outer";
    options.selected_case_by_choice[L"inner"] = L"invalid-inner";

    CCZ_REQUIRE(!initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"outer"),
                   LogicalNodeKey(L"invalid-outer"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"inner"),
                   LogicalNodeKey(L"invalid-inner"));
}

CCZ_TEST(choice_defaults_recurse_through_all_execution_containers) {
    SectionExecutionModel model;

    ExecutionNode entry_choice = node(L"entry-choice", ExecutionCategory::Choice);
    entry_choice.cases.push_back(
        node(L"entry-case", ExecutionCategory::Case));
    model.entry_gates.push_back(std::move(entry_choice));

    ExecutionNode predicate = node(L"predicate", ExecutionCategory::Predicate);
    ExecutionNode true_choice = node(L"true-choice", ExecutionCategory::Choice);
    true_choice.cases.push_back(node(L"true-case", ExecutionCategory::Case));
    predicate.true_children.push_back(std::move(true_choice));

    ExecutionNode false_choice =
        node(L"false-choice", ExecutionCategory::Choice);
    false_choice.cases.push_back(
        node(L"false-case", ExecutionCategory::Case));
    predicate.false_children.push_back(std::move(false_choice));

    ExecutionNode case_parent = node(L"case-parent", ExecutionCategory::Choice);
    ExecutionNode case_node = node(L"case-parent-case", ExecutionCategory::Case);
    ExecutionNode case_child = node(L"case-child-choice", ExecutionCategory::Choice);
    case_child.cases.push_back(node(L"case-child-case", ExecutionCategory::Case));
    case_node.ordinary_children.push_back(std::move(case_child));
    case_parent.cases.push_back(std::move(case_node));

    model.body.push_back(std::move(predicate));
    model.body.push_back(std::move(case_parent));

    ReplayOptions options;
    CCZ_REQUIRE(initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"entry-choice"),
                   LogicalNodeKey(L"entry-case"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"true-choice"),
                   LogicalNodeKey(L"true-case"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"false-choice"),
                   LogicalNodeKey(L"false-case"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"case-parent"),
                   LogicalNodeKey(L"case-parent-case"));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"case-child-choice"),
                   LogicalNodeKey(L"case-child-case"));
}
