#include "test_framework.h"

#include <PreviewIntegration/PlaybackInterpolation.h>

namespace {

using ccz::editor_preview::PlaybackPose;
using ccz::editor_preview::PlaybackPoint;
using ccz::editor_preview::sample_playback_pose;

}  // namespace

CCZ_TEST(dynamic_scene_interpolation_uses_logical_endpoints) {
    const PlaybackPose from{{10, 20}, 0, 3};
    const PlaybackPose to{{30, 60}, 2, 7};
    const PlaybackPose halfway = sample_playback_pose(from, to, 0.5);
    CCZ_REQUIRE_EQ(halfway.position.x, 20);
    CCZ_REQUIRE_EQ(halfway.position.y, 40);
}

CCZ_TEST(dynamic_scene_interpolation_keeps_direction_until_arrival) {
    const PlaybackPose from{{10, 20}, 0, 3};
    const PlaybackPose to{{30, 60}, 2, 7};
    CCZ_REQUIRE_EQ(sample_playback_pose(from, to, 0.99).direction, 0);
    CCZ_REQUIRE_EQ(sample_playback_pose(from, to, 1.0).direction, 2);
}

CCZ_TEST(dynamic_scene_interpolation_clamps_progress_without_writeback) {
    const PlaybackPose from{{10, 20}, 0, 3};
    const PlaybackPose to{{30, 60}, 2, 7};
    const PlaybackPose sampled = sample_playback_pose(from, to, 4.0);
    CCZ_REQUIRE_EQ(sampled.position.x, to.position.x);
    CCZ_REQUIRE_EQ(sampled.position.y, to.position.y);
    CCZ_REQUIRE_EQ(sampled.direction, to.direction);
    CCZ_REQUIRE_EQ(sampled.action, to.action);
    // The function returns a value copy; from/to remain untouched by design.
    CCZ_REQUIRE_EQ(from.position.x, 10);
    CCZ_REQUIRE_EQ(to.position.x, 30);
}
