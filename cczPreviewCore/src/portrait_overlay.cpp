#include <ccz/preview/portrait_overlay.h>

#include <ccz/preview/portrait_resolver.h>

#include <utility>

namespace ccz::preview {
namespace {

void append_diagnostic(std::wstring& target, const std::wstring& message) {
    if (message.empty()) {
        return;
    }
    if (!target.empty()) {
        target += L"；";
    }
    target += message;
}

}  // namespace

void populate_portrait_overlay(
    DialogueOverlayModel& model,
    std::int32_t person_id,
    std::uint16_t face_state,
    const std::vector<std::uint8_t>& data_e5,
    const DialogueProfile& profile,
    const FaceArchive& face_archive,
    const std::vector<std::uint8_t>& face_archive_bytes) {
    const PortraitResult portrait =
        resolve_portrait(person_id, face_state, data_e5, profile,
                         face_archive, face_archive_bytes);
    if (!portrait.resolved) {
        model.portrait_image.clear();
        append_diagnostic(model.diagnostic, portrait.diagnostic);
        return;
    }
    model.portrait_image = portrait.encoded_image;
}

}  // namespace ccz::preview


