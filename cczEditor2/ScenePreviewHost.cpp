#include "pch.h"

#include "cczEditor2.h"
#include "ScenePreviewHost.h"
#include "MainFrm.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"

#include <cwctype>
#include <string>

BEGIN_MESSAGE_MAP(CScenePreviewWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CScenePreviewWnd::Page CScenePreviewWnd::PageForPath(
	const wchar_t* documentPath)
{
	if (documentPath == nullptr || *documentPath == L'\0')
		return Page::Neutral;
	std::wstring fileName(documentPath);
	const std::size_t slash = fileName.find_last_of(L"\\/");
	if (slash != std::wstring::npos) fileName.erase(0, slash + 1);
	for (wchar_t& ch : fileName)
		ch = static_cast<wchar_t>(std::towupper(ch));
	if (fileName.rfind(L"S_", 0) == 0 || fileName.rfind(L"S-", 0) == 0)
		return Page::SBattle;
	if (fileName.rfind(L"R_", 0) == 0 || fileName.rfind(L"R-", 0) == 0)
		return Page::RScene;
	return Page::Neutral;
}

void CScenePreviewWnd::ShowPage(Page page)
{
	if (m_documentOpenDepth != 0) return;
	const bool pageChanged = m_activePage != page;
	m_activePage = page;
	if (m_rPage.GetSafeHwnd() != nullptr) {
		if (page != Page::RScene) m_rPage.ShowWindow(SW_HIDE);
	}
	if (m_sPage.GetSafeHwnd() != nullptr) {
		if (page != Page::SBattle) m_sPage.ShowWindow(SW_HIDE);
	}
	if (m_neutralLabel.GetSafeHwnd() != nullptr) {
		if (page != Page::Neutral) m_neutralLabel.ShowWindow(SW_HIDE);
	}

	CWnd* activePage = nullptr;
	switch (page) {
	case Page::RScene:
		activePage = &m_rPage;
		break;
	case Page::SBattle:
		activePage = &m_sPage;
		break;
	case Page::Neutral:
		activePage = &m_neutralLabel;
		break;
	}
	if (activePage == nullptr || activePage->GetSafeHwnd() == nullptr)
		return;

	activePage->ShowWindow(SW_SHOW);
	if (pageChanged) {
		// A hidden page can retain valid pixels from before another preview was
		// displayed. Repaint the full page and its child controls as one switch.
		activePage->RedrawWindow(nullptr, nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	}
}

int CScenePreviewWnd::OnCreate(LPCREATESTRUCT create)
{
	if (CDockablePane::OnCreate(create) == -1) return -1;
	if (!m_rPage.CreatePage(this, 0x7F60)) return -1;
	if (!m_sPage.CreatePage(this, 0x7F61)) return -1;
	if (!m_neutralLabel.Create(
			L"请加载R剧本或S剧本",
			WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
			CRect(0, 0, 1, 1), this, 0x7F62)) {
		return -1;
	}
	ShowPage(Page::Neutral);
	return 0;
}

void CScenePreviewWnd::OnSize(UINT type, int cx, int cy)
{
	CDockablePane::OnSize(type, cx, cy);
	const CRect bounds(0, 0, cx, cy);
	if (m_rPage.GetSafeHwnd() != nullptr) m_rPage.MoveWindow(bounds);
	if (m_sPage.GetSafeHwnd() != nullptr) m_sPage.MoveWindow(bounds);
	if (m_neutralLabel.GetSafeHwnd() != nullptr)
		m_neutralLabel.MoveWindow(bounds);
}

bool CScenePreviewWnd::RequestPreviewRefresh(
	CTreeCtrl& tree, HTREEITEM selectedItem, CcczEditor2Doc& document)
{
	if (m_documentOpenDepth != 0) return true;
	const wchar_t* documentPath = document.pathName;
	const Page page = PageForPath(documentPath);
	if (page == Page::RScene) {
		ParkActiveSBattleDocument();
		bool reusedUnchangedSession = false;
		if (!m_rPage.ActivateDocument(
				tree, selectedItem, document, reusedUnchangedSession)) {
			return false;
		}
		ShowPage(page);
		// Selection/edit requests remain forced refreshes.  Only MDI activation
		// is allowed to take the unchanged-session fast path.
		m_rPage.RequestPreviewRefresh(tree, selectedItem, document);
		return true;
	}
	if (page == Page::SBattle) {
		bool reusedUnchangedSession = false;
		if (!ActivateSBattleDocument(document, tree, reusedUnchangedSession))
			return false;
		m_rPage.ParkActiveDocument();
		if (reusedUnchangedSession) {
			ShowPage(page);
			return true;
		}
		if (!m_sPage.RequestPreviewRefresh(tree, selectedItem, documentPath))
			return false;
		ShowPage(page);
		return true;
	}
	ParkActiveSBattleDocument();
	m_rPage.ParkActiveDocument();
	ShowPage(Page::Neutral);
	return true;
}

bool CScenePreviewWnd::ActivateScriptPreview(
	CTreeCtrl& tree, HTREEITEM selectedItem, CcczEditor2Doc& document)
{
	if (m_documentOpenDepth != 0) return true;
	const Page page = PageForPath(document.pathName);
	if (page == Page::RScene) {
		ParkActiveSBattleDocument();
		bool reusedUnchangedSession = false;
		if (!m_rPage.ActivateDocument(
			tree, selectedItem, document, reusedUnchangedSession)) {
			return false;
		}
		ShowPage(page);
		if (!reusedUnchangedSession)
			m_rPage.RequestPreviewRefresh(tree, selectedItem, document);
		return true;
	}
	if (page == Page::SBattle) {
		m_rPage.ParkActiveDocument();
		bool reusedUnchangedSession = false;
		if (!ActivateSBattleDocument(document, tree, reusedUnchangedSession))
			return false;
		if (reusedUnchangedSession) {
			ShowPage(page);
			return true;
		}
		if (!m_sPage.RequestPreviewRefresh(
			tree, selectedItem, document.pathName)) return false;
		ShowPage(page);
		return true;
	}
	ParkActiveSBattleDocument();
	m_rPage.ParkActiveDocument();
	ShowPage(Page::Neutral);
	return true;
}

bool CScenePreviewWnd::ActivateSBattleDocument(
	CcczEditor2Doc& document, CTreeCtrl& tree,
	bool& reusedUnchangedSession)
{
	reusedUnchangedSession = false;
	const auto documentId = document.GetSPreviewDocumentId();
	if (documentId == 0) return false;
	const bool switchingDocument = m_activeSBattleDocumentId != documentId;
	if (switchingDocument)
		ParkActiveSBattleDocument();
	auto& session = m_sBattleSessions.GetOrCreate(documentId);
	const auto currentVersion = ccz::editor_preview::BattlePreviewDocumentVersion{
		document.GetPreviewContentEpoch(),
		document.GetPreviewTreeGeneration()};
	const auto activationCheck =
		ccz::editor_preview::CheckBattlePreviewActivation(
			session.has_baseline, session.observed_version, currentVersion);
	const CString currentDocumentPath(document.pathName);
	const bool sameDocumentPath = !session.document_path.empty() &&
		currentDocumentPath.CompareNoCase(session.document_path.c_str()) == 0;
	reusedUnchangedSession = switchingDocument &&
		sameDocumentPath &&
		activationCheck ==
			ccz::editor_preview::BattlePreviewActivationCheck::ReuseSession &&
		!session.ui.dynamic_preview_enabled;
	m_activeSBattleDocumentId = documentId;
	m_sPage.BindSession(session, document, tree);
	return true;
}

bool CScenePreviewWnd::SetBattleQuickModeEnabled(bool enabled)
{
	CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
	CcczEditor2View* view = child != nullptr
		? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
	if (view == nullptr || view->GetDocument() == nullptr) return false;
	auto& document = *view->GetDocument();
	CTreeCtrl& tree = view->GetTreeCtrl();
	bool reusedUnchangedSession = false;
	if (!ActivateSBattleDocument(document, tree, reusedUnchangedSession))
		return false;
	const wchar_t* path = document.pathName;
	const bool accepted = m_sPage.SetQuickModeEnabled(
		enabled, tree, tree.GetSelectedItem(), path);
	if (accepted) ShowPage(Page::SBattle);
	return accepted;
}

bool CScenePreviewWnd::CanCloseDocument(
	ccz::editor_preview::SPreviewDocumentId documentId)
{
	if (documentId == 0 ||
		m_closeApprovalDocumentIds.find(documentId) !=
			m_closeApprovalDocumentIds.end()) return true;
	const auto* session = m_sBattleSessions.Find(documentId);
	if (session == nullptr || !m_sPage.ConfirmDiscardSessionChanges(*session))
		return session == nullptr;
	m_closeApprovalDocumentIds.insert(documentId);
	return true;
}

void CScenePreviewWnd::CancelDocumentClose(
	ccz::editor_preview::SPreviewDocumentId documentId) noexcept
{
	m_closeApprovalDocumentIds.erase(documentId);
}

void CScenePreviewWnd::CloseSBattleDocument(
	ccz::editor_preview::SPreviewDocumentId documentId)
{
	if (documentId == 0) return;
	m_closeApprovalDocumentIds.erase(documentId);
	if (m_activeSBattleDocumentId == documentId) {
		m_sPage.UnbindSession();
		m_activeSBattleDocumentId = 0;
		if (m_activePage == Page::SBattle) ShowPage(Page::Neutral);
	}
	m_sBattleSessions.Erase(documentId);
}

void CScenePreviewWnd::CloseRDocument(
	ccz::editor_preview::PreviewDocumentId documentId)
{
	if (m_rPage.CloseDocument(documentId) && m_activePage == Page::RScene)
		ShowPage(Page::Neutral);
}

void CScenePreviewWnd::EditorTreeDestroyed(
	HWND treeWindow, CcczEditor2Doc& document)
{
	const bool wasActiveRDocument = PageForPath(document.pathName) == Page::RScene &&
		m_rPage.EditorTreeDestroyed(treeWindow, document);
	const bool wasBound = m_sPage.IsBoundToTree(treeWindow);
	if (wasBound) ParkActiveSBattleDocument();
	m_sPage.EditorTreeDestroyed(treeWindow);
	if (wasActiveRDocument && m_activePage == Page::RScene)
		ShowPage(Page::Neutral);
	if (wasBound && m_activePage == Page::SBattle)
		ShowPage(Page::Neutral);
}

void CScenePreviewWnd::PrepareForTreeRebuild(CcczEditor2Doc& document)
{
	const Page ownerPage = PageForPath(document.pathName);
	if (ownerPage == Page::RScene) {
		if (m_rPage.PrepareForTreeRebuild(document) &&
			m_activePage == Page::RScene) ShowPage(Page::Neutral);
		return;
	}
	if (ownerPage == Page::SBattle &&
		m_activeSBattleDocumentId == document.GetSPreviewDocumentId()) {
		// Park only the S session whose TreeView is being replaced. A different
		// document's rebuild must not disturb the currently visible preview.
		ParkActiveSBattleDocument();
		if (m_activePage == Page::SBattle) ShowPage(Page::Neutral);
	}
}

void CScenePreviewWnd::ParkActiveSBattleDocument()
{
	if (m_activeSBattleDocumentId == 0) return;
	m_sPage.UnbindSession();
	m_activeSBattleDocumentId = 0;
}

void CScenePreviewWnd::InvalidatePendingPreviewRefresh(
	CcczEditor2Doc& document)
{
	const Page ownerPage = PageForPath(document.pathName);
	if (ownerPage == Page::RScene) {
		m_rPage.InvalidateDocumentPreview(document.GetPreviewDocumentId());
		if (m_rPage.ActiveDocumentId() == document.GetPreviewDocumentId())
			ShowPage(Page::Neutral);
	}
	// S content edits are tracked by the owning document's content epoch. Keep
	// that document's draft/session intact so the next activation can revalidate
	// or mark it stale instead of silently clearing it here.
}

void CScenePreviewWnd::EndDocumentOpen()
{
	if (m_documentOpenDepth == 0 || --m_documentOpenDepth != 0) return;
	CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
	CcczEditor2View* view = child != nullptr
		? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
	// Never replay pointers captured by an intermediate activation: the MDI
	// owner and its tree are stable only after OpenDocumentFile returns.
	if (view != nullptr && view->m_previewTreeInitialized)
		view->RefreshScenePreview();
}

void CScenePreviewWnd::InitCharList(wchar_t(*pPer2)[30])
{
	if (m_documentOpenDepth != 0) return;
	// This compatibility entrypoint belongs exclusively to the R page.
	if (m_activePage == Page::RScene)
		m_rPage.InitCharList(pPer2);
}

void CScenePreviewWnd::HideInternalizedEnhancementViews(
	const std::wstring& diagnostic)
{
	if (m_activePage == Page::RScene)
		m_rPage.HideInternalizedEnhancementViews(diagnostic);
}
