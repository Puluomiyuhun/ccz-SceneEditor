#pragma once

#include <afxwin.h>
#include <array>

#include <ccz/preview/dialogue_overlay_model.h>

namespace ccz::editor_preview {

struct OverlayLayout {
    CRect panel;
    CRect portrait;
    CRect speaker;
    CRect body;
    CRect diagnostic;
    std::array<CRect, 3> half_bodies;
    bool game_half_body = false;
    float text_scale = 1.0f;
};

bool CalculateDialogueOverlayLayout(const CRect& client,
                                    OverlayLayout& result);

struct RSceneViewport {
    CRect bounds;
    float scale = 1.0f;
};
RSceneViewport CalculateRSceneViewport(const CRect& client, int player_height);

bool CalculateHalfBodyOverlayLayout(const CRect& scene, OverlayLayout& result);

bool CalculateHalfBodyDialogueLayout(const CRect& scene, const CRect& client, OverlayLayout& result);

void PaintDialogueOverlay(
    CDC& dc, const OverlayLayout& layout,
    const ccz::preview::DialogueOverlayModel& model);

}  // namespace ccz::editor_preview
