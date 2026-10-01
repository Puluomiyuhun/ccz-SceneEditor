#pragma once

#include <ccz/preview/battle_preview.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

enum class BattleMapToolMode {
    None,
    MovePickUnit,
    MovePickTarget,
    RevivePickCharacter,
    RevivePickTarget,
    ObstaclePlace,
};

struct BattleMapOperationInstruction {
    std::uint16_t instruction_id{};
    std::vector<std::int32_t> int_data;
    // Retained with the value object so the native batch boundary can verify
    // coordinates again instead of trusting only the UI's first validation.
    int map_width{};
    int map_height{};
};

struct BattleObstacleDraft {
    int type{};
    ccz::preview::BattleCoord coordinate;
};

bool BuildBattleMoveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int raw_direction,
    int map_width, int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic);

bool BuildBattleReviveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic);

bool BuildBattleObstacleInstruction(int type,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic);

bool ValidateBattleMapOperationInstruction(
    const BattleMapOperationInstruction& instruction,
    std::wstring& diagnostic);

class BattleObstacleDraftQueue {
public:
    bool Add(int type, const ccz::preview::BattleCoord& coordinate,
        int map_width, int map_height);
    const std::vector<BattleObstacleDraft>& items() const noexcept {
        return items_;
    }
    bool empty() const noexcept { return items_.empty(); }
    void Clear() noexcept { items_.clear(); }

private:
    std::vector<BattleObstacleDraft> items_;
};

}  // namespace ccz::editor_preview
