#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ccz::preview {

inline constexpr std::size_t kBattleMovementClassRows = 40;
inline constexpr std::size_t kBattleMovementTerrainColumns = 30;

struct BattleMovementTerrainTable {
    bool valid{};
    std::array<std::uint8_t,
        kBattleMovementClassRows * kBattleMovementTerrainColumns> values{};
};

// Decode Data.e5 resource 4 and retain only its second (per-step movement
// cost / impassable) 30-byte half for each of the 40 movement classes.
bool decode_battle_movement_terrain_table(
    const std::vector<std::uint8_t>& data_e5,
    BattleMovementTerrainTable& result);

// Data.e5 +26 movement-class code to the engine's 0..39 matrix row.
std::optional<std::size_t> battle_movement_class_row(
    int movement_class_code) noexcept;

// Returns no value for an invalid table, class code, or terrain ID.
std::optional<std::uint8_t> battle_movement_terrain_cost(
    const BattleMovementTerrainTable& table, int movement_class_code,
    int terrain_id) noexcept;

}  // namespace ccz::preview
