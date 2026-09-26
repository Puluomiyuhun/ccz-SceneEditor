#pragma once

#include <ccz/preview/dialogue_profile.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

struct FaceArchiveEntry {
    std::uint32_t zero_based_index{};
    std::uint32_t stored_size{};
    std::uint32_t decoded_size{};
    std::uint32_t data_offset{};
    bool looks_like_png{};
};

struct FaceArchive {
    bool valid{};
    std::uint32_t data_start{};
    std::uint32_t table_entry_count{};
    std::vector<FaceArchiveEntry> entries;
    std::wstring diagnostic;
};

FaceArchive parse_face_archive(
    const std::vector<std::uint8_t>& archive_bytes,
    const DialogueProfile& profile);

const FaceArchiveEntry* find_face_entry(
    const FaceArchive& archive, std::uint32_t zero_based_index);

std::vector<std::uint8_t> copy_face_payload(
    const std::vector<std::uint8_t>& archive_bytes,
    const FaceArchiveEntry& entry);

}  // namespace ccz::preview


