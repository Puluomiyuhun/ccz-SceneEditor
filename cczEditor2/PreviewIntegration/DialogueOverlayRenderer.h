#pragma once

#include <afxwin.h>

#include <ccz/preview/dialogue_overlay_model.h>

namespace ccz::editor_preview {

struct OverlayLayout {
    CRect panel;
    CRect portrait;
    CRect speaker;
    CRect body;
    CRect diagnostic;
};

bool CalculateDialogueOverlayLayout(const CRect& client,
                                    OverlayLayout& result);

void PaintDialogueOverlay(
    CDC& dc, const OverlayLayout& layout,
    const ccz::preview::DialogueOverlayModel& model);

}  // namespace ccz::editor_preview
