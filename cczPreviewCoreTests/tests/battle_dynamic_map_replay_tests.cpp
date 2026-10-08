#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattleDynamicMapReplay.h"
#include "../../cczEditor2/PreviewIntegration/BattleMapOperations.h"

#include <ccz/preview/native_replay_plan.h>

#include <initializer_list>

namespace {

using ccz::editor_preview::BattleDynamicMapEventKind;
using ccz::editor_preview::BattleDynamicObstacle;
using ccz::editor_preview::BattleDynamicObstacleBaselineForTarget;
using ccz::editor_preview::BuildBattleDynamicMapPlan;
using ccz::editor_preview::BuildBattleDynamicObstacleBaseline;
using ccz::editor_preview::MergeBattleDynamicMapPlans;
using ccz::editor_preview::ProjectBattleDynamicMap;
using ccz::preview::BattleCoord;
using ccz::preview::BattlePreviewState;
using ccz::preview::NativeReplayItem;
using ccz::preview::NativeReplayPlan;

NativeReplayItem command(std::uint16_t id,
                         std::vector<std::int32_t> fields,
                         const wchar_t* key) {
    NativeReplayItem item;
    item.logical_key = key;
    item.command.instruction_id = id;
    item.command.int_data = std::move(fields);
    return item;
}

BattlePreviewState baseline() {
    BattlePreviewState state;
    state.map.valid = true;
    state.map.projection_valid = true;
    state.map.width = 19;
    state.map.height = 18;
    ccz::preview::BattleUnit unit;
    unit.data_id = 47;
    unit.position = {2, 3};
    unit.direction = 0;
    unit.hidden = true;
    unit.fields_decoded = true;
    state.units.push_back(unit);
    return state;
}

std::vector<std::int32_t> editor_fields(
    std::initializer_list<std::int32_t> values) {
    std::vector<std::int32_t> fields(20, 0);
    std::size_t index = 0;
    for (const std::int32_t value : values) {
        if (index == fields.size()) break;
        fields[index++] = value;
    }
    return fields;
}

}  // namespace

CCZ_TEST(battle_dynamic_edit_reuses_projected_group_positions_and_preserves_baseline) {
    auto original = baseline();
    original.units[0].editor_cache_id = 1;
    original.units[0].hidden = false;
    auto second = original.units[0];
    second.data_id = 48;
    second.editor_cache_id = 2;
    second.position = {3, 3};
    original.units.push_back(second);
    NativeReplayPlan prefix;
    prefix.complete = true;
    prefix.items = {
        command(0x32, {0, 47, 0, 5, 6, 2}, L"first-move"),
        command(0x32, {0, 48, 0, 7, 6, 1}, L"second-move"),
    };
    const auto projection = ProjectBattleDynamicMap(original, {},
        BuildBattleDynamicMapPlan(prefix, original));
    std::vector<ccz::editor_preview::BattleRoleOperationSource> sources;
    for (const auto& unit : projection.battle.units)
        sources.push_back({unit.data_id, unit.position, unit.direction});
    for (const auto kind : {ccz::editor_preview::BattleRoleOperationKind::MoveTo,
                           ccz::editor_preview::BattleRoleOperationKind::ReviveTo}) {
        std::vector<ccz::editor_preview::BattleMapOperationInstruction> instructions;
        std::vector<BattleCoord> positions;
        std::wstring diagnostic;
        CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(kind,
            sources, 0, {8, 9}, 19, 18, 3, instructions, positions, diagnostic));
        CCZ_REQUIRE_EQ(positions[0], (BattleCoord{8, 9}));
        CCZ_REQUIRE_EQ(positions[1], (BattleCoord{10, 9}));
        CCZ_REQUIRE_EQ(instructions[0].int_data[5], 3);
        CCZ_REQUIRE_EQ(instructions[1].int_data[5], 3);
    }
    CCZ_REQUIRE_EQ(original.units[0].position, (BattleCoord{2, 3}));
    CCZ_REQUIRE_EQ(original.units[1].position, (BattleCoord{3, 3}));
    CCZ_REQUIRE_EQ(projection.battle.units[0].editor_cache_id, std::uint64_t(1));
    CCZ_REQUIRE_EQ(projection.battle.units[1].editor_cache_id, std::uint64_t(2));
}

CCZ_TEST(battle_dynamic_map_replay_moves_and_revives_one_scene1_unit) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x32, {0, 47, 0, 5, 6, 2}, L"move"),
        command(0x55, {0, 47, 0, 8, 9, 3}, L"revive"),
    };

    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.events[0].kind, BattleDynamicMapEventKind::Move);
    CCZ_REQUIRE_EQ(plan.events[1].kind, BattleDynamicMapEventKind::Revive);

    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, plan);
    CCZ_REQUIRE_EQ(projection.battle.units.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{8, 9}));
    CCZ_REQUIRE_EQ(projection.battle.units[0].direction, 3);
    CCZ_REQUIRE(!projection.battle.units[0].hidden);
    CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
    CCZ_REQUIRE(projection.labels[0].revived);
}

CCZ_TEST(battle_dynamic_map_replay_accepts_editor_padded_operation_fields) {
    NativeReplayPlan scene1Source;
    scene1Source.complete = true;
    scene1Source.items = {
        command(0x58, editor_fields({0, 0, 26, 3, 4, 0, 0}),
                L"scene1-fire"),
    };
    std::vector<BattleDynamicObstacle> scene1Obstacles;
    bool scene1Complete = false;
    std::wstring scene1Diagnostic;
    CCZ_REQUIRE(BuildBattleDynamicObstacleBaseline(scene1Source,
        baseline().map, scene1Obstacles, scene1Complete, scene1Diagnostic));
    CCZ_REQUIRE(scene1Complete);
    CCZ_REQUIRE_EQ(scene1Obstacles.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(scene1Obstacles[0].coordinate, (BattleCoord{3, 4}));

    NativeReplayPlan sectionSource;
    sectionSource.complete = true;
    sectionSource.items = {
        command(0x32, editor_fields({0, 47, 0, 5, 6, 2}), L"padded-move"),
        command(0x55, editor_fields({0, 47, 0, 8, 9, 3}), L"padded-revive"),
        command(0x58, editor_fields({2, 0, 26, 7, 8, 0, 0}),
                L"padded-fireboat"),
    };
    const auto plan = BuildBattleDynamicMapPlan(sectionSource, baseline());
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE(plan.diagnostic.empty());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(3));

    const auto projection = ProjectBattleDynamicMap(
        baseline(), scene1Obstacles, plan);
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{8, 9}));
    CCZ_REQUIRE_EQ(projection.battle.units[0].direction, 3);
    CCZ_REQUIRE(!projection.battle.units[0].hidden);
    CCZ_REQUIRE_EQ(projection.obstacles.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(projection.obstacles[1].type, 2);
    CCZ_REQUIRE_EQ(projection.obstacles[1].coordinate, (BattleCoord{7, 8}));
}

CCZ_TEST(battle_dynamic_map_replay_uses_u_select_obstacle_types_and_visibility) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x58, {1, 0, 27, 4, 5, 0, 0}, L"boat-show"),
        command(0x58, {1, 1, 27, 4, 5, 0, 0}, L"boat-hide"),
        command(0x58, {2, 0, 26, 7, 8, 0, 0}, L"fireboat-show"),
    };

    std::vector<BattleDynamicObstacle> obstacles;
    bool complete = false;
    std::wstring diagnostic;
    CCZ_REQUIRE(BuildBattleDynamicObstacleBaseline(source, baseline().map,
        obstacles, complete, diagnostic));
    CCZ_REQUIRE(complete);
    CCZ_REQUIRE_EQ(obstacles.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(obstacles[0].type, 2);
    CCZ_REQUIRE_EQ(obstacles[0].coordinate, (BattleCoord{7, 8}));

    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(3));
    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, plan);
    CCZ_REQUIRE_EQ(projection.obstacles.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(projection.obstacles[0].type, 2);
}

CCZ_TEST(battle_dynamic_map_replay_rewinds_scene1_obstacles_for_earlier_target) {
    NativeReplayPlan scene1Source;
    scene1Source.complete = true;
    scene1Source.items = {
        command(0x58, {0, 0, 26, 7, 8, 0, 0}, L"later-scene1-fire"),
        command(0x58, {1, 0, 27, 9, 10, 0, 0}, L"later-scene1-boat"),
        command(0x58, {2, 0, 26, 11, 12, 0, 0}, L"later-scene1-fireboat"),
    };
    std::vector<BattleDynamicObstacle> scene1FinalObstacles;
    bool scene1Complete = false;
    std::wstring diagnostic;
    CCZ_REQUIRE(BuildBattleDynamicObstacleBaseline(scene1Source,
        baseline().map, scene1FinalObstacles, scene1Complete, diagnostic));
    CCZ_REQUIRE(scene1Complete);
    CCZ_REQUIRE_EQ(scene1FinalObstacles.size(), std::size_t(3));

    NativeReplayPlan beforeObstacleSource;
    beforeObstacleSource.complete = true;
    const auto beforeObstaclePlan = BuildBattleDynamicMapPlan(
        beforeObstacleSource, baseline());
    const auto& scene1Start = BattleDynamicObstacleBaselineForTarget(
        true, scene1FinalObstacles);
    const auto beforeProjection = ProjectBattleDynamicMap(
        baseline(), scene1Start, beforeObstaclePlan);
    CCZ_REQUIRE(beforeProjection.obstacles.empty());

    const auto fullScene1Plan = BuildBattleDynamicMapPlan(
        scene1Source, baseline());
    const auto afterProjection = ProjectBattleDynamicMap(
        baseline(), scene1Start, fullScene1Plan);
    CCZ_REQUIRE_EQ(afterProjection.obstacles.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(afterProjection.obstacles[0].type, 0);
    CCZ_REQUIRE_EQ(afterProjection.obstacles[0].coordinate,
                   (BattleCoord{7, 8}));
    CCZ_REQUIRE_EQ(afterProjection.obstacles[1].type, 1);
    CCZ_REQUIRE_EQ(afterProjection.obstacles[1].coordinate,
                   (BattleCoord{9, 10}));
    CCZ_REQUIRE_EQ(afterProjection.obstacles[2].type, 2);
    CCZ_REQUIRE_EQ(afterProjection.obstacles[2].coordinate,
                   (BattleCoord{11, 12}));

    const auto& laterSectionStart = BattleDynamicObstacleBaselineForTarget(
        false, scene1FinalObstacles);
    CCZ_REQUIRE_EQ(laterSectionStart.size(), std::size_t(3));
}

CCZ_TEST(battle_dynamic_map_replay_rejects_invalid_obstacle_fields) {
    const std::vector<std::vector<std::int32_t>> invalid_fields = {
        {0, 0, 30, 4, 5, 0, 0},
        {1, 0, -1, 4, 5, 0, 0},
        {2, 0, 26, 4, 5, 2, 0},
        {0, 0, 26, 4, 5, 0, 2},
    };
    for (const auto& fields : invalid_fields) {
        NativeReplayPlan source;
        source.complete = true;
        source.items = {command(0x58, fields, L"bad-obstacle")};

        std::vector<BattleDynamicObstacle> obstacles;
        bool complete = true;
        std::wstring diagnostic;
        CCZ_REQUIRE(BuildBattleDynamicObstacleBaseline(source,
            baseline().map, obstacles, complete, diagnostic));
        CCZ_REQUIRE(!complete);
        CCZ_REQUIRE(obstacles.empty());
        CCZ_REQUIRE(!diagnostic.empty());

        const auto replay = BuildBattleDynamicMapPlan(source, baseline());
        CCZ_REQUIRE(!replay.path_complete);
        CCZ_REQUIRE(replay.events.empty());
    }
}

CCZ_TEST(battle_dynamic_map_replay_skips_unrelated_map_state_before_supported_event) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x32, {0, 47, 0, 5, 6, -1}, L"move"),
        command(0x33, {}, L"turn"),
        command(0x55, {0, 47, 0, 8, 9, -1}, L"revive"),
    };

    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE(plan.diagnostic.empty());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.events[0].kind, BattleDynamicMapEventKind::Move);
    CCZ_REQUIRE_EQ(plan.events[1].kind, BattleDynamicMapEventKind::Revive);

    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, plan);
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{8, 9}));
    CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
    CCZ_REQUIRE(projection.labels[0].revived);
}

CCZ_TEST(battle_dynamic_map_replay_keeps_supported_events_after_unprojected_state) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x30, {}, L"initial-deployment"),
        command(0x33, {}, L"turn"),
        command(0x32, {0, 47, 0, 5, 6, 2}, L"move"),
        command(0x55, {0, 47, 0, 8, 9, 3}, L"revive"),
    };

    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE(plan.diagnostic.empty());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.events[0].kind, BattleDynamicMapEventKind::Move);
    CCZ_REQUIRE_EQ(plan.events[1].kind, BattleDynamicMapEventKind::Revive);

    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, plan);
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{8, 9}));
    CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
    CCZ_REQUIRE(projection.labels[0].revived);
    CCZ_REQUIRE_EQ(projection.labels[0].coordinate, (BattleCoord{8, 9}));
}

CCZ_TEST(battle_dynamic_map_replay_toggles_hidden_units_by_data_and_battlefield_id) {
    BattlePreviewState state = baseline();
    state.units[0].source_instruction = 0x47;
    state.units[0].battlefield_index = 60;
    ccz::preview::BattleUnit own_unit;
    own_unit.data_id = -1;
    own_unit.deployment_index = 3;
    own_unit.battlefield_index = 3;
    own_unit.source_instruction = 0x4b;
    own_unit.position = {4, 5};
    own_unit.hidden = true;
    own_unit.fields_decoded = true;
    state.units.push_back(own_unit);

    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x4c, editor_fields({0, 47, -1}), L"reveal-by-data-id"),
        command(0x31, editor_fields({0, 47, 0, 0, 0, 0, 0}),
                L"hide-by-data-id"),
        command(0x4c, editor_fields({1, 0, 3}),
                L"reveal-by-battlefield-index"),
    };

    const auto plan = BuildBattleDynamicMapPlan(source, state);
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE(plan.diagnostic.empty());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(3));

    const auto projection = ProjectBattleDynamicMap(state, {}, plan);
    CCZ_REQUIRE_EQ(projection.battle.units.size(), std::size_t(2));
    CCZ_REQUIRE(projection.battle.units[0].hidden);
    CCZ_REQUIRE(!projection.battle.units[1].hidden);
}

CCZ_TEST(battle_dynamic_map_replay_hides_area_by_faction_and_reinforcement) {
    BattlePreviewState state = baseline();
    state.units[0].source_instruction = 0x47;
    state.units[0].battlefield_index = 60;
    state.units[0].hidden = false;

    ccz::preview::BattleUnit ally;
    ally.data_id = 48;
    ally.source_instruction = 0x46;
    ally.battlefield_index = 20;
    ally.position = {3, 4};
    ally.fields_decoded = true;
    state.units.push_back(ally);

    ccz::preview::BattleUnit reinforcement;
    reinforcement.data_id = 49;
    reinforcement.source_instruction = 0x47;
    reinforcement.reinforcement = true;
    reinforcement.battlefield_index = 61;
    reinforcement.position = {4, 5};
    reinforcement.fields_decoded = true;
    state.units.push_back(reinforcement);

    NativeReplayPlan source;
    source.complete = true;
    // Editor values: range mode, (x1,y1)-(x2,y2), enemies and reinforcements.
    source.items = {command(0x31,
        editor_fields({1, 0, 1, 2, 5, 6, 5}), L"hide-area")};

    const auto plan = BuildBattleDynamicMapPlan(source, state);
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(1));

    const auto projection = ProjectBattleDynamicMap(state, {}, plan);
    CCZ_REQUIRE(projection.battle.units[0].hidden);
    CCZ_REQUIRE(!projection.battle.units[1].hidden);
    CCZ_REQUIRE(projection.battle.units[2].hidden);
}

CCZ_TEST(battle_dynamic_map_replay_rejects_unmatched_hidden_target) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {command(0x4c, {0, 99, -1}, L"missing-hidden-target")};

    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE(!plan.path_complete);
    CCZ_REQUIRE(plan.events.empty());
    CCZ_REQUIRE(!plan.diagnostic.empty());
}

CCZ_TEST(battle_dynamic_map_replay_reveals_battlefield_slot_then_46_targets) {
    BattlePreviewState state = baseline();
    state.units[0].source_instruction = 0x46;
    state.units[0].battlefield_index = 26;
    state.units[0].hidden = true;

    ccz::preview::BattleUnit unit27 = state.units[0];
    unit27.data_id = 651;
    unit27.battlefield_index = 27;
    state.units.push_back(unit27);

    ccz::preview::BattleUnit unit28 = unit27;
    unit28.data_id = 652;
    unit28.battlefield_index = 28;
    state.units.push_back(unit28);

    ccz::preview::BattleUnit slot;
    slot.data_id = -1;
    slot.deployment_index = 0;
    slot.battlefield_index = 0;
    slot.source_instruction = 0x4b;
    slot.position = {4, 5};
    slot.hidden = true;
    slot.fields_decoded = true;
    state.units.push_back(slot);

    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x4c, editor_fields({1, 0, 0}), L"show-own-slot-0"),
        command(0x4c, editor_fields({1, 0, 26}), L"show-ally-26"),
        command(0x4c, editor_fields({1, 0, 27}), L"show-ally-27"),
        command(0x4c, editor_fields({1, 0, 28}), L"show-ally-28"),
    };

    const auto plan = BuildBattleDynamicMapPlan(source, state);
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE(plan.diagnostic.empty());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(4));

    const auto projection = ProjectBattleDynamicMap(state, {}, plan);
    CCZ_REQUIRE(!projection.battle.units[0].hidden);
    CCZ_REQUIRE(!projection.battle.units[1].hidden);
    CCZ_REQUIRE(!projection.battle.units[2].hidden);
    CCZ_REQUIRE(!projection.battle.units[3].hidden);
}

CCZ_TEST(battle_dynamic_map_area_disappearance_ignores_own_deployment_slots) {
    BattlePreviewState state = baseline();
    ccz::preview::BattleUnit slot;
    slot.data_id = -1;
    slot.deployment_index = 3;
    slot.battlefield_index = 3;
    slot.source_instruction = 0x4b;
    slot.position = {4, 5};
    slot.fields_decoded = true;
    state.units.push_back(slot);

    NativeReplayPlan source;
    source.complete = true;
    source.items = {command(0x31,
        editor_fields({1, 0, 3, 4, 5, 6, 0}), L"hide-own-units-in-area")};

    const auto plan = BuildBattleDynamicMapPlan(source, state);
    CCZ_REQUIRE(plan.path_complete);
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(1));

    const auto projection = ProjectBattleDynamicMap(state, {}, plan);
    CCZ_REQUIRE(!projection.battle.units[1].hidden);
}

CCZ_TEST(battle_dynamic_map_replay_composes_scene1_operations_before_target_section) {
    NativeReplayPlan scene1Source;
    scene1Source.complete = true;
    scene1Source.items = {
        command(0x32, {0, 47, 0, 5, 6, 1}, L"scene1-move"),
    };
    NativeReplayPlan sectionSource;
    sectionSource.complete = true;
    sectionSource.items = {
        command(0x55, {0, 47, 0, 8, 9, 3}, L"section-revive"),
    };

    const auto scene1Plan = BuildBattleDynamicMapPlan(scene1Source, baseline());
    const auto sectionPlan = BuildBattleDynamicMapPlan(sectionSource, baseline());
    const auto combined = MergeBattleDynamicMapPlans(scene1Plan, sectionPlan);
    CCZ_REQUIRE_EQ(combined.events.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(combined.events[0].logical_key, std::wstring(L"scene1-move"));
    CCZ_REQUIRE_EQ(combined.events[1].logical_key, std::wstring(L"section-revive"));

    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, combined);
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{8, 9}));
    CCZ_REQUIRE_EQ(projection.battle.units[0].direction, 3);
    CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
    CCZ_REQUIRE(projection.labels[0].revived);
    CCZ_REQUIRE_EQ(projection.labels[0].coordinate, (BattleCoord{8, 9}));
}

CCZ_TEST(battle_dynamic_map_replay_does_not_call_scene1_changes_noop_for_empty_section) {
    NativeReplayPlan scene1Source;
    scene1Source.complete = true;
    scene1Source.items = {
        command(0x32, {0, 47, 0, 5, 6, 1}, L"scene1-move"),
    };
    NativeReplayPlan emptySectionSource;
    emptySectionSource.complete = true;

    const auto scene1Plan = BuildBattleDynamicMapPlan(scene1Source, baseline());
    const auto emptySectionPlan = BuildBattleDynamicMapPlan(
        emptySectionSource, baseline());
    const auto combined = MergeBattleDynamicMapPlans(
        scene1Plan, emptySectionPlan);
    CCZ_REQUIRE_EQ(combined.events.size(), std::size_t(1));
    CCZ_REQUIRE(combined.diagnostic.empty());

    const auto projection = ProjectBattleDynamicMap(
        baseline(), {}, combined);
    CCZ_REQUIRE_EQ(projection.battle.units[0].position, (BattleCoord{5, 6}));
    CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
    CCZ_REQUIRE(!projection.labels[0].revived);
}

CCZ_TEST(battle_dynamic_map_replay_ignores_turn_and_keeps_supported_events) {
    const std::uint16_t barriers[] = {0x33};
    for (const std::uint16_t barrier : barriers) {
        NativeReplayPlan source;
        source.complete = true;
        source.items = {
            command(0x32, {0, 47, 0, 5, 6, -1}, L"move"),
            command(barrier, {}, L"unsupported-map-change"),
            command(0x55, {0, 47, 0, 8, 9, -1}, L"revive"),
        };

        const auto plan = BuildBattleDynamicMapPlan(source, baseline());
        CCZ_REQUIRE(plan.path_complete);
        CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(2));
        const auto projection = ProjectBattleDynamicMap(
            baseline(), {}, plan);
        CCZ_REQUIRE_EQ(projection.battle.units[0].position,
                       (BattleCoord{8, 9}));
        CCZ_REQUIRE_EQ(projection.labels.size(), std::size_t(1));
        CCZ_REQUIRE(projection.labels[0].revived);
    }
}

CCZ_TEST(battle_dynamic_map_replay_rejects_ambiguous_or_missing_units) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {command(0x32, {0, 47, 0, 5, 6, 1}, L"move")};
    BattlePreviewState state = baseline();
    state.units.push_back(state.units.front());

    const auto plan = BuildBattleDynamicMapPlan(source, state);
    CCZ_REQUIRE(!plan.path_complete);
    CCZ_REQUIRE(plan.events.empty());
    CCZ_REQUIRE(!plan.diagnostic.empty());
}

CCZ_TEST(battle_dynamic_map_projection_reports_only_applied_events) {
    NativeReplayPlan source;
    source.complete = true;
    source.items = {
        command(0x32, {0, 47, 0, 5, 6, 1}, L"move"),
        command(0x58, {0, 0, 26, 7, 8, 0, 0}, L"fire"),
    };
    const auto plan = BuildBattleDynamicMapPlan(source, baseline());
    CCZ_REQUIRE_EQ(plan.events.size(), std::size_t(2));

    BattlePreviewState changed_baseline = baseline();
    changed_baseline.units.clear();
    const auto projection = ProjectBattleDynamicMap(
        changed_baseline, {}, plan);
    CCZ_REQUIRE(projection.obstacles.empty());
    CCZ_REQUIRE(!projection.diagnostic.empty());
}

CCZ_TEST(battle_obstacle_projection_retains_native_source_and_custom_parameters) {
    NativeReplayPlan source; source.complete=true;
    auto item=command(0x58,{2,0,16,7,8,1,1},L"editable-fireboat");item.runtime_handle=991;
    source.items={item};
    const auto plan=BuildBattleDynamicMapPlan(source,baseline());
    CCZ_REQUIRE(plan.path_complete);
    auto projected=ProjectBattleDynamicMap(baseline(),{},plan);
    CCZ_REQUIRE_EQ(projected.obstacles.size(),std::size_t(1));
    CCZ_REQUIRE_EQ(projected.obstacles[0].source_handle,std::uintptr_t(991));
    CCZ_REQUIRE_EQ(projected.obstacles[0].source_key,std::wstring(L"editable-fireboat"));
    CCZ_REQUIRE(projected.obstacles[0].source_fields==item.command.int_data);
    source.items.clear();
    projected=ProjectBattleDynamicMap(baseline(),{},BuildBattleDynamicMapPlan(source,baseline()));
    CCZ_REQUIRE(projected.obstacles.empty());
}
CCZ_TEST(battle_unresolved_auxiliary_unit_target_does_not_block_later_obstacles) {
    for (const unsigned short id : {0x4d,0x4e,0x53,0x5c}) {
        auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(id);
        config.fields[id==0x5c?0:1]=0;
        NativeReplayPlan source; source.complete=true;
        source.items={command(id,config.fields,L"unresolved-data-zero"),command(0x58,{0,0,26,7,8,0,0},L"later-fire")};
        const auto plan=BuildBattleDynamicMapPlan(source,baseline());
        CCZ_REQUIRE(plan.path_complete); CCZ_REQUIRE_EQ(plan.events.size(),std::size_t(1));
        CCZ_REQUIRE(!plan.diagnostic.empty());
        CCZ_REQUIRE_EQ(ProjectBattleDynamicMap(baseline(),{},plan).obstacles.size(),std::size_t(1));
    }
}
CCZ_TEST(battle_ambiguous_auxiliary_target_still_stops_projection) {
    auto state=baseline();state.units.push_back(state.units.front());
    auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(0x4d);config.fields[1]=47;
    NativeReplayPlan source;source.complete=true;
    source.items={command(0x4d,config.fields,L"ambiguous"),command(0x58,{0,0,26,7,8,0,0},L"later-fire")};
    const auto plan=BuildBattleDynamicMapPlan(source,state);
    CCZ_REQUIRE(!plan.path_complete);CCZ_REQUIRE(plan.events.empty());
}
#include "../../cczEditor2/PreviewIntegration/BattleOwnUnitBindings.h"
CCZ_TEST(battle_own_preview_binding_resolves_data_status_without_mutating_source) {
    auto state=baseline();auto& u=state.units.front();u.data_id=-1;u.deployment_index=0;u.battlefield_index=0;u.source_instruction=0x4b;u.editor_cache_id=1;
    const auto original=state;std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::ApplyBattleOwnUnitBindings(state,{{0,0}},diagnostic));
    CCZ_REQUIRE_EQ(state.units.front().data_id,0);CCZ_REQUIRE_EQ(original.units.front().data_id,-1);
    CCZ_REQUIRE_EQ(state.units.front().raw_fields,original.units.front().raw_fields);
    auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(0x4d);config.fields[8]=0;config.fields[9]=2;
    NativeReplayPlan source;source.complete=true;source.items={command(0x4d,config.fields,L"own-status")};
    const auto plan=BuildBattleDynamicMapPlan(source,state);CCZ_REQUIRE(plan.path_complete);CCZ_REQUIRE_EQ(plan.events.size(),std::size_t(1));
    const auto projection=ProjectBattleDynamicMap(state,{},plan);CCZ_REQUIRE_EQ(projection.unit_details.at(1).attribute_changes.at(0),2);
}
CCZ_TEST(battle_own_battlefield_status_resolves_unbound_slot) {
    auto state=baseline();auto& u=state.units.front();u.data_id=-1;u.deployment_index=0;u.battlefield_index=0;u.source_instruction=0x4b;u.editor_cache_id=1;
    auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(0x4d);config.fields[0]=1;config.fields[2]=0;config.fields[8]=0;config.fields[9]=2;
    NativeReplayPlan source;source.complete=true;source.items={command(0x4d,config.fields,L"own-battlefield-status")};
    const auto plan=BuildBattleDynamicMapPlan(source,state);CCZ_REQUIRE(plan.path_complete);CCZ_REQUIRE_EQ(plan.events.size(),std::size_t(1));
    CCZ_REQUIRE_EQ(ProjectBattleDynamicMap(state,{},plan).unit_details.at(1).attribute_changes.at(0),2);
}
CCZ_TEST(battle_own_bindings_reject_duplicate_identity_atomically) {
    auto state=baseline();auto own=state.units.front();own.data_id=-1;own.source_instruction=0x4b;own.deployment_index=0;state.units.push_back(own);
    std::wstring diagnostic;CCZ_REQUIRE(!ccz::editor_preview::ApplyBattleOwnUnitBindings(state,{{0,47}},diagnostic));CCZ_REQUIRE_EQ(state.units.back().data_id,-1);
    own.deployment_index=1;state.units.push_back(own);
    CCZ_REQUIRE(!ccz::editor_preview::ApplyBattleOwnUnitBindings(state,{{0,0},{1,0}},diagnostic));CCZ_REQUIRE_EQ(state.units[1].data_id,-1);
    CCZ_REQUIRE(!ccz::editor_preview::ApplyBattleOwnUnitBindings(state,{{19,0}},diagnostic));
}

CCZ_TEST(battle_dynamic_labels_keep_distinct_unbound_battlefield_units) {
 auto b=baseline();b.units.clear();
 for(int i=0;i<2;++i){ccz::preview::BattleUnit u;u.data_id=-1;u.source_instruction=0x4b;u.deployment_index=i;u.battlefield_index=i;u.editor_cache_id=i+1;u.fields_decoded=true;u.position={i+1,1};b.units.push_back(u);}
 NativeReplayPlan p;p.complete=true;p.items={command(0x32,{1,0,0,3,4,0},L"move-slot-0"),command(0x55,{1,0,1,5,6,1},L"revive-slot-1")};
 const auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.labels.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(result.labels[0].coordinate,(BattleCoord{3,4}));
 CCZ_REQUIRE_EQ(result.labels[1].coordinate,(BattleCoord{5,6}));
 p.items.push_back(command(0x32,{1,0,0,7,8,0},L"move-slot-0-again"));
 const auto updated=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(updated.labels.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(updated.labels[0].coordinate,(BattleCoord{5,6}));
 CCZ_REQUIRE_EQ(updated.labels[1].coordinate,(BattleCoord{7,8}));
}

CCZ_TEST(acceptance_unbound_target_does_not_block_later_obstacle) {
 auto b=baseline();ccz::preview::BattleUnit own;
 own.source_instruction=0x4b;own.deployment_index=0;own.battlefield_index=0;
 own.data_id=-1;own.fields_decoded=true;own.editor_cache_id=900;own.position={1,1};b.units.push_back(own);
 NativeReplayPlan p;p.complete=true;
 p.items={command(0x32,{0,999,0,2,2,0},L"unknown-party"),command(0x58,{0,0,0,3,3,0,0},L"later-obstacle")};
 const auto plan=BuildBattleDynamicMapPlan(p,b);
 CCZ_REQUIRE(plan.path_complete);CCZ_REQUIRE(!plan.diagnostic.empty());
 const auto projection=ProjectBattleDynamicMap(b,{},plan);
 CCZ_REQUIRE_EQ(projection.obstacles.size(),std::size_t(1));
}

CCZ_TEST(acceptance_single_facing_uses_data_or_battlefield_reference) {
 auto b=baseline();auto target=b.units.front();target.data_id=888;target.editor_cache_id=901;
 target.battlefield_index=9;target.position={8,3};target.fields_decoded=true;b.units.push_back(target);
 b.units.front().position={2,3};b.units.front().direction=0;
 for(const int reference:{888,1037}){
  NativeReplayPlan p;p.complete=true;
  p.items={command(0x4f,{b.units.front().data_id,reference,-1,0,1,1},L"face-target")};
  const auto projection=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
  CCZ_REQUIRE_EQ(projection.battle.units.front().direction,1);
 }
}
