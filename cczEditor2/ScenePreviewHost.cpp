#include "pch.h"

#include "ScenePreviewHost.h"

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
	m_activePage = page;
	if (m_rPage.GetSafeHwnd() != nullptr)
		m_rPage.ShowWindow(page == Page::RScene ? SW_SHOW : SW_HIDE);
	if (m_sPage.GetSafeHwnd() != nullptr)
		m_sPage.ShowWindow(page == Page::SBattle ? SW_SHOW : SW_HIDE);
	if (m_neutralLabel.GetSafeHwnd() != nullptr)
		m_neutralLabel.ShowWindow(page == Page::Neutral ? SW_SHOW : SW_HIDE);
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

void CScenePreviewWnd::RequestPreviewRefresh(
	CTreeCtrl& tree, HTREEITEM selectedItem, const wchar_t* documentPath)
{
	const Page page = PageForPath(documentPath);
	if (page == Page::RScene) {
		m_sPage.ClearDocument();
		ShowPage(page);
		m_rPage.RequestPreviewRefresh(tree, selectedItem, documentPath);
		return;
	}
	if (page == Page::SBattle) {
		m_rPage.InvalidatePendingPreviewRefresh();
		ShowPage(page);
		m_sPage.RequestPreviewRefresh(tree, selectedItem, documentPath);
		return;
	}
	InvalidatePendingPreviewRefresh();
	ShowPage(Page::Neutral);
}

void CScenePreviewWnd::InvalidatePendingPreviewRefresh()
{
	m_rPage.InvalidatePendingPreviewRefresh();
	m_sPage.ClearDocument();
	ShowPage(Page::Neutral);
}

void CScenePreviewWnd::InitCharList(wchar_t(*pPer2)[30])
{
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
