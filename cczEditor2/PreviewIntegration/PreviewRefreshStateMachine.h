#pragma once

#include <cstdint>

namespace ccz::editor_preview {

// UI-independent coalescing state for the editor's event-driven preview
// boundary.  The callbacks are supplied by the UI layer, so this helper has
// no window-control or PreviewSession dependency and can be exercised by a
// small executable fixture.
class PreviewRefreshStateMachine {
public:
    struct Request {
        std::uint64_t key{};
        std::uint64_t generation{};

        bool valid() const noexcept { return key != 0 && generation != 0; }
    };

    std::uint64_t generation() const noexcept { return generation_; }
    bool active() const noexcept { return active_; }
    bool posted() const noexcept { return posted_; }
    bool retry_pending() const noexcept { return retry_pending_; }

    void Invalidate() noexcept {
        pending_ = {};
        posted_ = false;
        retry_pending_ = false;
        ++generation_;
    }

    template <typename Execute, typename Post>
    void Submit(Request request, Execute execute, Post post)
    {
        if (!request.valid() || request.generation != generation_) return;
        pending_ = request;
        if (active_) {
            if (!posted_) {
                posted_ = true;
                if (!post()) {
                    posted_ = false;
                    retry_pending_ = true;
                }
            }
            return;
        }
        Pump(execute);
    }

    template <typename Execute>
    void OnPostedMessage(Execute execute)
    {
        posted_ = false;
        if (active_) {
            // A same-thread UI dispatch cannot normally arrive here, but
            // treating it as retryable keeps the state machine total in the
            // executable fixture as well.
            retry_pending_ = true;
            return;
        }
        Pump(execute);
    }

    template <typename Execute>
    void Pump(Execute execute)
    {
        if (active_) return;
        while (pending_.valid()) {
            const Request request = pending_;
            pending_ = {};
            active_ = true;
            execute(request);
            active_ = false;

            // A failed PostMessage during execute leaves the newest request
            // in pending_.  Loop, rather than recursively entering execute,
            // until the latest request has been consumed or invalidated.
            if (retry_pending_) {
                retry_pending_ = false;
                continue;
            }
            break;
        }
    }

private:
    Request pending_{};
    std::uint64_t generation_ = 1;
    bool active_ = false;
    bool posted_ = false;
    bool retry_pending_ = false;
};

}  // namespace ccz::editor_preview
