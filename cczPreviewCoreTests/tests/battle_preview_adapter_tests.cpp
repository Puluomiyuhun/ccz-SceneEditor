#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattlePreviewAdapter.h"

#include <cstdint>
#include <utility>

namespace {

using ccz::editor_preview::CapturedEditorNode;

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

CapturedEditorNode section(std::uintptr_t handle,
                           std::vector<CapturedEditorNode> children) {
    return node(handle, -2, {}, std::move(children));
}

CapturedEditorNode document_root(
    std::vector<CapturedEditorNode> children) {
    return node(1, -1, {}, std::move(children));
}

}  // namespace

CCZ_TEST(battle_adapter_preserves_full_document_snapshot_and_revision) {
    ccz::editor_preview::CapturedEditorTree snapshot;
    snapshot.document_path = L"F:/mod/RS/S_00.eex";
    snapshot.section_path = {0};
    snapshot.selected_path = {1};
    snapshot.section_root = section(
        10, {node(11, 0x44, {17, 3}), node(12, 0)});
    snapshot.document_roots = {document_root({
        section(10, {node(11, 0x44, {17, 3}), node(12, 0)}),
        section(20, {node(21, 0x44, {19, 4}), node(22, 0)})})};

    const auto result = ccz::editor_preview::BuildBattlePreviewInput(snapshot);

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE_EQ(result.mode.mode, ccz::preview::PreviewMode::Battle);
    CCZ_REQUIRE_EQ(result.input.nodes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(result.input.nodes[0].instruction_id,
                   static_cast<std::uint16_t>(0xffff));
    CCZ_REQUIRE_EQ(result.input.nodes[0].children.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(result.input.nodes[0].children[0].instruction_id,
                   static_cast<std::uint16_t>(0xffff));
    // The complete document conversion keeps the marker inside the Section,
    // rather than replacing the document with only the selected Section body.
    CCZ_REQUIRE(result.input.nodes[0].children[0].children.size() == 2);
    CCZ_REQUIRE_EQ(result.input.nodes[0].children[0].children[0].instruction_id,
                   static_cast<std::uint16_t>(0x44));
    CCZ_REQUIRE_EQ(result.input.nodes[0].children[0].children[0].runtime_handle,
                   std::uintptr_t(11));
    CCZ_REQUIRE_EQ(result.input.scope_key,
                   std::wstring(L"F:\\mod\\RS\\S_00.eex|document|node:0/0"));
    CCZ_REQUIRE_EQ(result.input.nodes[0].children[0].children[0].command.int_data[1],
                   3);
    CCZ_REQUIRE_EQ(result.snapshot.document_path, snapshot.document_path);
    CCZ_REQUIRE(result.input.document_revision != 0);

    // A native one-step writeback undo is document-content scoped, not
    // selection scoped. Moving to another Section must not stale that undo.
    auto other_section = snapshot;
    other_section.section_path = {1};
    other_section.selected_path = {1, 0};
    other_section.section_root = section(
        20, {node(21, 0x44, {19, 4}), node(22, 0)});
    const auto other_result =
        ccz::editor_preview::BuildBattlePreviewInput(other_section);
    CCZ_REQUIRE(other_result.ok);
    CCZ_REQUIRE_EQ(other_result.input.document_revision,
                   result.input.document_revision);
}

CCZ_TEST(battle_adapter_rejects_idle_snapshot_without_section) {
    ccz::editor_preview::CapturedEditorTree snapshot;
    snapshot.document_path = L"F:/mod/RS/S_00.eex";
    const auto result = ccz::editor_preview::BuildBattlePreviewInput(snapshot);

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.diagnostic.find(L"Section") != std::wstring::npos);
}
