#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ccz::preview {

// Field roles are based on cczEditor2's code_instruct rows and the matching
// native dialogs.  They describe only the per-record data, never grouping or
// writeback order.
enum class BattleFieldRole {
    DataId,
    DeploymentIndex,
    Reinforcement,
    Hidden,
    X,
    Y,
    Direction,
    Level,
    UnitType,
    AiPolicy,
    TargetDataId,
    TargetX,
    TargetY,
};

struct BattleFieldSpec {
    std::uint16_t instruction_id{};
    std::size_t record_stride{};
    // Zero denotes records whose count is determined by the current payload.
    std::size_t record_capacity{};
    std::vector<std::uint16_t> field_types;
    std::vector<BattleFieldRole> field_roles;
    bool semantic_complete{};
};

// cczEditor2View::EditData reads type 0x04 as a 4-byte scalar.  The fixed
// field types used by this specification are otherwise 2-byte scalars.  This
// is metadata validation for editor-normalized int_data, not raw EEX parsing.
constexpr std::size_t battle_field_type_byte_width(std::uint16_t type) {
    return type == 0x4 ? 4U : 2U;
}
static_assert(battle_field_type_byte_width(0x4) == 4U,
              "0x04 is the editor's 4-byte field type");
static_assert(battle_field_type_byte_width(0x2) == 2U,
              "non-0x04 fixed battle field types are 2-byte scalars");

const BattleFieldSpec* get_battle_field_spec(std::uint16_t instruction_id);

}  // namespace ccz::preview
