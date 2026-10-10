#include "../../cczPreviewCoreTests/tests/test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattleCharacterCatalog.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using ccz::editor_preview::BattleCharacterCatalog;
using ccz::editor_preview::ParseBattleCharacterCatalog;

constexpr std::size_t kHeader = 396;
constexpr std::size_t kStride = 32;

void put_name(std::vector<std::uint8_t>& bytes, std::size_t index,
              const char* name, std::uint8_t arms = 3) {
    const std::size_t start = kHeader + index * kStride;
    for (std::size_t offset = 0; name[offset] != '\0' && offset < 12; ++offset)
        bytes[start + offset] = static_cast<std::uint8_t>(name[offset]);
    bytes[start + 26] = arms;
}

std::vector<std::uint8_t> data_fixture(std::size_t record_count) {
    return std::vector<std::uint8_t>(kHeader + record_count * kStride, 0);
}

void catalog_uses_pro_record_contract_and_cp936_names() {
    auto bytes = data_fixture(512);
    const std::uint8_t name[] = {0xd4, 0xc6, 0xf3, 0xe3}; // 云筱 in CP936
    for (std::size_t index = 0; index < sizeof(name); ++index)
        bytes[kHeader + index] = name[index];
    bytes[kHeader + 26] = 7;

    const BattleCharacterCatalog parsed = ParseBattleCharacterCatalog(bytes);
    CCZ_REQUIRE(parsed.ok);
    CCZ_REQUIRE(parsed.characters.size() == 512);
    CCZ_REQUIRE(parsed.characters[0].data_id == 0);
    CCZ_REQUIRE(parsed.characters[0].name == L"云筱");
    CCZ_REQUIRE(parsed.characters[0].has_name);
    CCZ_REQUIRE_EQ(parsed.characters[0].movement_class_code, 7);
    // Pro's brush selection supplies arm_kind separately (default 0); the
    // Data.e5 arms byte is only for the engine's base resource mapping.
    CCZ_REQUIRE(parsed.characters[0].unit_type == 0);
    CCZ_REQUIRE(parsed.characters[1].name == L"Data 1");
    CCZ_REQUIRE(!parsed.characters[1].has_name);
}

void catalog_decodes_the_12_byte_name_field() {
    auto bytes = data_fixture(512);
    const char* full_width_name = "ABCDEFGHIJKL";
    for (std::size_t index = 0; index < 12; ++index)
        bytes[kHeader + index] = static_cast<std::uint8_t>(full_width_name[index]);

    const auto parsed = ParseBattleCharacterCatalog(bytes);
    CCZ_REQUIRE(parsed.ok);
    CCZ_REQUIRE(parsed.characters[0].name == L"ABCDEFGHIJKL");
}

void catalog_preserves_actual_five_character_and_eleven_byte_names() {
    auto bytes = data_fixture(512);
    const std::uint8_t five_character_name[] = {
        0xca, 0xaf, 0xd4, 0xad, 0xb1, 0xf8, 0xb4, 0xce, 0xc0, 0xc9};
    const std::uint8_t eleven_byte_name[] = {
        0xc3, 0xfb, 0xbd, 0xab, 0xb1, 0xb8, 0xd3, 0xc3, 0x31, 0x30, 0x38};
    const std::size_t five_character_offset = kHeader + 163 * kStride;
    const std::size_t eleven_byte_offset = kHeader + 129 * kStride;
    for (std::size_t index = 0; index < sizeof(five_character_name); ++index)
        bytes[five_character_offset + index] = five_character_name[index];
    for (std::size_t index = 0; index < sizeof(eleven_byte_name); ++index)
        bytes[eleven_byte_offset + index] = eleven_byte_name[index];

    const auto parsed = ParseBattleCharacterCatalog(bytes);
    CCZ_REQUIRE(parsed.ok);
    CCZ_REQUIRE(parsed.characters[163].name == L"石原兵次郎");
    CCZ_REQUIRE(parsed.characters[129].name == L"名将备用108");
}

void catalog_selects_512_or_1024_branch_only_when_complete() {
    const auto small = ParseBattleCharacterCatalog(data_fixture(512));
    CCZ_REQUIRE(small.ok);
    CCZ_REQUIRE(small.characters.size() == 512);

    auto below_large_threshold = data_fixture(1024);
    below_large_threshold.resize(45999);
    const auto small_branch = ParseBattleCharacterCatalog(below_large_threshold);
    CCZ_REQUIRE(small_branch.ok);
    CCZ_REQUIRE(small_branch.characters.size() == 512);

    auto at_large_threshold = data_fixture(1024);
    at_large_threshold.resize(46000);
    const auto large = ParseBattleCharacterCatalog(at_large_threshold);
    CCZ_REQUIRE(large.ok);
    CCZ_REQUIRE(large.characters.size() == 1024);

    auto truncated = data_fixture(512);
    truncated.pop_back();
    const auto invalid = ParseBattleCharacterCatalog(truncated);
    CCZ_REQUIRE(!invalid.ok);
    CCZ_REQUIRE(invalid.characters.empty());
}

void brush_groups_ignore_only_ascii_digits_and_accept_adjacent_pairs() {
    auto bytes = data_fixture(512);
    put_name(bytes, 0, "Guard1");
    put_name(bytes, 1, "Guard2");
    put_name(bytes, 2, "Guard3");
    put_name(bytes, 3, "Guard4");
    put_name(bytes, 5, "Archer1");
    put_name(bytes, 6, "Archer2");
    put_name(bytes, 7, "Archer3");
    put_name(bytes, 8, "Archer4");
    put_name(bytes, 9, "ArcherX5");
    put_name(bytes, 10, "ArcherX6");
    put_name(bytes, 11, "ArcherX7");
    put_name(bytes, 12, "Scout1");
    put_name(bytes, 13, "Scout2");

    const auto parsed = ParseBattleCharacterCatalog(bytes);
    CCZ_REQUIRE(parsed.ok);
    CCZ_REQUIRE(parsed.brushes.size() == 4);
    CCZ_REQUIRE(parsed.brushes[0].name == L"Guard");
    CCZ_REQUIRE(parsed.brushes[0].first_data_id == 0);
    CCZ_REQUIRE(parsed.brushes[0].last_data_id == 3);
    CCZ_REQUIRE(parsed.brushes[1].name == L"Archer");
    CCZ_REQUIRE(parsed.brushes[1].first_data_id == 5);
    CCZ_REQUIRE(parsed.brushes[1].last_data_id == 8);
    CCZ_REQUIRE(parsed.brushes[2].name == L"ArcherX");
    CCZ_REQUIRE(parsed.brushes[2].first_data_id == 9);
    CCZ_REQUIRE(parsed.brushes[2].last_data_id == 11);
    CCZ_REQUIRE(parsed.brushes[3].name == L"Scout");
    CCZ_REQUIRE(parsed.brushes[3].first_data_id == 12);
    CCZ_REQUIRE(parsed.brushes[3].last_data_id == 13);
}

} // namespace

CCZ_TEST(battle_character_catalog_parses_S_Data_e5_records) {
    catalog_uses_pro_record_contract_and_cp936_names();
}

CCZ_TEST(battle_character_catalog_respects_the_12_byte_name_field) {
    catalog_decodes_the_12_byte_name_field();
}

CCZ_TEST(battle_character_catalog_preserves_five_character_names) {
    catalog_preserves_actual_five_character_and_eleven_byte_names();
}

CCZ_TEST(battle_character_catalog_validates_record_count_boundaries) {
    catalog_selects_512_or_1024_branch_only_when_complete();
}

CCZ_TEST(battle_minion_brush_grouping_preserves_Data_contiguous_pairs_and_runs) {
    brush_groups_ignore_only_ascii_digits_and_accept_adjacent_pairs();
}
