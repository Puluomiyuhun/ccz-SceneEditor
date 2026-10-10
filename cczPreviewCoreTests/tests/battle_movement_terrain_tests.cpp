#include "test_framework.h"

#include <ccz/preview/battle_movement_terrain.h>
#include <ccz/preview/e5_archive.h>

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

std::vector<std::uint8_t> make_data_archive() {
    constexpr std::size_t kIndexOffset = 0x110;
    constexpr std::size_t kEntrySize = 12;
    constexpr std::size_t kEntryCount = 5;
    constexpr std::size_t kFirstResource =
        kIndexOffset + kEntryCount * kEntrySize + 4;
    constexpr std::size_t kMovementTableBytes = 40 * 60;
    const std::size_t table_offset = kFirstResource + 4;
    std::vector<std::uint8_t> archive(table_offset + kMovementTableBytes, 0);

    for (std::size_t index = 0; index < 4; ++index) {
        const std::size_t entry = kIndexOffset + index * kEntrySize;
        write_be32(archive, entry, 1);
        write_be32(archive, entry + 8,
                   static_cast<std::uint32_t>(kFirstResource + index));
        archive[kFirstResource + index] = static_cast<std::uint8_t>(index);
    }
    const std::size_t table_entry = kIndexOffset + 4 * kEntrySize;
    write_be32(archive, table_entry,
               static_cast<std::uint32_t>(kMovementTableBytes));
    write_be32(archive, table_entry + 8,
               static_cast<std::uint32_t>(table_offset));

    // The engine reads the second 30-byte segment of every 60-byte row.
    for (std::size_t row = 0; row < 40; ++row) {
        for (std::size_t terrain = 0; terrain < 30; ++terrain) {
            archive[table_offset + row * 60 + 30 + terrain] = 1;
        }
    }
    archive[table_offset + 0 * 60 + 30 + 7] = 0xff;
    archive[table_offset + 20 * 60 + 30 + 7] = 2;
    archive[table_offset + 39 * 60 + 30 + 29] = 3;
    return archive;
}

}  // namespace

CCZ_TEST(e5_archive_finds_indexed_resource_without_copying) {
    const auto archive = make_data_archive();
    ccz::preview::E5ResourceView resource;
    CCZ_REQUIRE(ccz::preview::find_e5_resource(archive, 4, 4096, resource));
    CCZ_REQUIRE(resource.valid());
    CCZ_REQUIRE_EQ(resource.size, std::size_t{2400});
    CCZ_REQUIRE_EQ(resource.data[30 + 7], std::uint8_t{0xff});
    CCZ_REQUIRE(!ccz::preview::find_e5_resource(archive, 5, 4096, resource));
    CCZ_REQUIRE(!resource.valid());
}

CCZ_TEST(battle_movement_class_codes_map_to_engine_rows) {
    using ccz::preview::battle_movement_class_row;
    CCZ_REQUIRE_EQ(*battle_movement_class_row(0x00), std::size_t{0});
    CCZ_REQUIRE_EQ(*battle_movement_class_row(0x3b), std::size_t{19});
    CCZ_REQUIRE_EQ(*battle_movement_class_row(0x3c), std::size_t{20});
    CCZ_REQUIRE_EQ(*battle_movement_class_row(0x4f), std::size_t{39});
    CCZ_REQUIRE(!battle_movement_class_row(-1).has_value());
    CCZ_REQUIRE(!battle_movement_class_row(0x50).has_value());
}

CCZ_TEST(battle_movement_table_reads_resource_four_second_half) {
    ccz::preview::BattleMovementTerrainTable table;
    CCZ_REQUIRE(ccz::preview::decode_battle_movement_terrain_table(
        make_data_archive(), table));
    CCZ_REQUIRE(table.valid);
    CCZ_REQUIRE_EQ(*ccz::preview::battle_movement_terrain_cost(table, 0, 2),
                   std::uint8_t{1});
    CCZ_REQUIRE_EQ(*ccz::preview::battle_movement_terrain_cost(table, 0x3c, 7),
                   std::uint8_t{2});
    CCZ_REQUIRE_EQ(*ccz::preview::battle_movement_terrain_cost(table, 0x4f, 29),
                   std::uint8_t{3});
    CCZ_REQUIRE_EQ(*ccz::preview::battle_movement_terrain_cost(table, 0, 7),
                   std::uint8_t{0xff});
    CCZ_REQUIRE(!ccz::preview::battle_movement_terrain_cost(
        table, 0x50, 7).has_value());
    CCZ_REQUIRE(!ccz::preview::battle_movement_terrain_cost(
        table, 0, 30).has_value());
}

CCZ_TEST(battle_movement_table_rejects_truncated_resource_and_clears_old_data) {
    auto archive = make_data_archive();
    archive.resize(archive.size() - 1);
    ccz::preview::BattleMovementTerrainTable table;
    table.valid = true;
    table.values.fill(9);
    CCZ_REQUIRE(!ccz::preview::decode_battle_movement_terrain_table(
        archive, table));
    CCZ_REQUIRE(!table.valid);
    CCZ_REQUIRE(!ccz::preview::battle_movement_terrain_cost(table, 0, 0));
}
