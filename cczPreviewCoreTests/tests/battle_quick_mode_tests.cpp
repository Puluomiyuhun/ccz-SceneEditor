#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattlePreviewAdapter.h"
#include "../../cczEditor2/PreviewIntegration/BattleQuickModeResolver.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using ccz::editor_preview::CapturedEditorNode;
using ccz::editor_preview::CapturedEditorTree;

CapturedEditorNode node(std::uintptr_t handle, int instruction_id,
                        std::vector<int> int_data = {},
                        std::vector<CapturedEditorNode> children = {}) {
    CapturedEditorNode result;
    result.runtime_handle = handle;
    result.instruction_id = instruction_id;
    result.int_data = std::move(int_data);
    result.children = std::move(children);
    return result;
}

CapturedEditorNode battle_section(std::uintptr_t section_handle,
                                  std::uintptr_t first_instruction_handle,
                                  int map_id) {
    return node(section_handle, -2, {}, {
        node(first_instruction_handle, 0x44),
        node(first_instruction_handle + 1, 0x27, {3, 0, 0, 0, map_id}),
    });
}

CapturedEditorNode scene(std::uintptr_t handle,
                         std::vector<CapturedEditorNode> sections) {
    return node(handle, -1, {}, std::move(sections));
}

CapturedEditorTree document(std::vector<CapturedEditorNode> scenes) {
    CapturedEditorTree result;
    result.document_path = L"F:/mod/RS/S_10.eex";
    result.document_roots = {node(1, -1, {}, std::move(scenes))};
    return result;
}

}  // namespace

CCZ_TEST(battle_quick_mode_selects_unique_map_section_from_scene1) {
    const auto input = document({
        scene(2, {battle_section(10, 100, 7)}),
        scene(3, {battle_section(20, 200, 9)}),
    });

    const auto result =
        ccz::editor_preview::ResolveScene1BattleSection(input);

    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.section_handle, std::uintptr_t(10));
    CCZ_REQUIRE(result.section_path == (std::vector<std::uint32_t>{0, 0}));
    CCZ_REQUIRE(result.capture.ok);
    CCZ_REQUIRE_EQ(result.capture.snapshot.section_root.runtime_handle,
                   std::uintptr_t(10));
    CCZ_REQUIRE_EQ(result.capture.snapshot.document_roots.size(),
                   std::size_t(1));
}

CCZ_TEST(battle_quick_mode_reuses_one_capture_and_selects_later_valid_scene1_section) {
    const auto input = document({
        scene(2, {node(10, -2, {}, {node(11, 0x44)}),
                  battle_section(20, 200, 9)}),
        scene(3, {battle_section(30, 300, 12)}),
    });

    const auto result =
        ccz::editor_preview::ResolveScene1BattleSection(input);

    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.section_handle, std::uintptr_t(20));
    CCZ_REQUIRE(result.section_path == (std::vector<std::uint32_t>{0, 1}));
    CCZ_REQUIRE(result.capture.ok);
    CCZ_REQUIRE_EQ(result.capture.snapshot.section_root.runtime_handle,
                   std::uintptr_t(20));
    CCZ_REQUIRE(result.capture.input.scope_key.find(L"node:0/0/1") !=
                std::wstring::npos);
}

CCZ_TEST(battle_quick_mode_does_not_search_scene2_when_scene1_has_no_map) {
    const auto input = document({
        scene(2, {node(10, -2, {}, {node(11, 0x44)})}),
        scene(3, {battle_section(20, 200, 9)}),
    });

    const auto result =
        ccz::editor_preview::ResolveScene1BattleSection(input);

    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(result.section_handle == 0);
    CCZ_REQUIRE(result.diagnostic.find(L"Scene1") != std::wstring::npos);
}

CCZ_TEST(battle_quick_mode_rejects_ambiguous_scene1_map_sections) {
    const auto input = document({
        scene(2, {battle_section(10, 100, 7),
                  battle_section(11, 110, 8)}),
        scene(3, {}),
    });

    const auto result =
        ccz::editor_preview::ResolveScene1BattleSection(input);

    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(result.diagnostic.find(L"多个") != std::wstring::npos);
}

CCZ_TEST(battle_quick_mode_rebinds_fresh_document_to_pinned_section) {
    const auto input = document({
        scene(2, {battle_section(10, 100, 7),
                  battle_section(11, 110, 8)}),
    });
    CapturedEditorTree selected;
    std::wstring diagnostic;

    CCZ_REQUIRE(ccz::editor_preview::SelectCapturedBattleSection(
        input, std::uintptr_t(11), {0, 1}, selected, diagnostic));
    CCZ_REQUIRE_EQ(selected.section_root.runtime_handle, std::uintptr_t(11));
    CCZ_REQUIRE(selected.section_path == (std::vector<std::uint32_t>{0, 1}));
    CCZ_REQUIRE_EQ(selected.document_roots.size(), std::size_t(1));
}

CCZ_TEST(battle_quick_mode_tracks_scene1_section_revision_separately) {
    const auto baseline_document = document({
        scene(2, {battle_section(10, 100, 7)}),
        scene(3, {battle_section(20, 200, 9)}),
    });
    CapturedEditorTree baseline_section;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::SelectCapturedBattleSection(
        baseline_document, 10, {0, 0}, baseline_section, diagnostic));
    const auto baseline = ccz::editor_preview::BuildBattlePreviewInput(
        baseline_section);
    CCZ_REQUIRE(baseline.ok);

    auto other_scene_changed_document = baseline_document;
    other_scene_changed_document.document_roots[0].children[1]
        .children[0].children[1].int_data[4] = 10;
    CapturedEditorTree other_scene_changed_section;
    CCZ_REQUIRE(ccz::editor_preview::SelectCapturedBattleSection(
        other_scene_changed_document, 10, {0, 0},
        other_scene_changed_section, diagnostic));
    const auto other_scene_changed =
        ccz::editor_preview::BuildBattlePreviewInput(
            other_scene_changed_section);
    CCZ_REQUIRE(other_scene_changed.ok);
    CCZ_REQUIRE(other_scene_changed.input.document_revision !=
                baseline.input.document_revision);
    CCZ_REQUIRE_EQ(other_scene_changed.input.section_revision,
                   baseline.input.section_revision);

    auto scene1_changed_document = baseline_document;
    scene1_changed_document.document_roots[0].children[0]
        .children[0].children[1].int_data[4] = 11;
    CapturedEditorTree scene1_changed_section;
    CCZ_REQUIRE(ccz::editor_preview::SelectCapturedBattleSection(
        scene1_changed_document, 10, {0, 0}, scene1_changed_section,
        diagnostic));
    const auto scene1_changed = ccz::editor_preview::BuildBattlePreviewInput(
        scene1_changed_section);
    CCZ_REQUIRE(scene1_changed.ok);
    CCZ_REQUIRE(scene1_changed.input.section_revision !=
                baseline.input.section_revision);
}
