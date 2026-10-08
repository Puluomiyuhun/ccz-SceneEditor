#pragma once

#include <ccz/preview/dialogue_replay.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

struct NativeReplayItem {
    LogicalNodeKey logical_key;
    // Runtime TreeView handle for this refresh generation.  Callers may use
    // it only after validating it against the current TreeView; it must not
    // be persisted across a tree rebuild.
    RuntimeNodeHandle runtime_handle{};
    DialogueCommandView command;
};

struct NativeReplayPlan {
    std::vector<NativeReplayItem> items;
    ReplayStopReason stop_reason{ReplayStopReason::TargetNotFound};
    bool target_reached{};
    bool complete{};
    std::wstring diagnostic;
    std::vector<LogicalNodeKey> unresolved_status_predicates;
};

NativeReplayPlan build_native_replay_plan(
    const SectionExecutionModel& model,
    const LogicalNodeKey& target_node,
    const DialogueProfile& profile,
    const ReplayOptions& options);

// Build all native scene commands on the currently reachable path through a
// Section.  This is used for a special Scene's inherited base snapshot; it
// deliberately shares the same condition/Choice evaluation as target replay.
NativeReplayPlan build_native_replay_plan_to_end(
    const SectionExecutionModel& model,
    const DialogueProfile& profile,
    const ReplayOptions& options);

// Build the reachable Section path for S battlefield projection. In addition
// to the standard scene effects, this retains 0x55/0x58, while sharing the
// same condition and Choice evaluation.
NativeReplayPlan build_native_battle_map_plan_to_end(
    const SectionExecutionModel& model,
    const DialogueProfile& profile,
    const ReplayOptions& options,
    SimulationEnvironment* output_environment = nullptr);

// Build the reachable S battlefield command prefix through and including a
// selected instruction. This preserves condition/Choice traversal while
// retaining commands supported by the static S map projection.
NativeReplayPlan build_native_battle_map_plan_through_target(
    const SectionExecutionModel& model,
    const LogicalNodeKey& target_node,
    const DialogueProfile& profile,
    const ReplayOptions& options,
    SimulationEnvironment* output_environment = nullptr);

}  // namespace ccz::preview
