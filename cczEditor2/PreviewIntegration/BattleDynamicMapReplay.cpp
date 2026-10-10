#include "BattleDynamicMapReplay.h"
#include "BattleMapOperations.h"
#include "BattleFacing.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace ccz::editor_preview {
namespace {

using ccz::preview::NativeReplayItem;

bool valid_map(const ccz::preview::BattleMapInfo& map) {
    return map.valid && map.width > 0 && map.height > 0;
}

bool in_bounds(const ccz::preview::BattleCoord& coordinate,
               const ccz::preview::BattleMapInfo& map) {
    return ccz::preview::is_battle_coord_in_bounds(
        coordinate, map.width, map.height);
}

bool has_supported_field_storage(
    const std::vector<std::int32_t>& fields, std::size_t semantic_count) {
    // Existing editor instructions keep their parameters in a 20-slot
    // int_data buffer. Newly inserted preview operations use compact vectors.
    // The command semantics consume only the leading semantic_count entries.
    return fields.size() == semantic_count || fields.size() == 20;
}

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (message.empty() || destination.find(message) != std::wstring::npos)
        return;
    if (!destination.empty()) destination += L"；";
    destination += message;
}

bool read_obstacle_event(const NativeReplayItem& item,
                         const ccz::preview::BattleMapInfo& map,
                         BattleDynamicMapEvent& event,
                         std::wstring& diagnostic) {
    const auto& fields = item.command.int_data;
    if (!has_supported_field_storage(fields, 7) ||
        !IsBattleObstacleTypeSupported(fields[0]) ||
        (fields[1] != 0 && fields[1] != 1) ||
        fields[2] < 0 || fields[2] > 29 ||
        fields[5] < 0 || fields[5] > 1 || fields[6] < 0 || fields[6] > 1) {
        diagnostic = L"0x58 障碍物类型或字段格式不支持";
        return false;
    }
    const ccz::preview::BattleCoord coordinate{fields[3], fields[4]};
    if (!in_bounds(coordinate, map)) {
        diagnostic = L"0x58 障碍物坐标超出 Scene1 地图范围";
        return false;
    }
    event.logical_key = item.logical_key;
    event.coordinate = coordinate;
    event.obstacle_type = fields[0];
    event.obstacle_source_handle = item.runtime_handle;
    event.obstacle_source_fields = fields;
    event.kind = fields[1] == 0
        ? BattleDynamicMapEventKind::ObstacleShow
        : BattleDynamicMapEventKind::ObstacleHide;
    return true;
}

bool apply_obstacle_event(const BattleDynamicMapEvent& event,
                          std::vector<BattleDynamicObstacle>& obstacles) {
    const auto same_cell = [&event](const BattleDynamicObstacle& obstacle) {
        return obstacle.coordinate == event.coordinate;
    };
    if (event.kind == BattleDynamicMapEventKind::ObstacleShow) {
        obstacles.erase(std::remove_if(obstacles.begin(), obstacles.end(),
            same_cell), obstacles.end());
        obstacles.push_back({event.obstacle_type, event.coordinate, event.obstacle_source_handle, event.logical_key, event.obstacle_source_fields});
        return true;
    }
    const auto found = std::find_if(obstacles.rbegin(), obstacles.rend(),
        [&event](const BattleDynamicObstacle& obstacle) {
            return obstacle.type == event.obstacle_type &&
                   obstacle.coordinate == event.coordinate;
        });
    if (found != obstacles.rend())
        obstacles.erase(std::prev(found.base()));
    return true;
}

std::vector<std::size_t> matching_units(
    const std::vector<ccz::preview::BattleUnit>& units, int data_id) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (unit.data_id == data_id && unit.fields_decoded &&
            !unit.editor_pending_delete &&
            !ccz::preview::is_battle_deployment_slot_marker(unit)) {
            result.push_back(index);
        }
    }
    return result;
}

std::vector<std::size_t> matching_battlefield_index(
    const std::vector<ccz::preview::BattleUnit>& units, int battlefield_index) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (unit.battlefield_index == battlefield_index &&
            unit.fields_decoded && !unit.editor_pending_delete) {
            result.push_back(index);
        }
    }
    return result;
}

std::uint16_t deployment_instruction(
    const ccz::preview::BattleUnit& unit) {
    return unit.editor_deployment_instruction != 0xffff
        ? unit.editor_deployment_instruction
        : unit.source_instruction;
}

bool matches_faction(const ccz::preview::BattleUnit& unit, int faction) {
    const std::uint16_t deployment = deployment_instruction(unit);
    const bool own = deployment == 0x4b;
    const bool ally = deployment == 0x46;
    const bool enemy = deployment == 0x47 && !unit.reinforcement;
    const bool reinforcement = deployment == 0x47 && unit.reinforcement;
    switch (faction) {
    case 0: return own;
    case 1: return ally;
    case 2: return enemy;
    case 3: return reinforcement;
    case 4: return own || ally;
    case 5: return enemy || reinforcement;
    case 6: return own || ally || enemy || reinforcement;
    default: return false;
    }
}

bool inside_area(const ccz::preview::BattleUnit& unit,
                 const BattleDynamicMapEvent& event) {
    return unit.position.x >= event.coordinate.x &&
           unit.position.x <= event.area_end.x &&
           unit.position.y >= event.coordinate.y &&
           unit.position.y <= event.area_end.y &&
           matches_faction(unit, event.faction);
}

}  // namespace

bool BuildBattleDynamicObstacleBaseline(
    const ccz::preview::NativeReplayPlan& scene1_plan,
    const ccz::preview::BattleMapInfo& map,
    std::vector<BattleDynamicObstacle>& obstacles,
    bool& path_complete, std::wstring& diagnostic) {
    obstacles.clear();
    diagnostic.clear();
    path_complete = scene1_plan.complete;
    if (!valid_map(map)) {
        diagnostic = L"Scene1 障碍物基线缺少有效战场地图尺寸";
        path_complete = false;
        return false;
    }
    if (!scene1_plan.complete) {
        append_diagnostic(diagnostic, scene1_plan.diagnostic.empty()
            ? L"Scene1 条件路径未完整求值，障碍物基线只包含已知前缀"
            : scene1_plan.diagnostic);
    }
    for (const NativeReplayItem& item : scene1_plan.items) {
        if (item.command.instruction_id != 0x58) continue;
        BattleDynamicMapEvent event;
        event.instruction_id = item.command.instruction_id;
        event.runtime_handle = item.runtime_handle;
        event.source_fields = item.command.int_data;
        event.logical_key = item.logical_key;
        std::wstring event_diagnostic;
        if (!read_obstacle_event(item, map, event, event_diagnostic)) {
            path_complete = false;
            append_diagnostic(diagnostic, event_diagnostic);
            break;
        }
        apply_obstacle_event(event, obstacles);
    }
    return true;
}

const std::vector<BattleDynamicObstacle>&
BattleDynamicObstacleBaselineForTarget(
    bool target_is_scene1,
    const std::vector<BattleDynamicObstacle>& scene1_final_obstacles) {
    static const std::vector<BattleDynamicObstacle> empty_obstacles;
    return target_is_scene1 ? empty_obstacles : scene1_final_obstacles;
}

BattleDynamicMapPlan BuildBattleDynamicMapPlan(
    const ccz::preview::NativeReplayPlan& section_plan,
    const ccz::preview::BattlePreviewState& scene1_baseline) {
    BattleDynamicMapPlan result;
    result.path_complete = section_plan.complete;
    if (!valid_map(scene1_baseline.map)) {
        result.path_complete = false;
        result.diagnostic = L"动态预览缺少有效的 Scene1 地图尺寸";
        return result;
    }
    if (!section_plan.complete) {
        append_diagnostic(result.diagnostic, section_plan.diagnostic.empty()
            ? L"Section 条件路径未完整求值，结果只显示已知前缀"
            : section_plan.diagnostic);
    }

    for (const NativeReplayItem& item : section_plan.items) {
        const std::uint16_t instruction_id = item.command.instruction_id;
        if (instruction_id != 0x31 && instruction_id != 0x32 &&
            instruction_id != 0x4c && instruction_id != 0x55 &&
            instruction_id != 0x58 && instruction_id != 0x4d &&
            instruction_id != 0x4e && instruction_id != 0x5b &&
            instruction_id != 0x5c && instruction_id != 0x6b && instruction_id != 0x53 && instruction_id != 0x4f && instruction_id != 0x50) {
            // This projection deliberately covers only the operations exposed
            // by the S map tools. Setup/deployment and other map-state
            // instructions belong to the board baseline or are outside this
            // snapshot feature; they must not block a later supported command
            // in the same reachable Section prefix.
            continue;
        }

        BattleDynamicMapEvent event;
        event.instruction_id = item.command.instruction_id;
        event.runtime_handle = item.runtime_handle;
        event.source_fields = item.command.int_data;
        event.logical_key = item.logical_key;
        std::wstring event_diagnostic;
        if (instruction_id==0x4f || instruction_id==0x50) {
            const auto& f=item.command.int_data;
            const std::size_t count=instruction_id==0x4f?6:4;
            if(has_supported_field_storage(f,count) &&
               (!IsRegularBattlePerson(f[0]) || (instruction_id==0x4f && f[1]!=-1 && !IsRegularBattlePerson(f[1])))) {
                append_diagnostic(result.diagnostic,L"4F/50 的变量或特殊对象不提供预览支持，本条跳过");continue;
            }
            BattleMapOperationInstruction checked;
            if(!BuildBattleCommandInstruction({instruction_id,f},scene1_baseline.map.width,scene1_baseline.map.height,checked,event_diagnostic)) {
                result.path_complete=false;append_diagnostic(result.diagnostic,event_diagnostic);break;
            }
            event.kind=instruction_id==0x4f?BattleDynamicMapEventKind::Turn:BattleDynamicMapEventKind::Action;
            if(f[0]>=1028){event.target_kind=BattleDynamicUnitTargetKind::BattlefieldIndex;event.battlefield_index=f[0]-1028;}
            else event.data_id=f[0];
        } else if (instruction_id == 0x4d || instruction_id == 0x4e || instruction_id == 0x53 ||
            instruction_id == 0x5b || instruction_id == 0x5c || instruction_id == 0x6b) {
            const auto& f = item.command.int_data;
            BattleMapOperationInstruction checked;
            if (!BuildBattleCommandInstruction({instruction_id,f},scene1_baseline.map.width,scene1_baseline.map.height,checked,event_diagnostic)) {
                result.path_complete=false; append_diagnostic(result.diagnostic,event_diagnostic);break;
            }
            if (instruction_id==0x6b) {
                event.kind=BattleDynamicMapEventKind::Animation;event.coordinate={f[0],f[1]};
            } else if(instruction_id==0x5b) {
                event.kind=BattleDynamicMapEventKind::HighlightArea;event.coordinate={f[0],f[1]};event.area_end={f[2],f[3]};
            } else {
                event.kind=instruction_id==0x53?BattleDynamicMapEventKind::Retreat:instruction_id==0x4d?BattleDynamicMapEventKind::StatusChange:instruction_id==0x4e?BattleDynamicMapEventKind::PolicyChange:BattleDynamicMapEventKind::HighlightUnit;
                const int mode=instruction_id==0x5c?0:f[0];
                const int selector=instruction_id==0x5c?f[0]:f[1];
                if ((instruction_id==0x4d&&mode==2)||((instruction_id==0x4e||instruction_id==0x53)&&mode==1)) {
                    const int start=instruction_id==0x4d?3:2;
                    event.target_kind=BattleDynamicUnitTargetKind::AreaAndFaction;
                    event.coordinate={f[start],f[start+1]};event.area_end={f[start+2],f[start+3]};event.faction=f[start+4];
                } else if ((instruction_id==0x4d&&mode==1)||selector>=1028) {
                    event.target_kind=BattleDynamicUnitTargetKind::BattlefieldIndex;
                    event.battlefield_index=instruction_id==0x4d&&mode==1?f[2]:selector-1028;
                } else {event.data_id=selector;}
                if(event.target_kind!=BattleDynamicUnitTargetKind::AreaAndFaction) {
                    const auto matches=event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(scene1_baseline.units,event.data_id):matching_battlefield_index(scene1_baseline.units,event.battlefield_index);
                    // A Data target may refer to an unresolved player-party
                    // slot or a unit outside the projected board. This command
                    // cannot change a known unit, but it must not suppress
                    // later independent map operations.
                    if (matches.empty()) {
                        append_diagnostic(result.diagnostic,
                            L"0x" + (instruction_id == 0x4d ? std::wstring(L"4D") : instruction_id == 0x4e ? std::wstring(L"4E") : instruction_id == 0x53 ? std::wstring(L"53") : std::wstring(L"5C")) +
                            (event.target_kind == BattleDynamicUnitTargetKind::DataId ? L" Data " + std::to_wstring(event.data_id) : L" 战场编号 " + std::to_wstring(event.battlefield_index)) +
                            L" 未匹配当前棋盘，本条未呈现；若为我军，请绑定槽位，后续指令继续推演");
                        continue;
                    }
                    if (matches.size() != 1) {
                if(matches.empty() && event.target_kind==BattleDynamicUnitTargetKind::DataId &&
                   std::any_of(scene1_baseline.units.begin(),scene1_baseline.units.end(),[](const auto& u){return u.source_instruction==0x4b && u.data_id<0;})) {
                    append_diagnostic(result.diagnostic,L"目标 DATA 尚未绑定我军编号，本条跳过，后续继续推演");continue;
                }

                        result.path_complete = false;
                        append_diagnostic(result.diagnostic,
                            L"S 指令目标编号不唯一，预览停在该指令之前");
                        break;
                    }
                }
            }
        } else if (instruction_id == 0x58) {
            if (!read_obstacle_event(item, scene1_baseline.map, event,
                                     event_diagnostic)) {
                result.path_complete = false;
                append_diagnostic(result.diagnostic, event_diagnostic);
                break;
            }
        } else if (instruction_id == 0x4c) {
            const auto& fields = item.command.int_data;
            if (!has_supported_field_storage(fields, 3) ||
                (fields[0] != 0 && fields[0] != 1)) {
                result.path_complete = false;
                append_diagnostic(result.diagnostic,
                    L"0x4C 隐藏武将出现参数格式无效，预览停在该指令之前");
                break;
            }
            event.kind = BattleDynamicMapEventKind::HiddenAppear;
            event.logical_key = item.logical_key;
            if (fields[0] == 0) {
                if (fields[1] < 0 || fields[1] > 1023) {
                    result.path_complete = false;
                    append_diagnostic(result.diagnostic,
                        L"0x4C Data 角色号无效，预览停在该指令之前");
                    break;
                }
                event.target_kind = BattleDynamicUnitTargetKind::DataId;
                event.data_id = fields[1];
            } else {
                if (fields[2] < 0 || fields[2] > 249) {
                    result.path_complete = false;
                    append_diagnostic(result.diagnostic,
                        L"0x4C 战场编号无效，预览停在该指令之前");
                    break;
                }
                event.target_kind =
                    BattleDynamicUnitTargetKind::BattlefieldIndex;
                event.battlefield_index = fields[2];
            }
            const auto matches = event.target_kind ==
                    BattleDynamicUnitTargetKind::DataId
                ? matching_units(scene1_baseline.units, event.data_id)
                : matching_battlefield_index(scene1_baseline.units,
                                             event.battlefield_index);
            if (matches.size() != 1) {
                if(matches.empty() && event.target_kind==BattleDynamicUnitTargetKind::DataId &&
                   std::any_of(scene1_baseline.units.begin(),scene1_baseline.units.end(),[](const auto& u){return u.source_instruction==0x4b && u.data_id<0;})) {
                    append_diagnostic(result.diagnostic,L"目标 DATA 尚未绑定我军编号，本条跳过，后续继续推演");continue;
                }
                result.path_complete = false;
                append_diagnostic(result.diagnostic, matches.empty()
                    ? L"0x4C 目标在 Scene1 布置中不存在，预览停在该指令之前"
                    : L"0x4C 目标编号在 Scene1 布置中不唯一，预览停在该指令之前");
                break;
            }
        } else if (instruction_id == 0x31) {
            const auto& fields = item.command.int_data;
            BattleMapOperationInstruction checked;
            if(!BuildBattleCommandInstruction({instruction_id,fields},scene1_baseline.map.width,scene1_baseline.map.height,checked,event_diagnostic)) {
                result.path_complete=false;append_diagnostic(result.diagnostic,event_diagnostic);break;
            }
            if (!has_supported_field_storage(fields, 7) ||
                (fields[0] != 0 && fields[0] != 1)) {
                result.path_complete = false;
                append_diagnostic(result.diagnostic,
                    L"S 0x31 武将消失参数格式无效，预览停在该指令之前");
                break;
            }
            event.kind = BattleDynamicMapEventKind::Disappear;
            event.logical_key = item.logical_key;
            if (fields[0] == 0) {
                if ((fields[1] < 0 || fields[1] > 1023) && (fields[1]<1028 || fields[1]>1277)) {
                    result.path_complete = false;
                    append_diagnostic(result.diagnostic,
                        L"S 0x31 Data 角色号无效，预览停在该指令之前");
                    break;
                }
                event.target_kind = BattleDynamicUnitTargetKind::DataId;
                event.data_id = fields[1];
                if(fields[1]>=1028) {event.target_kind=BattleDynamicUnitTargetKind::BattlefieldIndex;event.battlefield_index=fields[1]-1028;event.data_id=-1;}
                const auto matches = event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(scene1_baseline.units,event.data_id):matching_battlefield_index(scene1_baseline.units,event.battlefield_index);
                if (matches.size() != 1) {
                if(matches.empty() && event.target_kind==BattleDynamicUnitTargetKind::DataId &&
                   std::any_of(scene1_baseline.units.begin(),scene1_baseline.units.end(),[](const auto& u){return u.source_instruction==0x4b && u.data_id<0;})) {
                    append_diagnostic(result.diagnostic,L"目标 DATA 尚未绑定我军编号，本条跳过，后续继续推演");continue;
                }

                    result.path_complete = false;
                    append_diagnostic(result.diagnostic, matches.empty()
                        ? L"S 0x31 目标在 Scene1 布置中不存在，预览停在该指令之前"
                        : L"S 0x31 目标 Data 号在 Scene1 布置中不唯一，预览停在该指令之前");
                    break;
                }
            } else {
                if (fields[6] < 0 || fields[6] > 6) {
                    result.path_complete = false;
                    append_diagnostic(result.diagnostic,
                        L"S 0x31 范围阵营参数无效，预览停在该指令之前");
                    break;
                }
                event.target_kind =
                    BattleDynamicUnitTargetKind::AreaAndFaction;
                event.coordinate = {fields[2], fields[3]};
                event.area_end = {fields[4], fields[5]};
                event.faction = fields[6];
            }
        } else {
            const auto& fields = item.command.int_data;
            BattleMapOperationInstruction checked;
            if(!BuildBattleCommandInstruction({instruction_id,fields},scene1_baseline.map.width,scene1_baseline.map.height,checked,event_diagnostic)) {
                result.path_complete=false;append_diagnostic(result.diagnostic,event_diagnostic);break;
            }
            if(fields[0]==1 || fields[1]>=1028) {
                event.target_kind=BattleDynamicUnitTargetKind::BattlefieldIndex;
                event.battlefield_index=fields[0]==1?fields[2]:fields[1]-1028;
            } else event.data_id=fields[1];
            event.coordinate = {fields[3], fields[4]};
            event.direction = fields[5];
            event.logical_key = item.logical_key;
            event.kind = instruction_id == 0x32
                ? BattleDynamicMapEventKind::Move
                : BattleDynamicMapEventKind::Revive;
            if (!in_bounds(event.coordinate, scene1_baseline.map)) {
                result.path_complete = false;
                append_diagnostic(result.diagnostic,
                    L"移动/复活目标坐标超出 Scene1 地图范围，预览停在该指令之前");
                break;
            }
            const auto matches = event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(scene1_baseline.units,event.data_id):matching_battlefield_index(scene1_baseline.units,event.battlefield_index);
            if (matches.size() != 1) {
                if(matches.empty() && event.target_kind==BattleDynamicUnitTargetKind::DataId &&
                   std::any_of(scene1_baseline.units.begin(),scene1_baseline.units.end(),[](const auto& u){return u.source_instruction==0x4b && u.data_id<0;})) {
                    append_diagnostic(result.diagnostic,L"目标 DATA 尚未绑定我军编号，本条跳过，后续继续推演");continue;
                }

                result.path_complete = false;
                append_diagnostic(result.diagnostic, matches.empty()
                    ? L"移动/复活目标在 Scene1 布置中不存在，预览停在该指令之前"
                    : L"移动/复活目标 Data 号在 Scene1 布置中不唯一，预览停在该指令之前");
                break;
            }
        }
        result.events.push_back(std::move(event));
    }

    return result;
}

BattleDynamicMapPlan MergeBattleDynamicMapPlans(
    const BattleDynamicMapPlan& prefix,
    const BattleDynamicMapPlan& suffix) {
    BattleDynamicMapPlan result;
    result.path_complete = prefix.path_complete && suffix.path_complete;
    append_diagnostic(result.diagnostic, prefix.diagnostic);
    append_diagnostic(result.diagnostic, suffix.diagnostic);
    result.events.reserve(prefix.events.size() + suffix.events.size());
    result.events.insert(result.events.end(), prefix.events.begin(),
                         prefix.events.end());
    result.events.insert(result.events.end(), suffix.events.begin(),
                         suffix.events.end());
    return result;
}

BattleDynamicMapProjection ProjectBattleDynamicMap(
    const ccz::preview::BattlePreviewState& scene1_baseline,
    const std::vector<BattleDynamicObstacle>& scene1_obstacles,
    const BattleDynamicMapPlan& plan,
    const ccz::preview::LogicalNodeKey& selected_instruction) {
    BattleDynamicMapProjection result;
    result.battle = scene1_baseline;
    result.obstacles = scene1_obstacles;
    result.diagnostic = plan.diagnostic;
    for (const BattleDynamicMapEvent& event : plan.events) {
        // These visual effects are a selection preview, not persistent board state.
        if ((event.kind == BattleDynamicMapEventKind::Action ||
             event.kind == BattleDynamicMapEventKind::Animation ||
             event.kind == BattleDynamicMapEventKind::HighlightArea ||
             event.kind == BattleDynamicMapEventKind::HighlightUnit) &&
            (selected_instruction.empty() || event.logical_key != selected_instruction)) continue;
        if (event.kind == BattleDynamicMapEventKind::ObstacleShow ||
            event.kind == BattleDynamicMapEventKind::ObstacleHide) {
            apply_obstacle_event(event, result.obstacles);
            continue;
        }
        if (event.kind == BattleDynamicMapEventKind::Animation) {
            result.animations.push_back({event.coordinate,event.source_fields[2],event.source_fields[3]!=0,event.logical_key});
            continue; // Visual only: this never changes HP, MP or visibility.
        }
        if (event.kind == BattleDynamicMapEventKind::HighlightArea) {
            const int type_flag=event.source_fields[4];
            result.highlights.erase(std::remove_if(result.highlights.begin(),result.highlights.end(),[&](const auto& h){return !h.unit_target && h.coordinate==event.coordinate && h.area_end==event.area_end && h.type_flag==type_flag;}),result.highlights.end());
            result.highlights.push_back({false,0,event.coordinate,event.area_end,true,type_flag});
            continue;
        }
        if(event.kind==BattleDynamicMapEventKind::Turn || event.kind==BattleDynamicMapEventKind::Action) {
            const auto targets=event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(result.battle.units,event.data_id):matching_battlefield_index(result.battle.units,event.battlefield_index);
            if(targets.size()!=1){append_diagnostic(result.diagnostic,L"4F/50 目标未唯一匹配当前棋盘，本条跳过，后续继续推演");continue;}
            auto& unit=result.battle.units[targets[0]];const auto& f=event.source_fields;
            if(event.kind==BattleDynamicMapEventKind::Action){
                if(f[1]>=0)result.unit_details[unit.editor_cache_id].action=f[1];
                continue;
            }
            if(f[1]==-1){
                unit.direction=BattleTurnResult(unit.direction,f[2],false,0,false);
                if(f[2]<0)append_diagnostic(result.diagnostic,L"4F 无参照且为默认方向：保留现有朝向");
                continue;
            }
            const auto reference=f[1]>=1028?matching_battlefield_index(result.battle.units,f[1]-1028):matching_units(result.battle.units,f[1]);
            if(reference.size()!=1){append_diagnostic(result.diagnostic,L"4F 参照武将未唯一匹配，本条跳过");continue;}
            const int desired=BattleFacingDirection(unit.position,result.battle.units[reference[0]].position,unit.direction,targets[0]==reference[0]);
            unit.direction=BattleTurnResult(unit.direction,f[2],true,desired,f[3]!=0);
            continue;
        }
        if (event.kind == BattleDynamicMapEventKind::Retreat || event.kind == BattleDynamicMapEventKind::StatusChange ||
            event.kind == BattleDynamicMapEventKind::PolicyChange ||
            event.kind == BattleDynamicMapEventKind::HighlightUnit) {
            std::vector<std::size_t> targets;
            if(event.target_kind==BattleDynamicUnitTargetKind::AreaAndFaction) {
                for(std::size_t i=0;i<result.battle.units.size();++i) {
                    const auto& u=result.battle.units[i];
                    if(u.fields_decoded && !u.editor_pending_delete && !ccz::preview::is_battle_deployment_slot_marker(u) && inside_area(u,event)) targets.push_back(i);
                }
            } else targets=event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(result.battle.units,event.data_id):matching_battlefield_index(result.battle.units,event.battlefield_index);
            if(event.target_kind!=BattleDynamicUnitTargetKind::AreaAndFaction && targets.size()!=1) {append_diagnostic(result.diagnostic,L"动态状态目标未唯一匹配，本条跳过");continue;}
            for(const auto i:targets) {
                const auto& u=result.battle.units[i];const auto& f=event.source_fields;
                if(event.kind==BattleDynamicMapEventKind::HighlightUnit) {
                    result.highlights.erase(std::remove_if(result.highlights.begin(),result.highlights.end(),[](const auto& h){return h.unit_target;}),result.highlights.end());
                    result.highlights.push_back({true,u.editor_cache_id,u.position,u.position,true});continue;
                }
                auto& details=result.unit_details[u.editor_cache_id];
                if(event.kind==BattleDynamicMapEventKind::Retreat) {
                    result.battle.units[i].hidden=true;
                    details.retreat_kind=f[7];continue;
                }
                if(event.kind==BattleDynamicMapEventKind::PolicyChange) {
                    details.policy=f[7];details.policy_target=f[8];details.policy_coordinate={f[9],f[10]};
                    auto& projectedUnit=result.battle.units[i];
                    projectedUnit.ai_policy=f[7];projectedUnit.target=f[8];
                    projectedUnit.target_x=f[9];projectedUnit.target_y=f[10];
                } else {
                    if(f[8]>=0&&f[9]>=0) details.attribute_changes[f[8]]=f[9];
                    if(f[10]>=0) {
                        const int bits=f[10]&126;
                        if(f[10]&128) {details.removed_status_bits|=bits;details.added_status_bits&=~bits;}
                        else {details.added_status_bits|=bits;details.removed_status_bits&=~bits;}
                    }
                    details.hp_change-=f[11];details.mp_change-=f[12];
                }
            }
            continue;
        }
        if (event.kind == BattleDynamicMapEventKind::HiddenAppear ||
            event.kind == BattleDynamicMapEventKind::Disappear) {
            const bool hidden =
                event.kind == BattleDynamicMapEventKind::Disappear;
            if (event.target_kind ==
                BattleDynamicUnitTargetKind::AreaAndFaction) {
                for (auto& unit : result.battle.units) {
                    if (unit.fields_decoded && !unit.editor_pending_delete &&
                        !ccz::preview::is_battle_deployment_slot_marker(unit) &&
                        inside_area(unit, event)) {
                        unit.hidden = hidden;
                    }
                }
                continue;
            }
            const auto matches = event.target_kind ==
                    BattleDynamicUnitTargetKind::DataId
                ? matching_units(result.battle.units, event.data_id)
                : matching_battlefield_index(result.battle.units,
                                             event.battlefield_index);
            if (matches.size() != 1) {
                append_diagnostic(result.diagnostic,
                    L"显隐投影中的目标不再唯一，后续操作未应用");
                break;
            }
            auto& unit=result.battle.units[matches.front()];
            unit.hidden = hidden;
            if(!hidden){const auto details=result.unit_details.find(unit.editor_cache_id);if(details!=result.unit_details.end())details->second.retreat_kind=-1;}
            continue;
        }
        const auto matches = event.target_kind==BattleDynamicUnitTargetKind::DataId?matching_units(result.battle.units,event.data_id):matching_battlefield_index(result.battle.units,event.battlefield_index);
        if (matches.size() != 1) {
            append_diagnostic(result.diagnostic,
                L"动态投影中的单位身份不再唯一，后续操作未应用");
            break;
        }
        auto& unit = result.battle.units[matches.front()];
        unit.position = event.coordinate;
        result.position_origins[unit.editor_cache_id]={event.instruction_id,event.runtime_handle,event.logical_key,event.source_fields};
        if (event.direction >= 0) unit.direction = event.direction;
        if (event.kind == BattleDynamicMapEventKind::Revive) {
            unit.hidden = false;
            const auto details=result.unit_details.find(unit.editor_cache_id);
            if(details!=result.unit_details.end())details->second.retreat_kind=-1;
        }
        result.labels.erase(std::remove_if(result.labels.begin(),
            result.labels.end(), [&matches](const BattleDynamicMapLabel& label) {
                return label.unit_index == matches.front();
            }), result.labels.end());
        result.labels.push_back({
            event.kind == BattleDynamicMapEventKind::Revive,
            unit.data_id, event.coordinate, matches.front()});
    }
    return result;
}

}  // namespace ccz::editor_preview
