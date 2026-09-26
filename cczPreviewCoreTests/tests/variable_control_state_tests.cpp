#include <ccz/preview/variable_control_state.h>

#include "test_framework.h"

using namespace ccz::preview;

namespace {

ScriptVariableIndex fixture_index() {
    ScriptVariableIndex index;
    index.valid = true;
    index.document_key = L"R_14.eex|document";
    index.boolean_variables[600] =
        BooleanVariableUsage{600, 2, 1, 1,
                             {L"R_14.eex|section:0|node:0",
                              L"R_14.eex|section:1|node:0"}, false};
    index.integer_variables[600] =
        IntegerVariableUsage{600, 2,
                             {L"R_14.eex|section:0|node:1",
                              L"R_14.eex|section:1|node:1"}};
    index.integer_variables[3000] =
        IntegerVariableUsage{3000, 1,
                             {L"R_14.eex|section:1|node:2"}};
    return index;
}

}  // namespace

CCZ_TEST(variable_control_rows_show_defaults_and_keep_types_separate) {
    const ScriptVariableIndex index = fixture_index();
    SimulationEnvironment environment;
    environment.boolean_variables[600] = true;
    environment.integer_variables[600] = 12;

    const auto rows = build_variable_control_rows(
        index, environment, L"", false, L"R_14.eex|section:0");
    CCZ_REQUIRE_EQ(rows.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(rows[0].kind, VariableControlKind::Boolean);
    CCZ_REQUIRE(rows[0].boolean_value);
    CCZ_REQUIRE_EQ(rows[1].kind, VariableControlKind::Integer);
    CCZ_REQUIRE_EQ(rows[1].id, 600);
    CCZ_REQUIRE_EQ(rows[1].integer_value, 12);
    CCZ_REQUIRE_EQ(rows[2].integer_value, 0);
    CCZ_REQUIRE(rows[2].is_default);
}

CCZ_TEST(variable_control_row_text_uses_english_boolean_values_without_list_counts) {
    const ScriptVariableIndex index = fixture_index();
    SimulationEnvironment environment;
    const auto rows = build_variable_control_rows(
        index, environment, L"", false, L"R_14.eex|section:0");

    const std::wstring boolean_text = format_variable_control_row(rows[0]);
    CCZ_REQUIRE(boolean_text.find(L"False") != std::wstring::npos);
    CCZ_REQUIRE(boolean_text.find(L"真列表") == std::wstring::npos);
    CCZ_REQUIRE(boolean_text.find(L"假列表") == std::wstring::npos);
    CCZ_REQUIRE(boolean_text.find(L"真") == std::wstring::npos);

    const std::wstring integer_text = format_variable_control_row(rows[2]);
    CCZ_REQUIRE(integer_text.find(L"n3000") != std::wstring::npos);
    CCZ_REQUIRE(integer_text.find(L"0") != std::wstring::npos);
    CCZ_REQUIRE(integer_text.find(L"Var3000") == std::wstring::npos);
}

CCZ_TEST(variable_control_rows_filter_by_search_and_section_without_mutation) {
    const ScriptVariableIndex index = fixture_index();
    SimulationEnvironment environment;
    const auto rows = build_variable_control_rows(
        index, environment, L"3000", true, L"R_14.eex|section:1");
    CCZ_REQUIRE_EQ(rows.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(rows[0].kind, VariableControlKind::Integer);
    CCZ_REQUIRE_EQ(rows[0].id, 3000);
    CCZ_REQUIRE(environment.boolean_variables.empty());
    CCZ_REQUIRE(environment.integer_variables.empty());
}

CCZ_TEST(variable_control_actions_change_only_the_selected_type) {
    SimulationEnvironment environment;
    environment.integer_variables[600] = 4;
    CCZ_REQUIRE(apply_variable_control_action(
        environment, VariableControlKind::Boolean, 600,
        VariableControlAction::SetBooleanTrue, 0));
    CCZ_REQUIRE(environment.boolean_variables.at(600));
    CCZ_REQUIRE_EQ(environment.integer_variables.at(600), 4);
    CCZ_REQUIRE(apply_variable_control_action(
        environment, VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 8));
    CCZ_REQUIRE_EQ(environment.integer_variables.at(600), 8);
    CCZ_REQUIRE(apply_variable_control_action(
        environment, VariableControlKind::Boolean, 600,
        VariableControlAction::RestoreBooleanDefault, 0));
    CCZ_REQUIRE(!environment.boolean_variables.at(600));
    CCZ_REQUIRE(apply_variable_control_action(
        environment, VariableControlKind::Integer, 600,
        VariableControlAction::RestoreIntegerDefault, 0));
    CCZ_REQUIRE_EQ(environment.integer_variables.find(600),
                   environment.integer_variables.end());
}


