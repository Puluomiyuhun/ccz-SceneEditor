#include "BattleMapOperations.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace ccz::editor_preview {
namespace {

bool valid_map_coordinate(const ccz::preview::BattleCoord& coordinate,
    int map_width, int map_height) {
    return map_width > 0 && map_height > 0 &&
        ccz::preview::is_battle_coord_in_bounds(
            coordinate, map_width, map_height);
}

bool valid_data_id(int data_id) {
    return data_id >= 0 && data_id <= 1023;
}

bool reject(BattleMapOperationInstruction& output,
    std::wstring& diagnostic, const wchar_t* message) {
    output = {};
    diagnostic = message;
    return false;
}

}  // namespace

bool BuildBattleMoveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int raw_direction,
    int map_width, int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic) {
    diagnostic.clear();
    if (!valid_data_id(data_id))
        return reject(output, diagnostic, L"移动至拒绝：Data 号必须在 0 到 1023 之间");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"移动至拒绝：目标坐标超出当前地图范围");
    if (raw_direction < -1 || raw_direction > 3)
        return reject(output, diagnostic, L"移动至拒绝：单位朝向无效");
    output = {0x32, {0, data_id, 0, coordinate.x, coordinate.y,
        raw_direction}, map_width, map_height};
    return true;
}

bool BuildBattleReviveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic, std::optional<int> direction_override) {
    diagnostic.clear();
    if (!valid_data_id(data_id))
        return reject(output, diagnostic, L"复活至拒绝：Data 号必须在 0 到 1023 之间");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"复活至拒绝：目标坐标超出当前地图范围");
    const int direction = direction_override.value_or(-1);
    if (direction < -1 || direction > 3)
        return reject(output, diagnostic, L"复活至拒绝：单位朝向无效");
    output = {0x55, {0, data_id, 0, coordinate.x, coordinate.y, direction},
        map_width, map_height};
    return true;
}

bool BuildBattleRoleOperationBatch(BattleRoleOperationKind kind,
    const std::vector<BattleRoleOperationSource>& sources,
    std::size_t anchor_index,
    const ccz::preview::BattleCoord& anchor_target,
    int map_width, int map_height,
    std::optional<int> direction_override,
    std::vector<BattleMapOperationInstruction>& instructions,
    std::vector<ccz::preview::BattleCoord>& target_positions,
    std::wstring& diagnostic) {
    instructions.clear();
    target_positions.clear();
    diagnostic.clear();
    if (sources.empty() || anchor_index >= sources.size()) {
        diagnostic = L"角色操作拒绝：没有有效的来源单位或锚点";
        return false;
    }
    if (!valid_map_coordinate(anchor_target, map_width, map_height)) {
        diagnostic = L"角色操作拒绝：锚点目标坐标超出当前地图范围";
        return false;
    }

    switch (kind) {
    case BattleRoleOperationKind::MoveTo:
    case BattleRoleOperationKind::ReviveTo:
        if (direction_override.has_value() &&
            (direction_override.value() < -1 ||
             direction_override.value() > 3)) {
            diagnostic = kind == BattleRoleOperationKind::ReviveTo
                ? L"复活至拒绝：单位朝向无效"
                : L"移动至拒绝：单位朝向无效";
            return false;
        }
        break;
    default:
        diagnostic = L"角色操作拒绝：未知操作类型";
        return false;
    }

    const auto& anchor = sources[anchor_index];
    if (!valid_map_coordinate(anchor.origin, map_width, map_height)) {
        diagnostic = L"角色操作拒绝：锚点来源坐标超出当前地图范围";
        return false;
    }
    const std::int64_t delta_x = static_cast<std::int64_t>(anchor_target.x) -
        anchor.origin.x;
    const std::int64_t delta_y = static_cast<std::int64_t>(anchor_target.y) -
        anchor.origin.y;

    std::vector<BattleMapOperationInstruction> prepared_instructions;
    std::vector<ccz::preview::BattleCoord> prepared_targets;
    prepared_instructions.reserve(sources.size());
    prepared_targets.reserve(sources.size());
    for (const auto& source : sources) {
        if (!valid_map_coordinate(source.origin, map_width, map_height)) {
            diagnostic = L"角色操作拒绝：组内来源坐标超出当前地图范围";
            return false;
        }
        const std::int64_t target_x =
            static_cast<std::int64_t>(source.origin.x) + delta_x;
        const std::int64_t target_y =
            static_cast<std::int64_t>(source.origin.y) + delta_y;
        if (target_x < 0 || target_y < 0 ||
            target_x >= map_width || target_y >= map_height) {
            diagnostic = L"角色操作拒绝：组内目标格超出当前地图范围";
            return false;
        }
        const ccz::preview::BattleCoord target{
            static_cast<int>(target_x), static_cast<int>(target_y)};
        BattleMapOperationInstruction instruction;
        const bool built = kind == BattleRoleOperationKind::ReviveTo
            ? BuildBattleReviveToInstruction(source.data_id, target,
                map_width, map_height, instruction, diagnostic,
                direction_override)
            : BuildBattleMoveToInstruction(source.data_id, target,
                direction_override.value_or(source.direction),
                map_width, map_height,
                instruction, diagnostic);
        if (!built) return false;
        prepared_targets.push_back(target);
        prepared_instructions.push_back(std::move(instruction));
    }

    instructions.swap(prepared_instructions);
    target_positions.swap(prepared_targets);
    return true;
}

bool BuildBattleObstacleInstruction(int type,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic) {
    diagnostic.clear();
    if (type < 0 || type > 2)
        return reject(output, diagnostic, L"障碍物拒绝：当前只支持火、船、火船三种类型");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"障碍物拒绝：目标坐标超出当前地图范围");
    const int terrain = type == 1 ? 27 : 26;
    output = {0x58, {type, 0, terrain, coordinate.x, coordinate.y, 0, 0},
        map_width, map_height};
    return true;
}

bool ValidateBattleMapOperationInstruction(
    const BattleMapOperationInstruction& instruction,
    std::wstring& diagnostic) {
    diagnostic.clear();
    if (!valid_map_coordinate(
            ccz::preview::BattleCoord{0, 0},
            instruction.map_width, instruction.map_height)) {
        diagnostic = L"地图操作拒绝：地图尺寸无效";
        return false;
    }
    if (instruction.instruction_id == 0x32 ||
        instruction.instruction_id == 0x55) {
        if (instruction.int_data.size() != 6U ||
            instruction.int_data[0] != 0 ||
            instruction.int_data[2] != 0 ||
            !valid_data_id(instruction.int_data[1])) {
            diagnostic = L"地图操作拒绝：移动/复活指令字段无效";
            return false;
        }
        const ccz::preview::BattleCoord coordinate{
            instruction.int_data[3], instruction.int_data[4]};
        if (!valid_map_coordinate(coordinate, instruction.map_width,
                instruction.map_height)) {
            diagnostic = L"地图操作拒绝：目标坐标超出地图范围";
            return false;
        }
        if ((instruction.instruction_id == 0x32 ||
             instruction.instruction_id == 0x55) &&
            (instruction.int_data[5] < -1 || instruction.int_data[5] > 3)) {
            diagnostic = instruction.instruction_id == 0x55
                ? L"地图操作拒绝：复活指令朝向无效"
                : L"地图操作拒绝：移动指令朝向无效";
            return false;
        }
        return true;
    }
    if (instruction.instruction_id == 0x58) {
        if (instruction.int_data.size() != 7U ||
            instruction.int_data[0] < 0 || instruction.int_data[0] > 2 ||
            instruction.int_data[1] != 0 ||
            instruction.int_data[2] !=
                (instruction.int_data[0] == 1 ? 27 : 26) ||
            instruction.int_data[5] != 0 || instruction.int_data[6] != 0) {
            diagnostic = L"地图操作拒绝：障碍物指令字段无效";
            return false;
        }
        const ccz::preview::BattleCoord coordinate{
            instruction.int_data[3], instruction.int_data[4]};
        if (!valid_map_coordinate(coordinate, instruction.map_width,
                instruction.map_height)) {
            diagnostic = L"地图操作拒绝：目标坐标超出地图范围";
            return false;
        }
        return true;
    }
    diagnostic = L"地图操作拒绝：指令不在移动、复活、障碍物白名单内";
    return false;
}

bool BattleObstacleDraftQueue::Add(int type,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height) {
    if (type < 0 || type > 2 ||
        !valid_map_coordinate(coordinate, map_width, map_height)) return false;
    items_.push_back({type, coordinate});
    return true;
}

bool BattleObstacleDraftQueue::RemoveAt(
    const ccz::preview::BattleCoord& coordinate) {
    const auto found = std::find_if(items_.rbegin(), items_.rend(),
        [&coordinate](const BattleObstacleDraft& item) {
            return item.coordinate == coordinate;
        });
    if (found == items_.rend()) return false;
    items_.erase(std::prev(found.base()));
    return true;
}

}  // namespace ccz::editor_preview
