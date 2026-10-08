#pragma once

#include <ccz/preview/battle_preview.h>
#include <ccz/preview/native_replay_plan.h>

#include <string>
#include <map>
#include <vector>

namespace ccz::editor_preview {

enum class BattleDynamicMapEventKind {
    Move,
    Revive,
    HiddenAppear,
    Disappear,
    ObstacleShow,
    ObstacleHide,
    Retreat,
    StatusChange,
    PolicyChange,
    HighlightArea,
    HighlightUnit,
    Animation,
    Turn,
    Action,
};

enum class BattleDynamicUnitTargetKind {
    DataId,
    BattlefieldIndex,
    AreaAndFaction,
};

struct BattleDynamicObstacle {
    int type{};
    ccz::preview::BattleCoord coordinate;
    std::uintptr_t source_handle{};
    ccz::preview::LogicalNodeKey source_key;
    std::vector<std::int32_t> source_fields;
};

struct BattleDynamicMapLabel {
    bool revived{};
    int data_id{-1};
    ccz::preview::BattleCoord coordinate;
    std::size_t unit_index{}; // Stable in the fixed Scene1 record order.
};

struct BattleDynamicMapEvent {
    BattleDynamicMapEventKind kind{BattleDynamicMapEventKind::Move};
    BattleDynamicUnitTargetKind target_kind{
        BattleDynamicUnitTargetKind::DataId};
    ccz::preview::LogicalNodeKey logical_key;
    int data_id{-1};
    int battlefield_index{-1};
    ccz::preview::BattleCoord coordinate;
    ccz::preview::BattleCoord area_end;
    int direction{-1};
    int obstacle_type{-1};
    std::uintptr_t obstacle_source_handle{};
    std::vector<std::int32_t> obstacle_source_fields;
    int faction{-1};
    std::uint16_t instruction_id{};
    std::uintptr_t runtime_handle{};
    std::vector<std::int32_t> source_fields;
};

struct BattleDynamicPositionOrigin {
    std::uint16_t instruction_id{};
    std::uintptr_t runtime_handle{};
    ccz::preview::LogicalNodeKey source_key;
    std::vector<std::int32_t> source_fields;
};
struct BattleDynamicUnitDetails {
    // Unknown baseline values stay unknown. Changes are recorded without
    // fabricating HP/MP, combat or attribute simulation.
    std::map<int,int> attribute_changes;
    int added_status_bits{};
    int removed_status_bits{};
    std::int64_t hp_change{};
    std::int64_t mp_change{};
    int retreat_kind{-1}; // Native 53: 0 retreat, 1 death; -1 cleared on appear/revive.
    int action{-1};
    int policy{-1};
    int policy_target{-1};
    ccz::preview::BattleCoord policy_coordinate;
};
struct BattleDynamicHighlight {
    bool unit_target{};
    std::uint64_t cache_id{};
    ccz::preview::BattleCoord coordinate;
    ccz::preview::BattleCoord area_end;
    bool enabled{};
    int type_flag{}; // 5B: 0 victory-condition area, 1 during-battle area.
};
struct BattleDynamicAnimation {
    ccz::preview::BattleCoord coordinate;
    int effect{};
    bool viewport{};
    ccz::preview::LogicalNodeKey source_key;
};

struct BattleDynamicMapPlan {
    std::vector<BattleDynamicMapEvent> events;
    bool path_complete{};
    std::wstring diagnostic;
};

struct BattleDynamicMapProjection {
    ccz::preview::BattlePreviewState battle;
    std::vector<BattleDynamicObstacle> obstacles;
    std::vector<BattleDynamicMapLabel> labels;
    std::map<std::uint64_t,BattleDynamicUnitDetails> unit_details;
    std::map<std::uint64_t,BattleDynamicPositionOrigin> position_origins;
    std::vector<BattleDynamicHighlight> highlights;
    std::vector<BattleDynamicAnimation> animations;
    std::wstring diagnostic;
};

// Resolve the final visible 0x58 type-0/1/2 obstacles on a condition-aware
// Scene1 replay path.  Unsupported obstacle types truncate the known prefix.
bool BuildBattleDynamicObstacleBaseline(
    const ccz::preview::NativeReplayPlan& scene1_plan,
    const ccz::preview::BattleMapInfo& map,
    std::vector<BattleDynamicObstacle>& obstacles,
    bool& path_complete, std::wstring& diagnostic);

// A target inside Scene1 must replay its selected prefix from the layout
// before Scene1 obstacle commands. Later Sections start from Scene1's final
// obstacle state.
const std::vector<BattleDynamicObstacle>&
BattleDynamicObstacleBaselineForTarget(
    bool target_is_scene1,
    const std::vector<BattleDynamicObstacle>& scene1_final_obstacles);

// Extract supported map and unit-visibility operations from a condition-aware
// native Section plan. Other instructions are skipped so they cannot hide a
// later supported operation; malformed supported operations stop the prefix.
BattleDynamicMapPlan BuildBattleDynamicMapPlan(
    const ccz::preview::NativeReplayPlan& section_plan,
    const ccz::preview::BattlePreviewState& scene1_baseline);

// Append the selected Section's supported operations after the Scene1
// operations, preserving script order and diagnostics.
BattleDynamicMapPlan MergeBattleDynamicMapPlans(
    const BattleDynamicMapPlan& prefix,
    const BattleDynamicMapPlan& suffix);

// Build one deterministic snapshot by applying every event in the selected
// instruction prefix to a value copy of the Scene1 baseline. Spell/highlight
// effects are emitted only for selected_instruction; an empty key emits none. A unit operation
// updates one existing Data ID record and never appends a second instance.
BattleDynamicMapProjection ProjectBattleDynamicMap(
    const ccz::preview::BattlePreviewState& scene1_baseline,
    const std::vector<BattleDynamicObstacle>& scene1_obstacles,
    const BattleDynamicMapPlan& plan,
    const ccz::preview::LogicalNodeKey& selected_instruction = {});

}  // namespace ccz::editor_preview
