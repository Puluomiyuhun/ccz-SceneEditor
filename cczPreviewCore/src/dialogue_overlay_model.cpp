#include <ccz/preview/dialogue_overlay_model.h>

namespace ccz::preview {

bool dialogue_overlay_model_equal(const DialogueOverlayModel& left,
                                  const DialogueOverlayModel& right) {
    return left.visible == right.visible &&
           left.show_speaker == right.show_speaker &&
           left.show_portrait == right.show_portrait &&
           left.speaker_label == right.speaker_label &&
           left.body == right.body &&
           left.portrait_image == right.portrait_image &&
           left.diagnostic == right.diagnostic &&
           left.inactive_sub_event == right.inactive_sub_event;
}

}  // namespace ccz::preview
