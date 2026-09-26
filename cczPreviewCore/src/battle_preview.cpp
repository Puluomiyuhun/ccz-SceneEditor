#include <ccz/preview/battle_preview.h>

#include <algorithm>
#include <cmath>

namespace ccz::preview {
namespace {

int RoundBattleCoordinate(float value) {
    // This mirrors the tool's signed 0.5 threshold instead of using a
    // locale-dependent or compiler-dependent cast from float to int.
    if (value >= 0.0F) {
        return static_cast<int>(std::floor(value + 0.5F));
    }
    return static_cast<int>(std::ceil(value - 0.5F));
}

}  // namespace

bool operator==(const BattleCoord& left, const BattleCoord& right) {
    return left.x == right.x && left.y == right.y;
}

bool operator!=(const BattleCoord& left, const BattleCoord& right) {
    return !(left == right);
}

bool is_battle_field_dirty(const BattleUnit& unit, BattleFieldRole role) {
    return std::find(unit.dirty_fields.begin(), unit.dirty_fields.end(), role) !=
           unit.dirty_fields.end();
}

void mark_battle_field_dirty(BattleUnit& unit, BattleFieldRole role) {
    if (!is_battle_field_dirty(unit, role)) {
        unit.dirty_fields.push_back(role);
    }
}

bool is_battle_deployment_slot_marker(const BattleUnit& unit) {
    return unit.source_instruction == 0x4b && unit.deployment_index >= 0 &&
           unit.data_id < 0 && !unit.editor_staged_new;
}

BattleCoord world_to_battle_cell(float world_x, float world_y, int offset_x,
                                 int offset_y) {
    const int rounded_x = RoundBattleCoordinate(world_x);
    const int rounded_y = RoundBattleCoordinate(world_y);
    return {rounded_x + offset_x, rounded_y - offset_y};
}

bool is_battle_terrain_blocked(std::uint8_t value) {
    switch (value) {
    case 5:
    case 6:
    case 11:
    case 12:
    case 14:
    case 15:
    case 17:
    case 25:
    case 26:
    case 27:
    case 28:
        return true;
    default:
        return false;
    }
}

bool is_battle_coord_in_bounds(const BattleCoord& coordinate, int width,
                               int height) {
    return width > 0 && height > 0 && coordinate.x >= 0 && coordinate.y >= 0 &&
           coordinate.x < width && coordinate.y < height;
}

bool is_battle_terrain_blocked_at(const BattlePreviewState& state,
                                  const BattleCoord& coordinate) {
    if (!state.map.valid ||
        !is_battle_coord_in_bounds(coordinate, state.map.width,
                                   state.map.height)) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(coordinate.y) *
            static_cast<std::size_t>(state.map.width) +
        static_cast<std::size_t>(coordinate.x);
    if (index >= state.terrain.size()) {
        return false;
    }
    return is_battle_terrain_blocked(state.terrain[index]);
}

bool can_place_battle_unit(const BattlePreviewState& state,
                           const BattleCoord& coordinate) {
    if (!state.map.valid ||
        !is_battle_coord_in_bounds(coordinate, state.map.width,
                                   state.map.height)) {
        return false;
    }
    const std::size_t expected_size =
        static_cast<std::size_t>(state.map.width) *
        static_cast<std::size_t>(state.map.height);
    if (state.terrain.size() < expected_size) {
        return false;
    }
    return !is_battle_terrain_blocked_at(state, coordinate);
}

}  // namespace ccz::preview
