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

bool BuildBattleHiddenAppearBatch(const std::vector<ccz::preview::BattleUnit>& units,
    int width,int height,std::vector<BattleMapOperationInstruction>& commands,
    std::size_t& skipped,std::wstring& diagnostic) {
    commands.clear();skipped=0;diagnostic.clear();
    std::vector<BattleMapOperationInstruction> pending;
    for(const auto& unit:units){
        if(!unit.hidden || unit.editor_pending_delete){++skipped;continue;}
        if(!unit.fields_decoded){diagnostic=L"隐藏角色的出场参数未解析，请重新读取";return false;}
        BattleCommandConfiguration config{0x4c,{0,0,0}};
        if(valid_data_id(unit.data_id))config.fields[1]=unit.data_id;
        else if(unit.battlefield_index>=0&&unit.battlefield_index<=249){config.fields[0]=1;config.fields[2]=unit.battlefield_index;}
        else{diagnostic=L"隐藏角色缺少可用的 DATA 号或战场编号";return false;}
        BattleMapOperationInstruction command;
        if(!BuildBattleCommandInstruction(config,width,height,command,diagnostic))return false;
        pending.push_back(std::move(command));
    }
    commands=std::move(pending);return true;
}

BattleCommandConfiguration DefaultBattleCommandConfiguration(std::uint16_t id) {
    switch (id) {
    case 0x25: case 0x4c: return {id, {0,0,0}};
    case 0x26: return {id, {0,0,0,0,0}};
    case 0x31: return {id, {0,0,0,0,0,0,6}};
    case 0x4f: return {id, {0,-1,0,0,1,1}};
    case 0x50: return {id, {0,0,1,1}};
    case 0x53: return {id, {0,0,0,0,0,0,6,0}};
    case 0x4d: return {id, {0,0,0,0,0,0,0,6,-1,-1,-1,0,0}};
    case 0x4e: return {id, {0,0,0,0,0,0,6,0,0,0,0}};
    case 0x5b: return {id, {0,0,0,0,1}};
    case 0x5c: return {id, {0}};
    case 0x6b: return {id, {0,0,0,0}};
    default: return {id,{}};
    }
}

bool BuildBattleCommandInstruction(const BattleCommandConfiguration& config,
    int width, int height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic) {
    BattleMapOperationInstruction candidate{config.instruction_id,config.fields,width,height};
    if (!ValidateBattleMapOperationInstruction(candidate, diagnostic)) {
        output = {}; return false;
    }
    output = std::move(candidate); return true;
}

bool BuildBattleMoveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int raw_direction,
    int map_width, int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic) {
    diagnostic.clear();
    if (!valid_data_id(data_id) && !(data_id>=1028 && data_id<=1277))
        return reject(output, diagnostic, L"移动至拒绝：Data 号必须在 0 到 1023 之间");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"移动至拒绝：目标坐标超出当前地图范围");
    if (raw_direction < -1 || raw_direction > 3)
        return reject(output, diagnostic, L"移动至拒绝：单位朝向无效");
    output = {0x32, {data_id>=1028?1:0, data_id>=1028?0:data_id, data_id>=1028?data_id-1028:0, coordinate.x, coordinate.y,
        raw_direction}, map_width, map_height};
    return true;
}

bool BuildBattleReviveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic, std::optional<int> direction_override) {
    diagnostic.clear();
    if (!valid_data_id(data_id) && !(data_id>=1028 && data_id<=1277))
        return reject(output, diagnostic, L"复活至拒绝：Data 号必须在 0 到 1023 之间");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"复活至拒绝：目标坐标超出当前地图范围");
    const int direction = direction_override.value_or(-1);
    if (direction < -1 || direction > 3)
        return reject(output, diagnostic, L"复活至拒绝：单位朝向无效");
    output = {0x55, {data_id>=1028?1:0, data_id>=1028?0:data_id, data_id>=1028?data_id-1028:0, coordinate.x, coordinate.y, direction},
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
    std::wstring& diagnostic, const BattleObstacleDefaults& parameters) {
    diagnostic.clear();
    if (!IsBattleObstacleTypeSupported(type))
        return reject(output, diagnostic, L"障碍物拒绝：类型须为火、船、火船或 Gate-1～Gate-247");
    if (!valid_map_coordinate(coordinate, map_width, map_height))
        return reject(output, diagnostic, L"障碍物拒绝：目标坐标超出当前地图范围");
    if (parameters.visibility < 0 || parameters.visibility > 1 || parameters.terrain < -1 || parameters.terrain > 29 || parameters.viewpoint < 0 || parameters.viewpoint > 1 || parameters.sound < 0 || parameters.sound > 1)
        return reject(output, diagnostic, L"障碍物参数无效");
    const int terrain = parameters.terrain < 0 ? (type >= 4 ? 17 : type == 1 ? 27 : 26) : parameters.terrain;
    output = {0x58, {type, parameters.visibility, terrain, coordinate.x, coordinate.y, parameters.viewpoint, parameters.sound},
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
    const auto& f = instruction.int_data;
    const auto id = instruction.instruction_id;
    const auto range = [](int v,int low,int high) { return v>=low && v<=high; };
    const auto per2 = [&](int v) { return valid_data_id(v) || range(v,1028,1277); };
    // Entry tests use the original per2 selector: DATA, function roles, battlefield slots, v0..v4095 and none.
    const auto entry_person = [&](int v) { return range(v,-4097,1277); };
    const auto rectangle = [&](int start) {
        return range(f[start],0,32767) && range(f[start+1],0,32767) &&
            range(f[start+2],0,32767) && range(f[start+3],0,32767) &&
            f[start]<=f[start+2] && f[start+1]<=f[start+3];
    };
    std::size_t count = (id==0x25||id==0x4c)?3:id==0x26?5:id==0x53?8:id==0x31?7:id==0x4d?13:id==0x4e?11:id==0x5b?5:id==0x5c?1:id==0x6b?4:id==0x4f?6:id==0x50?4:0;
    if (count) {
        if (f.size()!=count && f.size()!=20) { diagnostic=L"S 指令字段数量无效"; return false; }
        if (f.size()==20 && std::any_of(f.begin()+count,f.end(),[](int v){return v!=0;})) { diagnostic=L"S 指令尾部字段必须为零";return false; }
        bool valid=false;
        switch(id) {
        case 0x4c: valid=range(f[0],0,1)&&(f[0]==0?valid_data_id(f[1]):range(f[2],0,249));break;
        case 0x25: valid=entry_person(f[0])&&valid_map_coordinate({f[1],f[2]},instruction.map_width,instruction.map_height);break;
        case 0x26: valid=entry_person(f[0])&&rectangle(1)&&valid_map_coordinate({f[1],f[2]},instruction.map_width,instruction.map_height)&&valid_map_coordinate({f[3],f[4]},instruction.map_width,instruction.map_height);break;
        case 0x4f:
            valid=per2(f[0]) && (f[1]==-1 || per2(f[1])) && range(f[2],-1,3) &&
                range(f[3],0,1) && range(f[4],0,1) && range(f[5],0,1); break;
        case 0x50:
            valid=per2(f[0]) && range(f[1],-1,12) && range(f[2],0,1) && range(f[3],0,1); break;
        case 0x31:
            valid=range(f[0],0,1) && (f[0]==0 ? per2(f[1]) : rectangle(2)&&range(f[6],0,6)); break;
        case 0x53:
            valid=range(f[0],0,1) && (f[0]==0 ? per2(f[1]) : rectangle(2)&&range(f[6],0,6)) && range(f[7],0,1); break;
        case 0x4d:
            valid=range(f[0],0,2) && (f[0]==0 ? per2(f[1]) : f[0]==1 ? range(f[2],0,249) : rectangle(3)&&range(f[7],0,6)) &&
                range(f[8],-1,5) && range(f[9],-1,2) &&
                (f[10]==-1 || (range(f[10],2,254) && (f[10]&1)==0 && (f[10]&126)!=0)); break;
        case 0x4e:
            valid=range(f[0],0,1) && (f[0]==0 ? per2(f[1]) : rectangle(2)&&range(f[6],0,6)) && range(f[7],0,6) &&
                ((f[7]!=3&&f[7]!=5)||per2(f[8])) &&
                ((f[7]!=4&&f[7]!=6)||valid_map_coordinate({f[9],f[10]},instruction.map_width,instruction.map_height)); break;
        case 0x5b: valid=rectangle(0)&&range(f[4],0,1);break;
        case 0x5c: valid=per2(f[0]);break;
        case 0x6b: valid=valid_map_coordinate({f[0],f[1]},instruction.map_width,instruction.map_height)&&range(f[2],0,109)&&range(f[3],0,1);break;
        }
        if (!valid) diagnostic=L"S 指令参数超出当前模式、编号或地图范围";
        return valid;
    }
    if (instruction.instruction_id == 0x32 ||
        instruction.instruction_id == 0x55) {
        if ((f.size()!=6U && f.size()!=20U) ||
            (f.size()==20U && std::any_of(f.begin()+6,f.end(),[](int v){return v!=0;})) ||
            !range(f[0],0,1) || (f[0]==0 ? !per2(f[1]) : !range(f[2],0,249))) {
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
        if ((instruction.int_data.size() != 7U && instruction.int_data.size() != 20U) ||
            (instruction.int_data.size() == 20U && std::any_of(instruction.int_data.begin() + 7, instruction.int_data.end(), [](int value) { return value != 0; })) ||
            !IsBattleObstacleTypeSupported(instruction.int_data[0]) ||
            instruction.int_data[1] < 0 || instruction.int_data[1] > 1 ||
            instruction.int_data[2] < 0 || instruction.int_data[2] > 29 ||
            instruction.int_data[5] < 0 || instruction.int_data[5] > 1 ||
            instruction.int_data[6] < 0 || instruction.int_data[6] > 1) {
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
    int map_height, const BattleObstacleDefaults& parameters) {
    BattleMapOperationInstruction checked; std::wstring diagnostic;
    if (!BuildBattleObstacleInstruction(type, coordinate, map_width, map_height, checked, diagnostic, parameters)) return false;
    if (!valid_map_coordinate(coordinate, map_width, map_height)) return false;
    if (std::any_of(items_.begin(), items_.end(), [&coordinate](const auto& item) {
        return item.coordinate == coordinate;
    })) return false;
    items_.push_back({type, coordinate, parameters, next_identity_++});
    return true;
}

bool BattleObstacleDraftQueue::UpdateAt(const ccz::preview::BattleCoord& origin,
    const BattleObstacleDraft& replacement, int width, int height) {
    auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.coordinate == origin; });
    if (found == items_.end()) return false;
    BattleMapOperationInstruction checked; std::wstring diagnostic;
    if (!BuildBattleObstacleInstruction(replacement.type, replacement.coordinate, width, height, checked, diagnostic, replacement.parameters)) return false;
    if (std::any_of(items_.begin(), items_.end(), [&](const auto& item) { return &item != &*found && item.coordinate == replacement.coordinate; })) return false;
    const auto original_identity=found->identity;
    *found = replacement;
    found->identity=original_identity;
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
