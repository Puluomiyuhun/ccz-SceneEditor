#pragma once

#include <ccz/preview/battle_field_spec.h>
#include <ccz/preview/execution_model.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccz::preview {

// Battle coordinates are logical map cells.  They deliberately do not share
// the pixel/world coordinate conversion used by R scenes.
struct BattleCoord {
    int x{};
    int y{};
};

bool operator==(const BattleCoord& left, const BattleCoord& right);
bool operator!=(const BattleCoord& left, const BattleCoord& right);

struct BattleMapInfo {
    int map_id{-1};
    int image_index{-1};
    int width{};
    int height{};
    int offset_x{};
    int offset_y{};
    bool valid{};
    // Set only when a source-backed map-cell projection is proven.  For the
    // current S baseline this is the documented 48px-per-cell map JPG grid;
    // a loose image or decoded terrain bytes without that grid evidence are
    // still insufficient and must leave this false.
    bool projection_valid{};
};

struct BattleUnit {
    int data_id{-1};
    // 0x4B stores an ordinal deployment slot, not a character Data ID.
    // It is intentionally separate from data_id, which remains the visual /
    // roster identity when the editor has one in its temporary cache.
    int deployment_index{-1};
    int page{};
    BattleCoord position{};
    int direction{};
    bool hidden{};
    int level{};
    int unit_type{};
    bool reinforcement{};
    int ai_policy{-1};
    int appearance{};
    int target{-1};
    int target_x{-1};
    int target_y{-1};
    std::uint16_t source_instruction{};
    LogicalNodeKey source_key;

    // Raw fields remain available for read-only diagnostics.  A record with
    // this flag false must not be rendered as a decoded unit or exposed to
    // editing.
    bool fields_decoded{};
    std::vector<std::int32_t> raw_fields;
    // Parser output starts clean.  A value-only writeback builder must retain
    // raw field bytes until an explicit Battle interaction marks the matching
    // semantic role dirty.
    std::vector<BattleFieldRole> dirty_fields;
    std::size_t record_index{};
    std::wstring diagnostic;
    // Editor-only identity and writeback state.  cache_id is stable for one
    // editor session; source_key/instruction/record_index/page remain the
    // immutable provenance of an imported EEX row and are never used as the
    // identity of a newly staged unit.
    std::uint64_t editor_cache_id{};
    std::uint16_t editor_deployment_instruction{0xffff};
    bool editor_pending_delete{};
    bool editor_staged_new{};
    // Brush-placement session identity is distinct from the selected brush's
    // range.  first_id is the actual first Data ID allocated to this batch.
    std::uint64_t editor_minion_brush_batch_id{};
    int editor_minion_brush_first_id{-1};
    // Immutable imported row used to detect stale TreeView edits before a
    // native writeback.  raw_fields may be normalized for preview purposes.
    std::vector<std::int32_t> editor_origin_fields;
};

bool is_battle_field_dirty(const BattleUnit& unit, BattleFieldRole role);
void mark_battle_field_dirty(BattleUnit& unit, BattleFieldRole role);
bool is_battle_deployment_slot_marker(const BattleUnit& unit);
// Resolve the Pro arm_kind used for appearance-0 faction variants from the
// deployment instruction currently represented by this unit. Staged editor
// placement takes precedence over its immutable source instruction.
std::optional<int> battle_visual_arm_kind(const BattleUnit& unit) noexcept;

struct BattlePreviewState {
    BattleMapInfo map;
    std::vector<std::uint8_t> terrain;
    std::vector<BattleUnit> units;
    // Legacy bridge field kept for source compatibility.  It is always zero
    // in the all-units battle view; BattleUnit::page is source-group
    // provenance, not a display filter.
    std::size_t current_page{};
    bool initialized{};
    std::wstring diagnostic;
};

BattleCoord world_to_battle_cell(float world_x, float world_y,
                                 int offset_x, int offset_y);

bool is_battle_terrain_blocked(std::uint8_t value);

bool is_battle_coord_in_bounds(const BattleCoord& coordinate, int width,
                               int height);

bool is_battle_terrain_blocked_at(const BattlePreviewState& state,
                                  const BattleCoord& coordinate);

bool can_place_battle_unit(const BattlePreviewState& state,
                           const BattleCoord& coordinate);

}  // namespace ccz::preview
