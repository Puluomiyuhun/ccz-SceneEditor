#pragma once

#include <ccz/preview/battle_preview.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccz::editor_preview {

enum class BattleMapToolMode {
    None,
    MovePickUnit,
    MovePickTarget,
    RevivePickTarget,
    ObstaclePlace,
    ObstacleAdjust,
    DynamicRoleAdjust,
    TurnDirection,
    TurnPickTarget,
    CommandPickPoint,
    CommandPickArea,
};

struct BattleMapOperationInstruction {
    std::uint16_t instruction_id{};
    std::vector<std::int32_t> int_data;
    // Retained with the value object so the native batch boundary can verify
    // coordinates again instead of trusting only the UI's first validation.
    int map_width{};
    int map_height{};
};

bool BuildBattleHiddenAppearBatch(const std::vector<ccz::preview::BattleUnit>& units,
    int width,int height,std::vector<BattleMapOperationInstruction>& commands,
    std::size_t& skipped,std::wstring& diagnostic);

// One shared field contract for construction, native insertion and undo.
inline std::size_t BattleMapInstructionFieldCount(std::uint16_t id) noexcept {
    switch(id) {
    case 0x32: case 0x55: case 0x4f:return 6;
    case 0x50:return 4;
    case 0x31: case 0x58:return 7;
    case 0x4d:return 13;
    case 0x4e:return 11;
    case 0x53:return 8;
    case 0x25: case 0x4c:return 3;
    case 0x26:return 5;
    case 0x5b:return 5;
    case 0x5c:return 1;
    case 0x6b:return 4;
    default:return 0;
    }
}

// Fields follow the native S dialogs exactly; no R instruction uses this path.
struct BattleCommandConfiguration {
    std::uint16_t instruction_id{};
    std::vector<std::int32_t> fields;
};
BattleCommandConfiguration DefaultBattleCommandConfiguration(std::uint16_t id);
bool BuildBattleCommandInstruction(const BattleCommandConfiguration& configuration,
    int map_width, int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic);

struct BattleRoleOperationSource {
    int data_id{-1};
    ccz::preview::BattleCoord origin;
    int direction{-1};
};

enum class BattleRoleOperationKind {
    MoveTo,
    ReviveTo,
};

struct BattleObstacleDefaults {
    int visibility{};
    int terrain{-1}; // -1: match fire/boat/Gate type
    int viewpoint{};
    int sound{};
};

// 0-2 use U_select; 3 is reserved; 4-127 are the Gate.e5 pairs.
inline bool IsBattleObstacleTypeSupported(int type) noexcept {
    return (type >= 0 && type <= 2) || (type >= 4 && type <= 127);
}
inline int BattleGateClosedImageIndex(int type) noexcept {
    return (type - 4) * 2;
}

struct BattleObstacleDraft {
    int type{};
    ccz::preview::BattleCoord coordinate;
    BattleObstacleDefaults parameters;
    std::uint64_t identity{};
};

bool BuildBattleMoveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int raw_direction,
    int map_width, int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic);

bool BuildBattleReviveToInstruction(int data_id,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic,
    std::optional<int> direction_override = std::nullopt);

bool BuildBattleRoleOperationBatch(BattleRoleOperationKind kind,
    const std::vector<BattleRoleOperationSource>& sources,
    std::size_t anchor_index,
    const ccz::preview::BattleCoord& anchor_target,
    int map_width, int map_height,
    std::optional<int> direction_override,
    std::vector<BattleMapOperationInstruction>& instructions,
    std::vector<ccz::preview::BattleCoord>& target_positions,
    std::wstring& diagnostic);

bool BuildBattleObstacleInstruction(int type,
    const ccz::preview::BattleCoord& coordinate, int map_width,
    int map_height, BattleMapOperationInstruction& output,
    std::wstring& diagnostic,
    const BattleObstacleDefaults& parameters = {});

bool ValidateBattleMapOperationInstruction(
    const BattleMapOperationInstruction& instruction,
    std::wstring& diagnostic);

class BattleObstacleDraftQueue {
public:
    bool Add(int type, const ccz::preview::BattleCoord& coordinate,
        int map_width, int map_height, const BattleObstacleDefaults& parameters = {});
    bool UpdateAt(const ccz::preview::BattleCoord& origin, const BattleObstacleDraft& replacement, int map_width, int map_height);
    bool RemoveAt(const ccz::preview::BattleCoord& coordinate);
    const std::vector<BattleObstacleDraft>& items() const noexcept {
        return items_;
    }
    bool empty() const noexcept { return items_.empty(); }
    void Clear() noexcept { items_.clear(); }

private:
    std::vector<BattleObstacleDraft> items_;
    std::uint64_t next_identity_{1};
};

}  // namespace ccz::editor_preview
