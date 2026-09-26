#include "test_framework.h"

#include <PreviewIntegration/PreviewSession.h>

#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/condition_tree_state.h>
#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ccz::editor_preview;
using namespace ccz::preview;

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

InstructionCatalog fixture_catalog() {
    InstructionCatalog catalog;
    catalog.valid = true;
    catalog.names.emplace(0x00, L"事件结束");
    catalog.names.emplace(0x02, L"内部信息");
    catalog.names.emplace(0x03, L"else");
    catalog.names.emplace(0x05, L"变量测试");
    catalog.names.emplace(0x36, L"武将状态测试");
    catalog.names.emplace(0x12, L"选择框");
    catalog.names.emplace(0x13, L"case");
    catalog.names.emplace(0x14, L"对话");
    catalog.names.emplace(0x20, L"信息");
    catalog.names.emplace(0x30, L"人物出现");
    catalog.names.emplace(0x79, L"整型变量测试");
    return catalog;
}

DialogueProfile fixture_profile() {
    DialogueProfile profile;
    profile.architecture = "x86";
    profile.exe_sha256 =
        "781A3833B6031D216392591FCE5D5F43D899BAA5B3ABC148A999EC6D2B0B45C8";
    profile.data_record_base = 0x18C;
    profile.data_record_stride = 0x20;
    profile.avatar_offset = 0x0D;
    profile.avatar_width = 2;
    profile.r_image_offset = 0x0F;
    profile.r_image_width = 2;
    profile.face_index_base = 0;
    profile.face_index_bias = 7;
    profile.dialogue_ids = {
        {0x14, DialogueKind::Standard, true, {},
         PersonFieldEncoding::None, false},
        {0x20, DialogueKind::Standard, true, {},
         PersonFieldEncoding::None, false},
    };
    return profile;
}

EditorPreviewInput make_input(const std::wstring& document_key,
                              bool with_choice = true) {
    EditorPreviewInput input;
    input.document_key = document_key + L"|document";
    input.document_revision = 1;
    input.section.section_key = document_key + L"|section:0";
    input.section.selected_target_key = L"after";

    input.section.nodes.push_back(raw_node(L"boundary", 0x00, L"0:边界"));
    if (with_choice) {
        input.section.nodes.push_back(raw_node(
            L"choice", 0x12, L"12:选择",
            {raw_node(L"case-one", 0x13, L"13:case1",
                      {raw_node(L"case-one-body", 0x20, L"20:case1正文")}),
             raw_node(L"case-two", 0x13, L"13:case2",
                      {raw_node(L"case-two-body", 0x20, L"20:case2正文")})}));
    }
    input.section.nodes.push_back(raw_node(L"after", 0x20, L"20:选中指令"));
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_integer_condition_input(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"predicate", 0x79, L"79:整型条件",
                 {raw_node(L"true-body", 0x20, L"20:真正文")},
                 {4, 600, 0, 0, 1}));
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_boolean_variable_condition_input(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    std::vector<std::int32_t> boolean_data(50, -1);
    boolean_data[0] = 700;
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"boolean", 0x05, L"5:布尔变量",
                 {raw_node(L"true-body", 0x20, L"20:真正文")},
                 boolean_data));
    input.section.nodes.insert(
        input.section.nodes.begin() + 2,
        raw_node(L"else", 0x03, L"3:else",
                 {raw_node(L"false-body", 0x20, L"20:假正文")}));
    input.section.selected_target_key = L"true-body";
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_shared_variable_projection_input(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"integer-3000", 0x79, L"79:整型变量 3000",
                 {raw_node(L"integer-3000-true", 0x20, L"20:整型真正文")},
                 {4, 3000, 0, 0, 1}));
    std::vector<std::int32_t> boolean_data(50, -1);
    boolean_data[0] = 3001;
    input.section.nodes.insert(
        input.section.nodes.begin() + 2,
        raw_node(L"boolean-3001", 0x05, L"5:布尔变量 3001",
                 {raw_node(L"boolean-3001-true", 0x20, L"20:布尔真正文")},
                 boolean_data));
    input.section.nodes.insert(
        input.section.nodes.begin() + 3,
        raw_node(L"boolean-3001-else", 0x03, L"3:else",
                 {raw_node(L"boolean-3001-false", 0x20, L"20:布尔假正文")}));
    input.document_nodes = input.section.nodes;
    return input;
}

const ExecutionNode* find_execution_node(
    const std::vector<ExecutionNode>& nodes, const LogicalNodeKey& key) {
    for (const auto& node : nodes) {
        if (node.logical_key == key) {
            return &node;
        }
        if (const auto* result = find_execution_node(node.ordinary_children, key)) {
            return result;
        }
        if (const auto* result = find_execution_node(node.true_children, key)) {
            return result;
        }
        if (const auto* result = find_execution_node(node.false_children, key)) {
            return result;
        }
        if (const auto* result = find_execution_node(node.cases, key)) {
            return result;
        }
    }
    return nullptr;
}

EditorPreviewInput make_input_without_case_two(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, true);
    input.section.nodes[1].children.pop_back();
    input.document_revision = 2;
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_dialogue_input(const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"dialogue", 0x14, L"14:&云夜\\n正文"));
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_boolean_condition_input(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"boolean", 0x36, L"36:布尔条件",
                 {raw_node(L"boolean-body", 0x20, L"20:布尔正文")}));
    input.document_nodes = input.section.nodes;
    return input;
}

void qualify_fixture_node(RawReplayNode& node,
                          const std::wstring& section_key) {
    node.logical_key = section_key + L"|node:" + node.logical_key;
    for (RawReplayNode& child : node.children) {
        qualify_fixture_node(child, section_key);
    }
}

EditorPreviewInput make_qualified_choice_input(
    const std::wstring& document_key, const std::wstring& section_suffix,
    std::uint64_t revision) {
    EditorPreviewInput input = make_input(document_key, true);
    input.section.section_key =
        input.document_key + L"|section:" + section_suffix;
    input.document_revision = revision;
    for (RawReplayNode& node : input.section.nodes) {
        qualify_fixture_node(node, input.section.section_key);
    }
    input.section.selected_target_key =
        input.section.section_key + L"|node:after";
    input.document_nodes = input.section.nodes;
    return input;
}

EditorPreviewInput make_unsupported_condition_input(
    const std::wstring& document_key) {
    EditorPreviewInput input = make_input(document_key, false);
    input.section.nodes.insert(
        input.section.nodes.begin() + 1,
        raw_node(L"unsupported", 0x79, L"79:缺少结构化数据",
                 {raw_node(L"unsupported-body", 0x20, L"20:条件正文")},
                 {4, 600, 99, 0, 1}));
    input.document_nodes = input.section.nodes;
    return input;
}

PreviewAssetsView no_assets() {
    return {};
}

}  // namespace

CCZ_TEST(preview_session_first_refresh_initializes_case1_and_publishes_once) {
    PreviewSession session;
    const EditorPreviewInput input = make_input(L"R_13.eex");
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();

    const RefreshResult result =
        session.Refresh(input, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE(result.frame_changed);
    CCZ_REQUIRE(session.frame().model != nullptr);
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-one"));
}

CCZ_TEST(preview_session_default_case1_native_plan_excludes_case2) {
    PreviewSession session;
    EditorPreviewInput input = make_input(L"R_native_case1");
    // Keep the Choice unselected on input.  The first Refresh must initialize
    // Case1, then publish a native plan for only the selected branch.
    input.section.nodes[1] = raw_node(
        L"choice", 0x12, L"12:选择",
        {raw_node(L"case-one", 0x13, L"13:case1",
                  {raw_node(L"case-one-native", 0x30, L"30:case1人物出现")} ),
         raw_node(L"case-two", 0x13, L"13:case2",
                  {raw_node(L"case-two-native", 0x30, L"30:case2人物出现")} )});
    input.document_nodes = input.section.nodes;

    const RefreshResult result = session.Refresh(
        input, fixture_catalog(), fixture_profile(), no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-one"));
    CCZ_REQUIRE(session.frame().native_plan.complete);
    CCZ_REQUIRE(session.frame().native_plan.target_reached);
    CCZ_REQUIRE_EQ(session.frame().native_plan.items.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(session.frame().native_plan.items.front().logical_key,
                   LogicalNodeKey(L"case-one-native"));
    for (const NativeReplayItem& item : session.frame().native_plan.items) {
        CCZ_REQUIRE(item.logical_key != LogicalNodeKey(L"case-two-native"));
    }
}

CCZ_TEST(preview_session_second_identical_refresh_is_unchanged) {
    PreviewSession session;
    const EditorPreviewInput input = make_input(L"R_13.eex");
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();

    session.Refresh(input, catalog, profile, no_assets());
    const RefreshResult result =
        session.Refresh(input, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Unchanged);
    CCZ_REQUIRE(!result.frame_changed);
}

CCZ_TEST(preview_session_preserves_explicit_case2_across_refreshes) {
    PreviewSession session;
    EditorPreviewInput input = make_input(L"R_13.eex");
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        L"case-two", ConditionTreeAction::SelectCase, 0, &diagnostic));
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-two"));

    const RefreshResult same_section =
        session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(same_section.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-two"));

    input.document_revision = 2;
    const RefreshResult changed_section =
        session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(changed_section.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-two"));
}

CCZ_TEST(preview_session_document_switch_clears_transient_state) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    const EditorPreviewInput first = make_input(L"R_13.eex");
    session.Refresh(first, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        L"case-two", ConditionTreeAction::SelectCase, 0, &diagnostic));
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Boolean, 700,
        VariableControlAction::SetBooleanTrue, 0, &diagnostic));
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 42, &diagnostic));

    const EditorPreviewInput second = make_input(L"R_14.eex");
    session.Refresh(second, catalog, profile, no_assets());

    CCZ_REQUIRE(session.options().simulated_variables.boolean_variables.empty());
    CCZ_REQUIRE(session.options().simulated_variables.integer_variables.empty());
    CCZ_REQUIRE(session.options().selected_case_by_choice.find(L"case-two") ==
                session.options().selected_case_by_choice.end());
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-one"));
}

CCZ_TEST(preview_session_structure_change_removes_stale_case_selection) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    const EditorPreviewInput first = make_input(L"R_13.eex");
    session.Refresh(first, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        L"case-two", ConditionTreeAction::SelectCase, 0, &diagnostic));

    const EditorPreviewInput changed =
        make_input_without_case_two(L"R_13.eex");
    const RefreshResult result =
        session.Refresh(changed, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().selected_case_by_choice.at(L"choice"),
                   LogicalNodeKey(L"case-one"));
}

CCZ_TEST(preview_session_structure_change_removes_stale_predicate_state) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    const EditorPreviewInput first =
        make_boolean_condition_input(L"R_13.eex");
    session.Refresh(first, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        L"boolean", ConditionTreeAction::SetPredicateFalse, 0,
        &diagnostic));
    CCZ_REQUIRE_EQ(session.options().predicate_states.at(L"boolean"),
                   PredicateState::ExplicitFalse);

    EditorPreviewInput changed = make_input(L"R_13.eex", false);
    changed.document_revision = 2;
    session.Refresh(changed, catalog, profile, no_assets());
    CCZ_REQUIRE(session.options().predicate_states.find(L"boolean") ==
                session.options().predicate_states.end());
}

CCZ_TEST(preview_session_section_switch_preserves_document_variables) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    EditorPreviewInput first = make_integer_condition_input(L"R_13.eex");
    session.Refresh(first, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 42, &diagnostic));

    EditorPreviewInput second = first;
    second.section.section_key = L"R_13.eex|section:1";
    second.document_revision = 2;
    const RefreshResult result =
        session.Refresh(second, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   42);
}

CCZ_TEST(preview_session_section_switch_isolates_and_restores_choice_state) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    const EditorPreviewInput section_a =
        make_qualified_choice_input(L"R_13.eex", L"0", 1);
    session.Refresh(section_a, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        section_a.section.section_key + L"|node:case-two",
        ConditionTreeAction::SelectCase, 0, &diagnostic));

    const EditorPreviewInput section_b =
        make_qualified_choice_input(L"R_13.eex", L"1", 2);
    const RefreshResult switched =
        session.Refresh(section_b, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(switched.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(
        session.options().selected_case_by_choice.at(
            section_a.section.section_key + L"|node:choice"),
        section_a.section.section_key + L"|node:case-two");
    CCZ_REQUIRE_EQ(
        session.options().selected_case_by_choice.at(
            section_b.section.section_key + L"|node:choice"),
        section_b.section.section_key + L"|node:case-one");

    const RefreshResult returned =
        session.Refresh(section_a, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(returned.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(
        session.options().selected_case_by_choice.at(
            section_a.section.section_key + L"|node:choice"),
        section_a.section.section_key + L"|node:case-two");
}

CCZ_TEST(preview_session_keeps_boolean_and_integer_namespaces_separate) {
    PreviewSession session;
    const EditorPreviewInput input = make_input(L"R_13.eex", false);
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;
    session.Refresh(input, catalog, profile, no_assets());

    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Boolean, 600,
        VariableControlAction::SetBooleanTrue, 0, &diagnostic));
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 77, &diagnostic));

    CCZ_REQUIRE_EQ(session.options().simulated_variables.boolean_variables.at(600),
                   true);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   77);
}

CCZ_TEST(preview_session_failed_condition_assignment_is_transactional) {
    PreviewSession session;
    const EditorPreviewInput input = make_integer_condition_input(L"R_13.eex");
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;
    session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyConditionAction(
        L"predicate", ConditionTreeAction::SetIntegerValue, 2,
        &diagnostic));
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   2);

    CCZ_REQUIRE(!session.ApplyConditionAction(
        L"missing-node", ConditionTreeAction::SetIntegerValue, 99,
        &diagnostic));
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   2);
}

CCZ_TEST(preview_session_condition_action_refresh_projects_79_and_05_options) {
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;

    PreviewSession integer_session;
    const EditorPreviewInput integer_input =
        make_integer_condition_input(L"R_79.eex");
    integer_session.Refresh(integer_input, catalog, profile, no_assets());
    CCZ_REQUIRE(integer_session.ApplyConditionAction(
        L"predicate", ConditionTreeAction::SetIntegerValue, 1, &diagnostic));
    const RefreshResult integer_refresh = integer_session.Refresh(
        integer_input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(integer_refresh.status, RefreshStatus::Updated);
    const auto* integer_node = find_execution_node(
        integer_session.frame().model->body, L"predicate");
    CCZ_REQUIRE(integer_node != nullptr);
    CCZ_REQUIRE_EQ(integer_session.options().simulated_variables
                       .integer_variables.at(600), 1);
    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
        *integer_node, true, true, integer_session.options()));
    CCZ_REQUIRE(condition_tree_action_state(
                     integer_node, integer_session.options())
                     .can_set_integer);

    PreviewSession boolean_session;
    const EditorPreviewInput boolean_input =
        make_boolean_variable_condition_input(L"R_05.eex");
    boolean_session.Refresh(boolean_input, catalog, profile, no_assets());
    CCZ_REQUIRE(boolean_session.ApplyConditionAction(
        L"boolean", ConditionTreeAction::SetPredicateTrue, 0, &diagnostic));
    const RefreshResult boolean_refresh = boolean_session.Refresh(
        boolean_input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(boolean_refresh.status, RefreshStatus::Updated);
    const auto* boolean_node = find_execution_node(
        boolean_session.frame().model->body, L"boolean");
    CCZ_REQUIRE(boolean_node != nullptr);
    CCZ_REQUIRE_EQ(boolean_session.options().simulated_variables
                       .boolean_variables.at(700), true);
    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
        *boolean_node, true, true, boolean_session.options()));
    CCZ_REQUIRE(condition_tree_node_title(
                     *boolean_node, boolean_session.options())
                     .find(L"自动结果：真") != std::wstring::npos);
}

CCZ_TEST(preview_session_variable_control_3000_projects_into_shared_condition_model) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const EditorPreviewInput input =
        make_shared_variable_projection_input(L"R_shared_variables.eex");
    std::wstring diagnostic;

    session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Integer, 3000,
        VariableControlAction::SetIntegerValue, 1, &diagnostic));
    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Boolean, 3001,
        VariableControlAction::SetBooleanTrue, 0, &diagnostic));

    const RefreshResult refreshed =
        session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(refreshed.status, RefreshStatus::Updated);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(3000),
                   1);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.boolean_variables.at(3001),
                   true);

    const auto* integer_node = find_execution_node(
        session.frame().model->body, L"integer-3000");
    CCZ_REQUIRE(integer_node != nullptr);
    CCZ_REQUIRE_EQ(integer_node->integer_evaluation,
                   IntegerEvaluationState::True);
    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
        *integer_node, true, true, session.options()));

    const auto* boolean_node = find_execution_node(
        session.frame().model->body, L"boolean-3001");
    CCZ_REQUIRE(boolean_node != nullptr);
    CCZ_REQUIRE_EQ(boolean_node->boolean_evaluation,
                   BooleanEvaluationState::True);
    CCZ_REQUIRE(condition_tree_predicate_branch_reachable(
        *boolean_node, true, true, session.options()));
    CCZ_REQUIRE(condition_tree_node_title(
                     *boolean_node, session.options())
                     .find(L"自动结果：真") != std::wstring::npos);
}

CCZ_TEST(preview_session_failed_variable_action_is_transactional) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;
    session.Refresh(make_input(L"R_13.eex", false), catalog, profile,
                    no_assets());

    CCZ_REQUIRE(!session.ApplyVariableAction(
        VariableControlKind::Boolean, 600,
        VariableControlAction::SetIntegerValue, 9, &diagnostic));
    CCZ_REQUIRE(session.options().simulated_variables.boolean_variables.empty());
    CCZ_REQUIRE(session.options().simulated_variables.integer_variables.empty());
    CCZ_REQUIRE(!session.ApplyVariableAction(
        VariableControlKind::Boolean, 600,
        VariableControlAction::SetBooleanFalse, 0, &diagnostic));
    CCZ_REQUIRE(session.options().simulated_variables.boolean_variables.empty());
}

CCZ_TEST(preview_session_failed_integer_variable_action_is_transactional) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    std::wstring diagnostic;
    session.Refresh(make_integer_condition_input(L"R_13.eex"), catalog, profile,
                    no_assets());

    CCZ_REQUIRE(session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 42, &diagnostic));
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   42);

    CCZ_REQUIRE(!session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetIntegerValue, 42, &diagnostic));
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   42);

    CCZ_REQUIRE(!session.ApplyVariableAction(
        VariableControlKind::Integer, 600,
        VariableControlAction::SetBooleanTrue, 0, &diagnostic));
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables.at(600),
                   42);
    CCZ_REQUIRE(session.options().simulated_variables.boolean_variables.empty());
}

CCZ_TEST(preview_session_missing_target_is_rejected_without_publishing) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const EditorPreviewInput valid = make_input(L"R_13.eex", false);
    session.Refresh(valid, catalog, profile, no_assets());
    const auto previous_model = session.frame().model;

    EditorPreviewInput missing = valid;
    missing.section.selected_target_key = L"missing-target";
    const RefreshResult result =
        session.Refresh(missing, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::InvalidInput);
    CCZ_REQUIRE(!result.frame_changed);
    CCZ_REQUIRE(session.frame().model == previous_model);
    CCZ_REQUIRE_EQ(session.frame().active_node, LogicalNodeKey(L"after"));
}

CCZ_TEST(preview_session_text_survives_without_portrait_assets) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const RefreshResult result = session.Refresh(
        make_dialogue_input(L"R_13.eex"), catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE(session.frame().dialogue.visible);
    CCZ_REQUIRE(!session.frame().dialogue.body.empty());
    CCZ_REQUIRE(session.frame().dialogue.portrait_image.empty());
}

CCZ_TEST(preview_session_text_survives_with_partial_portrait_assets) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const std::vector<std::uint8_t> data_e5{0};
    const std::vector<std::uint8_t> face_e5{0};
    const PreviewAssetsView partial_assets{&data_e5, &face_e5, nullptr};
    const RefreshResult result = session.Refresh(
        make_dialogue_input(L"R_13.eex"), catalog, profile, partial_assets);

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE(session.frame().dialogue.visible);
    CCZ_REQUIRE(!session.frame().dialogue.body.empty());
    CCZ_REQUIRE(session.frame().dialogue.portrait_image.empty());
}

CCZ_TEST(preview_frame_selects_embedded_dialogue_segment) {
    const InstructionCatalog catalog = fixture_catalog();
    DialogueProfile profile = fixture_profile();
    profile.dialogue_ids.front().parse_speaker_prefix = true;

    EditorPreviewInput input = make_dialogue_input(L"R_multi_dialogue.eex");
    input.section.selected_target_key = L"dialogue";
    input.section.nodes[1].command.text =
        L"&陆坤\n第一段\n&*.174\n第二段";
    input.document_nodes = input.section.nodes;

    ReplayOptions options;
    const SectionExecutionModel model =
        build_section_execution_model(input.section, catalog, options);
    const PreviewFrame frame = BuildPreviewFrame(
        input, model, options, catalog, profile, no_assets(), std::size_t{1});

    CCZ_REQUIRE(frame.native_plan.complete);
    CCZ_REQUIRE(frame.dialogue.visible);
    CCZ_REQUIRE_EQ(frame.dialogue.speaker_label, std::wstring(L"*.174"));
    CCZ_REQUIRE_EQ(frame.dialogue.body, std::wstring(L"第二段"));
}

CCZ_TEST(preview_session_publishes_diagnostic_for_incomplete_native_plan) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const RefreshResult result = session.Refresh(
        make_unsupported_condition_input(L"R_13.eex"), catalog, profile,
        no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::Updated);
    CCZ_REQUIRE(!session.frame().native_plan.complete);
    CCZ_REQUIRE(!session.frame().native_plan.target_reached);
    CCZ_REQUIRE(!session.frame().native_plan.diagnostic.empty());
    CCZ_REQUIRE(!session.frame().diagnostics.empty());
}

#if defined(CCZ_PREVIEW_SESSION_TESTING)
CCZ_TEST(preview_session_reentrant_refresh_is_rejected_before_state_change) {
    PreviewSession session;
    const EditorPreviewInput input = make_input(L"R_13.eex");
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    RefreshResult nested;

    session.SetRefreshProbeForTesting([&] {
        session.SetRefreshProbeForTesting(nullptr);
        nested = session.Refresh(input, catalog, profile, no_assets());
    });

    const RefreshResult outer =
        session.Refresh(input, catalog, profile, no_assets());
    CCZ_REQUIRE_EQ(nested.status, RefreshStatus::ReentrantRejected);
    CCZ_REQUIRE(!nested.frame_changed);
    CCZ_REQUIRE_EQ(outer.status, RefreshStatus::Updated);
    CCZ_REQUIRE(session.frame().model != nullptr);
}
#endif

CCZ_TEST(preview_session_invalid_input_does_not_publish_partial_frame) {
    PreviewSession session;
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const EditorPreviewInput valid = make_input(L"R_13.eex", false);
    session.Refresh(valid, catalog, profile, no_assets());
    const auto previous_model = session.frame().model;
    const auto previous_options = session.options();

    EditorPreviewInput invalid = valid;
    invalid.section.nodes.clear();
    invalid.document_nodes.clear();
    const RefreshResult result =
        session.Refresh(invalid, catalog, profile, no_assets());

    CCZ_REQUIRE_EQ(result.status, RefreshStatus::InvalidInput);
    CCZ_REQUIRE(!result.frame_changed);
    CCZ_REQUIRE(session.frame().model == previous_model);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.boolean_variables,
                   previous_options.simulated_variables.boolean_variables);
    CCZ_REQUIRE_EQ(session.options().simulated_variables.integer_variables,
                   previous_options.simulated_variables.integer_variables);
}
