#pragma once

#include <ccz/preview/playback_timeline.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ccz::editor_preview {

enum class DynamicPlaybackState : std::uint8_t {
    Stopped,
    Playing,
    Paused,
    Finished,
};

struct PlaybackTick {
    DynamicPlaybackState state{DynamicPlaybackState::Stopped};
    std::size_t event_index{};
    double progress{};
    std::vector<std::size_t> completed_events;
};

class DynamicPlaybackSession {
public:
    bool Start(const ccz::preview::PlaybackTimeline& timeline,
               ccz::preview::PlaybackRange range,
               std::int64_t base_duration_ms = 1200);
    void Pause() noexcept;
    void Resume() noexcept;
    void Stop() noexcept;

    void SetSpeed(double speed) noexcept;
    double speed() const noexcept;

    PlaybackTick Tick(std::int64_t elapsed_ms);
    bool StepForward() noexcept;
    bool StepBackward() noexcept;

    DynamicPlaybackState state() const noexcept;
    std::size_t current_event_index() const noexcept;
    double progress() const noexcept;
    const ccz::preview::PlaybackEvent* current_event() const noexcept;
    ccz::preview::PlaybackRange range() const noexcept;

private:
    ccz::preview::PlaybackTimeline timeline_;
    ccz::preview::PlaybackRange range_ =
        ccz::preview::PlaybackRange::StartToTarget;
    DynamicPlaybackState state_ = DynamicPlaybackState::Stopped;
    std::size_t current_index_{};
    std::int64_t current_elapsed_ms_{};
    // At the default 1x speed this produces a 1200ms timed event.
    std::int64_t base_duration_ms_ = 1200;
    // The speed button cycles from the human-readable default 1x through the
    // existing selectable multipliers.
    double speed_ = 1.0;

    std::int64_t current_duration_ms() const noexcept;
    PlaybackTick snapshot() const;
};

}  // namespace ccz::editor_preview
