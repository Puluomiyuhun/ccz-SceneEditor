#pragma once
#include <ccz/preview/battle_preview.h>
#include <map>
#include <set>
#include <string>
namespace ccz::editor_preview {
using BattleOwnUnitBindings = std::map<int,int>;
inline bool ApplyBattleOwnUnitBindings(ccz::preview::BattlePreviewState& board,
    const BattleOwnUnitBindings& bindings, std::wstring& diagnostic) {
    if (bindings.empty()) { diagnostic.clear(); return true; }
    auto candidate=board;
    std::set<int> used;
    for(const auto& unit:board.units)if(unit.fields_decoded&&!unit.editor_pending_delete&&unit.data_id>=0)used.insert(unit.data_id);
    for(const auto& binding:bindings) {
        std::size_t index=0,count=0;
        for(std::size_t i=0;i<board.units.size();++i){const auto& u=board.units[i];if(u.source_instruction==0x4b&&u.deployment_index==binding.first&&u.fields_decoded&&!u.editor_pending_delete&&!u.editor_staged_new){index=i;++count;}}
        if(count!=1||board.units[index].data_id>=0||binding.second<0||binding.second>1023||!used.insert(binding.second).second){
            diagnostic=L"我军槽位绑定失效或 Data 号重复，请重新调整绑定";return false;
        }
        candidate.units[index].data_id=binding.second;
    }
    board=std::move(candidate); diagnostic.clear();return true;
}
}
