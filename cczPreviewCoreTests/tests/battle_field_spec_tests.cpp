#include "test_framework.h"

#include <ccz/preview/battle_field_spec.h>

#include <cstdint>
#include <vector>

namespace {

using ccz::preview::BattleFieldRole;
using ccz::preview::BattleFieldSpec;

void require_spec(std::uint16_t id, std::size_t stride, std::size_t capacity,
                  std::vector<std::uint16_t> types,
                  std::vector<BattleFieldRole> roles) {
    const BattleFieldSpec* spec = ccz::preview::get_battle_field_spec(id);
    CCZ_REQUIRE(spec != nullptr);
    CCZ_REQUIRE_EQ(spec->instruction_id, id);
    CCZ_REQUIRE_EQ(spec->record_stride, stride);
    CCZ_REQUIRE_EQ(spec->record_capacity, capacity);
    CCZ_REQUIRE(spec->semantic_complete);
    CCZ_REQUIRE_EQ(spec->field_types, types);
    CCZ_REQUIRE_EQ(spec->field_roles, roles);
}

}  // namespace

CCZ_TEST(battle_field_spec_matches_editor_source_rows) {
    require_spec(0x46, 11, 20,
                 {0x2, 0x26, 0x4, 0x4, 0x2b, 0x3e, 0x45, 0x7, 0x2,
                  0x4, 0x4},
                 {BattleFieldRole::DataId, BattleFieldRole::Hidden,
                  BattleFieldRole::X, BattleFieldRole::Y,
                  BattleFieldRole::Direction, BattleFieldRole::Level,
                  BattleFieldRole::UnitType, BattleFieldRole::AiPolicy,
                  BattleFieldRole::TargetDataId, BattleFieldRole::TargetX,
                  BattleFieldRole::TargetY});
    require_spec(0x47, 12, 80,
                 {0x2, 0x26, 0x26, 0x4, 0x4, 0x2b, 0x3e, 0x45, 0x7,
                  0x2, 0x4, 0x4},
                 {BattleFieldRole::DataId, BattleFieldRole::Reinforcement,
                  BattleFieldRole::Hidden, BattleFieldRole::X,
                  BattleFieldRole::Y, BattleFieldRole::Direction,
                  BattleFieldRole::Level, BattleFieldRole::UnitType,
                  BattleFieldRole::AiPolicy, BattleFieldRole::TargetDataId,
                  BattleFieldRole::TargetX, BattleFieldRole::TargetY});
    require_spec(0x4b, 5, 0, {0x4, 0x4, 0x4, 0x2b, 0x26},
                 {BattleFieldRole::DeploymentIndex, BattleFieldRole::X,
                  BattleFieldRole::Y, BattleFieldRole::Direction,
                  BattleFieldRole::Hidden});
    const BattleFieldSpec* our_deployment =
        ccz::preview::get_battle_field_spec(0x4b);
    CCZ_REQUIRE(our_deployment != nullptr);
    CCZ_REQUIRE_EQ(our_deployment->field_roles[0],
                   BattleFieldRole::DeploymentIndex);
}

CCZ_TEST(battle_field_spec_rejects_unknown_instruction) {
    CCZ_REQUIRE(ccz::preview::get_battle_field_spec(0x45) == nullptr);
}
