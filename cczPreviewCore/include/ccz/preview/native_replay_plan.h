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

}  // namespace ccz::preview

