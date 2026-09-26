#include <ccz/preview/instruction_catalog.h>
#include "test_framework.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

using ccz::preview::instruction_name;
using ccz::preview::parse_instruction_catalog;

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> values) {
    return std::vector<std::uint8_t>(values);
}

void append_ascii(std::vector<std::uint8_t>& destination, const char* text) {
    while (*text != '\0') {
        destination.push_back(static_cast<std::uint8_t>(*text));
        ++text;
    }
}

}  // namespace

CCZ_TEST(instruction_catalog_parses_ascii_and_cp936_names) {
    auto input = bytes({
        '0', ':', 'A', 's', 'c', 'i', 'i', ',',
        '5', ':', 0xb1, 0xe4, 0xc1, 0xbf, 0xb2, 0xe2, 0xca, 0xd4, ',',
        '0', 'x', '0', 'c', ':', 0xd1, 0xa1, 0xd4, 0xf1, 0xbf, 0xf2,
        '\r', '\n',
    });
    append_ascii(input, "99:ignored-second-line");
    const auto catalog = parse_instruction_catalog(input);

    CCZ_REQUIRE(catalog.valid);
    CCZ_REQUIRE_EQ(catalog.names.at(0), std::wstring(L"Ascii"));
    CCZ_REQUIRE_EQ(catalog.names.at(5), std::wstring(L"变量测试"));
    CCZ_REQUIRE_EQ(catalog.names.at(12), std::wstring(L"选择框"));
}

CCZ_TEST(instruction_catalog_treats_unprefixed_ids_as_hex) {
    std::vector<std::uint8_t> input;
    append_ascii(input, "36:StatusTest,37:MoneyTest,79:VariableTest\r\n");

    const auto catalog = parse_instruction_catalog(input);

    CCZ_REQUIRE(catalog.valid);
    CCZ_REQUIRE_EQ(catalog.names.at(0x36), std::wstring(L"StatusTest"));
    CCZ_REQUIRE_EQ(catalog.names.at(0x37), std::wstring(L"MoneyTest"));
    CCZ_REQUIRE_EQ(catalog.names.at(0x79), std::wstring(L"VariableTest"));
}

CCZ_TEST(instruction_catalog_accepts_single_trailing_comma) {
    std::vector<std::uint8_t> input;
    append_ascii(input, "12:Choice,13:Case,79:VariableTest,\r\n");

    const auto catalog = parse_instruction_catalog(input);

    CCZ_REQUIRE(catalog.valid);
    CCZ_REQUIRE(catalog.diagnostic.empty());
    CCZ_REQUIRE_EQ(catalog.names.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(catalog.names.at(0x79), std::wstring(L"VariableTest"));
}

CCZ_TEST(instruction_catalog_reports_bad_item_and_keeps_other_entries) {
    const auto catalog = parse_instruction_catalog(bytes({
        '5', ':', 0xb1, 0xe4, 0xc1, 0xbf, 0xb2, 0xe2, 0xca, 0xd4, ',',
        '6', ':', 0x81, ',',
        '7', ':', 'A', 'f', 't', 'e', 'r', '\n',
    }));

    CCZ_REQUIRE(catalog.valid);
    CCZ_REQUIRE_EQ(catalog.names.at(5), std::wstring(L"变量测试"));
    CCZ_REQUIRE_EQ(catalog.names.at(7), std::wstring(L"After"));
    CCZ_REQUIRE(catalog.names.find(6) == catalog.names.end());
    CCZ_REQUIRE(!catalog.diagnostic.empty());
}

CCZ_TEST(instruction_catalog_formats_unknown_id) {
    auto input = bytes({'5', ':'});
    append_ascii(input, "Known");
    const auto catalog = parse_instruction_catalog(input);

    CCZ_REQUIRE_EQ(instruction_name(catalog, 5), std::wstring(L"Known"));
    CCZ_REQUIRE_EQ(instruction_name(catalog, 0x7f),
                   std::wstring(L"0x7F:未知指令"));
}


