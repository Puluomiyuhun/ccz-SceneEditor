#pragma once

#include <afxdockablepane.h>

#include "BattlePreviewPage.h"
#include "ScenePreviewWnd.h"
#include "PreviewIntegration/BattlePreviewSession.h"

#include <set>

class CcczEditor2Doc;

class CScenePreviewWnd : public CDockablePane
{
public:
	CScenePreviewWnd() noexcept = default;
	virtual ~CScenePreviewWnd() = default;

	bool RequestPreviewRefresh(CTreeCtrl& tree, HTREEITEM selected_item,
		CcczEditor2Doc& document);
	bool ActivateScriptPreview(CTreeCtrl& tree, HTREEITEM selected_item,
		CcczEditor2Doc& document);
	bool ActivateSBattleDocument(CcczEditor2Doc& document, CTreeCtrl& tree,
		bool& reused_unchanged_session);
	bool SetBattleQuickModeEnabled(bool enabled);
	bool CanCloseDocument(ccz::editor_preview::SPreviewDocumentId document_id);
	void CancelDocumentClose(
		ccz::editor_preview::SPreviewDocumentId document_id) noexcept;
	void CloseSBattleDocument(
		ccz::editor_preview::SPreviewDocumentId document_id);
	void CloseRDocument(
		ccz::editor_preview::PreviewDocumentId document_id);
	void EditorTreeDestroyed(HWND tree_window, CcczEditor2Doc& document);
	void PrepareForTreeRebuild(CcczEditor2Doc& document);
	void ParkActiveSBattleDocument();
	void InvalidatePendingPreviewRefresh(CcczEditor2Doc& document);
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
	ccz::editor_preview::BattlePreviewSessionRegistry m_sBattleSessions;
	ccz::editor_preview::SPreviewDocumentId m_activeSBattleDocumentId{};
	std::set<ccz::editor_preview::SPreviewDocumentId>
		m_closeApprovalDocumentIds;
	CStatic m_neutralLabel;
	Page m_activePage = Page::Neutral;
};
