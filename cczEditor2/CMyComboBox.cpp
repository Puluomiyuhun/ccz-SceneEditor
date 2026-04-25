#include "pch.h"
#include "CMyComboBox.h"


BOOL CMyComboBox::PreTranslateMessage(MSG* pMsg)
{
	//return CComboBox::PreTranslateMessage(pMsg);
	if (pMsg->message == WM_KEYUP) {
		return TRUE;
	}
	if (pMsg->message == WM_CHAR) {
		wchar_t a = pMsg->wParam;
		inputs[cnt++] = a;
		CString txt;
		bool flag1 = false;
		for (int i = 0; i < this->GetCount(); i++)
		{
			this->GetLBText(i, txt);
			bool flag2 = true;
			if (cnt > txt.GetLength())continue;
			for (int j = 0; j < cnt; j++)
			{
				if (j >= txt.GetLength())continue;
				if (txt[j] != inputs[j]) flag2 = false;
			}
			if (flag2)
			{
				this->SetCurSel(i);
				cur_selected = i;
				flag1 = true;
				break;
			}
		}
		if (flag1 == false) {
			cnt = 0;
			inputs[cnt++] = a;
			CString txt;
			bool flag1 = false;
			for (int i = 0; i < this->GetCount(); i++)
			{
				this->GetLBText(i, txt);
				bool flag2 = true;
				for (int j = 0; j < cnt; j++)
				{
					if (txt[j] != inputs[j]) flag2 = false;
				}
				if (flag2)
				{
					this->SetCurSel(i);
					cur_selected = i;
					flag1 = true;
					break;
				}
			}
		}

		WPARAM wParam = MAKELPARAM(this->GetDlgCtrlID(), CBN_SELCHANGE);
		HWND hWnd = this->m_hWnd;
		this->GetParent()->SendMessage(WM_COMMAND, wParam, (LPARAM)hWnd);
		return TRUE;
	}
	// TODO: 在此添加专用代码和/或调用基类
	if (pMsg->message == WM_KEYDOWN) {
		wchar_t a = pMsg->wParam;
		if (a == 13)return CComboBox::PreTranslateMessage(pMsg);
		if (a >= VK_NUMPAD0 && a <= VK_NUMPAD9)
			a = a - VK_NUMPAD0 + '0';
		else if (a == VK_UP || a == VK_DOWN)
			return CComboBox::PreTranslateMessage(pMsg);
		else
			return CComboBox::PreTranslateMessage(pMsg);
		inputs[cnt++] = a;
		CString txt;
		bool flag1 = false;
		for (int i = 0; i < this->GetCount(); i++)
		{
			this->GetLBText(i, txt);
			bool flag2 = true;
			for (int j = 0; j < cnt; j++)
			{
				if (j >= txt.GetLength())continue;
				if (txt[j] != inputs[j]) flag2 = false;
			}
			if (flag2)
			{
				this->SetCurSel(i);
				cur_selected = i;
				flag1 = true;
				break;
			}
		}
		if (flag1 == false) {
			cnt = 0;
			inputs[cnt++] = a;
			CString txt;
			bool flag1 = false;
			for (int i = 0; i < this->GetCount(); i++)
			{
				this->GetLBText(i, txt);
				bool flag2 = true;
				for (int j = 0; j < cnt; j++)
				{
					if (txt[j] != inputs[j]) flag2 = false;
				}
				if (flag2)
				{
					this->SetCurSel(i);
					cur_selected = i;
					flag1 = true;
					break;
				}
			}
		}

		WPARAM wParam = MAKELPARAM(this->GetDlgCtrlID(), CBN_SELCHANGE);
		HWND hWnd = this->m_hWnd;
		this->GetParent()->SendMessage(WM_COMMAND, wParam, (LPARAM)hWnd);

		return TRUE;
	}

	return CComboBox::PreTranslateMessage(pMsg);
}

BOOL CMyEdit::PreTranslateMessage(MSG* pMsg)
{
	DWORD dwStyle = ::GetWindowLong(this->GetSafeHwnd(), GWL_STYLE);
	DWORD dwStyle2 = dwStyle &= ES_MULTILINE;
	if (dwStyle2)return CEdit::PreTranslateMessage(pMsg);
	if (pMsg->message == WM_CHAR)
	{
		if ((pMsg->wParam <= '9' && pMsg->wParam >= '0') || (pMsg->wParam <= 'f' && pMsg->wParam >= 'a')
			|| pMsg->wParam == '-' || pMsg->wParam == 'x' || pMsg->wParam == VK_BACK || pMsg->wParam == 3 || pMsg->wParam == 22)
		{
			return CEdit::PreTranslateMessage(pMsg);
		}
		else
			return TRUE;
	}
	/*if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_CONTROL)
			return CEdit::PreTranslateMessage(pMsg);
		if ((pMsg->wParam <= '9' && pMsg->wParam >= '0') || (pMsg->wParam <= VK_NUMPAD9 && pMsg->wParam >= VK_NUMPAD0)
			|| pMsg->wParam == '-' || pMsg->wParam == 'X' || pMsg->wParam == VK_BACK || pMsg->wParam == 'c' || pMsg->wParam == 'v')
		{
			return CEdit::PreTranslateMessage(pMsg);
		}
		else return TRUE;
	}*/
	return CEdit::PreTranslateMessage(pMsg);
}