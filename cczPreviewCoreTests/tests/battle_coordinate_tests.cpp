#include "test_framework.h"

#include <ccz/preview/battle_preview.h>

CCZ_TEST(battle_coordinate_rounding_preserves_signed_half_threshold) {
    using ccz::preview::BattleCoord;
    using ccz::preview::world_to_battle_cell;

    CCZ_REQUIRE_EQ(world_to_battle_cell(1.49F, 2.49F, 0, 0),
                   (BattleCoord{1, 2}));
    CCZ_REQUIRE_EQ(world_to_battle_cell(1.5F, 2.5F, 0, 0),
                   (BattleCoord{2, 3}));
    CCZ_REQUIRE_EQ(world_to_battle_cell(-0.49F, -0.49F, 0, 0),
                   (BattleCoord{0, 0}));
    CCZ_REQUIRE_EQ(world_to_battle_cell(-0.5F, -0.5F, 0, 0),
                   (BattleCoord{-1, -1}));
}

CCZ_TEST(battle_coordinate_applies_map_offset_after_world_rounding) {
    const auto result = ccz::preview::world_to_battle_cell(
        3.5F, -4.5F, 7, 9);
    CCZ_REQUIRE_EQ(result, (ccz::preview::BattleCoord{11, -14}));
}

CCZ_TEST(battle_terrain_rejects_exact_documented_values_only) {
    constexpr std::uint8_t rejected[] = {
        5, 6, 11, 12, 14, 15, 17, 25, 26, 27, 28};
    for (const std::uint8_t value : rejected) {
        CCZ_REQUIRE(ccz::preview::is_battle_terrain_blocked(value));
    }
    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked(0));
    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked(4));
    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked(29));
}

CCZ_TEST(battle_coordinate_bounds_reject_outside_map) {
    using ccz::preview::BattleCoord;
    CCZ_REQUIRE(ccz::preview::is_battle_coord_in_bounds(
        BattleCoord{0, 0}, 10, 8));
    CCZ_REQUIRE(ccz::preview::is_battle_coord_in_bounds(
        BattleCoord{9, 7}, 10, 8));
    CCZ_REQUIRE(!ccz::preview::is_battle_coord_in_bounds(
        BattleCoord{-1, 0}, 10, 8));
    CCZ_REQUIRE(!ccz::preview::is_battle_coord_in_bounds(
        BattleCoord{10, 7}, 10, 8));
    CCZ_REQUIRE(!ccz::preview::is_battle_coord_in_bounds(
        BattleCoord{0, 8}, 10, 8));
}

