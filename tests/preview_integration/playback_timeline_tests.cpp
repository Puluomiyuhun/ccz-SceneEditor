#include "test_framework.h"

#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/playback_timeline.h>
#include <PreviewIntegration/DynamicPlaybackSession.h>
#include <PreviewIntegration/PlaybackInterpolation.h>

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
                   {0x7a,L"对话3"},{0x77,L"变量运算"},{0x79,L"变量测试"}, {0x13, L"case"},       {0x14, L"对话"},
                   {0x09, L"延时"},        {0x29, L"地图头像显示"},
                   {0x2a, L"地图头像移动"}, {0x30, L"武将出现"},
                   {0x32, L"武将移动"}, {0x33, L"武将转向"},
                   {0x34, L"武将动作"}, {0x1c, L"绘图"}};
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

CCZ_TEST(playback_timeline_holds_character_turn_and_action_instructions) {
    ReplayOptions options;
    const LogicalNodeKey target = L"action";
    const SectionExecutionModel model = model_for(
        {raw_node(L"boundary", 0x00),
         raw_node(L"appear", 0x30, {}, {1, 10, 10, 2, 0}),
         raw_node(L"turn", 0x33, {}, {1, 0, 3, 0, 0}),
         raw_node(L"action", 0x34, {}, {1, 6, 0, 0, 0})},
        target, options);

    const PlaybackTimeline timeline = build_playback_timeline(
        model, target, PlaybackRange::StartToTarget, options);

    CCZ_REQUIRE(timeline.complete);
    CCZ_REQUIRE_EQ(timeline.events.size(), std::size_t{3});
    CCZ_REQUIRE_EQ(timeline.events[1].kind, PlaybackSegmentKind::Hold);
    CCZ_REQUIRE_EQ(timeline.events[1].duration_units, std::int32_t{1});
    CCZ_REQUIRE_EQ(timeline.events[2].kind, PlaybackSegmentKind::Hold);
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

CCZ_TEST(playback_timeline_dialogue3_has_one_event_per_page) {
 ReplayOptions options;
 auto model=model_for({raw_node(L"boundary",0x00),raw_node(L"dialogue",0x7a,{},{},L"&4,0,0,-1,-1\n一句\n&0&&\n二句\n&&")},L"dialogue",options);
 auto timeline=build_playback_timeline(model,L"dialogue",PlaybackRange::StartToTarget,options);
 CCZ_REQUIRE(timeline.complete);CCZ_REQUIRE_EQ(timeline.events.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(timeline.events[1].dialogue_segment_index,std::size_t(1));CCZ_REQUIRE_EQ(timeline.events[1].dialogue_segment_count,std::size_t(2));
}

PlaybackTimeline actual_play_button_timeline(const SectionExecutionModel& model,
    const LogicalNodeKey& key, PlaybackRange range, const ReplayOptions& options) {
    struct Frame {const SectionExecutionModel* model;LogicalNodeKey active_node;} frame{&model,key};
    struct Session {const ReplayOptions& value;const ReplayOptions& options() const{return value;}} m_previewSession{options};
    PlaybackTimeline m_dynamicTimeline;
    // PRODUCTION_START_TIMELINE
    return m_dynamicTimeline;
}

CCZ_TEST(play_button_starts_adjacent_character_moves_simultaneously) {
 ReplayOptions options;
 auto model=model_for({raw_node(L"boundary",0x00),
  raw_node(L"appear1",0x30,{},{1,10,20,0,0}),
  raw_node(L"appear2",0x30,{},{2,20,30,0,0}),
  raw_node(L"move1",0x32,{},{0,1,0,30,40,0}),
  raw_node(L"move2",0x32,{},{0,2,0,60,50,0}),
  raw_node(L"hold",0x09,{},{1})},L"hold",options);
 auto timeline=actual_play_button_timeline(model,L"hold",PlaybackRange::StartToTarget,options);
 CCZ_REQUIRE(timeline.complete);
 CCZ_REQUIRE_EQ(timeline.events.size(),std::size_t(4));
 CCZ_REQUIRE_EQ(timeline.events[2].logical_key,std::wstring(L"move2"));
 ccz::editor_preview::DynamicPlaybackSession session;
 CCZ_REQUIRE(session.Start(timeline,PlaybackRange::StartToTarget));
 session.Tick(0);auto halfway=session.Tick(600);
 CCZ_REQUIRE_EQ(halfway.event_index,std::size_t(2));CCZ_REQUIRE_EQ(halfway.progress,0.5);
 auto first=ccz::editor_preview::sample_playback_pose({{10,20},0,0},{{30,40},0,0},halfway.progress);
 auto second=ccz::editor_preview::sample_playback_pose({{20,30},0,0},{{60,50},0,0},halfway.progress);
 CCZ_REQUIRE_EQ(first.position.x,20);CCZ_REQUIRE_EQ(second.position.x,40);
}

CCZ_TEST(consecutive_moves_group_only_distinct_characters_inside_range) {
 PlaybackTimeline timeline;timeline.complete=true;
 for(int id:{1,2,1,3}) {PlaybackEvent event;event.kind=PlaybackSegmentKind::CharacterMove;event.logical_key=std::to_wstring(timeline.events.size());event.command.int_data={0,id,0,id*10,20,0};timeline.events.push_back(event);}
 timeline.range_begin=0;timeline.range_end=4;timeline.target_index=0;timeline.target_end_index=1;
 auto grouped=group_consecutive_character_moves(timeline);
 CCZ_REQUIRE_EQ(grouped.events.size(),std::size_t(2));CCZ_REQUIRE_EQ(grouped.events[0].logical_key,std::wstring(L"1"));
 CCZ_REQUIRE_EQ(grouped.events[1].logical_key,std::wstring(L"3"));
 timeline.range_end=1;grouped=group_consecutive_character_moves(timeline);
 CCZ_REQUIRE_EQ(grouped.events[0].logical_key,std::wstring(L"0"));
 timeline.range_begin=1;timeline.range_end=4;timeline.target_index=1;timeline.target_end_index=2;
 grouped=group_consecutive_character_moves(timeline);CCZ_REQUIRE_EQ(grouped.range_begin,std::size_t(1));
 CCZ_REQUIRE_EQ(grouped.events.size(),std::size_t(2));CCZ_REQUIRE_EQ(grouped.events[1].logical_key,std::wstring(L"3"));
}
