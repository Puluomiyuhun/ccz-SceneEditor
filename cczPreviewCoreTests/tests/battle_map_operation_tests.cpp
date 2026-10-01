#include "test_framework.h"

#include <ccz/preview/battle_preview.h>
#include "../../cczEditor2/PreviewIntegration/BattleMapOperations.h"
#include "../../cczEditor2/PreviewIntegration/BattleWritebackAdapter.h"

#include <cstdint>
#include <vector>

namespace {

using ccz::editor_preview::BattleMapOperationInstruction;
using ccz::editor_preview::BattleObstacleDraftQueue;
using ccz::editor_preview::BattleWritebackChangeKind;
using ccz::editor_preview::BattleWritebackNativeBatch;
using ccz::editor_preview::CapturedEditorNode;
using ccz::editor_preview::CapturedEditorTree;
using ccz::editor_preview::BattlePreviewCapture;
using ccz::preview::BattleCoord;
using ccz::preview::RawReplayNode;

BattlePreviewCapture selected_capture(int selected_id = 14,
                                      bool section_parent = false) {
    BattlePreviewCapture capture;
    capture.ok = true;
    capture.mode.mode = ccz::preview::PreviewMode::Battle;
    capture.input.document_path = L"S_10.eex";
    capture.input.scope_key = L"S|scene:0|section:0";
    capture.snapshot.document_path = capture.input.document_path;
    capture.snapshot.section_path = {0, 0};
    capture.snapshot.selected_handle = 40;
    capture.snapshot.selected_path = {0, 0, 0, 0};

    CapturedEditorNode selected;
    selected.runtime_handle = 40;
    selected.instruction_id = selected_id;
    CapturedEditorNode terminal;
    terminal.runtime_handle = 50;
    terminal.instruction_id = 0;
    if (section_parent) {
        CapturedEditorNode event;
        event.runtime_handle = 60;
        event.instruction_id = 0;
        CapturedEditorNode event_terminal;
        event_terminal.runtime_handle = 61;
        event_terminal.instruction_id = 0;
        event.children.push_back(event_terminal);

        CapturedEditorNode section;
        section.runtime_handle = 20;
        section.instruction_id = -2;
        section.children = {selected, event};
        capture.snapshot.section_root = section;
        capture.snapshot.document_roots = {section};
        capture.snapshot.selected_path = {0};

        RawReplayNode raw_selected;
        raw_selected.runtime_handle = 40;
        raw_selected.logical_key = L"S|scene:0|section:0|instruction:14";
        raw_selected.instruction_id = 14;
        RawReplayNode raw_event_terminal;
        raw_event_terminal.runtime_handle = 61;
        raw_event_terminal.logical_key = L"S|scene:0|section:0|instruction:0/0";
        raw_event_terminal.instruction_id = 0;
        RawReplayNode raw_event;
        raw_event.runtime_handle = 60;
        raw_event.logical_key = L"S|scene:0|section:0|instruction:0";
        raw_event.instruction_id = 0;
        raw_event.children = {raw_event_terminal};
        RawReplayNode raw_section;
        raw_section.runtime_handle = 20;
        raw_section.logical_key = capture.input.scope_key;
        raw_section.instruction_id = 0xffff;
        raw_section.children = {raw_selected, raw_event};
        capture.input.nodes = {raw_section};
        return capture;
    }

    CapturedEditorNode event;
    event.runtime_handle = 30;
    event.instruction_id = 0;
    event.children = {selected, terminal};
    CapturedEditorNode section;
    section.runtime_handle = 20;
    section.instruction_id = -2;
    section.children = {event};
    capture.snapshot.section_root = section;
    capture.snapshot.document_roots = {section};

    RawReplayNode raw_selected;
    raw_selected.runtime_handle = 40;
    raw_selected.logical_key = L"S|scene:0|section:0|instruction:0/0";
    raw_selected.instruction_id = static_cast<std::uint16_t>(selected_id);
    RawReplayNode raw_terminal;
    raw_terminal.runtime_handle = 50;
    raw_terminal.logical_key = L"S|scene:0|section:0|instruction:0/1";
    raw_terminal.instruction_id = 0;
    RawReplayNode raw_event;
    raw_event.runtime_handle = 30;
    raw_event.logical_key = L"S|scene:0|section:0|instruction:0";
    raw_event.instruction_id = 0;
    raw_event.children = {raw_selected, raw_terminal};
    RawReplayNode raw_section;
    raw_section.runtime_handle = 20;
    raw_section.logical_key = capture.input.scope_key;
    raw_section.instruction_id = 0xffff;
    raw_section.children = {raw_event};
    capture.input.nodes = {raw_section};
    return capture;
}

}  // namespace

CCZ_TEST(battle_map_operation_builders_preserve_native_fields) {
    std::wstring diagnostic;
    BattleMapOperationInstruction instruction;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleMoveToInstruction(
        47, BattleCoord{8, 12}, 2, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE_EQ(instruction.instruction_id, std::uint16_t(0x32));
    CCZ_REQUIRE_EQ(instruction.int_data,
                   (std::vector<std::int32_t>{0, 47, 0, 8, 12, 2}));

    CCZ_REQUIRE(ccz::editor_preview::BuildBattleReviveToInstruction(
        103, BattleCoord{4, 7}, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE_EQ(instruction.instruction_id, std::uint16_t(0x55));
    CCZ_REQUIRE_EQ(instruction.int_data,
                   (std::vector<std::int32_t>{0, 103, 0, 4, 7, -1}));

    const std::vector<std::vector<std::int32_t>> expected = {
        {0, 0, 26, 3, 5, 0, 0},
        {1, 0, 27, 3, 5, 0, 0},
        {2, 0, 26, 3, 5, 0, 0},
    };
    for (int type = 0; type < 3; ++type) {
        CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
            type, BattleCoord{3, 5}, 19, 18, instruction, diagnostic));
        CCZ_REQUIRE_EQ(instruction.instruction_id, std::uint16_t(0x58));
        CCZ_REQUIRE_EQ(instruction.int_data,
                       std::vector<std::int32_t>(expected[type].begin(),
                                                  expected[type].end()));
    }
}

CCZ_TEST(battle_map_move_instruction_accepts_default_direction_sentinel) {
    std::wstring diagnostic;
    BattleMapOperationInstruction instruction;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleMoveToInstruction(
        47, BattleCoord{8, 12}, -1, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE_EQ(instruction.int_data,
                   (std::vector<std::int32_t>{0, 47, 0, 8, 12, -1}));
    CCZ_REQUIRE(ccz::editor_preview::ValidateBattleMapOperationInstruction(
        instruction, diagnostic));
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleMoveToInstruction(
        47, BattleCoord{8, 12}, 4, 19, 18, instruction, diagnostic));
}

CCZ_TEST(battle_map_operation_builders_reject_invalid_ids_coordinates_and_types) {
    std::wstring diagnostic;
    BattleMapOperationInstruction instruction;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleMoveToInstruction(
        -1, BattleCoord{0, 0}, 0, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleMoveToInstruction(
        47, BattleCoord{19, 0}, 0, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleReviveToInstruction(
        1024, BattleCoord{0, 0}, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleObstacleInstruction(
        3, BattleCoord{0, 0}, 19, 18, instruction, diagnostic));
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleObstacleInstruction(
        1, BattleCoord{-1, 0}, 19, 18, instruction, diagnostic));
}

CCZ_TEST(battle_obstacle_drafts_keep_click_order_and_allow_occupied_targets) {
    BattleObstacleDraftQueue drafts;
    CCZ_REQUIRE(drafts.Add(2, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(drafts.Add(0, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE_EQ(drafts.items().size(), std::size_t(2));
    CCZ_REQUIRE_EQ(drafts.items()[0].type, 2);
    CCZ_REQUIRE_EQ(drafts.items()[1].type, 0);
    CCZ_REQUIRE(!drafts.Add(3, BattleCoord{5, 4}, 19, 18));
    CCZ_REQUIRE(drafts.Add(1, BattleCoord{18, 17}, 19, 18));
    drafts.Clear();
    CCZ_REQUIRE(drafts.empty());
}

CCZ_TEST(battle_map_writeback_batch_uses_selected_same_level_anchor_in_section) {
    auto capture = selected_capture();
    std::wstring diagnostic;
    std::vector<BattleMapOperationInstruction> instructions(2);
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleMoveToInstruction(
        7, BattleCoord{4, 6}, 2, 19, 18, instructions[0], diagnostic));
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleReviveToInstruction(
        9, BattleCoord{4, 6}, 19, 18, instructions[1], diagnostic));
    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, instructions);
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), std::size_t(2));
    for (const auto& change : batch.changes) {
        CCZ_REQUIRE_EQ(change.kind, BattleWritebackChangeKind::InsertInstruction);
        CCZ_REQUIRE_EQ(change.parent_handle, std::uintptr_t(30));
        CCZ_REQUIRE_EQ(change.insert_after_handle, std::uintptr_t(40));
        CCZ_REQUIRE_EQ(change.insert_before_handle, std::uintptr_t(50));
        CCZ_REQUIRE_EQ(change.insert_terminal_handle, std::uintptr_t(50));
    }
    CCZ_REQUIRE_EQ(batch.changes[0].instruction_id, std::uint16_t(0x32));
    CCZ_REQUIRE_EQ(batch.changes[1].instruction_id, std::uint16_t(0x55));
}

CCZ_TEST(battle_map_writeback_batch_rejects_structural_or_invalid_selected_anchor) {
    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        0, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));
    std::vector<BattleMapOperationInstruction> instructions = {obstacle};
    auto capture = selected_capture(0);
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
        capture, instructions).ok);
    capture = selected_capture();
    instructions[0].instruction_id = 0x4b;
    instructions[0].int_data.assign(20, 0);
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
        capture, instructions).ok);
}

CCZ_TEST(battle_map_writeback_rejects_anchor_inside_internal_info_group) {
    auto capture = selected_capture();
    auto& event = capture.snapshot.document_roots[0].children[0];
    CapturedEditorNode info_group;
    info_group.runtime_handle = 70;
    info_group.instruction_id = 2;
    info_group.long_text = L"我军出场";
    info_group.children = event.children;
    event.children = {info_group};
    event.children[0].children[0].instruction_id = 0x4b;
    event.children[0].children[0].int_data.assign(20, 0);

    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        0, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));
    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, {obstacle});
    CCZ_REQUIRE(!batch.ok);
}

CCZ_TEST(battle_map_writeback_batch_accepts_selected_section_entry_before_event) {
    auto capture = selected_capture(14, true);
    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        0, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));
    const std::vector<BattleMapOperationInstruction> instructions = {obstacle};
    const BattleWritebackNativeBatch batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, instructions);
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes[0].parent_handle, std::uintptr_t(20));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_before_handle, std::uintptr_t(60));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_terminal_handle, std::uintptr_t(60));
}

CCZ_TEST(battle_map_native_insert_is_undoable_but_malformed_node_is_rejected) {
    ccz::editor_preview::BattleWritebackNativeNodeSnapshot node;
    node.handle = 7;
    node.instruction_id = 0x32;
    node.int_data = {0, 22, 0, 3, 4, 1};
    CCZ_REQUIRE(ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node));
    node.int_data.pop_back();
    CCZ_REQUIRE(!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node));
}

CCZ_TEST(battle_map_native_batch_rechecks_dimensions_and_field_semantics) {
    auto capture = selected_capture();
    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        1, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));
    obstacle.int_data[2] = 26;
    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, {obstacle});
    CCZ_REQUIRE(!batch.ok);
}
