#pragma once
#include <afxwin.h>
class CMyComboBox :
    public CComboBox
{
public:
    virtual BOOL PreTranslateMessage(MSG* pMsg);
private:
    wchar_t inputs[10];
    int cnt = 0;
    int cur_selected = -1;
};

class CMyEdit :
    public CEdit
{
public:
    virtual BOOL PreTranslateMessage(MSG* pMsg);
};

class CMyList :
    public CListBox
{
};