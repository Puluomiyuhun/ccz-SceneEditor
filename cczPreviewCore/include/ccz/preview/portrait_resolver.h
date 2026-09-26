#pragma once

#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/face_archive.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

struct PortraitResult {
    bool resolved{};
    std::int32_t person_id{};
    std::uint16_t face_state{};
    std::uint32_t face_image_index{};
    std::vector<std::uint8_t> encoded_image;
    std::wstring diagnostic;
};

PortraitResult resolve_portrait(
    std::int32_t person_id,
    std::uint16_t face_state,
    const std::vector<std::uint8_t>& data_e5,
    const DialogueProfile& profile,
    const FaceArchive& face_archive,
    const std::vector<std::uint8_t>& face_archive_bytes);

}  // namespace ccz::preview


