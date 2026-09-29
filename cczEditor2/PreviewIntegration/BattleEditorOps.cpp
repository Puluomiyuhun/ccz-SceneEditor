#include "BattleEditorOps.h"
#include "BattlePreviewPredicates.h"

#include <ccz/preview/battle_field_spec.h>

#include <algorithm>
#include <cwctype>
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
    state.placement = {};
    state.placement.instruction_id = deployment_instruction;
    state.placement_defaults = BattlePlacementDefaults{};
    state.direction_locked = direction_locked;
    state.selection.reset();
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
    state_->mode = BattleEditorMode::Place;
    return success(L"当前战场角色与放置参数已切换");
}

int BattleEditorOps::NextAvailableBrushDataId(
    const BattleMinionBrush& brush) const {
    if (state_ == nullptr || brush.first_data_id < 0 ||
        brush.last_data_id < brush.first_data_id) return -1;
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
        // A newly selected brush starts at the lowest available Data ID.
        data_id = NextAvailableBrushDataId(brush);
    } else if (batch_metadata_valid && batch_first_id >= 0) {
        const long long next_id = static_cast<long long>(batch_first_id) +
            static_cast<long long>(batch_count);
        if (next_id >= brush.first_data_id && next_id <= brush.last_data_id) {
            const int candidate = static_cast<int>(next_id);
            const bool occupied = std::any_of(
                state_->battle.units.begin(), state_->battle.units.end(),
                [this, candidate](const BattleUnit& unit) {
                    return IsCurrentUnit(*state_, unit) &&
                           unit.data_id == candidate;
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
        state_->hover.reset();
    }
    state_->mode = mode;
    return success(L"战场编辑模式已切换");
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
    if (!ccz::preview::is_battle_coord_in_bounds(
            coordinate, state_->battle.map.width, state_->battle.map.height)) {
        result.diagnostic = L"战场放置坐标越界";
        return result;
    }
    for (const BattleUnit& unit : state_->battle.units) {
        if (IsCurrentUnit(*state_, unit) &&
            unit.data_id == state_->placement_defaults.data_id) {
            result.diagnostic = L"当前战场 Data 号已被其他单位占用";
            return result;
        }
    }
    if (ccz::preview::is_battle_terrain_blocked_at(state_->battle, coordinate)) {
        result.diagnostic = L"战场目标格受地形限制，拒绝放置";
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
    unit.direction = WrappedDirection(state_->mouse_direction);
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
    return success(L"已选择当前战场逻辑格单位");
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

    state_->selection = selection_for(*unit);
    state_->mode_before_position_adjustment = state_->mode;
    state_->adjusting_cache_id = cache_id;
    state_->mode = BattleEditorMode::AdjustPosition;
    state_->hover = unit->position;
    return success(L"位置调整已开始；落点只修改暂存缓存", true, 1);
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
    result.unit = *moving;
    result.unit.position = coordinate;
    if (!CompleteTerrain(*state_)) {
        result.diagnostic = L"战场地图/地形未闭合，拒绝位置预览";
        return result;
    }
    if (!ccz::preview::is_battle_coord_in_bounds(
            coordinate, state_->battle.map.width, state_->battle.map.height)) {
        result.diagnostic = L"战场目标坐标越界";
        return result;
    }
    if (coordinate != moving->position &&
        ccz::preview::is_battle_terrain_blocked_at(state_->battle, coordinate)) {
        result.diagnostic = L"战场目标格受地形限制，拒绝位置预览";
        return result;
    }
    for (const BattleUnit& other : state_->battle.units) {
        if (other.editor_cache_id != moving->editor_cache_id &&
            IsCurrentUnit(*state_, other) && other.position == coordinate) {
            result.diagnostic = L"战场目标格已有其他单位";
            return result;
        }
    }
    result.valid = true;
    result.diagnostic = L"战场位置调整预览有效；尚未写入剧本";
    return result;
}

BattleEditorResult BattleEditorOps::ConfirmPositionAdjustment(
    const ccz::preview::BattleCoord& coordinate) {
    const BattlePlacementPreview preview = PreviewPositionAdjustment(coordinate);
    if (!preview.valid) return failure(preview.diagnostic.c_str());
    const std::uint64_t cache_id = state_->adjusting_cache_id;
    BattleEditorState proposed = *state_;
    auto unit = std::find_if(
        proposed.battle.units.begin(), proposed.battle.units.end(),
        [cache_id](const BattleUnit& candidate) {
            return candidate.editor_cache_id == cache_id &&
                   !candidate.editor_pending_delete;
        });
    if (unit == proposed.battle.units.end())
        return failure(L"待调整单位已失效，缓存未修改");
    const bool changed = unit->position != coordinate;
    if (changed) {
        unit->position = coordinate;
        mark_role(*unit, BattleFieldRole::X);
        mark_role(*unit, BattleFieldRole::Y);
    }
    proposed.selection = selection_for(*unit);
    proposed.mode = proposed.mode_before_position_adjustment;
    proposed.adjusting_cache_id = 0;
    proposed.hover = coordinate;
    if (!changed) {
        *state_ = std::move(proposed);
        return success(L"位置未变化，已结束调整", false, 0);
    }
    return CommitMutation(proposed, 1, unit->editor_staged_new
        ? L"新增单位位置已调整（仅更新布阵缓存）"
        : L"既有单位坐标已暂存（写回前仍需重验来源记录）");
}

BattleEditorResult BattleEditorOps::CancelPositionAdjustment() {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (state_->mode != BattleEditorMode::AdjustPosition ||
        state_->adjusting_cache_id == 0) {
        return failure(L"当前没有进行中的战场位置调整");
    }
    state_->mode = state_->mode_before_position_adjustment;
    state_->adjusting_cache_id = 0;
    state_->hover.reset();
    return success(L"已取消位置调整；缓存未变化");
}

bool BattleEditorOps::ApplyPatch(BattleUnit& unit, const BattleUnitPatch& patch,
                                 std::wstring& diagnostic) const {
    const bool has_target = patch.target.has_value() ||
                            patch.target_x.has_value() ||
                            patch.target_y.has_value();
    if (has_target && (!patch.target.has_value() ||
                       !patch.target_x.has_value() ||
                       !patch.target_y.has_value())) {
        diagnostic = L"目标字段必须同时提供 Data 号与坐标";
        return false;
    }
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
    if (has_target && (!HasRole(unit, BattleFieldRole::TargetDataId) ||
                       !HasRole(unit, BattleFieldRole::TargetX) ||
                       !HasRole(unit, BattleFieldRole::TargetY))) {
        diagnostic = L"当前战场记录不含完整目标字段";
        return false;
    }

    if (patch.data_id) unit.data_id = *patch.data_id;
    if (patch.position) unit.position = *patch.position;
    if (patch.appearance) unit.appearance = *patch.appearance;
    if (patch.hidden) unit.hidden = *patch.hidden;
    if (patch.direction) unit.direction = WrappedDirection(*patch.direction);
    if (patch.level) unit.level = *patch.level;
    if (patch.unit_type) unit.unit_type = *patch.unit_type;
    if (patch.reinforcement) unit.reinforcement = *patch.reinforcement;
    if (patch.ai_policy) unit.ai_policy = *patch.ai_policy;
    if (has_target) {
        unit.target = *patch.target;
        unit.target_x = *patch.target_x;
        unit.target_y = *patch.target_y;
    }

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
    if (has_target) {
        mark_role(unit, BattleFieldRole::TargetDataId);
        mark_role(unit, BattleFieldRole::TargetX);
        mark_role(unit, BattleFieldRole::TargetY);
    }
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
            !ccz::preview::can_place_battle_unit(proposed.battle,
                                                 *patch.position)) {
            return failure(L"修改后的战场坐标越界、地形禁放或地形未闭合");
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
                          L"战场单位已临时修改（Task 5 写回门仍关闭）");
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
    BattleEditorResult result = CommitMutation(proposed, 1,
        L"战场单位已临时删除（Task 5 写回门仍关闭）");
    if (result.ok) RefreshSelectedBrushDataId();
    return result;
}

BattleEditorResult BattleEditorOps::CycleDirection(int delta) {
    if (state_ == nullptr) return failure(L"战场编辑器状态为空");
    if (delta == 0) return success(L"战场方向未变化");
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
    message << L"战场 painter 已临时修改当前战场逻辑格的" << mode_name(mode)
            << L"字段（Task 5 写回门仍关闭）";
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
    rebased.mouse_direction = draft.mouse_direction;
    rebased.document_modified = draft.document_modified;
    rebased.adjusting_cache_id = 0;
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

    result.ok = true;
    return result;
}

}  // namespace ccz::editor_preview
