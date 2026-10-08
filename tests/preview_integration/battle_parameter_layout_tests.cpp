#include "../../cczEditor2/pch.h"
#include "../../cczEditor2/PreviewIntegration/BattleCommandParametersPane.h"
#include <iostream>
CWinApp parameterTestApp;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 CHECK(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0));
 CWnd owner;CHECK(owner.CreateEx(0,AfxRegisterWndClass(0),L"Parameter tests",WS_OVERLAPPEDWINDOW,CRect(-3000,-3000,-2200,-2400),nullptr,0));
 CBattleCommandParametersPane pane;
 for(unsigned id:{0x25,0x26,0x4d,0x4e,0x31,0x53,0x5b,0x5c,0x6b,0x50}){
  auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(id);
  for(int area=0;area<2;++area){
   if(id==0x4d)config.fields[0]=area?2:0;
   if(id==0x4e||id==0x31||id==0x53)config.fields[0]=area;
   pane.Open(&owner,config,40,40,[](const auto&){return true;});
   CRect rect;pane.GetWindowRect(rect);CHECK(rect.Width()<850&&rect.Height()<405);
   if(id==0x25||id==0x26)CHECK((pane.GetDlgItem(300)->GetStyle()&WS_VISIBLE)!=0);
   if(id==0x6b)CHECK((pane.GetDlgItem(100)->GetStyle()&WS_VISIBLE)==0);
   pane.Hide();
  }
 }
 int writes=0;ccz::editor_preview::BattleMapOperationInstruction submitted;
 auto config=ccz::editor_preview::DefaultBattleCommandConfiguration(0x4f);config.fields[0]=17;config.fields[2]=1;
 pane.Open(&owner,config,40,40,[&](const auto& command){++writes;submitted=command;return true;},{},{1,2,3});
 CHECK((pane.GetDlgItem(504)->GetStyle()&WS_VISIBLE)!=0);
 ((CButton*)pane.GetDlgItem(504))->SetCheck(BST_UNCHECKED);((CButton*)pane.GetDlgItem(505))->SetCheck(BST_CHECKED);
 pane.SendMessage(WM_COMMAND,MAKEWPARAM(201,BN_CLICKED));
 CHECK(writes==1&&submitted.int_data[0]==17&&submitted.int_data[2]==1&&submitted.int_data[4]==0&&submitted.int_data[5]==1);
 pane.Open(&owner,config,40,40,[&](const auto&){++writes;return true;},{},{1,2,3});pane.SendMessage(WM_COMMAND,MAKEWPARAM(202,BN_CLICKED));CHECK(writes==1);
 for(unsigned id:{0x25,0x26})for(int row:{0,1023,1024,1025,1026,1027,1028,1277,1278,5373,5374}){
  auto entry=ccz::editor_preview::DefaultBattleCommandConfiguration(id);
  pane.Open(&owner,entry,40,40,[&](const auto& command){submitted=command;return true;});
  auto* people=static_cast<CMyComboBox*>(pane.GetDlgItem(300));CHECK(people->GetCount()==5375);
  people->SetCurSel(row);pane.SendMessage(WM_COMMAND,MAKEWPARAM(201,BN_CLICKED));CHECK(submitted.int_data[0]==Per2List2Code(row));
 }
 for(unsigned id:{0x25,0x26})for(const auto& selector:std::vector<std::pair<const wchar_t*,int>>{{L"任何部队",1024},{L"我军当前人物",1027},{L"w0",1028},{L"w249",1277},{L"v0",-2},{L"v4095",-4097},{L"无",-1}}){
  auto entry=ccz::editor_preview::DefaultBattleCommandConfiguration(id);
  pane.Open(&owner,entry,40,40,[&](const auto& command){submitted=command;return true;});
  auto* people=static_cast<CMyComboBox*>(pane.GetDlgItem(300));auto* edit=people->GetDlgItem(0x7F42);CHECK(edit!=nullptr);
  edit->SetWindowText(selector.first);CHECK(people->CommitIndexedInput());pane.SendMessage(WM_COMMAND,MAKEWPARAM(201,BN_CLICKED));CHECK(submitted.int_data[0]==selector.second);
 }
 pane.DestroyWindow();owner.DestroyWindow();std::cout<<"PASS compact role/area/entry parameter layouts; delay checkbox submission; cancel does not write; full entry selectors and name searches preserve original codes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
