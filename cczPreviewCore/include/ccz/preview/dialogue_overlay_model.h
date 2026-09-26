#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

struct DialogueOverlayModel {
    bool visible{};
    // Plain-text commands (0x16/0x2C/0x69) deliberately do not reserve a
    // speaker or portrait slot.  Dialogue commands keep both flags enabled.
    bool show_speaker{true};
    bool show_portrait{true};
    std::wstring speaker_label;
    std::wstring body;
    std::vector<std::uint8_t> portrait_image;
    std::wstring diagnostic;
    // The selected node exists in the Section model, but the current
    // predicate/choice state has ruled out its execution path.  This is kept
    // separate from ordinary diagnostics so the editor can render the
    // inactive-child hint in a stable error color without recoloring asset or
    // replay warnings.
    bool inactive_sub_event{};
};

bool dialogue_overlay_model_equal(const DialogueOverlayModel& left,
                                  const DialogueOverlayModel& right);

}  // namespace ccz::preview
