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

CCZ_TEST(battle_adapter_resolves_selected_target_from_nested_condition_path) {
    const auto selected_target = node(15, 0x47);
    const auto marker = node(14, 0x01, {}, {selected_target});
    std::vector<std::int32_t> boolean_fields(50, -1);
    boolean_fields[0] = 700;
    const auto predicate = node(13, 0x05, std::move(boolean_fields), {marker});
    const auto scene_section = section(10, {
        node(11, 0x44, {17, 3}),
        predicate,
        node(16, 0),
    });

    ccz::editor_preview::CapturedEditorTree snapshot;
    snapshot.document_path = L"F:/mod/RS/S_00.eex";
    snapshot.section_path = {0, 0};
    snapshot.selected_path = {1, 0, 0};
    snapshot.selected_handle = 15;
    snapshot.section_root = scene_section;
    snapshot.document_roots = {document_root({node(2, -1, {}, {scene_section})})};

    const auto result = ccz::editor_preview::BuildBattlePreviewInput(
        std::move(snapshot));

    CCZ_REQUIRE(result.ok);
    CCZ_REQUIRE_EQ(result.selected_target_key,
        result.input.nodes[0].children[0].children[0].children[1]
            .children[0].children[0].logical_key);
    CCZ_REQUIRE_EQ(result.input.nodes[0].children[0].children[0].children[1]
        .children[0].children[0].runtime_handle, std::uintptr_t(15));
}

CCZ_TEST(battle_adapter_document_revision_tracks_full_document_content) {
    auto make_snapshot = [] {
        ccz::editor_preview::CapturedEditorTree snapshot;
        snapshot.document_path = L"F:/mod/RS/S_00.eex";
        snapshot.section_path = {0};
        snapshot.section_root = section(
            10, {node(11, 0x44, {17, 3}), node(12, 0)});
        snapshot.document_roots = {document_root({
            section(10, {node(11, 0x44, {17, 3}), node(12, 0)}),
            section(20, {node(21, 0x44, {19, 4}), node(22, 0)})})};
        return snapshot;
    };

    auto baseline_snapshot = make_snapshot();
    const auto baseline =
        ccz::editor_preview::BuildBattlePreviewInput(
            std::move(baseline_snapshot));

    auto changed_snapshot = make_snapshot();
    changed_snapshot.document_roots[0].children[1].children[0].int_data[0] =
        99;
    const auto changed =
        ccz::editor_preview::BuildBattlePreviewInput(
            std::move(changed_snapshot));

    CCZ_REQUIRE(baseline.ok);
    CCZ_REQUIRE(changed.ok);
    CCZ_REQUIRE_EQ(changed.input.section_revision,
                   baseline.input.section_revision);
    CCZ_REQUIRE(changed.input.document_revision !=
                baseline.input.document_revision);
}

CCZ_TEST(battle_adapter_rejects_idle_snapshot_without_section) {
    ccz::editor_preview::CapturedEditorTree snapshot;
    snapshot.document_path = L"F:/mod/RS/S_00.eex";
    const auto result = ccz::editor_preview::BuildBattlePreviewInput(snapshot);

    CCZ_REQUIRE(!result.ok);
    CCZ_REQUIRE(result.diagnostic.find(L"Section") != std::wstring::npos);
}
#include "../../cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.h"
#include "../../cczEditor2/PreviewIntegration/BattleDynamicMapReplay.h"
#include <ccz/preview/native_replay_plan.h>
#include <ccz/preview/battle_instruction_parser.h>
CCZ_TEST(battle_s00_selected_battle_start_preserves_prior_map_commands) {
    using namespace ccz::editor_preview;
    CapturedEditorTree snapshot;
    snapshot.document_path=L"F:/mod/RS/S_00.eex";
    snapshot.section_path={0,0}; snapshot.selected_handle=99;
    std::vector<CapturedEditorNode> commands;
    commands.push_back(node(50,0x19));
    for(int i=0;i<4;++i)commands.push_back(node(60+i,0x58,{0,0,26,7+i,8,0,0}));
    commands.push_back(node(70,0x1a));
    for(int i=0;i<4;++i)commands.push_back(node(80+i,0x32,{0,418+i,0,7+i,8,0}));
    commands.push_back(node(99,0x5a));
    snapshot.selected_path={2,0,0,2,10};
    auto sec=section(10,{node(11,2),node(101,0x44,{0,0}),node(12,0,{},{node(13,1,{},{node(14,2,{}, {node(15,1),node(16,2),node(17,2,{},commands)})})})});
    snapshot.section_root=sec;
    snapshot.document_roots={document_root({node(2,-1,{}, {sec})})};
    auto input=BuildBattlePreviewInput(snapshot); CCZ_REQUIRE(input.ok); CCZ_REQUIRE(!input.selected_target_key.empty());
    ccz::preview::InstructionCatalog catalog; catalog.valid=true; for(unsigned short id : {0,1,2,0x19,0x1a,0x32,0x58,0x5a})catalog.names[id]=L"test"; ccz::preview::ReplayOptions options;
    auto condition=BuildBattleConditionTree(input,catalog,options); CCZ_REQUIRE(condition.ok);
    ccz::preview::SimulationEnvironment terminal;
    auto plan=ccz::preview::build_native_battle_map_plan_through_target(*condition.model,input.selected_target_key,{},options,&terminal);
    CCZ_REQUIRE(plan.complete); CCZ_REQUIRE_EQ(plan.items.size(),std::size_t(8));
    ccz::preview::BattlePreviewState baseline;
    baseline.map.valid=true; baseline.map.projection_valid=true; baseline.map.width=19; baseline.map.height=18;
    for(int i=0;i<4;++i){ccz::preview::BattleUnit unit; unit.data_id=418+i; unit.fields_decoded=true; unit.position={7+i,10}; baseline.units.push_back(unit);}
    const auto dynamic=BuildBattleDynamicMapPlan(plan,baseline);
    CCZ_REQUIRE(dynamic.path_complete); CCZ_REQUIRE_EQ(dynamic.events.size(),std::size_t(8));
    const auto projected=ProjectBattleDynamicMap(baseline,{},dynamic);
    CCZ_REQUIRE_EQ(projected.obstacles.size(),std::size_t(4)); CCZ_REQUIRE_EQ(projected.labels.size(),std::size_t(4));
}





CCZ_TEST(battle_live_s00_tree_replays_to_selected_battle_start) {
    using namespace ccz::editor_preview;
    CapturedEditorTree snapshot;
    snapshot.document_path=L"F:/mod/RS/S_00.eex";
    snapshot.section_path={0,0}; snapshot.selected_path={1,24}; snapshot.selected_handle=91637320;
    auto liveSection=
#include "fixtures/s00_dynamic_live_section.inl"
    ;
    snapshot.section_root=liveSection;
    snapshot.document_roots={document_root({node(2,-1,{}, {liveSection})})};
    auto input=BuildBattlePreviewInput(snapshot); CCZ_REQUIRE(input.ok); CCZ_REQUIRE(!input.selected_target_key.empty());
    ccz::preview::InstructionCatalog catalog; catalog.valid=true; for(unsigned short id=0;id<124;++id)catalog.names[id]=L"test";
    ccz::preview::ReplayOptions options; auto condition=BuildBattleConditionTree(input,catalog,options); CCZ_REQUIRE(condition.ok);
    ccz::preview::SimulationEnvironment terminal;
    auto plan=ccz::preview::build_native_battle_map_plan_through_target(*condition.model,input.selected_target_key,{},options,&terminal);
    CCZ_REQUIRE(plan.complete);
    std::size_t count=0; for(const auto& item:plan.items)if(item.command.instruction_id==0x58||item.command.instruction_id==0x32)++count;
    CCZ_REQUIRE_EQ(count,std::size_t(8));
    ccz::preview::BattleParseOptions parseOptions;
    parseOptions.scope_root_key=input.input.scope_key;
    parseOptions.filter_deployment_nodes=true;
    parseOptions.reachable_deployment_keys=condition.scope.reachable_deployment_keys;
    auto parsed=ccz::preview::parse_battle_instructions(input.input.document_path,input.input.nodes,parseOptions);
    CCZ_REQUIRE(parsed.ok); parsed.state.map.width=19; parsed.state.map.height=18; parsed.state.map.projection_valid=true;
    const auto dynamic=BuildBattleDynamicMapPlan(plan,parsed.state);
    CCZ_REQUIRE(dynamic.path_complete);
    std::size_t mapCount=0;for(const auto& event:dynamic.events)if(event.instruction_id==0x58||event.instruction_id==0x32)++mapCount;
    CCZ_REQUIRE_EQ(mapCount,std::size_t(8));
    const auto projected=ProjectBattleDynamicMap(parsed.state,{},dynamic);
    CCZ_REQUIRE_EQ(projected.obstacles.size(),std::size_t(4));CCZ_REQUIRE_EQ(projected.labels.size(),std::size_t(4));
}

