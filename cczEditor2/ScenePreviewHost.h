#pragma once

#include <afxdockablepane.h>

#include "BattlePreviewPage.h"
#include "ScenePreviewWnd.h"

class CScenePreviewWnd : public CDockablePane
{
public:
	CScenePreviewWnd() noexcept = default;
	virtual ~CScenePreviewWnd() = default;

	void RequestPreviewRefresh(CTreeCtrl& tree, HTREEITEM selected_item,
		const wchar_t* document_path);
	void InvalidatePendingPreviewRefresh();
	void InitCharList(wchar_t(*pPer2)[30]);
	void HideInternalizedEnhancementViews(const std::wstring& diagnostic);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT create);
	afx_msg void OnSize(UINT type, int cx, int cy);
	DECLARE_MESSAGE_MAP()

private:
	enum class Page { Neutral, RScene, SBattle };
	static Page PageForPath(const wchar_t* document_path);
	void ShowPage(Page page);

	CRScenePreviewPage m_rPage;
	CSBattlePreviewPage m_sPage;
	CStatic m_neutralLabel;
	Page m_activePage = Page::Neutral;
};
