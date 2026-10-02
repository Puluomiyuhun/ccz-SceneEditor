#include "test_framework.h"

#include <ccz/preview/battle_field_spec.h>
#include <ccz/preview/battle_preview.h>
#include "../../cczEditor2/PreviewIntegration/BattlePreviewAdapter.h"
#include "../../cczEditor2/PreviewIntegration/BattleEditorOps.h"
#include "../../cczEditor2/PreviewIntegration/BattleMapOperations.h"
#include "../../cczEditor2/PreviewIntegration/BattleWritebackAdapter.h"
#include "../../cczEditor2/PreviewIntegration/BattleWritebackQaGate.h"
#include "../../cczEditor2/PreviewIntegration/PreviewWritebackGate.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace {

using ccz::editor_preview::BattleWritebackEvidence;
using ccz::editor_preview::BattleWritebackChangeKind;
using ccz::editor_preview::BattleWritebackSectionSnapshot;
using ccz::editor_preview::BattleWritebackSourceInstruction;
using ccz::editor_preview::BattleEditorOps;
using ccz::editor_preview::BattleEditorState;
using ccz::preview::BattlePreviewState;
using ccz::preview::BattleUnit;
using ccz::editor_preview::BattlePreviewCapture;
using ccz::editor_preview::CapturedEditorNode;
using ccz::editor_preview::CapturedEditorTree;
using ccz::preview::RawReplayNode;

CCZ_TEST(battle_writeback_shared_gate_rejects_insertion_after_event_end_or_subevent_setting) {
    using ccz::editor_preview::IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting;
    CCZ_REQUIRE(IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(1));
    CCZ_REQUIRE(IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(0));
    CCZ_REQUIRE(!IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(2));
    CCZ_REQUIRE(!IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(3));
    CCZ_REQUIRE(!IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(48));
}

BattlePreviewCapture writeback_capture(std::uint64_t revision) {
    constexpr std::uintptr_t kSectionHandle = 100;
    constexpr std::uintptr_t kEventHandle = 101;
    constexpr std::uintptr_t kUnitHandle = 102;
    constexpr std::uintptr_t kEndHandle = 103;
    constexpr const wchar_t* kScope = L"S|scene:0|section:0";
    constexpr const wchar_t* kEventKey = L"S|scene:0|section:0|instruction:0";
    constexpr const wchar_t* kUnitKey =
        L"S|scene:0|section:0|instruction:0/0|instruction:75";
    constexpr const wchar_t* kEndKey =
        L"S|scene:0|section:0|instruction:0/1|instruction:0";

    CapturedEditorNode unit;
    unit.runtime_handle = kUnitHandle;
    unit.instruction_id = 0x4b;
    unit.ordinal = 17;
    unit.int_data.assign(20, 0);
    unit.int_data[0] = 42;

    CapturedEditorNode event;
    event.runtime_handle = kEventHandle;
    event.instruction_id = 0;
    event.ordinal = 8;
    event.children.push_back(unit);
    CapturedEditorNode terminal;
    terminal.runtime_handle = kEndHandle;
    terminal.instruction_id = 0;
    terminal.ordinal = 18;
    event.children.push_back(terminal);

    CapturedEditorNode section;
    section.runtime_handle = kSectionHandle;
    section.instruction_id = -2;
    section.children.push_back(event);

    const auto raw_node = [](std::uintptr_t handle, const wchar_t* key,
                             std::uint16_t instruction,
                             const std::vector<int>& data,
                             std::vector<RawReplayNode> children = {}) {
        RawReplayNode result;
        result.runtime_handle = handle;
        result.logical_key = key;
        result.instruction_id = instruction;
        result.command.instruction_id = instruction;
        result.command.int_data.assign(data.begin(), data.end());
        result.children = std::move(children);
        return result;
    };
    RawReplayNode raw_unit = raw_node(
        kUnitHandle, kUnitKey, 0x4b, unit.int_data);
    RawReplayNode raw_terminal = raw_node(
        kEndHandle, kEndKey, 0, terminal.int_data);
    RawReplayNode raw_event = raw_node(
        kEventHandle, kEventKey, 0, event.int_data,
        {std::move(raw_unit), std::move(raw_terminal)});
    RawReplayNode raw_section = raw_node(
        kSectionHandle, L"S|scene:0|section:0|instruction:65535",
        0xffff, section.int_data, {std::move(raw_event)});

    BattlePreviewCapture capture;
    capture.ok = true;
    capture.input.document_path = L"S_10.eex";
    capture.input.scope_key = kScope;
    capture.input.document_revision = revision;
    capture.input.section_revision = 88;
    capture.input.nodes.push_back(std::move(raw_section));
    capture.snapshot.document_path = capture.input.document_path;
    capture.snapshot.section_path = {0, 0};
    capture.snapshot.section_root = std::move(section);
    capture.mode.mode = ccz::preview::PreviewMode::Battle;
    return capture;
}

void append_writeback_deployment(BattlePreviewCapture& capture,
                                 std::uint16_t instruction_id,
                                 std::uintptr_t handle, int ordinal,
                                 const wchar_t* key) {
    CapturedEditorNode captured;
    captured.runtime_handle = handle;
    captured.instruction_id = instruction_id;
    captured.ordinal = ordinal;
    captured.int_data.assign(instruction_id == 0x4b ? 20 : 1000, 0);

    RawReplayNode raw;
    raw.runtime_handle = handle;
    raw.logical_key = key;
    raw.instruction_id = instruction_id;
    raw.command.instruction_id = instruction_id;
    raw.command.int_data = captured.int_data;

    auto& raw_children = capture.input.nodes[0].children[0].children;
    auto& captured_children = capture.snapshot.section_root.children[0].children;
    raw_children.insert(raw_children.end() - 1, std::move(raw));
    captured_children.insert(captured_children.end() - 1,
                             std::move(captured));
}

BattleWritebackEvidence closed_evidence() {
    BattleWritebackEvidence evidence;
    evidence.field_table_verified = true;
    evidence.sample_manifest_verified = true;
    evidence.group_order_verified = true;
    evidence.native_reload_verified = true;
    evidence.kind300_external_acceptance_verified = true;
    return evidence;
}

BattleUnit row(std::uint16_t instruction, std::size_t index,
               const wchar_t* source = L"section|instruction:70") {
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    BattleUnit unit;
    unit.source_instruction = instruction;
    unit.source_key = source;
    unit.record_index = index;
    unit.page = 0;
    unit.fields_decoded = true;
    unit.raw_fields.assign(spec->record_stride, 0);
    unit.data_id = 100 + static_cast<int>(index);
    unit.position = {10 + static_cast<int>(index), 20};
    unit.direction = 3;
    unit.hidden = false;
    unit.level = 5;
    unit.unit_type = 2;
    unit.ai_policy = 1;
    unit.target = -1;
    unit.target_x = 0;
    unit.target_y = 0;
    return unit;
}

BattlePreviewState fixed_state(std::uint16_t instruction) {
    BattlePreviewState state;
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    for (std::size_t index = 0; index < spec->record_capacity; ++index) {
        state.units.push_back(row(instruction, index));
    }
    return state;
}

void append_fixed(BattlePreviewState& state, std::uint16_t instruction,
                  const wchar_t* source, std::size_t page) {
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    for (std::size_t index = 0; index < spec->record_capacity; ++index) {
        auto unit = row(instruction, index, source);
        unit.page = static_cast<int>(page);
        state.units.push_back(std::move(unit));
    }
}

BattleWritebackSectionSnapshot section_snapshot() {
    BattleWritebackSectionSnapshot snapshot;
    snapshot.scope_key = L"scope";
    snapshot.insert_parent_key = L"scope|event-body";
    snapshot.insert_before_key = L"scope|event-end";
    return snapshot;
}

BattleWritebackSourceInstruction four_b_source(const wchar_t* key,
                                                int ordinal);
BattleUnit existing_four_b(const wchar_t* key);

CCZ_TEST(battle_writeback_capture_builds_scoped_snapshot_and_insert_anchors) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;

    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE_EQ(snapshot.scope_key, baseline.input.scope_key);
    CCZ_REQUIRE_EQ(snapshot.instructions.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].logical_key,
                   std::wstring(L"S|scene:0|section:0|instruction:0/0|instruction:75"));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].ordinal, 17);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].origin_int_data[0], 42);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].current_int_data[0], 42);
    CCZ_REQUIRE_EQ(snapshot.insert_parent_key,
                   std::wstring(L"S|scene:0|section:0|instruction:0"));
    CCZ_REQUIRE_EQ(snapshot.insert_before_key,
                   std::wstring(L"S|scene:0|section:0|instruction:0/1|instruction:0"));
}

CCZ_TEST(battle_writeback_capture_collects_each_deployment_kind_in_scope) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    constexpr const wchar_t* k46Key =
        L"S|scene:0|section:0|instruction:0/1|instruction:70";
    constexpr const wchar_t* k47Key =
        L"S|scene:0|section:0|instruction:0/2|instruction:71";
    append_writeback_deployment(baseline, 0x46, 104, 19, k46Key);
    append_writeback_deployment(current, 0x46, 104, 19, k46Key);
    append_writeback_deployment(baseline, 0x47, 105, 20, k47Key);
    append_writeback_deployment(current, 0x47, 105, 20, k47Key);

    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE_EQ(snapshot.instructions.size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(snapshot.instructions[1].instruction_id, 0x46);
    CCZ_REQUIRE_EQ(snapshot.instructions[1].ordinal, 19);
    CCZ_REQUIRE_EQ(snapshot.instructions[1].origin_int_data.size(),
                   static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(snapshot.instructions[2].instruction_id, 0x47);
    CCZ_REQUIRE_EQ(snapshot.instructions[2].ordinal, 20);
    CCZ_REQUIRE_EQ(snapshot.instructions[2].origin_int_data.size(),
                   static_cast<std::size_t>(1000));
}

CCZ_TEST(battle_writeback_capture_limits_nodes_and_anchor_to_active_child_event) {
    auto add_condition_event = [](BattlePreviewCapture& capture) {
        constexpr std::uintptr_t kConditionHandle = 110;
        constexpr std::uintptr_t kUnitHandle = 111;
        constexpr std::uintptr_t kTerminalHandle = 112;
        const std::wstring condition_key =
            L"S|scene:0|section:0|instruction:0/1|instruction:54";
        const std::wstring unit_key = condition_key + L"/0|instruction:70";
        const std::wstring terminal_key = condition_key + L"/1|instruction:0";

        CapturedEditorNode unit;
        unit.runtime_handle = kUnitHandle;
        unit.instruction_id = 0x46;
        unit.ordinal = 20;
        unit.int_data.assign(1000, 0);
        CapturedEditorNode terminal;
        terminal.runtime_handle = kTerminalHandle;
        terminal.instruction_id = 0;
        terminal.ordinal = 21;
        CapturedEditorNode condition;
        condition.runtime_handle = kConditionHandle;
        condition.instruction_id = 0x36;
        condition.ordinal = 19;
        condition.children = {unit, terminal};
        auto& captured_children =
            capture.snapshot.section_root.children[0].children;
        captured_children.insert(captured_children.end() - 1,
                                 std::move(condition));

        RawReplayNode raw_unit;
        raw_unit.runtime_handle = kUnitHandle;
        raw_unit.logical_key = unit_key;
        raw_unit.instruction_id = 0x46;
        raw_unit.command.instruction_id = 0x46;
        raw_unit.command.int_data.assign(1000, 0);
        RawReplayNode raw_terminal;
        raw_terminal.runtime_handle = kTerminalHandle;
        raw_terminal.logical_key = terminal_key;
        raw_terminal.instruction_id = 0;
        raw_terminal.command.instruction_id = 0;
        RawReplayNode raw_condition;
        raw_condition.runtime_handle = kConditionHandle;
        raw_condition.logical_key = condition_key;
        raw_condition.instruction_id = 0x36;
        raw_condition.command.instruction_id = 0x36;
        raw_condition.children = {std::move(raw_unit), std::move(raw_terminal)};
        auto& raw_children = capture.input.nodes[0].children[0].children;
        raw_children.insert(raw_children.end() - 1, std::move(raw_condition));
    };
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    add_condition_event(baseline);
    add_condition_event(current);

    ccz::editor_preview::BattleWritebackReachabilityScope active_scope;
    active_scope.filter_deployments = true;
    active_scope.reachable_deployment_keys = {
        L"S|scene:0|section:0|instruction:0/1|instruction:54/0|instruction:70"};
    active_scope.insertion_anchors.push_back({0x46,
        L"S|scene:0|section:0|instruction:0/1|instruction:54",
        L"S|scene:0|section:0|instruction:0/1|instruction:54/1|instruction:0",
        true, {}});

    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic,
        ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection,
        &active_scope));
    CCZ_REQUIRE_EQ(snapshot.instructions.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].instruction_id, 0x46);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].logical_key,
                   active_scope.reachable_deployment_keys[0]);
    CCZ_REQUIRE(snapshot.insert_parent_key.empty());
    CCZ_REQUIRE(snapshot.insert_before_key.empty());
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors[0].instruction_id, 0x46);
    CCZ_REQUIRE(snapshot.insertion_anchors[0].resolved);
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors[0].parent_key,
                   active_scope.insertion_anchors[0].parent_key);
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors[0].before_key,
                   active_scope.insertion_anchors[0].before_key);

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x46;
    insert.insert_parent_key = snapshot.insertion_anchors[0].parent_key;
    insert.insert_before_key = snapshot.insertion_anchors[0].before_key;
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));
    const auto native_batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, current, current.input.scope_key);
    CCZ_REQUIRE(native_batch.ok);
    CCZ_REQUIRE_EQ(native_batch.changes[0].parent_handle,
                   std::uintptr_t(110));
}

CCZ_TEST(battle_writeback_capture_leaves_unknown_insert_layout_unanchored) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    baseline.input.nodes[0].children[0].children.pop_back();
    baseline.snapshot.section_root.children[0].children.pop_back();
    current.input.nodes[0].children[0].children.pop_back();
    current.snapshot.section_root.children[0].children.pop_back();

    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE(snapshot.insert_parent_key.empty());
    CCZ_REQUIRE(snapshot.insert_before_key.empty());
}

CCZ_TEST(battle_writeback_capture_rejects_stale_or_cross_scope_snapshot) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(78);
    BattleWritebackSectionSnapshot snapshot;
    snapshot.scope_key = L"must-be-cleared";
    std::wstring diagnostic;

    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE(snapshot.instructions.empty());
    CCZ_REQUIRE(snapshot.scope_key.empty());

    current = writeback_capture(77);
    current.input.section_revision = 99;
    current.snapshot.section_root.children[0].children[0].int_data[1] = 6;
    current.input.nodes[0].children[0].children[0].command.int_data[1] = 6;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic,
        ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection));
    CCZ_REQUIRE_EQ(snapshot.instructions.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].origin_int_data[1], 0);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].current_int_data[1], 6);

    current = writeback_capture(77);
    current.input.scope_key = L"S|scene:1|section:0";
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE(snapshot.instructions.empty());
}

CCZ_TEST(battle_writeback_plan_merges_disjoint_live_and_preview_fields) {
    BattlePreviewState state;
    BattleUnit unit = existing_four_b(L"scope|unit-4b");
    unit.editor_cache_id = 1;
    unit.position = {9, 4};
    ccz::preview::mark_battle_field_dirty(
        unit, ccz::preview::BattleFieldRole::X);
    state.units.push_back(unit);

    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = 0;
    source.origin_int_data[1] = 3;
    source.origin_int_data[2] = 4;
    source.origin_int_data[3] = 1;
    source.origin_int_data[4] = 0;
    source.current_int_data = source.origin_int_data;
    source.current_int_data[2] = 6;  // User changed Y in the script tree.
    auto snapshot = section_snapshot();
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[1], 9);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 6);
}

CCZ_TEST(battle_writeback_plan_rejects_true_same_field_conflict) {
    BattlePreviewState state;
    BattleUnit unit = existing_four_b(L"scope|unit-4b");
    unit.editor_cache_id = 1;
    unit.position = {9, 4};
    ccz::preview::mark_battle_field_dirty(
        unit, ccz::preview::BattleFieldRole::X);
    state.units.push_back(unit);

    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = 0;
    source.origin_int_data[1] = 3;
    source.origin_int_data[2] = 4;
    source.origin_int_data[3] = 1;
    source.origin_int_data[4] = 0;
    source.current_int_data = source.origin_int_data;
    source.current_int_data[1] = 8;  // Both sides changed PositionX.
    auto snapshot = section_snapshot();
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"冲突") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_capture_uses_selected_instruction_when_no_deployments_exist) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    auto make_empty = [](BattlePreviewCapture& capture) {
        auto& event = capture.snapshot.section_root.children[0];
        auto& raw_event = capture.input.nodes[0].children[0];
        event.children.erase(event.children.begin());
        raw_event.children.erase(raw_event.children.begin());

        CapturedEditorNode selected;
        selected.runtime_handle = 104;
        selected.instruction_id = 0x27;
        selected.ordinal = 17;
        selected.int_data.assign(20, 0);
        RawReplayNode raw_selected;
        raw_selected.runtime_handle = 104;
        raw_selected.logical_key =
            L"S|scene:0|section:0|instruction:0/0|instruction:39";
        raw_selected.instruction_id = 0x27;
        raw_selected.command.instruction_id = 0x27;
        raw_selected.command.int_data.assign(20, 0);
        event.children.insert(event.children.begin(), selected);
        raw_event.children.insert(raw_event.children.begin(), raw_selected);
        capture.snapshot.selected_path = {0, 0};
    };
    make_empty(baseline);
    make_empty(current);

    ccz::editor_preview::BattleWritebackReachabilityScope reachability;
    reachability.filter_deployments = true;
    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic,
        ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection,
        &reachability));
    CCZ_REQUIRE_EQ(snapshot.insert_parent_key,
        std::wstring(L"S|scene:0|section:0|instruction:0"));
    CCZ_REQUIRE_EQ(snapshot.insert_before_key,
        std::wstring(L"S|scene:0|section:0|instruction:0/1|instruction:0"));
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors.size(), std::size_t(3));
    CCZ_REQUIRE(std::all_of(snapshot.insertion_anchors.begin(),
        snapshot.insertion_anchors.end(), [](const auto& anchor) {
            return anchor.resolved;
        }));
}

CCZ_TEST(battle_writeback_capture_uses_selected_anchor_without_condition_scope) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(77);
    for (BattlePreviewCapture* capture : {&baseline, &current}) {
        auto& event = capture->snapshot.section_root.children[0];
        auto& raw_event = capture->input.nodes[0].children[0];
        event.children.erase(event.children.begin());
        raw_event.children.erase(raw_event.children.begin());

        CapturedEditorNode selected;
        selected.runtime_handle = 104;
        selected.instruction_id = 0x27;
        selected.ordinal = 17;
        selected.int_data.assign(20, 0);
        RawReplayNode raw_selected;
        raw_selected.runtime_handle = 104;
        raw_selected.logical_key =
            L"S|scene:0|section:0|instruction:0/0|instruction:39";
        raw_selected.instruction_id = 0x27;
        raw_selected.command.instruction_id = 0x27;
        raw_selected.command.int_data.assign(20, 0);
        event.children.insert(event.children.begin(), selected);
        raw_event.children.insert(raw_event.children.begin(), raw_selected);
        capture->snapshot.selected_path = {0, 0};
    }

    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic,
        ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection));
    CCZ_REQUIRE_EQ(snapshot.insert_parent_key,
        std::wstring(L"S|scene:0|section:0|instruction:0"));
    CCZ_REQUIRE_EQ(snapshot.insert_before_key,
        std::wstring(L"S|scene:0|section:0|instruction:0/1|instruction:0"));
    CCZ_REQUIRE_EQ(snapshot.insertion_anchors.size(), std::size_t(3));
    CCZ_REQUIRE(std::all_of(snapshot.insertion_anchors.begin(),
        snapshot.insertion_anchors.end(), [](const auto& anchor) {
            return anchor.resolved;
        }));
}

CCZ_TEST(battle_writeback_capture_allows_changes_outside_pinned_section) {
    auto baseline = writeback_capture(77);
    auto current = writeback_capture(78);
    BattleWritebackSectionSnapshot snapshot;
    std::wstring diagnostic;

    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic));
    CCZ_REQUIRE(snapshot.instructions.empty());

    CCZ_REQUIRE(ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
        baseline, current, snapshot, diagnostic,
        ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection));
    CCZ_REQUIRE_EQ(snapshot.instructions.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(snapshot.instructions[0].origin_int_data[0], 42);
    CCZ_REQUIRE_EQ(snapshot.instructions[0].current_int_data[0], 42);
}

CCZ_TEST(battle_writeback_native_batch_resolves_all_handles_before_mutation) {
    const auto capture = writeback_capture(77);
    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;

    ccz::editor_preview::BattleWritebackChange update;
    update.kind = BattleWritebackChangeKind::UpdateInstruction;
    update.instruction_id = 0x4b;
    update.source_key =
        L"S|scene:0|section:0|instruction:0/0|instruction:75";
    update.source_ordinal = 17;
    update.int_data.assign(20, 0);
    update.int_data[0] = 43;
    plan.changes.push_back(update);

    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x46;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key =
        L"S|scene:0|section:0|instruction:0/1|instruction:0";
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(insert);

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");

    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), static_cast<std::size_t>(2));
    CCZ_REQUIRE_EQ(batch.changes[0].item_handle, std::uintptr_t(102));
    CCZ_REQUIRE_EQ(batch.changes[0].instruction_id, std::uint16_t(0x4b));
    CCZ_REQUIRE_EQ(batch.changes[1].parent_handle, std::uintptr_t(101));
    // The plan's "insert before terminal" anchor is converted to TreeView's
    // "insert after previous sibling" contract before any tree mutation.
    CCZ_REQUIRE_EQ(batch.changes[1].insert_after_handle, std::uintptr_t(102));
    CCZ_REQUIRE_EQ(batch.changes[1].int_data.size(), static_cast<std::size_t>(1000));
}

CCZ_TEST(battle_writeback_native_batch_rejects_stale_or_duplicate_nodes_atomically) {
    auto capture = writeback_capture(77);
    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    for (int i = 0; i < 2; ++i) {
        ccz::editor_preview::BattleWritebackChange update;
        update.kind = BattleWritebackChangeKind::UpdateInstruction;
        update.instruction_id = 0x4b;
        update.source_key =
            L"S|scene:0|section:0|instruction:0/0|instruction:75";
        update.source_ordinal = 17;
        update.int_data.assign(20, i);
        plan.changes.push_back(std::move(update));
    }

    auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(!batch.ok);
    CCZ_REQUIRE(batch.changes.empty());

    plan.changes.pop_back();
    plan.changes[0].source_ordinal = 18;
    batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(!batch.ok);
    CCZ_REQUIRE(batch.changes.empty());
}

CCZ_TEST(battle_writeback_native_batch_accepts_selected_sibling_insert_anchor) {
    const auto capture = writeback_capture(77);
    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x47;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key =
        L"S|scene:0|section:0|instruction:0/0|instruction:75";
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_before_handle, std::uintptr_t(102));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_terminal_handle, std::uintptr_t(103));
}

CCZ_TEST(battle_writeback_native_batch_rejects_insert_after_subevent_marker) {
    auto capture = writeback_capture(77);
    constexpr std::uintptr_t kMarkerHandle = 104;
    constexpr const wchar_t* kMarkerKey =
        L"S|scene:0|section:0|instruction:0/9|instruction:1";
    auto& captured_children =
        capture.snapshot.section_root.children[0].children;
    auto& raw_children = capture.input.nodes[0].children[0].children;
    captured_children.back().ordinal = 19;

    CapturedEditorNode marker;
    marker.runtime_handle = kMarkerHandle;
    marker.instruction_id = 1;
    marker.ordinal = 18;
    captured_children.insert(captured_children.end() - 1, marker);

    RawReplayNode raw_marker;
    raw_marker.runtime_handle = kMarkerHandle;
    raw_marker.logical_key = kMarkerKey;
    raw_marker.instruction_id = 1;
    raw_marker.command.instruction_id = 1;
    raw_children.insert(raw_children.end() - 1, std::move(raw_marker));

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x47;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key =
        L"S|scene:0|section:0|instruction:0/1|instruction:0";
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(!batch.ok);
    CCZ_REQUIRE(batch.diagnostic.find(L"子事件设定") != std::wstring::npos);
    CCZ_REQUIRE(batch.changes.empty());
}

CCZ_TEST(battle_writeback_native_batch_rejects_insert_after_event_end_marker) {
    auto capture = writeback_capture(80);
    constexpr std::uintptr_t kMarkerHandle = 104;
    constexpr const wchar_t* kMarkerKey =
        L"S|scene:0|section:0|instruction:0/9|instruction:0";
    auto& captured_children =
        capture.snapshot.section_root.children[0].children;
    auto& raw_children = capture.input.nodes[0].children[0].children;
    captured_children.back().ordinal = 19;

    CapturedEditorNode marker;
    marker.runtime_handle = kMarkerHandle;
    marker.instruction_id = 0;
    marker.ordinal = 18;
    captured_children.insert(captured_children.end() - 1, marker);

    RawReplayNode raw_marker;
    raw_marker.runtime_handle = kMarkerHandle;
    raw_marker.logical_key = kMarkerKey;
    raw_marker.instruction_id = 0;
    raw_marker.command.instruction_id = 0;
    raw_children.insert(raw_children.end() - 1, std::move(raw_marker));

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x47;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key =
        L"S|scene:0|section:0|instruction:0/1|instruction:0";
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(!batch.ok);
    CCZ_REQUIRE(batch.diagnostic.find(L"事件结束") != std::wstring::npos);
    CCZ_REQUIRE(batch.changes.empty());
}

CCZ_TEST(battle_writeback_native_batch_allows_insert_before_subevent_marker) {
    auto capture = writeback_capture(78);
    constexpr std::uintptr_t kMarkerHandle = 104;
    constexpr const wchar_t* kMarkerKey =
        L"S|scene:0|section:0|instruction:0/9|instruction:1";
    auto& captured_children =
        capture.snapshot.section_root.children[0].children;
    auto& raw_children = capture.input.nodes[0].children[0].children;

    CapturedEditorNode marker;
    marker.runtime_handle = kMarkerHandle;
    marker.instruction_id = 1;
    marker.ordinal = 19;
    captured_children.insert(captured_children.end() - 1, marker);

    RawReplayNode raw_marker;
    raw_marker.runtime_handle = kMarkerHandle;
    raw_marker.logical_key = kMarkerKey;
    raw_marker.instruction_id = 1;
    raw_marker.command.instruction_id = 1;
    raw_children.insert(raw_children.end() - 1, std::move(raw_marker));

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x47;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key = kMarkerKey;
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_before_handle, kMarkerHandle);
    CCZ_REQUIRE_EQ(batch.changes[0].insert_after_handle, std::uintptr_t(102));
}

CCZ_TEST(battle_writeback_native_batch_allows_insert_before_event_end_marker) {
    auto capture = writeback_capture(81);
    constexpr std::uintptr_t kMarkerHandle = 104;
    constexpr const wchar_t* kMarkerKey =
        L"S|scene:0|section:0|instruction:0/9|instruction:0";
    auto& captured_children =
        capture.snapshot.section_root.children[0].children;
    auto& raw_children = capture.input.nodes[0].children[0].children;

    CapturedEditorNode marker;
    marker.runtime_handle = kMarkerHandle;
    marker.instruction_id = 0;
    marker.ordinal = 19;
    captured_children.insert(captured_children.end() - 1, marker);

    RawReplayNode raw_marker;
    raw_marker.runtime_handle = kMarkerHandle;
    raw_marker.logical_key = kMarkerKey;
    raw_marker.instruction_id = 0;
    raw_marker.command.instruction_id = 0;
    raw_children.insert(raw_children.end() - 1, std::move(raw_marker));

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertInstruction;
    insert.instruction_id = 0x47;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key = kMarkerKey;
    insert.source_ordinal = -1;
    insert.int_data.assign(1000, 0);
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(batch.ok);
    CCZ_REQUIRE_EQ(batch.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(batch.changes[0].insert_before_handle, kMarkerHandle);
    CCZ_REQUIRE_EQ(batch.changes[0].insert_after_handle, std::uintptr_t(102));
}

CCZ_TEST(battle_writeback_native_batch_rejects_four_b_group_after_subevent_marker) {
    auto capture = writeback_capture(79);
    constexpr std::uintptr_t kMarkerHandle = 104;
    constexpr const wchar_t* kMarkerKey =
        L"S|scene:0|section:0|instruction:0/9|instruction:1";
    auto& captured_children =
        capture.snapshot.section_root.children[0].children;
    auto& raw_children = capture.input.nodes[0].children[0].children;

    CapturedEditorNode marker;
    marker.runtime_handle = kMarkerHandle;
    marker.instruction_id = 1;
    marker.ordinal = 19;
    captured_children.insert(captured_children.end() - 1, marker);

    RawReplayNode raw_marker;
    raw_marker.runtime_handle = kMarkerHandle;
    raw_marker.logical_key = kMarkerKey;
    raw_marker.instruction_id = 1;
    raw_marker.command.instruction_id = 1;
    raw_children.insert(raw_children.end() - 1, std::move(raw_marker));

    ccz::editor_preview::BattleWritebackPlanBuild plan;
    plan.ok = true;
    ccz::editor_preview::BattleWritebackChange insert;
    insert.kind = BattleWritebackChangeKind::InsertFourBGroup;
    insert.instruction_id = 0x4b;
    insert.insert_parent_key = L"S|scene:0|section:0|instruction:0";
    insert.insert_before_key =
        L"S|scene:0|section:0|instruction:0/1|instruction:0";
    insert.source_ordinal = -1;
    insert.group_label = L"我军出场";
    insert.group_records = {std::vector<std::int32_t>(20, 1)};
    plan.changes.push_back(std::move(insert));

    const auto batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
        plan, capture, L"S|scene:0|section:0");
    CCZ_REQUIRE(!batch.ok);
    CCZ_REQUIRE(batch.diagnostic.find(L"子事件设定") != std::wstring::npos);
    CCZ_REQUIRE(batch.changes.empty());
}

BattleWritebackSourceInstruction fixed_source(
    std::uint16_t instruction, const wchar_t* key, int ordinal) {
    BattleWritebackSourceInstruction source;
    source.instruction_id = instruction;
    source.logical_key = key;
    source.ordinal = ordinal;
    source.origin_int_data.assign(1000, 0);
    source.current_int_data.assign(1000, 0);
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    for (std::size_t row_index = 0;
         row_index < spec->record_capacity; ++row_index) {
        const std::size_t offset = row_index * spec->record_stride;
        for (std::size_t field = 0; field < spec->record_stride; ++field) {
            std::int32_t sentinel = 0;
            switch (spec->field_roles[field]) {
            case ccz::preview::BattleFieldRole::DataId:
            case ccz::preview::BattleFieldRole::TargetDataId:
            case ccz::preview::BattleFieldRole::Direction:
                sentinel = -1;
                break;
            case ccz::preview::BattleFieldRole::UnitType:
                sentinel = 2;
                break;
            case ccz::preview::BattleFieldRole::AiPolicy:
                sentinel = 1;
                break;
            default:
                break;
            }
            source.origin_int_data[offset + field] = sentinel;
            source.current_int_data[offset + field] = sentinel;
        }
    }
    return source;
}

BattleWritebackSourceInstruction four_b_source(const wchar_t* key,
                                                int ordinal) {
    BattleWritebackSourceInstruction source;
    source.instruction_id = 0x4b;
    source.logical_key = key;
    source.ordinal = ordinal;
    source.origin_int_data.assign(20, 0);
    source.origin_int_data[3] = -1;
    source.current_int_data = source.origin_int_data;
    return source;
}

BattleUnit staged_unit(std::uint16_t instruction, int data_id,
                       int x, int y, std::uint64_t cache_id) {
    const auto* spec = ccz::preview::get_battle_field_spec(instruction);
    BattleUnit unit;
    unit.data_id = data_id;
    unit.position = {x, y};
    unit.direction = 2;
    unit.hidden = true;
    unit.level = 3;
    unit.unit_type = 1;
    unit.reinforcement = instruction == 0x47;
    unit.ai_policy = 4;
    unit.target = -1;
    unit.target_x = 0;
    unit.target_y = 0;
    unit.fields_decoded = true;
    unit.raw_fields.assign(spec->record_stride, 0);
    unit.editor_cache_id = cache_id;
    unit.editor_deployment_instruction = instruction;
    unit.editor_staged_new = true;
    for (const auto role : spec->field_roles)
        ccz::preview::mark_battle_field_dirty(unit, role);
    return unit;
}

BattleUnit existing_four_b(const wchar_t* key) {
    BattleUnit unit;
    unit.source_instruction = 0x4b;
    unit.editor_deployment_instruction = 0x4b;
    unit.source_key = key;
    unit.record_index = 0;
    unit.page = 0;
    unit.data_id = -1;
    unit.deployment_index = 0;
    unit.position = {3, 4};
    unit.direction = 1;
    unit.fields_decoded = true;
    unit.raw_fields = {0, 3, 4, 1, 0};
    unit.editor_origin_fields = unit.raw_fields;
    unit.editor_cache_id = 1;
    return unit;
}

void set_fixed_source_row(BattleWritebackSourceInstruction& source,
                          std::size_t row_index,
                          const std::vector<std::int32_t>& fields) {
    const auto* spec = ccz::preview::get_battle_field_spec(source.instruction_id);
    const std::size_t offset = row_index * spec->record_stride;
    for (std::size_t field = 0; field < fields.size(); ++field) {
        source.origin_int_data[offset + field] = fields[field];
        source.current_int_data[offset + field] = fields[field];
    }
}

BattleEditorState brush_placement_state() {
    BattleEditorState state;
    state.scope_root = L"scope";
    state.battle.map.valid = true;
    state.battle.map.width = 8;
    state.battle.map.height = 8;
    state.battle.terrain.assign(64, 0);
    auto movement_terrain = std::make_shared<
        ccz::preview::BattleMovementTerrainTable>();
    movement_terrain->valid = true;
    movement_terrain->values.fill(1);
    state.movement_terrain = std::move(movement_terrain);
    for (int data_id = 358; data_id <= 365; ++data_id) {
        ccz::editor_preview::BattleCharacterEntry entry;
        entry.data_id = data_id;
        entry.name = L"unit";
        entry.appearance = 1;
        entry.default_direction = 2;
        entry.level = 3;
        entry.unit_type = 1;
        entry.ai_policy = 1;
        entry.movement_class_code = 0;
        state.characters.entries.push_back(std::move(entry));
    }
    state.minion_brushes.push_back({L"test brush", 358, 365});

    BattleUnit occupied = existing_four_b(L"scope|occupied-4b");
    occupied.data_id = 358;
    occupied.position = {7, 7};
    occupied.raw_fields = {358, 7, 7, 1, 0};
    occupied.editor_origin_fields = occupied.raw_fields;
    state.battle.units.push_back(std::move(occupied));
    return state;
}

class FakeBattleWritebackNativeHost final
    : public ccz::editor_preview::BattleWritebackNativeHost {
public:
    using NodeSnapshot =
        ccz::editor_preview::BattleWritebackNativeNodeSnapshot;
    using DocumentState =
        ccz::editor_preview::BattleWritebackNativeDocumentState;
    using HandleMapping =
        ccz::editor_preview::BattleWritebackNativeHandleMapping;

    FakeBattleWritebackNativeHost() {
        // Handle 1 models the selectable document-root TreeView item. It has
        // no ItemData, unlike the script nodes below.
        selectable_non_node_tree_item = 1;
        state.selected_handle = 12;
        state.next_ordinal = 30;
        state.custom_modified = false;
        state.framework_modified = true;
        AddNode(10, 0, 0, 0, 0, 0, {});
        AddNode(11, 10, 0x4b, 10, 0, 0, std::vector<std::int32_t>(20, 1));
        AddNode(12, 10, 0x4b, 11, 0, 0, std::vector<std::int32_t>(20, 2));
        AddNode(13, 10, 0, 12, 0, 0, {});
        children[10] = {11, 12, 13};
        RefreshRevision();
    }

    bool CaptureDocumentState(DocumentState& output) const override {
        ++capture_document_state_calls;
        output = state;
        output.document_revision = Revision();
        return true;
    }

    bool CaptureNode(std::uintptr_t handle, NodeSnapshot& output) const override {
        const auto found = nodes.find(handle);
        if (found == nodes.end()) return false;
        output = found->second;
        output.has_children = children.find(handle) != children.end();
        const auto siblings = children.find(output.parent_handle);
        if (siblings != children.end()) {
            const auto position = std::find(siblings->second.begin(),
                                            siblings->second.end(), handle);
            if (position == siblings->second.end()) return false;
            if (position != siblings->second.begin())
                output.previous_sibling_handle = *(position - 1);
            if (position + 1 != siblings->second.end())
                output.next_sibling_handle = *(position + 1);
        }
        return true;
    }

    bool NodeExists(std::uintptr_t handle) const override {
        return nodes.find(handle) != nodes.end();
    }

    bool TreeItemExists(std::uintptr_t handle) const {
        return handle == selectable_non_node_tree_item || NodeExists(handle);
    }

    std::uintptr_t SelectableNonNodeTreeItem() const {
        return selectable_non_node_tree_item;
    }

    bool SwapNodeIntDataAndRefresh(
        std::uintptr_t handle,
        std::vector<std::int32_t>& int_data) override {
        if (FailOnce(L"swap")) return false;
        const auto found = nodes.find(handle);
        if (found == nodes.end() || found->second.int_data.size() != int_data.size())
            return false;
        found->second.int_data.swap(int_data);
        if (corrupt_next_swap && !found->second.int_data.empty()) {
            corrupt_next_swap = false;
            found->second.int_data.back() ^= 1;
        }
        RefreshRevision();
        return true;
    }

    bool InsertBattleNode(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, std::uint16_t instruction_id,
        const std::vector<std::int32_t>& int_data, int ordinal,
        std::uintptr_t& inserted_handle) override {
		if (FailOnce(L"insert")) return false;
		const auto parent = nodes.find(parent_handle);
		const std::size_t expected = instruction_id == 0x4b ? 20U
			: instruction_id == 0x46 || instruction_id == 0x47 ? 1000U
			: instruction_id == 0x58 ? 7U
			: instruction_id == 0x32 || instruction_id == 0x55 ? 6U : 0U;
		if (parent == nodes.end() || children.find(parent_handle) == children.end() ||
			int_data.size() != expected ||
			(instruction_id != 0x4b && instruction_id != 0x46 &&
			 instruction_id != 0x47 && instruction_id != 0x32 &&
			 instruction_id != 0x55 && instruction_id != 0x58)) return false;
        auto& siblings = children[parent_handle];
        std::size_t index = 0;
        if (after_handle != 0) {
            const auto after = std::find(siblings.begin(), siblings.end(), after_handle);
            if (after == siblings.end()) return false;
            index = static_cast<std::size_t>(after - siblings.begin()) + 1;
        }
        inserted_handle = next_handle++;
        AddNode(inserted_handle, parent_handle, instruction_id, ordinal,
                0, 0, int_data);
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index),
                        inserted_handle);
        state.next_ordinal = ordinal + 1;
        RefreshRevision();
        return true;
    }

    bool InsertBattleFourBGroup(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, const std::wstring& label,
        const std::vector<std::vector<std::int32_t>>& records,
        int first_ordinal, std::uintptr_t& marker_handle,
        std::uintptr_t& group_handle,
        std::vector<std::uintptr_t>& record_handles,
        std::uintptr_t& terminal_handle) override {
        marker_handle = group_handle = terminal_handle = 0;
        record_handles.clear();
        if (FailOnce(L"group-insert") || label != L"我军出场" ||
            records.empty() || first_ordinal != state.next_ordinal ||
            nodes.find(parent_handle) == nodes.end() ||
            children.find(parent_handle) == children.end()) return false;
        for (const auto& record : records)
            if (record.size() != 20) return false;

        auto& siblings = children[parent_handle];
        std::size_t insert_index = 0;
        if (after_handle != 0) {
            const auto after = std::find(siblings.begin(), siblings.end(),
                                         after_handle);
            if (after == siblings.end()) return false;
            insert_index = static_cast<std::size_t>(after - siblings.begin()) + 1;
        }
        marker_handle = next_handle++;
        group_handle = next_handle++;
        AddNode(marker_handle, parent_handle, 1, first_ordinal,
                0, 0, {});
        AddNode(group_handle, parent_handle, 2, first_ordinal + 1,
                0, 0, {});
        nodes[group_handle].long_text = label;
        nodes[group_handle].has_long_text = true;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(insert_index),
                        marker_handle);
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(insert_index + 1),
                        group_handle);
        auto& group_children = children[group_handle];
        record_handles.reserve(records.size());
        for (std::size_t index = 0; index < records.size(); ++index) {
            const std::uintptr_t handle = next_handle++;
            AddNode(handle, group_handle, 0x4b,
                    first_ordinal + 3 + static_cast<int>(index),
                    0, 0, records[index]);
            record_handles.push_back(handle);
            group_children.push_back(handle);
        }
        terminal_handle = next_handle++;
        AddNode(terminal_handle, group_handle, 0,
                first_ordinal + 2, 0, 0, {});
        group_children.push_back(terminal_handle);
        state.next_ordinal = first_ordinal +
            static_cast<int>(records.size()) + 3;
        RefreshRevision();
        return true;
    }

    bool DeleteBattleFourBGroup(std::uintptr_t marker_handle,
        std::uintptr_t group_handle) override {
        if (FailOnce(L"group-delete")) return false;
        const auto marker = nodes.find(marker_handle);
        const auto group = nodes.find(group_handle);
        if (marker == nodes.end() || group == nodes.end() ||
            marker->second.instruction_id != 1 ||
            group->second.instruction_id != 2 ||
            group->second.long_text != L"我军出场" ||
            marker->second.parent_handle != group->second.parent_handle)
            return false;
        auto siblings = children.find(marker->second.parent_handle);
        auto group_children = children.find(group_handle);
        if (siblings == children.end() || group_children == children.end() ||
            group_children->second.size() < 2 ||
            group_children->second.back() == 0) return false;
        const auto marker_position = std::find(siblings->second.begin(),
            siblings->second.end(), marker_handle);
        if (marker_position == siblings->second.end() ||
            marker_position + 1 == siblings->second.end() ||
            *(marker_position + 1) != group_handle) return false;
        for (std::size_t index = 0;
             index + 1 < group_children->second.size(); ++index) {
            const auto record = nodes.find(group_children->second[index]);
            if (record == nodes.end() || record->second.instruction_id != 0x4b ||
                record->second.int_data.size() != 20) return false;
        }
        const auto terminal = nodes.find(group_children->second.back());
        if (terminal == nodes.end() || terminal->second.instruction_id != 0 ||
            !terminal->second.int_data.empty()) return false;

        const std::uintptr_t parent_handle = marker->second.parent_handle;
        if (state.selected_handle == marker_handle ||
            state.selected_handle == group_handle ||
            std::find(group_children->second.begin(), group_children->second.end(),
                      state.selected_handle) != group_children->second.end())
            state.selected_handle = parent_handle;
        for (const std::uintptr_t handle : group_children->second)
            nodes.erase(handle);
        children.erase(group_children);
        nodes.erase(group_handle);
        siblings = children.find(parent_handle);
        const auto erase_handle = [&siblings](std::uintptr_t handle) {
            const auto found = std::find(siblings->second.begin(),
                                         siblings->second.end(), handle);
            if (found != siblings->second.end()) siblings->second.erase(found);
        };
        erase_handle(group_handle);
        erase_handle(marker_handle);
        nodes.erase(marker_handle);
        RefreshRevision();
        return true;
    }

    bool RestoreBattleFourBGroupBefore(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, std::uintptr_t before_handle,
        const std::wstring& label,
        const std::vector<std::vector<std::int32_t>>& records,
        int first_ordinal, std::uintptr_t& marker_handle,
        std::uintptr_t& group_handle,
        std::vector<std::uintptr_t>& record_handles,
        std::uintptr_t& terminal_handle) override {
        if (FailOnce(L"group-restore")) return false;
        const int next_ordinal = state.next_ordinal;
        if (before_handle != 0) {
            const auto& siblings = children[parent_handle];
            const auto before = std::find(siblings.begin(), siblings.end(),
                                          before_handle);
            if (before == siblings.end())
                return false;
            if (after_handle != 0) {
                const auto after = std::find(siblings.begin(), siblings.end(),
                                             after_handle);
                if (after == siblings.end() || after >= before) return false;
            }
        }
        state.next_ordinal = first_ordinal;
        const bool inserted = InsertBattleFourBGroup(parent_handle,
            after_handle, label, records, first_ordinal, marker_handle,
            group_handle, record_handles, terminal_handle);
        state.next_ordinal = next_ordinal;
        RefreshRevision();
        return inserted;
    }

    bool DeleteBattleNode(std::uintptr_t handle) override {
        if (FailOnce(L"delete")) return false;
        const auto found = nodes.find(handle);
        if (found == nodes.end()) return false;
        NodeSnapshot snapshot = found->second;
        snapshot.has_children = children.find(handle) != children.end();
        if (!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(snapshot))
            return false;
        auto siblings = children.find(found->second.parent_handle);
        if (siblings == children.end()) return false;
        const auto position = std::find(siblings->second.begin(),
                                        siblings->second.end(), handle);
        if (position == siblings->second.end()) return false;
        if (state.selected_handle == handle)
            state.selected_handle = found->second.parent_handle;
        siblings->second.erase(position);
        nodes.erase(found);
        RefreshRevision();
        return true;
    }

    bool RestoreBattleNodeBefore(const NodeSnapshot& snapshot,
        std::uintptr_t before_handle,
        std::uintptr_t& restored_handle) override {
        if (FailOnce(L"restore")) return false;
        if (!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(snapshot))
            return false;
        if (nodes.find(snapshot.parent_handle) == nodes.end()) return false;
        auto& siblings = children[snapshot.parent_handle];
        std::size_t index = siblings.size();
        if (before_handle != 0) {
            const auto before = std::find(siblings.begin(), siblings.end(), before_handle);
            if (before == siblings.end()) return false;
            index = static_cast<std::size_t>(before - siblings.begin());
        }
        restored_handle = next_handle++;
        AddNode(restored_handle, snapshot.parent_handle, snapshot.instruction_id,
                snapshot.ordinal, 0, 0, snapshot.int_data);
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index),
                        restored_handle);
        RefreshRevision();
        return true;
    }

    bool MarkDocumentModified() override {
        if (FailOnce(L"mark")) return false;
        state.custom_modified = true;
        state.framework_modified = true;
        return true;
    }

    bool RestoreDocumentState(const DocumentState& target,
        const std::vector<HandleMapping>& remapped_handles) override {
        if (FailOnce(L"restore-state")) return false;
        state = target;
        for (const auto& mapping : remapped_handles) {
            if (state.selected_handle == mapping.old_handle)
                state.selected_handle = mapping.new_handle;
        }
        if (state.selected_handle != 0 &&
            !TreeItemExists(state.selected_handle))
            return false;
        RefreshRevision();
        return true;
    }

    void FailNext(const wchar_t* operation) {
        fail_operation = operation;
        failure_consumed = false;
        failure_countdown = 1;
    }

    void FailOnAttempt(const wchar_t* operation, int attempt) {
        fail_operation = operation;
        failure_consumed = false;
        failure_countdown = (std::max)(1, attempt);
    }

    void CorruptNextSwap() { corrupt_next_swap = true; }

    void ExternalChange(std::uintptr_t handle, std::int32_t value) {
        auto found = nodes.find(handle);
        if (found != nodes.end() && !found->second.int_data.empty()) {
            found->second.int_data[0] = value;
            RefreshRevision();
        }
    }

    std::vector<std::uintptr_t> Children(std::uintptr_t parent) const {
        const auto found = children.find(parent);
        return found == children.end() ? std::vector<std::uintptr_t>{}
                                       : found->second;
    }

    std::vector<std::int32_t> Data(std::uintptr_t handle) const {
        const auto found = nodes.find(handle);
        return found == nodes.end() ? std::vector<std::int32_t>{}
                                    : found->second.int_data;
    }

    void SetInstructionId(std::uintptr_t handle, std::uint16_t instruction) {
        const auto found = nodes.find(handle);
        if (found == nodes.end()) return;
        found->second.instruction_id = instruction;
        RefreshRevision();
    }

    void SetLongText(std::uintptr_t handle, std::wstring text) {
        const auto found = nodes.find(handle);
        if (found == nodes.end()) return;
        found->second.long_text = std::move(text);
        found->second.has_long_text = !found->second.long_text.empty();
        RefreshRevision();
    }

    std::uintptr_t Child(std::uintptr_t parent, std::size_t index) const {
        const auto found = children.find(parent);
        return found == children.end() || index >= found->second.size()
            ? 0 : found->second[index];
    }

    std::int32_t FirstInt(std::uintptr_t handle) const {
        const auto found = nodes.find(handle);
        return found == nodes.end() || found->second.int_data.empty()
            ? 0 : found->second.int_data.front();
    }

    void AddNestedEvent() {
        AddNode(20, 10, 0x36, 28, 0, 0, {});
        AddNode(21, 20, 0, 29, 0, 0, {});
        children[10].insert(children[10].end() - 1, 20);
        children[20] = {21};
        RefreshRevision();
    }

    DocumentState state;
    mutable int capture_document_state_calls{};

private:
    void AddNode(std::uintptr_t handle, std::uintptr_t parent,
        std::uint16_t instruction, int ordinal, std::uintptr_t previous,
        std::uintptr_t next, const std::vector<std::int32_t>& int_data) {
        NodeSnapshot node;
        node.handle = handle;
        node.parent_handle = parent;
        node.previous_sibling_handle = previous;
        node.next_sibling_handle = next;
        node.instruction_id = instruction;
        node.ordinal = ordinal;
        node.int_data = int_data;
        nodes[handle] = std::move(node);
    }

    bool FailOnce(const wchar_t* operation) {
        if (!failure_consumed && fail_operation == operation &&
            failure_countdown > 1) {
            --failure_countdown;
            return false;
        }
        if (!failure_consumed && fail_operation == operation) {
            failure_consumed = true;
            return true;
        }
        return false;
    }

    std::uint64_t Revision() const {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        for (const auto& entry : children) {
            mix(entry.first);
            for (const auto handle : entry.second) {
                const auto found = nodes.find(handle);
                if (found == nodes.end()) continue;
                mix(found->second.instruction_id);
                mix(static_cast<std::uint64_t>(found->second.ordinal));
                for (const auto value : found->second.int_data)
                    mix(static_cast<std::uint32_t>(value));
            }
        }
        return hash;
    }

    void RefreshRevision() { state.document_revision = Revision(); }

    std::unordered_map<std::uintptr_t, NodeSnapshot> nodes;
    std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> children;
    std::uintptr_t next_handle{100};
    std::wstring fail_operation;
    bool failure_consumed{};
    int failure_countdown{1};
    bool corrupt_next_swap{};
    std::uintptr_t selectable_non_node_tree_item{};
};

ccz::editor_preview::BattleWritebackNativeBatch native_batch_for_transaction() {
    using namespace ccz::editor_preview;
    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange update;
    update.kind = BattleWritebackChangeKind::UpdateInstruction;
    update.instruction_id = 0x4b;
    update.item_handle = 11;
    update.source_ordinal = 10;
    update.original_int_data.assign(20, 1);
    update.int_data.assign(20, 3);
    batch.changes.push_back(std::move(update));

    BattleWritebackNativeChange deletion;
    deletion.kind = BattleWritebackChangeKind::DeleteInstruction;
    deletion.instruction_id = 0x4b;
    deletion.item_handle = 12;
    deletion.source_ordinal = 11;
    deletion.original_int_data.assign(20, 2);
    deletion.int_data = deletion.original_int_data;
    batch.changes.push_back(std::move(deletion));

    for (const auto instruction : {0x46, 0x47}) {
        BattleWritebackNativeChange insertion;
        insertion.kind = BattleWritebackChangeKind::InsertInstruction;
        insertion.instruction_id = static_cast<std::uint16_t>(instruction);
        insertion.parent_handle = 10;
        // The original predecessor is deleted in the same transaction. The
        // bridge must walk to a surviving sibling and preserve plan order.
        insertion.insert_after_handle = 12;
        insertion.insert_before_handle = 13;
        insertion.source_ordinal = -1;
        insertion.int_data.assign(1000, instruction);
        batch.changes.push_back(std::move(insertion));
    }
    return batch;
}

}  // namespace

CCZ_TEST(battle_writeback_qa_gate_only_allows_marked_s_script_copies) {
    namespace fs = std::filesystem;
    using ccz::editor_preview::IsBattleWritebackQaCopy;
    using ccz::editor_preview::kBattleWritebackQaMarkerContent;
    using ccz::editor_preview::kBattleWritebackQaMarkerFile;

    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const fs::path base = fs::temp_directory_path() /
        (L"ccz-battle-writeback-qa-" + std::to_wstring(unique));
    const fs::path root = base / L"writeback-qa-copies";
    const fs::path outside = base / L"writeback-qa-copies-evil";
    const fs::path nested = root / L"nested";
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{ base };
    fs::create_directories(root);
    fs::create_directories(outside);
    fs::create_directories(nested);

    const auto write_file = [](const fs::path& path, const char* bytes) {
        std::ofstream file(path, std::ios::binary);
        file << bytes;
        file.close();
        return file.good();
    };
    CCZ_REQUIRE(write_file(root / kBattleWritebackQaMarkerFile,
        kBattleWritebackQaMarkerContent));
    CCZ_REQUIRE(write_file(root / L"S_test.eex", "copy"));
    CCZ_REQUIRE(write_file(root / L"s-dash.EEX", "copy"));
    CCZ_REQUIRE(write_file(root / L"R_test.eex", "not S"));
    CCZ_REQUIRE(write_file(root / L"S_test.txt", "wrong extension"));
    CCZ_REQUIRE(write_file(nested / L"S_nested.eex", "nested"));
    CCZ_REQUIRE(write_file(outside / L"S_outside.eex", "sibling"));

    CCZ_REQUIRE(IsBattleWritebackQaCopy(
        (root / L"S_test.eex").wstring(), root.wstring()));
    CCZ_REQUIRE(IsBattleWritebackQaCopy(
        (root / L"s-dash.EEX").wstring(), root.wstring()));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (root / L"R_test.eex").wstring(), root.wstring()));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (root / L"S_test.txt").wstring(), root.wstring()));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (root / L"nested" / L"S_nested.eex").wstring(), root.wstring()));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (outside / L"S_outside.eex").wstring(), root.wstring()));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (root / L".." / outside.filename() / L"S_outside.eex").wstring(),
        root.wstring()));

    const fs::path unmarked = base / L"unmarked";
    fs::create_directories(unmarked);
    CCZ_REQUIRE(write_file(unmarked / L"S_test.eex", "copy"));
    CCZ_REQUIRE(!IsBattleWritebackQaCopy(
        (unmarked / L"S_test.eex").wstring(), unmarked.wstring()));

}

CCZ_TEST(battle_writeback_qa_copy_root_is_fixed_next_to_executable) {
    using ccz::editor_preview::BattleWritebackQaCopyRootForExecutable;
    CCZ_REQUIRE(BattleWritebackQaCopyRootForExecutable(
        L"C:\\QA\\Debug\\s.exe") ==
        L"C:\\QA\\Debug\\writeback-qa-copies");
    CCZ_REQUIRE(BattleWritebackQaCopyRootForExecutable(
        L"s.exe").empty());
}

CCZ_TEST(battle_writeback_native_transaction_applies_and_undoes_one_atomic_batch) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    const auto initial_revision = host.state.document_revision;
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;

    if (!transaction.Apply(native_batch_for_transaction(), host, diagnostic))
        throw std::runtime_error("native transaction rejected the batch");
    CCZ_REQUIRE_EQ(host.capture_document_state_calls, 2);
    CCZ_REQUIRE(transaction.undo_available());
    CCZ_REQUIRE_EQ(host.Children(10).size(), static_cast<std::size_t>(4));
    const auto after_apply = host.Children(10);
    CCZ_REQUIRE_EQ(after_apply[0], std::uintptr_t(11));
    CCZ_REQUIRE_EQ(after_apply[1], std::uintptr_t(100));
    CCZ_REQUIRE_EQ(after_apply[2], std::uintptr_t(101));
    CCZ_REQUIRE_EQ(after_apply[3], std::uintptr_t(13));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 3);
    CCZ_REQUIRE_EQ(host.state.selected_handle, std::uintptr_t(10));
    CCZ_REQUIRE(host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 32);

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    const auto after_undo = host.Children(10);
    CCZ_REQUIRE_EQ(after_undo.size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(after_undo[0], std::uintptr_t(11));
    CCZ_REQUIRE_EQ(after_undo[2], std::uintptr_t(13));
    CCZ_REQUIRE(host.NodeExists(after_undo[1]));
    CCZ_REQUIRE(after_undo[1] != std::uintptr_t(12));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
    CCZ_REQUIRE_EQ(host.state.selected_handle, after_undo[1]);
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 30);
    CCZ_REQUIRE(!host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE_EQ(host.state.document_revision, initial_revision);
}

CCZ_TEST(battle_writeback_native_transaction_preserves_large_insert_batch_order) {
    using namespace ccz::editor_preview;
    constexpr std::size_t insert_count = 96;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeBatch batch;
    batch.ok = true;
    batch.changes.reserve(insert_count);
    for (std::size_t index = 0; index < insert_count; ++index) {
        BattleWritebackNativeChange insertion;
        insertion.kind = BattleWritebackChangeKind::InsertInstruction;
        insertion.instruction_id = static_cast<std::uint16_t>(
            index % 2 == 0 ? 0x46 : 0x47);
        insertion.parent_handle = 10;
        insertion.insert_after_handle = 12;
        insertion.insert_before_handle = 13;
        insertion.source_ordinal = -1;
        insertion.int_data.assign(1000, static_cast<std::int32_t>(index));
        batch.changes.push_back(std::move(insertion));
    }

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    const auto children = host.Children(10);
    CCZ_REQUIRE_EQ(children.size(), insert_count + 3);
    CCZ_REQUIRE_EQ(children.front(), std::uintptr_t(11));
    CCZ_REQUIRE_EQ(children.back(), std::uintptr_t(13));
    CCZ_REQUIRE_EQ(host.state.next_ordinal,
        static_cast<int>(30 + insert_count));
    CCZ_REQUIRE_EQ(host.capture_document_state_calls, 2);
    for (std::size_t index = 0; index < insert_count; ++index) {
        const auto handle = children[index + 2];
        const auto data = host.Data(handle);
        if (data.empty() || data.front() != static_cast<std::int32_t>(index)) {
            throw std::runtime_error("large insert order mismatch at " +
                std::to_string(index) + ": got " +
                (data.empty() ? std::string("empty") :
                    std::to_string(data.front())));
        }
    }
}

CCZ_TEST(battle_writeback_native_transaction_allows_sequential_commits_and_lifo_undo) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;

    const auto update_to = [](std::int32_t before, std::int32_t after) {
        BattleWritebackNativeBatch batch;
        batch.ok = true;
        BattleWritebackNativeChange update;
        update.kind = BattleWritebackChangeKind::UpdateInstruction;
        update.instruction_id = 0x4b;
        update.item_handle = 11;
        update.source_ordinal = 10;
        update.original_int_data.assign(20, before);
        update.int_data.assign(20, after);
        batch.changes.push_back(std::move(update));
        return batch;
    };

    CCZ_REQUIRE(transaction.Apply(update_to(1, 3), host, diagnostic));
    CCZ_REQUIRE(transaction.Apply(update_to(3, 5), host, diagnostic));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 5);
    CCZ_REQUIRE(transaction.undo_available());

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 3);
    CCZ_REQUIRE(transaction.undo_available());
    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_preserves_non_node_document_root_selection) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.state.selected_handle = host.SelectableNonNodeTreeItem();
    CCZ_REQUIRE(!host.NodeExists(host.state.selected_handle));

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange update;
    update.kind = BattleWritebackChangeKind::UpdateInstruction;
    update.instruction_id = 0x4b;
    update.item_handle = 11;
    update.source_ordinal = 10;
    update.original_int_data.assign(20, 1);
    update.int_data.assign(20, 3);
    batch.changes.push_back(std::move(update));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.state.selected_handle,
                   host.SelectableNonNodeTreeItem());
    CCZ_REQUIRE_EQ(host.FirstInt(11), 3);
    CCZ_REQUIRE(host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.state.selected_handle,
                   host.SelectableNonNodeTreeItem());
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
    CCZ_REQUIRE(!host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
}

CCZ_TEST(battle_writeback_native_transaction_rejects_miswritten_update) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.CorruptNextSwap();

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange update;
    update.kind = BattleWritebackChangeKind::UpdateInstruction;
    update.instruction_id = 0x4b;
    update.item_handle = 11;
    update.source_ordinal = 10;
    update.original_int_data.assign(20, 1);
    update.int_data.assign(20, 3);
    batch.changes.push_back(std::move(update));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    CCZ_REQUIRE_EQ(host.Data(11), std::vector<std::int32_t>(20, 1));
    CCZ_REQUIRE(diagnostic.find(L"回读") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_native_delete_policy_supports_every_deployment_type) {
    using ccz::editor_preview::BattleWritebackNativeNodeSnapshot;
    using ccz::editor_preview::CanDeleteBattleWritebackNativeNode;

    BattleWritebackNativeNodeSnapshot node;
    node.handle = 1;
    node.instruction_id = 0x4b;
    node.int_data.assign(20, 0);
    CCZ_REQUIRE(CanDeleteBattleWritebackNativeNode(node));

    node.instruction_id = 0x46;
    node.int_data.assign(1000, 0);
    CCZ_REQUIRE(CanDeleteBattleWritebackNativeNode(node));
    node.instruction_id = 0x47;
    CCZ_REQUIRE(CanDeleteBattleWritebackNativeNode(node));

    node.instruction_id = 0x46;
    node.int_data.resize(20);
    CCZ_REQUIRE(!CanDeleteBattleWritebackNativeNode(node));
    node.int_data.assign(1000, 0);
    node.has_children = true;
    CCZ_REQUIRE(!CanDeleteBattleWritebackNativeNode(node));
    node.has_children = false;
    node.has_long_text = true;
    CCZ_REQUIRE(!CanDeleteBattleWritebackNativeNode(node));
    node.has_long_text = false;
    node.has_structural_links = true;
    CCZ_REQUIRE(!CanDeleteBattleWritebackNativeNode(node));
    node.has_structural_links = false;
    node.instruction_id = 0x30;
    CCZ_REQUIRE(!CanDeleteBattleWritebackNativeNode(node));
}

CCZ_TEST(battle_writeback_next_ordinal_allows_gaps_beyond_document_maximum) {
    CapturedEditorTree document;
    CapturedEditorNode root;
    root.instruction_id = -1;
    CapturedEditorNode section;
    section.instruction_id = -2;
    CapturedEditorNode first;
    first.instruction_id = 0x46;
    first.ordinal = 4;
    CapturedEditorNode later;
    later.instruction_id = 0x47;
    later.ordinal = 19;
    section.children.push_back(std::move(first));
    section.children.push_back(std::move(later));
    root.children.push_back(std::move(section));
    document.document_roots.push_back(std::move(root));

    std::wstring diagnostic;
    CCZ_REQUIRE(ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
        document, 20, diagnostic));
    CCZ_REQUIRE(diagnostic.empty());
    // Removed instructions can leave holes; no renumbering is needed as long
    // as the document-global counter remains strictly above the maximum.
    CCZ_REQUIRE(ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
        document, 25, diagnostic));
}

CCZ_TEST(battle_writeback_next_ordinal_rejects_stale_or_colliding_counter) {
    CapturedEditorTree document;
    CapturedEditorNode root;
    root.instruction_id = -1;
    CapturedEditorNode instruction;
    instruction.instruction_id = 0x4b;
    instruction.ordinal = 25;
    root.children.push_back(std::move(instruction));
    document.document_roots.push_back(std::move(root));

    std::wstring diagnostic;
    CCZ_REQUIRE(!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
        document, 25, diagnostic));
    CCZ_REQUIRE(!diagnostic.empty());
    CCZ_REQUIRE(!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
        document, 8, diagnostic));
    CCZ_REQUIRE(!diagnostic.empty());
}

CCZ_TEST(battle_writeback_next_ordinal_rejects_missing_existing_ordinal) {
    CapturedEditorTree document;
    CapturedEditorNode root;
    root.instruction_id = -1;
    CapturedEditorNode instruction;
    instruction.instruction_id = 0x46;
    instruction.ordinal = -1;
    root.children.push_back(std::move(instruction));
    document.document_roots.push_back(std::move(root));

    std::wstring diagnostic;
    CCZ_REQUIRE(!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
        document, 0, diagnostic));
    CCZ_REQUIRE(!diagnostic.empty());
}

CCZ_TEST(battle_writeback_native_transaction_rolls_back_when_a_late_insert_fails) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    const auto initial_revision = host.state.document_revision;
    host.FailNext(L"insert");
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;

    CCZ_REQUIRE(!transaction.Apply(native_batch_for_transaction(), host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    CCZ_REQUIRE_EQ(host.Children(10).size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(host.Child(10, 0), std::uintptr_t(11));
    CCZ_REQUIRE_EQ(host.Child(10, 2), std::uintptr_t(13));
    CCZ_REQUIRE(host.NodeExists(11));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
    CCZ_REQUIRE_EQ(host.state.selected_handle, host.Child(10, 1));
    CCZ_REQUIRE(host.state.selected_handle != std::uintptr_t(12));
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 30);
    CCZ_REQUIRE(!host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE_EQ(host.state.document_revision, initial_revision);
    CCZ_REQUIRE(diagnostic.find(L"回滚") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_native_transaction_inserts_under_nested_condition_event) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.AddNestedEvent();

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x4b;
    insertion.parent_handle = 20;
    insertion.insert_before_handle = 21;
    insertion.source_ordinal = -1;
    insertion.int_data.assign(20, 7);
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(20).size(), std::size_t(2));
    const auto inserted = host.Child(20, 0);
    CCZ_REQUIRE_EQ(host.FirstInt(inserted), 7);
    CCZ_REQUIRE_EQ(host.Child(20, 1), std::uintptr_t(21));

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(20).size(), std::size_t(1));
    CCZ_REQUIRE_EQ(host.Child(20, 0), std::uintptr_t(21));
}

CCZ_TEST(battle_writeback_native_transaction_rejects_insert_after_subevent_marker) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(12, 1);
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(diagnostic.find(L"子事件设定") != std::wstring::npos);
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_rejects_insert_after_event_end_marker) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(12, 0);
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(diagnostic.find(L"事件结束") != std::wstring::npos);
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_rejects_deleted_anchor_that_moves_insert_after_event_end) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(11, 0);
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange deletion;
    deletion.kind = BattleWritebackChangeKind::DeleteInstruction;
    deletion.instruction_id = 0x4b;
    deletion.item_handle = 12;
    deletion.source_ordinal = 11;
    deletion.original_int_data = host.Data(12);
    deletion.int_data = deletion.original_int_data;
    batch.changes.push_back(std::move(deletion));

    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(diagnostic.find(L"事件结束") != std::wstring::npos);
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_allows_insert_before_subevent_marker) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(12, 1);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 11;
    insertion.insert_before_handle = 12;
    insertion.insert_terminal_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10).size(), std::size_t(4));
    CCZ_REQUIRE_EQ(host.Child(10, 1), std::uintptr_t(100));
    CCZ_REQUIRE_EQ(host.Child(10, 2), std::uintptr_t(12));
}

CCZ_TEST(battle_writeback_native_transaction_allows_insert_before_event_end_marker) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.insert_terminal_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10).size(), std::size_t(4));
    CCZ_REQUIRE_EQ(host.Child(10, 2), std::uintptr_t(100));
    CCZ_REQUIRE_EQ(host.Child(10, 3), std::uintptr_t(13));
}

CCZ_TEST(battle_writeback_native_transaction_rejects_four_b_group_after_subevent_marker) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(12, 1);
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertFourBGroup;
    insertion.instruction_id = 0x4b;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.group_label = L"我军出场";
    insertion.group_records = {std::vector<std::int32_t>(20, 1)};
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(diagnostic.find(L"子事件设定") != std::wstring::npos);
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_inserts_all_three_new_unit_types) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeBatch batch;
    batch.ok = true;
    for (const std::uint16_t instruction : {
             std::uint16_t(0x4b), std::uint16_t(0x46),
             std::uint16_t(0x47)}) {
        BattleWritebackNativeChange insertion;
        insertion.kind = BattleWritebackChangeKind::InsertInstruction;
        insertion.instruction_id = instruction;
        insertion.parent_handle = 10;
        insertion.insert_after_handle = 12;
        insertion.insert_before_handle = 13;
        insertion.source_ordinal = -1;
        insertion.int_data.assign(instruction == 0x4b ? 20U : 1000U,
                                  instruction);
        batch.changes.push_back(std::move(insertion));
    }

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    const auto committed_children = host.Children(10);
    CCZ_REQUIRE_EQ(committed_children.size(), std::size_t(6));
    for (const std::uint16_t instruction : {
             std::uint16_t(0x4b), std::uint16_t(0x46),
             std::uint16_t(0x47)}) {
        const auto inserted = std::find_if(
            committed_children.begin(), committed_children.end(),
            [&host, instruction](std::uintptr_t handle) {
                FakeBattleWritebackNativeHost::NodeSnapshot node;
                return host.CaptureNode(handle, node) &&
                       node.instruction_id == instruction;
            });
        CCZ_REQUIRE(inserted != committed_children.end());
        const auto data = host.Data(*inserted);
        CCZ_REQUIRE_EQ(data.size(), instruction == 0x4b
            ? std::size_t(20) : std::size_t(1000));
    }

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10).size(), std::size_t(3));
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_inserts_and_undoes_map_operations_in_order) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeBatch batch;
    batch.ok = true;
    const std::vector<std::pair<std::uint16_t,
        std::vector<std::int32_t>>> operations = {
        {0x32, {0, 47, 0, 8, 12, 2}},
        {0x55, {0, 103, 0, 8, 12, 2}},
        {0x58, {2, 0, 26, 8, 12, 0, 0}},
    };
    for (const auto& operation : operations) {
        BattleWritebackNativeChange insertion;
        insertion.kind = BattleWritebackChangeKind::InsertInstruction;
        insertion.instruction_id = operation.first;
        insertion.parent_handle = 10;
        insertion.insert_after_handle = 12;
        insertion.insert_before_handle = 13;
        insertion.source_ordinal = -1;
        insertion.int_data = operation.second;
        insertion.map_width = 19;
        insertion.map_height = 18;
        batch.changes.push_back(std::move(insertion));
    }

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    const auto committed = host.Children(10);
    CCZ_REQUIRE_EQ(committed.size(), std::size_t(6));
    for (std::size_t index = 0; index < operations.size(); ++index) {
        BattleWritebackNativeNodeSnapshot inserted;
        CCZ_REQUIRE(host.CaptureNode(committed[index + 2], inserted));
        CCZ_REQUIRE_EQ(inserted.instruction_id, operations[index].first);
        CCZ_REQUIRE_EQ(inserted.int_data, operations[index].second);
    }
    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10),
        (std::vector<std::uintptr_t>{11, 12, 13}));
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_rejects_map_operation_in_own_deployment_group) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(10, 2);
    host.SetLongText(10, L"我军出场");
    const auto original_children = host.Children(10);
    const auto original_data = host.Data(11);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(diagnostic.find(L"我军出场") != std::wstring::npos);
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
    CCZ_REQUIRE_EQ(host.Data(11), original_data);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_allows_map_operation_in_other_subevent) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(10, 5);
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10).size(), original_children.size() + 1);
    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
}

CCZ_TEST(battle_writeback_native_transaction_allows_map_operation_in_non_deployment_info_group) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    host.SetInstructionId(10, 2);
    host.SetLongText(10, L"事件子分组");
    const auto original_children = host.Children(10);

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x58;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data = {0, 0, 26, 8, 12, 0, 0};
    insertion.map_width = 19;
    insertion.map_height = 18;
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10).size(), original_children.size() + 1);
    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10), original_children);
}

CCZ_TEST(battle_writeback_native_transaction_inserts_and_undoes_atomic_4b_group) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeBatch batch;
    batch.ok = true;

    BattleWritebackNativeChange group;
    group.kind = BattleWritebackChangeKind::InsertFourBGroup;
    group.instruction_id = 0x4b;
    group.parent_handle = 10;
    group.insert_after_handle = 12;
    group.insert_before_handle = 13;
    group.insert_terminal_handle = 13;
    group.source_ordinal = -1;
    group.group_label = L"我军出场";
    group.group_records = {std::vector<std::int32_t>(20, 11),
                           std::vector<std::int32_t>(20, 22)};
    group.group_records[0][0] = 0;
    group.group_records[1][0] = 1;
    batch.changes.push_back(std::move(group));

    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x46;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.insert_terminal_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data.assign(1000, 46);
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(transaction.Apply(batch, host, diagnostic));
    const auto siblings = host.Children(10);
    CCZ_REQUIRE_EQ(siblings.size(), std::size_t(6));
    CCZ_REQUIRE_EQ(siblings[0], std::uintptr_t(11));
    CCZ_REQUIRE_EQ(siblings[1], std::uintptr_t(12));
    const std::uintptr_t marker_handle = siblings[2];
    const std::uintptr_t group_handle = siblings[3];
    BattleWritebackNativeNodeSnapshot following_instruction;
    CCZ_REQUIRE(host.CaptureNode(siblings[4], following_instruction));
    CCZ_REQUIRE_EQ(following_instruction.instruction_id, std::uint16_t(0x46));
    CCZ_REQUIRE_EQ(siblings[5], std::uintptr_t(13));

    BattleWritebackNativeNodeSnapshot marker;
    BattleWritebackNativeNodeSnapshot group_node;
    CCZ_REQUIRE(host.CaptureNode(marker_handle, marker));
    CCZ_REQUIRE(host.CaptureNode(group_handle, group_node));
    CCZ_REQUIRE_EQ(marker.instruction_id, std::uint16_t(1));
    CCZ_REQUIRE_EQ(group_node.instruction_id, std::uint16_t(2));
    CCZ_REQUIRE_EQ(group_node.long_text, std::wstring(L"我军出场"));
    const auto group_children = host.Children(group_handle);
    CCZ_REQUIRE_EQ(group_children.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(host.FirstInt(group_children[0]), 0);
    CCZ_REQUIRE_EQ(host.FirstInt(group_children[1]), 1);
    BattleWritebackNativeNodeSnapshot terminal;
    CCZ_REQUIRE(host.CaptureNode(group_children.back(), terminal));
    CCZ_REQUIRE_EQ(terminal.instruction_id, std::uint16_t(0));
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 36);

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE_EQ(host.Children(10),
                   (std::vector<std::uintptr_t>{11, 12, 13}));
    CCZ_REQUIRE(!host.NodeExists(marker_handle));
    CCZ_REQUIRE(!host.NodeExists(group_handle));
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 30);
    CCZ_REQUIRE(!transaction.undo_available());
}

CCZ_TEST(battle_writeback_native_transaction_rolls_back_new_4b_group_after_late_failure) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    const auto initial_revision = host.state.document_revision;
    host.FailNext(L"insert");

    BattleWritebackNativeBatch batch;
    batch.ok = true;
    BattleWritebackNativeChange group;
    group.kind = BattleWritebackChangeKind::InsertFourBGroup;
    group.instruction_id = 0x4b;
    group.parent_handle = 10;
    group.insert_after_handle = 12;
    group.insert_before_handle = 13;
    group.insert_terminal_handle = 13;
    group.source_ordinal = -1;
    group.group_label = L"我军出场";
    group.group_records = {std::vector<std::int32_t>(20, 7)};
    batch.changes.push_back(std::move(group));

    BattleWritebackNativeChange insertion;
    insertion.kind = BattleWritebackChangeKind::InsertInstruction;
    insertion.instruction_id = 0x46;
    insertion.parent_handle = 10;
    insertion.insert_after_handle = 12;
    insertion.insert_before_handle = 13;
    insertion.insert_terminal_handle = 13;
    insertion.source_ordinal = -1;
    insertion.int_data.assign(1000, 46);
    batch.changes.push_back(std::move(insertion));

    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    CCZ_REQUIRE(!transaction.Apply(batch, host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    CCZ_REQUIRE_EQ(host.Children(10),
                   (std::vector<std::uintptr_t>{11, 12, 13}));
    CCZ_REQUIRE(host.NodeExists(11));
    CCZ_REQUIRE(host.NodeExists(12));
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 30);
    CCZ_REQUIRE(!host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE_EQ(host.state.document_revision, initial_revision);
    CCZ_REQUIRE(diagnostic.find(L"完整回滚") != std::wstring::npos);
    CCZ_REQUIRE(!host.NodeExists(std::uintptr_t(100)));
}

CCZ_TEST(battle_writeback_native_transaction_rolls_back_after_first_insert_when_second_fails) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    const auto initial_revision = host.state.document_revision;
    host.FailOnAttempt(L"insert", 2);
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;

    CCZ_REQUIRE(!transaction.Apply(native_batch_for_transaction(), host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    const auto children = host.Children(10);
    CCZ_REQUIRE_EQ(children.size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(children[0], std::uintptr_t(11));
    CCZ_REQUIRE_EQ(children[2], std::uintptr_t(13));
    CCZ_REQUIRE(host.NodeExists(children[1]));
    CCZ_REQUIRE(children[1] != std::uintptr_t(12));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
    CCZ_REQUIRE_EQ(host.state.next_ordinal, 30);
    CCZ_REQUIRE(!host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE_EQ(host.state.document_revision, initial_revision);
    CCZ_REQUIRE(diagnostic.find(L"已完整回滚") != std::wstring::npos);
    // The first inserted handle has been removed rather than left as a partial
    // 0x46 command before the second (0x47) insertion failure.
    CCZ_REQUIRE(!host.NodeExists(std::uintptr_t(100)));
}

CCZ_TEST(battle_writeback_native_transaction_rejects_stale_undo_without_mutating) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    if (!transaction.Apply(native_batch_for_transaction(), host, diagnostic))
        throw std::runtime_error("native transaction rejected the batch");
    host.ExternalChange(11, 99);
    const auto changed_revision = host.state.document_revision;

    CCZ_REQUIRE(!transaction.Undo(host, diagnostic));
    CCZ_REQUIRE(transaction.undo_available());
    CCZ_REQUIRE_EQ(host.FirstInt(11), 99);
    CCZ_REQUIRE_EQ(host.state.document_revision, changed_revision);
    CCZ_REQUIRE(diagnostic.find(L"陈旧") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_native_transaction_rolls_back_a_partial_undo) {
    using namespace ccz::editor_preview;
    FakeBattleWritebackNativeHost host;
    BattleWritebackNativeTransaction transaction;
    std::wstring diagnostic;
    if (!transaction.Apply(native_batch_for_transaction(), host, diagnostic))
        throw std::runtime_error("native transaction rejected the batch");

    const auto committed_revision = host.state.document_revision;
    const auto committed_ord = host.state.next_ordinal;
    host.FailOnAttempt(L"swap", 1);
    CCZ_REQUIRE(!transaction.Undo(host, diagnostic));
    CCZ_REQUIRE(transaction.undo_available());
    const auto after_failed_undo = host.Children(10);
    CCZ_REQUIRE_EQ(after_failed_undo.size(), static_cast<std::size_t>(4));
    CCZ_REQUIRE_EQ(after_failed_undo[0], std::uintptr_t(11));
    CCZ_REQUIRE_EQ(after_failed_undo[3], std::uintptr_t(13));
    CCZ_REQUIRE(after_failed_undo[1] != std::uintptr_t(12));
    const auto restored_46 = host.Data(after_failed_undo[1]);
    const auto restored_47 = host.Data(after_failed_undo[2]);
    CCZ_REQUIRE_EQ(restored_46.size(), static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(restored_47.size(), static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(restored_46.front(), 0x46);
    CCZ_REQUIRE_EQ(restored_47.front(), 0x47);
    CCZ_REQUIRE_EQ(host.FirstInt(11), 3);
    CCZ_REQUIRE_EQ(host.state.selected_handle, std::uintptr_t(10));
    CCZ_REQUIRE_EQ(host.state.document_revision, committed_revision);
    CCZ_REQUIRE_EQ(host.state.next_ordinal, committed_ord);
    CCZ_REQUIRE(host.state.custom_modified);
    CCZ_REQUIRE(host.state.framework_modified);
    CCZ_REQUIRE(diagnostic.find(L"整体回滚") != std::wstring::npos);

    CCZ_REQUIRE(transaction.Undo(host, diagnostic));
    CCZ_REQUIRE(!transaction.undo_available());
    CCZ_REQUIRE_EQ(host.Children(10).size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(host.FirstInt(11), 1);
}

CCZ_TEST(battle_writeback_rejects_each_unclosed_gate) {
    const auto state = fixed_state(0x46);
    auto evidence = closed_evidence();
    evidence.native_reload_verified = false;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWriteback(state, evidence).ok);
    evidence = closed_evidence();
    evidence.kind300_external_acceptance_verified = false;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWriteback(state, evidence).ok);
    evidence = closed_evidence();
    evidence.sample_manifest_verified = false;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWriteback(state, evidence).ok);
    evidence = closed_evidence();
    evidence.field_table_verified = false;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWriteback(state, evidence).ok);
    evidence = closed_evidence();
    evidence.group_order_verified = false;
    CCZ_REQUIRE(!ccz::editor_preview::BuildBattleWriteback(state, evidence).ok);
}

CCZ_TEST(battle_writeback_builds_fixed_rows_and_preserves_unedited_values) {
    auto state = fixed_state(0x46);
    state.units[0].raw_fields[7] = 777;
    state.units[0].ai_policy = 777;
    state.units[0].position = {33, 44};
    state.units[0].hidden = true;
    ccz::preview::mark_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::X);
    ccz::preview::mark_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::Y);
    ccz::preview::mark_battle_field_dirty(
        state.units[0], ccz::preview::BattleFieldRole::Hidden);
    const auto build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE(!build.product_writeback_allowed);
    CCZ_REQUIRE_EQ(build.records.size(), static_cast<std::size_t>(1));
    const auto& record = build.records[0];
    CCZ_REQUIRE_EQ(record.instruction_id, static_cast<std::uint16_t>(0x46));
    CCZ_REQUIRE_EQ(record.fields.size(), static_cast<std::size_t>(20 * 11));
    CCZ_REQUIRE_EQ(record.fields[2], 33);
    CCZ_REQUIRE_EQ(record.fields[3], 44);
    CCZ_REQUIRE_EQ(record.fields[1], 1);
    CCZ_REQUIRE_EQ(record.fields[7], 777);
    CCZ_REQUIRE_EQ(record.bytes[0], static_cast<std::uint8_t>(0x46));
    CCZ_REQUIRE_EQ(record.bytes[1], static_cast<std::uint8_t>(0x00));
}

CCZ_TEST(battle_writeback_rejects_incomplete_fixed_group_without_mutating_source) {
    auto state = fixed_state(0x47);
    state.units.pop_back();
    const auto before = state.units[0].raw_fields;
    const auto build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(!build.ok);
    CCZ_REQUIRE_EQ(state.units[0].raw_fields, before);
}

CCZ_TEST(battle_writeback_preserves_first_seen_group_order_and_4b_sources) {
    BattlePreviewState state;
    append_fixed(state, 0x47, L"scope|node:2", 1);
    state.units.push_back(row(0x4b, 0, L"scope|node:4b-first"));
    append_fixed(state, 0x47, L"scope|node:10", 2);
    state.units.push_back(row(0x4b, 0, L"scope|node:4b-second"));
    const auto build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE_EQ(build.records.size(), static_cast<std::size_t>(4));
    CCZ_REQUIRE_EQ(build.records[0].source_key, std::wstring(L"scope|node:2"));
    CCZ_REQUIRE_EQ(build.records[0].page, static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(build.records[1].source_key,
                   std::wstring(L"scope|node:4b-first"));
    CCZ_REQUIRE_EQ(build.records[1].fields.size(), static_cast<std::size_t>(5));
    CCZ_REQUIRE_EQ(build.records[2].source_key, std::wstring(L"scope|node:10"));
    CCZ_REQUIRE_EQ(build.records[2].page, static_cast<std::size_t>(2));
    CCZ_REQUIRE_EQ(build.records[3].source_key,
                   std::wstring(L"scope|node:4b-second"));
    CCZ_REQUIRE_EQ(build.records[3].fields.size(), static_cast<std::size_t>(5));
}

CCZ_TEST(battle_writeback_rejects_multiple_4b_rows_from_one_source) {
    BattlePreviewState state;
    state.units.push_back(row(0x4b, 0, L"scope|one-4b"));
    auto second = row(0x4b, 1, L"scope|one-4b");
    second.page = 1;
    state.units.push_back(std::move(second));
    const auto build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(!build.ok);
    CCZ_REQUIRE(build.diagnostic.find(L"页码") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_preserves_raw_values_until_explicit_dirty_mark) {
    auto state = fixed_state(0x46);
    auto& unit = state.units[0];
    unit.raw_fields[1] = 2;
    unit.hidden = false;
    auto build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE_EQ(build.records[0].fields[1], 2);

    ccz::preview::mark_battle_field_dirty(unit,
                                          ccz::preview::BattleFieldRole::Hidden);
    build = ccz::editor_preview::BuildBattleWriteback(state, closed_evidence());
    CCZ_REQUIRE(build.ok);
    CCZ_REQUIRE_EQ(build.records[0].fields[1], 0);
}

CCZ_TEST(battle_writeback_transaction_rolls_back_battle_modified_and_selection) {
    ccz::editor_preview::BattleWritebackMemoryState current;
    current.battle = fixed_state(0x46);
    current.document_modified = false;
    current.selection = {L"before", 3, 0};
    const auto original = current;
    auto proposed = current;
    proposed.battle.units[0].position = {55, 66};
    proposed.document_modified = true;
    proposed.selection = {L"after", 9, 0};

    ccz::editor_preview::BattleWritebackTransaction transaction;
    CCZ_REQUIRE(transaction.Begin(current));
    CCZ_REQUIRE(transaction.Stage(proposed));
    CCZ_REQUIRE(transaction.Rollback(current));
    CCZ_REQUIRE_EQ(current.battle.units[0].position,
                   original.battle.units[0].position);
    CCZ_REQUIRE_EQ(current.document_modified, original.document_modified);
    CCZ_REQUIRE_EQ(current.selection.source_key, original.selection.source_key);
    CCZ_REQUIRE_EQ(current.selection.record_index, original.selection.record_index);
    CCZ_REQUIRE_EQ(current.selection.page, original.selection.page);
}

CCZ_TEST(battle_writeback_transaction_rejects_invalid_states_and_undoes_commit) {
    ccz::editor_preview::BattleWritebackMemoryState current;
    current.battle = fixed_state(0x46);
    current.selection = {L"before", 3, 2};
    const auto original = current;
    auto proposed = current;
    proposed.battle.current_page = 5;
    proposed.document_modified = true;
    proposed.selection = {L"after", 9, 7};

    ccz::editor_preview::BattleWritebackTransaction transaction;
    CCZ_REQUIRE(!transaction.Stage(proposed));
    CCZ_REQUIRE(!transaction.CommitToMemory(current));
    CCZ_REQUIRE(!transaction.Rollback(current));
    CCZ_REQUIRE(!transaction.UndoToMemory(current));
    CCZ_REQUIRE(transaction.Begin(current));
    CCZ_REQUIRE(!transaction.Begin(current));
    CCZ_REQUIRE(!transaction.CommitToMemory(current));
    CCZ_REQUIRE(transaction.Stage(proposed));
    CCZ_REQUIRE(transaction.CommitToMemory(current));
    CCZ_REQUIRE(!transaction.Rollback(current));
    CCZ_REQUIRE_EQ(current.selection.page, static_cast<std::size_t>(7));
    CCZ_REQUIRE(transaction.UndoToMemory(current));
    CCZ_REQUIRE_EQ(current.battle.current_page, original.battle.current_page);
    CCZ_REQUIRE_EQ(current.document_modified, original.document_modified);
    CCZ_REQUIRE_EQ(current.selection.source_key, original.selection.source_key);
    CCZ_REQUIRE_EQ(current.selection.record_index, original.selection.record_index);
    CCZ_REQUIRE_EQ(current.selection.page, original.selection.page);
    CCZ_REQUIRE(!transaction.UndoToMemory(current));
}

CCZ_TEST(battle_writeback_plan_updates_existing_position_without_losing_other_fields) {
    BattlePreviewState state;
    BattleUnit unit = existing_four_b(L"scope|unit-4b");
    unit.position = {8, 9};
    ccz::preview::mark_battle_field_dirty(unit,
                                         ccz::preview::BattleFieldRole::X);
    ccz::preview::mark_battle_field_dirty(unit,
                                         ccz::preview::BattleFieldRole::Y);
    state.units.push_back(unit);

    auto snapshot = section_snapshot();
    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = source.current_int_data[0] = 0;
    source.origin_int_data[1] = source.current_int_data[1] = 3;
    source.origin_int_data[2] = source.current_int_data[2] = 4;
    source.origin_int_data[3] = source.current_int_data[3] = 1;
    source.origin_int_data[15] = source.current_int_data[15] = 777;
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE(!plan.product_writeback_allowed);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[0], 0);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[1], 8);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 9);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[3], 1);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[15], 777);
}

CCZ_TEST(battle_writeback_position_adjustment_reaches_native_instruction_plan) {
    using ccz::preview::BattleFieldRole;

    BattleEditorState editor;
    editor.scope_root = L"scope";
    editor.battle.map.valid = true;
    editor.battle.map.width = 16;
    editor.battle.map.height = 16;
    editor.battle.terrain.assign(16 * 16, 0);
    // This imported 0x4B record has no Data ID in its native fields. This
    // writeback-planning test is unrelated to terrain classification, so run
    // the position adjustment with the explicit terrain-override mode.
    editor.unrestricted_terrain_mode = true;

    BattleUnit unit = existing_four_b(L"scope|unit-4b");
    unit.position = {3, 4};
    unit.raw_fields = {0, 3, 4, 1, 0};
    unit.editor_origin_fields = unit.raw_fields;
    editor.battle.units.push_back(unit);

    BattleEditorOps operations(editor);
    CCZ_REQUIRE(operations.BeginPositionAdjustment(unit.editor_cache_id).ok);
    CCZ_REQUIRE(operations.ConfirmPositionAdjustment({8, 9}).ok);
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        editor.battle.units[0], BattleFieldRole::X));
    CCZ_REQUIRE(ccz::preview::is_battle_field_dirty(
        editor.battle.units[0], BattleFieldRole::Y));

    auto snapshot = section_snapshot();
    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = source.current_int_data[0] = 0;
    source.origin_int_data[1] = source.current_int_data[1] = 3;
    source.origin_int_data[2] = source.current_int_data[2] = 4;
    source.origin_int_data[3] = source.current_int_data[3] = 1;
    source.origin_int_data[4] = source.current_int_data[4] = 0;
    snapshot.instructions.push_back(std::move(source));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        editor.battle, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[1], 8);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 9);
}

CCZ_TEST(battle_writeback_plan_creates_missing_categories_from_source_less_cache) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x4b, 20, 1, 2, 1));
    state.units.push_back(staged_unit(0x46, 21, 3, 4, 2));
    state.units.push_back(staged_unit(0x47, 22, 5, 6, 3));
    state.units.push_back(staged_unit(0x4b, 23, 7, 8, 4));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", section_snapshot());
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE(!plan.product_writeback_allowed);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(3));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::InsertFourBGroup);
    CCZ_REQUIRE_EQ(plan.changes[0].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(plan.changes[0].group_label, std::wstring(L"我军出场"));
    CCZ_REQUIRE_EQ(plan.changes[0].group_records.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(plan.changes[0].group_records[0].size(), std::size_t(20));
    // The character Data ID is editor-only for 0x4B; field 0 is a
    // deployment slot assigned by the writeback planner.
    CCZ_REQUIRE_EQ(plan.changes[0].group_records[0][0], 0);
    CCZ_REQUIRE_EQ(plan.changes[0].group_records[1][0], 1);
    CCZ_REQUIRE_EQ(plan.changes[0].group_records[1][1], 7);
    CCZ_REQUIRE_EQ(plan.changes[0].group_records[1][2], 8);
    CCZ_REQUIRE_EQ(plan.changes[1].instruction_id, 0x46);
    CCZ_REQUIRE_EQ(plan.changes[1].int_data.size(), static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(plan.changes[1].int_data[0], 21);
    CCZ_REQUIRE_EQ(plan.changes[1].int_data[4], 2);
    CCZ_REQUIRE_EQ(plan.changes[2].instruction_id, 0x47);
    CCZ_REQUIRE_EQ(plan.changes[2].int_data.size(), static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(plan.changes[2].int_data[0], 22);
    CCZ_REQUIRE_EQ(plan.changes[2].int_data[1], 1);
    CCZ_REQUIRE_EQ(plan.changes[0].insert_parent_key,
                   std::wstring(L"scope|event-body"));
    CCZ_REQUIRE_EQ(plan.changes[0].insert_before_key,
                   std::wstring(L"scope|event-end"));
    CCZ_REQUIRE_EQ(plan.changes[0].source_ordinal, -1);
    const auto first_binding = std::find_if(plan.bindings.begin(),
        plan.bindings.end(), [](const auto& binding) {
            return binding.cache_id == 1;
        });
    const auto second_binding = std::find_if(plan.bindings.begin(),
        plan.bindings.end(), [](const auto& binding) {
            return binding.cache_id == 4;
        });
    CCZ_REQUIRE(first_binding != plan.bindings.end());
    CCZ_REQUIRE(second_binding != plan.bindings.end());
    CCZ_REQUIRE_EQ(first_binding->change_index, std::size_t(0));
    CCZ_REQUIRE_EQ(first_binding->record_index, std::size_t(0));
    CCZ_REQUIRE_EQ(second_binding->change_index, std::size_t(0));
    CCZ_REQUIRE_EQ(second_binding->record_index, std::size_t(1));
}

CCZ_TEST(battle_writeback_plan_routes_new_instruction_types_to_distinct_child_events) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x4b, 30, 1, 2, 1));
    state.units.push_back(staged_unit(0x46, 31, 3, 4, 2));
    state.units.push_back(staged_unit(0x47, 32, 5, 6, 3));

    auto snapshot = section_snapshot();
    snapshot.insert_parent_key.clear();
    snapshot.insert_before_key.clear();
    snapshot.has_current_own_deployment_group = true;
    snapshot.insertion_anchors = {
        {0x4b, L"scope|event-4b", L"scope|end-4b", true, {}},
        {0x46, L"scope|event-46", L"scope|end-46", true, {}},
        {0x47, L"scope|event-47", L"scope|end-47", true, {}},
    };

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), std::size_t(3));
    for (const auto& change : plan.changes) {
        const std::wstring suffix = change.instruction_id == 0x4b
            ? L"4b" : change.instruction_id == 0x46 ? L"46" : L"47";
        CCZ_REQUIRE_EQ(change.kind,
                       BattleWritebackChangeKind::InsertInstruction);
        CCZ_REQUIRE_EQ(change.insert_parent_key,
                       L"scope|event-" + suffix);
        CCZ_REQUIRE_EQ(change.insert_before_key,
                       L"scope|end-" + suffix);
    }
}

CCZ_TEST(battle_writeback_plan_rejects_only_matching_unresolved_type_anchor) {
    auto snapshot = section_snapshot();
    snapshot.insert_parent_key.clear();
    snapshot.insert_before_key.clear();
    snapshot.insertion_anchors = {
        {0x4b, L"scope|event-4b", L"scope|end-4b", true, {}},
        {0x46, {}, {}, false, L"0x46 出场指令属于多个子事件，无法唯一选择新增目标"},
    };

    BattlePreviewState four_b_state;
    four_b_state.units.push_back(staged_unit(0x4b, 40, 1, 2, 1));
    const auto four_b_plan = ccz::editor_preview::BuildBattleWritebackPlan(
        four_b_state, L"scope", snapshot);
    CCZ_REQUIRE(four_b_plan.ok);
    CCZ_REQUIRE_EQ(four_b_plan.changes.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(four_b_plan.changes[0].instruction_id, 0x4b);

    BattlePreviewState four_six_state;
    four_six_state.units.push_back(staged_unit(0x46, 41, 3, 4, 2));
    const auto four_six_plan = ccz::editor_preview::BuildBattleWritebackPlan(
        four_six_state, L"scope", snapshot);
    CCZ_REQUIRE(!four_six_plan.ok);
    CCZ_REQUIRE(four_six_plan.changes.empty());
    CCZ_REQUIRE(four_six_plan.diagnostic.find(L"0x46") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_compacts_4b_slots_after_delete_and_appends_new_slots) {
    BattlePreviewState state;
    auto snapshot = section_snapshot();
    for (int slot = 0; slot < 3; ++slot) {
        const std::wstring key = L"scope|unit-" + std::to_wstring(slot);
        auto source = four_b_source(key.c_str(), 10 + slot);
        source.origin_int_data[0] = slot;
        source.origin_int_data[1] = 3 + slot;
        source.origin_int_data[2] = 4 + slot;
        source.origin_int_data[3] = 1;
        source.origin_int_data[4] = 0;
        source.current_int_data = source.origin_int_data;
        snapshot.instructions.push_back(source);

        BattleUnit unit = existing_four_b(key.c_str());
        unit.deployment_index = slot;
        unit.position = {3 + slot, 4 + slot};
        unit.raw_fields = {slot, 3 + slot, 4 + slot, 1, 0};
        unit.editor_origin_fields = unit.raw_fields;
        unit.editor_cache_id = static_cast<std::uint64_t>(slot + 1);
        unit.editor_pending_delete = slot == 1;
        state.units.push_back(std::move(unit));
    }
    state.units.push_back(staged_unit(0x4b, 99, 8, 9, 4));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE(!plan.product_writeback_allowed);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(3));

    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::DeleteInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].source_key,
                   std::wstring(L"scope|unit-1"));
    CCZ_REQUIRE_EQ(plan.changes[1].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[1].source_key,
                   std::wstring(L"scope|unit-2"));
    CCZ_REQUIRE_EQ(plan.changes[1].int_data[0], 1);
    CCZ_REQUIRE_EQ(plan.changes[2].kind,
                   BattleWritebackChangeKind::InsertInstruction);
    CCZ_REQUIRE_EQ(plan.changes[2].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(plan.changes[2].int_data[0], 2);

    CCZ_REQUIRE(std::find(plan.changes[1].affected_cache_ids.begin(),
                          plan.changes[1].affected_cache_ids.end(), 3) !=
                plan.changes[1].affected_cache_ids.end());
    CCZ_REQUIRE(std::any_of(plan.bindings.begin(), plan.bindings.end(),
        [](const auto& binding) {
            return binding.cache_id == 3 && binding.change_index == 1;
        }));
    CCZ_REQUIRE(std::any_of(plan.bindings.begin(), plan.bindings.end(),
        [](const auto& binding) {
            return binding.cache_id == 4 && binding.change_index == 2;
        }));
}

CCZ_TEST(battle_writeback_plan_fills_first_fixed_empty_row_before_adding_page) {
    BattlePreviewState state;
    auto source = fixed_source(0x46, L"scope|friend-page-1", 10);
    auto unit = staged_unit(0x46, 31, 7, 8, 44);
    state.units.push_back(unit);
    auto snapshot = section_snapshot();
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].source_key,
                   std::wstring(L"scope|friend-page-1"));
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[0], 31);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[1], 1);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 7);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[3], 8);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[11], -1);
}

CCZ_TEST(battle_writeback_plan_reuses_empty_data_id_slot_with_stale_payload) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x46, 31, 7, 8, 44));
    auto snapshot = section_snapshot();
    auto source = fixed_source(0x46, L"scope|friend-page-1", 10);

    // Real scripts can retain non-sentinel payload in a fixed row whose
    // DataId is -1.  DataId is the occupancy marker; a new placement replaces
    // the complete selected row while leaving all other rows untouched.
    source.origin_int_data[1] = source.current_int_data[1] = 1;
    source.origin_int_data[2] = source.current_int_data[2] = 19;
    source.origin_int_data[5] = source.current_int_data[5] = 6;
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[0], 31);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[1], 1);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 7);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[5], 3);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[11], -1);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[12], 0);
}

CCZ_TEST(battle_writeback_plan_rejects_stale_source_snapshot_atomically) {
    BattlePreviewState state;
    state.units.push_back(existing_four_b(L"scope|unit-4b"));
    auto snapshot = section_snapshot();
    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = 0;
    source.current_int_data = source.origin_int_data;
    source.current_int_data[4] = 99;
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"来源") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_deletes_single_record_and_clears_fixed_row) {
    BattlePreviewState state;
    BattleUnit four_b = existing_four_b(L"scope|unit-4b");
    four_b.editor_pending_delete = true;
    state.units.push_back(four_b);
    BattleUnit fixed = row(0x46, 0, L"scope|friend-page");
    fixed.data_id = 31;
    fixed.position = {7, 8};
    fixed.direction = 2;
    fixed.fields_decoded = true;
    fixed.raw_fields.assign(11, 0);
    fixed.editor_origin_fields = fixed.raw_fields;
    fixed.raw_fields[0] = fixed.editor_origin_fields[0] = 31;
    fixed.raw_fields[2] = fixed.editor_origin_fields[2] = 7;
    fixed.raw_fields[3] = fixed.editor_origin_fields[3] = 8;
    fixed.raw_fields[4] = fixed.editor_origin_fields[4] = 2;
    fixed.raw_fields[6] = fixed.editor_origin_fields[6] = 2;
    fixed.raw_fields[7] = fixed.editor_origin_fields[7] = 1;
    fixed.raw_fields[8] = fixed.editor_origin_fields[8] = -1;
    fixed.editor_cache_id = 2;
    fixed.editor_deployment_instruction = 0x46;
    fixed.editor_pending_delete = true;
    state.units.push_back(fixed);

    auto snapshot = section_snapshot();
    auto four_b_node = four_b_source(L"scope|unit-4b", 10);
    four_b_node.origin_int_data[0] = 0;
    four_b_node.origin_int_data[1] = 3;
    four_b_node.origin_int_data[2] = 4;
    four_b_node.origin_int_data[3] = 1;
    four_b_node.current_int_data = four_b_node.origin_int_data;
    snapshot.instructions.push_back(four_b_node);
    auto fixed_node = fixed_source(0x46, L"scope|friend-page", 11);
    fixed_node.origin_int_data[0] = 31;
    fixed_node.origin_int_data[1] = 0;
    fixed_node.origin_int_data[2] = 7;
    fixed_node.origin_int_data[3] = 8;
    fixed_node.origin_int_data[4] = 2;
    fixed_node.current_int_data = fixed_node.origin_int_data;
    snapshot.instructions.push_back(fixed_node);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(2));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::DeleteInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].instruction_id, 0x4b);
    CCZ_REQUIRE_EQ(plan.changes[1].kind,
                   BattleWritebackChangeKind::UpdateInstruction);
    CCZ_REQUIRE_EQ(plan.changes[1].int_data[0], -1);
    CCZ_REQUIRE_EQ(plan.changes[1].int_data[4], -1);
}

CCZ_TEST(battle_writeback_plan_rejects_category_migration_without_lossless_mapping) {
    BattlePreviewState state;
    BattleUnit unit = existing_four_b(L"scope|unit-4b");
    unit.editor_deployment_instruction = 0x46;
    state.units.push_back(unit);
    auto snapshot = section_snapshot();
    auto source = four_b_source(L"scope|unit-4b", 10);
    source.origin_int_data[0] = 0;
    source.origin_int_data[1] = 3;
    source.origin_int_data[2] = 4;
    source.origin_int_data[3] = 1;
    source.current_int_data = source.origin_int_data;
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"迁移") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_adds_page_when_fixed_source_is_full) {
    BattlePreviewState state;
    auto source = fixed_source(0x46, L"scope|friend-page-1", 10);
    for (std::size_t index = 0; index < 20; ++index) {
        const std::vector<std::int32_t> fields = {
            100 + static_cast<int>(index), 0, 2 + static_cast<int>(index),
            3, 1, 0, 2, 1, -1, 0, 0};
        set_fixed_source_row(source, index, fields);
        BattleUnit unit = row(0x46, index, L"scope|friend-page-1");
        unit.data_id = fields[0];
        unit.hidden = fields[1] != 0;
        unit.position = {fields[2], fields[3]};
        unit.direction = fields[4];
        unit.level = fields[5];
        unit.unit_type = fields[6];
        unit.ai_policy = fields[7];
        unit.target = fields[8];
        unit.target_x = fields[9];
        unit.target_y = fields[10];
        unit.raw_fields = fields;
        unit.editor_origin_fields = fields;
        unit.editor_cache_id = index + 1;
        unit.editor_deployment_instruction = 0x46;
        state.units.push_back(std::move(unit));
    }
    state.units.push_back(staged_unit(0x46, 500, 11, 12, 99));
    auto snapshot = section_snapshot();
    snapshot.instructions.push_back(source);

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(plan.changes[0].kind,
                   BattleWritebackChangeKind::InsertInstruction);
    CCZ_REQUIRE_EQ(plan.changes[0].instruction_id, 0x46);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data.size(), static_cast<std::size_t>(1000));
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[0], 500);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[2], 11);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[3], 12);
}

CCZ_TEST(battle_writeback_plan_rejects_missing_insert_anchor_without_partial_changes) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x47, 701, 6, 8, 8));
    auto snapshot = section_snapshot();
    snapshot.insert_parent_key.clear();
    snapshot.insert_before_key.clear();

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"插入锚点") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_rejects_insert_anchor_outside_scope) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x46, 701, 6, 8, 8));
    auto snapshot = section_snapshot();
    snapshot.insert_before_key = L"other-section|event-end";

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"Section") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_rejects_duplicate_source_ordinals) {
    BattlePreviewState state;
    auto snapshot = section_snapshot();
    snapshot.instructions.push_back(
        fixed_source(0x46, L"scope|friend-page-1", 10));
    snapshot.instructions.push_back(
        fixed_source(0x46, L"scope|friend-page-2", 10));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", snapshot);
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"顺序") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_rejects_undecoded_staged_unit) {
    BattlePreviewState state;
    BattleUnit unit = staged_unit(0x46, 702, 6, 8, 8);
    unit.fields_decoded = false;
    state.units.push_back(std::move(unit));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", section_snapshot());
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"字段") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_rejects_duplicate_cache_id) {
    BattlePreviewState state;
    state.units.push_back(staged_unit(0x4b, 702, 6, 8, 8));
    state.units.push_back(staged_unit(0x46, 703, 7, 8, 8));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", section_snapshot());
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"缓存 ID") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_rejects_noncontiguous_minion_brush_batch) {
    BattlePreviewState state;
    BattleUnit first = staged_unit(0x46, 358, 6, 8, 8);
    first.editor_minion_brush_first_id = 358;
    first.editor_minion_brush_batch_id = 42;
    BattleUnit third = staged_unit(0x46, 360, 8, 8, 9);
    third.editor_minion_brush_first_id = 358;
    third.editor_minion_brush_batch_id = 42;
    state.units.push_back(std::move(first));
    state.units.push_back(std::move(third));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", section_snapshot());
    CCZ_REQUIRE(!plan.ok);
    CCZ_REQUIRE(plan.changes.empty());
    CCZ_REQUIRE(plan.diagnostic.find(L"不连续") != std::wstring::npos);
}

CCZ_TEST(battle_writeback_plan_accepts_distinct_batches_starting_after_occupied_ids) {
    BattlePreviewState state;
    BattleUnit first = staged_unit(0x46, 359, 6, 8, 8);
    first.editor_minion_brush_first_id = 359;
    first.editor_minion_brush_batch_id = 42;
    BattleUnit second = staged_unit(0x46, 360, 7, 8, 9);
    second.editor_minion_brush_first_id = 359;
    second.editor_minion_brush_batch_id = 42;
    BattleUnit next_batch = staged_unit(0x46, 361, 8, 8, 10);
    next_batch.editor_minion_brush_first_id = 361;
    next_batch.editor_minion_brush_batch_id = 43;
    state.units.push_back(std::move(first));
    state.units.push_back(std::move(second));
    state.units.push_back(std::move(next_batch));

    const auto plan = ccz::editor_preview::BuildBattleWritebackPlan(
        state, L"scope", section_snapshot());
    CCZ_REQUIRE(plan.ok);
    CCZ_REQUIRE_EQ(plan.changes.size(), static_cast<std::size_t>(1));
    CCZ_REQUIRE_EQ(plan.changes[0].instruction_id, 0x46);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[0], 359);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[11], 360);
    CCZ_REQUIRE_EQ(plan.changes[0].int_data[22], 361);
}

CCZ_TEST(battle_editor_brush_batch_start_uses_first_available_id) {
    BattleEditorState state = brush_placement_state();
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    const auto& placed = state.battle.units.back();
    CCZ_REQUIRE_EQ(placed.data_id, 359);
    CCZ_REQUIRE_EQ(placed.editor_minion_brush_first_id, 359);
    CCZ_REQUIRE(placed.editor_minion_brush_batch_id != 0);
}

CCZ_TEST(battle_editor_minion_brush_keeps_wheel_direction_for_every_stamp) {
    BattleEditorState state = brush_placement_state();
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE(ops.CycleDirection(1).ok);
    CCZ_REQUIRE_EQ(state.mouse_direction, 3);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    CCZ_REQUIRE(ops.Place({1, 0}).ok);

    const auto first = std::find_if(state.battle.units.begin(),
        state.battle.units.end(), [](const BattleUnit& unit) {
            return unit.position == ccz::preview::BattleCoord{0, 0};
        });
    const auto second = std::find_if(state.battle.units.begin(),
        state.battle.units.end(), [](const BattleUnit& unit) {
            return unit.position == ccz::preview::BattleCoord{1, 0};
        });
    CCZ_REQUIRE(first != state.battle.units.end());
    CCZ_REQUIRE(second != state.battle.units.end());
    CCZ_REQUIRE_EQ(first->direction, 3);
    CCZ_REQUIRE_EQ(second->direction, 3);
    CCZ_REQUIRE_EQ(state.mouse_direction, 3);
}

CCZ_TEST(battle_editor_position_adjustment_can_change_direction_before_drop) {
    BattleEditorState state = brush_placement_state();
    BattleEditorOps ops(state);
    const auto existing = std::find_if(state.battle.units.begin(),
        state.battle.units.end(), [](const BattleUnit& unit) {
            return !unit.editor_staged_new;
        });
    CCZ_REQUIRE(existing != state.battle.units.end());
    const std::uint64_t cache_id = existing->editor_cache_id;
    const int original_direction = existing->direction;

    CCZ_REQUIRE(ops.BeginPositionAdjustment(cache_id).ok);
    CCZ_REQUIRE_EQ(state.mode, ccz::editor_preview::BattleEditorMode::AdjustPosition);
    CCZ_REQUIRE(ops.CycleDirection(1).ok);
    CCZ_REQUIRE_EQ(state.mode, ccz::editor_preview::BattleEditorMode::AdjustPosition);
    // Wheel input is staged for the position preview; the cached unit only
    // receives the new facing when the target is confirmed.
    CCZ_REQUIRE_EQ(ops.FindUnitByCacheId(cache_id)->direction,
                   original_direction);
    CCZ_REQUIRE(ops.ConfirmPositionAdjustment({6, 6}).ok);

    const BattleUnit* moved = ops.FindUnitByCacheId(cache_id);
    CCZ_REQUIRE(moved != nullptr);
    CCZ_REQUIRE_EQ(moved->position, (ccz::preview::BattleCoord{6, 6}));
    CCZ_REQUIRE_EQ(moved->direction, (original_direction + 1) % 4);
}

CCZ_TEST(battle_editor_brush_stops_at_occupied_batch_boundary) {
    BattleEditorState state = brush_placement_state();
    BattleUnit occupied_gap = existing_four_b(L"scope|occupied-360");
    occupied_gap.data_id = 360;
    occupied_gap.position = {6, 7};
    occupied_gap.raw_fields = {360, 6, 7, 1, 0};
    occupied_gap.editor_origin_fields = occupied_gap.raw_fields;
    state.battle.units.push_back(std::move(occupied_gap));
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    const auto first_batch = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{0, 0}; });
    CCZ_REQUIRE(first_batch != state.battle.units.end());
    CCZ_REQUIRE_EQ(first_batch->data_id, 359);
    const auto first_batch_id = first_batch->editor_minion_brush_batch_id;

    // The next ID belongs to an imported unit. The editor must close this
    // contiguous batch instead of silently skipping to 361 under the same ID.
    CCZ_REQUIRE(!state.selected_minion_brush.has_value());
    CCZ_REQUIRE_EQ(state.selected_minion_brush_batch_id, 0u);
    CCZ_REQUIRE(!ops.Place({1, 0}).ok);

    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE_EQ(state.placement_defaults.data_id, 361);
    CCZ_REQUIRE(ops.Place({2, 0}).ok);
    const auto next_batch = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{2, 0}; });
    CCZ_REQUIRE(next_batch != state.battle.units.end());
    CCZ_REQUIRE_EQ(next_batch->data_id, 361);
    CCZ_REQUIRE_EQ(next_batch->editor_minion_brush_first_id, 361);
    CCZ_REQUIRE(next_batch->editor_minion_brush_batch_id != first_batch_id);
    const auto next_batch_id = next_batch->editor_minion_brush_batch_id;

    CCZ_REQUIRE(ops.Place({3, 0}).ok);
    const auto next_batch_second = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{3, 0}; });
    CCZ_REQUIRE(next_batch_second != state.battle.units.end());
    CCZ_REQUIRE_EQ(next_batch_second->data_id, 362);
    CCZ_REQUIRE_EQ(next_batch_second->editor_minion_brush_first_id, 361);
    CCZ_REQUIRE_EQ(next_batch_second->editor_minion_brush_batch_id,
                   next_batch_id);
}

CCZ_TEST(battle_editor_brush_delete_compacts_only_its_selection_batch) {
    BattleEditorState state = brush_placement_state();
    BattleEditorOps ops(state);

    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE(ops.Place({0, 0}).ok);
    CCZ_REQUIRE(ops.Place({1, 0}).ok);
    CCZ_REQUIRE(ops.SelectMinionBrush(358).ok);
    CCZ_REQUIRE(ops.Place({2, 0}).ok);
    CCZ_REQUIRE(ops.Place({3, 0}).ok);
    CCZ_REQUIRE(ops.DeleteAt({0, 0}).ok);

    const auto second_first_batch = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{1, 0}; });
    const auto first_second_batch = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{2, 0}; });
    const auto second_second_batch = std::find_if(
        state.battle.units.begin(), state.battle.units.end(),
        [](const BattleUnit& unit) { return unit.position == ccz::preview::BattleCoord{3, 0}; });
    CCZ_REQUIRE(second_first_batch != state.battle.units.end());
    CCZ_REQUIRE(first_second_batch != state.battle.units.end());
    CCZ_REQUIRE(second_second_batch != state.battle.units.end());
    CCZ_REQUIRE_EQ(second_first_batch->data_id, 359);
    CCZ_REQUIRE_EQ(first_second_batch->data_id, 361);
    CCZ_REQUIRE_EQ(second_second_batch->data_id, 362);
    CCZ_REQUIRE_EQ(first_second_batch->editor_minion_brush_batch_id,
                   second_second_batch->editor_minion_brush_batch_id);
    CCZ_REQUIRE(second_first_batch->editor_minion_brush_batch_id !=
                first_second_batch->editor_minion_brush_batch_id);
}
