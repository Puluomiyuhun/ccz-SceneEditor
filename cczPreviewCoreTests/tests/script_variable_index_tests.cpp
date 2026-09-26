#include <ccz/preview/script_variable_index.h>

#include "test_framework.h"

#include <cstdint>
#include <initializer_list>
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

RawReplayNode bool_test(const wchar_t* key,
                        std::initializer_list<std::int32_t> true_ids,
                        std::initializer_list<std::int32_t> false_ids) {
    RawReplayNode node = raw_node(key, 0x05, L"5:变量测试");
    node.command.int_data.assign(50, -1);
    std::size_t index = 0;
    for (const std::int32_t id : true_ids) {
        node.command.int_data[index++] = id;
    }
    index = 25;
    for (const std::int32_t id : false_ids) {
        node.command.int_data[index++] = id;
    }
    return node;
}

RawReplayNode integer_test(const wchar_t* key, std::int32_t variable,
                           std::int32_t constant) {
    return raw_node(key, 0x79, L"79:变量测试",
                    {raw_node(L"body", 0x20, L"20:正文")},
                    {4, variable, 0, 0, constant});
}

}  // namespace

CCZ_TEST(script_index_scans_all_sections_and_keeps_types_separate) {
    const std::vector<RawReplayNode> document = {
        raw_node(L"scene-0", 0xffff, L"Scene", {
            raw_node(L"section-a", 0xffff, L"Section", {
                bool_test(L"bool-a", {600}, {1200}),
                integer_test(L"int-a", 600, 1),
            }),
        }),
        raw_node(L"scene-1", 0xffff, L"Scene", {
            raw_node(L"section-b", 0xffff, L"Section", {
                bool_test(L"bool-b", {600}, {}),
                integer_test(L"int-b", 600, 2),
            }),
        }),
    };

    const ScriptVariableIndex index =
        scan_script_variables(document, L"R_14.eex");

    CCZ_REQUIRE(index.valid);
    CCZ_REQUIRE_EQ(index.boolean_variables.at(600).occurrence_count,
                   std::size_t(2));
    CCZ_REQUIRE_EQ(index.boolean_variables.at(1200).false_list_occurrences,
                   std::size_t(1));
    CCZ_REQUIRE_EQ(index.integer_variables.at(600).occurrence_count,
                   std::size_t(2));
    CCZ_REQUIRE_EQ(index.boolean_variables.at(600).references.size(),
                   std::size_t(2));
}

CCZ_TEST(script_index_sync_preserves_values_and_separates_types) {
    const ScriptVariableIndex index = scan_script_variables(
        {bool_test(L"bool", {600}, {}), integer_test(L"int", 600, 1)},
        L"R_14.eex");
    SimulationEnvironment environment;
    environment.boolean_variables[600] = true;
    environment.boolean_variables[999] = true;
    environment.integer_variables[600] = 12;
    environment.integer_variables[999] = 34;

    synchronize_boolean_defaults(environment, index);
    synchronize_integer_scope(environment, index);

    CCZ_REQUIRE(environment.boolean_variables.at(600));
    CCZ_REQUIRE(environment.boolean_variables.find(999) ==
                environment.boolean_variables.end());
    CCZ_REQUIRE_EQ(environment.integer_variables.at(600), 12);
    CCZ_REQUIRE(environment.integer_variables.find(999) ==
                environment.integer_variables.end());
}

CCZ_TEST(script_index_reports_boolean_list_conflicts) {
    const ScriptVariableIndex index = scan_script_variables(
        {bool_test(L"conflict", {600}, {600})}, L"R_14.eex");

    CCZ_REQUIRE(index.boolean_variables.at(600).conflicts);
    CCZ_REQUIRE(!index.diagnostics.empty());
}


