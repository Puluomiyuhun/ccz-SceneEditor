#pragma once

#include <cstdint>

namespace ccz::editor_preview {

enum class BattleBatchParameter : std::uint32_t {
    Direction = 1u << 0,
    Hidden = 1u << 1,
    Level = 1u << 2,
    UnitType = 1u << 3,
    Reinforcement = 1u << 4,
    AiPolicy = 1u << 5,
    Target = 1u << 6,
};

using BattleBatchParameterMask = std::uint32_t;

constexpr BattleBatchParameterMask batch_parameter_bit(
    BattleBatchParameter parameter) noexcept {
    return static_cast<BattleBatchParameterMask>(parameter);
}

enum class BattleBatchWarningChoice {
    OverwriteAll,
    OnlyUniform,
    Cancel,
};

struct BattleBatchParameterSelection {
    bool proceed{};
    BattleBatchParameterMask checked{};
};

constexpr BattleBatchParameterSelection choose_batch_parameters(
    BattleBatchWarningChoice choice,
    BattleBatchParameterMask supported,
    BattleBatchParameterMask differing) noexcept {
    switch (choice) {
    case BattleBatchWarningChoice::OverwriteAll:
        return {true, supported};
    case BattleBatchWarningChoice::OnlyUniform:
        return {true, supported & ~differing};
    case BattleBatchWarningChoice::Cancel:
    default:
        return {false, 0};
    }
}

}  // namespace ccz::editor_preview
