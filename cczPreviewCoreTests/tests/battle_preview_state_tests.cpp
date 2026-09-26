#include "test_framework.h"

#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/battle_preview.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_be32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> make_hexzmap_fixture() {
    constexpr std::size_t kIndexOffset = 0x110;
    constexpr std::size_t kEntrySize = 12;
    constexpr std::size_t kMapCount = 3;
    constexpr std::size_t kFirstResource =
        kIndexOffset + kMapCount * kEntrySize + 4;
    constexpr std::size_t kLargeOffset = 70000;

    const std::vector<std::vector<std::uint8_t>> resources = {
        {3, 3, 9},
        {6, 6, 1, 2, 3, 4},
        {6, 6, 0, 5, 6, 29},
    };
    const std::size_t second_offset = kFirstResource + resources[0].size();
    const std::size_t third_offset = kLargeOffset;
    std::vector<std::uint8_t> archive(third_offset + resources[2].size(), 0);

    std::size_t resource_offset = kFirstResource;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        if (index == 1) resource_offset = second_offset;
        if (index == 2) resource_offset = third_offset;
        const std::size_t entry = kIndexOffset + index * kEntrySize;
        write_be32(archive, entry, static_cast<std::uint32_t>(resources[index].size()));
        write_be32(archive, entry + 8,
                   static_cast<std::uint32_t>(resource_offset));
        std::copy(resources[index].begin(), resources[index].end(),
                  archive.begin() + resource_offset);
    }
    return archive;
}

}  // namespace

CCZ_TEST(battle_state_terrain_lookup_rejects_out_of_bounds) {
    ccz::preview::BattlePreviewState state;
    state.map.valid = true;
    state.map.width = 2;
    state.map.height = 2;
    state.terrain = {0, 5, 6, 29};

    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked_at(
        state, ccz::preview::BattleCoord{0, 0}));
    CCZ_REQUIRE(ccz::preview::is_battle_terrain_blocked_at(
        state, ccz::preview::BattleCoord{1, 0}));
    CCZ_REQUIRE(ccz::preview::is_battle_terrain_blocked_at(
        state, ccz::preview::BattleCoord{0, 1}));
    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked_at(
        state, ccz::preview::BattleCoord{1, 1}));
    CCZ_REQUIRE(!ccz::preview::is_battle_terrain_blocked_at(
        state, ccz::preview::BattleCoord{-1, 0}));
    CCZ_REQUIRE(!ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{1, 0}));
    CCZ_REQUIRE(!ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{2, 0}));
    CCZ_REQUIRE(ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{0, 0}));
}

CCZ_TEST(battle_state_defaults_unknown_unit_fields_to_safe_values) {
    const ccz::preview::BattleUnit unit;
    CCZ_REQUIRE_EQ(unit.data_id, -1);
    CCZ_REQUIRE_EQ(unit.page, 0);
    CCZ_REQUIRE_EQ(unit.direction, 0);
    CCZ_REQUIRE(!unit.hidden);
    CCZ_REQUIRE(!unit.reinforcement);
    CCZ_REQUIRE_EQ(unit.ai_policy, -1);
    CCZ_REQUIRE_EQ(unit.target, -1);
    CCZ_REQUIRE_EQ(unit.target_x, -1);
    CCZ_REQUIRE_EQ(unit.target_y, -1);
    CCZ_REQUIRE(!unit.fields_decoded);
    CCZ_REQUIRE(unit.raw_fields.empty());
}

CCZ_TEST(battle_state_marks_imported_0x4b_slots_without_character_identity) {
    ccz::preview::BattleUnit imported;
    imported.source_instruction = 0x4b;
    imported.deployment_index = 7;
    imported.data_id = -1;
    CCZ_REQUIRE(ccz::preview::is_battle_deployment_slot_marker(imported));

    ccz::preview::BattleUnit staged;
    staged.editor_deployment_instruction = 0x4b;
    staged.data_id = 91;
    staged.editor_staged_new = true;
    CCZ_REQUIRE(!ccz::preview::is_battle_deployment_slot_marker(staged));

    ccz::preview::BattleUnit character;
    character.source_instruction = 0x46;
    character.deployment_index = 7;
    character.data_id = 91;
    CCZ_REQUIRE(!ccz::preview::is_battle_deployment_slot_marker(character));

    imported.deployment_index = -1;
    CCZ_REQUIRE(!ccz::preview::is_battle_deployment_slot_marker(imported));
}

CCZ_TEST(battle_hexzmap_decodes_selected_large_offset_grid_for_placement) {
    const auto archive = make_hexzmap_fixture();
    std::vector<std::uint8_t> terrain;
    CCZ_REQUIRE(ccz::preview::decode_battle_hexzmap(
        archive, 2, 2, 2, terrain));
    CCZ_REQUIRE_EQ(terrain.size(), 4u);
    CCZ_REQUIRE_EQ(terrain[0], 0);
    CCZ_REQUIRE_EQ(terrain[1], 5);
    CCZ_REQUIRE_EQ(terrain[2], 6);
    CCZ_REQUIRE_EQ(terrain[3], 29);

    ccz::preview::BattlePreviewState state;
    state.map.valid = true;
    state.map.width = 2;
    state.map.height = 2;
    state.terrain = terrain;
    CCZ_REQUIRE(ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{0, 0}));
    CCZ_REQUIRE(!ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{1, 0}));
    CCZ_REQUIRE(!ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{0, 1}));
    CCZ_REQUIRE(ccz::preview::can_place_battle_unit(
        state, ccz::preview::BattleCoord{1, 1}));
}

CCZ_TEST(battle_hexzmap_rejects_mismatched_or_truncated_resources) {
    const auto archive = make_hexzmap_fixture();
    std::vector<std::uint8_t> terrain{99};
    CCZ_REQUIRE(!ccz::preview::decode_battle_hexzmap(
        archive, 2, 1, 4, terrain));
    CCZ_REQUIRE(terrain.empty());

    auto truncated = archive;
    truncated.resize(70005);
    terrain = {99};
    CCZ_REQUIRE(!ccz::preview::decode_battle_hexzmap(
        truncated, 2, 2, 2, terrain));
    CCZ_REQUIRE(terrain.empty());
}
