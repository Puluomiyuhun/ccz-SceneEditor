#pragma once
#include <afxwin.h>
#include <functional>
#include <algorithm>
#include <limits>
#include <cerrno>
#include <initializer_list>
#include <cwchar>
#include "BattleMapOperations.h"
#include "../CMyComboBox.h"
#include "../myTool.h"

// Native S semantics, with the shared instruction builder as the apply gate.
class CBattleCommandParametersPane : public CWnd {
public:
 using Configuration=ccz::editor_preview::BattleCommandConfiguration;
 using Instruction=ccz::editor_preview::BattleMapOperationInstruction;
 using Handler=std::function<bool(const Instruction&)>;
 void Prepare(CWnd* owner,const std::vector<std::wstring>& data_names){
  if(owner==nullptr || (GetSafeHwnd() && IsWindowVisible()) ||
     (GetSafeHwnd() && prepared_names_==data_names &&
      person_cache_kind_[0]==1 && person_cache_kind_[1]==2 &&
      person_cache_kind_[8]==1))return;
  Open(owner,ccz::editor_preview::DefaultBattleCommandConfiguration(0x4e),1,1,
       {},data_names,{}, {},false);
  Open(owner,ccz::editor_preview::DefaultBattleCommandConfiguration(0x4f),1,1,
       {},data_names,{}, {},false);
  if(GetSafeHwnd())prepared_names_=data_names;
 }
 void Open(CWnd* owner,const Configuration& config,int width,int height,Handler handler,
           const std::vector<std::wstring>& data_names={},const std::vector<int>& extra_locked={},const std::vector<std::wstring>& entry_people={},bool show=true) {
  if(!GetSafeHwnd()) {
   auto cls=AfxRegisterWndClass(CS_DBLCLKS,LoadCursor(nullptr,IDC_ARROW),(HBRUSH)(COLOR_BTNFACE+1));
   if(!CreateEx(WS_EX_TOOLWINDOW,cls,L"S 战场指令参数",WS_POPUP|WS_CAPTION|WS_SYSMENU,CRect(0,0,850,405),owner,0)){AfxMessageBox(L"无法创建参数窗口");return;}
   font_.CreatePointFont(90,L"Microsoft YaHei");
   for(int i=0;i<13;++i){int x=12+(i%3)*278,y=14+(i/3)*40;
    labels_[i].Create(L"",WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE,CRect(x,y,x+106,y+26),this);
    edits_[i].Create(WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,CRect(x+108,y,x+260,y+26),this,100+i);
    combos_[i].Create(WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,CRect(x+108,y,x+260,y+250),this,300+i);
    labels_[i].SetFont(&font_);edits_[i].SetFont(&font_);combos_[i].SetFont(&font_);
    toggles_[i].Create(L"",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,CRect(x,y,x+260,y+26),this,500+i);toggles_[i].SetFont(&font_);
   }
   const wchar_t* names[]={L"麻痹",L"封咒",L"混乱",L"中毒",L"状态5",L"状态6"};
   for(int i=0;i<6;++i){checks_[i].Create(names[i],WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,CRect(12+i*134,220,138+i*134,247),this,400+i);checks_[i].SetFont(&font_);}
   note_.Create(L"",WS_CHILD|WS_VISIBLE,CRect(12,255,820,329),this);note_.SetFont(&font_);
   apply_.Create(L"应用指令",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,CRect(12,337,166,366),this,201);apply_.SetFont(&font_);
   cancel_.Create(L"关闭",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,CRect(180,337,320,366),this,202);cancel_.SetFont(&font_);
  }
  config_=config;width_=width;height_=height;handler_=std::move(handler);
  if(data_names!=cached_data_names_ || entry_people!=cached_entry_people_){
   cached_data_names_=data_names;cached_entry_people_=entry_people;
   std::fill(std::begin(person_cache_kind_),std::end(person_cache_kind_),0);
  }
  const auto defaults=ccz::editor_preview::DefaultBattleCommandConfiguration(config.instruction_id);
  if(config_.fields.size()==20)config_.fields.resize(defaults.fields.size());
  if(config_.fields.size()!=defaults.fields.size())config_=defaults;
  CString title;title.Format(L"S %02X %s",config.instruction_id,Title(config.instruction_id));SetWindowText(title);
  const auto names=Names(config.instruction_id);
  for(int i=0;i<13;++i){combo_field_[i]=false;bool visible=i<(int)config_.fields.size();labels_[i].ShowWindow(visible?SW_SHOW:SW_HIDE);edits_[i].ShowWindow(visible?SW_SHOW:SW_HIDE);combos_[i].ShowWindow(SW_HIDE);if(visible){labels_[i].SetWindowText(names[i]);edits_[i].SetWindowText(std::to_wstring(config_.fields[i]).c_str());}}
  const auto id=config_.instruction_id;
  if(id==0x25||id==0x26){EntryPeople(0,data_names,entry_people);
  } else if(id==0x4d){Options(0,{L"指定角色",L"战场编号",L"区域部队"});People(1,data_names);Faction(7);
   Options(8,{L"攻击",L"防御",L"精神",L"爆发",L"士气",L"移动力",L"不改变"},{0,1,2,3,4,5,-1});
   Options(9,{L"下降",L"正常",L"上升",L"不改变"},{0,1,2,-1});
   Options(10,{L"不改变",L"赋予状态",L"取消状态"},{-1,0,128});
   combos_[10].SetCurSel(config_.fields[10]<0?0:(config_.fields[10]&128)?2:1);
  } else if(id==0x4e){Options(0,{L"指定角色",L"区域部队"});People(1,data_names);Faction(6);Options(7,{L"被动出击",L"主动出击",L"坚守原地",L"攻击武将",L"到指定点",L"跟随武将",L"逃到指定点"});People(8,data_names);
  } else if(id==0x31||id==0x53){Options(0,{L"指定角色",L"区域部队"});People(1,data_names);Faction(6);if(id==0x53)Options(7,{L"撤退",L"死亡"});
  } else if(id==0x4f){People(0,data_names);People(1,data_names,false,true);
   Options(2,{L"朝北",L"朝东",L"朝南",L"朝西",L"默认方向"},{0,1,2,3,-1});
   Options(3,{L"转向",L"不转向"});Options(4,{L"无延迟",L"动作前延迟"});Options(5,{L"无延迟",L"动作后延迟"});
  } else if(id==0x50){People(0,data_names);Options(1,{L"静止",L"举起武器",L"防御",L"受攻击",L"虚弱",L"攻击预备",L"攻击",L"二次攻击",L"慢速转圈",L"喘气",L"晕倒",L"快速转圈",L"中速转圈",L"无"},{0,1,2,3,4,5,6,7,8,9,10,11,12,-1});Options(2,{L"无延迟",L"动作前延迟"});Options(3,{L"无延迟",L"动作后延迟"});
  } else if(id==0x5b){Options(4,{L"胜利条件",L"战斗中"});
  } else if(id==0x5c){People(0,data_names);
  } else if(id==0x6b){combo_field_[2]=true;combos_[2].ResetContent();values_[2].clear();person_cache_kind_[2]=0;for(int i=0;i<110;++i){CString label;if(i<100)label.Format(L"MEff-%d",i+1);else label.Format(L"MCall-%02d.e5",i-100);combos_[2].AddString(label);values_[2].push_back(i);}SelectValue(2,config_.fields[2]);Options(3,{L"不切换视点",L"切换视点"});}
  for(int i=0;i<13;++i)if(combo_field_[i]){const int selected=combos_[i].GetCurSel();combos_[i].EnablePersonSearch();combos_[i].SetCurSel(selected);edits_[i].ShowWindow(SW_HIDE);combos_[i].ShowWindow(SW_SHOW);}
  for(int i=0;i<6;++i){checks_[i].ShowWindow(id==0x4d?SW_SHOW:SW_HIDE);checks_[i].SetCheck(id==0x4d&&config_.fields[10]>=0&&(config_.fields[10]&(2<<i))?BST_CHECKED:BST_UNCHECKED);}
  note_.SetWindowText(id==0x25||id==0x26?L"角色列表与原编辑器一致，含 DATA、功能角色、战场编号、变量和无，不要求已经在场。坐标使用战场格子坐标；区域包含两端。\n写入的是条件测试指令，不会直接移动角色。":id==0x4f?L"指定参照武将时，按参照关系计算朝向；无参照时使用指定方向。\n复用原指令的不转向与延迟字段。":id==0x50?L"显示所选战场动作的静态姿态，不计算伤害或战斗状态。\n多选时对每个选中角色写入一条动作指令。":id==0x4d?L"先选择目标模式，再选择属性变化及异常状态。HP/MP 为原编辑器标注的扣减参数。\n区域坐标包含端点；范围选择基于当前预览位置。勾选状态后选择赋予或取消。":id==0x4e?L"AI 为攻击/跟随时选择目标武将；到点/逃到点时填写目标坐标。\n指定角色可搜索 Data 角色或战场编号。区域坐标包含端点。":id==0x6b?L"选择 MEff 或 MCall 法术画面及视点选项。\n预览只呈现法术画面，不计算伤害。":id==0x5b?L"选择胜利条件或战斗中类型，指定区域两端坐标。两种类型都保留区域。":L"选择目标角色，或切换到区域部队后填写区域与阵营。");
  locked_fields_.clear();
  if(id==0x25) locked_fields_={1,2};
  else if(id==0x26) locked_fields_={1,2,3,4};
  else if(id==0x6b) locked_fields_={0,1};
  else if(id==0x5b) locked_fields_={0,1,2,3};
  else if(id==0x5c||id==0x4f||id==0x50) locked_fields_={0};
  else if(id==0x4d) locked_fields_={0,1,2,3,4,5,6};
  else if(id==0x4e||id==0x31||id==0x53) locked_fields_={0,1,2,3,4,5};
  locked_fields_.insert(locked_fields_.end(),extra_locked.begin(),extra_locked.end());
  std::fill(std::begin(boolean_field_),std::end(boolean_field_),false);
  if(id==0x4f)boolean_field_[3]=boolean_field_[4]=boolean_field_[5]=true;
  if(id==0x50)boolean_field_[2]=boolean_field_[3]=true;
  if(id==0x6b)boolean_field_[3]=true;
  int count=0;for(int i=0;i<(int)config_.fields.size();++i)if(std::find(locked_fields_.begin(),locked_fields_.end(),i)==locked_fields_.end())++count;
  const int columns=count<=1?1:2;
  const int clientWidth=columns*278+20;
  int visible=0;
  for(auto& toggle:toggles_)toggle.ShowWindow(SW_HIDE);
  for(int i=0;i<(int)config_.fields.size();++i){
   if(std::find(locked_fields_.begin(),locked_fields_.end(),i)!=locked_fields_.end()){
    labels_[i].ShowWindow(SW_HIDE);edits_[i].ShowWindow(SW_HIDE);combos_[i].ShowWindow(SW_HIDE);continue;
   }
   const int x=12+(visible%columns)*278,y=14+(visible/columns)*34;++visible;
   labels_[i].MoveWindow(x,y,106,26);edits_[i].MoveWindow(x+108,y,152,26);combos_[i].MoveWindow(x+108,y,152,250);
   if(boolean_field_[i]){labels_[i].ShowWindow(SW_HIDE);edits_[i].ShowWindow(SW_HIDE);combos_[i].ShowWindow(SW_HIDE);toggles_[i].SetWindowText(Names(id)[i]);toggles_[i].SetCheck(config_.fields[i]?BST_CHECKED:BST_UNCHECKED);toggles_[i].MoveWindow(x,y,260,26);toggles_[i].ShowWindow(SW_SHOW);}
  }
  int bottom=14+((visible+columns-1)/columns)*34;
  if(id==0x4d){for(int i=0;i<6;++i)checks_[i].MoveWindow(12+(i%columns)*278,bottom+(i/columns)*26,260,24);bottom+=((6+columns-1)/columns)*26;}
  if(!extra_locked.empty()){SetWindowText(L"朝向延迟设置");note_.SetWindowText(L"设置本组指令的前延迟和后延迟；取消不写入。");}
  note_.MoveWindow(12,bottom+6,clientWidth-24,58);bottom+=70;
  apply_.MoveWindow(12,bottom,120,28);cancel_.MoveWindow(clientWidth-132,bottom,120,28);
  CRect window(0,0,clientWidth,bottom+40);AdjustWindowRectEx(&window,GetStyle(),FALSE,GetExStyle());
  UpdateEnabled();CRect parent;owner->GetWindowRect(&parent);SetWindowPos(&wndTop,parent.left+(parent.Width()-window.Width())/2,parent.top+(parent.Height()-window.Height())/2,window.Width(),window.Height(),show?SWP_SHOWWINDOW:(SWP_HIDEWINDOW|SWP_NOACTIVATE));
 }
 void Hide(){if(GetSafeHwnd())ShowWindow(SW_HIDE);handler_={};}
protected:
 BOOL OnCommand(WPARAM w,LPARAM l) override {
  const UINT id=LOWORD(w);if(id==202){Hide();return TRUE;}
  if(HIWORD(w)==CBN_SELCHANGE){UpdateEnabled();return TRUE;}
  if(id!=201)return CWnd::OnCommand(w,l);
  auto candidate=config_;
  for(std::size_t i=0;i<candidate.fields.size();++i){if(std::find(locked_fields_.begin(),locked_fields_.end(),(int)i)!=locked_fields_.end())continue;if(boolean_field_[i]){candidate.fields[i]=toggles_[i].GetCheck()==BST_CHECKED;continue;}if(combo_field_[i]){if(combos_[i].IsWindowEnabled()&&!combos_[i].CommitIndexedInput()){AfxMessageBox(L"请选择有效的选项");return TRUE;}candidate.fields[i]=Value((int)i);continue;}if(!edits_[i].IsWindowEnabled())continue;
   CString text;edits_[i].GetWindowText(text);wchar_t* end=nullptr;errno=0;long long value=wcstoll(text,&end,10);
   if(text.IsEmpty()||end==text.GetString()||*end||errno==ERANGE||value<(std::numeric_limits<std::int32_t>::min)()||value>(std::numeric_limits<std::int32_t>::max)()){AfxMessageBox(L"请输入有效整数");return TRUE;}candidate.fields[i]=(std::int32_t)value;
  }
  if(config_.instruction_id==0x4d){int status=Value(10);if(status>=0){for(int i=0;i<6;++i)if(checks_[i].GetCheck()==BST_CHECKED)status|=2<<i;if((status&126)==0)status=-1;}candidate.fields[10]=status;}
  Instruction instruction;std::wstring diagnostic;if(!ccz::editor_preview::BuildBattleCommandInstruction(candidate,width_,height_,instruction,diagnostic)){AfxMessageBox(diagnostic.c_str());return TRUE;}
  auto handler=handler_;if(handler&&handler(instruction)){config_=candidate;Hide();}return TRUE;
 }
 BOOL PreTranslateMessage(MSG* msg) override {if(msg->message==WM_KEYDOWN&&msg->wParam==VK_ESCAPE){Hide();return TRUE;}return CWnd::PreTranslateMessage(msg);}
 LRESULT WindowProc(UINT m,WPARAM w,LPARAM l) override {if(m==WM_CLOSE){Hide();return 0;}return CWnd::WindowProc(m,w,l);}
private:
 int Value(int i)const{int row=combos_[i].GetCurSel();return row<0||row>=(int)values_[i].size()?-1:values_[i][row];}
 void SelectValue(int i,int value){for(int row=0;row<(int)values_[i].size();++row)if(values_[i][row]==value){combos_[i].SetCurSel(row);return;}combos_[i].SetCurSel(0);}
 void Options(int i,std::initializer_list<const wchar_t*> labels,std::initializer_list<int> values={}){combo_field_[i]=true;person_cache_kind_[i]=0;combos_[i].ResetContent();values_[i].clear();int n=0;auto v=values.begin();for(auto label:labels){combos_[i].AddString(label);values_[i].push_back(values.size()?*v++:n);++n;}SelectValue(i,config_.fields[i]);}
 void Faction(int i){Options(i,{L"我军",L"友军",L"敌军",L"援军",L"我军及友军",L"敌军及援军",L"所有部队"});}
 void EntryPeople(int i,const std::vector<std::wstring>& names,const std::vector<std::wstring>& nativeLabels){
  combo_field_[i]=true;
  if(person_cache_kind_[i]==3){combos_[i].RestoreIndexedOptions();SelectValue(i,config_.fields[i]);return;}
  combos_[i].ResetContent();values_[i].clear();
  const wchar_t* functions[]={L"任何部队",L"我军或友军",L"敌军",L"我军当前人物"};
  for(int row=0;row<5375;++row){
   CString label;
   if(row<(int)nativeLabels.size())label=nativeLabels[row].c_str();
   else if(row<1024){label.Format(L"%d:",row);if(row<(int)names.size())label+=names[row].c_str();}
   else if(row<1028)label=functions[row-1024];
   else if(row<1278)label.Format(L"w%d ",row-1028);
   else if(row<5374)label.Format(L"v%d ",row-1278);
   else label=L"无";
   combos_[i].AddString(label);values_[i].push_back(Per2List2Code(row));
  }
  person_cache_kind_[i]=3;combos_[i].EnablePersonSearch();SelectValue(i,config_.fields[i]);
 }
 void People(int i,const std::vector<std::wstring>& names,bool dataOnly=false,bool noReference=false){
  combo_field_[i]=true;
  const int kind=noReference?2:dataOnly?4:1;
  if(person_cache_kind_[i]==1 && kind==2){
   combos_[i].RestoreIndexedOptions();
   combos_[i].AddString(L"无参照");values_[i].push_back(-1);
   person_cache_kind_[i]=2;
  }else if(person_cache_kind_[i]==2 && kind==1){
   if(combos_[i].RemoveFinalIndexedOption()){
    values_[i].pop_back();person_cache_kind_[i]=1;
   }else person_cache_kind_[i]=0;
  }
  if(person_cache_kind_[i]!=kind){
   combos_[i].ResetContent();values_[i].clear();
   for(int n=0;n<1024;++n){
    CString text;text.Format(L"%d: Data",n);
    if(n<(int)names.size()){text+=L" · ";text+=names[n].c_str();}
    combos_[i].AddString(text);values_[i].push_back(n);
   }
   if(!dataOnly)for(int n=0;n<250;++n){
    CString text;text.Format(L"战场编号 %d",n);
    combos_[i].AddString(text);values_[i].push_back(n+1028);
   }
   if(noReference){combos_[i].AddString(L"无参照");values_[i].push_back(-1);}
   person_cache_kind_[i]=kind;combos_[i].EnablePersonSearch();
  }else combos_[i].RestoreIndexedOptions();
  SelectValue(i,config_.fields[i]);
 }
 void Enable(int i,bool enabled){edits_[i].EnableWindow(enabled);combos_[i].EnableWindow(enabled);labels_[i].EnableWindow(enabled);toggles_[i].EnableWindow(enabled);}
 void UpdateEnabled(){const auto id=config_.instruction_id;for(int i=0;i<(int)config_.fields.size();++i)Enable(i,true);
  if(id==0x4d){int mode=Value(0);Enable(1,mode==0);Enable(2,mode==1);for(int i=3;i<=7;++i)Enable(i,mode==2);for(auto& c:checks_)c.EnableWindow(Value(10)>=0);}
  else if(id==0x31||id==0x4e||id==0x53){int mode=Value(0);Enable(1,mode==0);for(int i=2;i<=6;++i)Enable(i,mode==1);if(id==0x4e){int policy=Value(7);Enable(8,policy==3||policy==5);Enable(9,policy==4||policy==6);Enable(10,policy==4||policy==6);}}
 }
 static const wchar_t* Title(std::uint16_t id){return id==0x25?L"武将进入指定地点测试":id==0x26?L"武将进入指定区域测试":id==0x4f?L"战场朝向":id==0x50?L"战场动作":id==0x4d?L"武将状态改变":id==0x4e?L"武将行动AI":id==0x31?L"武将消失":id==0x53?L"战场撤退":id==0x5b?L"战场区域标记":id==0x5c?L"战场武将标记":L"法术画面";}
 static const wchar_t* const* Names(std::uint16_t id){
  static const wchar_t* pointTest[]={L"武将 DATA",L"X",L"Y"};
  static const wchar_t* regionTest[]={L"武将 DATA",L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y"};
  static const wchar_t* turn[]={L"操作武将",L"参照武将",L"方向",L"不转向",L"动作前延迟",L"动作后延迟"};
  static const wchar_t* action[]={L"操作武将",L"动作",L"动作前延迟",L"动作后延迟"};
  static const wchar_t* status[]={L"目标模式",L"目标角色",L"战场编号",L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y",L"阵营",L"属性",L"变化",L"异常状态",L"扣减 HP",L"扣减 MP"};
  static const wchar_t* policy[]={L"目标模式",L"目标角色",L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y",L"阵营",L"行动AI",L"目标武将",L"目标 X",L"目标 Y"};
  static const wchar_t* area[]={L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y",L"标记类型"};
  static const wchar_t* disappear[]={L"目标模式",L"目标角色",L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y",L"阵营"};
  static const wchar_t* retreat[]={L"目标模式",L"目标角色",L"区域左 X",L"区域上 Y",L"区域右 X",L"区域下 Y",L"阵营",L"退出类型"};
  static const wchar_t* highlight[]={L"目标角色"};static const wchar_t* effect[]={L"X",L"Y",L"法术画面",L"视点"};return id==0x25?pointTest:id==0x26?regionTest:id==0x4f?turn:id==0x50?action:id==0x4d?status:id==0x4e?policy:id==0x5b?area:id==0x31?disappear:id==0x53?retreat:id==0x5c?highlight:effect;
 }
 Configuration config_;int width_{},height_{};Handler handler_;bool combo_field_[13]{};std::vector<int> values_[13];std::vector<int> locked_fields_;
 int person_cache_kind_[13]{};std::vector<std::wstring> cached_data_names_,cached_entry_people_;
 std::vector<std::wstring> prepared_names_;
 bool boolean_field_[13]{};CButton toggles_[13];
 CStatic labels_[13],note_;CEdit edits_[13];CMyComboBox combos_[13];CButton checks_[6],apply_,cancel_;CFont font_;
};
