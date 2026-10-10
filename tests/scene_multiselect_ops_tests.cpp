#include <cassert>
#include <cstddef>
#include <vector>

#include "../cczEditor2/SceneMultiSelectOps.h"

using ccz::scene_preview::AlignNearLines;
using ccz::scene_preview::AlignToPrimaryAxis;
using ccz::scene_preview::DistributeAxes;
using ccz::scene_preview::PreciseDistributeAxis;
using ccz::scene_preview::CycleDirection;
using ccz::scene_preview::LayoutActor;
using ccz::scene_preview::ApplyBatchTransaction;

static void TestBatchTransactionRollsBackEarlierInsertions()
{
    std::vector<int> inserted;
    std::vector<int> rolled_back;
    const bool committed = ApplyBatchTransaction<int, int>(
        {1, 2, 3},
        [&](int value) {
            if (value == 2) return -1;
            inserted.push_back(value);
            return value;
        },
        [&](int handle) { rolled_back.push_back(handle); },
        [](int handle) { return handle >= 0; });

    assert(!committed);
    assert((inserted == std::vector<int>{1}));
    assert((rolled_back == std::vector<int>{1}));
}

static void TestAlignUsesSelectedActorsAndMajorityLine()
{
    std::vector<LayoutActor> actors = {
        {1, 10, 20},
        {2, 11, 25},
        {3, 10, 30},
        {4, 99, 77},
    };
    const std::vector<std::size_t> selected = {0, 1, 2};

    const auto result = AlignNearLines(actors, selected);

    assert(result.changed);
    assert(actors[0].x == 10);
    assert(actors[1].x == 10); // x=10 is the majority line
    assert(actors[2].x == 10);
    assert(actors[0].y == 20);
    assert(actors[1].y == 25);
    assert(actors[2].y == 30);
    assert(actors[3].x == 99 && actors[3].y == 77); // unselected actor untouched
}

static void TestAlignUsesStrictlyLessThanTwo()
{
    std::vector<LayoutActor> actors = {
        {1, 10, 20},
        {2, 12, 20},
    };

    const auto result = AlignNearLines(actors, {0, 1});

    assert(!result.changed);
    assert(actors[0].x == 10 && actors[1].x == 12);
}

static void TestAlignWithNoSelectionIsNoOp()
{
    std::vector<LayoutActor> actors = {
        {1, 10, 20},
        {2, 11, 20},
    };

    const auto result = AlignNearLines(actors, {});

    assert(!result.changed);
    assert(actors[0].x == 10 && actors[1].x == 11);
}

static void TestDistributionRequiresThreeSelectedActors()
{
    std::vector<LayoutActor> actors = {
        {1, 0, 10},
        {2, 5, 20},
        {3, 99, 99},
    };

    const auto result = DistributeAxes(actors, {0, 1});

    assert(!result.changed);
    assert(actors[0].x == 0 && actors[1].x == 5);
    assert(actors[2].x == 99 && actors[2].y == 99);
}

static void TestDistributionSortsBothAxesAndPreservesUnselected()
{
    std::vector<LayoutActor> actors = {
        {1, 0, 40},
        {2, 3, 10},
        {3, 7, 25},
        {4, 8, 30},
        {5, 100, 100},
    };
    const std::vector<std::size_t> selected = {0, 1, 2, 3};

    const auto result = DistributeAxes(actors, selected);

    assert(result.changed);
    // span 8 over 3 gaps is 2.666..., rounded to 3; the endpoint is not
    // forced back, so the final x coordinate is 9.
    assert(actors[0].x == 0);
    assert(actors[1].x == 3);
    assert(actors[2].x == 6);
    assert(actors[3].x == 9);
    // y order is 10,25,30,40, and the same rounded-step rule is applied.
    assert(actors[0].y == 10 + 3 * 10);
    assert(actors[1].y == 10);
    assert(actors[2].y == 20);
    assert(actors[3].y == 30);
    assert(actors[4].x == 100 && actors[4].y == 100);
}

static void TestAlignToPrimaryAxisPreservesPrimaryAndOtherAxis()
{
    std::vector<LayoutActor> actors = {
        {1, 10, 20},
        {2, 5, 7},
        {3, 15, 40},
        {4, 99, 77},
    };
    const std::vector<std::size_t> selected = {0, 1, 2};

    const auto result = AlignToPrimaryAxis(actors, 0, selected, true);

    assert(result.changed);
    assert(result.eligible);
    assert(actors[0].x == 10 && actors[0].y == 20); // primary is unchanged
    assert(actors[1].x == 10 && actors[1].y == 7);  // only X is aligned
    assert(actors[2].x == 10 && actors[2].y == 40);
    assert(actors[3].x == 99 && actors[3].y == 77);  // unselected untouched
}

static void TestAlignToPrimaryAxisRejectsUnselectedPrimary()
{
    std::vector<LayoutActor> actors = {
        {1, 10, 20},
        {2, 5, 7},
        {3, 15, 40},
    };

    const auto result = AlignToPrimaryAxis(actors, 0, {1, 2}, false);

    assert(!result.eligible);
    assert(!result.changed);
    assert(actors[1].y == 7 && actors[2].y == 40);
}

static void TestPreciseDistributionPreservesEndpointsAndSpreadsRemainder()
{
    std::vector<LayoutActor> actors = {
        {1, 0, 10},
        {2, 3, 20},
        {3, 10, 30},
        {4, 20, 40},
        {5, 100, 100},
    };

    const auto result = PreciseDistributeAxis(actors, {0, 1, 2, 3}, true);

    assert(result.changed);
    assert(result.eligible);
    assert(actors[0].x == 0);
    assert(actors[1].x == 7);  // round(20 / 3) = 7
    assert(actors[2].x == 13); // round(40 / 3) = 13
    assert(actors[3].x == 20); // endpoint is fixed
    assert(actors[4].x == 100 && actors[4].y == 100);
}

static void TestPreciseDistributionRequiresThreeActors()
{
    std::vector<LayoutActor> actors = {
        {1, 0, 10},
        {2, 20, 20},
    };

    const auto result = PreciseDistributeAxis(actors, {0, 1}, false);

    assert(!result.eligible);
    assert(!result.changed);
    assert(actors[0].y == 10 && actors[1].y == 20);
}

static void TestCycleDirectionWrapsLikeNativeWheel()
{
    assert(CycleDirection(2, 1) == 3);
    assert(CycleDirection(3, 1) == 0);
    assert(CycleDirection(0, -1) == 3);
    assert(CycleDirection(-1, 1) == 0);
}

int main()
{
    TestAlignUsesSelectedActorsAndMajorityLine();
    TestAlignUsesStrictlyLessThanTwo();
    TestAlignWithNoSelectionIsNoOp();
    TestDistributionRequiresThreeSelectedActors();
    TestDistributionSortsBothAxesAndPreservesUnselected();
    TestAlignToPrimaryAxisPreservesPrimaryAndOtherAxis();
    TestAlignToPrimaryAxisRejectsUnselectedPrimary();
    TestPreciseDistributionPreservesEndpointsAndSpreadsRemainder();
    TestPreciseDistributionRequiresThreeActors();
    TestCycleDirectionWrapsLikeNativeWheel();
    TestBatchTransactionRollsBackEarlierInsertions();
    return 0;
}
