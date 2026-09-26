#include "test_framework.h"

#include <ccz/preview/battle_interaction.h>

namespace {

ccz::preview::BattleUnit make_unit(const wchar_t* source_key,
                                   std::size_t record_index,
                                   std::uint16_t instruction,
                                   std::size_t page = 0) {
    ccz::preview::BattleUnit unit;
    unit.source_key = source_key;
    unit.record_index = record_index;
    unit.source_instruction = instruction;
    unit.page = page;
    unit.fields_decoded = true;
    unit.data_id = 100 + static_cast<int>(record_index);
    unit.position = {0, 0};
    unit.direction = 3;
    unit.level = 5;
    unit.unit_type = 2;
    unit.target = 7;
    unit.target_x = 2;
    unit.target_y = 2;
    return unit;
}

ccz::preview::BattlePreviewState proven_state() {
    ccz::preview::BattlePreviewState state;
    state.current_page = 0;
    state.map.valid = true;
    state.map.width = 3;
    state.map.height = 3;
    state.terrain.assign(9, 0);
    state.units.push_back(make_unit(L"scope|record", 0, 0x46));
    return state;
}

}  // namespace

CCZ_TEST(battle_interaction_rejects_move_without_proven_geometry) {
    ccz::preview::BattleInteraction interaction;
    ccz::preview::BattlePreviewState state;
    state.units.push_back(make_unit(L"scope", 0, 0x46));

    CCZ_REQUIRE(interaction.Select(state, L"scope", 0).ok);
    const auto before = state.units[0].position;
    CCZ_REQUIRE(!interaction.Move(state, {1, 2}).ok);
    CCZ_REQUIRE_EQ(state.units[0].position, before);
}

CCZ_TEST(battle_interaction_moves_and_rejects_invalid_targets_atomically) {
    ccz::preview::BattleInteraction interaction;
    auto state = proven_state();
    auto blocker = make_unit(L"scope|record", 1, 0x46);
    blocker.position = {2, 2};
    state.units.push_back(blocker);

    CCZ_REQUIRE(interaction.Select(state, L"scope|record", 0).ok);
    CCZ_REQUIRE(interaction.Move(state, {1, 1}).ok);
    CCZ_REQUIRE_EQ(state.units[0].position, (ccz::preview::BattleCoord{1, 1}));

    state.terrain[2 * state.map.width] = 5;
    CCZ_REQUIRE(!interaction.Move(state, {0, 2}).ok);
    CCZ_REQUIRE_EQ(state.units[0].position, (ccz::preview::BattleCoord{1, 1}));
    CCZ_REQUIRE(!interaction.Move(state, {2, 2}).ok);
    CCZ_REQUIRE_EQ(state.units[0].position, (ccz::preview::BattleCoord{1, 1}));
    CCZ_REQUIRE(!interaction.Move(state, {-1, 0}).ok);
    CCZ_REQUIRE_EQ(state.units[0].position, (ccz::preview::BattleCoord{1, 1}));
}

CCZ_TEST(battle_interaction_selection_uses_record_scope_not_display_page) {
    ccz::preview::BattleInteraction interaction;
    ccz::preview::BattlePreviewState state;
    auto first = make_unit(L"same", 0, 0x46);
    auto second = make_unit(L"same", 1, 0x46);
    state.units = {first, second};

    CCZ_REQUIRE(interaction.Select(state, L"same", 1).ok);
    CCZ_REQUIRE(interaction.SetHidden(state, true).ok);
    CCZ_REQUIRE(!state.units[0].hidden);
    CCZ_REQUIRE(state.units[1].hidden);

    state.current_page = 1;
    CCZ_REQUIRE(interaction.SetHidden(state, false).ok);
    CCZ_REQUIRE(!state.units[1].hidden);
    CCZ_REQUIRE(!interaction.Select(state, L"other", 1).ok);
}

CCZ_TEST(battle_interaction_rejects_fields_missing_from_record_spec) {
    ccz::preview::BattleInteraction interaction;
    ccz::preview::BattlePreviewState state;
    state.units.push_back(make_unit(L"4b", 0, 0x4b));
    auto& unit = state.units[0];
    const int old_level = unit.level;
    const int old_type = unit.unit_type;
    const int old_target = unit.target;
    const int old_x = unit.target_x;
    const int old_y = unit.target_y;

    CCZ_REQUIRE(interaction.Select(state, L"4b", 0).ok);
    CCZ_REQUIRE(!interaction.SetLevel(state, 9).ok);
    CCZ_REQUIRE(!interaction.SetUnitType(state, 8).ok);
    CCZ_REQUIRE(!interaction.SetTarget(state, 10, 1, 2).ok);
    CCZ_REQUIRE_EQ(unit.level, old_level);
    CCZ_REQUIRE_EQ(unit.unit_type, old_type);
    CCZ_REQUIRE_EQ(unit.target, old_target);
    CCZ_REQUIRE_EQ(unit.target_x, old_x);
    CCZ_REQUIRE_EQ(unit.target_y, old_y);
    CCZ_REQUIRE(interaction.SetHidden(state, true).ok);
    CCZ_REQUIRE(interaction.CycleDirection(state, 1, false).ok);
}

CCZ_TEST(battle_interaction_wraps_direction_and_restores_snapshot) {
    ccz::preview::BattleInteraction interaction;
    auto state = proven_state();
    interaction.CaptureScriptState(state);

    CCZ_REQUIRE(interaction.Select(state, L"scope|record", 0).ok);
    CCZ_REQUIRE(interaction.CycleDirection(state, 1, false).ok);
    CCZ_REQUIRE_EQ(state.units[0].direction, 0);
    CCZ_REQUIRE(!interaction.CycleDirection(state, 1, true).ok);
    CCZ_REQUIRE(interaction.SetHidden(state, true).ok);
    CCZ_REQUIRE(interaction.SetLevel(state, 9).ok);
    CCZ_REQUIRE(interaction.SetUnitType(state, 7).ok);
    CCZ_REQUIRE(interaction.SetTarget(state, 8, 2, 3).ok);
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::Direction));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::Hidden));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::Level));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::UnitType));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::TargetDataId));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::TargetX));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::TargetY));

    CCZ_REQUIRE(interaction.ResetToScriptState(state));
    CCZ_REQUIRE_EQ(state.units[0].direction, 3);
    CCZ_REQUIRE(!state.units[0].hidden);
    CCZ_REQUIRE_EQ(state.units[0].level, 5);
    CCZ_REQUIRE_EQ(state.units[0].unit_type, 2);
    CCZ_REQUIRE_EQ(state.units[0].target, 7);
    CCZ_REQUIRE(!interaction.HasSelection());

    interaction.ClearScriptState();
    CCZ_REQUIRE(!interaction.ResetToScriptState(state));
    CCZ_REQUIRE(!interaction.HasSelection());
}
