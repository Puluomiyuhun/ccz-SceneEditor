#include <ccz/preview/battle_field_spec.h>

bool CSBattlePreviewPage::RequireImmediateInstructionSelection(){
 auto* frame=DYNAMIC_DOWNCAST(CMainFrame,AfxGetMainWnd());auto* child=frame?frame->MDIGetActive():nullptr;
 auto* view=child?DYNAMIC_DOWNCAST(CcczEditor2View,child->GetActiveView()):nullptr;
 if(!view){AfxMessageBox(L"请先选择需要插入新指令的位置",MB_OK|MB_ICONINFORMATION);return false;}
 std::wstring diagnostic;return RequireBattleInsertionSelection(*view,diagnostic);
}

void CSBattlePreviewPage::OnMapCommands() {
 m_regionToolsExpanded=!m_regionToolsExpanded;LayoutEditorControls();
}
void CSBattlePreviewPage::OnRegionTool(UINT controlId){
 const unsigned index=controlId-0xA180;
 if(index==8){OpenOwnUnitBindings();return;}
 const std::uint16_t ids[]={0x4d,0x4e,0x5b,0x6b,0x31,0x53,0x25,0x26};
 if(index<8)BeginMapCommandSelection(ids[index]);
}
void CSBattlePreviewPage::OnClearRoleDrafts(){
 if(!m_editorOps)return;
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();
 auto& state=ActiveSession().editor_state;
 const auto previousGroup=state.selection_group;
 state.selection_group.clear();
 for(const auto& unit:state.battle.units)if(unit.editor_staged_new&&!unit.editor_pending_delete)
  state.selection_group.push_back({unit.source_key,unit.record_index,static_cast<std::size_t>(unit.page),unit.editor_cache_id});
 if(state.selection_group.empty()){state.selection_group=previousGroup;return;}
 const auto result=m_editorOps->DeleteSelectionGroup();
 if(!result.ok)state.selection_group=previousGroup;
 ShowOperationResult(result);RefreshCharacterListIfDeploymentChanged();RefreshEditorControls();
}

void CSBattlePreviewPage::BeginMapCommandSelection(std::uint16_t id) {
 if(!RequireImmediateInstructionSelection())return;
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();m_commandParametersPane.Hide();
 m_mapCommandId=id;
 m_battleMapToolMode=(id==0x25||id==0x6b)?BattleMapToolMode::CommandPickPoint:BattleMapToolMode::CommandPickArea;
 m_diagnostic=m_battleMapToolMode==BattleMapToolMode::CommandPickPoint
  ?L"请点击目标格，随后设置参数；右键或Esc取消。"
  :L"请按住左键框选区域，松开后设置参数；右键或Esc取消。";
 RefreshEditorControls();Invalidate(FALSE);
}
void CSBattlePreviewPage::OpenBattleCommand(std::uint16_t id,bool area,std::uint64_t cacheId,bool group,
 std::optional<ccz::preview::BattleCoord> start,std::optional<ccz::preview::BattleCoord> end) {
 using namespace ccz::editor_preview;
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();
 auto config=DefaultBattleCommandConfiguration(id);
 std::vector<ccz::preview::BattleUnit> units;std::size_t anchor=0;std::wstring diagnostic;
 if(!area&&!CaptureRoleOperationUnits(cacheId,group,units,anchor,diagnostic)){AfxMessageBox(diagnostic.c_str());return;}
 const auto& map=EditingBattle().map;
 if(area) {
  if(!start||!end)return;
  const int left=(std::min)(start->x,end->x),top=(std::min)(start->y,end->y);
  const int right=(std::max)(start->x,end->x),bottom=(std::max)(start->y,end->y);
  if(id==0x25){config.fields[1]=left;config.fields[2]=top;}
  else if(id==0x6b){config.fields[0]=left;config.fields[1]=top;}
  else {
   int offset=0;
   if(id==0x4d){config.fields[0]=2;offset=3;}
   else if(id==0x4e||id==0x31||id==0x53){config.fields[0]=1;offset=2;}
   else if(id==0x26)offset=1;
   config.fields[offset]=left;config.fields[offset+1]=top;
   config.fields[offset+2]=right;config.fields[offset+3]=bottom;
  }
 }else{
  const auto& unit=units[anchor];
  if(id==0x5c||id==0x4f||id==0x50)config.fields[0]=BattlePersonForUnit(unit);
  else if(id==0x6b){config.fields[0]=unit.position.x;config.fields[1]=unit.position.y;}
  else config.fields[1]=BattlePersonForUnit(unit);
 }
 std::vector<std::wstring> names(1024);
 for(const auto& character:ActiveSession().editor_state.characters.entries)if(character.data_id>=0&&character.data_id<1024)names[character.data_id]=character.name;
 std::vector<std::wstring> entryPeople;
 if(id==0x25||id==0x26){
  auto* frame=DYNAMIC_DOWNCAST(CMainFrame,AfxGetMainWnd());auto* child=frame?frame->MDIGetActive():nullptr;
  auto* view=child?DYNAMIC_DOWNCAST(CcczEditor2View,child->GetActiveView()):nullptr;
  if(view){entryPeople.reserve(5375);for(int row=0;row<5375;++row)entryPeople.emplace_back(view->per2[row]);}
 }
 const auto documentId=ActiveSession().document_id;
 const auto epoch=m_activeDocument?m_activeDocument->GetPreviewContentEpoch():0;
 m_commandParametersPane.Open(this,config,map.width,map.height,
  [this,units,area,documentId,epoch](const BattleMapOperationInstruction& edited){
   if(ActiveSession().document_id!=documentId||!m_activeDocument||m_activeDocument->GetPreviewContentEpoch()!=epoch){AfxMessageBox(L"剧本已变化，请重新打开操作框");return false;}
   std::vector<BattleMapOperationInstruction> commands;
   if(area||units.size()<=1)commands.push_back(edited);
   else {
    const auto id=edited.instruction_id;
    if((id==0x4d&&edited.int_data[0]==2)||((id==0x4e||id==0x31||id==0x53)&&edited.int_data[0]==1)){AfxMessageBox(L"批量角色操作不能切换为区域；请从地图工具打开区域操作");return false;}
    std::set<std::pair<int,int>> spellCells;
    for(const auto& unit:units){
     auto config=ccz::editor_preview::BattleCommandConfiguration{id,edited.int_data};
     if(id==0x6b){if(!spellCells.insert({unit.position.x,unit.position.y}).second)continue;config.fields[0]=unit.position.x;config.fields[1]=unit.position.y;}
     else if(id==0x4d&&config.fields[0]==1){if(unit.battlefield_index<0){AfxMessageBox(L"所选角色缺少战场编号");return false;}config.fields[2]=unit.battlefield_index;}
     else {
      const int source=(id==0x5c||id==0x4f||id==0x50)?edited.int_data[0]:edited.int_data[1];const bool battlefield=source>=1028;
      if(battlefield&&unit.battlefield_index<0){AfxMessageBox(L"所选角色缺少战场编号");return false;}
      config.fields[(id==0x5c||id==0x4f||id==0x50)?0:1]=battlefield?1028+unit.battlefield_index:BattlePersonForUnit(unit);
     }
     BattleMapOperationInstruction command;std::wstring problem;
     if(!ccz::editor_preview::BuildBattleCommandInstruction(config,edited.map_width,edited.map_height,command,problem)){AfxMessageBox(problem.c_str());return false;}
     commands.push_back(std::move(command));
    }
   }
   std::wstring problem;const bool ok=ApplyBattleMapOperations(commands,problem);if(ok)m_commandParametersPane.Hide();return ok;
  },names,{},entryPeople);
}

void CSBattlePreviewPage::ApplyHiddenBattleAppear(std::uint64_t cacheId,bool group){
 using namespace ccz::editor_preview;
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();
 const auto& state=ActiveSession().editor_state;
 const auto& selection=m_dynamicPreviewEnabled?m_dynamicSelection:state.selection_group;
 std::vector<ccz::preview::BattleUnit> units;
 // Read hidden state at submission, from the currently displayed projection.
 for(const auto& current:EditingBattle().units){
  const bool chosen=group?std::any_of(selection.begin(),selection.end(),[&](const auto& selected){return selected.cache_id==current.editor_cache_id;}):current.editor_cache_id==cacheId;
  if(!chosen)continue;
  auto unit=current;
  if(unit.source_instruction==0x4b){
   const auto binding=ActiveSession().own_unit_bindings.find(unit.deployment_index);
   if(unit.data_id<0 && binding!=ActiveSession().own_unit_bindings.end())unit.data_id=binding->second;
   if(m_roleSelectorBattlefield)unit.data_id=-1;
  }
  units.push_back(unit);
 }
 std::vector<BattleMapOperationInstruction> commands;std::size_t skipped=0;std::wstring problem;
 if(!BuildBattleHiddenAppearBatch(units,EditingBattle().map.width,EditingBattle().map.height,commands,skipped,problem)){AfxMessageBox(problem.c_str());return;}
 if(commands.empty()){m_diagnostic=L"没有可出现的隐藏角色，未隐藏角色已跳过；未写入指令";Invalidate(FALSE);return;}
 const auto count=commands.size();
 if(ApplyBattleMapOperations(commands,problem)){
  m_diagnostic=(L"隐藏武将出现：已写入 "+std::to_wstring(count)+L" 条，跳过 "+std::to_wstring(skipped)+L" 个未隐藏角色").c_str();Invalidate(FALSE);
 }
}

void CSBattlePreviewPage::BeginDynamicRoleAdjustment(std::uint64_t cacheId,bool group) {
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();
 std::wstring diagnostic;
 if(!CaptureRoleOperationUnits(cacheId,group,m_roleOperationUnits,m_roleOperationAnchorIndex,diagnostic)){AfxMessageBox(diagnostic.c_str());return;}
 m_dynamicAdjustOrigins=m_dynamicProjection.position_origins;
 m_dynamicAdjustEpoch=m_activeDocument?m_activeDocument->GetPreviewContentEpoch():0;
 m_dynamicAdjustGeneration=m_activeDocument?m_activeDocument->GetPreviewTreeGeneration():0;
 m_roleOperationDirection=m_roleOperationUnits[m_roleOperationAnchorIndex].direction;
 m_roleOperationDirectionAdjusted=false;m_battleMapToolMode=BattleMapToolMode::DynamicRoleAdjust;
 m_diagnostic=L"调整位置：点击目标格，滚轮调整朝向；右键或Esc取消。确认修改各角色最近一次位置来源指令。";
 for(const auto& unit:m_roleOperationUnits){auto found=m_dynamicAdjustOrigins.find(unit.editor_cache_id);if(found!=m_dynamicAdjustOrigins.end())m_diagnostic+= (L" 来源："+found->second.source_key).c_str();}
 Invalidate(FALSE);
}

bool CSBattlePreviewPage::ApplyDynamicUpdateBatch(ccz::editor_preview::BattleWritebackNativeBatch& batch,std::wstring& diagnostic) {
 auto* frame=DYNAMIC_DOWNCAST(CMainFrame,AfxGetMainWnd());auto* child=frame?frame->MDIGetActive():nullptr;
 auto* view=child?DYNAMIC_DOWNCAST(CcczEditor2View,child->GetActiveView()):nullptr;auto* document=view?view->GetDocument():nullptr;
 if(!document||document->GetSPreviewDocumentId()!=ActiveSession().document_id||ActiveSession().stale||ActiveSession().native_writeback_rebase_required||HasPendingPreviewDrafts()){diagnostic=L"当前来源不可修改，请先写回暂存单位或重新读取";return false;}
 const auto section=reinterpret_cast<HTREEITEM>(ActiveSession().baseline_capture.snapshot.section_root.runtime_handle);
 MfcBattleWritebackNativeHost host(*view,section);BattlePreviewRefreshSuppression suppress(*view);
 if(!ActiveSession().native_writeback_transaction.Apply(batch,host,diagnostic))return false;
 document->MarkEditorContentChanged();ActiveSession().native_writeback_rebase_required=true;
 if(RebaseBattlePreviewAfterNativeWriteback(*view,section,diagnostic))RefreshDynamicPreview(view->GetTreeCtrl(),view->GetTreeCtrl().GetSelectedItem(),document->pathName);
 RefreshEditorControls();Invalidate(FALSE);return true;
}

bool CSBattlePreviewPage::ConfirmDynamicRoleAdjustment(const ccz::preview::BattleCoord& target) {
 using namespace ccz::editor_preview;using ccz::preview::BattleFieldRole;
 auto reject=[](const std::wstring& text){AfxMessageBox(text.c_str());return false;};
 if(!m_dynamicPreviewEnabled||!m_dynamicPreviewReady||!m_activeDocument||m_activeDocument->GetPreviewContentEpoch()!=m_dynamicAdjustEpoch||m_activeDocument->GetPreviewTreeGeneration()!=m_dynamicAdjustGeneration)return reject(L"调整期间剧本或预览位置已变化，请重新选择");
 if(m_roleOperationUnits.empty()||m_roleOperationAnchorIndex>=m_roleOperationUnits.size())return false;
 const auto anchor=m_roleOperationUnits[m_roleOperationAnchorIndex].position;
 const long long dx=(long long)target.x-anchor.x,dy=(long long)target.y-anchor.y;
 auto* frame=DYNAMIC_DOWNCAST(CMainFrame,AfxGetMainWnd());auto* child=frame?frame->MDIGetActive():nullptr;
 auto* view=child?DYNAMIC_DOWNCAST(CcczEditor2View,child->GetActiveView()):nullptr;if(!view)return false;
 const auto section=reinterpret_cast<HTREEITEM>(ActiveSession().baseline_capture.snapshot.section_root.runtime_handle);
 MfcBattleWritebackNativeHost host(*view,section);BattleWritebackNativeBatch batch;batch.ok=true;
 const auto& map=EditingBattle().map;
 auto findNode=[&](const ccz::preview::LogicalNodeKey& key)->const ccz::preview::RawReplayNode*{
  std::function<const ccz::preview::RawReplayNode*(const std::vector<ccz::preview::RawReplayNode>&)> scan;
  scan=[&](const auto& nodes)->const ccz::preview::RawReplayNode*{for(const auto& node:nodes){if(node.logical_key==key)return &node;if(auto* result=scan(node.children))return result;}return nullptr;};
  return scan(ActiveSession().baseline_capture.input.nodes);
 };
 for(const auto& unit:m_roleOperationUnits){
  const long long x=unit.position.x+dx,y=unit.position.y+dy;
  if(x<0||y<0||x>=map.width||y>=map.height)return reject(L"调整后角色组超出地图范围");
  const int direction=m_roleOperationDirectionAdjusted?m_roleOperationDirection:unit.direction;
  auto positionOrigin=m_dynamicAdjustOrigins.find(unit.editor_cache_id);
  std::uintptr_t handle=0;std::vector<std::int32_t> expected;
  if(positionOrigin!=m_dynamicAdjustOrigins.end()){handle=positionOrigin->second.runtime_handle;expected=positionOrigin->second.source_fields;}
  else {auto* node=findNode(unit.source_key);if(!node)return reject(L"无法定位初始出场指令");handle=node->runtime_handle;expected=node->command.int_data;}
  BattleWritebackNativeNodeSnapshot source;if(!host.CaptureNode(handle,source)||source.int_data!=expected)return reject(L"对应位置来源指令已改变，请重新选择");
  auto found=std::find_if(batch.changes.begin(),batch.changes.end(),[&](const auto& c){return c.item_handle==handle;});
  if(found==batch.changes.end()){
   BattleWritebackNativeChange change;change.kind=BattleWritebackChangeKind::UpdateInstruction;change.instruction_id=source.instruction_id;change.item_handle=handle;change.parent_handle=source.parent_handle;change.source_ordinal=source.ordinal;change.original_int_data=source.int_data;change.int_data=source.int_data;change.map_width=map.width;change.map_height=map.height;batch.changes.push_back(std::move(change));found=std::prev(batch.changes.end());
  }
  if(positionOrigin!=m_dynamicAdjustOrigins.end()){
   if(found->int_data.size()<6)return reject(L"位置来源字段不足");found->int_data[3]=(int)x;found->int_data[4]=(int)y;found->int_data[5]=direction;
  }else{
   const auto* spec=ccz::preview::get_battle_field_spec(source.instruction_id);if(!spec||!spec->semantic_complete)return reject(L"初始出场指令字段未完整解析");
   const auto offset=unit.record_index*spec->record_stride;
   if(offset+spec->record_stride>found->int_data.size())return reject(L"初始出场记录已失效");
   // Initial layout keeps its existing collision rule; moved/revived units do not use occupancy gates.
   for(const auto& other:m_dynamicScene1Baseline.units){if(other.editor_pending_delete||other.editor_cache_id==unit.editor_cache_id)continue;const bool ownGroup=std::any_of(m_roleOperationUnits.begin(),m_roleOperationUnits.end(),[&](const auto& u){return u.source_key==other.source_key&&u.record_index==other.record_index;});if(!ownGroup&&other.position==ccz::preview::BattleCoord{(int)x,(int)y})return reject(L"初始布置目标格已有其他角色");}
   for(std::size_t i=0;i<spec->record_stride;++i){const auto role=spec->field_roles[i];if(role==BattleFieldRole::X)found->int_data[offset+i]=(int)x;else if(role==BattleFieldRole::Y)found->int_data[offset+i]=(int)y;else if(role==BattleFieldRole::Direction)found->int_data[offset+i]=direction;}
  }
 }
 std::wstring diagnostic;if(!ApplyDynamicUpdateBatch(batch,diagnostic))return reject(diagnostic);
 m_diagnostic=L"位置及朝向已更新到对应来源指令；可整组撤回";return true;
}

void CSBattlePreviewPage::DrawEnhancedBattleOverlays(Gdiplus::Graphics& graphics,const ccz::preview::BattlePreviewState& battle,int ox,int oy,float cell) {
 const auto saved=graphics.Save();graphics.SetClip(Gdiplus::Rect(m_canvasRect.left,m_canvasRect.top,m_canvasRect.Width(),m_canvasRect.Height()),Gdiplus::CombineModeReplace);
 if(m_mapCommandStart&&m_mapCommandEnd&&m_battleMapToolMode==BattleMapToolMode::CommandPickArea){
  const auto a=*m_mapCommandStart,b=*m_mapCommandEnd;
  Gdiplus::RectF rectangle(ox+(std::min)(a.x,b.x)*cell,oy+(std::min)(a.y,b.y)*cell,(std::abs(a.x-b.x)+1)*cell,(std::abs(a.y-b.y)+1)*cell);
  Gdiplus::SolidBrush fill(Gdiplus::Color(65,115,165,245));Gdiplus::Pen edge(Gdiplus::Color(255,145,190,255),2.0f);
  graphics.FillRectangle(&fill,rectangle);graphics.DrawRectangle(&edge,rectangle);
 }
 Gdiplus::Font font(L"Microsoft YaHei",(std::max)(7.0f,(std::min)(10.0f,cell*.2f)),Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
 Gdiplus::StringFormat format;format.SetAlignment(Gdiplus::StringAlignmentCenter);format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
 Gdiplus::SolidBrush white(Gdiplus::Color(255,255,255,255)),draftFill(Gdiplus::Color(235,143,54,92));
 auto draftLabel=[&](ccz::preview::BattleCoord at){float h=(std::max)(12.0f,(std::min)(17.0f,cell*.3f));Gdiplus::RectF r(ox+at.x*cell+2,oy+(at.y+1)*cell-h-2,cell-4,h);graphics.FillRectangle(&draftFill,r);graphics.DrawString(L"暂存",-1,&font,r,&format,&white);};
 // Both new deployments and edits to existing source fields await Writeback.
 for(const auto& unit:ActiveSession().editor_state.battle.units)
  if(unit.fields_decoded && !unit.editor_pending_delete &&
     (unit.editor_staged_new || !unit.dirty_fields.empty()))draftLabel(unit.position);
 for(const auto& item:m_obstacleDrafts.items())draftLabel(item.coordinate);
 Gdiplus::Pen dark(Gdiplus::Color(255,15,47,43),(std::max)(3.0f,m_zoom*3));
 Gdiplus::Pen turquoise(Gdiplus::Color(255,53,214,197),(std::max)(1.5f,m_zoom*1.5f));
 for(const auto& item:m_obstacleSelection.items){auto at=item.value.coordinate;if(m_obstacleSelection.moving){at.x+=m_obstacleSelection.ghost_offset.x;at.y+=m_obstacleSelection.ghost_offset.y;}
  Gdiplus::RectF r(ox+at.x*cell+1,oy+at.y*cell+1,cell-2,cell-2);graphics.DrawRectangle(&dark,r);graphics.DrawRectangle(&turquoise,r);
 }
 if(m_dynamicPreviewEnabled&&m_dynamicPreviewReady){
  Gdiplus::SolidBrush light(Gdiplus::Color(55,250,225,115));Gdiplus::Pen edge(Gdiplus::Color(255,250,225,115),(std::max)(2.0f,m_zoom*2));
  for(const auto& item:m_dynamicProjection.highlights){auto a=item.coordinate,b=item.area_end;if(item.unit_target){const auto unit=std::find_if(battle.units.begin(),battle.units.end(),[&](const auto& u){return u.editor_cache_id==item.cache_id;});if(unit==battle.units.end()||unit->hidden)continue;a=b=unit->position;}
   Gdiplus::RectF r(ox+a.x*cell+1,oy+a.y*cell+1,(b.x-a.x+1)*cell-2,(b.y-a.y+1)*cell-2);graphics.FillRectangle(&light,r);graphics.DrawRectangle(&edge,r);
  }
  for(const auto& spell:m_spellPreviews) {
   if(spell.asset.frames.empty()){Gdiplus::RectF r(ox+spell.coordinate.x*cell+2,oy+spell.coordinate.y*cell+2,cell-4,cell-4);graphics.DrawEllipse(&edge,r);continue;}
   const auto frame=(std::min)(m_spellFrame,spell.asset.frames.size()-1);
   const float w=spell.asset.width*m_zoom,h=spell.asset.height*m_zoom;
   const auto offset=frame<spell.asset.frame_offsets.size()?spell.asset.frame_offsets[frame]:std::pair<std::int32_t,std::int32_t>{0,0};
   graphics.DrawImage(spell.asset.frames[frame].get(),Gdiplus::RectF(ox+(spell.coordinate.x+.5f)*cell-w*.5f+offset.first*m_zoom,oy+(spell.coordinate.y+.5f)*cell-h*.5f+offset.second*m_zoom,w,h));
  }
 }
 graphics.Restore(saved);
}

void CSBattlePreviewPage::SyncBattleSpellAnimations() {
 std::wstring signature=m_documentPath.GetString();
 for(const auto& item:m_dynamicProjection.animations)signature+=L"|"+item.source_key+L":"+std::to_wstring(item.effect)+L":"+std::to_wstring(item.coordinate.x)+L","+std::to_wstring(item.coordinate.y);
 if(signature==m_spellSignature)return;
 m_spellSignature=signature;KillTimer(0xB601);m_spellPreviews.clear();m_spellFrame=0;
 std::map<int,ccz::editor_preview::BattleSpellAnimationAsset> effects;
 std::size_t pixelsAllocated=0;
 for(const auto& item:m_dynamicProjection.animations){
  auto found=effects.find(item.effect);
  if(found==effects.end()) {
   auto asset=ccz::editor_preview::LoadBattleSpellAnimation(battle_mod_root(m_documentPath.GetString()),item.effect);
   const auto bytes=asset.frames.size()*std::size_t(asset.width)*asset.height*4;
   if(bytes>128u*1024u*1024u-pixelsAllocated){asset.frames.clear();asset.diagnostic=L"法术动画缓存达到128MB；其余效果保留位置标记";}else pixelsAllocated+=bytes;
   found=effects.emplace(item.effect,std::move(asset)).first;
  }
  SpellPreview spell;spell.coordinate=item.coordinate;spell.asset=found->second;
  if(!spell.asset.diagnostic.empty())AppendDiagnostic(spell.asset.diagnostic);
  m_spellPreviews.push_back(std::move(spell));
 }
 if(std::any_of(m_spellPreviews.begin(),m_spellPreviews.end(),[](const auto& p){return p.asset.frames.size()>1;}))SetTimer(0xB601,80,nullptr);
}
void CSBattlePreviewPage::OnTimer(UINT_PTR id){
 if(id==0xB603){
  KillTimer(id);
  if(m_activeSession && m_editorOps){
   std::vector<std::wstring> names(1024);
   for(const auto& character:ActiveSession().editor_state.characters.entries)
    if(character.data_id>=0&&character.data_id<1024)names[character.data_id]=character.name;
   m_commandParametersPane.Prepare(this,names);
  }
  return;
 }
 if(id==0xB602){KillTimer(id);m_brushRefreshPending=false;if(m_obstacleStampActive)UpdateObstacleDraftControls();InvalidateRect(m_canvasRect,FALSE);UpdateWindow();return;}
 if(id!=0xB601){CWnd::OnTimer(id);return;}
 if(!m_dynamicPreviewEnabled||!m_dynamicPreviewReady){KillTimer(id);m_spellPreviews.clear();m_spellSignature.clear();return;}
 ++m_spellFrame;
 const bool playing=std::any_of(m_spellPreviews.begin(),m_spellPreviews.end(),[this](const auto& p){return m_spellFrame+1<p.asset.frames.size();});
 if(!playing)KillTimer(id);InvalidateRect(m_canvasRect,FALSE);
}

void CSBattlePreviewPage::OpenOwnUnitBindings(int initialSlot) {
    if(!m_activeSession||!ActiveSession().has_baseline)return;
    CBattleOwnUnitBindingDialog dialog;
    dialog.baseline=m_dynamicPreviewEnabled&&m_dynamicScene1Baseline.map.valid?m_dynamicScene1Baseline:ActiveSession().editor_state.battle;
    dialog.bindings=ActiveSession().own_unit_bindings;
    for(auto& unit:dialog.baseline.units)if(unit.source_instruction==0x4b&&!unit.editor_staged_new&&dialog.bindings.count(unit.deployment_index))unit.data_id=-1;
    dialog.initial_slot=initialSlot;dialog.names.resize(1024);
    for(const auto& character:ActiveSession().editor_state.characters.entries)if(character.data_id>=0&&character.data_id<1024)dialog.names[character.data_id]=character.name;
    if(dialog.Show(this)!=IDOK)return;
    ActiveSession().own_unit_bindings=std::move(dialog.bindings);
    ActiveSession().editor_state.own_unit_bindings=ActiveSession().own_unit_bindings;
    if(m_dynamicPreviewEnabled&&m_sourceTreeControl)RefreshDynamicPreview(*m_sourceTreeControl,m_sourceTreeControl->GetSelectedItem(),m_documentPath.GetString());
    else AppendDiagnostic(L"我军预览绑定已更新，按绑定 DATA 显示模型及角色信息");
    Invalidate(FALSE);
}

void CSBattlePreviewPage::ApplyBatchBattleDirection(std::uint64_t cacheId,int direction,bool group) {
 if(direction<0||direction>3)return;
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();
 std::vector<ccz::preview::BattleUnit> units;std::size_t anchor=0;std::wstring diagnostic;
 if(!CaptureRoleOperationUnits(cacheId,group,units,anchor,diagnostic)){AfxMessageBox(diagnostic.c_str());return;}
 std::vector<ccz::editor_preview::BattleMapOperationInstruction> commands;
 const auto& map=EditingBattle().map;
 for(const auto& unit:units){
  ccz::editor_preview::BattleMapOperationInstruction command;
  if(!ccz::editor_preview::BuildBattleCommandInstruction(
      {0x4f,{ccz::editor_preview::BattlePersonForUnit(unit),-1,direction,0,1,1}},
      map.width,map.height,command,diagnostic)){AfxMessageBox(diagnostic.c_str());return;}
  commands.push_back(std::move(command));
 }
 if(!commands.empty())OpenFacingDelaySettings(std::move(commands));
}

void CSBattlePreviewPage::OpenFacingDelaySettings(std::vector<ccz::editor_preview::BattleMapOperationInstruction> commands){
 if(commands.empty())return;
 const auto documentId=ActiveSession().document_id;
 const auto epoch=m_activeDocument?m_activeDocument->GetPreviewContentEpoch():0;
 auto config=ccz::editor_preview::BattleCommandConfiguration{0x4f,commands.front().int_data};
 config.fields[5]=commands.back().int_data[5];
 const auto& map=EditingBattle().map;
 m_commandParametersPane.Open(this,config,map.width,map.height,
  [this,commands,documentId,epoch](const auto& edited)mutable{
   if(ActiveSession().document_id!=documentId||!m_activeDocument||m_activeDocument->GetPreviewContentEpoch()!=epoch){AfxMessageBox(L"剧本已变化，请重新选择角色");return false;}
   for(std::size_t i=0;i<commands.size();++i){commands[i].int_data[4]=i==0?edited.int_data[4]:0;commands[i].int_data[5]=i+1==commands.size()?edited.int_data[5]:0;}
   std::wstring diagnostic;return ApplyBattleMapOperations(commands,diagnostic);
  },{}, {1,2,3});
}

void CSBattlePreviewPage::BeginBattleFacing(std::uint64_t cacheId,bool group,bool target) {
 CancelBattleMapTool(true);CancelConflictingBattleEditorMode();m_obstacleSelection.enabled=false;std::wstring diagnostic;
 if(!CaptureRoleOperationUnits(cacheId,group,m_roleOperationUnits,m_roleOperationAnchorIndex,diagnostic)){AfxMessageBox(diagnostic.c_str());return;}
 m_roleOperationDirection=m_roleOperationUnits[m_roleOperationAnchorIndex].direction;
 if(m_roleOperationDirection<0 || m_roleOperationDirection>3)m_roleOperationDirection=0;
 m_battleMapToolMode=target?BattleMapToolMode::TurnPickTarget:BattleMapToolMode::TurnDirection;
 m_wheelRemainder=0;m_diagnostic=target?L"朝向武将：点击目标角色后确认；右键或Esc取消":L"调整朝向：滚轮旋转，左键确认写入；右键或Esc取消";Invalidate(FALSE);
}
bool CSBattlePreviewPage::ApplyBattleFacing(const ccz::preview::BattleUnit* reference) {
 using namespace ccz::editor_preview;
 std::vector<BattleMapOperationInstruction> commands;std::wstring diagnostic;const auto& map=EditingBattle().map;
 for(const auto& unit:m_roleOperationUnits){
  BattleMapOperationInstruction command;
  if(!BuildBattleCommandInstruction({0x4f,{BattlePersonForUnit(unit),reference?BattlePersonForUnit(*reference):-1,reference?-1:m_roleOperationDirection,0,1,1}},map.width,map.height,command,diagnostic)){AfxMessageBox(diagnostic.c_str());return false;}
  commands.push_back(std::move(command));
 }
 if(commands.empty())return false;
 CancelBattleMapTool(true);OpenFacingDelaySettings(std::move(commands));return true;
}
int CSBattlePreviewPage::PreviewBattleFacing(const ccz::preview::BattleUnit& unit) const {
 using namespace ccz::editor_preview;
 if(m_battleMapToolMode!=BattleMapToolMode::TurnDirection && m_battleMapToolMode!=BattleMapToolMode::TurnPickTarget)return unit.direction;
 if(std::none_of(m_roleOperationUnits.begin(),m_roleOperationUnits.end(),[&](const auto& u){return u.editor_cache_id==unit.editor_cache_id;}))return unit.direction;
 if(m_battleMapToolMode==BattleMapToolMode::TurnDirection)return m_roleOperationDirection;
 if(m_hoverCell){const auto hit=HitTestUnit(*m_hoverCell);if(hit && *hit<EditingBattle().units.size()){const auto& reference=EditingBattle().units[*hit];return BattleFacingDirection(unit.position,reference.position,unit.direction,reference.editor_cache_id==unit.editor_cache_id);}}
 return unit.direction;
}
