#include "PreviewRefreshStateMachine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using State = ccz::editor_preview::PreviewRefreshStateMachine;

struct Fixture {
    State state;
    std::vector<std::uint64_t> executed;
    std::vector<bool> postResults;
    std::size_t postIndex = 0;
    int postAttempts = 0;
    int depth = 0;
    int maxDepth = 0;
    std::uint64_t nextKey = 100;
    int reentrantRequests = 0;
    std::vector<std::uint64_t> burstKeys;
    bool idle = false;
    int sessionFrame = 1;

    State::Request request(std::uint64_t key)
    {
        return {key, state.generation()};
    }

    bool post()
    {
        ++postAttempts;
        if (postIndex >= postResults.size()) return true;
        return postResults[postIndex++];
    }

    void submit(std::uint64_t key)
    {
        state.Submit(
            request(key),
            [this](const State::Request& current) { execute(current); },
            [this]() { return post(); });
    }

    void execute(const State::Request& current)
    {
        ++depth;
        if (depth > maxDepth) maxDepth = depth;
        executed.push_back(current.key);
        if (idle) {
            // Root/document selection is an accepted idle request, not an
            // instruction replay.  The callback still consumes it exactly
            // once, which is the routing boundary under test.
            idle = false;
            sessionFrame = 0;
        }
        if (reentrantRequests > 0) {
            --reentrantRequests;
            submit(nextKey++);
        }
        for (const std::uint64_t key : burstKeys) submit(key);
        burstKeys.clear();
        --depth;
    }
};

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "PREVIEW_REFRESH_STATE_MACHINE_FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_root_idle_request()
{
    Fixture fixture;
    fixture.idle = true;
    fixture.submit(1);
    expect(fixture.executed == std::vector<std::uint64_t>{1},
        "root/idle request was not consumed once");
    expect(fixture.sessionFrame == 0,
        "root/idle request did not clear the prior session frame");
    expect(!fixture.state.active() && !fixture.state.posted(),
        "root/idle request left the state active or posted");
}

void test_latest_coalescing_and_message_lifecycle()
{
    Fixture fixture;
    fixture.postResults = { true };
    fixture.burstKeys = { 11, 12 };
    fixture.submit(10);
    expect(fixture.executed == std::vector<std::uint64_t>{10},
        "reentrant requests were not coalesced before the posted message");
    expect(fixture.postAttempts == 1 && fixture.state.posted(),
        "successful coalescing post was not recorded");

    fixture.state.OnPostedMessage(
        [&fixture](const State::Request& current) { fixture.execute(current); });
    expect(fixture.executed == std::vector<std::uint64_t>{10, 12},
        "posted message did not consume the latest request");
    expect(!fixture.state.active() && !fixture.state.posted(),
        "posted message left the state active or posted");
}

void test_continuous_post_failures_are_iterative()
{
    Fixture fixture;
    fixture.postResults = { false, false };
    fixture.reentrantRequests = 2;
    fixture.nextKey = 21;
    fixture.submit(20);
    expect(fixture.executed == std::vector<std::uint64_t>{20, 21, 22},
        "continuous PostMessage failures did not drain the latest request");
    expect(fixture.postAttempts == 2 && fixture.maxDepth == 1,
        "retry path was recursive or did not retry each reentrant request");
    expect(!fixture.state.active() && !fixture.state.posted() &&
            !fixture.state.retry_pending(),
        "continuous failure path left retry state behind");
}

void test_generation_invalidation_drops_pending()
{
    Fixture fixture;
    const State::Request oldRequest = fixture.request(30);
    fixture.state.Submit(
        oldRequest,
        [&fixture, oldRequest](const State::Request& current) {
            fixture.executed.push_back(current.key);
            fixture.state.Submit(
                {31, oldRequest.generation},
                [&fixture](const State::Request& pending) {
                    fixture.executed.push_back(pending.key);
                },
                []() { return true; });
            fixture.state.Invalidate();
        },
        []() { return true; });
    expect(fixture.executed == std::vector<std::uint64_t>{30},
        "TreeView/document generation invalidation executed stale pending work");
    expect(fixture.state.generation() != oldRequest.generation &&
            !fixture.state.active() && !fixture.state.posted(),
        "generation invalidation did not clear pending lifecycle state");
}

}  // namespace

int main()
{
    test_root_idle_request();
    test_latest_coalescing_and_message_lifecycle();
    test_continuous_post_failures_are_iterative();
    test_generation_invalidation_drops_pending();
    std::cout << "PREVIEW_REFRESH_STATE_MACHINE_PASS\n";
    return 0;
}
