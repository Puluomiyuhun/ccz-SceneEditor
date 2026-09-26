#include "test_framework.h"

#include <ccz/preview/native_replay_plan.h>

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

std::vector<std::int32_t> boolean_expression_data(
    std::int32_t required_true_variable) {
    std::vector<std::int32_t> values(50, -1);
    values[0] = required_true_variable;
    return values;
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
    catalog.names.emplace(static_cast<std::uint16_t>(0x24), L"普通事件");
    catalog.names.emplace(static_cast<std::uint16_t>(0x27), L"背景显示");
    catalog.names.emplace(static_cast<std::uint16_t>(0x30), L"人物出现");
    catalog.names.emplace(static_cast<std::uint16_t>(0x36), L"武将状态测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x79), L"变量测试");
    return catalog;
}

SectionExecutionModel make_model(
    std::vector<RawReplayNode> nodes,
    const wchar_t* selected_target,
    const ReplayOptions& options) {
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

CCZ_TEST(native_replay_plan_includes_scene_effect_from_nested_condition) {
    ReplayOptions options;
    options.simulated_variables.boolean_variables[600] = true;
    options.simulated_variables.integer_variables[601] = 1;

    const RawReplayNode nested_integer = raw_node(
        L"nested-integer", 0x79, L"79:整型条件",
        {raw_node(L"container", 0x02, L"2:内部信息",
                  {raw_node(L"b-actor", 0x30, L"48:人物出现", {},
                             {7, 20, 30, 2, 0})})},
        {4, 601, 0, 0, 1});
    const RawReplayNode outer_boolean = raw_node(
        L"outer-boolean", 0x05, L"5:布尔条件", {nested_integer},
        boolean_expression_data(600));
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         outer_boolean, raw_node(L"c-target", 0x24, L"C")},
        L"c-target", options);

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, options);

    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE_EQ(plan.stop_reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(plan.items.front().logical_key,
                   LogicalNodeKey(L"b-actor"));
    CCZ_REQUIRE_EQ(plan.items.front().runtime_handle,
                   RuntimeNodeHandle(0));
    CCZ_REQUIRE_EQ(plan.items.front().command.instruction_id,
                   static_cast<std::uint16_t>(0x30));
}

CCZ_TEST(native_replay_plan_to_end_follows_selected_condition_path) {
    ReplayOptions options;
    options.simulated_variables.boolean_variables[600] = true;
    const RawReplayNode gate = raw_node(
        L"gate", 0x05, L"5:条件", {
            raw_node(L"true-actor", 0x30, L"48:人物出现", {},
                     {7, 20, 30, 2, 0}),
        }, boolean_expression_data(600));
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), gate,
         raw_node(L"else", 0x03, L"3:else",
                  {raw_node(L"false-actor", 0x30, L"48:人物出现", {},
                            {8, 20, 30, 2, 0})}),
         raw_node(L"tail", 0x27, L"27:背景显示", {}, {4})},
        L"tail", options);

    const NativeReplayPlan plan = build_native_replay_plan_to_end(
        model, DialogueProfile{}, options);

    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.items[0].logical_key, LogicalNodeKey(L"true-actor"));
    CCZ_REQUIRE_EQ(plan.items[1].logical_key, LogicalNodeKey(L"tail"));
}

CCZ_TEST(native_replay_plan_preserves_current_generation_runtime_handle) {
    ReplayOptions options;
    RawReplayNode actor = raw_node(
        L"actor", 0x30, L"48:人物出现", {}, {7, 20, 30, 2, 0});
    actor.runtime_handle = static_cast<RuntimeNodeHandle>(0x1234);
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), actor,
         raw_node(L"c-target", 0x24, L"C")},
        L"c-target", options);

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, options);

    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(plan.items.front().runtime_handle,
                   RuntimeNodeHandle(0x1234));
}

CCZ_TEST(native_replay_plan_omits_scene_effect_from_false_branch) {
    ReplayOptions options;
    options.simulated_variables.boolean_variables[600] = false;

    const RawReplayNode predicate = raw_node(
        L"predicate", 0x05, L"5:布尔条件",
        {raw_node(L"true-actor", 0x30, L"48:真分支人物出现", {},
                  {7, 20, 30, 2, 0})},
        boolean_expression_data(600));
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), predicate,
         raw_node(L"c-target", 0x24, L"C")},
        L"c-target", options);

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, options);

    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE(plan.items.empty());
}

CCZ_TEST(native_replay_plan_79_predicate_overrides_all_states) {
    const struct Case {
        PredicateState state;
        std::size_t expected_items;
        ReplayStopReason expected_stop;
    } cases[] = {
        {PredicateState::ExplicitTrue, 1, ReplayStopReason::ReachedTarget},
        {PredicateState::ExplicitFalse, 1, ReplayStopReason::ReachedTarget},
        {PredicateState::Unsupported, 0,
         ReplayStopReason::UnsupportedCondition},
    };

    for (const Case& value : cases) {
        ReplayOptions options;
        options.predicate_states[L"predicate"] = value.state;
        const SectionExecutionModel model = make_model(
            {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
             raw_node(L"predicate", 0x79, L"79:旧文本",
                      {raw_node(L"true-actor", 0x30, L"48:真分支人物出现",
                                {}, {7, 20, 30, 2, 0})},
                      {4, 600, 2, 0, 10}),
             raw_node(L"else", 0x03, L"3:else",
                      {raw_node(L"false-actor", 0x30, L"48:假分支人物出现",
                                {}, {8, 21, 31, 2, 0})}),
             raw_node(L"target", 0x24, L"24:目标")},
            L"target", options);

        const NativeReplayPlan plan = build_native_replay_plan(
            model, L"target", DialogueProfile{}, options);
        CCZ_REQUIRE_EQ(plan.stop_reason, value.expected_stop);
        CCZ_REQUIRE_EQ(plan.items.size(), value.expected_items);
    }
}

CCZ_TEST(native_replay_plan_skips_unresolved_choice_and_reports_diagnostic) {
    const RawReplayNode choice = raw_node(
        L"choice", 0x12, L"12:选择",
        {raw_node(L"case-a", 0x13, L"13:甲",
                  {raw_node(L"unresolved-actor", 0x30, L"48:人物出现", {},
                             {99, 1, 2, 0, 0})})});
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"before", 0x30, L"48:人物出现", {},
                  {7, 20, 30, 2, 0}),
         choice,
         raw_node(L"after", 0x30, L"48:人物出现", {},
                  {8, 21, 31, 2, 0}),
         raw_node(L"c-target", 0x24, L"C")},
        L"c-target", {});

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, {});

    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.items[0].logical_key, LogicalNodeKey(L"before"));
    CCZ_REQUIRE_EQ(plan.items[1].logical_key, LogicalNodeKey(L"after"));
    CCZ_REQUIRE(has_diagnostic(plan.diagnostic, L"包含未决选择支事件"));
}

CCZ_TEST(native_replay_plan_does_not_enter_unresolved_case_target) {
    const RawReplayNode choice = raw_node(
        L"choice", 0x12, L"12:选择",
        {raw_node(L"case-a", 0x13, L"13:甲",
                  {raw_node(L"case-target", 0x30, L"48:人物出现", {},
                             {7, 1, 2, 0, 0})})});
    SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), choice},
        L"case-target", {});
    // Override the model's target-based auto selection to exercise the
    // genuinely unresolved Choice path.
    model.body[1].choice_state = ChoiceState::Unselected;
    model.body[1].cases[0].choice_state = ChoiceState::Unselected;

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"case-target", DialogueProfile{}, {});

    CCZ_REQUIRE(!plan.target_reached);
    CCZ_REQUIRE(!plan.complete);
    CCZ_REQUIRE_EQ(plan.stop_reason, ReplayStopReason::WaitingForChoice);
    CCZ_REQUIRE(plan.items.empty());
    CCZ_REQUIRE(has_diagnostic(plan.diagnostic, L"包含未决选择支事件"));
}

CCZ_TEST(native_replay_plan_enters_selected_choice_case) {
    ReplayOptions options;
    options.selected_case_by_choice[L"choice"] = L"case-a";
    const RawReplayNode choice = raw_node(
        L"choice", 0x12, L"12:选择",
        {raw_node(L"case-a", 0x13, L"13:甲",
                  {raw_node(L"selected-actor", 0x30, L"48:人物出现", {},
                             {7, 20, 30, 2, 0})})});
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), choice,
         raw_node(L"c-target", 0x24, L"C")},
        L"c-target", options);

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, options);

    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(plan.items.front().logical_key,
                   LogicalNodeKey(L"selected-actor"));
    CCZ_REQUIRE(!has_diagnostic(plan.diagnostic, L"包含未决选择支事件"));
}

CCZ_TEST(native_replay_plan_stops_on_unsupported_condition) {
    ReplayOptions options;
    options.predicate_states[L"unsupported"] = PredicateState::Unsupported;
    const RawReplayNode predicate = raw_node(
        L"unsupported", 0x36, L"36:武将状态测试",
        {raw_node(L"blocked-actor", 0x30, L"48:人物出现", {},
                  {7, 20, 30, 2, 0})});
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), predicate,
         raw_node(L"c-target", 0x24, L"C")},
        L"c-target", options);

    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"c-target", DialogueProfile{}, options);

    CCZ_REQUIRE(!plan.target_reached);
    CCZ_REQUIRE(!plan.complete);
    CCZ_REQUIRE_EQ(plan.stop_reason, ReplayStopReason::UnsupportedCondition);
    CCZ_REQUIRE(plan.items.empty());
    CCZ_REQUIRE(has_diagnostic(plan.diagnostic, L"条件节点的静态回放不支持"));
}
