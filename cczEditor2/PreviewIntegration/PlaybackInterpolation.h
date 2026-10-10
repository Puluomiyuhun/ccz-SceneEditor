#pragma once

#include <algorithm>
#include <cmath>

namespace ccz::editor_preview {

struct PlaybackPoint {
    int x{};
    int y{};
};

struct PlaybackPose {
    PlaybackPoint position;
    int direction{};
    int action{};
};

// Sample a temporary render pose.  This helper is deliberately value-only:
// it never touches PositionWritebackSource or any editor tree item.  The
// direction/action switch at arrival preserves the editor's existing rule
// that movement interpolation changes only coordinates until the segment is
// complete.
inline PlaybackPose sample_playback_pose(const PlaybackPose& from,
                                         const PlaybackPose& to,
                                         double progress) noexcept {
    const double t = (std::max)(0.0, (std::min)(1.0, progress));
    PlaybackPose result = from;
    result.position.x = static_cast<int>(std::lround(
        static_cast<double>(from.position.x) +
        (static_cast<double>(to.position.x - from.position.x) * t)));
    result.position.y = static_cast<int>(std::lround(
        static_cast<double>(from.position.y) +
        (static_cast<double>(to.position.y - from.position.y) * t)));
    if (t >= 1.0) {
        result.direction = to.direction;
        result.action = to.action;
    }
    return result;
}

}  // namespace ccz::editor_preview
