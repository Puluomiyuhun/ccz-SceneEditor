#pragma once

#include <cmath>

namespace ccz {
namespace scene_preview {

enum class SceneCoordinateSpace {
    Unknown,
    OuterPixels,
    InnerLogical,
};

struct ScenePoint {
    int x = 0;
    int y = 0;
};

inline SceneCoordinateSpace coordinate_space_for_background_branch(int branch) noexcept
{
    switch (branch) {
        case 0: return SceneCoordinateSpace::OuterPixels;
        case 2: return SceneCoordinateSpace::InnerLogical;
        default: return SceneCoordinateSpace::Unknown;
    }
}

inline ScenePoint screen_to_scene_point(
    SceneCoordinateSpace space,
    int screenX,
    int screenY,
    int offsetX,
    int offsetY,
    float scale) noexcept
{
    if (scale <= 0.0f) return {};

    const float cx = (screenX - offsetX) / scale;
    const float cy = (screenY - offsetY) / scale;

    if (space == SceneCoordinateSpace::OuterPixels) {
        return {
            static_cast<int>(std::lround(cx)),
            static_cast<int>(std::lround(cy)),
        };
    }

    if (space == SceneCoordinateSpace::InnerLogical) {
        // Existing isometric transform:
        // px = 8 * (X - Y + 42), py = 4 * (X + Y - 50).
        const float xMinusY = cx / 8.0f - 42.0f;
        const float xPlusY = cy / 4.0f + 50.0f;
        return {
            static_cast<int>(std::lround((xMinusY + xPlusY) / 2.0f)),
            static_cast<int>(std::lround((xPlusY - xMinusY) / 2.0f)),
        };
    }

    return {};
}

}  // namespace scene_preview
}  // namespace ccz
