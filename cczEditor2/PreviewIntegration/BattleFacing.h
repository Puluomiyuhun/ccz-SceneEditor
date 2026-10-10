#pragma once
#include "BattleMapOperations.h"
#include <cstdlib>
namespace ccz::editor_preview {
inline bool IsRegularBattlePerson(int value) {
    return (value>=0 && value<=1023) || (value>=1028 && value<=1277);
}
inline int BattleFacingDirection(const ccz::preview::BattleCoord& a,
    const ccz::preview::BattleCoord& b,int previous,bool same_identity=false) {
    // Main engine 4351DA: same identity keeps direction; diagonal ties use X.
    if(same_identity)return previous;
    const auto dx=static_cast<long long>(b.x)-a.x,dy=static_cast<long long>(b.y)-a.y;
    if(dx==0 && dy==0)return 1; // Native equal-Y/equal-X branch at 4357DD.
    if(std::llabs(dy)>std::llabs(dx))return dy<0?0:2;
    return dx<0?3:1;
}
inline int BattleTurnResult(int previous,int specified,bool has_reference,
    int reference_direction,bool reverse) {
    // 457428: reference takes precedence over specified direction. The native
    // "不转向" flag reverses a newly computed reference-facing direction.
    if(!has_reference)return specified>=0?specified:previous;
    if(reference_direction==previous)return previous; // native early return
    return reverse?(reference_direction+2)%4:reference_direction;
}
inline int BattlePersonForUnit(const ccz::preview::BattleUnit& unit) {
    // Prefer explicit battlefield identity when Data identity is not decoded.
    return unit.data_id>=0 && unit.data_id<=1023 ? unit.data_id :
        unit.battlefield_index>=0 && unit.battlefield_index<=249 ? 1028+unit.battlefield_index:-1;
}
inline bool BuildBattleMutualFacing(const std::vector<ccz::preview::BattleUnit>& units,
    int width,int height,std::vector<BattleMapOperationInstruction>& commands,
    std::wstring& diagnostic) {
    commands.clear();diagnostic.clear();
    if(units.size()!=2 || units[0].editor_cache_id==units[1].editor_cache_id ||
        units[0].position==units[1].position) {
        diagnostic=L"武将相对需要选中两个不同且不在同格的角色";return false;
    }
    const int a=BattlePersonForUnit(units[0]),b=BattlePersonForUnit(units[1]);
    if(a==b || !IsRegularBattlePerson(a) || !IsRegularBattlePerson(b)) {
        diagnostic=L"武将相对需要两个可明确匹配的角色编号";return false;
    }
    BattleMapOperationInstruction first,second;
    if(!BuildBattleCommandInstruction({0x4f,{a,b,-1,0,1,0}},width,height,first,diagnostic) ||
       !BuildBattleCommandInstruction({0x4f,{b,a,-1,0,0,1}},width,height,second,diagnostic))return false;
    commands={std::move(first),std::move(second)};return true;
}
inline const wchar_t* BattleActionName(int action) {
    static const wchar_t* names[]={L"静止",L"举起武器",L"防御",L"受攻击",L"虚弱",L"攻击预备",L"攻击",L"二次攻击",L"慢速转圈",L"喘气",L"晕倒",L"快速转圈",L"中速转圈"};
    return action>=0 && action<=12?names[action]:L"无";
}
} // namespace
