#include <ccz/preview/battle_movement_terrain.h>

#include <ccz/preview/e5_archive.h>

#include <algorithm>

namespace ccz::preview {

bool decode_battle_movement_terrain_table(
    const std::vector<std::uint8_t>& data_e5,
    BattleMovementTerrainTable& result) {
    result = {};
    E5ResourceView resource;
    constexpr std::size_t kResourceIndex = 4;
    constexpr std::size_t kMaximumDataResources = 4096;
    constexpr std::size_t kBytesPerClass = 60;
    constexpr std::size_t kSecondHalfOffset = 30;
    constexpr std::size_t kRequiredBytes =
        kBattleMovementClassRows * kBytesPerClass;
    if (!find_e5_resource(data_e5, kResourceIndex, kMaximumDataResources,
                          resource) || resource.size < kRequiredBytes) {
        return false;
    }

    for (std::size_t row = 0; row < kBattleMovementClassRows; ++row) {
        const std::size_t source = row * kBytesPerClass + kSecondHalfOffset;
        const std::size_t destination =
            row * kBattleMovementTerrainColumns;
        std::copy_n(resource.data + source,
                    kBattleMovementTerrainColumns,
                    result.values.begin() + destination);
    }
    result.valid = true;
    return true;
}

std::optional<std::size_t> battle_movement_class_row(
    int movement_class_code) noexcept {
    if (movement_class_code >= 0 && movement_class_code <= 0x3b)
        return static_cast<std::size_t>(movement_class_code / 3);
    if (movement_class_code >= 0x3c && movement_class_code <= 0x4f)
        return static_cast<std::size_t>(movement_class_code - 0x28);
    return std::nullopt;
}

std::optional<std::uint8_t> battle_movement_terrain_cost(
    const BattleMovementTerrainTable& table, int movement_class_code,
    int terrain_id) noexcept {
    if (!table.valid || terrain_id < 0 ||
        terrain_id >= static_cast<int>(kBattleMovementTerrainColumns)) {
        return std::nullopt;
    }
    const auto row = battle_movement_class_row(movement_class_code);
    if (!row.has_value()) return std::nullopt;
    return table.values[*row * kBattleMovementTerrainColumns +
                        static_cast<std::size_t>(terrain_id)];
}

}  // namespace ccz::preview
