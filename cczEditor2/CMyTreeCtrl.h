#pragma once
#include <afxcmn.h>
class CMyTreeCtrl :
    public CTreeCtrl
{
public:
    CMyTreeCtrl();
    virtual ~CMyTreeCtrl();
    void SetItemColor(HTREEITEM hItem, COLORREF color);
protected:
    afx_msg void OnPaint();

protected:
    DECLARE_MESSAGE_MAP()
};

