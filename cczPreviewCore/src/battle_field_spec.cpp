#include <ccz/preview/battle_field_spec.h>

namespace ccz::preview {
namespace {

const BattleFieldSpec kSpecs[] = {
    {0x46,
     11,
     20,
     {0x2, 0x26, 0x4, 0x4, 0x2b, 0x3e, 0x45, 0x7, 0x2, 0x4, 0x4},
     {BattleFieldRole::DataId, BattleFieldRole::Hidden, BattleFieldRole::X,
      BattleFieldRole::Y, BattleFieldRole::Direction, BattleFieldRole::Level,
      BattleFieldRole::UnitType, BattleFieldRole::AiPolicy,
      BattleFieldRole::TargetDataId, BattleFieldRole::TargetX,
      BattleFieldRole::TargetY},
     true},
    {0x47,
     12,
     80,
     {0x2, 0x26, 0x26, 0x4, 0x4, 0x2b, 0x3e, 0x45, 0x7, 0x2, 0x4, 0x4},
     {BattleFieldRole::DataId, BattleFieldRole::Reinforcement,
      BattleFieldRole::Hidden, BattleFieldRole::X, BattleFieldRole::Y,
      BattleFieldRole::Direction, BattleFieldRole::Level,
      BattleFieldRole::UnitType, BattleFieldRole::AiPolicy,
      BattleFieldRole::TargetDataId, BattleFieldRole::TargetX,
      BattleFieldRole::TargetY},
     true},
    {0x4b,
     5,
     0,
     {0x4, 0x4, 0x4, 0x2b, 0x26},
     {BattleFieldRole::DeploymentIndex, BattleFieldRole::X, BattleFieldRole::Y,
      BattleFieldRole::Direction, BattleFieldRole::Hidden},
     true},
};

}  // namespace

const BattleFieldSpec* get_battle_field_spec(std::uint16_t instruction_id) {
    for (const BattleFieldSpec& spec : kSpecs) {
        if (spec.instruction_id == instruction_id) {
            return &spec;
        }
    }
    return nullptr;
}

}  // namespace ccz::preview
