#include "test_framework.h"

#include <ccz/preview/battle_instruction_parser.h>
#include <ccz/preview/battle_field_spec.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace {

ccz::preview::RawReplayNode instruction(
    std::uint16_t instruction_id, std::vector<std::int32_t> int_data,
    std::wstring key) {
    ccz::preview::RawReplayNode node;
    node.logical_key = std::move(key);
    node.instruction_id = instruction_id;
    node.command.instruction_id = instruction_id;
    node.command.int_data = std::move(int_data);
    return node;
}

std::vector<std::int32_t> battle_map_payload(std::int32_t map_id) {
    // cczEditor2View::EditData selects the value at mode + 1.  Mode 3 is
    // the battle-map form, so the map id is the fifth normalized field.
    return {3, -1, -1, -1, map_id};
}

ccz::preview::RawReplayNode container(
    std::wstring key, std::vector<ccz::preview::RawReplayNode> children) {
    ccz::preview::RawReplayNode node;
    node.logical_key = std::move(key);
    node.instruction_id = 0xffff;
    node.children = std::move(children);
    return node;
}

ccz::preview::BattleParseOptions scoped_options(const wchar_t* key) {
    ccz::preview::BattleParseOptions options;
    options.scope_root_key = key;
    return options;
}

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

private:
    std::filesystem::path previous_;
};

class ScopedTemporaryDirectory {
public:
    ScopedTemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("ccz-battle-field-spec-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~ScopedTemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

CCZ_TEST(battle_parser_reads_map_and_initialization_without_guessing_0x44) {
    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_00.eex",
        {container(scope, {
             instruction(0x44, {}, scope + L"|node:0|instruction:68"),
             instruction(0x27, battle_map_payload(42),
                        scope + L"|node:1|instruction:39"),
         })},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.state.initialized);
    CCZ_REQUIRE(result.state.map.valid);
    CCZ_REQUIRE_EQ(result.state.map.map_id, 42);
    CCZ_REQUIRE_EQ(result.state.map.image_index, -1);
    CCZ_REQUIRE(result.diagnostics.empty());
    CCZ_REQUIRE(result.ok);
}

CCZ_TEST(battle_parser_rejects_map_mode_without_dynamic_map_field) {
    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_00.eex",
        {container(scope, {instruction(
             0x27, {3, 42}, scope + L"|node:0|instruction:39")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(!result.state.map.valid);
    CCZ_REQUIRE(result.diagnostics.size() == std::size_t{1});
    CCZ_REQUIRE(result.diagnostics[0].find(L"0x27") != std::wstring::npos);
    CCZ_REQUIRE(result.diagnostics[0].find(L"长度") != std::wstring::npos);
}

CCZ_TEST(battle_parser_preserves_record_order_duplicates_and_raw_fields) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> row_a(11, 0);
    std::vector<std::int32_t> row_b(11, 0);
    row_a[0] = 101;
    row_b[0] = 101;
    row_a[3] = 7;
    row_b[3] = 8;
    row_a.insert(row_a.end(), row_b.begin(), row_b.end());

    auto result = ccz::preview::parse_battle_instructions(
        L"S_13.eex",
        {container(scope, {instruction(
             0x46, row_a, scope + L"|node:0|instruction:70")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{2});
    CCZ_REQUIRE_EQ(result.state.units[0].source_instruction,
                   static_cast<std::uint16_t>(0x46));
    CCZ_REQUIRE_EQ(result.state.units[0].raw_fields[0], 101);
    CCZ_REQUIRE_EQ(result.state.units[0].raw_fields[3], 7);
    CCZ_REQUIRE_EQ(result.state.units[1].raw_fields[3], 8);
    CCZ_REQUIRE_EQ(result.state.units[0].data_id, 101);
    CCZ_REQUIRE_EQ(result.state.units[0].position.y, 7);
    CCZ_REQUIRE(result.state.units[0].fields_decoded);
    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE(result.field_table_complete);
    CCZ_REQUIRE(result.diagnostics.empty());
}

CCZ_TEST(battle_parser_decodes_confirmed_roles_without_instructions_json) {
    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_15.eex",
        {container(scope, {instruction(
             0x47, {202, 1, 1, 12, 13, 4, 9, 77, 3, 808, 14, 15},
             scope + L"|node:0|instruction:71")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{1});
    const auto& unit = result.state.units[0];
    CCZ_REQUIRE(unit.fields_decoded);
    CCZ_REQUIRE_EQ(unit.data_id, 202);
    CCZ_REQUIRE(unit.hidden);
    CCZ_REQUIRE_EQ(unit.position.x, 12);
    CCZ_REQUIRE_EQ(unit.position.y, 13);
    CCZ_REQUIRE_EQ(unit.direction, 4);
    CCZ_REQUIRE_EQ(unit.level, 9);
    CCZ_REQUIRE_EQ(unit.unit_type, 77);
    CCZ_REQUIRE(unit.reinforcement);
    CCZ_REQUIRE_EQ(unit.ai_policy, 3);
    CCZ_REQUIRE_EQ(unit.target, 808);
    CCZ_REQUIRE_EQ(unit.target_x, 14);
    CCZ_REQUIRE_EQ(unit.target_y, 15);
    CCZ_REQUIRE(result.field_table_complete);
}

CCZ_TEST(battle_parser_leaves_inapplicable_confirmed_fields_at_explicit_defaults) {
    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_15.eex",
        {container(scope, {instruction(
             0x4b, {303, 7, 8, 2, 1},
             scope + L"|node:0|instruction:75")})},
        scoped_options(scope.c_str()));

    const auto& unit = result.state.units[0];
    CCZ_REQUIRE(unit.fields_decoded);
    CCZ_REQUIRE(!unit.reinforcement);
    CCZ_REQUIRE_EQ(unit.ai_policy, -1);
    CCZ_REQUIRE_EQ(unit.target, -1);
    CCZ_REQUIRE_EQ(unit.target_x, -1);
    CCZ_REQUIRE_EQ(unit.target_y, -1);
}

CCZ_TEST(battle_parser_does_not_treat_0x4b_deployment_slot_as_data_id) {
    const std::wstring scope = L"battle:slot";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_10.eex",
        {container(scope, {instruction(
             0x4b, {0, 7, 8, 2, 1},
             scope + L"|node:0|instruction:75")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{1});
    const auto& unit = result.state.units[0];
    CCZ_REQUIRE(unit.fields_decoded);
    CCZ_REQUIRE_EQ(unit.data_id, -1);
    CCZ_REQUIRE_EQ(unit.deployment_index, 0);
    CCZ_REQUIRE_EQ(unit.position.x, 7);
    CCZ_REQUIRE_EQ(unit.position.y, 8);
    CCZ_REQUIRE_EQ(unit.direction, 2);
    CCZ_REQUIRE(unit.hidden);
}

CCZ_TEST(battle_spec_and_parser_ignore_conflicting_runtime_instructions_json) {
    ScopedTemporaryDirectory temporary_directory;
    ScopedCurrentPath current_path(temporary_directory.path());
    {
        std::ofstream conflict("Instructions.json", std::ios::binary);
        conflict << R"({"0x46":{"size":2,"params":[{"type":"0x05"}]},"
                 "0x47":{"size":1},"0x4B":{"size":999}})";
    }

    const auto* spec = ccz::preview::get_battle_field_spec(0x47);
    CCZ_REQUIRE(spec != nullptr);
    CCZ_REQUIRE_EQ(spec->record_stride, std::size_t{12});
    CCZ_REQUIRE_EQ(spec->field_roles[1], ccz::preview::BattleFieldRole::Reinforcement);

    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"temporary/S_15.eex",
        {container(scope, {instruction(
             0x47, {202, 1, 1, 12, 13, 4, 9, 77, 3, 808, 14, 15},
             scope + L"|node:0|instruction:71")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE(result.field_table_complete);
    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(result.state.units[0].position.x, 12);
    CCZ_REQUIRE_EQ(result.state.units[0].ai_policy, 3);
    CCZ_REQUIRE_EQ(result.state.units[0].target_x, 14);
    CCZ_REQUIRE_EQ(result.state.units[0].target_y, 15);
}

CCZ_TEST(battle_parser_handles_all_known_record_strides) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> row_47(12, 0);
    std::vector<std::int32_t> row_4b(5, 0);
    row_47[0] = 202;
    row_4b[0] = 303;
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_15.eex",
        {container(scope, {
             instruction(0x47, row_47,
                        scope + L"|node:0|instruction:71"),
             instruction(0x4b, row_4b,
                        scope + L"|node:1|instruction:75"),
         })},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{2});
    CCZ_REQUIRE_EQ(result.state.units[0].raw_fields.size(), std::size_t{12});
    CCZ_REQUIRE_EQ(result.state.units[1].raw_fields.size(), std::size_t{5});
    CCZ_REQUIRE_EQ(result.state.units[0].source_instruction,
                   static_cast<std::uint16_t>(0x47));
    CCZ_REQUIRE_EQ(result.state.units[1].source_instruction,
                   static_cast<std::uint16_t>(0x4b));
}

CCZ_TEST(battle_parser_trims_native_reserved_field_storage) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> native_46(1000, 0);
    native_46[0] = 401;
    native_46[2] = 17;
    native_46[3] = 21;
    std::vector<std::int32_t> native_47(1000, 0);
    native_47[0] = 402;
    native_47[3] = 31;
    native_47[4] = 35;
    std::vector<std::int32_t> native_4b(20, 0);
    native_4b[0] = 403;
    native_4b[1] = 44;
    native_4b[2] = 55;
    native_4b[3] = 2;
    native_4b[4] = 1;

    const auto result = ccz::preview::parse_battle_instructions(
        L"S_10.eex",
        {container(scope, {
             instruction(0x46, std::move(native_46),
                         scope + L"|node:0|instruction:70"),
             instruction(0x47, std::move(native_47),
                         scope + L"|node:1|instruction:71"),
             instruction(0x4b, std::move(native_4b),
                         scope + L"|node:2|instruction:75"),
         })},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE(result.diagnostics.empty());
    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{101});
    CCZ_REQUIRE_EQ(result.state.units[0].data_id, 401);
    CCZ_REQUIRE_EQ(result.state.units[0].position.x, 17);
    CCZ_REQUIRE_EQ(result.state.units[0].position.y, 21);
    CCZ_REQUIRE_EQ(result.state.units[20].data_id, 402);
    CCZ_REQUIRE_EQ(result.state.units[20].position.x, 31);
    CCZ_REQUIRE_EQ(result.state.units[20].position.y, 35);
    CCZ_REQUIRE_EQ(result.state.units[100].data_id, -1);
    CCZ_REQUIRE_EQ(result.state.units[100].deployment_index, 403);
    CCZ_REQUIRE_EQ(result.state.units[100].position.x, 44);
    CCZ_REQUIRE_EQ(result.state.units[100].position.y, 55);
    CCZ_REQUIRE(result.field_table_complete);
}

CCZ_TEST(battle_parser_assigns_fixed_record_pages_in_scoped_node_order) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> first(12, 0);
    std::vector<std::int32_t> second(12, 0);
    first[0] = 101;
    second[0] = 202;
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_13.eex",
        {container(scope, {
             instruction(0x47, first,
                         scope + L"|node:2|instruction:71"),
             instruction(0x47, second,
                         scope + L"|node:10|instruction:71"),
         })},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE_EQ(result.state.units.size(), std::size_t{2});
    CCZ_REQUIRE_EQ(result.state.units[0].page, 0);
    CCZ_REQUIRE_EQ(result.state.units[1].page, 1);
}

CCZ_TEST(battle_parser_keeps_two_complete_47_pages_and_all_records) {
    const std::wstring scope = L"battle:A";
    constexpr std::size_t kStride = 12;
    constexpr std::size_t kRowsPerPage = 80;
    std::vector<std::int32_t> first(kRowsPerPage * kStride, -1);
    std::vector<std::int32_t> second(kRowsPerPage * kStride, -1);
    for (std::size_t row = 0; row < kRowsPerPage; ++row) {
        const std::size_t offset = row * kStride;
        first[offset + 0] = 100 + static_cast<std::int32_t>(row);
        first[offset + 3] = static_cast<std::int32_t>(row);
        first[offset + 4] = static_cast<std::int32_t>(row + 1);
        second[offset + 0] = 300 + static_cast<std::int32_t>(row);
        second[offset + 3] = static_cast<std::int32_t>(row + 10);
        second[offset + 4] = static_cast<std::int32_t>(row + 11);
    }

    const auto result = ccz::preview::parse_battle_instructions(
        L"S_10.eex",
        {container(scope, {
             instruction(0x47, std::move(first),
                         scope + L"|node:2|instruction:71"),
             instruction(0x47, std::move(second),
                         scope + L"|node:10|instruction:71"),
         })},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE(result.diagnostics.empty());
    CCZ_REQUIRE_EQ(result.state.units.size(), kRowsPerPage * 2);
    CCZ_REQUIRE_EQ(result.state.units[79].page, 0);
    CCZ_REQUIRE_EQ(result.state.units[79].record_index, 79);
    CCZ_REQUIRE_EQ(result.state.units[79].data_id, 179);
    CCZ_REQUIRE_EQ(result.state.units[80].page, 1);
    CCZ_REQUIRE_EQ(result.state.units[80].record_index, 0);
    CCZ_REQUIRE_EQ(result.state.units[80].data_id, 300);
    CCZ_REQUIRE_EQ(result.state.units[159].page, 1);
    CCZ_REQUIRE_EQ(result.state.units[159].record_index, 79);
    CCZ_REQUIRE_EQ(result.state.units[159].data_id, 379);
}

CCZ_TEST(battle_parser_rejects_incomplete_record_length_with_diagnostic) {
    const std::wstring scope = L"battle:A";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_00.eex",
        {container(scope, {instruction(
             0x46, {1, 2, 3}, scope + L"|node:0|instruction:70")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.diagnostics.size() == std::size_t{1});
    CCZ_REQUIRE(result.diagnostics[0].find(L"0x46") != std::wstring::npos);
    CCZ_REQUIRE(result.diagnostics[0].find(L"长度") != std::wstring::npos);
    CCZ_REQUIRE(result.state.units.empty());
}

CCZ_TEST(battle_parser_rejects_fixed_capacity_overflow) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> rows(21 * 11, 0);
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_00.eex",
        {container(scope, {instruction(
             0x46, std::move(rows), scope + L"|node:0|instruction:70")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.state.units.empty());
    CCZ_REQUIRE(result.diagnostics[0].find(L"容量") != std::wstring::npos);
}

CCZ_TEST(battle_parser_rejects_multiple_4b_rows_from_one_source) {
    const std::wstring scope = L"battle:A";
    std::vector<std::int32_t> rows(10, 0);
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_00.eex",
        {container(scope, {instruction(
             0x4b, std::move(rows), scope + L"|node:0|instruction:75")})},
        scoped_options(scope.c_str()));

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.state.units.empty());
    CCZ_REQUIRE(result.diagnostics[0].find(L"0x4B") != std::wstring::npos);
}

CCZ_TEST(battle_parser_does_not_scan_outside_explicit_scope) {
    const std::wstring selected = L"battle:selected";
    const std::wstring other = L"battle:other";
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_13.eex",
        {container(other, {instruction(
             0x27, battle_map_payload(999),
             other + L"|node:0|instruction:39")}),
         container(selected, {instruction(
             0x27, battle_map_payload(42),
             selected + L"|node:0|instruction:39")})},
        scoped_options(selected.c_str()));

    CCZ_REQUIRE(result.state.map.valid);
    CCZ_REQUIRE_EQ(result.state.map.map_id, 42);
    CCZ_REQUIRE(result.diagnostics.empty());
}

CCZ_TEST(battle_parser_matches_editor_logical_key_scope_prefix) {
    const std::wstring scope = L"S_13.eex|document|node:0/0";
    ccz::preview::RawReplayNode section = container(
        scope + L"|instruction:65535",
        {instruction(0x44, {}, scope + L"/0|instruction:68"),
         instruction(0x27, battle_map_payload(13),
                     scope + L"/1|instruction:39")});

    const auto result = ccz::preview::parse_battle_instructions(
        L"S_13.eex", {std::move(section)}, scoped_options(scope.c_str()));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE(result.state.initialized);
    CCZ_REQUIRE(result.state.map.valid);
    CCZ_REQUIRE_EQ(result.state.map.map_id, 13);
}

CCZ_TEST(battle_parser_requires_explicit_scope) {
    const auto result = ccz::preview::parse_battle_instructions(
        L"S_15.eex",
        {instruction(0x27, battle_map_payload(42),
                     L"battle:A|node:0|instruction:39")},
        ccz::preview::BattleParseOptions{});

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.diagnostics.size() == std::size_t{1});
    CCZ_REQUIRE(result.diagnostics[0].find(L"作用域") != std::wstring::npos);
}
