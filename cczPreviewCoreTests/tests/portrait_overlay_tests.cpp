#include "test_framework.h"

#include <ccz/preview/face_archive.h>
#include <ccz/preview/portrait_overlay.h>

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
    profile.face_index_table_offset = 0x110;
    profile.face_entry_size = 12;
    profile.face_index_base = 0;
    profile.face_index_bias = 7;
    return profile;
}

std::vector<std::uint8_t> make_data_e5() {
    const std::size_t record = 0x18c + 0x20;
    std::vector<std::uint8_t> bytes(record + 0x20, 0);
    write_le16(bytes, record + 0x0d, 1);
    return bytes;
}

std::vector<std::uint8_t> make_face_e5() {
    constexpr std::size_t count = 9;
    constexpr std::size_t table = 0x110;
    constexpr std::size_t entry_size = 12;
    const std::size_t data_start = table + count * entry_size + 4;
    std::vector<std::uint8_t> bytes(data_start + count, 0);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t entry = table + index * entry_size;
        write_be32(bytes, entry + 0, 1);
        write_be32(bytes, entry + 4, 1);
        write_be32(bytes, entry + 8,
                   static_cast<std::uint32_t>(data_start + index));
        bytes[data_start + index] = static_cast<std::uint8_t>(0x80 + index);
    }
    return bytes;
}

}  // namespace

CCZ_TEST(portrait_overlay_populates_image_for_normal_data_person) {
    const DialogueProfile profile = make_profile();
    const std::vector<std::uint8_t> data = make_data_e5();
    const std::vector<std::uint8_t> face_bytes = make_face_e5();
    const FaceArchive archive = parse_face_archive(face_bytes, profile);
    DialogueOverlayModel model;

    populate_portrait_overlay(model, 1, 0, data, profile, archive, face_bytes);

    CCZ_REQUIRE_EQ(model.portrait_image.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(model.portrait_image[0], static_cast<std::uint8_t>(0x88));
    CCZ_REQUIRE(model.diagnostic.empty());
}

CCZ_TEST(portrait_overlay_keeps_text_model_and_reports_unmapped_state) {
    DialogueProfile profile = make_profile();
    const std::vector<std::uint8_t> data = make_data_e5();
    const std::vector<std::uint8_t> face_bytes = make_face_e5();
    const FaceArchive archive = parse_face_archive(face_bytes, profile);
    DialogueOverlayModel model;
    model.speaker_label = L"人物";
    model.body = L"台词";

    populate_portrait_overlay(model, 1, 16, data, profile, archive, face_bytes);

    CCZ_REQUIRE(model.portrait_image.empty());
    CCZ_REQUIRE(!model.diagnostic.empty());
    CCZ_REQUIRE_EQ(model.speaker_label, std::wstring(L"人物"));
    CCZ_REQUIRE_EQ(model.body, std::wstring(L"台词"));
}


