#pragma once

#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/face_archive.h>
#include <ccz/preview/dialogue_overlay_model.h>

#include <cstdint>
#include <vector>

namespace ccz::preview {

void populate_portrait_overlay(
    DialogueOverlayModel& model,
    std::int32_t person_id,
    std::uint16_t face_state,
    const std::vector<std::uint8_t>& data_e5,
    const DialogueProfile& profile,
    const FaceArchive& face_archive,
    const std::vector<std::uint8_t>& face_archive_bytes);

}  // namespace ccz::preview

