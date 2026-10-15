#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattleDynamicMapReplay.h"

#include <ccz/preview/native_replay_plan.h>

#include <algorithm>
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
    catalog.names.emplace(static_cast<std::uint16_t>(0x01), L"子事件设定");
    catalog.names.emplace(static_cast<std::uint16_t>(0x02), L"内部信息");
    catalog.names.emplace(static_cast<std::uint16_t>(0x03), L"else");
    catalog.names.emplace(static_cast<std::uint16_t>(0x05), L"变量测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x12), L"选择框");
    catalog.names.emplace(static_cast<std::uint16_t>(0x13), L"case");
    catalog.names.emplace(static_cast<std::uint16_t>(0x24), L"普通事件");
    catalog.names.emplace(static_cast<std::uint16_t>(0x27), L"背景显示");
    catalog.names.emplace(static_cast<std::uint16_t>(0x30), L"人物出现");
    catalog.names.emplace(static_cast<std::uint16_t>(0x31), L"武将消失");
    catalog.names.emplace(static_cast<std::uint16_t>(0x36), L"武将状态测试");
    catalog.names.emplace(static_cast<std::uint16_t>(0x32), L"武将移动");
    catalog.names.emplace(static_cast<std::uint16_t>(0x4c), L"隐藏武将出现");
    catalog.names.emplace(static_cast<std::uint16_t>(0x55), L"战场复活");
    catalog.names.emplace(static_cast<std::uint16_t>(0x58), L"战场障碍设定");
    catalog.names.emplace(static_cast<std::uint16_t>(0x6d), L"相对移动");
    catalog.names.emplace(static_cast<std::uint16_t>(0x77), L"变量运算");
    catalog.names.emplace(static_cast<std::uint16_t>(0x78), L"整型变量赋值");
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

CCZ_TEST(r_native_replay_uses_zero_for_unavailable_game_property) {
    ReplayOptions options;
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"seed", 0x77, L"77:设置 n0", {}, {2, 0, 2, 0, 343}),
         raw_node(L"read", 0x78, L"78:读取属性", {}, {0, 0, 0, 16}),
         raw_node(L"check", 0x79, L"79:整型测试",
                  {raw_node(L"target", 0x27, L"27:背景显示", {}, {4})},
                  {4, 0, 0, 0, 0})},
        L"target", options);
    const NativeReplayPlan plan = build_native_replay_plan(
        model, L"target", DialogueProfile{}, options);
    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE(plan.target_reached);
    const NativeReplayPlan battle = build_native_battle_map_plan_to_end(
        model, DialogueProfile{}, options);
    CCZ_REQUIRE(!battle.complete);
    CCZ_REQUIRE_EQ(battle.stop_reason, ReplayStopReason::UnsupportedCondition);
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

CCZ_TEST(native_battle_map_plan_preserves_dynamic_map_commands) {
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"move", 0x32, L"32:武将移动", {}, {0, 47, 0, 5, 6, 2}),
         raw_node(L"relative-move", 0x6d, L"6d:相对移动", {}, {0, 48, 47}),
         raw_node(L"revive", 0x55, L"55:战场复活", {}, {0, 47, 0, 8, 9, 3}),
         raw_node(L"obstacle", 0x58, L"58:战场障碍设定", {},
                  {2, 0, 26, 7, 8, 0, 0})},
        L"", {});

    const NativeReplayPlan standard_plan =
        build_native_replay_plan_to_end(model, DialogueProfile{}, {});
    CCZ_REQUIRE_EQ(standard_plan.items.size(), std::size_t(1));

    // S uses an explicit native plan mode for supported battlefield map
    // operations. Relative movement (0x6d) is intentionally omitted.
    const NativeReplayPlan plan =
        build_native_battle_map_plan_to_end(
            model, DialogueProfile{}, {});
    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(plan.items[0].command.instruction_id,
                   static_cast<std::uint16_t>(0x32));
    CCZ_REQUIRE_EQ(plan.items[1].command.instruction_id,
                   static_cast<std::uint16_t>(0x55));
    CCZ_REQUIRE_EQ(plan.items[2].command.instruction_id,
                   static_cast<std::uint16_t>(0x58));

    ccz::preview::BattlePreviewState baseline;
    baseline.map.valid = true;
    baseline.map.width = 19;
    baseline.map.height = 18;
    ccz::preview::BattleUnit unit;
    unit.data_id = 47;
    unit.fields_decoded = true;
    baseline.units.push_back(unit);
    std::vector<ccz::editor_preview::BattleDynamicObstacle> obstacles;
    bool obstacle_baseline_complete = false;
    std::wstring obstacle_diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleDynamicObstacleBaseline(
        plan, baseline.map, obstacles, obstacle_baseline_complete,
        obstacle_diagnostic));
    CCZ_REQUIRE(obstacle_baseline_complete);
    CCZ_REQUIRE_EQ(obstacles.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(obstacles.front().type, 2);
    CCZ_REQUIRE_EQ(obstacles.front().coordinate,
                   (ccz::preview::BattleCoord{7, 8}));
    const auto projection = ccz::editor_preview::BuildBattleDynamicMapPlan(
        plan, baseline);
    CCZ_REQUIRE(projection.path_complete);
    CCZ_REQUIRE_EQ(projection.events.size(), std::size_t(3));
}

CCZ_TEST(native_battle_map_plan_adds_hidden_appear_only_to_s_map_scope) {
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"s-disappear", 0x31, L"31:武将消失", {},
                  {0, 47, 0, 0, 0, 0, 0}),
         raw_node(L"s-hidden-appear", 0x4c, L"4c:隐藏武将出现", {},
                  {0, 47, -1})},
        L"", {});

    const NativeReplayPlan standard_plan =
        build_native_replay_plan_to_end(model, DialogueProfile{}, {});
    const auto has_instruction = [](const NativeReplayPlan& plan,
                                    std::uint16_t instruction_id) {
        return std::any_of(plan.items.begin(), plan.items.end(),
            [instruction_id](const NativeReplayItem& item) {
                return item.command.instruction_id == instruction_id;
            });
    };
    CCZ_REQUIRE(has_instruction(standard_plan, 0x31));
    CCZ_REQUIRE(!has_instruction(standard_plan, 0x4c));

    const NativeReplayPlan battle_map_plan =
        build_native_battle_map_plan_to_end(model, DialogueProfile{}, {});
    CCZ_REQUIRE(has_instruction(battle_map_plan, 0x31));
    CCZ_REQUIRE(has_instruction(battle_map_plan, 0x4c));
}

CCZ_TEST(native_battle_map_plan_stops_at_selected_instruction) {
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"move", 0x32, L"32:武将移动", {}, {0, 47, 0, 5, 6, 2}),
         raw_node(L"relative-move", 0x6d, L"6d:相对移动", {}, {0, 48, 47}),
         raw_node(L"revive", 0x55, L"55:战场复活", {}, {0, 47, 0, 8, 9, 3}),
         raw_node(L"obstacle", 0x58, L"58:战场障碍设定", {},
                  {2, 0, 26, 7, 8, 0, 0})},
        L"obstacle", {});

    const NativeReplayPlan plan = build_native_battle_map_plan_through_target(
        model, L"revive", DialogueProfile{}, {});

    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE_EQ(plan.stop_reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.items[0].command.instruction_id,
                   static_cast<std::uint16_t>(0x32));
    CCZ_REQUIRE_EQ(plan.items[1].command.instruction_id,
                   static_cast<std::uint16_t>(0x55));
}

CCZ_TEST(native_battle_map_plan_keeps_revives_before_selected_marker) {
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         raw_node(L"revive-group-marker", 0x01, L"1:子事件设定",
             {raw_node(L"revive-group", 0x02, L"2:内部信息",
                 {raw_node(L"revive-a", 0x55, L"55:战场复活", {},
                            {0, 47, 0, 8, 9, 3}),
                  raw_node(L"revive-b", 0x55, L"55:战场复活", {},
                            {0, 48, 0, 10, 11, 2}),
                  raw_node(L"revive-group-end", 0x00, L"0:事件结束")})}),
         raw_node(L"selected-marker", 0x01, L"1:子事件设定",
             {raw_node(L"following-group", 0x02, L"2:内部信息",
                       {raw_node(L"after-target", 0x55, L"55:战场复活", {},
                                  {0, 49, 0, 12, 13, 1}),
                        raw_node(L"following-group-end", 0x00,
                                 L"0:事件结束")})})},
        L"selected-marker", {});

    const NativeReplayPlan plan = build_native_battle_map_plan_through_target(
        model, L"selected-marker", DialogueProfile{}, {});

    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE(plan.target_reached);
    CCZ_REQUIRE_EQ(plan.stop_reason, ReplayStopReason::ReachedTarget);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.items[0].logical_key, LogicalNodeKey(L"revive-a"));
    CCZ_REQUIRE_EQ(plan.items[1].logical_key, LogicalNodeKey(L"revive-b"));
}

CCZ_TEST(native_battle_map_target_plan_follows_condition_tree) {
    ReplayOptions options;
    options.simulated_variables.boolean_variables[600] = true;
    const RawReplayNode conditional = raw_node(
        L"condition", 0x05, L"5:布尔条件",
        {raw_node(L"true-move", 0x32, L"32:武将移动", {},
                  {0, 47, 0, 5, 6, 2})},
        boolean_expression_data(600));
    const SectionExecutionModel model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"),
         conditional,
         raw_node(L"else", 0x03, L"3:else",
             {raw_node(L"false-revive", 0x55, L"55:战场复活", {},
                       {0, 47, 0, 8, 9, 3})}),
         raw_node(L"target-obstacle", 0x58, L"58:战场障碍设定", {},
                  {2, 0, 26, 7, 8, 0, 0})},
        L"target-obstacle", options);

    const NativeReplayPlan plan = build_native_battle_map_plan_through_target(
        model, L"target-obstacle", DialogueProfile{}, options);

    CCZ_REQUIRE(plan.complete);
    CCZ_REQUIRE_EQ(plan.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.items[0].logical_key,
                   LogicalNodeKey(L"true-move"));
    CCZ_REQUIRE_EQ(plan.items[1].logical_key,
                   LogicalNodeKey(L"target-obstacle"));
}

CCZ_TEST(native_battle_map_plan_respects_condition_tree_branch) {
    const RawReplayNode condition = raw_node(
        L"map-condition", 0x05, L"5:布尔条件",
        {raw_node(L"move", 0x32, L"32:武将移动", {},
                  {0, 47, 0, 5, 6, 2}),
         raw_node(L"revive", 0x55, L"55:战场复活", {},
                  {0, 47, 0, 8, 9, 3}),
         raw_node(L"obstacle", 0x58, L"58:战场障碍设定", {},
                  {2, 0, 26, 7, 8, 0, 0})},
        boolean_expression_data(600));

    ReplayOptions false_options;
    false_options.simulated_variables.boolean_variables[600] = false;
    const auto false_model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), condition},
        L"", false_options);
    const NativeReplayPlan false_plan =
        build_native_battle_map_plan_to_end(
            false_model, DialogueProfile{}, false_options);
    CCZ_REQUIRE(false_plan.complete);
    CCZ_REQUIRE(false_plan.items.empty());

    ReplayOptions true_options;
    true_options.simulated_variables.boolean_variables[600] = true;
    const auto true_model = make_model(
        {raw_node(L"section-boundary", 0x00, L"0:事件结束"), condition},
        L"", true_options);
    const NativeReplayPlan true_plan =
        build_native_battle_map_plan_to_end(
            true_model, DialogueProfile{}, true_options);
    CCZ_REQUIRE(true_plan.complete);
    CCZ_REQUIRE_EQ(true_plan.items.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(true_plan.items[0].command.instruction_id,
                   static_cast<std::uint16_t>(0x32));
    CCZ_REQUIRE_EQ(true_plan.items[1].command.instruction_id,
                   static_cast<std::uint16_t>(0x55));
    CCZ_REQUIRE_EQ(true_plan.items[2].command.instruction_id,
                   static_cast<std::uint16_t>(0x58));
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
CCZ_TEST(native_replay_battle_turn_action_only_collected_when_s_mode_enabled) {
 auto catalog=fixture_catalog();catalog.names[0x4f]=L"战场转向";catalog.names[0x50]=L"战场动作";
 ReplayOptions options;
 SectionInput input;input.section_key=L"regular-S";input.selected_target_key=L"end";input.nodes={raw_node(L"boundary",0x00,L"0:事件结束"),raw_node(L"turn",0x4f,L"",{},{7,-1,0,0,1,1}),raw_node(L"action",0x50,L"",{},{7,6,1,1}),raw_node(L"end",0x24,L"")};
 const auto model=build_section_execution_model(input,catalog,options);
 const auto r=build_native_replay_plan(model,L"end",DialogueProfile{},options);
 CCZ_REQUIRE(r.target_reached);CCZ_REQUIRE(r.items.empty());
 const auto s=build_native_battle_map_plan_through_target(model,L"end",DialogueProfile{},options);
 CCZ_REQUIRE(s.target_reached);CCZ_REQUIRE_EQ(s.items.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(s.items[0].command.instruction_id,std::uint16_t(0x4f));CCZ_REQUIRE_EQ(s.items[1].command.instruction_id,std::uint16_t(0x50));
}

CCZ_TEST(native_replay_battle_appearance_direct_and_variable_commands_share_state) {
 auto catalog=fixture_catalog();
 ReplayOptions options;
 SectionInput input;input.section_key=L"appearance";input.selected_target_key=L"end";
 input.nodes={raw_node(L"boundary",0x00,L"0:事件结束"),
  raw_node(L"direct",0x75,L"75:S特殊形象指定",{},{823,12}),
  raw_node(L"value",0x77,L"77:变量运算",{},{2,0,2,0,343}),
  raw_node(L"write",0x78,L"78:整型变量赋值",{},{0,1,823,27}),
  raw_node(L"end",0x24,L"24:CD音轨")};
 const auto model=build_section_execution_model(input,catalog,options);
 SimulationEnvironment result;
 const auto plan=build_native_battle_map_plan_to_end(model,DialogueProfile{},options,&result);
 CCZ_REQUIRE(plan.complete);
 CCZ_REQUIRE(result.data_attributes.at({823,27})==343);
}
