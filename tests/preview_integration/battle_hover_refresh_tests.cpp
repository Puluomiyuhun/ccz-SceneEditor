#include <windows.h>
#include <optional>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <string>
using CPoint=POINT;
bool operator==(const POINT&a,const POINT&b){return a.x==b.x&&a.y==b.y;}
bool operator!=(const POINT&a,const POINT&b){return !(a==b);}
struct Cell {int x{},y{};};
bool operator==(const Cell&a,const Cell&b){return a.x==b.x&&a.y==b.y;}
bool operator!=(const Cell&a,const Cell&b){return !(a==b);}
namespace ccz {namespace editor_preview {
std::vector<Cell> BattleSelectionPathCells(Cell a,Cell b){return {a,b};}
}}
enum class BattleMapToolMode {None,CommandPickPoint,CommandPickArea};
struct CSBattlePreviewPage {
 BattleMapToolMode m_battleMapToolMode=BattleMapToolMode::None;
 bool m_mapCommandDragging=false;std::optional<Cell> m_mapCommandEnd;
 bool m_trackingMouseLeave=true,m_panActive=false;
 CPoint m_panStartPoint{},m_hoverPoint{-1,-1};int m_panStartScrollX{},m_panStartScrollY{};
 std::optional<Cell> m_hoverCell;
 struct Obstacle {bool enabled=false,moving=false,gesture_active=false,drag_started=false;int press_x{},press_y{};std::vector<int> items;std::optional<Cell> press_cell;Cell ghost_offset{},anchor{};std::optional<Cell> last_cell;}m_obstacleSelection;
 bool m_selectionGestureActive=false,m_editorOps=true,m_selectionDragStarted=false;
 std::optional<Cell> m_selectionPressCell,m_selectionLastCell,m_selectedUnit;
 CPoint m_selectionPressPoint{};
 bool m_obstacleStampActive=false,m_leftStampActive=false;
 std::optional<Cell> m_obstacleLastStamp;
 int paints=0,selections=0,stamps=0;std::vector<Cell> selectedCells;
 std::wstring m_diagnostic;
 bool IsObstacleGroupTargetValid(Cell){return true;}
 HWND GetSafeHwnd(){return nullptr;}
 void Invalidate(BOOL){++paints;}
 void SetViewportScroll(int,int){++paints;}
 std::optional<Cell> BattleCoordFromScreen(CPoint p){if(p.x<0||p.y<0)return {};return Cell{int(p.x/48),int(p.y/48)};}
 void ToggleObstacleSelectionAt(Cell c,bool){++selections;selectedCells.push_back(c);}
 void ClearBoardSelection(){}
 void SelectBoardCell(Cell,bool){++selections;}
 void StampObstacleAt(Cell){++stamps;}
 void StampAt(Cell){++stamps;}
 void OnMouseMove(UINT flags,CPoint point);
};
// PRODUCTION_HELPER
void require(bool value,const char* text){if(!value){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
int main(){
 for(bool obstacle:{false,true}){
  CSBattlePreviewPage p;p.m_obstacleSelection.enabled=obstacle;
  p.OnMouseMove(0,{49,49});require(p.paints>0,"hover must repaint without clicking in normal and obstacle selection modes");
  require(p.m_hoverCell==std::optional<Cell>({1,1}),"hover cell updates");require(p.selections==0&&p.stamps==0,"idle hover cannot edit");
  p.paints=0;p.OnMouseMove(0,{60,60});require(p.paints>0,"hover card follows pointer within same cell");
  p.paints=0;p.OnMouseMove(0,{60,60});require(p.paints==0,"duplicate coordinates do not request redundant repaint");
  p.paints=0;p.OnMouseMove(0,{-1,-1});require(p.paints>0&&!p.m_hoverCell,"leaving canvas clears stale hover");
 }
 CSBattlePreviewPage p;p.m_obstacleSelection.enabled=true;p.m_obstacleSelection.gesture_active=true;p.m_obstacleSelection.last_cell=Cell{1,1};p.m_obstacleSelection.press_cell=Cell{1,1};p.OnMouseMove(MK_LBUTTON,{100,49});require(p.selections>0,"obstacle selection gesture remains active");
 require(p.selectedCells.front()==Cell{1,1},"drag retains its starting obstacle");
 CSBattlePreviewPage jitter;jitter.m_obstacleSelection.enabled=true;jitter.m_obstacleSelection.gesture_active=true;
 jitter.m_obstacleSelection.press_x=49;jitter.m_obstacleSelection.press_y=49;jitter.m_obstacleSelection.press_cell=Cell{1,1};jitter.m_obstacleSelection.last_cell=Cell{1,1};
 jitter.OnMouseMove(MK_LBUTTON,{50,50});require(!jitter.m_obstacleSelection.drag_started&&jitter.selections==0,"pointer jitter does not start a new selection");
 CSBattlePreviewPage moving;moving.m_obstacleSelection.enabled=true;moving.m_obstacleSelection.moving=true;moving.m_obstacleSelection.anchor={1,1};
 moving.OnMouseMove(0,{144,96});require(moving.m_obstacleSelection.ghost_offset==Cell{2,1},"group ghost follows anchor offset");require(moving.m_hoverCell==std::optional<Cell>({3,2}),"moving group keeps hover coordinates live");
 CSBattlePreviewPage q;q.m_obstacleStampActive=true;q.m_obstacleLastStamp=Cell{1,1};q.OnMouseMove(MK_LBUTTON,{100,49});require(q.stamps>0,"obstacle brush remains active");
 CSBattlePreviewPage area;area.m_battleMapToolMode=BattleMapToolMode::CommandPickArea;area.m_mapCommandDragging=true;
 area.m_obstacleSelection.enabled=true;area.m_obstacleSelection.gesture_active=true;area.m_obstacleSelection.last_cell=Cell{0,0};
 area.OnMouseMove(MK_LBUTTON,{100,49});require(area.m_mapCommandEnd==std::optional<Cell>({2,1}),"area endpoint follows pointer");require(area.selections==0&&area.stamps==0,"area pick cannot alter role or obstacle selection");
 std::cout<<"PASS: production hover refresh, same-cell follow, idle safety, selection and brush\n";
}
