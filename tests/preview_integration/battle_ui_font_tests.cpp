#include "../../cczEditor2/pch.h"
#include "../../cczEditor2/PreviewIntegration/BattleObstacleBatchPane.h"
#include "../../cczEditor2/PreviewIntegration/BattleObstacleParametersPane.h"
#include "../../cczEditor2/PreviewIntegration/BattleCommandParametersPane.h"
#include "../../cczEditor2/PreviewIntegration/BattleSectionRelationDialog.h"
#include "../../cczEditor2/PreviewIntegration/BattleOwnUnitBindingDialog.h"
#include <iostream>
CWinApp fontTestApp;
static bool fontsGood(HWND parent){
 bool good=true;
 EnumChildWindows(parent,[](HWND hwnd,LPARAM data)->BOOL{
  wchar_t cls[80]{};GetClassName(hwnd,cls,80);
  if(wcscmp(cls,L"Button")&&wcscmp(cls,L"Static")&&wcscmp(cls,L"Edit")&&wcscmp(cls,L"ComboBox")&&wcscmp(cls,L"SysListView32"))return TRUE;
  HFONT font=(HFONT)SendMessage(hwnd,WM_GETFONT,0,0);LOGFONT value{};
  if(!font||!GetObject(font,sizeof(value),&value)||wcscmp(value.lfFaceName,L"Microsoft YaHei")){*(bool*)data=false;std::wcerr<<L"Incorrect font: "<<cls<<L" id="<<GetDlgCtrlID(hwnd)<<L"\n";}
  return TRUE;
 },(LPARAM)&good);return good;
}
struct SectionProbe:ccz::editor_preview::BattleSectionRelationDialog{bool good=false;BOOL OnInitDialog()override{BattleSectionRelationDialog::OnInitDialog();good=fontsGood(m_hWnd);EndDialog(IDOK);return TRUE;}};
struct BindingProbe:CBattleOwnUnitBindingDialog{bool good=false;BOOL OnInitDialog()override{CBattleOwnUnitBindingDialog::OnInitDialog();good=fontsGood(m_hWnd);EndDialog(IDOK);return TRUE;}};
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 CHECK(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0));
 CWnd owner;CHECK(owner.CreateEx(0,AfxRegisterWndClass(0),L"font test",WS_OVERLAPPEDWINDOW,CRect(-3000,-3000,-2200,-2400),nullptr,0));
 ccz::editor_preview::BattleObstacleDraft value;std::vector<std::wstring> terrains{L"平原",L"火焰"};
 CBattleObstacleBatchPane batch;batch.Open(&owner,value,terrains,[](const auto&,const auto&){return true;});CHECK(fontsGood(batch.m_hWnd));batch.Hide();batch.Open(&owner,value,terrains,[](const auto&,const auto&){return true;});CHECK(fontsGood(batch.m_hWnd));batch.Hide();
 CBattleObstacleParametersPane obstacle;obstacle.Open(&owner,true,value,terrains,[](const auto&){return true;});CHECK(fontsGood(obstacle.m_hWnd));obstacle.Hide();
 CBattleCommandParametersPane command;command.Open(&owner,ccz::editor_preview::DefaultBattleCommandConfiguration(0x25),40,40,[](const auto&){return true;});CHECK(fontsGood(command.m_hWnd));command.Hide();
 for(bool overview:{false,true}){SectionProbe dialog;dialog.overview=overview;dialog.Show(&owner);CHECK(dialog.good);}
 for(int slot:{-1,0}){BindingProbe dialog;dialog.initial_slot=slot;dialog.Show(&owner);CHECK(dialog.good);}
 CMyComboBox combo;CHECK(combo.Create(WS_CHILD|CBS_DROPDOWNLIST,CRect(0,0,200,200),&owner,999));combo.AddString(L"0: 测试");combo.EnablePersonSearch();CFont font;font.CreatePointFont(90,L"Microsoft YaHei");combo.SetFont(&font);CHECK(fontsGood(combo.m_hWnd));
 combo.DestroyWindow();command.DestroyWindow();obstacle.DestroyWindow();batch.DestroyWindow();owner.DestroyWindow();
 std::cout<<"PASS: actual obstacle batch/single, command, Section select/overview, binding single/overview and late searchable-combo fonts\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
