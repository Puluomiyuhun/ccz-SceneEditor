#include <cassert>

#include "../cczEditor2/SceneCoordinate.h"

using ccz::scene_preview::SceneCoordinateSpace;
using ccz::scene_preview::ScenePoint;
using ccz::scene_preview::screen_to_scene_point;

static void TestOuterSceneUsesRawPixels()
{
    const ScenePoint point = screen_to_scene_point(
        SceneCoordinateSpace::OuterPixels, 930, 260, 10, 20, 2.0f);
    assert(point.x == 460);
    assert(point.y == 120);
}

static void TestInnerSceneUsesIsometricLogicalCoordinates()
{
    // GameToPixel(10, 20) = (256, -80), then apply a canvas offset/scale.
    const ScenePoint point = screen_to_scene_point(
        SceneCoordinateSpace::InnerLogical, 532, -140, 20, 20, 2.0f);
    assert(point.x == 10);
    assert(point.y == 20);
}

static void TestUnknownSpaceDoesNotInventAConversion()
{
    const ScenePoint point = screen_to_scene_point(
        SceneCoordinateSpace::Unknown, 930, 260, 10, 20, 2.0f);
    assert(point.x == 0);
    assert(point.y == 0);
}

int main()
{
    TestOuterSceneUsesRawPixels();
    TestInnerSceneUsesIsometricLogicalCoordinates();
    TestUnknownSpaceDoesNotInventAConversion();
    return 0;
}
