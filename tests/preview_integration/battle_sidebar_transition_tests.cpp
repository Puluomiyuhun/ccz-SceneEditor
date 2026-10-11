#include <afxwin.h>
#include <vector>
#include <memory>
#include <iostream>
#include <stdexcept>
CWinApp application;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
enum class BattleMapToolMode {None, Other};
struct TestSession {bool stale=false;};
struct TestSelection {bool enabled=true;std::vector<int> items{1,2};void CancelGesture(){}};
struct TestOps {
 bool* dynamic=nullptr; int character=-1,brush=-1;
 bool SelectCharacter(int id){CHECK(!*dynamic);character=id;return true;}
 bool SelectMinionBrush(int id){CHECK(!*dynamic);brush=id;return true;}
};
class TestSidebar {
public:
 CListBox m_characterList;
 std::unique_ptr<TestOps> m_editorOps;
 TestSession session;
 TestSelection m_obstacleSelection;
 bool m_dynamicPreviewEnabled=true,m_brushTabActive=false;
 bool m_autoExitForCharacterSelection=false;
 int exits=0,results=0;
 BattleMapToolMode m_battleMapToolMode=BattleMapToolMode::None;
 TestSession& ActiveSession(){return session;}
 void OnDynamicPreviewClicked(){CHECK(m_autoExitForCharacterSelection);++exits;m_dynamicPreviewEnabled=false;m_characterList.ResetContent();}
 void ShowOperationResult(bool result){CHECK(result);++results;}
 void OnCharacterSelectionChanged();
};
// PRODUCTION_SELECTION
void run(bool brush) {
 CWnd parent;CHECK(parent.CreateEx(0,AfxRegisterWndClass(0),L"Sidebar test",WS_POPUP,CRect(0,0,300,400),nullptr,0));
 TestSidebar page;page.m_brushTabActive=brush;
 page.m_editorOps=std::make_unique<TestOps>();page.m_editorOps->dynamic=&page.m_dynamicPreviewEnabled;
 CHECK(page.m_characterList.Create(WS_CHILD|LBS_NOTIFY,CRect(0,0,200,300),&parent,101));
 int row=page.m_characterList.AddString(L"selected");page.m_characterList.SetItemData(row,532);page.m_characterList.SetCurSel(row);
 page.OnCharacterSelectionChanged();CHECK(page.exits==1);CHECK(page.results==1);
 CHECK(!page.m_dynamicPreviewEnabled);CHECK(!page.m_autoExitForCharacterSelection);CHECK(!page.m_obstacleSelection.enabled);CHECK(page.m_obstacleSelection.items.empty());
 CHECK((brush?page.m_editorOps->brush:page.m_editorOps->character)==532);
 CHECK((brush?page.m_editorOps->character:page.m_editorOps->brush)==-1);
 parent.DestroyWindow();
}
int main(){try{CHECK(AfxWinInit(GetModuleHandleW(nullptr),nullptr,GetCommandLineW(),0));run(false);run(true);std::cout<<"BATTLE_SIDEBAR_TRANSITION_PASS: character and minion selection exits dynamic before placement; captured ID survives list refresh\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;}
