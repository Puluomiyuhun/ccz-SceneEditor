
// cczEditor2.h: cczEditor2 应用程序的主头文件
//
#pragma once

#ifndef __AFXWIN_H__
	#error "在包含此文件之前包含 'pch.h' 以生成 PCH"
#endif

#include "resource.h"       // 主符号
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"


// CcczEditor2App:
// 有关此类的实现，请参阅 cczEditor2.cpp
//

class CcczEditor2App : public CWinAppEx
{
public:
	CcczEditor2App() noexcept;


// 重写
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();

	void deleteCopy(ItemData *data);

	void OnFileOpen();
// 实现
	UINT  m_nAppLook;
	BOOL  m_bHiColorIcons;
	ItemData *copy;
	ItemData* copys[1000];
	int copys_sum = 0;
	int condition = 0;

	byte custom_color[124][2][4];
	byte custom_height;
	LOGFONT custom_font;
	CFont custom_fonts;
	bool custom_use_font;
	bool custom_night_mode;

	int last_item_use = 0x14;
	bool search = false;
	int search_goal = -1;

	virtual void PreLoadState();
	virtual void LoadCustomState();
	virtual void SaveCustomState();

	afx_msg void OnAppAbout();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnSaveCustom();
};

extern CcczEditor2App theApp;
