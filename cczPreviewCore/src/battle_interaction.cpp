#include <ccz/preview/battle_interaction.h>

#include <algorithm>
#include <sstream>

namespace ccz::preview {
namespace {

int wrap_direction(int value) {
    value %= 4;
    if (value < 0) value += 4;
    return value;
}

BattleInteractionResult failure(const wchar_t* message) {
    BattleInteractionResult result;
    result.diagnostic = message;
    return result;
}

}  // namespace

BattleInteractionResult BattleInteraction::Select(
    const BattlePreviewState& state, const LogicalNodeKey& source_key,
    std::size_t record_index) {
    if (source_key.empty()) return failure(L"战场单位来源为空，拒绝选择");
    for (const BattleUnit& unit : state.units) {
        if (unit.source_key == source_key &&
            unit.record_index == record_index &&
            unit.fields_decoded) {
            selected_ = Selection{source_key, record_index};
            BattleInteractionResult result;
            result.ok = true;
            result.diagnostic = L"已选择当前战场单位（临时状态）";
            result.after_summary = Summary(unit);
            return result;
        }
    }
    return failure(L"单位未解码或不在当前作用域");
}

BattleInteractionResult BattleInteraction::ResolveSelected(
    BattlePreviewState& state, BattleUnit*& unit) {
    unit = nullptr;
    if (!selected_) return failure(L"没有可编辑的当前作用域单位");
    for (BattleUnit& candidate : state.units) {
        if (candidate.source_key == selected_->source_key &&
            candidate.record_index == selected_->record_index &&
            candidate.fields_decoded) {
            unit = &candidate;
            BattleInteractionResult result;
            result.ok = true;
            return result;
        }
    }
    return failure(L"当前选择已离开战场作用域，未修改状态");
}

bool BattleInteraction::HasRole(const BattleUnit& unit, BattleFieldRole role) {
    const BattleFieldSpec* spec = get_battle_field_spec(unit.source_instruction);
    if (spec == nullptr || !spec->semantic_complete) return false;
    return std::find(spec->field_roles.begin(), spec->field_roles.end(), role) !=
           spec->field_roles.end();
}

BattleInteractionResult BattleInteraction::RequireSelectedRole(
    BattlePreviewState& state, BattleFieldRole role, BattleUnit*& unit) {
    BattleInteractionResult result = ResolveSelected(state, unit);
    if (!result.ok) return result;
    if (!HasRole(*unit, role)) {
        unit = nullptr;
        return failure(L"当前战场记录不包含该字段，未修改状态");
    }
    return result;
}

std::wstring BattleInteraction::Summary(const BattleUnit& unit) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << unit.source_instruction
           << std::dec << L" record=" << unit.record_index << L" group="
           << unit.page << L" data=" << unit.data_id << L" pos=("
           << unit.position.x << L"," << unit.position.y << L") dir="
           << wrap_direction(unit.direction) << L" hidden="
           << (unit.hidden ? 1 : 0) << L" level=" << unit.level << L" type="
           << unit.unit_type << L" target=" << unit.target << L" ("
           << unit.target_x << L"," << unit.target_y << L")";
    return stream.str();
}

BattleInteractionResult BattleInteraction::ChangedResult(
    const std::wstring& before, const std::wstring& after) {
    BattleInteractionResult result;
    result.ok = true;
    result.changed = before != after;
    result.before_summary = before;
    result.after_summary = after;
    result.diagnostic = result.changed ? L"战场临时状态已修改，尚未写入剧本"
                                       : L"临时状态未发生变化";
    return result;
}

BattleInteractionResult BattleInteraction::Move(BattlePreviewState& state,
                                                 BattleCoord target) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(state,
                                                         BattleFieldRole::X,
                                                         unit);
    if (!result.ok) return result;
    if (!HasRole(*unit, BattleFieldRole::Y)) {
        return failure(L"当前战场记录不包含完整坐标字段，未修改状态");
    }
    if (!state.map.valid || state.map.width <= 0 || state.map.height <= 0) {
        return failure(L"战场地图尺寸未闭合，拒绝临时移动");
    }
    const std::size_t expected_size =
        static_cast<std::size_t>(state.map.width) *
        static_cast<std::size_t>(state.map.height);
    if (state.terrain.size() < expected_size) {
        return failure(L"战场地形数据未闭合，拒绝临时移动");
    }
    if (!can_place_battle_unit(state, target)) {
        return failure(L"目标坐标越界或禁放");
    }
    for (const BattleUnit& other : state.units) {
        if (&other == unit || !other.fields_decoded ||
            other.position != target) {
            continue;
        }
        return failure(L"目标坐标已被当前战场单位占用");
    }
    const std::wstring before = Summary(*unit);
    unit->position = target;
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::X);
        mark_battle_field_dirty(*unit, BattleFieldRole::Y);
    }
    return changed;
}

BattleInteractionResult BattleInteraction::CycleDirection(
    BattlePreviewState& state, int delta, bool direction_locked) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(
        state, BattleFieldRole::Direction, unit);
    if (!result.ok) return result;
    if (direction_locked) return failure(L"方向锁定，未修改");
    const std::wstring before = Summary(*unit);
    unit->direction = wrap_direction(unit->direction + delta);
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::Direction);
    }
    return changed;
}

BattleInteractionResult BattleInteraction::SetHidden(BattlePreviewState& state,
                                                      bool hidden) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(
        state, BattleFieldRole::Hidden, unit);
    if (!result.ok) return result;
    const std::wstring before = Summary(*unit);
    unit->hidden = hidden;
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::Hidden);
    }
    return changed;
}

BattleInteractionResult BattleInteraction::SetLevel(BattlePreviewState& state,
                                                     int level) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(
        state, BattleFieldRole::Level, unit);
    if (!result.ok) return result;
    const std::wstring before = Summary(*unit);
    unit->level = level;
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::Level);
    }
    return changed;
}

BattleInteractionResult BattleInteraction::SetUnitType(
    BattlePreviewState& state, int unit_type) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(
        state, BattleFieldRole::UnitType, unit);
    if (!result.ok) return result;
    const std::wstring before = Summary(*unit);
    unit->unit_type = unit_type;
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::UnitType);
    }
    return changed;
}

BattleInteractionResult BattleInteraction::SetTarget(
    BattlePreviewState& state, int data_id, int x, int y) {
    BattleUnit* unit = nullptr;
    BattleInteractionResult result = RequireSelectedRole(
        state, BattleFieldRole::TargetDataId, unit);
    if (!result.ok) return result;
    if (!HasRole(*unit, BattleFieldRole::TargetX) ||
        !HasRole(*unit, BattleFieldRole::TargetY)) {
        return failure(L"当前战场记录不包含完整目标字段，未修改状态");
    }
    const std::wstring before = Summary(*unit);
    unit->target = data_id;
    unit->target_x = x;
    unit->target_y = y;
    BattleInteractionResult changed = ChangedResult(before, Summary(*unit));
    if (changed.changed) {
        mark_battle_field_dirty(*unit, BattleFieldRole::TargetDataId);
        mark_battle_field_dirty(*unit, BattleFieldRole::TargetX);
        mark_battle_field_dirty(*unit, BattleFieldRole::TargetY);
    }
    return changed;
}

void BattleInteraction::CaptureScriptState(const BattlePreviewState& state) {
    baseline_ = state;
    ResetSelection();
}

void BattleInteraction::ClearScriptState() noexcept {
    baseline_.reset();
    ResetSelection();
}

bool BattleInteraction::ResetToScriptState(BattlePreviewState& state) {
    if (!baseline_) return false;
    state = *baseline_;
    ResetSelection();
    return true;
}

void BattleInteraction::ResetSelection() noexcept { selected_.reset(); }

}  // namespace ccz::preview
