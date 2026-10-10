#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattleMovementTerrainResourceCache.h"

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

std::vector<std::uint8_t> make_data_archive(std::uint8_t terrain_cost) {
    constexpr std::size_t kIndexOffset = 0x110;
    constexpr std::size_t kEntrySize = 12;
    constexpr std::size_t kEntryCount = 5;
    constexpr std::size_t kFirstResource =
        kIndexOffset + kEntryCount * kEntrySize + 4;
    constexpr std::size_t kTableBytes = 40 * 60;
    const std::size_t table_offset = kFirstResource + 4;
    std::vector<std::uint8_t> archive(table_offset + kTableBytes, 0);
    for (std::size_t index = 0; index < 4; ++index) {
        const std::size_t entry = kIndexOffset + index * kEntrySize;
        write_be32(archive, entry, 1);
        write_be32(archive, entry + 8,
                   static_cast<std::uint32_t>(kFirstResource + index));
        archive[kFirstResource + index] = static_cast<std::uint8_t>(index);
    }
    const std::size_t entry = kIndexOffset + 4 * kEntrySize;
    write_be32(archive, entry, static_cast<std::uint32_t>(kTableBytes));
    write_be32(archive, entry + 8, static_cast<std::uint32_t>(table_offset));
    for (std::size_t row = 0; row < 40; ++row)
        for (std::size_t terrain = 0; terrain < 30; ++terrain)
            archive[table_offset + row * 60 + 30 + terrain] = terrain_cost;
    return archive;
}

}  // namespace

CCZ_TEST(battle_movement_resource_cache_shares_one_preloaded_mod_revision) {
    ccz::editor_preview::BattleMovementTerrainResourceCache cache;
    const auto data = make_data_archive(3);
    const auto first_script = cache.Preload(L"c:\\mods\\main", 101, data);
    const auto second_script = cache.Preload(L"c:\\mods\\main", 101, data);
    CCZ_REQUIRE(first_script != nullptr);
    CCZ_REQUIRE(first_script == second_script);
    CCZ_REQUIRE(first_script->valid);
    CCZ_REQUIRE_EQ(first_script->values[0], std::uint8_t{3});
}

CCZ_TEST(battle_movement_resource_cache_is_mod_and_data_revision_scoped) {
    ccz::editor_preview::BattleMovementTerrainResourceCache cache;
    const auto data_a = make_data_archive(2);
    const auto data_b = make_data_archive(7);
    const auto mod_a = cache.Preload(L"c:\\mods\\a", 101, data_a);
    const auto same_revision = cache.Preload(L"c:\\mods\\a", 101, data_b);
    const auto changed_revision = cache.Preload(L"c:\\mods\\a", 102, data_b);
    const auto another_mod = cache.Preload(L"c:\\mods\\b", 101, data_b);
    CCZ_REQUIRE(mod_a != nullptr);
    CCZ_REQUIRE(mod_a == same_revision);
    CCZ_REQUIRE(mod_a != changed_revision);
    CCZ_REQUIRE(mod_a != another_mod);
    CCZ_REQUIRE_EQ(mod_a->values[0], std::uint8_t{2});
    CCZ_REQUIRE_EQ(changed_revision->values[0], std::uint8_t{7});
    CCZ_REQUIRE_EQ(another_mod->values[0], std::uint8_t{7});
}

CCZ_TEST(battle_movement_resource_cache_does_not_cache_missing_data) {
    ccz::editor_preview::BattleMovementTerrainResourceCache cache;
    const std::vector<std::uint8_t> missing;
    CCZ_REQUIRE(!cache.Preload(L"c:\\mods\\main", 0, missing));
    const auto available = cache.Preload(
        L"c:\\mods\\main", 0, make_data_archive(4));
    CCZ_REQUIRE(available != nullptr);
    CCZ_REQUIRE(available->valid);
    CCZ_REQUIRE_EQ(available->values[0], std::uint8_t{4});
}
