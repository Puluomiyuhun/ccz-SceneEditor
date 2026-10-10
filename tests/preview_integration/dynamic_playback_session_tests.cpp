#include "test_framework.h"

#include <PreviewIntegration/DynamicPlaybackSession.h>

#include <ccz/preview/playback_timeline.h>

#include <cstddef>
#include <vector>

namespace {

using namespace ccz::editor_preview;
using namespace ccz::preview;

PlaybackTimeline timeline() {
    PlaybackTimeline value;
    value.complete = true;
    value.target_index = 1;
    value.events = {
        {L"instant", 0, {}, PlaybackSegmentKind::Instant, 0},
        {L"move", 0, {}, PlaybackSegmentKind::CharacterMove, 1},
    };
    return value;
}

}  // namespace

CCZ_TEST(dynamic_playback_session_advances_instant_and_timed_events) {
    DynamicPlaybackSession session;
    CCZ_REQUIRE(session.Start(timeline(), PlaybackRange::StartToTarget, 120));
    session.SetSpeed(1.0);
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Playing);

    const PlaybackTick instant = session.Tick(0);
    CCZ_REQUIRE_EQ(instant.completed_events.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(instant.completed_events.front(), std::size_t{0});
    CCZ_REQUIRE_EQ(instant.event_index, std::size_t{1});
    CCZ_REQUIRE_EQ(instant.progress, 0.0);

    const PlaybackTick finished = session.Tick(120);
    CCZ_REQUIRE_EQ(finished.completed_events.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(finished.completed_events.front(), std::size_t{1});
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Finished);
}

CCZ_TEST(dynamic_playback_session_uses_one_x_default_speed) {
    DynamicPlaybackSession session;
    CCZ_REQUIRE_EQ(session.speed(), 1.0);
}

CCZ_TEST(dynamic_playback_session_default_timed_event_last_1200ms) {
    DynamicPlaybackSession session;
    PlaybackTimeline value = timeline();
    value.events.erase(value.events.begin());
    value.target_index = 0;
    CCZ_REQUIRE(session.Start(value, PlaybackRange::TargetToEnd));

    const PlaybackTick before_end = session.Tick(1199);
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Playing);
    CCZ_REQUIRE(before_end.progress < 1.0);

    session.Tick(1);
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Finished);
}

CCZ_TEST(dynamic_playback_session_pause_freezes_clock_and_resume_continues) {
    DynamicPlaybackSession session;
    PlaybackTimeline value = timeline();
    value.events.erase(value.events.begin());
    value.target_index = 0;
    CCZ_REQUIRE(session.Start(value, PlaybackRange::TargetToEnd, 120));
    session.SetSpeed(1.0);

    const PlaybackTick half = session.Tick(60);
    CCZ_REQUIRE_EQ(half.event_index, std::size_t{0});
    CCZ_REQUIRE_EQ(half.progress, 0.5);
    session.Pause();
    const PlaybackTick frozen = session.Tick(60);
    CCZ_REQUIRE_EQ(frozen.progress, 0.5);
    session.Resume();
    session.Tick(60);
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Finished);
}

CCZ_TEST(dynamic_playback_session_speed_changes_duration_only) {
    DynamicPlaybackSession session;
    PlaybackTimeline value = timeline();
    value.events.erase(value.events.begin());
    value.target_index = 0;
    CCZ_REQUIRE(session.Start(value, PlaybackRange::TargetToEnd, 120));
    session.SetSpeed(1.0);
    session.SetSpeed(2.0);

    const PlaybackTick tick = session.Tick(60);
    CCZ_REQUIRE_EQ(tick.completed_events.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Finished);
}

CCZ_TEST(dynamic_playback_session_step_backward_reopens_last_event) {
    DynamicPlaybackSession session;
    PlaybackTimeline value = timeline();
    value.events.erase(value.events.begin());
    value.target_index = 0;
    CCZ_REQUIRE(session.Start(value, PlaybackRange::TargetToEnd, 120));
    session.SetSpeed(1.0);
    session.Tick(120);
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Finished);

    CCZ_REQUIRE(session.StepBackward());
    CCZ_REQUIRE_EQ(session.state(), DynamicPlaybackState::Paused);
    CCZ_REQUIRE_EQ(session.current_event_index(), std::size_t{0});
    CCZ_REQUIRE_EQ(session.progress(), 0.0);
}

CCZ_TEST(dynamic_playback_session_step_backward_crosses_dialogue_segment_boundary) {
    DynamicPlaybackSession session;
    PlaybackTimeline value;
    value.complete = true;
    value.target_index = 1;
    value.range_begin = 1;
    value.range_end = 4;
    value.events = {
        {L"prefix", 0, {}, PlaybackSegmentKind::Instant, 0},
        {L"dialogue", 0, {}, PlaybackSegmentKind::Hold, 1, 0, 2},
        {L"dialogue", 0, {}, PlaybackSegmentKind::Hold, 1, 1, 2},
        {L"next", 0, {}, PlaybackSegmentKind::Instant, 0},
    };
    CCZ_REQUIRE(session.Start(value, PlaybackRange::TargetToEnd));
    CCZ_REQUIRE_EQ(session.current_event_index(), std::size_t{1});

    CCZ_REQUIRE(session.StepForward());
    CCZ_REQUIRE_EQ(session.current_event_index(), std::size_t{2});
    CCZ_REQUIRE(session.StepBackward());
    CCZ_REQUIRE_EQ(session.current_event_index(), std::size_t{1});

    // At the first segment of the selected range there is no artificial
    // stall: the UI layer can now rewind to the preceding instruction (or
    // report that the range origin has been reached).
    CCZ_REQUIRE(!session.StepBackward());
    CCZ_REQUIRE_EQ(session.current_event_index(), std::size_t{1});
}
