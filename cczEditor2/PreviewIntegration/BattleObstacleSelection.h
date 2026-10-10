#pragma once
#include "BattleMapOperations.h"
#include "BattleDynamicMapReplay.h"
#include <algorithm>

namespace ccz::editor_preview {
struct BattleObstacleSelectionItem {
    bool draft{};
    std::uint64_t identity{};
    BattleObstacleDraft value;
    BattleDynamicObstacle source;
    std::uint64_t document_id{}, tree_generation{}, content_epoch{};
};
struct BattleObstacleSelection {
    std::vector<BattleObstacleSelectionItem> items;
    bool enabled{}, gesture_active{}, moving{}, drag_started{};
    int press_x{},press_y{};
    std::uintptr_t section_handle{};
    std::optional<ccz::preview::BattleCoord> press_cell;
    std::optional<ccz::preview::BattleCoord> last_cell;
    ccz::preview::BattleCoord anchor{}, ghost_offset{};
    bool Contains(bool draft, std::uint64_t identity) const {
        return std::any_of(items.begin(), items.end(), [&](const auto& item) {
            return item.draft == draft && item.identity == identity;
        });
    }
    void Select(const BattleObstacleSelectionItem& item, bool toggle) {
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto& current) {
            return current.draft == item.draft && current.identity == item.identity;
        });
        if (found == items.end()) items.push_back(item);
        else if (toggle) items.erase(found);
    }
    void CancelGesture() { gesture_active=false; drag_started=false;press_cell.reset(); last_cell.reset(); moving=false; ghost_offset={}; }
};
}
