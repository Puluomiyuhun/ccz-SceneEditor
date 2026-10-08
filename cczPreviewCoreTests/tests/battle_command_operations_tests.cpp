#include "test_framework.h"
#include "../../cczEditor2/PreviewIntegration/BattleDynamicMapReplay.h"
#include "../../cczEditor2/PreviewIntegration/BattleMapOperations.h"
namespace {
using namespace ccz::editor_preview;
ccz::preview::NativeReplayItem item(unsigned short id,std::vector<std::int32_t> f,const wchar_t* key) {
 ccz::preview::NativeReplayItem i;i.command.instruction_id=id;i.command.int_data=std::move(f);i.logical_key=key;i.runtime_handle=123;return i;
}
ccz::preview::BattlePreviewState command_baseline() {
 ccz::preview::BattlePreviewState b;b.map.valid=true;b.map.width=10;b.map.height=10;
 ccz::preview::BattleUnit u;u.data_id=7;u.battlefield_index=2;u.editor_cache_id=8;u.fields_decoded=true;u.position={1,1};u.source_instruction=0x47;
 b.units.push_back(u);return b;
}
}
CCZ_TEST(battle_commands_validate_per2_gap_and_status_bit_domain) {
 BattleMapOperationInstruction out;std::wstring diagnostic;
 for(auto id:{0x31,0x4d,0x4e,0x5b,0x5c,0x6b})CCZ_REQUIRE(BuildBattleCommandInstruction(DefaultBattleCommandConfiguration(id),10,10,out,diagnostic));
 auto c=DefaultBattleCommandConfiguration(0x4d);
 for(int invalid:{1024,1025,1026,1027,1278}) {c.fields[1]=invalid;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));CCZ_REQUIRE(out.int_data.empty());}
 c.fields[1]=1030;c.fields[10]=130;CCZ_REQUIRE(BuildBattleCommandInstruction(c,10,10,out,diagnostic));
 c.fields[10]=128;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));
 c.fields[10]=3;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));
 c=DefaultBattleCommandConfiguration(0x4e);c.fields[7]=4;c.fields[9]=10;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));
 c=DefaultBattleCommandConfiguration(0x5b);c.fields[0]=3;c.fields[2]=2;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));
 c=DefaultBattleCommandConfiguration(0x6b);c.fields[2]=109;CCZ_REQUIRE(BuildBattleCommandInstruction(c,10,10,out,diagnostic));c.fields[2]=110;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));
}
CCZ_TEST(battle_commands_project_selected_prefix_and_animation_has_no_damage) {
 const auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x32,{0,7,0,4,4,-1},L"move"),item(0x4d,{2,0,0,4,4,4,4,6,2,1,6,20,3},L"status"),item(0x4d,{1,0,2,0,0,0,0,6,-1,-1,130,0,0},L"cancel"),item(0x4e,{0,1030,0,0,0,0,6,4,0,5,5},L"policy"),item(0x5b,{4,4,5,5,1},L"area"),item(0x5c,{1030},L"unit"),item(0x6b,{4,4,100,1},L"animation")};
 const auto plan=BuildBattleDynamicMapPlan(p,baseline);CCZ_REQUIRE(plan.path_complete);CCZ_REQUIRE_EQ(plan.events.size(),std::size_t(7));
 const auto result=ProjectBattleDynamicMap(baseline,{},plan,L"animation");const auto& details=result.unit_details.at(8);
 CCZ_REQUIRE_EQ(details.hp_change,std::int64_t(-20));CCZ_REQUIRE_EQ(details.mp_change,std::int64_t(-3));
 CCZ_REQUIRE_EQ(details.added_status_bits,4);CCZ_REQUIRE_EQ(details.removed_status_bits,2);CCZ_REQUIRE_EQ(details.attribute_changes.at(2),1);CCZ_REQUIRE_EQ(details.policy,4);
 CCZ_REQUIRE_EQ(result.animations.size(),std::size_t(1));CCZ_REQUIRE_EQ(result.animations.front().effect,100);
 CCZ_REQUIRE(result.highlights.empty());CCZ_REQUIRE_EQ(result.position_origins.at(8).runtime_handle,std::uintptr_t(123));
 CCZ_REQUIRE_EQ(result.position_origins.at(8).source_key,std::wstring(L"move"));CCZ_REQUIRE_EQ(baseline.units[0].position,(ccz::preview::BattleCoord{1,1}));
}
CCZ_TEST(battle_commands_disappear_encoded_battlefield_and_last_position_origin) {
 const auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x32,{0,7,0,2,2,-1},L"earlier"),item(0x55,{0,7,0,3,3,1},L"latest"),item(0x31,{0,1030,0,0,0,0,6},L"gone")};
 const auto result=ProjectBattleDynamicMap(baseline,{},BuildBattleDynamicMapPlan(p,baseline));
 CCZ_REQUIRE(result.battle.units[0].hidden);CCZ_REQUIRE_EQ(result.position_origins.at(8).source_key,std::wstring(L"latest"));CCZ_REQUIRE_EQ(result.position_origins.at(8).instruction_id,std::uint16_t(0x55));
}
CCZ_TEST(battle_commands_invalid_supported_command_stops_before_later_animation) {
 auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x32,{0,7,0,2,2,-1},L"move"),item(0x6b,{10,0,0,0},L"bad"),item(0x6b,{0,0,0,0},L"later")};
 const auto plan=BuildBattleDynamicMapPlan(p,baseline);CCZ_REQUIRE(!plan.path_complete);CCZ_REQUIRE_EQ(plan.events.size(),std::size_t(1));
 CCZ_REQUIRE(ProjectBattleDynamicMap(baseline,{},plan).animations.empty());
}

CCZ_TEST(battle_commands_5b_preserves_victory_and_during_battle_types) {
 auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x5b,{1,1,2,2,1},L"battle"),item(0x5b,{2,2,3,3,0},L"victory")};
 const auto plan=BuildBattleDynamicMapPlan(p,baseline);
 for(const auto& key:{std::wstring(L"battle"),std::wstring(L"victory")}){
  const auto result=ProjectBattleDynamicMap(baseline,{},plan,key);
  CCZ_REQUIRE_EQ(result.highlights.size(),std::size_t(1));CCZ_REQUIRE(result.highlights[0].enabled);
  CCZ_REQUIRE_EQ(result.highlights[0].type_flag,key==L"battle"?1:0);
 }
 CCZ_REQUIRE(ProjectBattleDynamicMap(baseline,{},plan).highlights.empty());
}

CCZ_TEST(battle_commands_position_source_accepts_native20_and_battlefield_modes) {
 auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 std::vector<std::int32_t> full(20,0);full[0]=1;full[2]=2;full[3]=3;full[4]=4;full[5]=-1;
 BattleMapOperationInstruction out;std::wstring diagnostic;CCZ_REQUIRE(BuildBattleCommandInstruction({0x32,full},10,10,out,diagnostic));
 p.items={item(0x32,full,L"battlefield"),item(0x55,{0,1030,0,5,5,-1},L"encoded")};
 const auto plan=BuildBattleDynamicMapPlan(p,baseline);CCZ_REQUIRE(plan.path_complete);const auto result=ProjectBattleDynamicMap(baseline,{},plan);
 CCZ_REQUIRE_EQ(result.battle.units[0].position,(ccz::preview::BattleCoord{5,5}));CCZ_REQUIRE_EQ(result.position_origins.at(8).source_key,std::wstring(L"encoded"));
 full[19]=1;CCZ_REQUIRE(!BuildBattleCommandInstruction({0x32,full},10,10,out,diagnostic));
}

CCZ_TEST(battle_commands_native_pipeline_retains_all_s_commands_only_in_battle_mode) {
 ccz::preview::SectionExecutionModel model;model.boundary_complete=true;
 for(auto id:{0x4d,0x4e,0x5b,0x5c,0x6b}) {
  ccz::preview::ExecutionNode node;node.category=ccz::preview::ExecutionCategory::Ordinary;node.logical_key=L"command-"+std::to_wstring(id);node.command.instruction_id=(std::uint16_t)id;node.command.int_data=DefaultBattleCommandConfiguration((std::uint16_t)id).fields;if(id==0x4d||id==0x4e)node.command.int_data[1]=7;if(id==0x5c)node.command.int_data[0]=7;model.body.push_back(node);
 }
 const ccz::preview::DialogueProfile profile;const ccz::preview::ReplayOptions options;
 const auto battle=ccz::preview::build_native_battle_map_plan_to_end(model,profile,options);
 const auto regular=ccz::preview::build_native_replay_plan_to_end(model,profile,options);
 CCZ_REQUIRE(battle.complete);CCZ_REQUIRE_EQ(battle.items.size(),std::size_t(5));CCZ_REQUIRE(regular.items.empty());
 const auto projected=BuildBattleDynamicMapPlan(battle,command_baseline());
 CCZ_REQUIRE(projected.path_complete);
}

CCZ_TEST(battle_commands_53_single_data_and_encoded_battlefield_retire_without_hp_simulation) {
 const auto baseline=command_baseline();
 for(int selector:{7,1030}) {
  ccz::preview::NativeReplayPlan p;p.complete=true;p.items={item(0x53,{0,selector,0,0,0,0,6,1},L"death")};
  const auto plan=BuildBattleDynamicMapPlan(p,baseline);CCZ_REQUIRE(plan.path_complete);const auto result=ProjectBattleDynamicMap(baseline,{},plan);
  CCZ_REQUIRE(result.battle.units[0].hidden);CCZ_REQUIRE(!result.battle.units[0].editor_pending_delete);CCZ_REQUIRE_EQ(result.battle.units.size(),std::size_t(1));
  CCZ_REQUIRE_EQ(result.unit_details.at(8).retreat_kind,1);CCZ_REQUIRE_EQ(result.unit_details.at(8).hp_change,std::int64_t(0));CCZ_REQUIRE(!baseline.units[0].hidden);
 }
}
CCZ_TEST(battle_commands_53_region_uses_projected_positions_and_55_restores_unit_record) {
 auto baseline=command_baseline();auto second=baseline.units[0];second.data_id=9;second.battlefield_index=3;second.editor_cache_id=10;second.source_instruction=0x46;baseline.units.push_back(second);
 ccz::preview::NativeReplayPlan p;p.complete=true;p.items={item(0x32,{0,7,0,4,4,-1},L"move"),item(0x53,{1,0,4,4,4,4,2,0},L"retreat")};
 const auto retired=ProjectBattleDynamicMap(baseline,{},BuildBattleDynamicMapPlan(p,baseline));CCZ_REQUIRE(retired.battle.units[0].hidden);CCZ_REQUIRE(!retired.battle.units[1].hidden);CCZ_REQUIRE_EQ(retired.unit_details.at(8).retreat_kind,0);
 p.items.push_back(item(0x55,{0,1030,0,5,5,-1},L"revive"));const auto restored=ProjectBattleDynamicMap(baseline,{},BuildBattleDynamicMapPlan(p,baseline));
 CCZ_REQUIRE(!restored.battle.units[0].hidden);CCZ_REQUIRE_EQ(restored.unit_details.at(8).retreat_kind,-1);CCZ_REQUIRE_EQ(restored.battle.units.size(),std::size_t(2));CCZ_REQUIRE_EQ(restored.battle.units[0].position,(ccz::preview::BattleCoord{5,5}));
 BattleMapOperationInstruction out;std::wstring diagnostic;auto configuration=DefaultBattleCommandConfiguration(0x53);CCZ_REQUIRE(BuildBattleCommandInstruction(configuration,10,10,out,diagnostic));configuration.fields[7]=2;CCZ_REQUIRE(!BuildBattleCommandInstruction(configuration,10,10,out,diagnostic));
}

CCZ_TEST(battle_commands_53_then_4c_appears_using_existing_visibility_behavior) {
 const auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x53,{0,7,0,0,0,0,6,0},L"retreat"),item(0x4c,{0,7,0},L"appear")};
 const auto result=ProjectBattleDynamicMap(baseline,{},BuildBattleDynamicMapPlan(p,baseline));CCZ_REQUIRE(!result.battle.units[0].hidden);CCZ_REQUIRE_EQ(result.unit_details.at(8).retreat_kind,-1);
}
#include "../../cczEditor2/PreviewIntegration/BattleFacing.h"
CCZ_TEST(battle_facing_regular_fields_and_special_rejection) {
 using namespace ccz::editor_preview;
 BattleMapOperationInstruction op;std::wstring d;
 CCZ_REQUIRE(BuildBattleCommandInstruction({0x4f,{7,1030,-1,0,1,0}},10,10,op,d));
 CCZ_REQUIRE(BuildBattleCommandInstruction({0x50,{1030,12,0,1}},10,10,op,d));
 for(int person:{-2,1024,1027,1278}) {
  CCZ_REQUIRE(!BuildBattleCommandInstruction({0x4f,{7,person,-1,0,1,0}},10,10,op,d));
  CCZ_REQUIRE(!BuildBattleCommandInstruction({0x50,{person,6,1,1}},10,10,op,d));
 }
 CCZ_REQUIRE(!BuildBattleCommandInstruction({0x50,{7,13,1,1}},10,10,op,d));
}
CCZ_TEST(battle_facing_native_coordinate_rules_and_reference_precedence) {
 using namespace ccz::editor_preview;
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{5,1},2),0);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{9,5},0),1);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{5,9},0),2);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{1,5},0),3);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{6,4},2),1);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{4,6},2),3);
 CCZ_REQUIRE_EQ(BattleFacingDirection({5,5},{5,5},3,true),3);
 CCZ_REQUIRE_EQ(BattleTurnResult(0,3,true,1,false),1);
 CCZ_REQUIRE_EQ(BattleTurnResult(0,3,true,1,true),3);
 CCZ_REQUIRE_EQ(BattleTurnResult(1,3,true,1,true),1);
 CCZ_REQUIRE_EQ(BattleTurnResult(1,-1,false,0,false),1);
}
CCZ_TEST(battle_turn_uses_current_moved_reference_and_restores_on_prefix_rewind) {
 auto b=command_baseline();b.units[0].direction=0;
 auto other=b.units[0];other.editor_cache_id=9;other.data_id=8;other.battlefield_index=3;other.position={1,6};b.units.push_back(other);
 ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x32,{0,8,0,5,1,-1},L"move-B"),item(0x4f,{7,8,3,0,1,1},L"turn-A")};
 auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.battle.units[0].direction,1);
 CCZ_REQUIRE_EQ(result.battle.units[0].position,(ccz::preview::BattleCoord{1,1}));
 CCZ_REQUIRE(!result.position_origins.count(8));
 p.items.resize(1);result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.battle.units[0].direction,0);
}
CCZ_TEST(battle_turn_action_unknown_special_skip_does_not_block_later_events) {
 auto b=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x4f,{7,-2,-1,0,1,0},L"variable"),item(0x50,{-2,6,1,1},L"variable-action"),item(0x4f,{99,7,-1,0,1,0},L"missing"),item(0x4f,{7,99,-1,0,1,0},L"missing-reference"),item(0x4f,{7,-1,3,0,0,0},L"regular"),item(0x50,{7,2,1,1},L"defend")};
 const auto plan=BuildBattleDynamicMapPlan(p,b);CCZ_REQUIRE(plan.path_complete);
 const auto result=ProjectBattleDynamicMap(b,{},plan,L"defend");
 CCZ_REQUIRE_EQ(result.battle.units[0].direction,3);
 CCZ_REQUIRE_EQ(result.unit_details.at(8).action,2);
 CCZ_REQUIRE(!result.diagnostic.empty());
}
CCZ_TEST(battle_turn_action_encoded_battlefield_and_no_damage_or_visibility_change) {
 auto b=command_baseline();b.units[0].hidden=true;b.units[0].direction=2;
 ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x4f,{1030,-1,0,0,0,0},L"north"),item(0x50,{1030,6,1,1},L"attack"),item(0x50,{1030,-1,0,0},L"none")};
 const auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b),L"attack");
 CCZ_REQUIRE_EQ(result.battle.units[0].direction,0);CCZ_REQUIRE(result.battle.units[0].hidden);
 const auto& details=result.unit_details.at(8);CCZ_REQUIRE_EQ(details.action,6);
 CCZ_REQUIRE_EQ(details.hp_change,std::int64_t(0));CCZ_REQUIRE_EQ(details.mp_change,std::int64_t(0));
 p.items.clear();const auto reset=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b),L"attack");CCZ_REQUIRE(reset.unit_details.empty());CCZ_REQUIRE_EQ(reset.battle.units[0].direction,2);
}
CCZ_TEST(battle_mutual_facing_batch_matches_native_pair_and_projects_each_direction) {
 using namespace ccz::editor_preview;
 auto b=command_baseline();auto other=b.units[0];other.data_id=8;other.editor_cache_id=9;other.battlefield_index=3;other.position={5,1};other.direction=0;b.units.push_back(other);
 std::vector<BattleMapOperationInstruction> pair;std::wstring d;
 CCZ_REQUIRE(BuildBattleMutualFacing(b.units,10,10,pair,d));CCZ_REQUIRE_EQ(pair.size(),std::size_t(2));
 CCZ_REQUIRE_EQ(pair[0].int_data,(std::vector<std::int32_t>{7,8,-1,0,1,0}));CCZ_REQUIRE_EQ(pair[1].int_data,(std::vector<std::int32_t>{8,7,-1,0,0,1}));
 ccz::preview::NativeReplayPlan p;p.complete=true;p.items={item(0x4f,pair[0].int_data,L"A-to-B"),item(0x4f,pair[1].int_data,L"B-to-A")};
 const auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.battle.units[0].direction,1);CCZ_REQUIRE_EQ(result.battle.units[1].direction,3);
 b.units[1].position=b.units[0].position;CCZ_REQUIRE(!BuildBattleMutualFacing(b.units,10,10,pair,d));CCZ_REQUIRE(pair.empty());
}

CCZ_TEST(battle_entry_tests_accept_original_person_selectors_without_deployed_unit) {
 BattleMapOperationInstruction out;std::wstring diagnostic;
 for(auto id:{0x25,0x26}){
  auto c=DefaultBattleCommandConfiguration(id);c.fields[0]=1023;
  CCZ_REQUIRE(BuildBattleCommandInstruction(c,10,10,out,diagnostic));
  CCZ_REQUIRE_EQ(out.int_data.size(),id==0x25?std::size_t(3):std::size_t(5));
  for(int person:{0,1023,1024,1025,1026,1027,1028,1277,-2,-4097,-1}){c.fields[0]=person;CCZ_REQUIRE(BuildBattleCommandInstruction(c,10,10,out,diagnostic));CCZ_REQUIRE_EQ(out.int_data[0],person);}
  for(int invalid:{1278,-4098}){c.fields[0]=invalid;CCZ_REQUIRE(!BuildBattleCommandInstruction(c,10,10,out,diagnostic));}
 }
 CCZ_REQUIRE(!BuildBattleCommandInstruction({0x25,{7,10,0}},10,10,out,diagnostic));
 CCZ_REQUIRE(BuildBattleCommandInstruction({0x26,{7,0,0,9,9}},10,10,out,diagnostic));
 CCZ_REQUIRE(!BuildBattleCommandInstruction({0x26,{7,5,0,4,9}},10,10,out,diagnostic));
 CCZ_REQUIRE(!BuildBattleCommandInstruction({0x26,{7,0,0,10,9}},10,10,out,diagnostic));
}

CCZ_TEST(battle_policy_projection_updates_hover_fields_and_rewinds) {
 auto b=command_baseline();b.units[0].ai_policy=2;b.units[0].target=0;
 ccz::preview::NativeReplayPlan p;p.complete=true;p.items={item(0x4e,{0,7,0,0,0,0,6,5,1030,0,0},L"follow")};
 auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.battle.units[0].ai_policy,5);CCZ_REQUIRE_EQ(result.battle.units[0].target,1030);
 p.items.push_back(item(0x4e,{0,7,0,0,0,0,6,4,0,8,9},L"point"));
 result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));
 CCZ_REQUIRE_EQ(result.battle.units[0].ai_policy,4);CCZ_REQUIRE_EQ(result.battle.units[0].target_x,8);CCZ_REQUIRE_EQ(result.battle.units[0].target_y,9);
 p.items.clear();result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(p,b));CCZ_REQUIRE_EQ(result.battle.units[0].ai_policy,2);CCZ_REQUIRE_EQ(b.units[0].ai_policy,2);
}

CCZ_TEST(battle_transient_effects_follow_only_exact_selected_instruction) {
 const auto baseline=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x5b,{1,1,2,2,1},L"predecessor/area"),item(0x5c,{1030},L"unit"),item(0x6b,{3,3,100,0},L"spell"),item(0x32,{0,7,0,4,4,-1},L"move")};
 const auto plan=BuildBattleDynamicMapPlan(p,baseline);
 for(const auto& key:{std::wstring(),std::wstring(L"move"),std::wstring(L"section"),std::wstring(L"unreachable")}){
  const auto result=ProjectBattleDynamicMap(baseline,{},plan,key);
  CCZ_REQUIRE(result.highlights.empty());CCZ_REQUIRE(result.animations.empty());
  CCZ_REQUIRE_EQ(result.battle.units[0].position,(ccz::preview::BattleCoord{4,4}));
 }
 const auto area=ProjectBattleDynamicMap(baseline,{},plan,L"predecessor/area");
 CCZ_REQUIRE_EQ(area.highlights.size(),std::size_t(1));CCZ_REQUIRE(!area.highlights[0].unit_target);CCZ_REQUIRE(area.animations.empty());
 const auto unit=ProjectBattleDynamicMap(baseline,{},plan,L"unit");
 CCZ_REQUIRE_EQ(unit.highlights.size(),std::size_t(1));CCZ_REQUIRE(unit.highlights[0].unit_target);CCZ_REQUIRE(unit.animations.empty());
 const auto spell=ProjectBattleDynamicMap(baseline,{},plan,L"spell");
 CCZ_REQUIRE_EQ(spell.animations.size(),std::size_t(1));CCZ_REQUIRE(spell.highlights.empty());
}

CCZ_TEST(battle_action_is_transient_and_clears_on_other_selection) {
 auto b=command_baseline();ccz::preview::NativeReplayPlan p;p.complete=true;
 p.items={item(0x4f,{7,-1,3,0,1,1},L"turn"),item(0x50,{7,6,1,1},L"attack"),item(0x50,{7,2,1,1},L"defend")};
 const auto plan=BuildBattleDynamicMapPlan(p,b);
 for(const auto& key:{std::wstring(),std::wstring(L"turn"),std::wstring(L"section"),std::wstring(L"other")}){
  const auto result=ProjectBattleDynamicMap(b,{},plan,key);
  CCZ_REQUIRE(result.unit_details.empty());CCZ_REQUIRE_EQ(result.battle.units[0].direction,3);
 }
 CCZ_REQUIRE_EQ(ProjectBattleDynamicMap(b,{},plan,L"attack").unit_details.at(8).action,6);
 CCZ_REQUIRE_EQ(ProjectBattleDynamicMap(b,{},plan,L"defend").unit_details.at(8).action,2);
}

CCZ_TEST(battle_hidden_appear_batch_skips_visible_and_supports_both_identity_modes) {
 auto b=command_baseline();auto visible=b.units[0];visible.hidden=false;visible.data_id=-1;visible.battlefield_index=-1;
 auto data=b.units[0];data.hidden=true;
 auto battlefield=data;battlefield.data_id=-1;battlefield.battlefield_index=3;battlefield.editor_cache_id=9;
 std::vector<BattleMapOperationInstruction> commands;std::size_t skipped;std::wstring diagnostic;
 CCZ_REQUIRE(BuildBattleHiddenAppearBatch({visible,data,battlefield},10,10,commands,skipped,diagnostic));
 CCZ_REQUIRE_EQ(skipped,std::size_t(1));CCZ_REQUIRE_EQ(commands.size(),std::size_t(2));
 CCZ_REQUIRE((commands[0].int_data==std::vector<std::int32_t>{0,7,0}));
 CCZ_REQUIRE((commands[1].int_data==std::vector<std::int32_t>{1,0,3}));
 CCZ_REQUIRE_EQ(BattleMapInstructionFieldCount(0x4c),std::size_t(3));
}
CCZ_TEST(battle_hidden_appear_single_and_no_hidden_are_safe) {
 auto b=command_baseline();std::vector<BattleMapOperationInstruction> commands;std::size_t skipped;std::wstring diagnostic;
 CCZ_REQUIRE(BuildBattleHiddenAppearBatch(b.units,10,10,commands,skipped,diagnostic));CCZ_REQUIRE(commands.empty());
 b.units[0].hidden=true;CCZ_REQUIRE(BuildBattleHiddenAppearBatch(b.units,10,10,commands,skipped,diagnostic));CCZ_REQUIRE_EQ(commands.size(),std::size_t(1));
 ccz::preview::NativeReplayPlan plan;plan.complete=true;plan.items={item(0x4c,commands[0].int_data,L"appear")};
 const auto result=ProjectBattleDynamicMap(b,{},BuildBattleDynamicMapPlan(plan,b));CCZ_REQUIRE(!result.battle.units[0].hidden);
 b.units[0].hidden=false;CCZ_REQUIRE(BuildBattleHiddenAppearBatch(b.units,10,10,commands,skipped,diagnostic));CCZ_REQUIRE(commands.empty());
}
CCZ_TEST(battle_hidden_appear_rejects_invalid_hidden_identity_without_partial_batch) {
 auto b=command_baseline();b.units[0].hidden=true;auto invalid=b.units[0];invalid.data_id=-1;invalid.battlefield_index=-1;
 std::vector<BattleMapOperationInstruction> commands;std::size_t skipped;std::wstring diagnostic;
 CCZ_REQUIRE(!BuildBattleHiddenAppearBatch({b.units[0],invalid},10,10,commands,skipped,diagnostic));CCZ_REQUIRE(commands.empty());
 BattleMapOperationInstruction out;CCZ_REQUIRE(!BuildBattleCommandInstruction({0x4c,{0,1024,0}},10,10,out,diagnostic));
 CCZ_REQUIRE(!BuildBattleCommandInstruction({0x4c,{1,0,250}},10,10,out,diagnostic));
}
