#pragma once

#include <ccz/preview/native_replay_plan.h>

#include <vector>

namespace ccz::editor_preview {

// Compose the completed appearance Section snapshot with the currently
// selected Free-R trigger Section.  A background command (0x27) in the
// trigger starts a new scene, so only the suffix from its last background is
// retained; otherwise the trigger is appended to the inherited snapshot.
ccz::preview::NativeReplayPlan ComposeFreeRReplayPlan(
    const ccz::preview::NativeReplayPlan& appearance_prefix,
    const ccz::preview::NativeReplayPlan& trigger_plan);

// Compose the completed appearance Section with all trigger-Scene Sections
// before the selected Section, then the selected Section's plan.  The vector
// is already in source order and is intentionally limited by the resolver to
// the current adjacent Free-R Scene pair.
ccz::preview::NativeReplayPlan ComposeFreeRReplayPlan(
    const ccz::preview::NativeReplayPlan& appearance_prefix,
    const std::vector<ccz::preview::NativeReplayPlan>& trigger_prefixes,
    const ccz::preview::NativeReplayPlan& trigger_plan);

}  // namespace ccz::editor_preview
