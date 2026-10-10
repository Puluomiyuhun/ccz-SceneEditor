#include "DynamicPlaybackSession.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ccz::editor_preview {
namespace {

double clamp_speed(double speed) noexcept {
    if (!std::isfinite(speed)) return 1.0;
    return (std::max)(0.25, (std::min)(8.0, speed));
}

}  // namespace

bool DynamicPlaybackSession::Start(
    const ccz::preview::PlaybackTimeline& timeline,
    ccz::preview::PlaybackRange range, std::int64_t base_duration_ms) {
    if (!timeline.complete || timeline.events.empty()) {
        Stop();
        return false;
    }
    timeline_ = timeline;
    if (timeline_.range_end == 0) {
        timeline_.range_begin = 0;
        timeline_.range_end = timeline_.events.size();
    }
    if (timeline_.range_begin >= timeline_.range_end ||
        timeline_.range_end > timeline_.events.size()) {
        Stop();
        return false;
    }
    range_ = range;
    base_duration_ms_ = (std::max<std::int64_t>)(1, base_duration_ms);
    current_index_ = timeline_.range_begin;
    current_elapsed_ms_ = 0;
    state_ = DynamicPlaybackState::Playing;
    return true;
}

void DynamicPlaybackSession::Pause() noexcept {
    if (state_ == DynamicPlaybackState::Playing) {
        state_ = DynamicPlaybackState::Paused;
    }
}

void DynamicPlaybackSession::Resume() noexcept {
    if (state_ == DynamicPlaybackState::Paused) {
        state_ = DynamicPlaybackState::Playing;
    }
}

void DynamicPlaybackSession::Stop() noexcept {
    timeline_ = {};
    range_ = ccz::preview::PlaybackRange::StartToTarget;
    state_ = DynamicPlaybackState::Stopped;
    current_index_ = 0;
    current_elapsed_ms_ = 0;
}

void DynamicPlaybackSession::SetSpeed(double speed) noexcept {
    speed_ = clamp_speed(speed);
}

double DynamicPlaybackSession::speed() const noexcept { return speed_; }

std::int64_t DynamicPlaybackSession::current_duration_ms() const noexcept {
    if (current_index_ >= timeline_.range_end ||
        current_index_ >= timeline_.events.size()) return 0;
    const auto& event = timeline_.events[current_index_];
    if (event.duration_units <= 0) return 0;
    const double duration = static_cast<double>(base_duration_ms_) *
                            static_cast<double>(event.duration_units) / speed_;
    return (std::max<std::int64_t>)(1,
        static_cast<std::int64_t>(std::llround(duration)));
}

PlaybackTick DynamicPlaybackSession::snapshot() const {
    PlaybackTick result;
    result.state = state_;
    result.event_index = current_index_;
    const std::int64_t duration = current_duration_ms();
    if (duration <= 0) {
        result.progress = 0.0;
    } else {
        result.progress = (std::max)(0.0, (std::min)(1.0,
            static_cast<double>(current_elapsed_ms_) /
                static_cast<double>(duration)));
    }
    return result;
}

PlaybackTick DynamicPlaybackSession::Tick(std::int64_t elapsed_ms) {
    PlaybackTick result = snapshot();
    if (state_ != DynamicPlaybackState::Playing) {
        return result;
    }

    std::int64_t remaining = (std::max<std::int64_t>)(0, elapsed_ms);
    while (current_index_ < timeline_.range_end) {
        const std::int64_t duration = current_duration_ms();
        if (duration <= 0) {
            result.completed_events.push_back(current_index_);
            ++current_index_;
            current_elapsed_ms_ = 0;
            continue;
        }

        const std::int64_t left = duration - current_elapsed_ms_;
        if (remaining < left) {
            current_elapsed_ms_ += remaining;
            remaining = 0;
            break;
        }

        remaining -= left;
        result.completed_events.push_back(current_index_);
        ++current_index_;
        current_elapsed_ms_ = 0;
        if (remaining == 0) {
            break;
        }
    }

    if (current_index_ >= timeline_.range_end) {
        state_ = DynamicPlaybackState::Finished;
        current_index_ = timeline_.range_end;
        current_elapsed_ms_ = 0;
    }
    std::vector<std::size_t> completed = std::move(result.completed_events);
    result = snapshot();
    // Preserve the list accumulated while snapshot() rebuilt the scalar view.
    // The finished cursor deliberately points one past the last event.
    result.completed_events = std::move(completed);
    return result;
}

bool DynamicPlaybackSession::StepForward() noexcept {
    if (state_ == DynamicPlaybackState::Stopped ||
        state_ == DynamicPlaybackState::Finished ||
        current_index_ >= timeline_.range_end) {
        return false;
    }
    ++current_index_;
    current_elapsed_ms_ = 0;
    state_ = current_index_ >= timeline_.range_end
                 ? DynamicPlaybackState::Finished
                 : DynamicPlaybackState::Paused;
    return true;
}

bool DynamicPlaybackSession::StepBackward() noexcept {
    if (timeline_.events.empty()) return false;
    if (state_ == DynamicPlaybackState::Stopped) return false;
    if (state_ == DynamicPlaybackState::Finished ||
        current_index_ >= timeline_.range_end) {
        current_index_ = timeline_.range_end - 1;
    } else if (current_index_ > timeline_.range_begin) {
        --current_index_;
    } else {
        return false;
    }
    current_elapsed_ms_ = 0;
    state_ = DynamicPlaybackState::Paused;
    return true;
}

DynamicPlaybackState DynamicPlaybackSession::state() const noexcept {
    return state_;
}

std::size_t DynamicPlaybackSession::current_event_index() const noexcept {
    return current_index_;
}

double DynamicPlaybackSession::progress() const noexcept {
    return snapshot().progress;
}

const ccz::preview::PlaybackEvent*
DynamicPlaybackSession::current_event() const noexcept {
    if (current_index_ >= timeline_.events.size()) return nullptr;
    return &timeline_.events[current_index_];
}

ccz::preview::PlaybackRange DynamicPlaybackSession::range() const noexcept {
    return range_;
}

}  // namespace ccz::editor_preview
