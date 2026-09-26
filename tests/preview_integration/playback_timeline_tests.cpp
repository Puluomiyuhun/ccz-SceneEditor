#include "test_framework.h"

#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/playback_timeline.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ccz::preview;

RawReplayNode raw_node(const wchar_t* key, std::uint16_t instruction_id,
                       std::vector<RawReplayNode> children = {},
                       std::vector<std::int32_t> int_data = {},
                       const wchar_t* text = L"") {
    RawReplayNode node;
    node.logical_key = key;
    node.instruction_id = instruction_id;
    node.command.instruction_id = instruction_id;
    node.command.int_data = std::move(int_data);
    node.command.text = text;
    node.source_text = key;
    node.children = std::move(children);
    return node;
}

InstructionCatalog catalog() {
    InstructionCatalog value;
    value.valid = true;
    value.names = {{0x00, L"事件结束"}, {0x12, L"选择框"},
                   {0x13, L"case"},       {0x14, L"对话"},
                   {0x09, L"延时"},        {0x29, L"地图头像显示"},
                   {0x2a, L"地图头像移动"}, {0x30, L"武将出现"},
                   {0x32, L"武将移动"}, {0x1c, L"绘图"}};
    return value;
}

SectionExecutionModel model_for(std::vector<RawReplayNode> nodes,
                                const LogicalNodeKey& target,
                                ReplayOptions& options) {
    SectionInput input;
    input.section_key = L"fixture|section";
    input.selected_target_key = target;
    input.nodes = std::move(nodes);
    SectionExecutionModel model =
        build_section_execution_model(input, catalog(), options);
    return model;
}

}  // namespace

CCZ_TEST(playback_timeline_start_to_target_includes_target_once) {
    ReplayOptions options;
    const LogicalNodeKey target = L"move";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"show", 0x30, {}, {1, 10, 10, 2, 0}),
         raw_node(L"move", 0x32, {}, {0, 1, 0, 12, 10, 1}),
         raw_node(L"after", 0x30, {}, {2, 20, 20, 2, 0})},
        target, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{3});
    CCZ_REQUIRE_EQ(timeline.events[0].logical_key, LogicalNodeKey(L"show"));
    CCZ_REQUIRE_EQ(timeline.events[1].logical_key, target);
    CCZ_REQUIRE_EQ(timeline.events[2].logical_key, LogicalNodeKey(L"after"));
    CCZ_REQUIRE_EQ(timeline.target_index, std::size_t{1});
    CCZ_REQUIRE_EQ(timeline.range_begin, std::size_t{0});
    CCZ_REQUIRE_EQ(timeline.range_end, std::size_t{2});
}

CCZ_TEST(playback_timeline_target_to_end_starts_at_target) {
    ReplayOptions options;
    const LogicalNodeKey target = L"move";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"show", 0x30, {}, {1, 10, 10, 2, 0}),
         raw_node(L"move", 0x32, {}, {0, 1, 0, 12, 10, 1}),
         raw_node(L"dialogue", 0x14),
         raw_node(L"after", 0x30, {}, {2, 20, 20, 2, 0})},
        target, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::TargetToEnd, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{4});
    CCZ_REQUIRE_EQ(timeline.events.front().logical_key, LogicalNodeKey(L"show"));
    CCZ_REQUIRE_EQ(timeline.events[1].logical_key, target);
    CCZ_REQUIRE_EQ(timeline.events[2].logical_key,
                   LogicalNodeKey(L"dialogue"));
    CCZ_REQUIRE_EQ(timeline.events[2].kind, PlaybackSegmentKind::Hold);
    CCZ_REQUIRE_EQ(timeline.events.back().logical_key,
                   LogicalNodeKey(L"after"));
    CCZ_REQUIRE_EQ(timeline.target_index, std::size_t{1});
    CCZ_REQUIRE_EQ(timeline.range_begin, std::size_t{1});
    CCZ_REQUIRE_EQ(timeline.range_end, std::size_t{4});
    CCZ_REQUIRE_EQ(timeline.pre_target_key, LogicalNodeKey(L"show"));
}

CCZ_TEST(playback_timeline_expands_multi_dialogue_node_and_target_span) {
    ReplayOptions options;
    const LogicalNodeKey target = L"dialogue";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"before", 0x30, {}, {1, 10, 10, 2, 0}),
         raw_node(L"dialogue", 0x14, {}, {},
                  L"&陆坤\n哎。\n&云筱\n久闻大名。"),
         raw_node(L"after", 0x30, {}, {2, 20, 20, 2, 0})},
        target, options);

    const PlaybackTimeline from_start = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);
    CCZ_REQUIRE(from_start.complete);
    CCZ_REQUIRE_EQ(from_start.events.size(), std::size_t{4});
    CCZ_REQUIRE_EQ(from_start.target_index, std::size_t{1});
    CCZ_REQUIRE_EQ(from_start.target_end_index, std::size_t{3});
    CCZ_REQUIRE_EQ(from_start.range_begin, std::size_t{0});
    CCZ_REQUIRE_EQ(from_start.range_end, std::size_t{3});
    CCZ_REQUIRE_EQ(from_start.events[1].dialogue_segment_index,
                   std::size_t{0});
    CCZ_REQUIRE_EQ(from_start.events[2].dialogue_segment_index,
                   std::size_t{1});
    CCZ_REQUIRE_EQ(from_start.events[1].dialogue_segment_count,
                   std::size_t{2});

    const PlaybackTimeline to_end = build_playback_timeline(
        model, target, PlaybackRange::TargetToEnd, options);
    CCZ_REQUIRE(to_end.complete);
    CCZ_REQUIRE_EQ(to_end.range_begin, std::size_t{1});
    CCZ_REQUIRE_EQ(to_end.range_end, std::size_t{4});
    CCZ_REQUIRE_EQ(to_end.target_index, std::size_t{1});
    CCZ_REQUIRE_EQ(to_end.target_end_index, std::size_t{3});
}

CCZ_TEST(playback_timeline_classifies_outer_portrait_move_and_delay) {
    ReplayOptions options;
    const LogicalNodeKey target = L"portrait-move";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"portrait-show", 0x29, {}, {124, 320, 50}),
         raw_node(L"delay", 0x09, {}, {3}),
         raw_node(L"portrait-move", 0x2a, {}, {124, 420, 80})},
        target, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{3});
    CCZ_REQUIRE_EQ(timeline.events[0].kind, PlaybackSegmentKind::Instant);
    CCZ_REQUIRE_EQ(timeline.events[1].kind, PlaybackSegmentKind::Instant);
    CCZ_REQUIRE_EQ(timeline.events[2].kind, PlaybackSegmentKind::PortraitMove);
    CCZ_REQUIRE_EQ(timeline.events[2].duration_units, std::int32_t{1});
}

CCZ_TEST(playback_timeline_classifies_draw_as_timed_hold) {
    ReplayOptions options;
    const LogicalNodeKey target = L"draw";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00), raw_node(L"draw", 0x1c)}, target,
        options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(timeline.events.front().kind, PlaybackSegmentKind::Hold);
    CCZ_REQUIRE_EQ(timeline.events.front().duration_units, std::int32_t{1});
}

CCZ_TEST(playback_timeline_default_case1_excludes_case2) {
    ReplayOptions options;
    const LogicalNodeKey target = L"case-one-body";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"choice", 0x12,
                  {raw_node(L"case-one", 0x13,
                            {raw_node(L"case-one-body", 0x30)}),
                   raw_node(L"case-two", 0x13,
                            {raw_node(L"case-two-body", 0x30)})})},
        target, options);
    initialize_default_choice_selections(model, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(timeline.events.front().logical_key, target);
}

CCZ_TEST(playback_timeline_rejects_inactive_case_target) {
    ReplayOptions options;
    const LogicalNodeKey target = L"case-two-body";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"choice", 0x12,
                  {raw_node(L"case-one", 0x13,
                            {raw_node(L"case-one-body", 0x30)}),
                   raw_node(L"case-two", 0x13,
                            {raw_node(L"case-two-body", 0x30)})})},
        target, options);
    initialize_default_choice_selections(model, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(!timeline.complete);
    CCZ_REQUIRE(!timeline.diagnostic.empty());
}
