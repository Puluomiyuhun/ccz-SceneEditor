#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <cstdlib>
#include <iostream>
namespace Gdiplus {struct Bitmap {};}
namespace ccz { namespace preview {struct BattleUnit{std::uint64_t editor_cache_id=1;int data_id=7;};inline const int* battle_visual_arm_kind(const BattleUnit&){static int kind=0;return &kind;}}
namespace editor_preview {
 enum class BattleEditorMode{Select,AdjustPosition,Place};
 struct BattleAssetCache {
  struct UnitSpriteRef{};
  std::wstring path=L"mod/Unit_mov.e5";std::uint64_t revision=100,movRevision=10;int lastAction=-1;
  bool ResolveUnitSprite(int,int,int,UnitSpriteRef*,int action){lastAction=action;return true;}
  const std::wstring& unit_mov_path()const{return path;}
  std::uint64_t sprite_revision()const{return revision;}
  std::uint64_t unit_mov_revision()const{return movRevision;}
 };
}}
// Include the member required by the production sprite resolver.
// The fixture exercises the production cache guard before bitmap decoding.
enum class BattleMapToolMode{None,TurnDirection,DynamicRoleAdjust,RevivePickTarget,ObstaclePlace};
struct CWnd{static void OnKeyDown(UINT,UINT,UINT){}};
struct CSBattlePreviewPage:CWnd {
 ccz::editor_preview::BattleAssetCache m_assets;std::wstring m_unitSpritePath;std::uint64_t m_unitSpriteRevision=0;int clears=0,cancels=0,positionCancels=0;
 bool m_dynamicPreviewEnabled=false,m_dynamicPreviewReady=false;
 struct Details{int action=6;};struct Projection{std::map<std::uint64_t,Details> unit_details;}m_dynamicProjection;
 struct Obstacles{bool enabled=false;int cancelled=0;void CancelGesture(){++cancelled;}}m_obstacleSelection;
 BattleMapToolMode m_battleMapToolMode=BattleMapToolMode::None;
 struct Editor{CSBattlePreviewPage* owner;int CancelPositionAdjustment(){++owner->positionCancels;return 0;}int CancelPlacementBrush(){return 0;}}editor{this};Editor* m_editorOps=&editor;
 struct Session{struct State{ccz::editor_preview::BattleEditorMode mode=ccz::editor_preview::BattleEditorMode::Select;}editor_state;}session;
 Session& ActiveSession(){return session;}
 void ClearUnitSprites(){++clears;}
 void CancelBattleMapTool(bool){++cancels;m_battleMapToolMode=BattleMapToolMode::None;}
 CSBattlePreviewPage* GetCapture(){return nullptr;}
 void ReleaseCapture(){}void Invalidate(BOOL){}void ShowOperationResult(int){}void RefreshEditorControls(){}void RefreshCharacterListIfDeploymentChanged(){}
 Gdiplus::Bitmap* GetUnitSprite(const ccz::preview::BattleUnit&,int,ccz::editor_preview::BattleAssetCache::UnitSpriteRef*);
 void OnKeyDown(UINT,UINT,UINT);void CancelConflictingBattleEditorMode();
};
// SPRITE_GUARD
// KEY_HANDLER
// TOOL_HANDOFF
void require(bool v,const char* msg){if(!v){std::cerr<<"FAIL: "<<msg<<'\n';std::exit(1);}}
int main(){
 CSBattlePreviewPage p;ccz::preview::BattleUnit unit;ccz::editor_preview::BattleAssetCache::UnitSpriteRef ref;
 p.GetUnitSprite(unit,0,&ref);p.GetUnitSprite(unit,1,&ref);require(p.clears==1,"repeated sprite fetch must retain cache");
 ++p.m_assets.revision;p.GetUnitSprite(unit,0,&ref);require(p.clears==2,"asset revision change invalidates once");
 p.m_assets.path=L"other/Unit_mov.e5";p.GetUnitSprite(unit,0,&ref);require(p.clears==3,"Mod switch invalidates cache");
 p.m_obstacleSelection.enabled=true;p.m_battleMapToolMode=BattleMapToolMode::DynamicRoleAdjust;p.OnKeyDown(VK_ESCAPE,0,0);
 require(p.cancels==1&&p.m_battleMapToolMode==BattleMapToolMode::None,"Escape cancels active role tool despite obstacle selection");
 p.session.editor_state.mode=ccz::editor_preview::BattleEditorMode::AdjustPosition;p.OnKeyDown(VK_ESCAPE,0,0);require(p.positionCancels==1,"Escape cancels native role adjustment first");
 p.session.editor_state.mode=ccz::editor_preview::BattleEditorMode::Select;p.OnKeyDown(VK_ESCAPE,0,0);require(p.m_obstacleSelection.cancelled==1,"Escape still cancels obstacle gesture");
 p.CancelConflictingBattleEditorMode();require(!p.m_obstacleSelection.enabled,"starting another editor tool releases obstacle pointer selection");
 std::cout<<"PASS: production sprite cache revision and Escape ownership\n";
}
