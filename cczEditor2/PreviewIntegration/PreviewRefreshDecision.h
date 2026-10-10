#pragma once

#include "PreviewSession.h"

namespace ccz::editor_preview {

// The native scene renderer and the value-owned PreviewSession have distinct
// lifetimes.  Keep the boundary decision explicit so an unchanged published
// frame cannot fall through to UpdatePreview/Invalidate, while invalid or
// idle captures still clear stale native state.
enum class PreviewRefreshAction {
    Repaint,
    SkipRepaint,
    ClearAndRepaint,
};

inline PreviewRefreshAction DecidePreviewRefreshAction(
    RefreshStatus status, bool frame_changed, bool capture_valid) noexcept {
    if (!capture_valid || status == RefreshStatus::Idle ||
        status == RefreshStatus::InvalidInput ||
        status == RefreshStatus::ReentrantRejected) {
        return PreviewRefreshAction::ClearAndRepaint;
    }
    if (status == RefreshStatus::Unchanged || !frame_changed) {
        return PreviewRefreshAction::SkipRepaint;
    }
    return PreviewRefreshAction::Repaint;
}

}  // namespace ccz::editor_preview
