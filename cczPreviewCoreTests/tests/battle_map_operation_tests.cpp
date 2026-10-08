#include "test_framework.h"

#include <ccz/preview/battle_preview.h>
#include "../../cczEditor2/PreviewIntegration/BattleMapOperations.h"
#include "../../cczEditor2/PreviewIntegration/BattleEditorOps.h"
#include "../../cczEditor2/PreviewIntegration/BattleWritebackAdapter.h"

#include <cstdint>
#include <algorithm>
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
                                      bool section_parent = false,
                                      std::uint16_t selected_parent_id = 0) {
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
    event.instruction_id = selected_parent_id;
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
    raw_event.instruction_id = selected_parent_id;
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

CCZ_TEST(battle_map_role_operation_batch_preserves_group_offsets_and_is_atomic) {
    using ccz::editor_preview::BattleRoleOperationKind;
    using ccz::editor_preview::BattleRoleOperationSource;
    std::wstring diagnostic;
    std::vector<BattleMapOperationInstruction> instructions;
    std::vector<BattleCoord> targets;
    const std::vector<BattleRoleOperationSource> single_source = {
        {47, BattleCoord{8, 12}, 3},
    };
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::MoveTo, single_source, 0, BattleCoord{9, 11},
        20, 20, 1, instructions, targets, diagnostic));
    CCZ_REQUIRE_EQ(instructions.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(instructions[0].int_data,
        (std::vector<std::int32_t>{0, 47, 0, 9, 11, 1}));
    CCZ_REQUIRE_EQ(single_source[0].direction, 3);

    const std::vector<BattleRoleOperationSource> sources = {
        {10, BattleCoord{4, 5}, 0},
        {11, BattleCoord{6, 5}, 3},
        {12, BattleCoord{4, 7}, 1},
    };

    CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::MoveTo, sources, 1, BattleCoord{9, 10},
        20, 20, std::nullopt, instructions, targets, diagnostic));
    CCZ_REQUIRE_EQ(targets,
        (std::vector<BattleCoord>{{7, 10}, {9, 10}, {7, 12}}));
    CCZ_REQUIRE_EQ(instructions.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(instructions[0].instruction_id, std::uint16_t(0x32));
    CCZ_REQUIRE_EQ(instructions[0].int_data,
        (std::vector<std::int32_t>{0, 10, 0, 7, 10, 0}));
    CCZ_REQUIRE_EQ(instructions[1].int_data,
        (std::vector<std::int32_t>{0, 11, 0, 9, 10, 3}));
    CCZ_REQUIRE_EQ(instructions[2].int_data,
        (std::vector<std::int32_t>{0, 12, 0, 7, 12, 1}));

    CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::MoveTo, sources, 1, BattleCoord{9, 10},
        20, 20, 2, instructions, targets, diagnostic));
    for (const auto& instruction : instructions)
        CCZ_REQUIRE_EQ(instruction.int_data[5], 2);
    CCZ_REQUIRE_EQ(sources[0].direction, 0);
    CCZ_REQUIRE_EQ(sources[1].direction, 3);
    CCZ_REQUIRE_EQ(sources[2].direction, 1);

    // Occupied target/source cells remain valid; game-side avoidance is authoritative.
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::ReviveTo, sources, 1, BattleCoord{4, 5},
        20, 20, std::nullopt, instructions, targets, diagnostic));
    CCZ_REQUIRE_EQ(targets,
        (std::vector<BattleCoord>{{2, 5}, {4, 5}, {2, 7}}));
    CCZ_REQUIRE_EQ(instructions.size(), std::size_t(3));
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        CCZ_REQUIRE_EQ(instructions[index].instruction_id, std::uint16_t(0x55));
        CCZ_REQUIRE_EQ(instructions[index].int_data[5], -1);
    }
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::ReviveTo, sources, 1, BattleCoord{4, 5},
        20, 20, 2, instructions, targets, diagnostic));
    CCZ_REQUIRE_EQ(instructions.size(), std::size_t(3));
    for (const auto& instruction : instructions) {
        CCZ_REQUIRE_EQ(instruction.instruction_id, std::uint16_t(0x55));
        CCZ_REQUIRE_EQ(instruction.int_data[5], 2);
        CCZ_REQUIRE(ccz::editor_preview::ValidateBattleMapOperationInstruction(
            instruction, diagnostic));
    }
    CCZ_REQUIRE_EQ(sources[0].direction, 0);
    CCZ_REQUIRE_EQ(sources[1].direction, 3);
    CCZ_REQUIRE_EQ(sources[2].direction, 1);

    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::ReviveTo, sources, 1, BattleCoord{4, 5},
        20, 20, 4, instructions, targets, diagnostic));
    CCZ_REQUIRE(instructions.empty());
    CCZ_REQUIRE(targets.empty());

    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleRoleOperationBatch(
        BattleRoleOperationKind::MoveTo, sources, 1, BattleCoord{19, 19},
        20, 20, std::nullopt, instructions, targets, diagnostic));
    CCZ_REQUIRE(instructions.empty());
    CCZ_REQUIRE(targets.empty());
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

CCZ_TEST(battle_obstacle_drafts_keep_order_and_reject_same_cell_across_types) {
    BattleObstacleDraftQueue drafts;
    CCZ_REQUIRE(drafts.Add(2, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(!drafts.Add(0, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(!drafts.Add(2, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(drafts.Add(0, BattleCoord{4, 4}, 19, 18));
    CCZ_REQUIRE_EQ(drafts.items().size(), std::size_t(2));
    CCZ_REQUIRE_EQ(drafts.items()[0].type, 2);
    CCZ_REQUIRE_EQ(drafts.items()[1].type, 0);
    CCZ_REQUIRE(!drafts.Add(3, BattleCoord{5, 4}, 19, 18));
    CCZ_REQUIRE(drafts.Add(1, BattleCoord{18, 17}, 19, 18));
    drafts.Clear();
    CCZ_REQUIRE(drafts.empty());
}

CCZ_TEST(battle_obstacle_draft_remove_releases_cell_for_another_type) {
    BattleObstacleDraftQueue drafts;
    CCZ_REQUIRE(drafts.Add(2, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(!drafts.Add(0, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(drafts.Add(1, BattleCoord{8, 9}, 19, 18));

    CCZ_REQUIRE(drafts.RemoveAt(BattleCoord{3, 4}));
    CCZ_REQUIRE_EQ(drafts.items().size(), std::size_t(1));
    CCZ_REQUIRE_EQ(drafts.items()[0].type, 1);
    CCZ_REQUIRE(!drafts.RemoveAt(BattleCoord{3, 4}));
    CCZ_REQUIRE(drafts.Add(0, BattleCoord{3, 4}, 19, 18));
    CCZ_REQUIRE(drafts.RemoveAt(BattleCoord{3, 4}));
    CCZ_REQUIRE(drafts.RemoveAt(BattleCoord{8, 9}));
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

CCZ_TEST(battle_map_writeback_batch_accepts_selected_instruction_inside_condition_subevent) {
    auto capture = selected_capture(14, false, 0x05);
    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        0, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));

    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, {obstacle});
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(batch.changes.front().parent_handle, std::uintptr_t(30));
    CCZ_REQUIRE_EQ(batch.changes.front().insert_after_handle,
                   std::uintptr_t(40));
    CCZ_REQUIRE_EQ(batch.changes.front().insert_before_handle,
                   std::uintptr_t(50));
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

CCZ_TEST(battle_map_writeback_allows_anchor_inside_other_internal_info_subevent) {
    auto capture = selected_capture();
    auto& event = capture.snapshot.document_roots[0].children[0];
    CapturedEditorNode info_group;
    info_group.runtime_handle = 70;
    info_group.instruction_id = 2;
    info_group.long_text = L"事件子分组";
    info_group.children = event.children;
    event.children = {info_group};

    std::wstring diagnostic;
    BattleMapOperationInstruction obstacle;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleObstacleInstruction(
        0, BattleCoord{2, 3}, 19, 18, obstacle, diagnostic));
    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, {obstacle});
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.front().parent_handle, std::uintptr_t(70));
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
    obstacle.int_data[2] = 30;
    const auto batch =
        ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(
            capture, {obstacle});
    CCZ_REQUIRE(!batch.ok);
}

CCZ_TEST(battle_obstacle_defaults_are_snapshotted_and_draft_moves_reject_collisions) {
    using namespace ccz::editor_preview;
    BattleObstacleDraftQueue drafts;
    BattleObstacleDefaults defaults{0,16,1,1};
    CCZ_REQUIRE(drafts.Add(2,{2,3},20,20,defaults));
    defaults.terrain=5;
    CCZ_REQUIRE_EQ(drafts.items()[0].parameters.terrain,16);
    CCZ_REQUIRE(drafts.Add(1,{4,3},20,20));
    auto updated=drafts.items()[0];updated.coordinate={4,3};
    CCZ_REQUIRE(!drafts.UpdateAt({2,3},updated,20,20));
    CCZ_REQUIRE(drafts.items()[0].coordinate==BattleCoord({2,3}));
    updated.coordinate={6,7};updated.type=1;updated.parameters={1,-1,0,1};
    CCZ_REQUIRE(drafts.UpdateAt({2,3},updated,20,20));
    BattleMapOperationInstruction instruction;std::wstring diagnostic;
    CCZ_REQUIRE(BuildBattleObstacleInstruction(updated.type,updated.coordinate,20,20,instruction,diagnostic,updated.parameters));
    CCZ_REQUIRE(instruction.int_data==std::vector<std::int32_t>({1,1,27,6,7,0,1}));
    CCZ_REQUIRE(ValidateBattleMapOperationInstruction(instruction,diagnostic));
    updated.coordinate={-1,7};
    CCZ_REQUIRE(!drafts.UpdateAt({6,7},updated,20,20));
    CCZ_REQUIRE(drafts.items()[0].coordinate==BattleCoord({6,7}));
}

CCZ_TEST(battle_obstacle_brush_path_fills_crossed_cells_without_duplicate_stamps) {
    using namespace ccz::editor_preview;
    BattleObstacleDraftQueue drafts;
    for(const auto& cell:BattleSelectionPathCells({1,2},{9,2}))drafts.Add(0,cell,20,20);
    for(const auto& cell:BattleSelectionPathCells({9,2},{1,2}))drafts.Add(2,cell,20,20);
    CCZ_REQUIRE_EQ(drafts.items().size(),std::size_t(9));
    for(int x=1;x<=9;++x) {
        CCZ_REQUIRE(std::any_of(drafts.items().begin(),drafts.items().end(),[x](const auto& draft){return draft.coordinate==BattleCoord({x,2});}));
    }
}

CCZ_TEST(battle_obstacle_disappearance_builds_new_insert_after_selection) {
    using namespace ccz::editor_preview;
    auto capture=selected_capture();
    BattleMapOperationInstruction hide;std::wstring diagnostic;
    CCZ_REQUIRE(BuildBattleObstacleInstruction(2,{3,4},19,18,hide,diagnostic,{1,16,1,1}));
    const auto batch=BuildBattleWritebackInsertBatchAfterSelection(capture,{hide});
    CCZ_REQUIRE(batch.ok);CCZ_REQUIRE_EQ(batch.changes.size(),std::size_t(1));
    CCZ_REQUIRE(batch.changes[0].kind==BattleWritebackChangeKind::InsertInstruction);
    CCZ_REQUIRE_EQ(batch.changes[0].item_handle,std::uintptr_t(0));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_after_handle,std::uintptr_t(40));
    CCZ_REQUIRE(batch.changes[0].int_data==std::vector<std::int32_t>({2,1,16,3,4,1,1}));
}
CCZ_TEST(battle_turn_action_native_batch_preserves_pair_order_and_undo_contract) {
 auto capture=selected_capture();std::wstring diagnostic;
 BattleMapOperationInstruction a,b,c;
 CCZ_REQUIRE(ccz::editor_preview::BuildBattleCommandInstruction({0x4f,{7,9,-1,0,1,0}},19,18,a,diagnostic));
 CCZ_REQUIRE(ccz::editor_preview::BuildBattleCommandInstruction({0x4f,{9,7,-1,0,0,1}},19,18,b,diagnostic));
 CCZ_REQUIRE(ccz::editor_preview::BuildBattleCommandInstruction({0x50,{7,6,1,1}},19,18,c,diagnostic));
 auto batch=ccz::editor_preview::BuildBattleWritebackInsertBatchAfterSelection(capture,{a,b,c});
 CCZ_REQUIRE(batch.ok);CCZ_REQUIRE_EQ(batch.changes.size(),std::size_t(3));
 CCZ_REQUIRE_EQ(batch.changes[0].int_data,a.int_data);CCZ_REQUIRE_EQ(batch.changes[1].int_data,b.int_data);CCZ_REQUIRE_EQ(batch.changes[2].int_data,c.int_data);
 for(const auto& change:batch.changes){
  ccz::editor_preview::BattleWritebackNativeNodeSnapshot n;n.handle=7;n.instruction_id=change.instruction_id;n.int_data=change.int_data;
  CCZ_REQUIRE(ccz::editor_preview::CanDeleteBattleWritebackNativeNode(n));
 }
}

CCZ_TEST(battle_entry_tests_use_existing_writeback_and_undo) {
 using namespace ccz::editor_preview;
 auto capture=selected_capture();std::wstring diagnostic;BattleMapOperationInstruction a,b;
 CCZ_REQUIRE(BuildBattleCommandInstruction({0x25,{1023,3,4}},19,18,a,diagnostic));
 CCZ_REQUIRE(BuildBattleCommandInstruction({0x26,{999,1,2,7,8}},19,18,b,diagnostic));
 auto batch=BuildBattleWritebackInsertBatchAfterSelection(capture,{a,b});
 CCZ_REQUIRE(batch.ok);CCZ_REQUIRE_EQ(batch.changes.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(batch.changes[0].int_data,a.int_data);CCZ_REQUIRE_EQ(batch.changes[1].int_data,b.int_data);
 for(const auto& change:batch.changes){BattleWritebackNativeNodeSnapshot n;n.handle=7;n.instruction_id=change.instruction_id;n.int_data=change.int_data;CCZ_REQUIRE(CanDeleteBattleWritebackNativeNode(n));}
}
