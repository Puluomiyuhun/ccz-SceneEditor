#include "test_framework.h"

#include <ccz/preview/dialogue_replay.h>
#include <ccz/preview/execution_model.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace ccz::preview;

CCZ_TEST(r_appearance_direct_and_variable_write_update_same_preview_attribute) {
 PreviewState state;
 DialogueProfile profile;
 apply_dialogue_command(state,{0x35,{823,343},L""},profile);
 CCZ_REQUIRE(state.simulated.data_attributes.at({823,0})==343);
 apply_dialogue_command(state,{0x77,{2,0,2,0,344},L""},profile);
 apply_dialogue_command(state,{0x78,{0,1,823,0},L""},profile);
 CCZ_REQUIRE(state.simulated.data_attributes.at({823,0})==344);
}

namespace {

DialogueProfile make_profile() {
    DialogueProfile profile{};
    profile.dialogue_ids = {
        {20, DialogueKind::Standard, true, {},
         PersonFieldEncoding::SpeakerPrefixLabel, true},
    };
    return profile;
}

DialogueProfile make_extended_profile() {
    DialogueProfile profile = make_profile();
    profile.dialogue_ids.push_back(
        {0x16, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    profile.dialogue_ids.push_back(
        {0x2c, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    profile.dialogue_ids.push_back(
        {0x69, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    return profile;
}

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
    catalog.names.emplace(static_cast<std::uint16_t>(0x01), L"子事件设定");
    catalog.names.emplace(static_cast<std::uint16_t>(0x02), L"内部信息");
    catalog.names.emplace(static_cast<std::uint16_t>(0x03), L"else");
    catalog.names.emplace(static_cast<std::uint16_t>(0x05), L"变量测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x12), L"选择框");
    catalog.names.emplace(static_cast<std::uint16_t>(0x13), L"case");
    catalog.names.emplace(static_cast<std::uint16_t>(20), L"对话");
    catalog.names.emplace(static_cast<std::uint16_t>(24), L"普通事件");
    catalog.names.emplace(static_cast<std::uint16_t>(32), L"头像状态");
    catalog.names.emplace(static_cast<std::uint16_t>(0x36), L"武将状态测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x37), L"数值测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x72), L"信息传送");
    catalog.names.emplace(static_cast<std::uint16_t>(0x77), L"变量运算");
    catalog.names.emplace(static_cast<std::uint16_t>(0x79), L"变量测试");
    return catalog;
}

SectionExecutionModel make_model(
    std::vector<RawReplayNode> nodes, const wchar_t* selected_target = L"",
    ReplayOptions options = {}) {
    SectionInput input;
    input.section_key = L"fixture|section:0";
    input.nodes = std::move(nodes);
    input.selected_target_key = selected_target;
    return build_section_execution_model(input, fixture_catalog(), options);
}

bool has_diagnostic(const std::wstring& diagnostic,
                    const wchar_t* fragment) {
    return diagnostic.find(fragment) != std::wstring::npos;
}

}  // namespace

CCZ_TEST(dialogue_replay_replays_section_entry_and_unconditional_container) {
    const SectionExecutionModel model = make_model({
        raw_node(L"entry", 20, L"入口"),
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"container", 0x02, L"2:内部信息",
                 {raw_node(L"face", 32, L"", {}, {7}),
                  raw_node(L"inside", 20, L"容器台词")}),
        raw_node(L"target", 24, L"正文目标"),
    });

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(model.entry_gates.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(model.body.front().category,
                   ExecutionCategory::SectionGate);
    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"容器台词"));
    CCZ_REQUIRE(result.state.face_state_known);
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(7));
    CCZ_REQUIRE_EQ(result.last_applied_node,
                   LogicalNodeKey(L"inside"));
}

CCZ_TEST(dialogue_replay_skips_diagnostic_subtrees_without_changing_state) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"before-face", 32, L"", {}, {7}),
        raw_node(L"before", 20, L"此前"),
        raw_node(L"orphan-else", 0x03, L"3:else",
                 {raw_node(L"orphan-else-face", 32, L"", {}, {99}),
                  raw_node(L"orphan-else-dialogue", 20, L"孤立 else")}),
        raw_node(L"stray-case", 0x13, L"13:case",
                 {raw_node(L"stray-case-face", 32, L"", {}, {88}),
                  raw_node(L"stray-case-dialogue", 20, L"孤立 Case")}),
        raw_node(L"unknown", 0x7f, L"127:未知",
                 {raw_node(L"unknown-face", 32, L"", {}, {77}),
                  raw_node(L"unknown-dialogue", 20, L"孤立未知子树")}),
        raw_node(L"target", 24, L"目标"),
    });

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"此前"));
    CCZ_REQUIRE(result.state.face_state_known);
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(7));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"before"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"else 未能绑定"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"Case 未连接"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"未注册的指令"));
}

CCZ_TEST(dialogue_replay_defaults_missing_predicate_to_true) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"predicate", 0x05, L"5:变量测试",
                 {raw_node(L"true-dialogue", 20, L"真分支")}),
        raw_node(L"else", 0x03, L"3:else",
                 {raw_node(L"false-dialogue", 20, L"假分支")}),
        raw_node(L"target", 24, L"目标"),
    });

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"真分支"));
}

CCZ_TEST(dialogue_replay_explicit_false_enters_else_branch) {
    ReplayOptions options;
    options.predicate_states[L"predicate"] = PredicateState::ExplicitFalse;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"predicate", 0x05, L"5:变量测试",
                  {raw_node(L"true-face", 32, L"", {}, {11}),
                   raw_node(L"true-dialogue", 20, L"真分支")}),
         raw_node(L"else", 0x03, L"3:else",
                  {raw_node(L"false-face", 32, L"", {}, {22}),
                   raw_node(L"false-dialogue", 20, L"假分支")}),
          raw_node(L"target", 24, L"目标")},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"假分支"));
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(22));
}

CCZ_TEST(dialogue_replay_79_predicate_overrides_all_states) {
    const struct Case {
        PredicateState state;
        ReplayStopReason expected_reason;
        const wchar_t* expected_body;
    } cases[] = {
        {PredicateState::ExplicitTrue, ReplayStopReason::ReachedTarget,
         L"真分支"},
        {PredicateState::ExplicitFalse, ReplayStopReason::ReachedTarget,
         L"假分支"},
        {PredicateState::Unsupported, ReplayStopReason::UnsupportedCondition,
         L""},
    };

    for (const Case& value : cases) {
        ReplayOptions options;
        options.predicate_states[L"predicate"] = value.state;
        const SectionExecutionModel model = make_model(
            {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
             raw_node(L"predicate", 0x79, L"79:旧文本",
                      {raw_node(L"true-dialogue", 20, L"真分支")},
                      {4, 600, 2, 0, 10}),
             raw_node(L"else", 0x03, L"3:else",
                      {raw_node(L"false-dialogue", 20, L"假分支")}),
              raw_node(L"target", 24, L"目标")},
            L"", options);

        const ReplayResult result = replay_section_to(
            model, L"target", make_profile(), options);
        CCZ_REQUIRE_EQ(result.reason, value.expected_reason);
        if (value.expected_body[0] == L'\0') {
            CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"不支持"));
        } else {
            CCZ_REQUIRE_EQ(result.state.dialogue.body,
                           std::wstring(value.expected_body));
        }
    }
}

CCZ_TEST(dialogue_replay_explicit_false_without_else_continues_silently) {
    ReplayOptions options;
    options.predicate_states[L"predicate"] = PredicateState::ExplicitFalse;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"predicate", 0x05, L"5:变量测试",
                  {raw_node(L"true-dialogue", 20, L"不应进入")}),
         raw_node(L"after-dialogue", 20, L"条件之后"),
         raw_node(L"after", 24, L"目标")},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"after", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"条件之后"));
    CCZ_REQUIRE(!has_diagnostic(result.diagnostic, L"无假分支"));
}

CCZ_TEST(dialogue_replay_evaluates_integer_predicate_from_simulated_value) {
    const std::vector<RawReplayNode> nodes = {
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"integer-predicate", 0x79, L"79:变量测试",
                 {raw_node(L"true-body", 20, L"整型真")},
                 {4, 600, 0, 0, 1}),
        raw_node(L"else", 0x03, L"3:else",
                 {raw_node(L"false-body", 20, L"整型假")}),
        raw_node(L"target", 24, L"目标"),
    };

    ReplayOptions false_options;
    false_options.simulated_variables.boolean_variables[600] = true;
    const SectionExecutionModel false_model =
        make_model(nodes, L"target", false_options);
    const ReplayResult false_result = replay_section_to(
        false_model, L"target", make_profile(), false_options);

    ReplayOptions true_options;
    true_options.simulated_variables.integer_variables[600] = 1;
    const SectionExecutionModel true_model =
        make_model(nodes, L"target", true_options);
    const ReplayResult true_result = replay_section_to(
        true_model, L"target", make_profile(), true_options);

    CCZ_REQUIRE_EQ(false_result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(false_result.state.dialogue.body, std::wstring(L"整型假"));
    CCZ_REQUIRE_EQ(true_result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(true_result.state.dialogue.body, std::wstring(L"整型真"));
}

CCZ_TEST(dialogue_replay_false_integer_predicate_without_else_is_silent) {
    ReplayOptions options;
    options.simulated_variables.integer_variables[600] = 0;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"integer-predicate", 0x79, L"79:变量测试",
                  {raw_node(L"true-body", 20, L"不应进入")},
                  {4, 600, 0, 0, 1}),
         raw_node(L"after-dialogue", 20, L"条件之后"),
         raw_node(L"target", 24, L"目标")},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"条件之后"));
    CCZ_REQUIRE(!has_diagnostic(result.diagnostic, L"无假分支"));
}

CCZ_TEST(dialogue_replay_unsupported_predicate_stops_with_logical_diagnostic) {
    ReplayOptions options;
    options.predicate_states[L"predicate"] = PredicateState::Unsupported;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"before", 20, L"此前"),
         raw_node(L"predicate", 0x36, L"36:武将状态测试",
                  {raw_node(L"true-dialogue", 20, L"真分支")}),
          raw_node(L"target", 24, L"目标")},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);


    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::UnsupportedCondition);
    CCZ_REQUIRE_EQ(result.waiting_node, LogicalNodeKey(L"predicate"));
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"此前"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"不支持"));
}

CCZ_TEST(dialogue_replay_unsupported_predicate_blocks_nested_case_target) {
    ReplayOptions options;
    options.predicate_states[L"unsupported"] = PredicateState::Unsupported;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"outer-choice", 0x12, L"12:外层选择"),
         raw_node(L"outer-case", 0x13, L"13:外层分支",
                  {raw_node(L"inner-choice", 0x12, L"12:内层选择"),
                   raw_node(L"inner-case", 0x13, L"13:内层分支",
                            {raw_node(L"unsupported", 0x36,
                                      L"36:不支持条件",
                                      {raw_node(L"unsupported-body", 20,
                                                L"不应进入")}),
                             raw_node(L"target", 24, L"不应到达")} ),
                   raw_node(L"inner-other-case", 0x13, L"13:内层其他分支")}),
         raw_node(L"outer-other-case", 0x13, L"13:外层其他分支")},
        L"target", options);

    const ExecutionNode& outer_choice = model.body[1];
    const ExecutionNode& inner_choice =
        outer_choice.cases[0].ordinary_children[0];
    CCZ_REQUIRE_EQ(outer_choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(inner_choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(outer_choice.cases[0].choice_state,
                   ChoiceState::Unselected);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.state.dialogue.body.empty());
    CCZ_REQUIRE(result.waiting_node.empty());
}

CCZ_TEST(
    dialogue_replay_unsupported_predicate_does_not_auto_target_after_nested_choice) {
    ReplayOptions options;
    options.predicate_states[L"unsupported"] = PredicateState::Unsupported;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"outer-choice", 0x12, L"12:外层选择"),
         raw_node(L"outer-case", 0x13, L"13:外层分支",
                  {raw_node(L"inner-choice", 0x12, L"12:内层选择"),
                   raw_node(L"inner-case", 0x13, L"13:内层分支",
                            {raw_node(L"unsupported", 0x36,
                                      L"36:不支持条件",
                                      {raw_node(L"unsupported-body", 20,
                                                L"不应进入")})}),
                   raw_node(L"inner-other-case", 0x13, L"13:内层其他分支"),
                   raw_node(L"target", 24, L"终止路径之后的目标")}),
         raw_node(L"outer-other-case", 0x13, L"13:外层其他分支")},
        L"target", options);

    const ExecutionNode& outer_choice = model.body[1];
    CCZ_REQUIRE_EQ(outer_choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(outer_choice.cases[0].choice_state,
                   ChoiceState::Unselected);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.state.dialogue.body.empty());
    CCZ_REQUIRE(result.waiting_node.empty());
}

CCZ_TEST(
    dialogue_replay_terminated_case_blocks_auto_target_with_reachable_sibling) {
    ReplayOptions options;
    options.predicate_states[L"unsupported"] = PredicateState::Unsupported;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"choice", 0x12, L"12:选择"),
         raw_node(L"terminated-case", 0x13, L"13:终止分支",
                  {raw_node(L"unsupported", 0x36, L"36:不支持条件")}),
         raw_node(L"reachable-case", 0x13, L"13:可达分支",
                  {raw_node(L"target", 24, L"目标")})},
        L"target", options);

    const ExecutionNode& choice = model.body[1];
    CCZ_REQUIRE_EQ(choice.choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(choice.cases[0].choice_state, ChoiceState::Unselected);
    CCZ_REQUIRE_EQ(choice.cases[1].choice_state, ChoiceState::Unselected);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::WaitingForChoice);
    CCZ_REQUIRE_EQ(result.waiting_node, LogicalNodeKey(L"choice"));
    CCZ_REQUIRE(result.state.dialogue.body.empty());
}

CCZ_TEST(dialogue_replay_target_predicate_preserves_precondition_state) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"before", 20, L"此前"),
        raw_node(L"predicate", 0x79, L"79:变量测试"),
        raw_node(L"after", 20, L"之后"),
    });

    const ReplayResult result = replay_section_to(
        model, L"predicate", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"此前"));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"before"));
}

CCZ_TEST(dialogue_replay_target_container_preserves_entry_state) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"before", 20, L"此前"),
        raw_node(L"container", 0x02, L"2:内部信息",
                 {raw_node(L"inside", 20, L"不可提前应用")}),
    });

    const ReplayResult result = replay_section_to(
        model, L"container", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"此前"));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"before"));
}

CCZ_TEST(dialogue_replay_face_state_is_inherited_by_following_dialogue) {
    const DialogueProfile profile = make_profile();
    CCZ_REQUIRE_EQ(profile.face_state_instruction_id,
                   static_cast<std::uint16_t>(32));
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"face", 32, L"", {}, {128}),
        raw_node(L"dialogue", 20, L"带状态台词"),
        raw_node(L"target", 24, L"目标"),
    });

    const ReplayResult result = replay_section_to(
        model, L"target", profile, {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE(result.state.face_state_known);
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(128));
    CCZ_REQUIRE(result.state.dialogue.face_state_known);
    CCZ_REQUIRE_EQ(result.state.dialogue.face_state,
                   static_cast<std::uint16_t>(128));
}

CCZ_TEST(dialogue_replay_applies_selected_dialogue_before_stopping) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"before", 20, L"此前"),
        raw_node(L"target", 20, L"选中的台词"),
        raw_node(L"after", 20, L"不应执行")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"选中的台词"));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"target"));
}

CCZ_TEST(dialogue_replay_keeps_last_embedded_dialogue_before_later_target) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"multi", 20, L"&陆坤\n第一段\n&云筱\n第二段"),
        raw_node(L"target", 24, L"目标")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.speaker_label,
                   std::wstring(L"云筱"));
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"第二段"));
}

CCZ_TEST(dialogue_replay_selected_multi_dialogue_starts_at_first_segment) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"target", 20, L"&陆坤\n第一段\n&云筱\n第二段")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.speaker_label,
                   std::wstring(L"陆坤"));
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"第一段"));
}

CCZ_TEST(dialogue_replay_applies_selected_face_state_before_stopping) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"target", 32, L"", {}, {128}),
        raw_node(L"after", 20, L"不应执行")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE(result.state.face_state_known);
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(128));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"target"));
}

CCZ_TEST(dialogue_replay_defaults_face_state_to_zero_without_face_command) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"target", 20, L"&*.174\\n默认头像状态")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(0));
    CCZ_REQUIRE(!result.state.face_state_known);
    CCZ_REQUIRE_EQ(result.state.dialogue.face_state,
                   static_cast<std::uint16_t>(0));
}

CCZ_TEST(dialogue_replay_applies_section_rename_before_data_speaker_dialogue) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"rename-target", 0x77, L"", {}, {2, 4054, 2, 0, 174}),
        raw_node(L"rename-value", 0x72, L"影魇", {}, {0}),
        raw_node(L"dialogue", 20, L"&*.174\\n改名后的台词"),
        raw_node(L"target", 24, L"目标")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.speaker_label, std::wstring(L"影魇"));
    CCZ_REQUIRE_EQ(result.state.data_name_overrides.at(174),
                   std::wstring(L"影魇"));
}

CCZ_TEST(dialogue_replay_uses_last_same_person_rename) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"rename-target-1", 0x77, L"", {}, {2, 4054, 2, 0, 174}),
        raw_node(L"rename-value-1", 0x72, L"旧名", {}, {0}),
        raw_node(L"rename-target-2", 0x77, L"", {}, {2, 4054, 2, 0, 174}),
        raw_node(L"rename-value-2", 0x72, L"新名", {}, {0}),
        raw_node(L"dialogue", 20, L"&*.174\\n正文"),
        raw_node(L"target", 24, L"目标")});

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.state.dialogue.speaker_label, std::wstring(L"新名"));
    CCZ_REQUIRE_EQ(result.state.data_name_overrides.at(174),
                   std::wstring(L"新名"));
}

CCZ_TEST(dialogue_model_keeps_information_narration_and_map_text_plain) {
    const DialogueProfile profile = make_extended_profile();
    for (const std::uint16_t instruction_id : {
             static_cast<std::uint16_t>(0x16u),
             static_cast<std::uint16_t>(0x2cu),
             static_cast<std::uint16_t>(0x69u)}) {
        DialogueCommandView command;
        command.instruction_id = instruction_id;
        command.text = L"&不应解析为说话人\\n纯文本";
        const DialogueState state = decode_dialogue_command(command, profile);
        CCZ_REQUIRE(state.visible);
        CCZ_REQUIRE(state.speaker_label.empty());
        CCZ_REQUIRE_EQ(state.body,
                       std::wstring(L"&不应解析为说话人\n纯文本"));
    }
}

std::vector<RawReplayNode> choice_fixture() {
    return {
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"before-choice", 20, L"选择前"),
        raw_node(L"choice", 0x12, L"12:选择框"),
        raw_node(L"case-a", 0x13, L"13:甲",
                 {raw_node(L"case-a-face", 32, L"", {}, {11}),
                  raw_node(L"case-a-dialogue", 20, L"甲分支"),
                  raw_node(L"case-a-tail", 24, L"甲尾")}),
        raw_node(L"case-b", 0x13, L"13:乙",
                 {raw_node(L"case-b-face", 32, L"", {}, {22}),
                  raw_node(L"case-b-dialogue", 20, L"乙分支"),
                  raw_node(L"case-b-tail", 24, L"乙尾")}),
        raw_node(L"after-choice", 24, L"选择后"),
    };
}

CCZ_TEST(dialogue_replay_unselected_choice_waits_and_preserves_before_state) {
    const SectionExecutionModel model = make_model(choice_fixture());

    const ReplayResult result = replay_section_to(
        model, L"case-b-tail", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::WaitingForChoice);
    CCZ_REQUIRE_EQ(result.waiting_node, LogicalNodeKey(L"choice"));
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"选择前"));
    CCZ_REQUIRE(!result.state.face_state_known);
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"选择"));
}

CCZ_TEST(dialogue_replay_selected_case_enters_only_requested_case) {
    ReplayOptions options;
    options.selected_case_by_choice[L"choice"] = L"case-b";
    const SectionExecutionModel model = make_model(
        choice_fixture(), L"", options);

    const ReplayResult result = replay_section_to(
        model, L"after-choice", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"乙分支"));
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(22));
}

CCZ_TEST(dialogue_replay_auto_target_selects_unique_case_without_first_case_default) {
    const SectionExecutionModel model = make_model(
        choice_fixture(), L"case-b-tail");

    const ReplayResult result = replay_section_to(
        model, L"case-b-tail", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"乙分支"));
    CCZ_REQUIRE_EQ(result.state.face_state, static_cast<std::uint16_t>(22));
}

CCZ_TEST(dialogue_replay_auto_targets_choice_with_marker_children) {
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(
             L"choice", 0x12, L"12:选择框",
             {raw_node(L"case-a-marker", 0x01, L"1:子事件设定"),
              raw_node(L"case-a", 0x13, L"13:甲",
                       {raw_node(L"case-a-dialogue", 20, L"甲分支")}),
              raw_node(L"case-b-marker", 0x01, L"1:子事件设定"),
              raw_node(L"case-b", 0x13, L"13:乙",
                       {raw_node(L"case-b-dialogue", 20, L"乙分支"),
                        raw_node(L"target", 24, L"乙尾")})})},
        L"target");

    const ReplayResult result =
        replay_section_to(model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"乙分支"));
    CCZ_REQUIRE(!has_diagnostic(result.diagnostic, L"缺少连续 Case"));
}

CCZ_TEST(dialogue_replay_handles_two_nested_choices) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"outer-choice", 0x12, L"12:外层选择"),
        raw_node(L"outer-case-a", 0x13, L"13:外层甲",
                 {raw_node(L"outer-a-before", 20, L"外甲前"),
                  raw_node(L"inner-choice", 0x12, L"12:内层选择"),
                  raw_node(L"inner-case-a", 0x13, L"13:内层甲",
                           {raw_node(L"inner-a", 20, L"内甲")}),
                  raw_node(L"inner-case-b", 0x13, L"13:内层乙",
                           {raw_node(L"inner-b", 20, L"内乙"),
                            raw_node(L"inner-target", 24, L"内乙尾")})}),
        raw_node(L"outer-case-b", 0x13, L"13:外层乙",
                 {raw_node(L"outer-b", 20, L"外乙")}),
        raw_node(L"after", 20, L"之后"),
    });
    ReplayOptions options;
    options.selected_case_by_choice[L"outer-choice"] = L"outer-case-a";
    options.selected_case_by_choice[L"inner-choice"] = L"inner-case-b";

    const ReplayResult result = replay_section_to(
        model, L"inner-target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"内乙"));
}

CCZ_TEST(dialogue_replay_branch_switches_start_from_empty_state) {
    const std::vector<RawReplayNode> nodes = {
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"predicate", 0x05, L"5:变量测试",
                 {raw_node(L"true-face", 32, L"", {}, {101}),
                  raw_node(L"true-dialogue", 20, L"真台词")}),
        raw_node(L"else", 0x03, L"3:else",
                 {raw_node(L"false-face", 32, L"", {}, {202}),
                  raw_node(L"false-dialogue", 20, L"假台词")}),
        raw_node(L"target", 24, L"目标"),
    };
    ReplayOptions false_options;
    false_options.predicate_states[L"predicate"] =
        PredicateState::ExplicitFalse;
    ReplayOptions true_options;
    true_options.predicate_states[L"predicate"] =
        PredicateState::ExplicitTrue;
    const SectionExecutionModel model = make_model(nodes);

    const ReplayResult false_result = replay_section_to(
        model, L"target", make_profile(), false_options);
    const ReplayResult true_result = replay_section_to(
        model, L"target", make_profile(), true_options);

    CCZ_REQUIRE_EQ(false_result.state.dialogue.body, std::wstring(L"假台词"));
    CCZ_REQUIRE_EQ(false_result.state.face_state,
                   static_cast<std::uint16_t>(202));
    CCZ_REQUIRE_EQ(true_result.state.dialogue.body, std::wstring(L"真台词"));
    CCZ_REQUIRE_EQ(true_result.state.face_state,
                   static_cast<std::uint16_t>(101));
}

CCZ_TEST(dialogue_replay_case_switches_start_from_empty_state) {
    const SectionExecutionModel model = make_model(choice_fixture());
    ReplayOptions case_a;
    case_a.selected_case_by_choice[L"choice"] = L"case-a";
    ReplayOptions case_b;
    case_b.selected_case_by_choice[L"choice"] = L"case-b";

    const ReplayResult first = replay_section_to(
        model, L"after-choice", make_profile(), case_a);
    const ReplayResult second = replay_section_to(
        model, L"after-choice", make_profile(), case_b);

    CCZ_REQUIRE_EQ(first.state.dialogue.body, std::wstring(L"甲分支"));
    CCZ_REQUIRE_EQ(first.state.face_state, static_cast<std::uint16_t>(11));
    CCZ_REQUIRE_EQ(second.state.dialogue.body, std::wstring(L"乙分支"));
    CCZ_REQUIRE_EQ(second.state.face_state, static_cast<std::uint16_t>(22));
}

CCZ_TEST(dialogue_replay_target_not_found_reports_full_diagnostic) {
    const SectionExecutionModel model = make_model({
        raw_node(L"section-boundary", 0x00, L"0:事件结束"),
        raw_node(L"dialogue", 20, L"已有台词"),
    });

    const ReplayResult result = replay_section_to(
        model, L"missing", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.waiting_node.empty());
    CCZ_REQUIRE(result.last_applied_node == LogicalNodeKey(L"dialogue"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"目标节点不在当前 Section"));
}

CCZ_TEST(dialogue_replay_rejects_incomplete_section_before_applying_nodes) {
    const SectionExecutionModel model = make_model({
        raw_node(L"before", 20, L"此前"),
        raw_node(L"target", 24, L"目标"),
    });

    CCZ_REQUIRE(!model.boundary_complete);
    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), {});

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.state.dialogue.body.empty());
    CCZ_REQUIRE(result.last_applied_node.empty());
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"不完整 Section"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"不可执行"));
}

CCZ_TEST(dialogue_replay_does_not_wait_at_later_choice_for_pruned_target) {
    ReplayOptions options;
    options.predicate_states[L"pruned-predicate"] =
        PredicateState::ExplicitFalse;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"pruned-predicate", 0x05, L"5:已剪枝条件",
                  {raw_node(L"pruned-target", 24, L"剪枝目标")}),
         raw_node(L"later-choice", 0x12, L"12:后续选择"),
         raw_node(L"later-case-a", 0x13, L"13:甲",
                  {raw_node(L"case-a-body", 20, L"甲")}),
         raw_node(L"later-case-b", 0x13, L"13:乙",
                  {raw_node(L"case-b-body", 20, L"乙")})},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"pruned-target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.waiting_node.empty());
    CCZ_REQUIRE(!has_diagnostic(result.diagnostic, L"无假分支"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"回放路径"));
}

CCZ_TEST(dialogue_replay_stops_before_section_tail_for_pruned_target) {
    ReplayOptions options;
    options.predicate_states[L"pruned-predicate"] =
        PredicateState::ExplicitFalse;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"before", 20, L"条件之前"),
         raw_node(L"pruned-predicate", 0x36, L"36:已剪枝条件",
                  {raw_node(L"pruned-target", 24, L"剪枝目标")}),
         raw_node(L"section-tail", 20, L"Section 末尾的不相关台词")},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"pruned-target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"条件之前"));
    CCZ_REQUIRE_EQ(result.last_applied_node, LogicalNodeKey(L"before"));
    CCZ_REQUIRE(has_diagnostic(result.diagnostic, L"回放路径"));
}

CCZ_TEST(dialogue_replay_does_not_wait_for_a_pruned_case_target) {
    ReplayOptions options;
    options.predicate_states[L"case-predicate"] =
        PredicateState::ExplicitFalse;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"choice", 0x12, L"12:选择"),
         raw_node(L"case-a", 0x13, L"13:甲",
                  {raw_node(L"case-predicate", 0x05, L"5:甲条件",
                            {raw_node(L"pruned-case-target", 24,
                                      L"剪枝 Case 目标")})}),
         raw_node(L"case-b", 0x13, L"13:乙",
                  {raw_node(L"case-b-body", 20, L"乙")})},
        L"", options);

    const ReplayResult result = replay_section_to(
        model, L"pruned-case-target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::TargetNotFound);
    CCZ_REQUIRE(result.waiting_node.empty());
}

CCZ_TEST(dialogue_replay_enters_selected_integer_dispatch_member) {
    ReplayOptions options;
    options.simulated_variables.integer_variables[600] = 2;
    const SectionExecutionModel model = make_model(
        {raw_node(L"boundary", 0x00, L"0:边界"),
         raw_node(L"case-1", 0x79, L"79:Var600 == 1",
                  {raw_node(L"case-1-body", 20, L"甲")},
                  {4, 600, 0, 0, 1}),
         raw_node(L"case-2", 0x79, L"79:Var600 == 2",
                  {raw_node(L"case-2-body", 20, L"乙")},
                  {4, 600, 0, 0, 2}),
         raw_node(L"case-3", 0x79, L"79:Var600 == 3",
                  {raw_node(L"case-3-body", 20, L"丙")},
                  {4, 600, 0, 0, 3}),
         raw_node(L"target", 24, L"目标")},
        L"target", options);

    const ReplayResult result = replay_section_to(
        model, L"target", make_profile(), options);

    CCZ_REQUIRE_EQ(result.reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(result.state.dialogue.body, std::wstring(L"乙"));
    CCZ_REQUIRE_EQ(result.last_applied_node,
                   LogicalNodeKey(L"case-2-body"));
}
