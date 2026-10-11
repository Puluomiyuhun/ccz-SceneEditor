
// cczEditor2View.h: CcczEditor2View 类的接口
//

#pragma once
#include "CMyComboBox.h"
#include "TextWrapEditSession.h"
#include "TemplateLibrary/TemplateModel.h"
#include <vector>

class CcczEditor2View : public CTreeView
{
protected: // 仅从序列化创建
	CcczEditor2View() noexcept;
	DECLARE_DYNCREATE(CcczEditor2View)

// 特性
public:
	CcczEditor2Doc* GetDocument() const;

// 操作
public:
	HTREEITEM cur_item;
	bool CaptureTemplateSelection(ccz::templates::Entry& entry, CString& error);
	bool InsertTemplate(const ccz::templates::Entry& entry, CString& error);
	bool CanUndoTemplateInsertion() const;
	bool UndoTemplateInsertion(CString& error);
	afx_msg void OnSaveTemplate();
	struct TemplateInsertionUndo {
		std::vector<HTREEITEM> roots;
		HTREEITEM anchor = nullptr;
		std::uint64_t epoch = 0, generation = 0;
		int ordinal_before = 0;
		bool modified_before = false;
	};
	std::vector<TemplateInsertionUndo> m_templateUndo;
	int cur_code;
	int cur_code_ord = 0;

	/*读取剧本时，辅助无条件跳转的变量*/
	int code_off[200000];
	std::vector<HTREEITEM> jmp_list;

	/*写入剧本时，辅助无条件跳转的变量*/
	std::vector<int> jmp_pos;
	std::vector<int> jmp_goal;

	/*保存剧本刷新时，辅助无条件跳转的变量*/
	int new_ord[200000];

	int code_sum = 123;
	wchar_t code[200][30] = { L"0:事件结束",L"1:子事件设定",L"2:内部信息",L"3:else",L"4:询问测试",L"5:变量测试",L"6:我军出场限制",L"7:出战测试",L"8:菜单处理",L"9:延时",L"a:初始化局部变量",L"b:变量赋值",L"c:结束Section",L"d:结束Scene",L"e:战斗失败",L"f:结局设定",L"10:许子将指导",L"11:剧本跳转",L"12:选择框",L"13:case",L"14:对话 ",L"15:对话2",L"16:信息",L"17:场所名",L"18:事件名称设定",L"19:胜利条件",L"1a:显示胜利条件",L"1b:撤退信息是否显示设定",L"1c:绘图",L"1d:调色板设定",L"1e:武将重绘",L"1f:地图视点切换",L"20:武将头像状态设置",L"21:战场物体添加",L"22:动画",L"23:音效",L"24:CD音轨",L"25:武将进入指定地点测试",L"26:武将进入指定区域测试",L"27:背景显示",L"28:自由R启动指令",L"29:地图头像显示",L"2a:地图头像移动",L"2b:地图头像消失",L"2c:地图文字显示",L"2d:武将点击测试",L"2e:武将相邻测试",L"2f:清除人物",L"30:武将出现",L"31:武将消失",L"32:武将移动",L"33:武将转向",L"34:武将动作",L"35:武将形象改变",L"36:武将状态测试",L"37:钱、章节编号、忠奸测试",L"38:武将能力设定",L"39:武将等级提升",L"3a:钱、章节编号、忠奸设置",L"3b:武将加入",L"3c:武将加入测试",L"3d:获得物品",L"3e:加入装备设定",L"3f:回合测试",L"40:行动方测试",L"41:战场人数测试",L"42:战斗胜利测试",L"43:战斗失败测试",L"44:战斗初始化",L"45:战场全局变量",L"46:友军出场设定",L"47:敌军出场设定",L"48:敌方装备设定",L"49:战斗结束",L"4a:我军出场强制设定",L"4b:我军出场设定",L"4c:隐藏武将出现",L"4d:武将状态变更",L"4e:武将方针变更",L"4f:战场转向设置",L"50:战场动作设定",L"51:战场恢复行动权",L"52:兵种改变",L"53:战场撤退",L"54:战场撤退确认",L"55:战场复活",L"56:天气序号设定",L"57:当前天气组设定",L"58:战场障碍设定",L"59:战利品",L"5a:战场操作开始",L"5b:战场高亮区域",L"5c:战场高亮人物",L"5d:回合上限设定",L"5e:武将不同测试",L"5f:单挑结束",L"60:单挑武将出场",L"61:单挑胜负显示",L"62:单挑阵亡",L"63:单挑对话",L"64:单挑动作",L"65:单挑攻击1",L"66:单挑攻击2",L"67:章名",L"68:单挑开始",L"69:旁白",L"6a:Game Over指令",L"6b:法术",L"6c:武将能力复制",L"6d:相对复活或移动",L"6e:概率测试",L"6f:丢弃物品",L"70:能力选择复制",L"71:特效请求",L"72:信息传送",L"73:人物五围和测试",L"74:开/禁存档",L"75:S特殊形象指定",L"76:无条件跳转",L"77:变量运算",L"78:整型变量赋值",L"79:变量测试",L"7A:对话3",L"7B:信息传送测试" };
	int xxcs_sum = 29;
	wchar_t xxcs[34][30] = { L"0:武将改名",L"1:对指定人物释放法术",L"2:对指定地点释放法术",L"3:对指定范围施放法术",L"4:习得策略",L"5:习得特技",L"6:习得必杀",L"7:商店商家变更",L"8:战场地图扩展",L"9:结局角色展示",L"10:装备专属和套装",L"11:限定AI行动范围(逐个)",L"12:S插图",L"13:剧本调用函数",L"14:战场明暗变化",L"15:更换动图",L"16:清理离队武将信息",L"17:限制区域AI行动范围（范围）",L"18:全队成员提升到平均等级",L"19:清除一个特殊指针变量",L"20:待开发",L"21:部队行动标识",L"22:结局设置",L"23:指定武将列传",L"24:指定武将点击语音",L"25:R数字",L"26:产生一个随机数",L"27:清空指定武将的功勋",L"28:R插图",L"29:pl版指令",L"30:R文字",L"31:只使用战场的一个矩形范围 ",L"32:控制动态图",L"33:设置一个矩形范围的黑幕" };
	byte code_test[124] = { 0,0,2,1,2,2,0,2,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,2,2,0,0,0,0,2,0,0,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,2,0,2 };
	int code_instruct[124][13] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x35,0x35,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2E,0x4,0x38,0x38,0x38,0x38,0x38,0x39,0x39,0x39,0x39,0x39,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2E,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x27,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x12,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x37,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x27,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4A,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x4,0x10,0x26,0x26,-1,-1,-1,-1,-1,-1,-1,-1,
		0x1B,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x1E,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x4,0x4,0x4,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2D,0xC,0x1A,0x1C,0x15,-1,-1,-1,-1,-1,-1,-1,-1,
		0x36,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,0x26,0x26,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x4,0x2B,0xD,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2C,0x2,0x4,0x4,0x4,0x4,0x3,-1,-1,-1,-1,-1,-1,
		0x40,0x2,0x4,0x4,0x4,0x2B,-1,-1,-1,-1,-1,-1,-1,
		0x2,0xD,0x2B,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0xD,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x13,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x23,0x4,0x24,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x28,0x4,0x24,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x23,0x34,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x28,0x34,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0xE,0x3E,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0xE,0x3A,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x17,0x49,0x26,0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x3B,0x49,0x3C,0x49,0x3D,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x24,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x48,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x3,0x4,0x24,0x3F,0x4,0x4,0x4,0x4,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x26,0x4,0x3E,0x32,0x2,0x32,0x2,0x47,0x22,-1,-1,-1,
		0x2,0x26,0x4,0x4,0x2B,0x3E,0x45,0x7,0x2,0x4,0x4,-1,-1,
		0x2,0x26,0x26,0x4,0x4,0x2B,0x3E,0x45,0x7,0x2,0x4,0x4,-1,
		0x2,0x3B,0x49,0x3C,0x49,0x3D,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x38,0x38,0x38,0x38,0x38,0x39,0x39,0x39,0x39,0x39,-1,-1,
		0x4,0x4,0x4,0x2B,0x26,-1,-1,-1,-1,-1,-1,-1,-1,
		0x40,0x2,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x41,0x2,0x4,0x4,0x4,0x4,0x4,0x3,0x2F,0x18,0x30,0x4,0x4,
		0x2C,0x2,0x4,0x4,0x4,0x4,0x3,0x7,0x2,0x4,0x4,-1,-1,
		0x2,0x2,0x2B,0x26,0x26,0x26,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x46,0x26,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2C,0x2,0x4,0x4,0x4,0x4,0x3,0x26,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x40,0x2,0x4,0x4,0x4,0x2B,-1,-1,-1,-1,-1,-1,-1,
		0x47,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x22,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x42,0x43,0x44,0x4,0x4,0x26,0x26,-1,-1,-1,-1,-1,-1,
		0x4,0x17,0x49,0x17,0x49,0x17,0x49,0x26,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x4,0x4,0x4,0x26,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x34,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x5,0x4C,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x5,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x4C,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x4D,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x26,0x4E,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,0x4F,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x4,0x4B,0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x40,0x2,0x4,0x2,0x4,0x4,0x2B,0x26,-1,-1,-1,-1,-1,
		0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x17,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x2,0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x4,0x24,0x26,0x26,0x26,0x26,0x26,-1,-1,-1,-1,-1,
		0x26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x2,0x50,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x51,0x4,0x53,0x52,0x4,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4,0x54,0x2,0x55,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x52,0x4,0x56,0x52,0x4,-1,-1,-1,-1,-1,-1,-1,-1,
		0x5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		0x4, 0x5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	};
	wchar_t per1[5400][30];
	wchar_t per2[5400][30];
	wchar_t item[512][30];
	wchar_t job[80][20];
	wchar_t meff[256][20];
	wchar_t teji[256][30];
	int face_condition_sum = 9;
	wchar_t face_condition[30][20] = { L"曹操-普通",L"曹操-惊讶",L"曹操-愤怒",L"曹操-欣喜",L"夏侯惇-蒙目",L"孔明-邪恶",L"曹丕-称帝",L"夏侯惇-独目",L"孔明-正常" };
	wchar_t movie[128][20];
	wchar_t dir[5][20] = { L"朝北",L"朝东", L"朝南", L"朝西", L"默认方向" };
	wchar_t ges[21][20] = { L"普通",L"下跪",L"脸红",L"举手",L"哭",L"伸手",L"作揖",L"盘坐脸红",L"盘坐举手",L"盘坐哭",L"倒下",L"单膝跪地",L"被缚",L"挥剑扬起",L"挥剑劈下",L"活埋",L"起身",L"单手举起",L"未知",L"变量",L"无"};
	wchar_t ges_war[14][20] = { L"静止",L"举起武器",L"防御",L"受攻击",L"虚弱",L"攻击预备",L"攻击",L"二次攻击",L"慢速转圈",L"喘气",L"晕倒",L"快速转圈",L"中速转圈",L"无" };
	wchar_t zhenying[7][20] = { L"我军",L"友军", L"敌军", L"援军", L"我军及友军", L"敌军及援军", L"所有部队" };
	int per_condition_sum = 16;
	wchar_t per_condition[30][20] = { L"攻击",L"防御",L"精神",L"爆发",L"士气",L"HP",L"MP",L"HPCur",L"MPCur",L"Lv",L"武力",L"统率",L"智力",L"敏捷",L"运气",L"头像" };
	int per_condition_war_sum = 7;
	wchar_t per_condition_war[30][20] = { L"攻击",L"防御",L"精神",L"爆发",L"士气" ,L"移动力",L"无" };
	int compare_sum = 3;
	wchar_t compare[10][3] = { L">=",L"<",L"=" };
	int compare2_sum = 3;
	wchar_t compare2[10][3] = { L"==",L">=",L"<" };
	int operate_sum = 3;
	wchar_t operate[15][3] = { L"=",L"+",L"-" };
	int operate2_sum = 7;
	wchar_t operate2[15][5] = { L"+=",L"-=",L"=", L"*=",L"/=",L"%=",L"M="};
	wchar_t changes[4][10] = { L"下降",L"正常",L"上升",L"无" };
	wchar_t debuff[6][30] = { L"麻痹",L"封咒",L"混乱",L"中毒",L"5号",L"6号" };
	int join_condition_sum = 3;
	wchar_t join_condition[15][20] = {L"data加入", L"内存加入", L"离开"};
	wchar_t weather[5][5] = { L"普通",L"晴好", L"阴雨", L"小雪", L"大雪" };
	wchar_t weather2[6][30] = { L"晴",L"晴/晴/阴/晴/阴",L"晴/晴/雨/阴/雪",L"阴/晴/雨/阴/雪",L"雨/阴/豪雨/雪/雪",L"豪雨/雨/豪雨/雪/雪" };
	int policy_sum = 7;
	wchar_t policy[15][30] = { L"被动出击",L"主动出击", L"坚守原地", L"攻击武将", L"到指定点", L"跟随武将", L"逃到指定点" };
	wchar_t object[128][30];
	wchar_t hexz[30][10] = { L"平原",L"草原",L"树林",L"荒地",L"山地",L"岩山",L"山崖",L"雪原",L"桥梁",L"浅滩",L"沼泽",L"池塘",L"小河",L"大河",L"栅栏",L"城墙",L"城内",L"城门",L"城池",L"关隘",L"鹿柴",L"村落",L"兵营",L"居民",L"宝库",L"水池",L"火焰",L"船",L"祭坛",L"地下" };
	wchar_t solo_ges[16][20] = { L"无",L"后转",L"前移",L"小步前移",L"小步后退",L"举起武器",L"防御",L"受攻击",L"攻击预备",L"攻击",L"二次攻击",L"晕倒",L"喘气",L"撤退",L"跳舞1",L"跳舞2" };
	wchar_t solo_atk1[5][20] = { L"命中",L"格挡", L"格挡后退", L"后退", L"闪躲绕前" };
	wchar_t solo_atk2[3][20] = { L"原地攻击",L"移动攻击",L"互相冲锋" };
	int var_kind_sum = 3;
	wchar_t var_kind[10][20] = { L"指针变量(*p)",L"指针变量(p)",L"整型变量" };
	int var_kind2_sum = 6;
	wchar_t var_kind2[15][20] = { L"常数",L"指针变量(*p)",L"指针变量(p)",L"指针变量(&p)",L"整型变量(a)",L"整型变量(&a)" };
	int all_condition_sum = 42;
	wchar_t all_condition[60][30] = { L"R形象",L"头像",L"攻击",L"防御",L"精神",L"爆发",L"士气",L"HP",L"MP",L"武力",L"统率",L"智力",L"敏捷",L"运气",L"出战场数",L"撤退场数",L"我军标识",L"兵种",L"人物等级",L"人物经验值",L"武器",L"武器等级",L"武器经验值",L"防具",L"防具等级",L"防具经验值",L"辅助",L"战场特殊形象",L"战场编号",L"战场横坐标",L"战场纵坐标",L"战场行动标识",L"战场人物朝向",L"HpCur",L"MpCur",L"战场人物攻击状态",L"战场人物防御状态",L"战场人物精神状态",L"战场人物爆发状态",L"战场人物士气状态",L"战场人物移动状态",L"战场人物健康状态" };
	int set_sound[3] = { 100, 100, 55 };
	int set_cd = 999;
	int set_level = -1;
	int set_weapon = -1;
	int set_armor = -1;
	int set_product = -1;
	int set_rs = -1;
	int set_fashu = 100;
	int set_tejibase = -1;
	bool zishijian = false;
	ItemData cur_data;

	bool ctrl_on = false;
	bool checkbox = false;
	bool read_e5 = false;
	bool read_exe = false;
	std::vector<HTREEITEM> checkbox_selected;

	CBrush m_brBk;
	COLORREF m_bgcolor = RGB(30, 30, 30);
	bool night_mode = true;

	int fast_total = 0; //直接输入数字更改指令的记录

	bool extend = false; //扩展引擎

	bool m_bBgLoaded = false;
	CImage m_bgImage;

// 重写
public:
	virtual void OnDraw(CDC* pDC);  // 重写以绘制该视图
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void OnActivateView(BOOL activate, CView* activate_view,
		CView* deactivate_view) override;
protected:
	virtual void OnInitialUpdate(); // 构造后第一次调用
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnPaint();
// 实现
public:
	virtual ~CcczEditor2View();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

public:
	void ReadString();
	void ReadDataFromIni();
	void ReadDataFromFile();
	void ReadDataFromLocal();
	void ReadDataDefault();
	void NormalizeItemSegmentCounts();
	void InitBaseData();
	void CreateRoot(LPWSTR root_name);
	void CreateNewTree();
	void CreateFileTree();
	HTREEITEM FindLast(HTREEITEM me);
	int EditData(ItemData *me, unsigned char* c);
	ItemData* InitData(int id);
	void UpdateShow(HTREEITEM me);
	HTREEITEM CreateItem(int id, HTREEITEM parent, HTREEITEM hInsertAfter);
	HTREEITEM InsertInstructionAt(int id, HTREEITEM hAfter, const int* intData, int dataCount);
	HTREEITEM InsertBattleInstructionAt(int instructionId, HTREEITEM parent,
		HTREEITEM hInsertAfter, std::vector<int> intData);
	HTREEITEM CreateZsj(int id, HTREEITEM parent, HTREEITEM hInsertAfter);
	void CreateScene(HTREEITEM parent, HTREEITEM hInsertAfter);
	void CreateSection(HTREEITEM parent, HTREEITEM hInsertAfter);
	void DrawBackground(CDC& dc);
// 生成的消息映射函数
protected:
	afx_msg void OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMRClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMDBLClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnEditModify();
	afx_msg void OnEditAdd();
	afx_msg void OnEditAddi();
	afx_msg void OnEditDelete();
	bool OnCopy();
	afx_msg void OnCopyMsg();
	afx_msg void OnPaste();
	void refreshCheckbox(HTREEITEM cur, int kind);
	void recurExpand(HTREEITEM cur);
	HTREEITEM recurSearchOrd(HTREEITEM cur, int ord);
	HTREEITEM recurSearchItem(HTREEITEM cur, HTREEITEM ddl, bool &ddly, int id);
	bool checkCheckbox();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnCut();
	afx_msg void OnExpand();
	afx_msg void OnJump();
	afx_msg void OnSearchItem();
	afx_msg void OnSearchItemNext();
	afx_msg void OnVarList();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnMoveUp();
	afx_msg void OnMoveDown();
	afx_msg void OnTvnSelchanged(NMHDR* pNMHDR, LRESULT* pResult);
	bool RefreshScenePreview();
	void SelectPlaybackItem(HTREEITEM item, bool suppress_preview_refresh);
	afx_msg void OnEditDuplicate();
	afx_msg void OnEditBatch();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnDestroy();
	bool m_suppressPreviewRefreshOnce = false;
	// MFC may activate a view before OnInitialUpdate builds its script tree.
	bool m_previewTreeInitialized = false;
	// S battle native writeback suppresses selection-triggered preview reloads
	// until its complete transaction (or rollback) has restored a stable state.
	bool m_suppressBattleWritebackPreview = false;
};

#ifndef _DEBUG  // cczEditor2View.cpp 中的调试版本
inline CcczEditor2Doc* CcczEditor2View::GetDocument() const
   { return reinterpret_cast<CcczEditor2Doc*>(m_pDocument); }
#endif

class myDialog : public CDialogEx
{
public:
	myDialog(UINT temp) noexcept : CDialogEx(temp) {}
};

class Dialog_SelectCode :
	public myDialog
{
public:
	Dialog_SelectCode() noexcept : myDialog(IDD_SELECTCODE) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SELECTCODE };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX); DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_2 :
	public myDialog
{
public:
	Dialog_2() noexcept : myDialog(IDD_DIALOG2) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG2};
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_WRAP_ENABLE, wrap_enable);
		DDX_Control(pDX, IDC_WRAP_BYTES, wrap_bytes);
		DDX_Control(pDX, IDC_WRAP_MODE, wrap_mode);
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg) /// 热键
	{
		if (pMsg->message == WM_KEYDOWN) {
			CString str;
			if (pMsg->wParam == VK_CONTROL)
			{
				ctrl_on = true;
				return TRUE;
			}
			/*if (pMsg->wParam == 9)
			{
				OnBnClickedOk();
			}*/
		}
		if (pMsg->message == WM_KEYUP)
		{
			if (pMsg->wParam == VK_CONTROL)
			{
				ctrl_on = false;
				return TRUE;
			}
		}

		return CDialogEx::PreTranslateMessage(pMsg);
	}
public:
	CMyEdit edit1;
	CButton wrap_enable;
	CEdit wrap_bytes;
	CButton wrap_mode;
	CButton color_assist;
	afx_msg void OnColorAssist();
	bool wrap_history_mode = false;
	bool wrap_supported = false;
	bool wrap_updating = false;
	ccz::text_wrap::EditTextState wrap_state;
	bool ctrl_on;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedWrapMode();
	afx_msg void OnBnClickedWrapEnable();
	afx_msg void OnEnChangeWrapText();
};

class Dialog_4 :
	public myDialog
{
public:
	Dialog_4() noexcept : myDialog(IDD_DIALOG4) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG4 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK2, check1);
	}
public:
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_5 :
	public myDialog
{
public:
	Dialog_5() noexcept : myDialog(IDD_DIALOG5) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG5 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
	}
	void parser(CString c, int* a);
public:
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_6 :
	public myDialog
{
public:
	Dialog_6() noexcept : myDialog(IDD_DIALOG6) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG6 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_LIST1, list1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	int per[10];
	CButton check1;
	CMyEdit edit1;
	CListBox list1;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnLbnSelchangeList1();
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnBnClickedOk();
};


class Dialog_9 :
	public myDialog
{
public:
	Dialog_9() noexcept : myDialog(IDD_DIALOG9) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG9 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_11 :
	public myDialog
{
public:
	Dialog_11() noexcept : myDialog(IDD_DIALOG11) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG11 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyEdit edit1;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_15 :
	public myDialog
{
public:
	Dialog_15() noexcept : myDialog(IDD_DIALOG15) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG15 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_17 :
	public myDialog
{
public:
	Dialog_17() noexcept : myDialog(IDD_DIALOG17) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG17 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_18 :
	public myDialog
{
public:
	Dialog_18() noexcept : myDialog(IDD_DIALOG18) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG18 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyEdit edit1;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_21 :
	public myDialog
{
public:
 CButton color_assist;
 afx_msg void OnColorAssist();
	Dialog_21() noexcept : myDialog(IDD_DIALOG21) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG21 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_WRAP_ENABLE, wrap_enable);
		DDX_Control(pDX, IDC_WRAP_BYTES, wrap_bytes);
	}
public:
	CMyEdit edit1;
	CMyComboBox combo1;
	CMyComboBox combo2;
	CButton wrap_enable;
	CEdit wrap_bytes;
	bool wrap_updating = false;
	ccz::text_wrap::EditTextState wrap_state;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedWrapEnable();
	afx_msg void OnEnChangeWrapText();
};



class Dialog_27 :
	public myDialog
{
public:
	Dialog_27() noexcept : myDialog(IDD_DIALOG27) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG27 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_31 :
	public myDialog
{
public:
	Dialog_31() noexcept : myDialog(IDD_DIALOG31) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG31 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_32 :
	public myDialog
{
public:
	Dialog_32() noexcept : myDialog(IDD_DIALOG32) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG32 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_33 :
	public myDialog
{
public:
	Dialog_33() noexcept : myDialog(IDD_DIALOG33) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG33 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK3, check2);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	CComboBox combo1;
	CButton check1;
	CButton check2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_34 :
	public myDialog
{
public:
	Dialog_34() noexcept : myDialog(IDD_DIALOG34) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG34 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_35 :
	public myDialog
{
public:
	Dialog_35() noexcept : myDialog(IDD_DIALOG35) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG35 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_36 :
	public myDialog
{
public:
	Dialog_36() noexcept : myDialog(IDD_DIALOG36) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG36 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_37 :
	public myDialog
{
public:
	Dialog_37() noexcept : myDialog(IDD_DIALOG37) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG37 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_38 :
	public myDialog
{
public:
	Dialog_38() noexcept : myDialog(IDD_DIALOG38) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG38 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};



class Dialog_39 :
	public myDialog
{
public:
	Dialog_39() noexcept : myDialog(IDD_DIALOG39) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG39 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	int dat[4];
	CMyComboBox combo1;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_43 :
	public myDialog
{
public:
	Dialog_43() noexcept : myDialog(IDD_DIALOG43) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG43 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_44 :
	public myDialog
{
public:
 CButton color_assist;
 afx_msg void OnColorAssist();
	Dialog_44() noexcept : myDialog(IDD_DIALOG44) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG44 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
		DDX_Control(pDX, IDC_CHECK3, check3);
		DDX_Control(pDX, IDC_WRAP_ENABLE, wrap_enable);
		DDX_Control(pDX, IDC_WRAP_BYTES, wrap_bytes);
	}
public:
	CMyEdit edit1;
	CButton check1;
	CButton check2;
	CButton check3;
	CButton wrap_enable;
	CEdit wrap_bytes;
	bool wrap_updating = false;
	ccz::text_wrap::EditTextState wrap_state;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedWrapEnable();
	afx_msg void OnEnChangeWrapText();
};


class Dialog_46 :
	public myDialog
{
public:
	Dialog_46() noexcept : myDialog(IDD_DIALOG46) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG46 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_CHECK1, check1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_48 :
	public myDialog
{
public:
	Dialog_48() noexcept : myDialog(IDD_DIALOG48) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG48 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};



class Dialog_49 :
	public myDialog
{
public:
	Dialog_49() noexcept : myDialog(IDD_DIALOG49) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG49 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_50 :
	public myDialog
{
public:
	Dialog_50() noexcept : myDialog(IDD_DIALOG50) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG50 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_51 :
	public myDialog
{
public:
	Dialog_51() noexcept : myDialog(IDD_DIALOG51) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG51 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_52 :
	public myDialog
{
public:
	Dialog_52() noexcept : myDialog(IDD_DIALOG52) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG52 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_53 :
	public myDialog
{
public:
	Dialog_53() noexcept : myDialog(IDD_DIALOG53) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG53 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_54 :
	public myDialog
{
public:
	Dialog_54() noexcept : myDialog(IDD_DIALOG54) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG54 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_55 :
	public myDialog
{
public:
	Dialog_55() noexcept : myDialog(IDD_DIALOG55) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG55 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_56 :
	public myDialog
{
public:
	Dialog_56() noexcept : myDialog(IDD_DIALOG56) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG56 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_58 :
	public myDialog
{
public:
	Dialog_58() noexcept : myDialog(IDD_DIALOG58) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG58 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_59 :
	public myDialog
{
public:
	Dialog_59() noexcept : myDialog(IDD_DIALOG59) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG59 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_60 :
	public myDialog
{
public:
	Dialog_60() noexcept : myDialog(IDD_DIALOG60) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG60 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_61 :
	public myDialog
{
public:
	Dialog_61() noexcept : myDialog(IDD_DIALOG61) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG61 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO3, combo3);
		DDX_Control(pDX, IDC_CHECK1, check1);
	}
public:
	CMyComboBox combo1;
	CEdit edit1;
	CMyComboBox combo3;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_62 :
	public myDialog
{
public:
	Dialog_62() noexcept : myDialog(IDD_DIALOG62) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG62 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
		DDX_Control(pDX, IDC_COMBO4, combo4);
		DDX_Control(pDX, IDC_COMBO5, combo5);
		DDX_Control(pDX, IDC_COMBO6, combo6);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyComboBox combo5;
	CMyComboBox combo6;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_63 :
	public myDialog
{
public:
	Dialog_63() noexcept : myDialog(IDD_DIALOG63) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG63 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};



class Dialog_64 :
	public myDialog
{
public:
	Dialog_64() noexcept : myDialog(IDD_DIALOG64) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG64 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_65 :
	public myDialog
{
public:
	Dialog_65() noexcept : myDialog(IDD_DIALOG65) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG65 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
		DDX_Control(pDX, IDC_EDIT5, edit5);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	CMyEdit edit5;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};



class Dialog_69 :
	public myDialog
{
public:
	Dialog_69() noexcept : myDialog(IDD_DIALOG69) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG69 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK3, check2);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO5, combo2);
		DDX_Control(pDX, IDC_COMBO7, combo3);
		DDX_Control(pDX, IDC_COMBO8, combo4);
		DDX_Control(pDX, IDC_COMBO9, combo5);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT6, edit3);
	}
public:
	CButton check1;
	CButton check2;
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyComboBox combo5;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};



class Dialog_70 :
	public myDialog
{
public:
	Dialog_70() noexcept : myDialog(IDD_DIALOG70) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG70 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK4, check2);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT7, edit3);
		DDX_Control(pDX, IDC_EDIT8, edit4);
		DDX_Control(pDX, IDC_LIST1, list1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO5, combo2);
		DDX_Control(pDX, IDC_COMBO10, combo3);
		DDX_Control(pDX, IDC_COMBO11, combo4);
		DDX_Control(pDX, IDC_COMBO12, combo5);
		DDX_Control(pDX, IDC_COMBO13, combo6);
		DDX_Control(pDX, IDC_STATIC1, static1);
		DDX_Control(pDX, IDC_STATIC3, static2);
	}
public:
	int list_line;
	int dat[1000];
	int quan = 0; //表示这是连续的第几个指令
	bool ending = false;
	CButton check1;
	CButton check2;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	CListBox list1;
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyComboBox combo5;
	CMyComboBox combo6;
	CStatic static1;
	CStatic static2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnCbnSelchangeCombo2();
	afx_msg void OnLbnSelchangeList1();
	afx_msg void OnBnClickedOk();
};


class Dialog_75 :
	public myDialog
{
public:
	Dialog_75() noexcept : myDialog(IDD_DIALOG75) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG75 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_CHECK1, check1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_76 :
	public myDialog
{
public:
	Dialog_76() noexcept : myDialog(IDD_DIALOG76) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG76 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};

class Dialog_77 :
	public myDialog
{
public:
	Dialog_77() noexcept : myDialog(IDD_DIALOG77) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG77 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_COMBO10, combo4);
		DDX_Control(pDX, IDC_COMBO14, combo5);
		DDX_Control(pDX, IDC_COMBO15, combo6);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
		DDX_Control(pDX, IDC_EDIT5, edit5);
		DDX_Control(pDX, IDC_EDIT8, edit6);
		DDX_Control(pDX, IDC_EDIT9, edit7);
		DDX_Control(pDX, IDC_CHECK3, check1);
		DDX_Control(pDX, IDC_CHECK8, check2);
		DDX_Control(pDX, IDC_CHECK9, check3);
		DDX_Control(pDX, IDC_CHECK10, check4);
		DDX_Control(pDX, IDC_CHECK11, check5);
		DDX_Control(pDX, IDC_CHECK12, check6);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyComboBox combo5;
	CMyComboBox combo6;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	CMyEdit edit5;
	CMyEdit edit6;
	CMyEdit edit7;
	CButton check1;
	CButton check2;
	CButton check3;
	CButton check4;
	CButton check5;
	CButton check6;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_78 :
	public myDialog
{
public:
	Dialog_78() noexcept : myDialog(IDD_DIALOG78) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG78 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_COMBO8, combo4);
		DDX_Control(pDX, IDC_COMBO9, combo5);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
		DDX_Control(pDX, IDC_EDIT5, edit5);
		DDX_Control(pDX, IDC_EDIT6, edit6);
		DDX_Control(pDX, IDC_STATIC1, static1);
		DDX_Control(pDX, IDC_STATIC3, static2);
		DDX_Control(pDX, IDC_STATIC4, static3);
	}
public:
	CStatic static1;
	CStatic static2;
	CStatic static3;
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyComboBox combo5;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	CMyEdit edit5;
	CMyEdit edit6;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnCbnSelchangeCombo8();
};


class Dialog_79 :
	public myDialog
{
public:
	Dialog_79() noexcept : myDialog(IDD_DIALOG79) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG79 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
		DDX_Control(pDX, IDC_CHECK3, check3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CButton check1;
	CButton check2;
	CButton check3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_80 :
	public myDialog
{
public:
	Dialog_80() noexcept : myDialog(IDD_DIALOG80) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG80 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CButton check1;
	CButton check2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_82 :
	public myDialog
{
public:
	Dialog_82() noexcept : myDialog(IDD_DIALOG82) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG82 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_83 :
	public myDialog
{
public:
	Dialog_83() noexcept : myDialog(IDD_DIALOG83) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG83 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
		DDX_Control(pDX, IDC_CHECK3, check1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_86 :
	public myDialog
{
public:
	Dialog_86() noexcept : myDialog(IDD_DIALOG86) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG86 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_88 :
	public myDialog
{
public:
	Dialog_88() noexcept : myDialog(IDD_DIALOG88) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG88 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	CButton check1;
	CButton check2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_89 :
	public myDialog
{
public:
	Dialog_89() noexcept : myDialog(IDD_DIALOG89) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG89 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
		DDX_Control(pDX, IDC_COMBO5, combo5);
		DDX_Control(pDX, IDC_EDIT6, edit6);
		DDX_Control(pDX, IDC_CHECK3, check1);
	}
public:
	CStatic edit1;
	CMyComboBox combo1;
	CEdit edit2;
	CMyComboBox combo3;
	CEdit edit4;
	CMyComboBox combo5;
	CEdit edit6;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_91 :
	public myDialog
{
public:
	Dialog_91() noexcept : myDialog(IDD_DIALOG91) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG91 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK3, check1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_EDIT4, edit4);
	}
public:
	CButton check1;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CMyEdit edit4;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_93 :
	public myDialog
{
public:
	Dialog_93() noexcept : myDialog(IDD_DIALOG93) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG93 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_96 :
	public myDialog
{
public:
	Dialog_96() noexcept : myDialog(IDD_DIALOG96) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG96 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CButton check1;
	CMyEdit edit1;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_99 :
	public myDialog
{
public:
	Dialog_99() noexcept : myDialog(IDD_DIALOG99) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG99 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CButton check1;
	CButton check2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_100 :
	public myDialog
{
public:
	Dialog_100() noexcept : myDialog(IDD_DIALOG100) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG100 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CButton check1;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_101 :
	public myDialog
{
public:
	Dialog_101() noexcept : myDialog(IDD_DIALOG101) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG101 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CButton check1;
	CButton check2;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_102 :
	public myDialog
{
public:
	Dialog_102() noexcept : myDialog(IDD_DIALOG102) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG102 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK2, check2);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CButton check1;
	CButton check2;
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_103 :
	public myDialog
{
public:
	Dialog_103() noexcept : myDialog(IDD_DIALOG103) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG103
	};
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_104 :
	public myDialog
{
public:
	Dialog_104() noexcept : myDialog(IDD_DIALOG104) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG104 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_107 :
	public myDialog
{
public:
	Dialog_107() noexcept : myDialog(IDD_DIALOG107) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG107 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_CHECK1, check1);
	}
public:
	CMyComboBox combo1;
	CMyEdit edit1;
	CMyEdit edit2;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_109 :
	public myDialog
{
public:
	Dialog_109() noexcept : myDialog(IDD_DIALOG109) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG109 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_COMBO4, combo3);
		DDX_Control(pDX, IDC_COMBO8, combo4);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
		DDX_Control(pDX, IDC_CHECK1, check1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyComboBox combo4;
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	CButton check1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
};


class Dialog_111 :
	public myDialog
{
public:
	Dialog_111() noexcept : myDialog(IDD_DIALOG111) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG111 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
	}
public:
	CMyComboBox combo1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_112 :
	public myDialog
{
public:
	Dialog_112() noexcept : myDialog(IDD_DIALOG112) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG112 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_EDIT1, edit1);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_114 :
	public myDialog
{
public:
	Dialog_114() noexcept : myDialog(IDD_DIALOG114) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum {
		IDD = IDD_DIALOG114
	};
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_COMBO3, combo2);
		DDX_Control(pDX, IDC_BUTTON1, button1);
		DDX_Control(pDX, IDC_LIST1, list1);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	CComboBox combo1;
	CComboBox combo2;
	CButton button1;
	CListBox list1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnCbnSelchangeCombo2();
	afx_msg void OnBnClickedButton1();
};

class Dialog_115 :
	public myDialog
{
public:
	Dialog_115() noexcept : myDialog(IDD_DIALOG115) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG115 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_CHECK1, check1);
		DDX_Control(pDX, IDC_CHECK3, check2);
		DDX_Control(pDX, IDC_CHECK5, check3);
		DDX_Control(pDX, IDC_CHECK6, check4);
		DDX_Control(pDX, IDC_CHECK7, check5);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyEdit edit1;
	CButton check1;
	CButton check2;
	CButton check3;
	CButton check4;
	CButton check5;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_119 :
	public myDialog
{
public:
	Dialog_119() noexcept : myDialog(IDD_DIALOG119) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG119};
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT3, edit2);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_120 :
	public myDialog
{
public:
	Dialog_120() noexcept : myDialog(IDD_DIALOG120) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG120 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_121 :
	public myDialog
{
public:
	Dialog_121() noexcept : myDialog(IDD_DIALOG121) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG121 };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_COMBO1, combo1);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT3, edit2);
		DDX_Control(pDX, IDC_COMBO2, combo2);
		DDX_Control(pDX, IDC_COMBO3, combo3);
	}
public:
	CMyComboBox combo1;
	CMyComboBox combo2;
	CMyComboBox combo3;
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_Dup :
	public myDialog
{
public:
	Dialog_Dup() noexcept : myDialog(IDD_DIALOGDUP) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOGDUP };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
		DDX_Control(pDX, IDC_EDIT3, edit3);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	CMyEdit edit3;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};


class Dialog_Edit :
	public myDialog
{
public:
	Dialog_Edit() noexcept : myDialog(IDD_DIALOGEDIT) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOGEDIT };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_EDIT1, edit1);
		DDX_Control(pDX, IDC_EDIT2, edit2);
	}
public:
	CMyEdit edit1;
	CMyEdit edit2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};

class Dialog_Var :
	public myDialog
{
public:
	Dialog_Var() noexcept : myDialog(IDD_DIALOGVAR) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOGVAR };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) {
		CDialogEx::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_BUTTON1, button1);
		DDX_Control(pDX, IDC_BUTTON2, button2);
		DDX_Control(pDX, IDC_LIST1, list1);
		DDX_Control(pDX, IDC_LIST2, list2);
	}
	void recur_SearchVar(HTREEITEM cur, CcczEditor2View* pView);
public:
	CButton button1;
	CButton button2;
	CListBox list1;
	CListBox list2;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnDClickList1();
	afx_msg void OnDClickList2();
	afx_msg void OnBnClickedOk1();
	afx_msg void OnBnClickedOk2();
};


class Dialog_Color :
	public myDialog
{
public:
	Dialog_Color() noexcept : myDialog(IDD_DIALOGCOLOR) {}
	virtual BOOL OnInitDialog();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOGCOLOR};
#endif
public:
	CButton button[174];
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
};
