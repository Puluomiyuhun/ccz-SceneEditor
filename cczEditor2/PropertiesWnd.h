
#pragma once

class CPropertiesToolBar : public CMFCToolBar
{
public:
	virtual void OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL bDisableIfNoHndler)
	{
		CMFCToolBar::OnUpdateCmdUI((CFrameWnd*) GetOwner(), bDisableIfNoHndler);
	}

	virtual BOOL AllowShowOnList() const { return FALSE; }
};

class CPropertiesWnd : public CDockablePane
{
// 构造
public:
	CPropertiesWnd() noexcept;

	void AdjustLayout();

// 特性
public:
	void SetVSDotNetLook(BOOL bSet)
	{
		m_wndPropList.SetVSDotNetLook(bSet);
		m_wndPropList.SetGroupNameFullWidth(bSet);
	}

protected:
	CFont m_fntPropList;
	CComboBox m_wndObjectCombo;
	CPropertiesToolBar m_wndToolBar;
	CMFCPropertyGridCtrl m_wndPropList;

	wchar_t code[200][30] = { L"0:事件结束",L"1:子事件设定",L"2:内部信息",L"3:else",L"4:询问测试",L"5:变量测试",L"6:我军出场限制",L"7:出战测试",L"8:菜单处理",L"9:延时",L"a:初始化局部变量",L"b:变量赋值",L"c:结束Section",L"d:结束Scene",L"e:战斗失败",L"f:结局设定",L"10:许子将指导",L"11:剧本跳转",L"12:选择框",L"13:case",L"14:对话 ",L"15:对话2",L"16:信息",L"17:场所名",L"18:事件名称设定",L"19:胜利条件",L"1a:显示胜利条件",L"1b:撤退信息是否显示设定",L"1c:绘图",L"1d:调色板设定",L"1e:武将重绘",L"1f:地图视点切换",L"20:武将头像状态设置",L"21:战场物体添加",L"22:动画",L"23:音效",L"24:CD音轨",L"25:武将进入指定地点测试",L"26:武将进入指定区域测试",L"27:背景显示",L"28:自由R启动指令",L"29:地图头像显示",L"2a:地图头像移动",L"2b:地图头像消失",L"2c:地图文字显示",L"2d:武将点击测试",L"2e:武将相邻测试",L"2f:清除人物",L"30:武将出现",L"31:武将消失",L"32:武将移动",L"33:武将转向",L"34:武将动作",L"35:武将形象改变",L"36:武将状态测试",L"37:钱、章节编号、忠奸测试",L"38:武将能力设定",L"39:武将等级提升",L"3a:钱、章节编号、忠奸设置",L"3b:武将加入",L"3c:武将加入测试",L"3d:获得物品",L"3e:加入装备设定",L"3f:回合测试",L"40:行动方测试",L"41:战场人数测试",L"42:战斗胜利测试",L"43:战斗失败测试",L"44:战斗初始化",L"45:战场全局变量",L"46:友军出场设定",L"47:敌军出场设定",L"48:敌方装备设定",L"49:战斗结束",L"4a:我军出场强制设定",L"4b:我军出场设定",L"4c:隐藏武将出现",L"4d:武将状态变更",L"4e:武将方针变更",L"4f:战场转向设置",L"50:战场动作设定",L"51:战场恢复行动权",L"52:兵种改变",L"53:战场撤退",L"54:战场撤退确认",L"55:战场复活",L"56:天气类别设定",L"57:当前天气设定",L"58:战场障碍设定",L"59:战利品",L"5a:战场操作开始",L"5b:战场高亮区域",L"5c:战场高亮人物",L"5d:回合上限设定",L"5e:武将不同测试",L"5f:单挑结束",L"60:单挑武将出场",L"61:单挑胜负显示",L"62:单挑阵亡",L"63:单挑对话",L"64:单挑动作",L"65:单挑攻击1",L"66:单挑攻击2",L"67:章名",L"68:单挑开始",L"69:旁白",L"6a:Game Over指令",L"6b:法术",L"6c:武将能力复制",L"6d:相对复活或移动",L"6e:概率测试",L"6f:丢弃物品",L"70:能力选择复制",L"71:特效请求",L"72:信息传送",L"73:人物五围和测试",L"74:开/禁存档",L"75:S特殊形象指定",L"76:无条件跳转",L"77:变量运算",L"78:整型变量赋值",L"79:变量测试",L"7a:对话3" };


// 实现
public:
	virtual ~CPropertiesWnd();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnExpandAllProperties();
	afx_msg void OnUpdateExpandAllProperties(CCmdUI* pCmdUI);
	afx_msg void OnSortProperties();
	afx_msg void OnUpdateSortProperties(CCmdUI* pCmdUI);
	afx_msg void OnProperties1();
	afx_msg void OnUpdateProperties1(CCmdUI* pCmdUI);
	afx_msg void OnProperties2();
	afx_msg void OnUpdateProperties2(CCmdUI* pCmdUI);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg
		LRESULT OnPropertyChanged(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

	void InitPropList();
	void SetPropListFont();

	int m_nComboHeight;
};

