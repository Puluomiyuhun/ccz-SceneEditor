#include "BattleEditorOps.h"
#include "BattlePreviewPredicates.h"

#include <ccz/preview/battle_field_spec.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace ccz::editor_preview {
namespace {

using ccz::preview::BattleFieldRole;
using ccz::preview::BattleFieldSpec;
using ccz::preview::BattleUnit;

BattleEditorResult failure(const wchar_t* diagnostic) {
    BattleEditorResult result;
    result.diagnostic = diagnostic;
    return result;
}

BattleEditorResult success(const wchar_t* diagnostic, bool changed = false,
                           std::size_t affected_count = 0) {
    BattleEditorResult result;
    result.ok = true;
    result.changed = changed;
    result.affected_count = affected_count;
    result.diagnostic = diagnostic;
    return result;
}

bool contains_ignore_case(const std::wstring& text,
                          const std::wstring& query) {
    if (query.empty()) return true;
    if (text.size() < query.size()) return false;
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start) {
        bool match = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset) {
            if (std::towlower(text[start + offset]) !=
                std::towlower(query[offset])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool matches_query(const BattleCharacterEntry& entry,
                   const std::wstring& query) {
    if (query.empty()) return true;
    if (contains_ignore_case(entry.name, query)) return true;
    std::wstringstream id;
    id << entry.data_id;
    return contains_ignore_case(id.str(), query);
}

bool placement_profile_is_empty(const BattlePlacementProfile& profile) {
    return !profile.direction && !profile.hidden && !profile.level &&
        !profile.unit_type && !profile.reinforcement && !profile.ai_policy &&
        !profile.target && !profile.target_x && !profile.target_y;
}

std::wstring mode_name(BattlePainterMode mode) {
    switch (mode) {
    case BattlePainterMode::Appearance: return L"外观";
    case BattlePainterMode::Hidden: return L"隐藏";
    case BattlePainterMode::Direction: return L"朝向";
    case BattlePainterMode::Level: return L"等级";
    case BattlePainterMode::UnitType: return L"类型";
    case BattlePainterMode::Target: return L"目标";
    }
    return L"未知";
}

bool has_role(const BattleUnit& unit, BattleFieldRole role) {
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(
            unit.editor_deployment_instruction != 0xffff
                ? unit.editor_deployment_instruction
                : unit.source_instruction);
    if (spec == nullptr || !spec->semantic_complete) return false;
    return std::find(spec->field_roles.begin(), spec->field_roles.end(), role) !=
           spec->field_roles.end();
}

void mark_role(BattleUnit& unit, BattleFieldRole role) {
    ccz::preview::mark_battle_field_dirty(unit, role);
}

BattleSelection selection_for(const BattleUnit& unit) {
    return BattleSelection{unit.source_key, unit.record_index,
                           static_cast<std::size_t>(unit.page),
                           unit.editor_cache_id};
}

bool complete_terrain(const ccz::preview::BattlePreviewState& state) {
    if (!state.map.valid || state.map.width <= 0 || state.map.height <= 0) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(state.map.width) *
                                 static_cast<std::size_t>(state.map.height);
    return state.terrain.size() >= expected;
}

bool has_complete_source_anchor(const BattleUnit& unit) {
    if (unit.source_key.empty() || unit.page < 0 ||
        (unit.source_instruction != 0x4b &&
         unit.source_instruction != 0x46 &&
         unit.source_instruction != 0x47)) {
        return false;
    }
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(unit.source_instruction);
    return spec != nullptr && spec->semantic_complete &&
           spec->record_stride != 0 &&
           unit.raw_fields.size() == spec->record_stride &&
           unit.editor_origin_fields.size() == spec->record_stride &&
           unit.raw_fields == unit.editor_origin_fields;
}

void clear_character_dependent_state(BattleEditorState& state) {
    // A query/page change invalidates the selected role and any active move.
    // Keep generic wheel/lock and deployment-brush settings; a new role can
    // be placed without binding an existing EEX source instruction.
    const bool direction_locked = state.direction_locked;
    const std::uint16_t deployment_instruction =
        state.placement.instruction_id == 0x4b ||
                state.placement.instruction_id == 0x46 ||
                state.placement.instruction_id == 0x47
            ? state.placement.instruction_id : 0x4b;
    state.characters.selected_data_id = -1;
    state.selected_minion_brush.reset();
    state.selected_minion_brush_batch_id = 0;
    state.mode = BattleEditorMode::Select;
    state.mode_before_position_adjustment = BattleEditorMode::Select;
    state.adjusting_cache_id = 0;
    state.position_adjustment_direction = -1;
    state.placement = {};
    state.placement.instruction_id = deployment_instruction;
    state.placement_defaults = BattlePlacementDefaults{};
    state.direction_locked = direction_locked;
    state.selection.reset();
    state.selection_group.clear();
    state.hover.reset();
}

bool same_source_row(const BattleUnit& left, const BattleUnit& right) {
    return !left.editor_staged_new && !right.editor_staged_new &&
        left.source_key == right.source_key &&
        left.source_instruction == right.source_instruction &&
        left.record_index == right.record_index && left.page == right.page;
}

int unit_role_value(const BattleUnit& unit, BattleFieldRole role) {
    switch (role) {
    case BattleFieldRole::DataId: return unit.data_id;
    case BattleFieldRole::DeploymentIndex: return unit.deployment_index;
    case BattleFieldRole::Reinforcement: return unit.reinforcement ? 1 : 0;
    case BattleFieldRole::Hidden: return unit.hidden ? 1 : 0;
    case BattleFieldRole::X: return unit.position.x;
    case BattleFieldRole::Y: return unit.position.y;
    case BattleFieldRole::Direction: return unit.direction;
    case BattleFieldRole::Level: return unit.level;
    case BattleFieldRole::UnitType: return unit.unit_type;
    case BattleFieldRole::AiPolicy: return unit.ai_policy;
    case BattleFieldRole::TargetDataId: return unit.target;
    case BattleFieldRole::TargetX: return unit.target_x;
    case BattleFieldRole::TargetY: return unit.target_y;
    }
    return 0;
}

void copy_unit_role(const BattleUnit& source, BattleUnit& target,
                    BattleFieldRole role) {
    switch (role) {
    case BattleFieldRole::DataId: target.data_id = source.data_id; break;
    case BattleFieldRole::DeploymentIndex:
        target.deployment_index = source.deployment_index; break;
    case BattleFieldRole::Reinforcement:
        target.reinforcement = source.reinforcement; break;
    case BattleFieldRole::Hidden: target.hidden = source.hidden; break;
    case BattleFieldRole::X: target.position.x = source.position.x; break;
    case BattleFieldRole::Y: target.position.y = source.position.y; break;
    case BattleFieldRole::Direction: target.direction = source.direction; break;
    case BattleFieldRole::Level: target.level = source.level; break;
    case BattleFieldRole::UnitType: target.unit_type = source.unit_type; break;
    case BattleFieldRole::AiPolicy: target.ai_policy = source.ai_policy; break;
    case BattleFieldRole::TargetDataId: target.target = source.target; break;
    case BattleFieldRole::TargetX: target.target_x = source.target_x; break;
    case BattleFieldRole::TargetY: target.target_y = source.target_y; break;
    }
}

const wchar_t* role_label(BattleFieldRole role) {
    switch (role) {
    case BattleFieldRole::DataId: return L"Data号";
    case BattleFieldRole::DeploymentIndex: return L"出场槽";
    case BattleFieldRole::Reinforcement: return L"援军标记";
    case BattleFieldRole::Hidden: return L"隐藏";
    case BattleFieldRole::X: return L"X坐标";
    case BattleFieldRole::Y: return L"Y坐标";
    case BattleFieldRole::Direction: return L"方向";
    case BattleFieldRole::Level: return L"等级";
    case BattleFieldRole::UnitType: return L"单位类型";
    case BattleFieldRole::AiPolicy: return L"AI策略";
    case BattleFieldRole::TargetDataId: return L"目标Data号";
    case BattleFieldRole::TargetX: return L"目标X坐标";
    case BattleFieldRole::TargetY: return L"目标Y坐标";
    }
    return L"未知字段";
}

bool has_manual_appearance_override(const BattleEditorState& state,
                                    const BattleUnit& unit) {
    const auto character = std::find_if(
        state.characters.entries.begin(), state.characters.entries.end(),
        [&unit](const BattleCharacterEntry& entry) {
            return entry.data_id == unit.data_id;
        });
    return character != state.characters.entries.end() &&
        character->appearance != unit.appearance;
}

std::wstring source_row_label(const BattleUnit& unit) {
    std::wostringstream stream;
    stream << L"来源 " << unit.source_key << L" / 指令 0x" << std::hex
           << std::uppercase << unit.source_instruction << std::dec
           << L" / 记录 " << unit.record_index + 1;
    return stream.str();
}

}  // namespace

BattleEditorOps::BattleEditorOps(BattleEditorState& state) noexcept
    : state_(&state) {
    for (BattleUnit& unit : state.battle.units) {
        if (!unit.fields_decoded || IsBattleFixedEmptySlot(unit)) continue;
        if (unit.editor_cache_id == 0) {
            unit.editor_cache_id = state.next_cache_id++;
        } else if (unit.editor_cache_id >= state.next_cache_id &&
                   unit.editor_cache_id !=
                       (std::numeric_limits<std::uint64_t>::max)()) {
            state.next_cache_id = unit.editor_cache_id + 1;
        }
        if (unit.editor_deployment_instruction == 0xffff)
            unit.editor_deployment_instruction = unit.source_instruction;
    }
}

std::vector<std::size_t> BattleEditorOps::MatchingCharacterIndices(
    const BattleCharacterListState& characters) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < characters.entries.size(); ++index) {
        if (matches_query(characters.entries[index], characters.query)) {
            indices.push_back(index);
        }
    }
    return indices;
}

std::size_t BattleEditorOps::CharacterPageCount() const noexcept {
    if (state_ == nullptr || state_->characters.page_size == 0) return 0;
    const std::size_t count = MatchingCharacterIndices(state_->characters).size();
    return (count + state_->characters.page_size - 1) /
           state_->characters.page_size;
}

std::size_t BattleEditorOps::BattlePageCount() const noexcept {
    if (state_ == nullptr) return 0;
    // `BattleUnit::page` is the source-record group assigned while parsing
    // repeated 0x47/0x4B instructions.  It is provenance for writeback and
    // must not become a user-facing battle page: the original arranger shows
    // every unit in the current Battle scope at once.
    for (const BattleUnit& unit : state_->battle.units) {
        if (IsCurrentUnit(*state_, unit)) return 1;
    }
    return 0;
}

std::vector<BattleCharacterEntry> BattleEditorOps::VisibleCharacters() const {
    std::vector<BattleCharacterEntry> result;
    if (state_ == nullptr || state_->characters.page_size == 0) return result;
    const std::vector<std::size_t> indices =
        MatchingCharacterIndices(state_->characters);
    const std::size_t begin = state_->characters.current_page *
                              state_->characters.page_size;
    if (begin >= indices.size()) return result;
    const std::size_t end =
        (std::min)(indices.size(), begin + state_->characters.page_size);
    result.reserve(end - begin);
    for (std::size_t position = begin; position < end; ++position) {
        result.push_back(state_->characters.entries[indices[position]]);
    }
    return result;
}

std::vector<int> BattleEditorOps::CurrentDeployedCharacterDataIds() const {
    std::vector<int> data_ids;
    if (state_ == nullptr) return data_ids;
    data_ids.reserve(state_->battle.units.size());
    for (const BattleUnit& unit : state_->battle.units) {
        if (!IsCurrentUnit(*state_, unit) || unit.data_id < 0) continue;
        data_ids.push_back(unit.data_id);
    }
    std::sort(data_ids.begin(), data_ids.end());
    data_ids.erase(std::unique(data_ids.begin(), data_ids.end()),
                   data_ids.end());
    return data_ids;
}

BattleEditorResult BattleEditorOps::SetCharacterSearch(
    const std::wstring& query) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    state_->characters.query = query;
    state_->characters.current_page = 0;
    // A new query changes the visible page contract even when it still has
    // results.  Do not leave a selection from the previous query looking
    // valid to the window bridge or to a later placement click.
    clear_character_dependent_state(*state_);
    return success(L"战场角色检索已更新");
}

BattleEditorResult BattleEditorOps::SetCharacterPage(std::size_t page) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::size_t pages = CharacterPageCount();
    if (pages == 0 || page >= pages) {
        return failure(L"战场角色页码越界，未切换");
    }
    state_->characters.current_page = page;
    clear_character_dependent_state(*state_);
    return success(L"战场角色页码已切换");
}

BattleEditorResult BattleEditorOps::SetBattlePage(std::size_t page) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (page != 0) {
        return failure(L"战场单位视图不分页，所有记录同时显示");
    }
    // Keep the legacy field stable for callers compiled against the old
    // bridge, but never use it as a visibility filter.
    state_->battle.current_page = 0;
    return success(L"战场单位视图固定显示全部记录");
}

BattleEditorResult BattleEditorOps::SelectCharacter(int data_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<BattleCharacterEntry> visible = VisibleCharacters();
    auto entry = std::find_if(visible.begin(), visible.end(),
                              [data_id](const BattleCharacterEntry& value) {
                                  return value.data_id == data_id;
                              });
    if (entry == visible.end()) {
        return failure(L"角色不在当前战场角色页，拒绝切换");
    }
    state_->selected_minion_brush.reset();
    state_->selected_minion_brush_batch_id = 0;
    state_->characters.selected_data_id = data_id;
    state_->placement_defaults.data_id = entry->data_id;
    state_->placement_defaults.appearance = entry->appearance;
    state_->placement_defaults.direction =
        WrappedDirection(entry->default_direction);
    state_->mouse_direction = state_->placement_defaults.direction;
    state_->placement_defaults.level = entry->level;
    state_->placement_defaults.unit_type = entry->unit_type;
    state_->placement_defaults.reinforcement = entry->reinforcement;
    state_->placement_defaults.ai_policy = entry->ai_policy;
    ApplyPlacementProfileToDefaults();
    state_->mode = BattleEditorMode::Place;
    return success(L"当前战场角色与放置参数已切换");
}

int BattleEditorOps::NextAvailableBrushDataId(
    const BattleMinionBrush& brush) const {
    if (state_ == nullptr || brush.first_data_id < 0 ||
        brush.last_data_id < brush.first_data_id) return -1;
    if (state_->copy_person_mode) return brush.first_data_id;
    for (int data_id = brush.first_data_id; data_id <= brush.last_data_id;
         ++data_id) {
        const bool occupied = std::any_of(
            state_->battle.units.begin(), state_->battle.units.end(),
            [this, data_id](const BattleUnit& unit) {
                return IsCurrentUnit(*state_, unit) && unit.data_id == data_id;
            });
        if (!occupied) return data_id;
    }
    return -1;
}

bool BattleEditorOps::RefreshSelectedBrushDataId(bool reset_direction) {
    if (state_ == nullptr || !state_->selected_minion_brush.has_value())
        return false;

    const int retained_direction = state_->mouse_direction;
    const BattleMinionBrush brush = *state_->selected_minion_brush;
    int data_id = -1;
    int batch_first_id = -1;
    std::size_t batch_count = 0;
    bool batch_metadata_valid = true;
    bool close_batch = false;
    for (const BattleUnit& unit : state_->battle.units) {
        if (!unit.editor_staged_new ||
            unit.editor_minion_brush_batch_id !=
                state_->selected_minion_brush_batch_id) continue;
        if (unit.editor_minion_brush_first_id < 0 ||
            (batch_first_id >= 0 &&
             unit.editor_minion_brush_first_id != batch_first_id)) {
            batch_metadata_valid = false;
            break;
        }
        batch_first_id = unit.editor_minion_brush_first_id;
        ++batch_count;
    }

    if (batch_metadata_valid && batch_count == 0) {
        // Copy mode intentionally starts each brush batch at the range's
        // first ID. Batch provenance/cache IDs keep repeated Data IDs safe.
        data_id = NextAvailableBrushDataId(brush);
    } else if (batch_metadata_valid && batch_first_id >= 0) {
        const long long next_id = static_cast<long long>(batch_first_id) +
            static_cast<long long>(batch_count);
        if (next_id >= brush.first_data_id && next_id <= brush.last_data_id) {
            const int candidate = static_cast<int>(next_id);
            const bool occupied = std::any_of(
                state_->battle.units.begin(), state_->battle.units.end(),
                [this, candidate](const BattleUnit& unit) {
                    if (!IsCurrentUnit(*state_, unit) ||
                        unit.data_id != candidate) return false;
                    return !state_->copy_person_mode ||
                        (unit.editor_staged_new &&
                         unit.editor_minion_brush_batch_id ==
                             state_->selected_minion_brush_batch_id);
                });
            if (!occupied) {
                data_id = candidate;
            } else {
                close_batch = true;
            }
        } else if (next_id < brush.first_data_id) {
            close_batch = true;
        }
    }

    if (!batch_metadata_valid) close_batch = true;
    if (close_batch) {
        // A batch cannot jump over an occupied Data ID. Close it so that an
        // explicit reselection starts a new contiguous batch at the next free
        // run, which the writeback planner can represent safely.
        state_->selected_minion_brush.reset();
        state_->selected_minion_brush_batch_id = 0;
        state_->mode = BattleEditorMode::Select;
    }
    state_->placement_defaults.data_id = data_id;
    state_->characters.selected_data_id = data_id;
    if (data_id < 0) return false;
    const auto entry = std::find_if(
        state_->characters.entries.begin(), state_->characters.entries.end(),
        [data_id](const BattleCharacterEntry& character) {
            return character.data_id == data_id;
        });
    if (entry == state_->characters.entries.end()) {
        state_->placement_defaults.data_id = -1;
        state_->characters.selected_data_id = -1;
        state_->selected_minion_brush.reset();
        state_->selected_minion_brush_batch_id = 0;
        state_->mode = BattleEditorMode::Select;
        return false;
    }
    state_->placement_defaults.appearance = entry->appearance;
    state_->placement_defaults.direction = reset_direction
        ? WrappedDirection(entry->default_direction)
        : WrappedDirection(retained_direction);
    state_->placement_defaults.level = entry->level;
    state_->placement_defaults.unit_type = entry->unit_type;
    state_->placement_defaults.reinforcement = entry->reinforcement;
    state_->placement_defaults.ai_policy = entry->ai_policy;
    ApplyPlacementProfileToDefaults();
    state_->mouse_direction = state_->placement_defaults.direction;
    return true;
}

BattleEditorResult BattleEditorOps::SelectMinionBrush(int first_data_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->next_minion_brush_batch_id == 0) {
        return failure(L"小兵笔刷布置批次编号已耗尽");
    }
    const auto brush = std::find_if(
        state_->minion_brushes.begin(), state_->minion_brushes.end(),
        [first_data_id](const BattleMinionBrush& candidate) {
            return candidate.first_data_id == first_data_id;
        });
    if (brush == state_->minion_brushes.end()) {
        return failure(L"小兵笔刷不在当前 S 角色目录中");
    }
    state_->selected_minion_brush = *brush;
    state_->selected_minion_brush_batch_id =
        state_->next_minion_brush_batch_id;
    if (state_->next_minion_brush_batch_id ==
        (std::numeric_limits<std::uint64_t>::max)()) {
        state_->next_minion_brush_batch_id = 0;
    } else {
        ++state_->next_minion_brush_batch_id;
    }
    state_->characters.selected_data_id = -1;
    state_->mode = BattleEditorMode::Place;
    RefreshSelectedBrushDataId(true);
    return state_->placement_defaults.data_id < 0
        ? success(L"小兵笔刷已选中，但组内 Data 号已耗尽")
        : success(L"小兵笔刷已选中，从组内最小可用 Data 号开始");
}

BattleEditorResult BattleEditorOps::SetPlacementParameters(
    const BattlePlacementDefaults& parameters) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (parameters.data_id < 0) {
        return failure(L"放置角色 Data 号无效，拒绝切换");
    }
    BattleEditorState proposed = *state_;
    proposed.placement_defaults = parameters;
    proposed.placement_defaults.direction = WrappedDirection(parameters.direction);
    proposed.mouse_direction = proposed.placement_defaults.direction;
    *state_ = std::move(proposed);
    return success(L"战场放置参数已更新");
}

BattleEditorResult BattleEditorOps::SetPlacementProfile(
    std::uint16_t instruction_id,
    const BattlePlacementProfile& profile) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(instruction_id);
    if (spec == nullptr || !spec->semantic_complete ||
        (instruction_id != 0x46 && instruction_id != 0x47 &&
         instruction_id != 0x4b)) {
        return failure(L"不支持的战场出场参数类别");
    }
    if (profile.direction &&
        (*profile.direction < -1 || *profile.direction > 3)) {
        return failure(L"朝向值超出默认方向或四向编码范围");
    }
    if (profile.ai_policy &&
        (*profile.ai_policy < 0 || *profile.ai_policy > 6)) {
        return failure(L"新增单位默认方针必须选择有效的七种方针之一");
    }
    const bool any_target = profile.target || profile.target_x ||
        profile.target_y;
    if (any_target &&
        (!profile.target || !profile.target_x || !profile.target_y)) {
        return failure(L"默认目标必须同时提供武将 Data 号与坐标");
    }
    // Store one maximum-field profile. New placements apply only the roles
    // supported by their own deployment instruction.
    if (placement_profile_is_empty(profile)) {
        state_->placement_profiles.erase(0x47);
    } else {
        state_->placement_profiles[0x47] = profile;
    }
    ApplyPlacementProfileToDefaults();
    return success(L"当前分支的共享新增单位默认参数已更新；摆放时按阵营适配", true);
}

BattleEditorResult BattleEditorOps::ResetPlacementProfile(
    std::uint16_t instruction_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (instruction_id != 0x46 && instruction_id != 0x47 &&
        instruction_id != 0x4b) {
        return failure(L"不支持的战场出场参数类别");
    }
    state_->placement_profiles.erase(0x47);
    const int data_id = state_->placement_defaults.data_id;
    const auto entry = std::find_if(
        state_->characters.entries.begin(), state_->characters.entries.end(),
        [data_id](const BattleCharacterEntry& character) {
            return character.data_id == data_id;
        });
    state_->placement_defaults.direction = entry == state_->characters.entries.end()
        ? 0 : WrappedDirection(entry->default_direction);
    state_->placement_defaults.hidden = false;
    state_->placement_defaults.level = entry == state_->characters.entries.end()
        ? 0 : entry->level;
    state_->placement_defaults.unit_type = 2;
    state_->placement_defaults.reinforcement = entry != state_->characters.entries.end() &&
        entry->reinforcement;
    state_->placement_defaults.ai_policy = 0;
    state_->placement_defaults.target = -1;
    state_->placement_defaults.target_x = -1;
    state_->placement_defaults.target_y = -1;
    state_->mouse_direction = state_->placement_defaults.direction;
    return success(L"共享新增参数已恢复基础默认值：高级兵种、被动出击", true);
}

void BattleEditorOps::ApplyPlacementProfileToDefaults() {
    if (state_ == nullptr) return;
    // Shared new-unit defaults start at the editor's standard enemy/ally
    // values. Character records still provide identity, direction and level;
    // an explicit shared profile can override these two values.
    state_->placement_defaults.unit_type = 2;
    state_->placement_defaults.ai_policy = 0;
    const auto found = state_->placement_profiles.find(0x47);
    if (found == state_->placement_profiles.end()) return;
    const BattlePlacementProfile& profile = found->second;
    BattlePlacementDefaults& defaults = state_->placement_defaults;
    if (profile.direction) {
        defaults.direction = *profile.direction == -1
            ? -1 : WrappedDirection(*profile.direction);
        state_->mouse_direction = defaults.direction;
    }
    if (profile.hidden) defaults.hidden = *profile.hidden;
    if (profile.level) defaults.level = *profile.level;
    if (profile.unit_type) defaults.unit_type = *profile.unit_type;
    if (profile.reinforcement) defaults.reinforcement = *profile.reinforcement;
    if (profile.ai_policy && *profile.ai_policy >= 0 &&
        *profile.ai_policy <= 6) {
        defaults.ai_policy = *profile.ai_policy;
    }
    if (profile.target) defaults.target = *profile.target;
    if (profile.target_x) defaults.target_x = *profile.target_x;
    if (profile.target_y) defaults.target_y = *profile.target_y;
}

BattleEditorResult BattleEditorOps::SetPlacementContext(
    const BattlePlacementContext& context) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(context.instruction_id);
    if (spec == nullptr || !spec->semantic_complete) {
        return failure(L"战场放置来源字段未闭合，拒绝切换");
    }
    if (!context.source_key.empty() &&
        !InScope(context.source_key, state_->scope_root)) {
        return failure(L"战场放置来源越过当前 Battle 作用域");
    }
    const bool changed = state_->placement.source_key != context.source_key ||
        state_->placement.instruction_id != context.instruction_id ||
        state_->placement.page != context.page;
    state_->placement = context;
    if (changed) ClearUndo();
    return success(L"战场放置来源已绑定当前作用域");
}

BattleEditorResult BattleEditorOps::SetDeploymentInstruction(
    std::uint16_t instruction_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (instruction_id != 0x4b && instruction_id != 0x46 &&
        instruction_id != 0x47) {
        return failure(L"不支持的战场出场类别");
    }
    state_->placement = {};
    state_->placement.instruction_id = instruction_id;
    ApplyPlacementProfileToDefaults();
    return success(L"战场布置笔刷类别已切换；无需既有出场指令");
}

BattleEditorResult BattleEditorOps::SetMode(BattleEditorMode mode) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (mode == BattleEditorMode::AdjustPosition &&
        state_->adjusting_cache_id == 0) {
        return failure(L"位置调整必须先通过单位菜单指定目标单位");
    }
    if (state_->mode == BattleEditorMode::AdjustPosition &&
        mode != BattleEditorMode::AdjustPosition) {
        state_->adjusting_cache_id = 0;
        state_->position_adjustment_direction = -1;
        state_->hover.reset();
    }
    state_->mode = mode;
    return success(L"战场编辑模式已切换");
}

BattleEditorResult BattleEditorOps::SetCopyPersonMode(bool enabled) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    state_->copy_person_mode = enabled;
    return success(enabled ? L"复制人模式已开启" : L"复制人模式已关闭");
}

BattleEditorResult BattleEditorOps::SetUnrestrictedTerrainMode(bool enabled) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    state_->unrestricted_terrain_mode = enabled;
    return success(enabled ? L"无地形限制模式已开启" : L"无地形限制模式已关闭");
}

BattleEditorResult BattleEditorOps::CancelPlacementBrush() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode != BattleEditorMode::Place) {
        return failure(L"当前没有活动的战场布置笔刷");
    }
    state_->selected_minion_brush.reset();
    state_->selected_minion_brush_batch_id = 0;
    state_->characters.selected_data_id = -1;
    state_->placement_defaults = BattlePlacementDefaults{};
    state_->mode = BattleEditorMode::Select;
    state_->mode_before_position_adjustment = BattleEditorMode::Select;
    state_->adjusting_cache_id = 0;
    state_->position_adjustment_direction = -1;
    state_->hover.reset();
    return success(L"战场布置笔刷已取消");
}

BattleEditorResult BattleEditorOps::SetDirectionLock(bool enabled) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    state_->direction_locked = enabled;
    if (enabled) state_->placement_defaults.direction = state_->mouse_direction;
    return success(enabled ? L"战场方向锁已开启" : L"战场方向锁已关闭");
}

int BattleEditorOps::WrappedDirection(int value) {
    value %= 4;
    if (value < 0) value += 4;
    return value;
}

bool BattleEditorOps::InScope(
    const ccz::preview::LogicalNodeKey& source_key,
    const ccz::preview::LogicalNodeKey& scope_root) {
    if (source_key.empty() || scope_root.empty()) return false;
    if (source_key == scope_root) return true;
    if (source_key.size() <= scope_root.size() ||
        source_key.compare(0, scope_root.size(), scope_root) != 0) {
        return false;
    }
    const wchar_t separator = source_key[scope_root.size()];
    return separator == L'|' || separator == L'/';
}

bool BattleEditorOps::HasRole(const BattleUnit& unit, BattleFieldRole role) {
    return has_role(unit, role);
}

bool BattleEditorOps::IsCurrentUnit(const BattleEditorState& state,
                                    const BattleUnit& unit) {
    return unit.fields_decoded &&
           !unit.editor_pending_delete &&
           (unit.editor_staged_new || InScope(unit.source_key, state.scope_root)) &&
           !IsBattleFixedEmptySlot(unit);
}

std::vector<std::size_t> BattleEditorOps::UnitsAt(
    const BattleEditorState& state,
    const ccz::preview::BattleCoord& coordinate) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < state.battle.units.size(); ++index) {
        const BattleUnit& unit = state.battle.units[index];
        if (IsCurrentUnit(state, unit) && unit.position == coordinate) {
            result.push_back(index);
        }
    }
    return result;
}

bool BattleEditorOps::CompleteTerrain(const BattleEditorState& state) {
    return complete_terrain(state.battle);
}

bool BattleEditorOps::TerrainAllowsUnit(const ccz::preview::BattleCoord& coordinate,
 const ccz::preview::BattleUnit& unit,std::wstring& diagnostic) const {
 if(state_ && unit.source_instruction==0x4b && !unit.editor_staged_new){
  const auto binding=state_->own_unit_bindings.find(unit.deployment_index);
  if(binding!=state_->own_unit_bindings.end())return TerrainAllows(coordinate,binding->second,diagnostic);
  return TerrainAllows(coordinate,-1,diagnostic,0);
 }
 return TerrainAllows(coordinate,unit.data_id,diagnostic);
}

bool BattleEditorOps::TerrainAllows(
    const ccz::preview::BattleCoord& coordinate, int data_id,
    std::wstring& diagnostic, int movement_class_override) const {
    if (state_ == nullptr) {
        diagnostic = L"战场编辑器状态为空，无法校验地形";
        return false;
    }
    if (state_->unrestricted_terrain_mode) return true;
    if (!CompleteTerrain(*state_) || !ccz::preview::is_battle_coord_in_bounds(
            coordinate, state_->battle.map.width,
            state_->battle.map.height)) {
        diagnostic = L"战场地图/地形未闭合或坐标越界，拒绝放置";
        return false;
    }
    const auto character = std::find_if(
        state_->characters.entries.begin(), state_->characters.entries.end(),
        [data_id](const BattleCharacterEntry& entry) {
            return entry.data_id == data_id;
        });
    if (movement_class_override < 0 && (data_id < 0 || character == state_->characters.entries.end())) {
        diagnostic = L"无法确定该单位的 Data.e5 兵种，地形校验失败";
        return false;
    }
    const std::size_t terrain_offset =
        static_cast<std::size_t>(coordinate.y) *
            static_cast<std::size_t>(state_->battle.map.width) +
        static_cast<std::size_t>(coordinate.x);
    if (terrain_offset >= state_->battle.terrain.size()) {
        diagnostic = L"战场目标格没有有效的 Hexzmap 地形编号";
        return false;
    }
    const int terrain_id = state_->battle.terrain[terrain_offset];
    if (!state_->movement_terrain) {
        diagnostic = L"当前 Mod 的兵种地形表未预载，拒绝放置";
        return false;
    }
    const auto cost = ccz::preview::battle_movement_terrain_cost(
        *state_->movement_terrain, movement_class_override >= 0 ? movement_class_override : character->movement_class_code, terrain_id);
    if (!cost.has_value()) {
        diagnostic = L"Data.e5 兵种地形表、兵种码或地形编号无效，拒绝放置";
        return false;
    }
    if (*cost == 0xff) {
        diagnostic = L"该兵种无法进入目标地形，拒绝放置";
        return false;
    }
    diagnostic.clear();
    return true;
}

std::wstring BattleEditorOps::UnitSummary(const BattleUnit& unit) {
    std::wstringstream result;
    const std::uint16_t deployment =
        unit.editor_deployment_instruction != 0xffff
            ? unit.editor_deployment_instruction : unit.source_instruction;
    if (unit.editor_staged_new) {
        result << L"draft(cache=" << unit.editor_cache_id << L", output=0x"
               << std::hex << std::uppercase << deployment << std::dec << L")";
    } else {
        result << L"source=0x" << std::hex << std::uppercase
               << unit.source_instruction << std::dec << L"#"
               << unit.record_index << L"/group" << unit.page
               << L" cache=" << unit.editor_cache_id;
    }
    result << L" data=" << unit.data_id << L" pos=(" << unit.position.x << L","
           << unit.position.y << L") dir=" << WrappedDirection(unit.direction)
           << L" hidden=" << (unit.hidden ? 1 : 0) << L" level=" << unit.level
           << L" type=" << unit.unit_type << L" appearance="
           << unit.appearance << L" target=" << unit.target << L" ("
           << unit.target_x << L"," << unit.target_y << L")"
           << (unit.editor_pending_delete ? L" pending-delete" : L"");
    return result.str();
}

std::wstring BattleEditorOps::Summary(const BattleEditorState& state) {
    std::wstringstream result;
    result << L"view=all units="
           << state.battle.units.size();
    for (const BattleUnit& unit : state.battle.units) {
        result << L" [" << UnitSummary(unit) << L"]";
    }
    return result.str();
}

BattleWritebackMemoryState BattleEditorOps::ToMemoryState(
    const BattleEditorState& state) {
    BattleWritebackMemoryState memory;
    memory.battle = state.battle;
    memory.document_modified = state.document_modified;
    if (state.selection.has_value()) {
        memory.selection.source_key = state.selection->source_key;
        memory.selection.record_index = state.selection->record_index;
        memory.selection.page = state.selection->page;
        memory.selection.cache_id = state.selection->cache_id;
    }
    memory.selection_group_cache_ids.reserve(state.selection_group.size());
    for (const BattleSelection& selected : state.selection_group) {
        if (selected.cache_id != 0)
            memory.selection_group_cache_ids.push_back(selected.cache_id);
    }
    return memory;
}

void BattleEditorOps::ApplyMemoryState(
    BattleEditorState& state, const BattleWritebackMemoryState& memory) {
    state.battle = memory.battle;
    state.document_modified = memory.document_modified;
    if (memory.selection.cache_id == 0 && memory.selection.source_key.empty()) {
        state.selection.reset();
    } else {
        state.selection = BattleSelection{memory.selection.source_key,
                                          memory.selection.record_index,
                                          memory.selection.page,
                                          memory.selection.cache_id};
    }
    state.selection_group.clear();
    for (const std::uint64_t cache_id : memory.selection_group_cache_ids) {
        const auto unit = std::find_if(
            state.battle.units.begin(), state.battle.units.end(),
            [cache_id](const BattleUnit& candidate) {
                return candidate.editor_cache_id == cache_id &&
                    !candidate.editor_pending_delete &&
                    candidate.fields_decoded &&
                    !IsBattleFixedEmptySlot(candidate);
            });
        if (unit != state.battle.units.end())
            state.selection_group.push_back(selection_for(*unit));
    }
}

BattleEditorResult BattleEditorOps::CommitMutation(
    const BattleEditorState& proposed, std::size_t affected_count,
    const wchar_t* diagnostic) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const BattleWritebackMemoryState before = ToMemoryState(*state_);
    BattleWritebackMemoryState after = ToMemoryState(proposed);
    BattleWritebackTransaction transaction;
    if (!transaction.Begin(before) || !transaction.Stage(after) ||
        !transaction.CommitToMemory(after)) {
        return failure(L"战场编辑事务提交失败，状态保持不变");
    }
    BattleEditorResult result = success(diagnostic, true, affected_count);
    result.before_summary = Summary(*state_);
    BattleEditorState applied = proposed;
    ApplyMemoryState(applied, after);
    result.after_summary = Summary(applied);
    *state_ = std::move(applied);
    undo_stack_.push_back(std::move(transaction));
    return result;
}

bool BattleEditorOps::CanUsePlacementContext(std::wstring& diagnostic) const {
    if (state_ == nullptr) {
        diagnostic = L"战场编辑器状态为空";
        return false;
    }
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(state_->placement.instruction_id);
    if (spec == nullptr || !spec->semantic_complete) {
        diagnostic = L"战场放置指令字段未闭合";
        return false;
    }
    if (state_->scope_root.empty() ||
        (!state_->placement.source_key.empty() &&
         !InScope(state_->placement.source_key, state_->scope_root))) {
        diagnostic = L"战场放置来源越过当前 Battle 作用域";
        return false;
    }
    if (state_->placement_defaults.data_id < 0) {
        diagnostic = state_->selected_minion_brush.has_value()
            ? L"小兵笔刷组内 Data 号已耗尽"
            : L"尚未选择当前战场角色";
        return false;
    }
    return true;
}

BattlePlacementPreview BattleEditorOps::BuildPlacementPreview(
    const ccz::preview::BattleCoord& coordinate) const {
    BattlePlacementPreview result;
    result.coordinate = coordinate;
    std::wstring diagnostic;
    if (!CanUsePlacementContext(diagnostic)) {
        result.diagnostic = std::move(diagnostic);
        return result;
    }
    if (!CompleteTerrain(*state_)) {
        result.diagnostic = L"战场地图/地形未闭合，拒绝放置预览";
        return result;
    }
    std::size_t own_count = 0;
    std::size_t ally_count = 0;
    std::size_t enemy_count = 0;
    for (const BattleUnit& existing : state_->battle.units) {
        if (!IsCurrentUnit(*state_, existing)) continue;
        const std::uint16_t instruction =
            existing.editor_deployment_instruction != 0xffff
                ? existing.editor_deployment_instruction
                : existing.source_instruction;
        switch (instruction) {
        case 0x4b: ++own_count; break;
        case 0x46: ++ally_count; break;
        case 0x47: ++enemy_count; break;
        default: break;
        }
    }
    const std::size_t total_count = own_count + ally_count + enemy_count;
    const std::uint16_t instruction = state_->placement.instruction_id;
    if ((instruction == 0x4b && own_count >= 20) ||
        (instruction == 0x46 && ally_count >= 40) ||
        (instruction == 0x47 && enemy_count >= 190)) {
        result.diagnostic = L"对应阵营已达到战场人数上限，拒绝继续添加";
        return result;
    }
    if (total_count >= 250) {
        result.diagnostic = L"战场总人数已达到250人上限，拒绝继续添加";
        return result;
    }
    if (!ccz::preview::is_battle_coord_in_bounds(
            coordinate, state_->battle.map.width, state_->battle.map.height)) {
        result.diagnostic = L"战场放置坐标越界";
        return result;
    }
    for (const BattleUnit& unit : state_->battle.units) {
        if (!state_->copy_person_mode && IsCurrentUnit(*state_, unit) &&
            unit.data_id == state_->placement_defaults.data_id) {
            result.diagnostic = L"当前战场 Data 号已被其他单位占用";
            return result;
        }
    }
    if (!TerrainAllows(coordinate, state_->placement_defaults.data_id,
                       result.diagnostic)) {
        return result;
    }
    for (const BattleUnit& unit : state_->battle.units) {
        if (IsCurrentUnit(*state_, unit) && unit.position == coordinate) {
            result.diagnostic = L"当前战场目标格已有单位";
            return result;
        }
    }
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(state_->placement.instruction_id);
    BattleUnit unit;
    unit.data_id = state_->placement_defaults.data_id;
    unit.page = 0;
    unit.position = coordinate;
    unit.direction = state_->mouse_direction == -1
        ? -1 : WrappedDirection(state_->mouse_direction);
    unit.hidden = state_->placement_defaults.hidden;
    unit.reinforcement = state_->placement_defaults.reinforcement;
    unit.level = state_->placement_defaults.level;
    unit.unit_type = state_->placement_defaults.unit_type;
    unit.ai_policy = state_->placement_defaults.ai_policy;
    unit.appearance = state_->placement_defaults.appearance;
    unit.target = state_->placement_defaults.target;
    unit.target_x = state_->placement_defaults.target_x;
    unit.target_y = state_->placement_defaults.target_y;
    // The deployment kind is an output category, not a source instruction.
    // New units remain source-less until the final document transaction.
    unit.editor_deployment_instruction = state_->placement.instruction_id;
    unit.editor_staged_new = true;
    unit.fields_decoded = true;
    if (unit.raw_fields.size() != spec->record_stride) {
        unit.raw_fields.assign(spec->record_stride, 0);
    }
    unit.dirty_fields.clear();
    for (const BattleFieldRole role : spec->field_roles) mark_role(unit, role);
    result.unit = std::move(unit);
    result.valid = true;
    result.diagnostic = L"战场悬浮放置预览有效（仅在布阵缓存中）";
    return result;
}

BattlePlacementPreview BattleEditorOps::PreviewPlacement(
    const ccz::preview::BattleCoord& coordinate) const {
    return BuildPlacementPreview(coordinate);
}

BattleEditorResult BattleEditorOps::Place(
    const ccz::preview::BattleCoord& coordinate) {
    const BattlePlacementPreview preview = BuildPlacementPreview(coordinate);
    if (!preview.valid) return failure(preview.diagnostic.c_str());
    BattleEditorState proposed = *state_;
    BattleUnit staged = preview.unit;
    staged.editor_cache_id = proposed.next_cache_id++;
    if (proposed.selected_minion_brush.has_value()) {
        if (proposed.selected_minion_brush_batch_id == 0) {
            return failure(L"小兵笔刷缺少当前布置批次身份");
        }
        staged.editor_minion_brush_batch_id =
            proposed.selected_minion_brush_batch_id;
        staged.editor_minion_brush_first_id = staged.data_id;
        for (const BattleUnit& existing : proposed.battle.units) {
            if (!existing.editor_staged_new ||
                existing.editor_minion_brush_batch_id !=
                    staged.editor_minion_brush_batch_id) continue;
            if (existing.editor_minion_brush_first_id < 0) {
                return failure(L"小兵笔刷现有缓存批次缺少稳定起始号");
            }
            staged.editor_minion_brush_first_id =
                existing.editor_minion_brush_first_id;
            break;
        }
    }
    auto existing = std::find_if(
        proposed.battle.units.begin(), proposed.battle.units.end(),
        [&staged](const BattleUnit& candidate) {
            return !staged.source_key.empty() &&
                   candidate.source_key == staged.source_key &&
                   candidate.source_instruction == staged.source_instruction &&
                   candidate.page == staged.page &&
                   candidate.record_index == staged.record_index;
        });
    if (existing != proposed.battle.units.end()) {
        if (!IsBattleFixedEmptySlot(*existing)) {
            return failure(L"目标固定槽不是已证明的空槽，拒绝覆盖");
        }
        *existing = staged;
    } else {
        proposed.battle.units.push_back(staged);
    }
    proposed.selection = selection_for(staged);
    proposed.selection_group.clear();
    proposed.mode = proposed.selected_minion_brush.has_value()
        ? BattleEditorMode::Place : BattleEditorMode::Modify;
    BattleEditorResult result = CommitMutation(proposed, 1,
        L"战场单位已临时放置（仍在缓存，尚未写入剧本）");
    if (result.ok && !RefreshSelectedBrushDataId() &&
        !state_->selected_minion_brush.has_value()) {
        result.diagnostic = L"战场单位已临时放置；当前小兵笔刷批次因连续 Data 号被占用或耗尽而结束，请重新选择笔刷开启新批次";
    }
    return result;
}

BattleEditorResult BattleEditorOps::SelectAt(
    const ccz::preview::BattleCoord& coordinate) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"当前战场作用域没有可选单位");
    if (indices.size() != 1) {
        return failure(L"当前逻辑格存在多个单位，拒绝歧义选择");
    }
    const BattleUnit& unit = state_->battle.units[indices.front()];
    state_->selection = selection_for(unit);
    state_->selection_group.clear();
    return success(L"已选择当前战场逻辑格单位");
}

BattleEditorResult BattleEditorOps::ToggleSelectionAt(
    const ccz::preview::BattleCoord& coordinate) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode == BattleEditorMode::Place ||
        state_->mode == BattleEditorMode::AdjustPosition) {
        return failure(L"当前编辑模式不支持多选");
    }
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"当前战场作用域没有可选单位");
    if (indices.size() != 1)
        return failure(L"当前逻辑格存在多个单位，拒绝歧义选择");

    const BattleSelection selected =
        selection_for(state_->battle.units[indices.front()]);
    if (!selected.valid()) return failure(L"目标单位缺少稳定缓存身份");

    // Multi-selection has no persistent primary unit. The unit clicked when
    // a batch operation starts supplies that operation's temporary anchor.
    const bool clicked_single_selection = state_->selection.has_value() &&
        state_->selection->cache_id == selected.cache_id;
    state_->selection.reset();
    const auto existing = std::find_if(
        state_->selection_group.begin(), state_->selection_group.end(),
        [&selected](const BattleSelection& item) {
            return item.cache_id == selected.cache_id;
        });
    if (existing != state_->selection_group.end()) {
        state_->selection_group.erase(existing);
        return success(L"已将光标指向角色移出多选组", true, 1);
    }
    if (clicked_single_selection) {
        return success(L"已取消当前角色选择", true, 1);
    }

    state_->selection_group.push_back(selected);
    return success(L"单位已加入多选组", true, 1);
}

BattleEditorResult BattleEditorOps::AddSelectionAt(
    const ccz::preview::BattleCoord& coordinate) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode == BattleEditorMode::Place ||
        state_->mode == BattleEditorMode::AdjustPosition) {
        return failure(L"当前编辑模式不支持多选");
    }
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"当前战场作用域没有可选单位");
    if (indices.size() != 1)
        return failure(L"当前逻辑格存在多个单位，拒绝歧义选择");
    const BattleSelection selected =
        selection_for(state_->battle.units[indices.front()]);
    if (!selected.valid()) return failure(L"目标单位缺少稳定缓存身份");
    state_->selection.reset();
    const auto existing = std::find_if(
        state_->selection_group.begin(), state_->selection_group.end(),
        [&selected](const BattleSelection& item) {
            return item.cache_id == selected.cache_id;
        });
    if (existing != state_->selection_group.end())
        return success(L"目标单位已在多选组中");
    state_->selection_group.push_back(selected);
    return success(L"单位已加入多选组", true, 1);
}

BattleEditorResult BattleEditorOps::RemoveSelectionAt(
    const ccz::preview::BattleCoord& coordinate) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"光标指向位置没有可选单位");
    if (indices.size() != 1)
        return failure(L"当前逻辑格存在多个单位，拒绝歧义取消");
    const std::uint64_t cache_id =
        state_->battle.units[indices.front()].editor_cache_id;
    const auto selected = std::find_if(
        state_->selection_group.begin(), state_->selection_group.end(),
        [cache_id](const BattleSelection& item) {
            return item.cache_id == cache_id;
        });
    if (selected == state_->selection_group.end())
        return failure(L"光标指向的角色不在当前选择组");
    state_->selection_group.erase(selected);
    state_->selection.reset();
    return success(L"已将光标指向角色移出多选组", true, 1);
}

BattleEditorResult BattleEditorOps::FilterSelectionByDeployment(
    std::uint16_t instruction_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (instruction_id != 0x4b && instruction_id != 0x46 &&
        instruction_id != 0x47) {
        return failure(L"不支持的战场阵营筛选");
    }
    if (state_->selection_group.empty())
        return failure(L"当前没有多选角色可筛选");
    std::vector<BattleSelection> filtered;
    filtered.reserve(state_->selection_group.size());
    for (const BattleSelection& selected : state_->selection_group) {
        const BattleUnit* unit = FindUnitByCacheId(selected.cache_id);
        if (unit == nullptr || !IsCurrentUnit(*state_, *unit)) continue;
        const std::uint16_t deployment =
            unit->editor_deployment_instruction != 0xffff
                ? unit->editor_deployment_instruction
                : unit->source_instruction;
        if (deployment == instruction_id) filtered.push_back(selection_for(*unit));
    }
    state_->selection.reset();
    if (filtered.size() == state_->selection_group.size())
        return success(L"多选组已符合当前阵营筛选");
    state_->selection_group = std::move(filtered);
    return success(L"多选组已按阵营筛选", true,
                   state_->selection_group.size());
}

BattleEditorResult BattleEditorOps::ClearSelectionGroup() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const bool changed = !state_->selection_group.empty() ||
                         state_->selection.has_value();
    state_->selection_group.clear();
    state_->selection.reset();
    return success(L"已清空全部角色选择", changed,
                   changed ? 1U : 0U);
}

BattleEditorResult BattleEditorOps::DeleteSelectionGroup() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->selection_group.empty())
        return failure(L"当前没有多选角色可删除");
    BattleEditorState proposed = *state_;
    std::vector<std::size_t> indices;
    indices.reserve(proposed.selection_group.size());
    for (const BattleSelection& selected : proposed.selection_group) {
        const auto found = std::find_if(
            proposed.battle.units.begin(), proposed.battle.units.end(),
            [&selected, &proposed](const BattleUnit& unit) {
                return unit.editor_cache_id == selected.cache_id &&
                       IsCurrentUnit(proposed, unit);
            });
        if (found == proposed.battle.units.end())
            return failure(L"多选组中有角色已离开当前战场，未执行删除");
        indices.push_back(static_cast<std::size_t>(
            std::distance(proposed.battle.units.begin(), found)));
    }
    std::sort(indices.begin(), indices.end(), std::greater<std::size_t>());
    const std::size_t affected_count = indices.size();
    for (const std::size_t index : indices) {
        if (index >= proposed.battle.units.size())
            return failure(L"多选删除目标索引失效，缓存未修改");
        const BattleUnit removed = proposed.battle.units[index];
        if (!removed.editor_staged_new) {
            proposed.battle.units[index].editor_pending_delete = true;
            continue;
        }
        proposed.battle.units.erase(proposed.battle.units.begin() +
                                    static_cast<std::ptrdiff_t>(index));
        if (removed.editor_minion_brush_batch_id == 0) continue;
        for (BattleUnit& unit : proposed.battle.units) {
            if (!unit.editor_staged_new ||
                unit.editor_minion_brush_batch_id !=
                    removed.editor_minion_brush_batch_id ||
                unit.data_id <= removed.data_id) continue;
            --unit.data_id;
            const auto entry = std::find_if(
                proposed.characters.entries.begin(),
                proposed.characters.entries.end(),
                [&unit](const BattleCharacterEntry& character) {
                    return character.data_id == unit.data_id;
                });
            if (entry != proposed.characters.entries.end()) {
                unit.appearance = entry->appearance;
                unit.unit_type = entry->unit_type;
            }
            mark_role(unit, BattleFieldRole::DataId);
        }
    }
    proposed.selection.reset();
    proposed.selection_group.clear();
    BattleEditorResult result = CommitMutation(proposed, affected_count,
        L"已批量删除多选角色（一次撤回可恢复整组）");
    if (result.ok) RefreshSelectedBrushDataId();
    return result;
}

const BattleUnit* BattleEditorOps::FindUnitByCacheId(
    std::uint64_t cache_id) const noexcept {
    if (state_ == nullptr || cache_id == 0) return nullptr;
    const auto found = std::find_if(
        state_->battle.units.begin(), state_->battle.units.end(),
        [cache_id](const BattleUnit& unit) {
            return unit.editor_cache_id == cache_id &&
                   !unit.editor_pending_delete;
        });
    return found == state_->battle.units.end() ? nullptr : &*found;
}

BattleEditorResult BattleEditorOps::BeginPositionAdjustment(
    std::uint64_t cache_id) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode == BattleEditorMode::AdjustPosition)
        return failure(L"已有单位正在调整位置");
    const BattleUnit* unit = FindUnitByCacheId(cache_id);
    if (unit == nullptr || !IsCurrentUnit(*state_, *unit))
        return failure(L"目标单位已不在当前战场缓存中");
    if (unit->editor_cache_id == 0)
        return failure(L"目标单位缺少稳定缓存身份，拒绝调整");
    if (!unit->editor_staged_new && !has_complete_source_anchor(*unit))
        return failure(L"既有单位缺少完整指令来源/原始字段基线，拒绝调整");

    const bool group_anchor = state_->selection_group.size() > 1 &&
        std::any_of(state_->selection_group.begin(),
                    state_->selection_group.end(),
            [cache_id](const BattleSelection& item) {
                return item.cache_id == cache_id;
            });
    int adjustment_direction = HasRole(*unit, BattleFieldRole::Direction)
        ? WrappedDirection(unit->direction) : -1;
    if (group_anchor) {
        for (const BattleSelection& selected : state_->selection_group) {
            const BattleUnit* member = FindUnitByCacheId(selected.cache_id);
            if (member == nullptr || !IsCurrentUnit(*state_, *member) ||
                !HasRole(*member, BattleFieldRole::Direction)) {
                return failure(L"多选组中有单位不支持统一调整朝向");
            }
        }
        adjustment_direction = WrappedDirection(unit->direction);
    }
    if (!group_anchor) state_->selection_group.clear();
    state_->selection = selection_for(*unit);
    state_->mode_before_position_adjustment = state_->mode;
    state_->adjusting_cache_id = cache_id;
    state_->position_adjustment_direction = adjustment_direction;
    state_->mode = BattleEditorMode::AdjustPosition;
    state_->hover = unit->position;
    return success(L"位置调整已开始；落点只修改暂存缓存", true,
                   group_anchor ? state_->selection_group.size() : 1U);
}

BattlePlacementPreview BattleEditorOps::PreviewPositionAdjustment(
    const ccz::preview::BattleCoord& coordinate) const {
    BattlePlacementPreview result;
    result.coordinate = coordinate;
    if (state_ == nullptr || state_->mode != BattleEditorMode::AdjustPosition ||
        state_->adjusting_cache_id == 0) {
        result.diagnostic = L"当前没有进行中的战场位置调整";
        return result;
    }
    const BattleUnit* moving = FindUnitByCacheId(state_->adjusting_cache_id);
    if (moving == nullptr || !IsCurrentUnit(*state_, *moving)) {
        result.diagnostic = L"待调整单位已不在当前战场缓存中";
        return result;
    }
    if (!moving->editor_staged_new && !has_complete_source_anchor(*moving)) {
        result.diagnostic = L"既有单位来源锚点或原始字段基线已失效";
        return result;
    }
    if (!CompleteTerrain(*state_)) {
        result.diagnostic = L"战场地图/地形未闭合，拒绝位置预览";
        return result;
    }
    std::vector<const BattleUnit*> moving_units;
    const bool move_group = state_->selection_group.size() > 1 &&
        std::any_of(state_->selection_group.begin(),
                    state_->selection_group.end(),
            [moving](const BattleSelection& item) {
                return item.cache_id == moving->editor_cache_id;
            });
    if (move_group) {
        moving_units.reserve(state_->selection_group.size());
        for (const BattleSelection& selected : state_->selection_group) {
            const BattleUnit* member = FindUnitByCacheId(selected.cache_id);
            if (member == nullptr || !IsCurrentUnit(*state_, *member)) {
                result.diagnostic = L"多选组中有单位已离开当前战场";
                return result;
            }
            if (!member->editor_staged_new &&
                !has_complete_source_anchor(*member)) {
                result.diagnostic = L"多选组中有单位的来源锚点或字段基线已失效";
                return result;
            }
            moving_units.push_back(member);
        }
    } else {
        moving_units.push_back(moving);
    }
    const int delta_x = coordinate.x - moving->position.x;
    const int delta_y = coordinate.y - moving->position.y;
    result.units.reserve(moving_units.size());
    for (const BattleUnit* member : moving_units) {
        BattleUnit destination = *member;
        destination.position = {member->position.x + delta_x,
                                member->position.y + delta_y};
        if (state_->position_adjustment_direction >= 0)
            destination.direction = state_->position_adjustment_direction;
        if (!ccz::preview::is_battle_coord_in_bounds(
                destination.position, state_->battle.map.width,
                state_->battle.map.height)) {
            result.diagnostic = L"战场目标坐标越界";
            result.units.clear();
            return result;
        }
        if (destination.position != member->position &&
            !TerrainAllowsUnit(destination.position, *member,
                           result.diagnostic)) {
            if (result.diagnostic.empty())
                result.diagnostic = L"战场目标格受兵种地形限制，拒绝位置预览";
            result.units.clear();
            return result;
        }
        for (const BattleUnit& prior : result.units) {
            if (prior.position == destination.position) {
                result.diagnostic = L"多选组移动后会发生单位重叠";
                result.units.clear();
                return result;
            }
        }
        result.units.push_back(std::move(destination));
    }
    for (const BattleUnit& other : state_->battle.units) {
        if (!IsCurrentUnit(*state_, other)) continue;
        const bool is_moving = std::any_of(
            moving_units.begin(), moving_units.end(),
            [&other](const BattleUnit* member) {
                return member->editor_cache_id == other.editor_cache_id;
            });
        if (is_moving) continue;
        if (std::any_of(result.units.begin(), result.units.end(),
                [&other](const BattleUnit& destination) {
                    return destination.position == other.position;
                })) {
            result.diagnostic = L"战场目标格已有其他单位";
            result.units.clear();
            return result;
        }
    }
    const auto moved_anchor = std::find_if(
        result.units.begin(), result.units.end(),
        [moving](const BattleUnit& unit) {
            return unit.editor_cache_id == moving->editor_cache_id;
        });
    if (moved_anchor == result.units.end()) {
        result.diagnostic = L"待调整单位不在多选移动结果中";
        result.units.clear();
        return result;
    }
    result.unit = *moved_anchor;
    result.valid = true;
    result.diagnostic = L"战场位置调整预览有效；尚未写入剧本";
    return result;
}

BattleEditorResult BattleEditorOps::ConfirmPositionAdjustment(
    const ccz::preview::BattleCoord& coordinate) {
    const BattlePlacementPreview preview = PreviewPositionAdjustment(coordinate);
    if (!preview.valid) return failure(preview.diagnostic.c_str());
    BattleEditorState proposed = *state_;
    std::size_t changed_count = 0;
    for (const BattleUnit& destination : preview.units) {
        auto unit = std::find_if(
            proposed.battle.units.begin(), proposed.battle.units.end(),
            [&destination](const BattleUnit& candidate) {
                return candidate.editor_cache_id ==
                           destination.editor_cache_id &&
                       !candidate.editor_pending_delete;
            });
        if (unit == proposed.battle.units.end())
            return failure(L"待调整组中有单位已失效，缓存未修改");
        const bool position_changed = unit->position != destination.position;
        const bool direction_changed = unit->direction != destination.direction;
        if (position_changed) {
            unit->position = destination.position;
            mark_role(*unit, BattleFieldRole::X);
            mark_role(*unit, BattleFieldRole::Y);
        }
        if (direction_changed) {
            unit->direction = destination.direction;
            mark_role(*unit, BattleFieldRole::Direction);
        }
        if (position_changed || direction_changed) ++changed_count;
    }
    const auto anchor = std::find_if(
        proposed.battle.units.begin(), proposed.battle.units.end(),
        [this](const BattleUnit& candidate) {
            return candidate.editor_cache_id == state_->adjusting_cache_id &&
                   !candidate.editor_pending_delete;
        });
    if (anchor == proposed.battle.units.end())
        return failure(L"待调整单位已失效，缓存未修改");
    proposed.selection = selection_for(*anchor);
    proposed.mode = proposed.mode_before_position_adjustment;
    proposed.adjusting_cache_id = 0;
    proposed.position_adjustment_direction = -1;
    proposed.hover = coordinate;
    if (changed_count == 0) {
        *state_ = std::move(proposed);
        return success(L"位置与朝向未变化，已结束调整", false, 0);
    }
    return CommitMutation(proposed, changed_count,
        preview.units.size() > 1
            ? L"多选单位位置与朝向已统一调整（一个撤回事务）"
            : (anchor->editor_staged_new
                ? L"新增单位位置已调整（仅更新布阵缓存）"
                : L"既有单位坐标已暂存（写回前仍需重验来源记录）"));
}

BattleEditorResult BattleEditorOps::CancelPositionAdjustment() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode != BattleEditorMode::AdjustPosition ||
        state_->adjusting_cache_id == 0) {
        return failure(L"当前没有进行中的战场位置调整");
    }
    state_->mode = state_->mode_before_position_adjustment;
    state_->adjusting_cache_id = 0;
    state_->position_adjustment_direction = -1;
    state_->hover.reset();
    return success(L"已取消位置与朝向调整；预览未写入缓存");
}

bool BattleEditorOps::ApplyPatch(BattleUnit& unit, const BattleUnitPatch& patch,
                                 std::wstring& diagnostic) const {
    if (patch.data_id && !HasRole(unit, BattleFieldRole::DataId)) {
        diagnostic = L"当前战场记录不含 Data 号字段";
        return false;
    }
    if (patch.position && (!HasRole(unit, BattleFieldRole::X) ||
                          !HasRole(unit, BattleFieldRole::Y))) {
        diagnostic = L"当前战场记录不含完整坐标字段";
        return false;
    }
    if (patch.hidden && !HasRole(unit, BattleFieldRole::Hidden)) {
        diagnostic = L"当前战场记录不含隐藏字段";
        return false;
    }
    if (patch.direction && !HasRole(unit, BattleFieldRole::Direction)) {
        diagnostic = L"当前战场记录不含朝向字段";
        return false;
    }
    if (patch.level && !HasRole(unit, BattleFieldRole::Level)) {
        diagnostic = L"当前战场记录不含等级字段";
        return false;
    }
    if (patch.unit_type && !HasRole(unit, BattleFieldRole::UnitType)) {
        diagnostic = L"当前战场记录不含类型字段";
        return false;
    }
    if (patch.reinforcement &&
        !HasRole(unit, BattleFieldRole::Reinforcement)) {
        diagnostic = L"当前战场记录不含援军字段";
        return false;
    }
    if (patch.ai_policy && !HasRole(unit, BattleFieldRole::AiPolicy)) {
        diagnostic = L"当前战场记录不含 AI 字段";
        return false;
    }
    if (patch.target && !HasRole(unit, BattleFieldRole::TargetDataId)) {
        diagnostic = L"当前战场记录不含目标 Data 字段";
        return false;
    }
    if (patch.target_x && !HasRole(unit, BattleFieldRole::TargetX)) {
        diagnostic = L"当前战场记录不含目标 X 字段";
        return false;
    }
    if (patch.target_y && !HasRole(unit, BattleFieldRole::TargetY)) {
        diagnostic = L"当前战场记录不含目标 Y 字段";
        return false;
    }

    if (patch.data_id) unit.data_id = *patch.data_id;
    if (patch.position) unit.position = *patch.position;
    if (patch.appearance) unit.appearance = *patch.appearance;
    if (patch.hidden) unit.hidden = *patch.hidden;
    if (patch.direction) unit.direction = *patch.direction == -1
        ? -1 : WrappedDirection(*patch.direction);
    if (patch.level) unit.level = *patch.level;
    if (patch.unit_type) unit.unit_type = *patch.unit_type;
    if (patch.reinforcement) unit.reinforcement = *patch.reinforcement;
    if (patch.ai_policy) unit.ai_policy = *patch.ai_policy;
    if (patch.target) unit.target = *patch.target;
    if (patch.target_x) unit.target_x = *patch.target_x;
    if (patch.target_y) unit.target_y = *patch.target_y;

    if (patch.data_id) mark_role(unit, BattleFieldRole::DataId);
    if (patch.position) {
        mark_role(unit, BattleFieldRole::X);
        mark_role(unit, BattleFieldRole::Y);
    }
    if (patch.hidden) mark_role(unit, BattleFieldRole::Hidden);
    if (patch.direction) mark_role(unit, BattleFieldRole::Direction);
    if (patch.level) mark_role(unit, BattleFieldRole::Level);
    if (patch.unit_type) mark_role(unit, BattleFieldRole::UnitType);
    if (patch.reinforcement) mark_role(unit, BattleFieldRole::Reinforcement);
    if (patch.ai_policy) mark_role(unit, BattleFieldRole::AiPolicy);
    if (patch.target) mark_role(unit, BattleFieldRole::TargetDataId);
    if (patch.target_x) mark_role(unit, BattleFieldRole::TargetX);
    if (patch.target_y) mark_role(unit, BattleFieldRole::TargetY);
    // Appearance is intentionally not marked dirty: Task 2A's confirmed EEX
    // roles contain UnitType but do not prove a separate appearance slot.  It
    // is still available as a temporary visual parameter; raw fields remain
    // preserved until a future evidence task closes this mapping.
    return true;
}

BattleEditorResult BattleEditorOps::ModifyAt(
    const ccz::preview::BattleCoord& coordinate, const BattleUnitPatch& patch) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"当前战场作用域没有可修改单位");
    if (indices.size() != 1) return failure(L"当前逻辑格存在多个单位，拒绝修改");
    BattleEditorState proposed = *state_;
    BattleUnit& unit = proposed.battle.units[indices.front()];
    std::wstring diagnostic;
    if (!ApplyPatch(unit, patch, diagnostic)) return failure(diagnostic.c_str());
    if (patch.position) {
        if (!CompleteTerrain(proposed) ||
            !ccz::preview::is_battle_coord_in_bounds(
                *patch.position, proposed.battle.map.width,
                proposed.battle.map.height)) {
            return failure(L"修改后的战场坐标越界或地形未闭合");
        }
        if (!TerrainAllowsUnit(*patch.position, unit, diagnostic)) {
            return failure(diagnostic.c_str());
        }
        for (std::size_t index = 0; index < proposed.battle.units.size(); ++index) {
            if (index == indices.front()) continue;
            const BattleUnit& other = proposed.battle.units[index];
            if (IsCurrentUnit(proposed, other) &&
                other.position == *patch.position) {
                return failure(L"修改后的战场坐标已被当前战场单位占用");
            }
        }
    }
    const BattleUnit& selected = proposed.battle.units[indices.front()];
    proposed.selection = selection_for(selected);
    return CommitMutation(proposed, 1,
                          L"战场单位已临时修改，等待写回");
}

BattleEditorResult BattleEditorOps::ModifySelectionGroup(
    const std::vector<std::uint64_t>& cache_ids,
    const BattleUnitPatch& patch) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (cache_ids.size() < 2)
        return failure(L"批量修改至少需要两个单位");
    if (patch.data_id || patch.position || patch.appearance)
        return failure(L"批量参数修改不支持角色身份、坐标或临时形象");

    BattleEditorState proposed = *state_;
    std::vector<std::size_t> indices;
    std::vector<std::uint64_t> seen_cache_ids;
    indices.reserve(cache_ids.size());
    seen_cache_ids.reserve(cache_ids.size());
    for (const std::uint64_t cache_id : cache_ids) {
        if (cache_id == 0 || std::find(seen_cache_ids.begin(),
                seen_cache_ids.end(), cache_id) != seen_cache_ids.end()) {
            return failure(L"批量修改目标列表无效或包含重复单位");
        }
        const auto found = std::find_if(proposed.battle.units.begin(),
            proposed.battle.units.end(), [cache_id](const BattleUnit& unit) {
                return unit.editor_cache_id == cache_id;
            });
        if (found == proposed.battle.units.end() ||
            !IsCurrentUnit(proposed, *found) || !found->fields_decoded ||
            IsBattleFixedEmptySlot(*found)) {
            return failure(L"批量修改组中有单位已离开当前战场");
        }
        indices.push_back(static_cast<std::size_t>(
            std::distance(proposed.battle.units.begin(), found)));
        seen_cache_ids.push_back(cache_id);
    }

    for (const std::size_t index : indices) {
        std::wstring diagnostic;
        if (!ApplyPatch(proposed.battle.units[index], patch, diagnostic))
            return failure(diagnostic.c_str());
    }
    return CommitMutation(proposed, indices.size(),
        L"多选单位参数已统一修改（一个撤回事务）");
}

BattleEditorResult BattleEditorOps::DeleteAt(
    const ccz::preview::BattleCoord& coordinate) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) return failure(L"当前战场作用域没有可删除单位");
    if (indices.size() != 1) return failure(L"当前逻辑格存在多个单位，拒绝删除");
    const BattleUnit removed = state_->battle.units[indices.front()];
    BattleEditorState proposed = *state_;
    if (removed.editor_staged_new) {
        proposed.battle.units.erase(proposed.battle.units.begin() +
                                    static_cast<std::ptrdiff_t>(indices.front()));
        if (removed.editor_minion_brush_batch_id != 0) {
            for (BattleUnit& unit : proposed.battle.units) {
                if (!unit.editor_staged_new ||
                    unit.editor_minion_brush_batch_id !=
                        removed.editor_minion_brush_batch_id ||
                    unit.data_id <= removed.data_id) continue;
                --unit.data_id;
                const auto entry = std::find_if(
                    proposed.characters.entries.begin(),
                    proposed.characters.entries.end(),
                    [&unit](const BattleCharacterEntry& character) {
                        return character.data_id == unit.data_id;
                    });
                if (entry != proposed.characters.entries.end()) {
                    unit.appearance = entry->appearance;
                    unit.unit_type = entry->unit_type;
                }
                mark_role(unit, BattleFieldRole::DataId);
            }
        }
        proposed.selection.reset();
        proposed.selection_group.clear();
        BattleEditorResult result = CommitMutation(proposed, 1,
            L"新增单位已从临时布阵中删除");
        if (result.ok) RefreshSelectedBrushDataId();
        return result;
    }
    // Keep an imported row and its original source anchor intact until the
    // final writeback planner commits a deletion.  The renderer/hit testing
    // filters this tombstone, while undo can still restore the exact row.
    proposed.battle.units[indices.front()].editor_pending_delete = true;
    proposed.selection.reset();
    proposed.selection_group.clear();
    BattleEditorResult result = CommitMutation(proposed, 1,
        L"战场单位已标记删除，等待写回");
    if (result.ok) RefreshSelectedBrushDataId();
    return result;
}

BattleEditorResult BattleEditorOps::CycleDirection(int delta) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (delta == 0) return success(L"战场方向未变化");
    const bool adjusting_position =
        state_->mode == BattleEditorMode::AdjustPosition &&
        state_->adjusting_cache_id != 0;
    if (adjusting_position) {
        if (state_->position_adjustment_direction < 0)
            return failure(L"当前单位不支持调整朝向");
        state_->position_adjustment_direction = WrappedDirection(
            state_->position_adjustment_direction + delta);
        const bool adjusting_group = state_->selection_group.size() > 1;
        return success(adjusting_group
                ? L"多选移动预览朝向已统一调整；确认后整组应用"
                : L"单体位置预览朝向已调整；确认后应用",
            true, adjusting_group ? state_->selection_group.size() : 1U);
    }
    if (!state_->selection.has_value() ||
        state_->mode == BattleEditorMode::Place) {
        state_->mouse_direction = WrappedDirection(state_->mouse_direction + delta);
        state_->placement_defaults.direction = state_->mouse_direction;
        return success(L"战场默认放置朝向已调整", true);
    }
    const BattleSelection selection = *state_->selection;
    BattleEditorState proposed = *state_;
    auto unit = std::find_if(
        proposed.battle.units.begin(), proposed.battle.units.end(),
        [&selection, &proposed](const BattleUnit& candidate) {
            return (selection.cache_id != 0
                       ? candidate.editor_cache_id == selection.cache_id
                       : candidate.source_key == selection.source_key &&
                         candidate.record_index == selection.record_index &&
                         InScope(candidate.source_key, proposed.scope_root)) &&
                   !candidate.editor_pending_delete &&
                   candidate.fields_decoded;
        });
    if (unit == proposed.battle.units.end()) {
        return failure(L"当前选择已离开战场作用域，未修改朝向");
    }
    if (!HasRole(*unit, BattleFieldRole::Direction)) {
        return failure(L"当前战场记录不含朝向字段");
    }
    unit->direction = WrappedDirection(unit->direction + delta);
    mark_role(*unit, BattleFieldRole::Direction);
    if (proposed.direction_locked) {
        proposed.mouse_direction = unit->direction;
        proposed.placement_defaults.direction = unit->direction;
    }
    return CommitMutation(proposed, 1,
                          L"战场单位朝向已临时调整（方向锁语义已隔离）");
}

bool BattleEditorOps::TargetReference(const BattleTargetRef& target,
                                      std::size_t& target_index,
                                      std::wstring& diagnostic) const {
    if (state_ == nullptr) {
        diagnostic = L"战场编辑器状态为空";
        return false;
    }
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < state_->battle.units.size(); ++index) {
        const BattleUnit& unit = state_->battle.units[index];
        if (!IsCurrentUnit(*state_, unit)) continue;
        if (target.kind == BattleTargetKind::DataId) {
            if (unit.data_id == target.data_id) candidates.push_back(index);
        } else if (unit.position == target.coordinate) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        diagnostic = L"目标模式未在当前战场作用域解析到单位";
        return false;
    }
    if (candidates.size() != 1) {
        diagnostic = L"目标模式在当前战场逻辑格得到多个单位，拒绝歧义目标";
        return false;
    }
    target_index = candidates.front();
    return true;
}

BattleEditorResult BattleEditorOps::PaintAt(
    BattlePainterMode mode, const ccz::preview::BattleCoord& coordinate,
    const BattlePainterValue& value) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    const std::vector<std::size_t> indices = UnitsAt(*state_, coordinate);
    if (indices.empty()) {
        return failure(L"当前战场逻辑格没有可刷写的战场单位");
    }

    std::optional<std::size_t> target_index;
    if (mode == BattlePainterMode::Target) {
        std::size_t resolved = 0;
        std::wstring diagnostic;
        if (!TargetReference(value.target, resolved, diagnostic)) {
            return failure(diagnostic.c_str());
        }
        target_index = resolved;
    }

    BattleEditorState proposed = *state_;
    const BattleUnit& target_unit =
        target_index ? proposed.battle.units[*target_index]
                     : proposed.battle.units[indices.front()];
    for (const std::size_t index : indices) {
        BattleUnit& unit = proposed.battle.units[index];
        switch (mode) {
        case BattlePainterMode::Appearance:
            // See ApplyPatch: no separate EEX appearance role is proven yet.
            unit.appearance = value.integer;
            break;
        case BattlePainterMode::Hidden:
            if (!HasRole(unit, BattleFieldRole::Hidden)) {
                return failure(L"当前战场记录不含隐藏字段");
            }
            unit.hidden = value.boolean;
            mark_role(unit, BattleFieldRole::Hidden);
            break;
        case BattlePainterMode::Direction:
            if (!HasRole(unit, BattleFieldRole::Direction)) {
                return failure(L"当前战场记录不含朝向字段");
            }
            unit.direction = WrappedDirection(value.integer);
            mark_role(unit, BattleFieldRole::Direction);
            break;
        case BattlePainterMode::Level:
            if (!HasRole(unit, BattleFieldRole::Level)) {
                return failure(L"当前战场记录不含等级字段");
            }
            unit.level = value.integer;
            mark_role(unit, BattleFieldRole::Level);
            break;
        case BattlePainterMode::UnitType:
            if (!HasRole(unit, BattleFieldRole::UnitType)) {
                return failure(L"当前战场记录不含类型字段");
            }
            unit.unit_type = value.integer;
            mark_role(unit, BattleFieldRole::UnitType);
            break;
        case BattlePainterMode::Target:
            if (!HasRole(unit, BattleFieldRole::TargetDataId) ||
                !HasRole(unit, BattleFieldRole::TargetX) ||
                !HasRole(unit, BattleFieldRole::TargetY)) {
                return failure(L"当前战场记录不含完整目标字段");
            }
            unit.target = target_unit.data_id;
            unit.target_x = target_unit.position.x;
            unit.target_y = target_unit.position.y;
            mark_role(unit, BattleFieldRole::TargetDataId);
            mark_role(unit, BattleFieldRole::TargetX);
            mark_role(unit, BattleFieldRole::TargetY);
            break;
        }
    }
    std::wstringstream message;
    message << L"战场参数笔刷已临时修改当前逻辑格的" << mode_name(mode)
            << L"字段，等待写回";
    const std::wstring diagnostic = message.str();
    return CommitMutation(proposed, indices.size(), diagnostic.c_str());
}

BattleEditorResult BattleEditorOps::SetViewportSize(int width, int height) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (width <= 0 || height <= 0) return failure(L"战场视口尺寸无效");
    state_->viewport.width = width;
    state_->viewport.height = height;
    if (state_->battle.map.valid) {
        const int max_x = (std::max)(0, state_->battle.map.width - width);
        const int max_y = (std::max)(0, state_->battle.map.height - height);
        state_->viewport.x = (std::min)((std::max)(0, state_->viewport.x), max_x);
        state_->viewport.y = (std::min)((std::max)(0, state_->viewport.y), max_y);
    }
    return success(L"战场视口尺寸已更新");
}

BattleEditorResult BattleEditorOps::Pan(int dx, int dy) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (!state_->battle.map.valid || state_->battle.map.width <= 0 ||
        state_->battle.map.height <= 0 || state_->viewport.width <= 0 ||
        state_->viewport.height <= 0) {
        return failure(L"战场地图/视口边界未闭合，拒绝平移");
    }
    const int max_x = (std::max)(0, state_->battle.map.width - state_->viewport.width);
    const int max_y = (std::max)(0, state_->battle.map.height - state_->viewport.height);
    const int old_x = state_->viewport.x;
    const int old_y = state_->viewport.y;
    state_->viewport.x = (std::min)(max_x, (std::max)(0, old_x + dx));
    state_->viewport.y = (std::min)(max_y, (std::max)(0, old_y + dy));
    const bool changed = old_x != state_->viewport.x || old_y != state_->viewport.y;
    return success(changed ? L"战场地图已平移" : L"战场地图已在边界，未平移",
                   changed);
}

bool BattleEditorOps::CanUndo() const noexcept {
    return state_ != nullptr && !undo_stack_.empty();
}

BattleEditorResult BattleEditorOps::Undo() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (undo_stack_.empty()) return failure(L"没有可撤回的战场编辑事务");
    BattleWritebackMemoryState current = ToMemoryState(*state_);
    BattleWritebackTransaction transaction = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    if (!transaction.UndoToMemory(current)) {
        return failure(L"战场编辑撤回失败，状态保持不变");
    }
    const std::wstring before = Summary(*state_);
    ApplyMemoryState(*state_, current);
    RefreshSelectedBrushDataId();
    BattleEditorResult result = success(
        L"战场临时编辑已撤回（未写入剧本）", true);
    result.before_summary = before;
    result.after_summary = Summary(*state_);
    return result;
}

void BattleEditorOps::ClearUndo() noexcept { undo_stack_.clear(); }

BattleEditorDraftRebaseResult RebaseBattleEditorDraft(
    const BattleEditorState& draft,
    const BattleEditorState& fresh_baseline) {
    BattleEditorDraftRebaseResult result;
    result.state = fresh_baseline;
    BattleEditorState& rebased = result.state;

    // Keep editor choices and transient tools, while all map/source data comes
    // from the fresh parse below.
    rebased.characters = draft.characters;
    rebased.minion_brushes = draft.minion_brushes;
    rebased.selected_minion_brush = draft.selected_minion_brush;
    rebased.next_minion_brush_batch_id = draft.next_minion_brush_batch_id;
    rebased.selected_minion_brush_batch_id =
        draft.selected_minion_brush_batch_id;
    rebased.placement = draft.placement;
    rebased.placement_defaults = draft.placement_defaults;
    rebased.mode = draft.mode == BattleEditorMode::AdjustPosition
        ? draft.mode_before_position_adjustment : draft.mode;
    rebased.mode_before_position_adjustment = rebased.mode;
    rebased.viewport = draft.viewport;
    rebased.direction_locked = draft.direction_locked;
    rebased.copy_person_mode = draft.copy_person_mode;
    rebased.unrestricted_terrain_mode = draft.unrestricted_terrain_mode;
    rebased.mouse_direction = draft.mouse_direction;
    rebased.document_modified = draft.document_modified;
    rebased.adjusting_cache_id = 0;
    rebased.position_adjustment_direction = -1;
    rebased.hover.reset();

    std::map<std::uint64_t, std::uint64_t> cache_id_remap;
    for (const BattleUnit& old_unit : draft.battle.units) {
        if (old_unit.editor_staged_new) {
            BattleUnit staged = old_unit;
            if (staged.editor_pending_delete) continue;
            if (rebased.next_cache_id == 0 ||
                rebased.next_cache_id ==
                    (std::numeric_limits<std::uint64_t>::max)()) {
                result.conflicts.push_back(
                    L"新增单位缓存 ID 已耗尽，无法安全保留该布置");
                continue;
            }
            staged.editor_cache_id = rebased.next_cache_id++;
            if (staged.editor_cache_id != old_unit.editor_cache_id)
                cache_id_remap[old_unit.editor_cache_id] =
                    staged.editor_cache_id;
            const bool overlaps = std::any_of(
                rebased.battle.units.begin(), rebased.battle.units.end(),
                [&staged](const BattleUnit& candidate) {
                    return !candidate.editor_pending_delete &&
                        candidate.position == staged.position;
                });
            if (overlaps) {
                result.conflicts.push_back(source_row_label(old_unit) +
                    L"：新来源已有单位占据临时新增单位的格子");
                continue;
            }
            rebased.battle.units.push_back(std::move(staged));
            continue;
        }

        const bool manual_appearance =
            has_manual_appearance_override(draft, old_unit);
        const bool edited = old_unit.editor_pending_delete ||
            !old_unit.dirty_fields.empty() || manual_appearance;
        std::vector<std::size_t> matches;
        for (std::size_t index = 0; index < rebased.battle.units.size(); ++index) {
            if (same_source_row(old_unit, rebased.battle.units[index]))
                matches.push_back(index);
        }
        if (matches.empty()) {
            if (old_unit.editor_pending_delete) continue;
            if (edited) {
                result.conflicts.push_back(source_row_label(old_unit) +
                    L"：新来源中已找不到对应单位");
            }
            continue;
        }
        if (matches.size() != 1) {
            if (edited) {
                result.conflicts.push_back(source_row_label(old_unit) +
                    L"：新来源映射到多个单位");
            }
            continue;
        }

        BattleUnit& fresh_unit = rebased.battle.units[matches.front()];
        if (old_unit.editor_cache_id != 0)
            cache_id_remap[old_unit.editor_cache_id] =
                fresh_unit.editor_cache_id;
        if (old_unit.editor_pending_delete) {
            fresh_unit.editor_pending_delete = true;
            continue;
        }
        if (old_unit.dirty_fields.empty()) {
            // Appearance has no proven EEX field, but an explicit painter
            // choice is still part of the preview draft.
            if (manual_appearance)
                fresh_unit.appearance = old_unit.appearance;
            continue;
        }

        const BattleFieldSpec* spec =
            ccz::preview::get_battle_field_spec(old_unit.source_instruction);
        if (spec == nullptr || !spec->semantic_complete ||
            old_unit.editor_origin_fields.size() != spec->record_stride ||
            fresh_unit.raw_fields.size() != spec->record_stride ||
            spec->field_roles.size() != spec->record_stride) {
            result.conflicts.push_back(source_row_label(old_unit) +
                L"：字段基线不完整，无法合并临时修改");
            continue;
        }

        for (const BattleFieldRole role : old_unit.dirty_fields) {
            bool role_found = false;
            bool local_value_needed = false;
            const int local_value = unit_role_value(old_unit, role);
            for (std::size_t field = 0; field < spec->record_stride; ++field) {
                if (spec->field_roles[field] != role) continue;
                role_found = true;
                const int old_value = old_unit.editor_origin_fields[field];
                const int fresh_value = fresh_unit.raw_fields[field];
                if (fresh_value != old_value && fresh_value != local_value) {
                    result.conflicts.push_back(source_row_label(old_unit) +
                        L"：" + role_label(role) + L"也被剧本来源修改");
                    local_value_needed = false;
                    break;
                }
                if (fresh_value != local_value) local_value_needed = true;
            }
            if (!role_found) {
                result.conflicts.push_back(source_row_label(old_unit) +
                    L"：字段表中找不到" + role_label(role));
                continue;
            }
            if (local_value_needed) {
                copy_unit_role(old_unit, fresh_unit, role);
                ccz::preview::mark_battle_field_dirty(fresh_unit, role);
            }
        }
        if (manual_appearance) {
            fresh_unit.appearance = old_unit.appearance;
        }
    }

    if (!result.conflicts.empty()) {
        result.state = draft;
        return result;
    }

    if (draft.selection.has_value()) {
        BattleSelection selection = *draft.selection;
        const auto mapped = cache_id_remap.find(selection.cache_id);
        if (mapped != cache_id_remap.end()) {
            selection.cache_id = mapped->second;
            rebased.selection = selection;
        } else {
            const auto source = std::find_if(
                rebased.battle.units.begin(), rebased.battle.units.end(),
                [&selection](const BattleUnit& unit) {
                    return unit.source_key == selection.source_key &&
                        unit.record_index == selection.record_index &&
                        unit.page == static_cast<int>(selection.page) &&
                        !unit.editor_pending_delete;
                });
            if (source == rebased.battle.units.end()) {
                rebased.selection.reset();
            } else {
                selection.cache_id = source->editor_cache_id;
                rebased.selection = selection;
            }
        }
    } else {
        rebased.selection.reset();
    }

    rebased.selection_group.clear();
    for (const BattleSelection& old_selection : draft.selection_group) {
        BattleSelection selection = old_selection;
        const auto mapped = cache_id_remap.find(selection.cache_id);
        const BattleUnit* matched = nullptr;
        if (mapped != cache_id_remap.end()) {
            const auto unit = std::find_if(
                rebased.battle.units.begin(), rebased.battle.units.end(),
                [&mapped](const BattleUnit& candidate) {
                    return candidate.editor_cache_id == mapped->second &&
                        !candidate.editor_pending_delete &&
                        !IsBattleFixedEmptySlot(candidate);
                });
            if (unit != rebased.battle.units.end()) matched = &*unit;
        } else {
            const auto unit = std::find_if(
                rebased.battle.units.begin(), rebased.battle.units.end(),
                [&selection](const BattleUnit& candidate) {
                    return candidate.source_key == selection.source_key &&
                        candidate.record_index == selection.record_index &&
                        candidate.page == static_cast<int>(selection.page) &&
                        !candidate.editor_pending_delete &&
                        !IsBattleFixedEmptySlot(candidate);
                });
            if (unit != rebased.battle.units.end()) matched = &*unit;
        }
        if (matched == nullptr) continue;
        const BattleSelection rebased_selection = selection_for(*matched);
        const bool duplicate = std::any_of(
            rebased.selection_group.begin(), rebased.selection_group.end(),
            [&rebased_selection](const BattleSelection& existing) {
                return existing.cache_id == rebased_selection.cache_id;
            });
        if (!duplicate) rebased.selection_group.push_back(rebased_selection);
    }

    result.ok = true;
    return result;
}

std::vector<ccz::preview::BattleCoord> BattleSelectionPathCells(
    const ccz::preview::BattleCoord& start,
    const ccz::preview::BattleCoord& end) {
    using ccz::preview::BattleCoord;
    std::vector<BattleCoord> result;
    const long long delta_x = static_cast<long long>(end.x) - start.x;
    const long long delta_y = static_cast<long long>(end.y) - start.y;
    const long long abs_x = delta_x < 0 ? -delta_x : delta_x;
    const long long abs_y = delta_y < 0 ? -delta_y : delta_y;
    const long long step_x = delta_x < 0 ? -1 : 1;
    const long long step_y = delta_y < 0 ? -1 : 1;
    long long error = abs_x - abs_y;
    long long x = start.x;
    long long y = start.y;
    for (;;) {
        result.push_back({static_cast<int>(x), static_cast<int>(y)});
        if (x == end.x && y == end.y) break;
        const long long doubled_error = error * 2;
        if (doubled_error > -abs_y) {
            error -= abs_y;
            x += step_x;
        }
        if (doubled_error < abs_x) {
            error += abs_x;
            y += step_y;
        }
    }
    return result;
}

}  // namespace ccz::editor_preview
