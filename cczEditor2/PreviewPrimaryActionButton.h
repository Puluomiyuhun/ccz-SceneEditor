#pragma once
#include <afxwin.h>

class CBattlePrimaryActionButton : public CButton
{
public:
	void SetUndoStyle() noexcept { m_undoStyle = true; }
	void SetActivationStyle() noexcept { m_activationStyle = true; }
protected:
	void DrawItem(LPDRAWITEMSTRUCT draw_item) override;
private:
	bool m_undoStyle{};
	bool m_activationStyle{};
};

