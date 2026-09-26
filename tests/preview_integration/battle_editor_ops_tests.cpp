#include "../../cczPreviewCoreTests/tests/test_framework.h"

#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/battle_field_spec.h>
#include <ccz/preview/battle_preview.h>
#include "../../cczEditor2/PreviewIntegration/BattleEditorOps.h"
#include "../../cczEditor2/PreviewIntegration/BattleWritebackAdapter.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using ccz::editor_preview::BattleCharacterEntry;
using ccz::editor_preview::BattleMinionBrush;
using ccz::editor_preview::BattleEditorMode;
using ccz::editor_preview::BattleEditorOps;
using ccz::editor_preview::BattleEditorState;
using ccz::editor_preview::BattlePainterMode;
using ccz::editor_preview::BattlePainterValue;
using ccz::editor_preview::BattleTargetKind;
using ccz::editor_preview::BattleTargetRef;
using ccz::editor_preview::BattleUnitPatch;
using ccz::editor_preview::BattleWritebackEvidence;
using ccz::editor_preview::IsBattleFixedEmptySlot;
using ccz::preview::BattlePreviewState;
using ccz::preview::BattleUnit;

constexpr const wchar_t* kScope = L"battle|scene:0";
constexpr const wchar_t* kSource = L"battle|scene:0|section:0|node:46";

BattleUnit unit(std::uint16_t instruction, std::size_t record_index,
                std::size_t page, int data_id, int x, int y,
                const wchar_t* source = kSource) {
    BattleUnit result;
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    result.source_instruction = instruction;
    result.source_key = source;
    result.record_index = record_index;
    result.page = static_cast<int>(page);
    result.data_id = data_id;
    result.position = {x, y};
    result.direction = 3;
    result.hidden = false;
    result.level = 5;
    result.unit_type = 2;
    result.appearance = 1;
    result.ai_policy = 7;
    result.target = -1;
    result.target_x = -1;
    result.target_y = -1;
    result.fields_decoded = spec != nullptr && spec->semantic_complete;
    if (spec != nullptr) result.raw_fields.assign(spec->record_stride, 0);
    return result;
}

BattleEditorState fixture() {
    BattleEditorState state;
    state.scope_root = kScope;
    state.battle.current_page = 0;
    state.battle.map.valid = true;
    state.battle.map.width = 4;
    state.battle.map.height = 4;
    state.battle.terrain.assign(16, 0);
    state.battle.units.push_back(unit(0x46, 0, 0, 100, 1, 1));
    state.battle.units.push_back(unit(0x46, 1, 0, 101, 2, 1));
    // A second source-record group is still part of the same Battle view.  Use
    // a distinct cell so the all-units interaction contract is unambiguous.
    state.battle.units.push_back(unit(0x46, 0, 1, 200, 3, 1));
    state.battle.units.push_back(unit(
        0x46, 0, 0, 300, 1, 1, L"battle|scene:1|section:0|node:46"));
    state.characters.entries = {
        {100, L"曹操", 1, 2, 5, 2, false, 7},
        {101, L"夏侯惇", 2, 3, 6, 3, false, 8},
        {200, L"援军张辽", 3, 0, 4, 4, true, 9},
        {201, L"典韦", 4, 1, 7, 5, false, 10},
        {202, L"许褚", 5, 2, 8, 6, false, 11},
    };
    state.characters.page_size = 2;
    state.placement.source_key = kSource;
    state.placement.instruction_id = 0x46;
    state.placement.page = 0;
    state.mode = BattleEditorMode::Place;
    return state;
}

// Synthetic evidence for the value-only adapter tests.  The flags exercise
// framing/ordering code paths; they are not native reload or product
// acceptance proof.
BattleWritebackEvidence value_adapter_test_evidence() {
    BattleWritebackEvidence evidence;
    evidence.field_table_verified = true;
    evidence.sample_manifest_verified = true;
    evidence.group_order_verified = true;
    evidence.native_reload_verified = true;
    evidence.kind300_external_acceptance_verified = true;
    return evidence;
}

int empty_sentinel(ccz::preview::BattleFieldRole role) {
    switch (role) {
    case ccz::preview::BattleFieldRole::DataId:
    case ccz::preview::BattleFieldRole::TargetDataId:
        return -1;
    case ccz::preview::BattleFieldRole::Direction:
        return -1;
    case ccz::preview::BattleFieldRole::UnitType:
        return 2;
    case ccz::preview::BattleFieldRole::AiPolicy:
        return 1;
    case ccz::preview::BattleFieldRole::Reinforcement:
    case ccz::preview::BattleFieldRole::Hidden:
    case ccz::preview::BattleFieldRole::Level:
    case ccz::preview::BattleFieldRole::X:
    case ccz::preview::BattleFieldRole::Y:
    case ccz::preview::BattleFieldRole::TargetX:
    case ccz::preview::BattleFieldRole::TargetY:
        return 0;
    }
    return 0;
}

std::vector<std::uint8_t> four_by_four_hexzmap() {
    constexpr std::size_t kIndexOffset = 0x110;
    constexpr std::size_t kResourceOffset = kIndexOffset + 12 + 4;
    constexpr std::size_t kCellCount = 16;
    std::vector<std::uint8_t> bytes(kResourceOffset + 2 + kCellCount, 0);
    const auto write_be32 = [&bytes](std::size_t offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 24);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 3] = static_cast<std::uint8_t>(value);
    };
    write_be32(kIndexOffset, static_cast<std::uint32_t>(2 + kCellCount));
    write_be32(kIndexOffset + 8, static_cast<std::uint32_t>(kResourceOffset));
    bytes[kResourceOffset] = 12;
    bytes[kResourceOffset + 1] = 12;
    bytes[kResourceOffset + 2 + 4] = 5;
    return bytes;
}

void write_semantic_fields(BattleUnit& result) {
    const auto* spec = ccz::preview::get_battle_field_spec(
        result.source_instruction);
    result.raw_fields.assign(spec->record_stride, 0);
    for (std::size_t index = 0; index < spec->field_roles.size(); ++index) {
        switch (spec->field_roles[index]) {
        case ccz::preview::BattleFieldRole::DataId:
            result.raw_fields[index] = result.data_id;
            break;
        case ccz::preview::BattleFieldRole::Reinforcement:
            result.raw_fields[index] = result.reinforcement ? 1 : 0;
            break;
        case ccz::preview::BattleFieldRole::Hidden:
            result.raw_fields[index] = result.hidden ? 1 : 0;
            break;
        case ccz::preview::BattleFieldRole::X:
            result.raw_fields[index] = result.position.x;
            break;
        case ccz::preview::BattleFieldRole::Y:
            result.raw_fields[index] = result.position.y;
            break;
        case ccz::preview::BattleFieldRole::Direction:
            result.raw_fields[index] = result.direction;
            break;
        case ccz::preview::BattleFieldRole::Level:
            result.raw_fields[index] = result.level;
            break;
        case ccz::preview::BattleFieldRole::UnitType:
            result.raw_fields[index] = result.unit_type;
            break;
        case ccz::preview::BattleFieldRole::AiPolicy:
            result.raw_fields[index] = result.ai_policy;
            break;
        case ccz::preview::BattleFieldRole::TargetDataId:
            result.raw_fields[index] = result.target;
            break;
        case ccz::preview::BattleFieldRole::TargetX:
            result.raw_fields[index] = result.target_x;
            break;
        case ccz::preview::BattleFieldRole::TargetY:
            result.raw_fields[index] = result.target_y;
            break;
        }
    }
}

BattleUnit fixed_empty_slot(std::uint16_t instruction, std::size_t index,
                            std::size_t page, const wchar_t* source) {
    BattleUnit result;
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    result.source_instruction = instruction;
    result.source_key = source;
    result.record_index = index;
    result.page = static_cast<int>(page);
    result.fields_decoded = true;
    result.raw_fields.reserve(spec->record_stride);
    for (const auto role : spec->field_roles) {
        result.raw_fields.push_back(empty_sentinel(role));
    }
    result.data_id = -1;
    result.position = {0, 0};
    result.direction = -1;
    result.hidden = false;
    result.level = 0;
    result.unit_type = 2;
    result.reinforcement = false;
    result.ai_policy = 1;
    result.target = -1;
    result.target_x = 0;
    result.target_y = 0;
    return result;
}

BattleUnit fixed_occupied_slot(std::uint16_t instruction, std::size_t index,
                               std::size_t page, const wchar_t* source) {
    BattleUnit result = unit(
        instruction, index, page, 1000 + static_cast<int>(index),
        static_cast<int>(index % 16), 1 + static_cast<int>(index / 16), source);
    result.level = 5;
    result.unit_type = 2;
    result.ai_policy = 1;
    result.target = -1;
    result.target_x = 0;
    result.target_y = 0;
    result.reinforcement = instruction == 0x47 && (index % 2) != 0;
    write_semantic_fields(result);
    return result;
}

BattleEditorState fixed_editor_fixture(std::uint16_t instruction,
                                       std::size_t empty_index,
                                       std::size_t page = 0,
                                       const wchar_t* scope =
                                           L"battle|scene:13",
                                       const wchar_t* source =
                                           L"battle|scene:13|section:0|node:46") {
    BattleEditorState state;
    state.scope_root = scope;
    state.battle.current_page = page;
    state.battle.map.valid = true;
    state.battle.map.width = 16;
    state.battle.map.height = 8;
    state.battle.terrain.assign(128, 0);
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    for (std::size_t index = 0; index < spec->record_capacity; ++index) {
        if (index == empty_index) {
            state.battle.units.push_back(
                fixed_empty_slot(instruction, index, page, source));
        } else {
            state.battle.units.push_back(
                fixed_occupied_slot(instruction, index, page, source));
        }
    }
    state.characters.entries = {{900, L"测试角色", 2, 1, 5, 2, false, 1}};
    state.characters.page_size = 20;
    state.placement.source_key = source;
    state.placement.instruction_id = instruction;
    state.placement.page = page;
    state.placement_defaults.data_id = 900;
    state.placement_defaults.appearance = 2;
    state.placement_defaults.direction = 1;
    state.placement_defaults.level = 7;
    state.placement_defaults.unit_type = 2;
    state.placement_defaults.ai_policy = 1;
    state.mode = BattleEditorMode::Place;
    return state;
}

BattleEditorState partial_fixed_47_fixture(std::size_t missing_index) {
    auto state = fixed_editor_fixture(
        0x47, (std::numeric_limits<std::size_t>::max)());
    CCZ_REQUIRE(missing_index < state.battle.units.size());
    state.battle.units.erase(
        state.battle.units.begin() + static_cast<std::ptrdiff_t>(missing_index));
    return state;
}

void seed_stale_role_defaults(BattleEditorState& state) {
    state.placement_defaults.data_id = 100;
    state.placement_defaults.appearance = 88;
    state.placement_defaults.direction = 2;
    state.placement_defaults.hidden = true;
    state.placement_defaults.level = 17;
    state.placement_defaults.unit_type = 9;
    state.placement_defaults.reinforcement = true;
    state.placement_defaults.ai_policy = 88;
    state.placement_defaults.target = 123;
    state.placement_defaults.target_x = 7;
    state.placement_defaults.target_y = 8;
    state.direction_locked = true;
    state.mouse_direction = 3;
}

void require_safe_role_defaults(const BattleEditorState& state) {
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, -1);
    CCZ_REQUIRE_EQ(state.placement_defaults.appearance, -1);
    CCZ_REQUIRE_EQ(state.placement_defaults.direction, 0);
    CCZ_REQUIRE(!state.placement_defaults.hidden);
    CCZ_REQUIRE_EQ(state.placement_defaults.level, 0);
    CCZ_REQUIRE_EQ(state.placement_defaults.unit_type, 0);
    CCZ_REQUIRE(!state.placement_defaults.reinforcement);
    CCZ_REQUIRE_EQ(state.placement_defaults.ai_policy, -1);
    CCZ_REQUIRE_EQ(state.placement_defaults.target, -1);
    CCZ_REQUIRE_EQ(state.placement_defaults.target_x, -1);
    CCZ_REQUIRE_EQ(state.placement_defaults.target_y, -1);
    CCZ_REQUIRE(state.direction_locked);
    CCZ_REQUIRE_EQ(state.mouse_direction, 3);
}

}  // namespace

CCZ_TEST(battle_editor_character_list_pages_and_search_without_cross_scope) {
    auto state = fixture();
    BattleEditorOps ops(state);

    CCZ_REQUIRE_EQ(ops.CharacterPageCount(), std::size_t(3));
    const auto first_page = ops.VisibleCharacters();
    CCZ_REQUIRE_EQ(first_page.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(first_page[0].data_id, 100);
    CCZ_REQUIRE(ops.SetCharacterPage(1).ok);
    const auto second_page = ops.VisibleCharacters();
    CCZ_REQUIRE_EQ(second_page[0].data_id, 200);
    CCZ_REQUIRE(!ops.SelectCharacter(100).ok);
    CCZ_REQUIRE(ops.SelectCharacter(200).ok);
    CCZ_REQUIRE(ops.SetCharacterPage(2).ok);
    CCZ_REQUIRE_EQ(state.characters.selected_data_id, -1);
    CCZ_REQUIRE(ops.SetCharacterPage(1).ok);
    CCZ_REQUIRE(ops.SetCharacterSearch(L"曹").ok);
    CCZ_REQUIRE_EQ(ops.CharacterPageCount(), std::size_t(1));
    CCZ_REQUIRE_EQ(state.characters.selected_data_id, -1);
    const auto search_page = ops.VisibleCharacters();
    CCZ_REQUIRE_EQ(search_page.size(), std::size_t(1));
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 100);
}

CCZ_TEST(battle_editor_character_search_clears_place_context_and_rejects_stale_place) {
    auto state = fixture();
    BattleEditorOps ops(state);
    const auto bound = ops.SetPlacementContext(
        {kSource, 0x46, state.battle.current_page});
    CCZ_REQUIRE(bound.ok);
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    CCZ_REQUIRE(ops.SetMode(BattleEditorMode::Place).ok);
    seed_stale_role_defaults(state);
    state.selection = ccz::editor_preview::BattleSelection{kSource, 0, 0};
    state.hover = ccz::preview::BattleCoord{1, 1};

    const auto record_page = state.battle.current_page;
    CCZ_REQUIRE(ops.SetCharacterSearch(L"曹").ok);
    CCZ_REQUIRE_EQ(state.battle.current_page, record_page);
    CCZ_REQUIRE_EQ(state.characters.selected_data_id, -1);
    CCZ_REQUIRE_EQ(state.mode, BattleEditorMode::Select);
    CCZ_REQUIRE(state.placement.source_key.empty());
    require_safe_role_defaults(state);
    CCZ_REQUIRE(!state.selection.has_value());
    CCZ_REQUIRE(!state.hover.has_value());
    CCZ_REQUIRE(!ops.Place({0, 0}).ok);
}

CCZ_TEST(battle_editor_character_page_clears_place_context_and_rejects_stale_place) {
    auto state = fixture();
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetPlacementContext(
                    {kSource, 0x46, state.battle.current_page})
                    .ok);
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    CCZ_REQUIRE(ops.SetMode(BattleEditorMode::Place).ok);
    seed_stale_role_defaults(state);
    state.selection = ccz::editor_preview::BattleSelection{kSource, 0, 0};
    state.hover = ccz::preview::BattleCoord{1, 1};

    const auto record_page = state.battle.current_page;
    CCZ_REQUIRE(ops.SetCharacterPage(1).ok);
    CCZ_REQUIRE_EQ(state.battle.current_page, record_page);
    CCZ_REQUIRE_EQ(state.characters.current_page, std::size_t(1));
    CCZ_REQUIRE_EQ(state.characters.selected_data_id, -1);
    CCZ_REQUIRE_EQ(state.mode, BattleEditorMode::Select);
    CCZ_REQUIRE(state.placement.source_key.empty());
    require_safe_role_defaults(state);
    CCZ_REQUIRE(!state.selection.has_value());
    CCZ_REQUIRE(!state.hover.has_value());
    CCZ_REQUIRE(!ops.Place({0, 0}).ok);
}

CCZ_TEST(battle_editor_placement_preview_and_terrain_gate_are_atomic) {
    auto state = fixture();
    state.battle.terrain[3 * state.battle.map.width + 3] = 5;
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetCharacterPage(2).ok);
    CCZ_REQUIRE(ops.SetPlacementContext({kSource, 0x46, 0}).ok);
    CCZ_REQUIRE(ops.SelectCharacter(202).ok);

    const auto preview = ops.PreviewPlacement({0, 0});
    CCZ_REQUIRE(preview.valid);
    CCZ_REQUIRE_EQ(preview.unit.data_id, 202);
    CCZ_REQUIRE(!ops.PreviewPlacement({3, 3}).valid);
    const auto before_units = state.battle.units.size();
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    CCZ_REQUIRE_EQ(state.mode, BattleEditorMode::Modify);
    CCZ_REQUIRE_EQ(state.battle.units.size(), before_units + 1);
    CCZ_REQUIRE_EQ(state.battle.units.back().position,
                   (ccz::preview::BattleCoord{0, 0}));
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), before_units);

    state.battle.terrain.clear();
    CCZ_REQUIRE(!ops.PreviewPlacement({0, 1}).valid);
    CCZ_REQUIRE_EQ(state.battle.units.size(), before_units);
}

CCZ_TEST(battle_editor_can_stage_new_unit_without_existing_deployment_instruction) {
    auto state = fixture();
    state.placement = {};
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetCharacterPage(2).ok);
    CCZ_REQUIRE(ops.SelectCharacter(202).ok);
    const auto preview = ops.PreviewPlacement({0, 0});
    CCZ_REQUIRE(preview.valid);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    CCZ_REQUIRE_EQ(state.battle.units.back().data_id, 202);
    CCZ_REQUIRE_EQ(state.battle.units.back().position,
                   (ccz::preview::BattleCoord{0, 0}));
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(4));
}

CCZ_TEST(battle_editor_deployment_brush_stages_category_without_source_node) {
    auto state = fixture();
    state.placement = {};
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetCharacterPage(2).ok);
    CCZ_REQUIRE(ops.SelectCharacter(202).ok);
    CCZ_REQUIRE(ops.SetDeploymentInstruction(0x47).ok);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    const auto& staged = state.battle.units.back();
    CCZ_REQUIRE(staged.editor_staged_new);
    CCZ_REQUIRE_EQ(staged.source_instruction, std::uint16_t(0));
    CCZ_REQUIRE(staged.source_key.empty());
    CCZ_REQUIRE_EQ(staged.editor_deployment_instruction,
                   std::uint16_t(0x47));
    CCZ_REQUIRE(!ops.SetDeploymentInstruction(0x30).ok);
}

CCZ_TEST(battle_editor_staged_minion_delete_compacts_survivors_from_first_id) {
    auto state = fixture();
    state.placement = {};
    state.characters.entries.insert(state.characters.entries.end(), {
        {400, L"刀卫1", 1, 0, 0, 3, false, -1},
        {401, L"刀卫2", 2, 0, 0, 3, false, -1},
        {402, L"刀卫3", 3, 0, 0, 3, false, -1},
    });
    state.minion_brushes.push_back({L"刀卫", 400, 402});
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SelectMinionBrush(400).ok);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    CCZ_REQUIRE(ops.Place({0, 1}).ok);
    CCZ_REQUIRE(ops.Place({0, 2}).ok);
    CCZ_REQUIRE(ops.DeleteAt({0, 1}).ok);
    const auto first = std::find_if(state.battle.units.begin(),
        state.battle.units.end(), [](const BattleUnit& value) {
            return value.editor_staged_new &&
                   value.position == ccz::preview::BattleCoord{0, 0};
        });
    const auto last = std::find_if(state.battle.units.begin(),
        state.battle.units.end(), [](const BattleUnit& value) {
            return value.editor_staged_new &&
                   value.position == ccz::preview::BattleCoord{0, 2};
        });
    CCZ_REQUIRE(first != state.battle.units.end());
    CCZ_REQUIRE(last != state.battle.units.end());
    CCZ_REQUIRE_EQ(first->data_id, 400);
    CCZ_REQUIRE_EQ(last->data_id, 401);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.back().data_id, 402);
}

CCZ_TEST(battle_editor_wheel_in_place_mode_rotates_brush_not_selected_unit) {
    auto state = fixture();
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    CCZ_REQUIRE(ops.SelectAt({1, 1}).ok);
    const int original_direction = state.battle.units[0].direction;
    const int old_brush_direction = state.mouse_direction;
    CCZ_REQUIRE(ops.CycleDirection(1).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].direction, original_direction);
    CCZ_REQUIRE_EQ(state.mouse_direction, (old_brush_direction + 1) % 4);
    CCZ_REQUIRE_EQ(state.placement_defaults.direction, state.mouse_direction);
}

CCZ_TEST(battle_editor_place_works_with_decoded_hexzmap_terrain) {
    auto state = fixture();
    state.battle.map.map_id = 0;
    state.battle.terrain.clear();
    CCZ_REQUIRE(ccz::preview::decode_battle_hexzmap(
        four_by_four_hexzmap(), 0, state.battle.map.width,
        state.battle.map.height, state.battle.terrain));

    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetCharacterPage(2).ok);
    CCZ_REQUIRE(ops.SetPlacementContext({kSource, 0x46, 0}).ok);
    CCZ_REQUIRE(ops.SelectCharacter(202).ok);
    CCZ_REQUIRE(ops.SetMode(BattleEditorMode::Place).ok);
    CCZ_REQUIRE(ops.PreviewPlacement({0, 0}).valid);
    CCZ_REQUIRE(!ops.PreviewPlacement({0, 1}).valid);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
}

CCZ_TEST(battle_editor_modify_delete_and_undo_preserve_raw_fields) {
    auto state = fixture();
    state.battle.units[0].raw_fields[7] = 777;
    BattleEditorOps ops(state);

    BattleUnitPatch patch;
    patch.hidden = true;
    patch.level = 9;
    patch.unit_type = 4;
    patch.appearance = 12;
    CCZ_REQUIRE(ops.ModifyAt({1, 1}, patch).ok);
    CCZ_REQUIRE(state.battle.units[0].hidden);
    CCZ_REQUIRE_EQ(state.battle.units[0].level, 9);
    CCZ_REQUIRE_EQ(state.battle.units[0].unit_type, 4);
    CCZ_REQUIRE_EQ(state.battle.units[0].appearance, 12);
    CCZ_REQUIRE_EQ(state.battle.units[0].raw_fields[7], 777);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE(!state.battle.units[0].hidden);
    CCZ_REQUIRE_EQ(state.battle.units[0].level, 5);

    const auto count = state.battle.units.size();
    CCZ_REQUIRE(ops.DeleteAt({2, 1}).ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), count);
    CCZ_REQUIRE(state.battle.units[1].editor_pending_delete);
    CCZ_REQUIRE_EQ(state.battle.units[1].data_id, 101);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), count);
    CCZ_REQUIRE(!state.battle.units[1].editor_pending_delete);
    CCZ_REQUIRE_EQ(state.battle.units[1].data_id, 101);
}

CCZ_TEST(battle_editor_wheel_direction_and_lock_only_sync_current_defaults) {
    auto state = fixture();
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    CCZ_REQUIRE(ops.SelectAt({1, 1}).ok);
    CCZ_REQUIRE(ops.SetMode(BattleEditorMode::Modify).ok);
    const int old_second_direction = state.battle.units[1].direction;
    CCZ_REQUIRE(ops.CycleDirection(1).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].direction, 0);
    CCZ_REQUIRE_EQ(state.battle.units[1].direction, old_second_direction);
    CCZ_REQUIRE(ops.SetDirectionLock(true).ok);
    CCZ_REQUIRE(ops.CycleDirection(-1).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].direction, 3);
    CCZ_REQUIRE_EQ(state.placement_defaults.direction, 3);
    CCZ_REQUIRE_EQ(state.battle.units[1].direction, old_second_direction);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].direction, 0);
}

CCZ_TEST(battle_editor_painters_filter_cell_and_scope) {
    auto state = fixture();
    BattleEditorOps ops(state);

    BattlePainterValue appearance;
    appearance.integer = 12;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Appearance, {1, 1}, appearance).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].appearance, 12);
    CCZ_REQUIRE_EQ(state.battle.units[2].appearance, 1);
    CCZ_REQUIRE_EQ(state.battle.units[3].appearance, 1);

    BattlePainterValue hidden;
    hidden.boolean = true;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Hidden, {1, 1}, hidden).ok);
    CCZ_REQUIRE(state.battle.units[0].hidden);
    CCZ_REQUIRE(!state.battle.units[2].hidden);
    CCZ_REQUIRE(!state.battle.units[3].hidden);

    BattlePainterValue direction;
    direction.integer = 6;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Direction, {1, 1}, direction).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].direction, 2);

    BattlePainterValue level;
    level.integer = 10;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Level, {1, 1}, level).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].level, 10);

    BattlePainterValue type;
    type.integer = 9;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::UnitType, {1, 1}, type).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].unit_type, 9);
}

CCZ_TEST(battle_editor_target_painter_resolves_scope_data_or_cell) {
    auto state = fixture();
    BattleEditorOps ops(state);

    BattlePainterValue by_data;
    by_data.target = BattleTargetRef{BattleTargetKind::DataId, 101, {}};
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Target, {1, 1}, by_data).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].target, 101);
    CCZ_REQUIRE_EQ(state.battle.units[0].target_x, 2);
    CCZ_REQUIRE_EQ(state.battle.units[0].target_y, 1);

    BattlePainterValue by_cell;
    by_cell.target = BattleTargetRef{
        BattleTargetKind::Coordinate, -1, {2, 1}};
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Target, {1, 1}, by_cell).ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].target, 101);
    CCZ_REQUIRE(!ops.PaintAt(
        BattlePainterMode::Target, {1, 1},
        BattlePainterValue{0, false,
                           BattleTargetRef{BattleTargetKind::DataId, 999, {}}})
                      .ok);
    CCZ_REQUIRE_EQ(state.battle.units[0].target, 101);
}

CCZ_TEST(battle_editor_pan_is_clamped_to_proven_map_bounds) {
    auto state = fixture();
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SetViewportSize(2, 2).ok);
    CCZ_REQUIRE(ops.Pan(99, 99).ok);
    CCZ_REQUIRE_EQ(state.viewport.x, 2);
    CCZ_REQUIRE_EQ(state.viewport.y, 2);
    CCZ_REQUIRE(ops.Pan(-99, -99).ok);
    CCZ_REQUIRE_EQ(state.viewport.x, 0);
    CCZ_REQUIRE_EQ(state.viewport.y, 0);
    state.battle.map.valid = false;
    CCZ_REQUIRE(!ops.Pan(1, 0).ok);
    CCZ_REQUIRE_EQ(state.viewport.x, 0);
}

CCZ_TEST(battle_editor_rejects_unknown_or_cross_battle_scope_records) {
    auto state = fixture();
    auto unknown = unit(0x4b, 0, 0, 500, 0, 0, L"other|scene:0|node:4b");
    state.battle.units.push_back(unknown);
    BattleEditorOps ops(state);
    CCZ_REQUIRE(!ops.SelectAt({0, 0}).ok);
    CCZ_REQUIRE(!ops.ModifyAt({0, 0}, BattleUnitPatch{}).ok);
    CCZ_REQUIRE(!ops.PaintAt(BattlePainterMode::Hidden, {0, 0},
                             BattlePainterValue{0, true, {}})
                      .ok);
}

CCZ_TEST(battle_editor_fixed_slots_choose_first_middle_last_and_preserve_framing) {
    for (const std::size_t empty_index : {std::size_t(0), std::size_t(10),
                                          std::size_t(19)}) {
        auto state = fixed_editor_fixture(0x46, empty_index);
        BattleEditorOps ops(state);
        const auto preview = ops.PreviewPlacement({0, 0});
        CCZ_REQUIRE(preview.valid);
        CCZ_REQUIRE(preview.unit.source_key.empty());
        CCZ_REQUIRE_EQ(preview.unit.source_instruction, std::uint16_t(0));
        CCZ_REQUIRE_EQ(preview.unit.editor_deployment_instruction,
                       std::uint16_t(0x46));
        CCZ_REQUIRE_EQ(preview.unit.page, 0);
        CCZ_REQUIRE_EQ(preview.unit.raw_fields.size(), std::size_t(11));
        const auto size_before = state.battle.units.size();
        CCZ_REQUIRE(ops.Place({0, 0}).ok);
        CCZ_REQUIRE_EQ(state.battle.units.size(), size_before + 1);
        CCZ_REQUIRE_EQ(state.battle.units[empty_index].record_index,
                       empty_index);
        CCZ_REQUIRE_EQ(state.battle.units[empty_index].page, 0);
        CCZ_REQUIRE(IsBattleFixedEmptySlot(state.battle.units[empty_index]));
        const BattleUnit& staged = state.battle.units.back();
        CCZ_REQUIRE(staged.editor_staged_new);
        CCZ_REQUIRE(staged.source_key.empty());
        CCZ_REQUIRE_EQ(staged.editor_deployment_instruction,
                       std::uint16_t(0x46));
        CCZ_REQUIRE_EQ(staged.data_id, 900);
        CCZ_REQUIRE_EQ(state.battle.units[empty_index].raw_fields.size(),
                       std::size_t(11));
        const auto build = ccz::editor_preview::BuildBattleWriteback(
            state.battle, value_adapter_test_evidence());
        CCZ_REQUIRE(build.ok);
        CCZ_REQUIRE_EQ(build.records.size(), std::size_t(1));
        CCZ_REQUIRE_EQ(build.records[0].fields.size(), std::size_t(20 * 11));
    }

    auto state = fixed_editor_fixture(0x47, 79);
    BattleEditorOps ops(state);
    const auto preview = ops.PreviewPlacement({0, 0});
    CCZ_REQUIRE(preview.valid);
    CCZ_REQUIRE(preview.unit.source_key.empty());
    CCZ_REQUIRE_EQ(preview.unit.editor_deployment_instruction,
                   std::uint16_t(0x47));
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    const auto build = ccz::editor_preview::BuildBattleWriteback(
        state.battle, value_adapter_test_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE_EQ(build.records[0].fields.size(), std::size_t(80 * 12));
}

CCZ_TEST(battle_editor_treats_minus_one_data_id_as_empty_with_stale_fields) {
    BattleUnit stale_empty = unit(0x46, 7, 0, -1, 21, 22);

    CCZ_REQUIRE(IsBattleFixedEmptySlot(stale_empty));
}

CCZ_TEST(battle_editor_fixed_delete_stages_tombstone_and_undo_restores_row) {
    auto state = fixed_editor_fixture(
        0x46, (std::numeric_limits<std::size_t>::max)());
    BattleUnit before = state.battle.units[5];
    before.raw_fields[7] = 777;
    state.battle.units[5] = before;
    BattleEditorOps ops(state);
    const auto coordinate = state.battle.units[5].position;
    CCZ_REQUIRE(ops.DeleteAt(coordinate).ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(20));
    const auto& pending_delete = state.battle.units[5];
    CCZ_REQUIRE(pending_delete.editor_pending_delete);
    CCZ_REQUIRE_EQ(pending_delete.record_index, before.record_index);
    CCZ_REQUIRE_EQ(pending_delete.page, before.page);
    CCZ_REQUIRE_EQ(pending_delete.data_id, before.data_id);
    CCZ_REQUIRE_EQ(pending_delete.position, before.position);
    CCZ_REQUIRE_EQ(pending_delete.raw_fields, before.raw_fields);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(20));
    CCZ_REQUIRE_EQ(state.battle.units[5].record_index, before.record_index);
    CCZ_REQUIRE_EQ(state.battle.units[5].raw_fields, before.raw_fields);
    CCZ_REQUIRE_EQ(state.battle.units[5].data_id, before.data_id);
    CCZ_REQUIRE_EQ(state.battle.units[5].position, before.position);
    CCZ_REQUIRE_EQ(state.battle.units[5].ai_policy, before.ai_policy);

    // Even a malformed/partially decoded fixed group must keep its row in
    // place; fixed-capacity deletion may never fall back to vector erase.
    auto partial = fixed_editor_fixture(
        0x46, (std::numeric_limits<std::size_t>::max)());
    partial.battle.units.erase(partial.battle.units.begin() + 1,
                               partial.battle.units.end());
    const auto partial_size = partial.battle.units.size();
    BattleEditorOps partial_ops(partial);
    CCZ_REQUIRE(partial_ops.DeleteAt(partial.battle.units.front().position).ok);
    CCZ_REQUIRE_EQ(partial.battle.units.size(), partial_size);
    CCZ_REQUIRE(partial.battle.units.front().editor_pending_delete);
    CCZ_REQUIRE_EQ(partial.battle.units.front().data_id,
                   1000 + static_cast<int>(partial.battle.units.front().record_index));
}

CCZ_TEST(battle_editor_fixed_47_delete_stages_tombstone_and_undo_restores_row) {
    // Deletion is only a cache tombstone.  The value planner materializes the
    // native empty-row sentinel when building the eventual document change.
    auto state = fixed_editor_fixture(
        0x47, (std::numeric_limits<std::size_t>::max)());
    constexpr std::size_t kDeletedIndex = 37;
    const BattleUnit before = state.battle.units[kDeletedIndex];
    const auto coordinate = before.position;
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.DeleteAt(coordinate).ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(80));
    const auto& pending_delete = state.battle.units[kDeletedIndex];
    CCZ_REQUIRE(pending_delete.editor_pending_delete);
    CCZ_REQUIRE_EQ(pending_delete.record_index, kDeletedIndex);
    CCZ_REQUIRE_EQ(pending_delete.page, 0);
    CCZ_REQUIRE_EQ(pending_delete.data_id, before.data_id);
    CCZ_REQUIRE_EQ(pending_delete.position, before.position);
    CCZ_REQUIRE_EQ(pending_delete.raw_fields, before.raw_fields);

    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(80));
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].record_index,
                   before.record_index);
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].page, before.page);
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].raw_fields,
                   before.raw_fields);
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].data_id, before.data_id);
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].position, before.position);
    CCZ_REQUIRE_EQ(state.battle.units[kDeletedIndex].reinforcement,
                   before.reinforcement);
    CCZ_REQUIRE(!state.battle.units[kDeletedIndex].editor_pending_delete);
}

CCZ_TEST(battle_editor_partial_47_delete_undo_and_build_rejects_missing_row) {
    constexpr std::size_t kMissingIndex = 17;
    constexpr std::size_t kDeletedIndex = 5;
    auto state = partial_fixed_47_fixture(kMissingIndex);
    const BattleUnit before = state.battle.units[kDeletedIndex];
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(79));
    CCZ_REQUIRE_EQ(before.page, 0);
    CCZ_REQUIRE_EQ(before.record_index, kDeletedIndex);
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.DeleteAt(before.position).ok);
    CCZ_REQUIRE_EQ(state.battle.units.size(), std::size_t(79));
    const auto& pending_delete = state.battle.units[kDeletedIndex];
    CCZ_REQUIRE(pending_delete.editor_pending_delete);
    CCZ_REQUIRE_EQ(pending_delete.record_index, kDeletedIndex);
    CCZ_REQUIRE_EQ(pending_delete.page, before.page);
    CCZ_REQUIRE_EQ(pending_delete.source_key, before.source_key);
    CCZ_REQUIRE_EQ(pending_delete.data_id, before.data_id);
    CCZ_REQUIRE_EQ(pending_delete.raw_fields, before.raw_fields);

    const auto build = ccz::editor_preview::BuildBattleWriteback(
        state.battle, value_adapter_test_evidence());
    CCZ_REQUIRE(!build.ok);
    CCZ_REQUIRE(build.diagnostic.find(L"0x47") != std::wstring::npos);
    CCZ_REQUIRE(build.diagnostic.find(L"记录数不完整") != std::wstring::npos);

    CCZ_REQUIRE(ops.Undo().ok);
    const auto& restored = state.battle.units[kDeletedIndex];
    CCZ_REQUIRE_EQ(restored.record_index, before.record_index);
    CCZ_REQUIRE_EQ(restored.page, before.page);
    CCZ_REQUIRE_EQ(restored.source_key, before.source_key);
    CCZ_REQUIRE_EQ(restored.raw_fields, before.raw_fields);
    CCZ_REQUIRE_EQ(restored.data_id, before.data_id);
    CCZ_REQUIRE_EQ(restored.position, before.position);
    CCZ_REQUIRE_EQ(restored.direction, before.direction);
    CCZ_REQUIRE_EQ(restored.hidden, before.hidden);
    CCZ_REQUIRE_EQ(restored.level, before.level);
    CCZ_REQUIRE_EQ(restored.unit_type, before.unit_type);
    CCZ_REQUIRE_EQ(restored.reinforcement, before.reinforcement);
    CCZ_REQUIRE_EQ(restored.ai_policy, before.ai_policy);
    CCZ_REQUIRE_EQ(restored.target, before.target);
    CCZ_REQUIRE_EQ(restored.target_x, before.target_x);
    CCZ_REQUIRE_EQ(restored.target_y, before.target_y);
    CCZ_REQUIRE_EQ(restored.fields_decoded, before.fields_decoded);
    CCZ_REQUIRE(!restored.editor_pending_delete);
}

CCZ_TEST(battle_editor_appearance_is_visual_only_and_undo_restores_without_raw_output) {
    auto state = fixed_editor_fixture(
        0x46, (std::numeric_limits<std::size_t>::max)());
    BattleEditorOps ops(state);
    const auto raw_before = state.battle.units[1].raw_fields;
    const auto dirty_before = state.battle.units[1].dirty_fields;
    const auto appearance_before = state.battle.units[1].appearance;
    BattlePainterValue value;
    value.integer = 42;
    CCZ_REQUIRE(ops.PaintAt(BattlePainterMode::Appearance,
                            state.battle.units[1].position, value)
                    .ok);
    CCZ_REQUIRE_EQ(state.battle.units[1].appearance, 42);
    CCZ_REQUIRE_EQ(state.battle.units[1].raw_fields, raw_before);
    CCZ_REQUIRE_EQ(state.battle.units[1].dirty_fields, dirty_before);
    const auto build = ccz::editor_preview::BuildBattleWriteback(
        state.battle, value_adapter_test_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE_EQ(build.records[0].fields[11 + 7], raw_before[7]);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.battle.units[1].appearance, appearance_before);
    CCZ_REQUIRE_EQ(state.battle.units[1].raw_fields, raw_before);
    CCZ_REQUIRE_EQ(state.battle.units[1].dirty_fields, dirty_before);
}

CCZ_TEST(battle_editor_shows_all_battle_record_groups_without_visible_paging) {
    auto page0 = fixed_editor_fixture(0x46, 0);
    auto page1 = fixed_editor_fixture(
        0x46, 1, 1, L"battle|scene:13",
        L"battle|scene:13|section:0|node:46-page-1");
    // The second 0x46 source group is a distinct record group, not a second
    // user-facing battle view.  Keep one unit at a unique cell so the test
    // proves the current UI scope can select it without a page switch.
    page1.battle.units[0].position = {15, 7};
    page0.battle.units.insert(page0.battle.units.end(), page1.battle.units.begin(),
                              page1.battle.units.end());
    page0.characters.current_page = 2;
    BattleEditorOps ops(page0);
    CCZ_REQUIRE_EQ(ops.BattlePageCount(), std::size_t(1));
    CCZ_REQUIRE(ops.SelectAt(page0.battle.units[1].position).ok);
    CCZ_REQUIRE(ops.SelectAt(page1.battle.units[0].position).ok);
    CCZ_REQUIRE(page0.selection.has_value());
    CCZ_REQUIRE_EQ(page0.selection->page, std::size_t(1));
    CCZ_REQUIRE(!ops.SetBattlePage(1).ok);
    CCZ_REQUIRE_EQ(page0.battle.current_page, std::size_t(0));
    CCZ_REQUIRE_EQ(page0.characters.current_page, std::size_t(2));
}

CCZ_TEST(battle_editor_brush_uses_smallest_free_id_and_advances_only_after_place) {
    auto state = fixture();
    state.characters.entries.insert(state.characters.entries.end(), {
        {400, L"刀卫1", 0, 0, 0, 3, false, -1},
        {401, L"刀卫2", 0, 0, 0, 3, false, -1},
        {402, L"刀卫3", 0, 0, 0, 3, false, -1},
    });
    state.minion_brushes.push_back({L"刀卫", 400, 402});
    state.battle.units.push_back(unit(0x46, 3, 0, 402, 0, 2));
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(400).ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 400);
    CCZ_REQUIRE(ops.PreviewPlacement({0, 0}).valid);
    CCZ_REQUIRE(!ops.PreviewPlacement({100, 100}).valid);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 400);

    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    const auto first = std::find_if(state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& candidate) { return candidate.position == ccz::preview::BattleCoord{0, 0}; });
    CCZ_REQUIRE(first != state.battle.units.end());
    CCZ_REQUIRE_EQ(first->data_id, 400);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 401);

    CCZ_REQUIRE(ops.Place({0, 1}).ok);
    auto second = std::find_if(state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& candidate) { return candidate.position == ccz::preview::BattleCoord{0, 1}; });
    CCZ_REQUIRE(second != state.battle.units.end());
    CCZ_REQUIRE_EQ(second->data_id, 401);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, -1);
    CCZ_REQUIRE(!ops.PreviewPlacement({1, 0}).valid);

    CCZ_REQUIRE(ops.DeleteAt({0, 1}).ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 401);
    const auto survivor400 = std::find_if(state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& candidate) { return candidate.position == ccz::preview::BattleCoord{0, 0}; });
    const auto survivor402 = std::find_if(state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& candidate) { return candidate.position == ccz::preview::BattleCoord{0, 2}; });
    CCZ_REQUIRE(survivor400 != state.battle.units.end());
    CCZ_REQUIRE(survivor402 != state.battle.units.end());
    CCZ_REQUIRE_EQ(survivor400->data_id, 400);
    CCZ_REQUIRE_EQ(survivor402->data_id, 402);

    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, -1);
    CCZ_REQUIRE(ops.Undo().ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 401);
}

CCZ_TEST(battle_editor_character_search_clears_selected_minion_brush) {
    auto state = fixture();
    state.characters.entries.insert(state.characters.entries.end(), {
        {400, L"刀卫1", 0, 0, 0, 3, false, -1},
        {401, L"刀卫2", 0, 0, 0, 3, false, -1},
        {402, L"刀卫3", 0, 0, 0, 3, false, -1},
    });
    state.minion_brushes.push_back({L"刀卫", 400, 402});
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(400).ok);
    CCZ_REQUIRE(state.selected_minion_brush.has_value());
    CCZ_REQUIRE(ops.SetCharacterSearch(L"曹").ok);
    CCZ_REQUIRE(!state.selected_minion_brush.has_value());
    CCZ_REQUIRE_EQ(state.mode, BattleEditorMode::Select);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, -1);
}

CCZ_TEST(battle_editor_prevents_duplicate_character_data_id_placement) {
    auto state = fixture();
    BattleEditorOps ops(state);
    CCZ_REQUIRE(ops.SelectCharacter(100).ok);
    const auto preview = ops.PreviewPlacement({0, 0});
    CCZ_REQUIRE(!preview.valid);
    CCZ_REQUIRE(preview.diagnostic.find(L"Data") != std::wstring::npos);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 100);
}
