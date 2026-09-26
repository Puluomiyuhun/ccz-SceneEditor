#include "test_framework.h"

#include <ccz/preview/battle_mode_classifier.h>

#include <utility>

namespace {

ccz::preview::RawReplayNode node(
    std::uint16_t instruction_id,
    std::vector<ccz::preview::RawReplayNode> children = {}) {
    ccz::preview::RawReplayNode result;
    result.instruction_id = instruction_id;
    result.children = std::move(children);
    return result;
}

}  // namespace

CCZ_TEST(battle_mode_requires_s_prefix_and_known_marker) {
    using ccz::preview::PreviewMode;

    const auto result = ccz::preview::classify_preview_mode(
        L"F:/mod/RS/S_00.eex", {node(0x44)});

    CCZ_REQUIRE_EQ(result.mode, PreviewMode::Battle);
    CCZ_REQUIRE(result.prefix_candidate);
    CCZ_REQUIRE(result.marker_valid);
    CCZ_REQUIRE(result.diagnostic.empty());
}

CCZ_TEST(battle_mode_recognizes_nested_marker) {
    const auto result = ccz::preview::classify_preview_mode(
        L"F:\\mod\\RS\\s-13.eex", {node(0x02, {node(0x4b)})});

    CCZ_REQUIRE_EQ(result.mode, ccz::preview::PreviewMode::Battle);
    CCZ_REQUIRE(result.marker_valid);
}

CCZ_TEST(battle_mode_rejects_s_document_without_marker) {
    const auto result = ccz::preview::classify_preview_mode(
        L"F:/mod/RS/S_15.eex", {node(0x27), node(0x79)});

    CCZ_REQUIRE_EQ(result.mode, ccz::preview::PreviewMode::Unresolved);
    CCZ_REQUIRE(result.prefix_candidate);
    CCZ_REQUIRE(!result.marker_valid);
    CCZ_REQUIRE(result.diagnostic.find(L"缺少") != std::wstring::npos);
}

CCZ_TEST(battle_marker_does_not_change_r_document_route) {
    const auto result = ccz::preview::classify_preview_mode(
        L"F:/mod/RS/R_13.eex", {node(0x46)});

    CCZ_REQUIRE_EQ(result.mode, ccz::preview::PreviewMode::Ordinary);
    CCZ_REQUIRE(!result.prefix_candidate);
    CCZ_REQUIRE(result.marker_valid);
    CCZ_REQUIRE(result.diagnostic.empty());
}

CCZ_TEST(battle_marker_in_unknown_document_is_unresolved) {
    const auto result = ccz::preview::classify_preview_mode(
        L"F:/mod/RS/custom.eex", {node(0x47)});

    CCZ_REQUIRE_EQ(result.mode, ccz::preview::PreviewMode::Unresolved);
    CCZ_REQUIRE(!result.prefix_candidate);
    CCZ_REQUIRE(result.marker_valid);
    CCZ_REQUIRE(result.diagnostic.find(L"前缀") != std::wstring::npos);
}
