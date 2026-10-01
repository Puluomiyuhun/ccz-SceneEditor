#include <cassert>

#include "../cczEditor2/ScenePositionSource.h"

using ccz::scene_preview::PositionSourceKind;
using ccz::scene_preview::PositionSourceTracker;
using ccz::scene_preview::PositionSourceMatchesInstruction;
using ccz::scene_preview::position_direction_index;

static void TestActorMoveSupersedesAppear()
{
    PositionSourceTracker tracker;
    assert(tracker.recordActor(7, 48, 101));
    assert(tracker.source.kind == PositionSourceKind::ActorAppear);
    assert(tracker.source.instructionId == 48);
    assert(tracker.source.xIndex == 1);
    assert(tracker.source.yIndex == 2);
    assert(position_direction_index(tracker.source.kind) == 3);
    assert(PositionSourceMatchesInstruction(tracker.source, {7, 10, 20, 2, 0}));
    assert(!PositionSourceMatchesInstruction(tracker.source, {8, 10, 20, 2, 0}));

    assert(tracker.recordActor(7, 50, 102));
    assert(tracker.source.kind == PositionSourceKind::ActorMove);
    assert(tracker.source.instructionId == 50);
    assert(tracker.source.xIndex == 3);
    assert(tracker.source.yIndex == 4);
    assert(position_direction_index(tracker.source.kind) == 5);
    assert(tracker.source.token == 102);
}

static void TestPortraitMoveSupersedesShow()
{
    PositionSourceTracker tracker;
    assert(tracker.recordPortrait(12, 41, 201));
    assert(tracker.recordPortrait(12, 42, 202));
    assert(tracker.source.kind == PositionSourceKind::PortraitMove);
    assert(tracker.source.instructionId == 42);
    assert(tracker.source.xIndex == 1);
    assert(tracker.source.yIndex == 2);
    assert(position_direction_index(tracker.source.kind) == -1);
    assert(tracker.source.token == 202);
    assert(PositionSourceMatchesInstruction(tracker.source, {12, 30, 40}));
    assert(!PositionSourceMatchesInstruction(tracker.source, {13, 30, 40}));
}

static void TestHideClearsOnlyMatchingSubject()
{
    PositionSourceTracker tracker;
    assert(tracker.recordPortrait(12, 41, 201));
    assert(!tracker.hidePortrait(13));
    assert(tracker.source.valid());
    assert(tracker.hidePortrait(12));
    assert(!tracker.source.valid());
}

static void TestInvalidInstructionDoesNotReplaceSource()
{
    PositionSourceTracker tracker;
    assert(tracker.recordActor(7, 48, 101));
    assert(!tracker.recordActor(7, 49, 102));
    assert(tracker.source.kind == PositionSourceKind::ActorAppear);
    assert(tracker.source.token == 101);
}

int main()
{
    assert(position_direction_index(PositionSourceKind::ActorAppear) == 3);
    assert(position_direction_index(PositionSourceKind::ActorMove) == 5);
    assert(position_direction_index(PositionSourceKind::PortraitShow) == -1);
    assert(position_direction_index(PositionSourceKind::PortraitMove) == -1);
    assert(position_direction_index(PositionSourceKind::None) == -1);
    TestActorMoveSupersedesAppear();
    TestPortraitMoveSupersedesShow();
    TestHideClearsOnlyMatchingSubject();
    TestInvalidInstructionDoesNotReplaceSource();
    return 0;
}
