#pragma once

#include <ccz/preview/dialogue_model.h>
#include <ccz/preview/execution_model.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ccz::preview {

struct PreviewState {
    DialogueState dialogue;
    SimulationEnvironment simulated;
    std::uint16_t face_state{};
    bool face_state_known{};
    // Section-local rename state.  The editor engine encodes a rename as a
    // 0x77 assignment to integer variable 4054 followed immediately by a
    // 0x72 information command.  Keep the map in the canonical replay state
    // so both dialogue text and the editor-owned frame see the same result.
    std::map<std::int32_t, std::wstring> data_name_overrides;
    std::optional<std::int32_t> pending_rename_target;
};

enum class ReplayStopReason : std::uint8_t {
    ReachedTarget,
    ReachedEnd,
    WaitingForPredicate,
    WaitingForChoice,
    UnsupportedCondition,
    TargetNotFound,
    // Source compatibility for the pre-Task-3 bridge and its condition
    // button.  Canonical replay uses WaitingForPredicate/WaitingForChoice.
    WaitingForCondition = WaitingForPredicate,
};

struct ReplayResult {
    PreviewState state;
    ReplayStopReason reason{ReplayStopReason::TargetNotFound};
    LogicalNodeKey waiting_node;
    LogicalNodeKey last_applied_node;
    std::wstring diagnostic;
};

void apply_dialogue_command(
    PreviewState& state,
    const DialogueCommandView& command,
    const DialogueProfile& profile,
    std::wstring* diagnostic = nullptr,
    bool use_last_embedded_dialogue = false);

ReplayResult replay_section_to(
    const SectionExecutionModel& model,
    const LogicalNodeKey& target_node,
    const DialogueProfile& profile,
    const ReplayOptions& options);

}  // namespace ccz::preview
