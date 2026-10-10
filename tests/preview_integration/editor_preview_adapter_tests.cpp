#include "test_framework.h"

#include <PreviewIntegration/EditorPreviewAdapter.h>

#include <cstdint>
#include <string>
#include <utility>

namespace {

using ccz::editor_preview::BuildEditorPreviewInput;
using ccz::editor_preview::BuildEditorPreviewInputForSection;
using ccz::editor_preview::CapturedEditorNode;
using ccz::editor_preview::CapturedEditorTree;
using ccz::editor_preview::CopyLongText;

CapturedEditorTree MakeAdapterFixture(std::uintptr_t runtime_handle) {
    CapturedEditorTree tree;
    tree.document_path = L"D:\\Mod\\R_13.eex";
    tree.section_path = {0, 0};
    tree.selected_path = {0, 0};

    tree.section_root.runtime_handle = runtime_handle + 1;
    tree.section_root.instruction_id = -2;
    tree.section_root.display_text = L"Section";

    CapturedEditorNode boundary;
    boundary.runtime_handle = runtime_handle + 2;
    boundary.instruction_id = 0;
    boundary.ordinal = 0;
    boundary.display_text = L"0:事件结束";

    CapturedEditorNode selected;
    selected.runtime_handle = runtime_handle;
    selected.instruction_id = 0x14;
    selected.ordinal = 1;
    selected.int_data = {7, 8, 9};
    selected.long_text = L"&云夜\\n测试";
    selected.display_text = L"14:对话 &云夜\\n测试";
    boundary.children.push_back(std::move(selected));
    tree.section_root.children.push_back(std::move(boundary));

    CapturedEditorNode document_root;
    document_root.instruction_id = -1;
    document_root.display_text = L"Document";
    document_root.children.push_back(tree.section_root);
    tree.document_roots.push_back(std::move(document_root));
    return tree;
}

CapturedEditorNode MakeWideNode(std::size_t child_count,
                                int instruction_id = 0x14) {
    CapturedEditorNode root;
    root.instruction_id = instruction_id;
    root.children.resize(child_count);
    for (std::size_t index = 0; index < child_count; ++index) {
        root.children[index].instruction_id = 0x14;
        root.children[index].ordinal = static_cast<int>(index);
    }
    return root;
}

CapturedEditorTree MakeEntryBoundaryBodyFixture(
    std::uintptr_t runtime_handle) {
    CapturedEditorTree tree;
    tree.document_path = L"D:\\Mod\\R_13.eex";
    tree.section_path = {0, 0};
    tree.selected_path = {1, 0};

    tree.section_root.runtime_handle = runtime_handle + 10;
    tree.section_root.instruction_id = -2;
    tree.section_root.display_text = L"Section";

    CapturedEditorNode entry;
    entry.runtime_handle = runtime_handle + 11;
    entry.instruction_id = 0x02;
    entry.display_text = L"2:内部信息";

    CapturedEditorNode boundary;
    boundary.runtime_handle = runtime_handle + 12;
    boundary.instruction_id = 0x00;
    boundary.display_text = L"0:事件结束";

    CapturedEditorNode selected_body;
    selected_body.runtime_handle = runtime_handle;
    selected_body.instruction_id = 0x14;
    selected_body.ordinal = 1;
    selected_body.long_text = L"正文";
    selected_body.display_text = L"14:对话 正文";
    boundary.children.push_back(std::move(selected_body));

    tree.section_root.children.push_back(std::move(entry));
    tree.section_root.children.push_back(std::move(boundary));
    CapturedEditorNode document_root;
    document_root.instruction_id = -1;
    document_root.display_text = L"Document";
    document_root.children.push_back(tree.section_root);
    tree.document_roots.push_back(std::move(document_root));
    return tree;
}

CCZ_TEST(editor_preview_adapter_stable_key_ignores_runtime_handle) {
    const auto first = BuildEditorPreviewInput(MakeAdapterFixture(0x1111));
    const auto second = BuildEditorPreviewInput(MakeAdapterFixture(0x9999));

    CCZ_REQUIRE(first.ok);
    CCZ_REQUIRE(second.ok);
    CCZ_REQUIRE_EQ(first.input.section.selected_target_key,
                   second.input.section.selected_target_key);
    CCZ_REQUIRE_EQ(first.input.document_key, second.input.document_key);
    CCZ_REQUIRE_EQ(first.input.document_revision,
                   second.input.document_revision);
}

CCZ_TEST(editor_preview_adapter_preserves_paths_and_structured_values) {
    const auto capture = BuildEditorPreviewInput(MakeAdapterFixture(0x1111));

    CCZ_REQUIRE(capture.ok);
    CCZ_REQUIRE_EQ(capture.input.section.section_key,
                   std::wstring(L"D:\\Mod\\R_13.eex|section:0/0"));
    CCZ_REQUIRE_EQ(capture.input.section.selected_target_key,
                   std::wstring(L"D:\\Mod\\R_13.eex|section:0/0|node:0/0|instruction:20"));
    CCZ_REQUIRE_EQ(capture.input.section.nodes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].instruction_id,
                   static_cast<std::uint16_t>(0x00));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].children.size(),
                   std::size_t(1));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].children[0].command.int_data,
                   std::vector<std::int32_t>({7, 8, 9}));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].children[0].command.text,
                   std::wstring(L"&云夜\\n测试"));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].children[0].source_text,
                   std::wstring(L"14:对话 &云夜\\n测试"));
}

CCZ_TEST(editor_preview_adapter_preserves_entry_boundary_and_body_sequence) {
    const auto capture =
        BuildEditorPreviewInput(MakeEntryBoundaryBodyFixture(0x2211));

    CCZ_REQUIRE(capture.ok);
    CCZ_REQUIRE_EQ(capture.input.section.nodes.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[0].instruction_id,
                   static_cast<std::uint16_t>(0x02));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[1].instruction_id,
                   static_cast<std::uint16_t>(0x00));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[1].children.size(),
                   std::size_t(1));
    CCZ_REQUIRE_EQ(capture.input.section.nodes[1].children[0].instruction_id,
                   static_cast<std::uint16_t>(0x14));
    CCZ_REQUIRE_EQ(capture.input.section.selected_target_key,
                   std::wstring(L"D:\\Mod\\R_13.eex|section:0/0|node:1/0|instruction:20"));
}

CCZ_TEST(editor_preview_adapter_can_build_a_different_section_from_document_snapshot) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    CapturedEditorNode second_section;
    second_section.runtime_handle = 0x2200;
    second_section.instruction_id = -2;
    CapturedEditorNode boundary;
    boundary.runtime_handle = 0x2201;
    boundary.instruction_id = 0;
    CapturedEditorNode actor;
    actor.runtime_handle = 0x2202;
    actor.instruction_id = 0x30;
    actor.int_data = {7, 20, 30, 2, 0};
    boundary.children.push_back(actor);
    second_section.children.push_back(boundary);
    tree.document_roots.front().children.push_back(second_section);

    const auto capture = BuildEditorPreviewInputForSection(
        tree, 0x2200, 0x2202);

    CCZ_REQUIRE(capture.ok);
    CCZ_REQUIRE_EQ(capture.input.section.section_key,
                   std::wstring(L"D:\\Mod\\R_13.eex|section:1"));
    CCZ_REQUIRE_EQ(capture.input.section.selected_target_key,
                   std::wstring(L"D:\\Mod\\R_13.eex|section:1|node:0/0|instruction:48"));
}

CCZ_TEST(editor_preview_snapshot_bounds_long_text_by_capacity) {
    const wchar_t text[] = L"abcdef";
    CCZ_REQUIRE_EQ(CopyLongText(text, 3), std::wstring(L"abc"));
    CCZ_REQUIRE_EQ(CopyLongText(text, 7), std::wstring(L"abcdef"));
    CCZ_REQUIRE_EQ(CopyLongText(nullptr, 7), std::wstring());
    CCZ_REQUIRE_EQ(CopyLongText(text, 0), std::wstring());
}

CCZ_TEST(editor_preview_adapter_document_root_selection_is_idle) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.section_path.clear();
    tree.selected_path.clear();

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(capture.ok);
    CCZ_REQUIRE(capture.idle);
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

CCZ_TEST(editor_preview_adapter_rejects_missing_section_boundary_atomically) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.section_root.children.clear();

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(!capture.ok);
    CCZ_REQUIRE(!capture.idle);
    CCZ_REQUIRE(capture.input.document_key.empty());
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

CCZ_TEST(editor_preview_adapter_rejects_invalid_selected_path_atomically) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.selected_path = {9};

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(!capture.ok);
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

CCZ_TEST(editor_preview_adapter_rejects_depth_limit_without_partial_input) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.selected_path.clear();
    CapturedEditorNode* current = &tree.section_root;
    for (int index = 0; index < 130; ++index) {
        current->children.emplace_back();
        current = &current->children.back();
        current->instruction_id = 0x14;
        current->int_data = {index};
    }

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(!capture.ok);
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

CCZ_TEST(editor_preview_adapter_rejects_node_count_limit_without_partial_input) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.selected_path.clear();
    tree.section_root.children.clear();
    tree.document_roots.clear();
    tree.section_root.children.resize(100001);
    tree.section_root.children[0].instruction_id = 0;
    for (std::size_t index = 1; index < tree.section_root.children.size();
         ++index) {
        tree.section_root.children[index].instruction_id = 0x14;
        tree.section_root.children[index].ordinal = static_cast<int>(index);
    }

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(!capture.ok);
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

CCZ_TEST(editor_preview_adapter_rejects_combined_document_and_section_node_budget_overflow) {
    CapturedEditorTree tree = MakeAdapterFixture(0x1111);
    tree.document_roots.clear();
    tree.document_roots.push_back(MakeWideNode(60000));
    tree.section_root.children.clear();
    tree.section_root.children.resize(40000);
    tree.section_root.children[0].instruction_id = 0;
    for (std::size_t index = 1; index < tree.section_root.children.size();
         ++index) {
        tree.section_root.children[index].instruction_id = 0x14;
        tree.section_root.children[index].ordinal = static_cast<int>(index);
    }

    const auto capture = BuildEditorPreviewInput(tree);
    CCZ_REQUIRE(!capture.ok);
    CCZ_REQUIRE(capture.input.section.nodes.empty());
    CCZ_REQUIRE(capture.input.document_nodes.empty());
}

}  // namespace
