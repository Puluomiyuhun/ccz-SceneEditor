#include "test_framework.h"

#include <ccz/preview/face_archive.h>

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

DialogueProfile make_face_profile() {
    DialogueProfile profile;
    profile.face_index_table_offset = 0x110;
    profile.face_entry_size = 12;
    profile.face_index_base = 0;
    return profile;
}

std::vector<std::uint8_t> make_archive() {
    constexpr std::size_t table_offset = 0x110;
    constexpr std::size_t entry_size = 12;
    constexpr std::size_t entry_count = 2;
    constexpr std::size_t data_start = table_offset + entry_size * entry_count + 4;
    constexpr std::size_t first_size = 8;
    constexpr std::size_t second_size = 5;
    constexpr std::size_t second_offset = data_start + first_size;

    std::vector<std::uint8_t> bytes(second_offset + second_size, 0);
    write_be32(bytes, table_offset + 0, first_size);
    write_be32(bytes, table_offset + 4, first_size);
    write_be32(bytes, table_offset + 8, data_start);
    write_be32(bytes, table_offset + entry_size + 0, second_size);
    write_be32(bytes, table_offset + entry_size + 4, second_size);
    write_be32(bytes, table_offset + entry_size + 8, second_offset);

    const std::uint8_t png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
    };
    for (std::size_t index = 0; index < first_size; ++index) {
        bytes[data_start + index] = png_signature[index];
    }
    for (std::size_t index = 0; index < second_size; ++index) {
        bytes[second_offset + index] = static_cast<std::uint8_t>(0xa0 + index);
    }
    return bytes;
}

}  // namespace

CCZ_TEST(face_archive_parses_big_endian_table_and_entries) {
    const FaceArchive archive = parse_face_archive(make_archive(), make_face_profile());

    CCZ_REQUIRE(archive.valid);
    CCZ_REQUIRE_EQ(archive.table_entry_count, std::uint32_t(2));
    CCZ_REQUIRE_EQ(archive.data_start, std::uint32_t(0x12c));
    CCZ_REQUIRE_EQ(archive.entries.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(archive.entries[0].stored_size, std::uint32_t(8));
    CCZ_REQUIRE_EQ(archive.entries[0].decoded_size, std::uint32_t(8));
    CCZ_REQUIRE_EQ(archive.entries[0].data_offset, std::uint32_t(0x12c));
    CCZ_REQUIRE(archive.entries[0].looks_like_png);
    CCZ_REQUIRE_EQ(archive.entries[1].zero_based_index, std::uint32_t(1));
    CCZ_REQUIRE_EQ(archive.entries[1].data_offset, std::uint32_t(0x134));
}

CCZ_TEST(face_archive_finds_zero_based_entry_and_copies_payload) {
    const std::vector<std::uint8_t> bytes = make_archive();
    const FaceArchive archive = parse_face_archive(bytes, make_face_profile());
    const FaceArchiveEntry* entry = find_face_entry(archive, 1);

    CCZ_REQUIRE(entry != nullptr);
    const std::vector<std::uint8_t> payload = copy_face_payload(bytes, *entry);
    CCZ_REQUIRE_EQ(payload.size(), std::size_t(5));
    CCZ_REQUIRE_EQ(payload[0], static_cast<std::uint8_t>(0xa0));
    CCZ_REQUIRE_EQ(payload[4], static_cast<std::uint8_t>(0xa4));
    CCZ_REQUIRE(find_face_entry(archive, 2) == nullptr);
}

CCZ_TEST(face_archive_rejects_out_of_range_payload) {
    std::vector<std::uint8_t> bytes = make_archive();
    write_be32(bytes, 0x110 + 12 + 8, 0x7fffffff);
    const FaceArchive archive = parse_face_archive(bytes, make_face_profile());

    CCZ_REQUIRE(!archive.valid);
    CCZ_REQUIRE(!archive.diagnostic.empty());
    CCZ_REQUIRE(archive.entries.empty());
}

CCZ_TEST(face_archive_rejects_missing_table_or_bad_layout) {
    DialogueProfile profile = make_face_profile();
    profile.face_entry_size = 8;
    const std::vector<std::uint8_t> bytes(0x120, 0);
    const FaceArchive archive = parse_face_archive(bytes, profile);

    CCZ_REQUIRE(!archive.valid);
    CCZ_REQUIRE(!archive.diagnostic.empty());
}


