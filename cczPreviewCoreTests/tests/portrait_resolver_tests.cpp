#include "test_framework.h"

#include <ccz/preview/face_archive.h>
#include <ccz/preview/portrait_resolver.h>

#include <cstdint>
#include <vector>

using namespace ccz::preview;

namespace {

void write_be32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    bytes[offset + 3] = static_cast<std::uint8_t>(value & 0xff);
}
void write_le16(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

DialogueProfile make_profile() {
    DialogueProfile profile;
    profile.data_record_base = 0x18c;
    profile.data_record_stride = 0x20;
    profile.avatar_offset = 0x0d;
    profile.avatar_width = 2;
    profile.r_image_offset = 0x0f;
    profile.r_image_width = 2;
    profile.face_index_base = 0;
    profile.face_entry_size = 12;
    profile.face_index_table_offset = 0x110;
    profile.face_index_bias = 7;
    return profile;
}

std::vector<std::uint8_t> make_data_e5(std::int32_t person_id,
                                       std::uint16_t avatar_code,
                                       std::uint16_t r_image_code) {
    const std::size_t record_offset =
        0x18c + static_cast<std::size_t>(person_id) * 0x20;
    std::vector<std::uint8_t> bytes(record_offset + 0x20, 0);
    write_le16(bytes, record_offset + 0x0d, avatar_code);
    write_le16(bytes, record_offset + 0x0f, r_image_code);
    return bytes;
}

std::vector<std::uint8_t> make_face_archive(std::uint32_t entry_count) {
    constexpr std::size_t table_offset = 0x110;
    constexpr std::size_t entry_size = 12;
    constexpr std::size_t padding_size = 4;
    constexpr std::size_t payload_size = 1;
    const std::size_t data_start =
        table_offset + entry_size * entry_count + padding_size;
    std::vector<std::uint8_t> bytes(data_start + entry_count * payload_size, 0);

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::size_t entry_offset = table_offset + entry_size * index;
        const std::uint32_t data_offset =
            static_cast<std::uint32_t>(data_start + index * payload_size);
        write_be32(bytes, entry_offset + 0, payload_size);
        write_be32(bytes, entry_offset + 4, payload_size);
        write_be32(bytes, entry_offset + 8, data_offset);
        bytes[data_offset] = static_cast<std::uint8_t>(0x80 + index);
    }
    return bytes;
}

}  // namespace

CCZ_TEST(portrait_resolver_uses_data_avatar_not_r_image) {
    const DialogueProfile profile = make_profile();
    const std::vector<std::uint8_t> data = make_data_e5(1, 1, 99);
    const std::vector<std::uint8_t> face_bytes = make_face_archive(9);
    const FaceArchive archive = parse_face_archive(face_bytes, profile);

    const PortraitResult result =
        resolve_portrait(1, 0, data, profile, archive, face_bytes);

    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.person_id, 1);
    CCZ_REQUIRE_EQ(result.face_image_index, std::uint32_t(8));
    CCZ_REQUIRE_EQ(result.encoded_image.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(result.encoded_image[0], static_cast<std::uint8_t>(0x88));
}

CCZ_TEST(portrait_resolver_applies_explicit_special_state_mapping) {
    DialogueProfile profile = make_profile();
    profile.special_state_map.push_back({1, 4, 4});
    const std::vector<std::uint8_t> data = make_data_e5(1, 1, 99);
    const std::vector<std::uint8_t> face_bytes = make_face_archive(9);
    const FaceArchive archive = parse_face_archive(face_bytes, profile);

    const PortraitResult result =
        resolve_portrait(1, 4, data, profile, archive, face_bytes);

    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.face_state, static_cast<std::uint16_t>(4));
    CCZ_REQUIRE_EQ(result.face_image_index, std::uint32_t(4));
    CCZ_REQUIRE_EQ(result.encoded_image[0], static_cast<std::uint8_t>(0x84));
}

CCZ_TEST(portrait_resolver_reports_unmapped_state_without_guessing) {
    DialogueProfile profile = make_profile();
    profile.special_state_map.push_back({1, 4, 4});
    const std::vector<std::uint8_t> data = make_data_e5(1, 1, 99);
    const std::vector<std::uint8_t> face_bytes = make_face_archive(9);
    const FaceArchive archive = parse_face_archive(face_bytes, profile);

    const PortraitResult result =
        resolve_portrait(1, 16, data, profile, archive, face_bytes);

    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE_EQ(result.face_image_index, std::uint32_t(0));
    CCZ_REQUIRE(result.encoded_image.empty());
    CCZ_REQUIRE(!result.diagnostic.empty());
}

CCZ_TEST(portrait_resolver_reports_invalid_person_and_missing_archive_entry) {
    const DialogueProfile profile = make_profile();
    const std::vector<std::uint8_t> data = make_data_e5(1, 1, 99);
    const std::vector<std::uint8_t> face_bytes = make_face_archive(8);
    const FaceArchive archive = parse_face_archive(face_bytes, profile);

    const PortraitResult invalid_person =
        resolve_portrait(-1, 0, data, profile, archive, face_bytes);
    const PortraitResult missing_entry =
        resolve_portrait(1, 0, data, profile, archive, face_bytes);

    CCZ_REQUIRE(!invalid_person.resolved);
    CCZ_REQUIRE(!invalid_person.diagnostic.empty());
    CCZ_REQUIRE(!missing_entry.resolved);
    CCZ_REQUIRE(!missing_entry.diagnostic.empty());
}

CCZ_TEST(portrait_resolver_reports_truncated_data_record) {
    const DialogueProfile profile = make_profile();
    const std::vector<std::uint8_t> data(0x18c + 0x20, 0);
    const std::vector<std::uint8_t> face_bytes = make_face_archive(9);
    const FaceArchive archive = parse_face_archive(face_bytes, profile);

    const PortraitResult result =
        resolve_portrait(2, 0, data, profile, archive, face_bytes);

    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(!result.diagnostic.empty());
}


