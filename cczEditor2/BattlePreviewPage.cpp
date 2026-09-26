#include "pch.h"

#include "cczEditor2.h"
#include "BattlePreviewPage.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"
#include "LongCharData.h"
#include "MainFrm.h"
#include "PreviewIntegration/BattlePreviewAdapter.h"
#include "PreviewIntegration/BattlePreviewPredicates.h"
#include "PreviewIntegration/E5ImageLoader.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
#include "PreviewIntegration/BattleWritebackQaGate.h"
#endif

#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/battle_instruction_parser.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>
#include <tuple>

namespace {
constexpr int kCellPixels = 48;
constexpr float kDefaultZoom = 1.0f;
constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 3.0f;
constexpr float kZoomStep = 0.25f;
constexpr int kSidebarWidth = 246;
constexpr UINT kCharacterTabId = 0xA100;
constexpr UINT kBrushTabId = 0xA101;
constexpr UINT kHideSidebarId = 0xA102;
constexpr UINT kShowSidebarId = 0xA103;
constexpr UINT kPreviousPageId = 0xA104;
constexpr UINT kNextPageId = 0xA105;
constexpr UINT kUndoId = 0xA106;
constexpr UINT kSearchId = 0xA107;
constexpr UINT kDeploymentComboId = 0xA108;
constexpr UINT kCharacterListId = 0xA109;
constexpr UINT kPageLabelId = 0xA10A;
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
constexpr UINT kQaWritebackId = 0xA10B;

std::wstring battle_writeback_qa_copy_root()
{
	std::vector<wchar_t> executablePath(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
		static_cast<DWORD>(executablePath.size()));
	if (length == 0 || length >= executablePath.size()) return {};
	return ccz::editor_preview::BattleWritebackQaCopyRootForExecutable(
		std::wstring_view(executablePath.data(), length));
}
#endif

bool contains_ignore_case(const std::wstring& value,
	const std::wstring& query)
{
	if (query.empty()) return true;
	if (value.size() < query.size()) return false;
	for (std::size_t start = 0; start + query.size() <= value.size(); ++start) {
		bool matches = true;
		for (std::size_t offset = 0; offset < query.size(); ++offset) {
			if (std::towlower(value[start + offset]) !=
				std::towlower(query[offset])) {
				matches = false;
				break;
			}
		}
		if (matches) return true;
	}
	return false;
}

void draw_deployment_slot_marker(Gdiplus::Graphics& graphics,
	float centerX, float baseY, float cell, int deploymentIndex,
	float opacity, bool hidden)
{
	const float visibility = opacity * (hidden ? 0.62f : 1.0f);
	const auto alpha = [visibility](int value) {
		return static_cast<BYTE>((std::min)(255,
			(std::max)(0, static_cast<int>(value * visibility))));
	};
	const float markerWidth = (std::max)(24.0f, cell * 0.80f);
	const float markerHeight = (std::max)(16.0f, cell * 0.42f);
	const Gdiplus::RectF markerRect(centerX - markerWidth * 0.5f,
		baseY - markerHeight - cell * 0.12f, markerWidth, markerHeight);
	Gdiplus::SolidBrush markerFill(Gdiplus::Color(
		alpha(232), 54, 61, 68));
	Gdiplus::Pen markerBorder(Gdiplus::Color(
		alpha(245), 210, 216, 220), (std::max)(1.0f, cell * 0.025f));
	graphics.FillEllipse(&markerFill, markerRect);
	graphics.DrawEllipse(&markerBorder, markerRect);

	// Keep the native zero-based slot visible; it is not a Data ID.
	const std::wstring markerText = L"#" + std::to_wstring(deploymentIndex);
	Gdiplus::Font markerFont(L"Microsoft YaHei",
		(std::max)(8.0f, cell * 0.21f),
		Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::StringFormat markerFormat;
	markerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
	markerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	Gdiplus::SolidBrush markerTextBrush(Gdiplus::Color(
		alpha(255), 245, 245, 235));
	graphics.DrawString(markerText.c_str(), -1, &markerFont,
		markerRect, &markerFormat, &markerTextBrush);
}

bool in_scope(const ccz::preview::LogicalNodeKey& key,
	const ccz::preview::LogicalNodeKey& scope)
{
	if (key.empty() || scope.empty()) return false;
	if (key == scope) return true;
	if (key.size() <= scope.size() || key.compare(0, scope.size(), scope) != 0)
		return false;
	const wchar_t separator = key[scope.size()];
	return separator == L'|' || separator == L'/';
}

class BattlePreviewRefreshSuppression {
public:
	explicit BattlePreviewRefreshSuppression(CcczEditor2View& view)
		: view_(view), previous_(view.m_suppressBattleWritebackPreview)
	{
		view_.m_suppressBattleWritebackPreview = true;
		view_.m_suppressPreviewRefreshOnce = false;
	}
	~BattlePreviewRefreshSuppression()
	{
		view_.m_suppressPreviewRefreshOnce = false;
		view_.m_suppressBattleWritebackPreview = previous_;
	}
	BattlePreviewRefreshSuppression(const BattlePreviewRefreshSuppression&) = delete;
	BattlePreviewRefreshSuppression& operator=(
		const BattlePreviewRefreshSuppression&) = delete;

private:
	CcczEditor2View& view_;
	bool previous_{};
};

class MfcBattleWritebackNativeHost final
	: public ccz::editor_preview::BattleWritebackNativeHost {
public:
	MfcBattleWritebackNativeHost(CcczEditor2View& view,
		HTREEITEM revision_section)
		: view_(view), document_(view.GetDocument()), tree_(view.GetTreeCtrl()),
		  revision_section_(revision_section)
	{
	}

	bool CaptureDocumentState(
		ccz::editor_preview::BattleWritebackNativeDocumentState& state) const override
	{
		state = {};
		if (document_ == nullptr || document_->pathName == nullptr ||
			!NodeExists(reinterpret_cast<std::uintptr_t>(revision_section_)))
			return false;
		try {
			ccz::editor_preview::CapturedEditorTree snapshot;
			std::wstring diagnostic;
			if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree_,
				revision_section_, std::wstring(document_->pathName), snapshot,
				diagnostic)) return false;
			const auto capture =
				ccz::editor_preview::BuildBattlePreviewInput(snapshot);
			if (!capture.ok) return false;
			// cur_code_ord is global to the native document.  A stale counter may
			// otherwise assign a duplicate ordinal to a newly inserted instruction,
			// corrupting later save/index operations.  Gaps are safe; collisions are
			// not, so only accept a counter strictly beyond every existing node.
			if (!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
				snapshot, view_.cur_code_ord, diagnostic)) return false;

			state.selected_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetSelectedItem());
			state.next_ordinal = view_.cur_code_ord;
			state.custom_modified = document_->modified;
			state.framework_modified = document_->IsModified() != FALSE;
			state.document_revision = capture.input.document_revision;
			return state.next_ordinal >= 0 && state.document_revision != 0;
		} catch (...) {
			state = {};
			return false;
		}
	}

	bool CaptureNode(std::uintptr_t handle,
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot& node) const override
	{
		node = {};
		if (!NodeExists(handle)) return false;
		const HTREEITEM item = reinterpret_cast<HTREEITEM>(handle);
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(item));
		if (data == nullptr) return false;
		try {
			node.handle = handle;
			node.parent_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetParentItem(item));
			node.previous_sibling_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetPrevSiblingItem(item));
			node.next_sibling_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetNextSiblingItem(item));
			node.instruction_id = static_cast<std::uint16_t>(data->id);
			node.ordinal = data->ord;
			node.int_data.assign(data->int_data.begin(), data->int_data.end());
			node.has_children = tree_.GetChildItem(item) != nullptr;
			node.has_long_text = data->long_char_data != nullptr ||
				data->long_char_data_capacity != 0;
			node.has_structural_links = data->child != nullptr || data->bro != nullptr;
			return true;
		} catch (...) {
			node = {};
			return false;
		}
	}

	bool NodeExists(std::uintptr_t handle) const override
	{
		if (handle == 0 || tree_.GetSafeHwnd() == nullptr) return false;
		TVITEM item{};
		item.mask = TVIF_HANDLE | TVIF_PARAM;
		item.hItem = reinterpret_cast<HTREEITEM>(handle);
		return TreeView_GetItem(tree_.GetSafeHwnd(), &item) != FALSE &&
			item.lParam != 0;
	}

	bool SwapNodeIntDataAndRefresh(std::uintptr_t handle,
		std::vector<std::int32_t>& int_data) override
	{
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot node;
		if (!CaptureNode(handle, node) || node.has_children ||
			node.has_long_text || node.has_structural_links ||
			node.int_data.size() != int_data.size()) return false;
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(
			reinterpret_cast<HTREEITEM>(handle)));
		if (data == nullptr) return false;
		data->int_data.swap(int_data);
		try {
			view_.UpdateShow(reinterpret_cast<HTREEITEM>(handle));
		} catch (...) {
			data->int_data.swap(int_data);
			try {
				view_.UpdateShow(reinterpret_cast<HTREEITEM>(handle));
			} catch (...) {
				// Keep the original native fields even if display regeneration also
				// fails; a later normal tree refresh can reconstruct the label.
			}
			return false;
		}
		return true;
	}

	bool InsertBattleNode(std::uintptr_t parent_handle,
		std::uintptr_t after_handle, std::uint16_t instruction_id,
		const std::vector<std::int32_t>& int_data, int ordinal,
		std::uintptr_t& inserted_handle) override
	{
		inserted_handle = 0;
		if (parent_handle == 0 || !NodeExists(parent_handle) ||
			ordinal != view_.cur_code_ord) return false;
		HTREEITEM after = after_handle == 0 ? TVI_FIRST
			: reinterpret_cast<HTREEITEM>(after_handle);
		if (after_handle != 0 && (!NodeExists(after_handle) ||
			tree_.GetParentItem(after) != reinterpret_cast<HTREEITEM>(parent_handle)))
			return false;
		std::vector<int> native_data;
		try {
			native_data.assign(int_data.begin(), int_data.end());
		} catch (...) {
			return false;
		}
		const HTREEITEM inserted = view_.InsertBattleInstructionAt(
			instruction_id, reinterpret_cast<HTREEITEM>(parent_handle), after,
			native_data);
		if (inserted == nullptr) return false;
		inserted_handle = reinterpret_cast<std::uintptr_t>(inserted);
		return true;
	}

	bool DeleteBattleNode(std::uintptr_t handle) override
	{
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot node;
		if (!CaptureNode(handle, node) ||
			!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node))
			return false;
		const HTREEITEM item = reinterpret_cast<HTREEITEM>(handle);
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(item));
		if (data == nullptr || !tree_.DeleteItem(item)) return false;
		ccz::editor::ReleaseLongCharData(
			data->long_char_data, data->long_char_data_capacity);
		delete data;
		return true;
	}

	bool RestoreBattleNodeBefore(
		const ccz::editor_preview::BattleWritebackNativeNodeSnapshot& node,
		std::uintptr_t before_handle,
		std::uintptr_t& restored_handle) override
	{
		restored_handle = 0;
		// Undo rollback may need to restore an inserted 0x46/0x47 page after a
		// later inverse operation fails. Keep restoration aligned with the same
		// exact deployment-node policy used by deletion.
		if (!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node) ||
			node.parent_handle == 0 ||
		!NodeExists(node.parent_handle)) return false;
		const HTREEITEM parent = reinterpret_cast<HTREEITEM>(node.parent_handle);
		HTREEITEM after = TVI_LAST;
		if (before_handle != 0) {
			if (!NodeExists(before_handle)) return false;
			const HTREEITEM before = reinterpret_cast<HTREEITEM>(before_handle);
			if (tree_.GetParentItem(before) != parent) return false;
			const HTREEITEM previous = tree_.GetPrevSiblingItem(before);
			after = previous != nullptr ? previous : TVI_FIRST;
		}

		std::unique_ptr<ItemData> data;
		try {
			data = std::make_unique<ItemData>(
				static_cast<int>(node.int_data.size()));
			data->id = node.instruction_id;
			data->ord = node.ordinal;
			data->int_data.assign(node.int_data.begin(), node.int_data.end());
		} catch (...) {
			return false;
		}
		TVINSERTSTRUCT insert{};
		insert.hParent = parent;
		insert.hInsertAfter = after;
		insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE |
			TVIF_SELECTEDIMAGE;
		insert.item.pszText = view_.code[node.instruction_id];
		insert.item.lParam = 0;
		insert.item.iImage = 0;
		insert.item.iSelectedImage = 1;
		const HTREEITEM restored = tree_.InsertItem(&insert);
		if (restored == nullptr) return false;
		if (!tree_.SetItemData(restored, reinterpret_cast<DWORD_PTR>(data.get()))) {
			tree_.DeleteItem(restored);
			return false;
		}
		try {
			view_.UpdateShow(restored);
		} catch (...) {
			tree_.DeleteItem(restored);
			return false;
		}
		data.release();
		restored_handle = reinterpret_cast<std::uintptr_t>(restored);
		return true;
	}

	bool MarkDocumentModified() override
	{
		if (document_ == nullptr) return false;
		document_->OnModified(TRUE);
		document_->SetModifiedFlag(TRUE);
		return document_->modified && document_->IsModified();
	}

	bool RestoreDocumentState(
		const ccz::editor_preview::BattleWritebackNativeDocumentState& state,
		const std::vector<ccz::editor_preview::BattleWritebackNativeHandleMapping>&
			remapped_handles) override
	{
		if (document_ == nullptr || state.next_ordinal < 0) return false;
		std::uintptr_t selected = state.selected_handle;
		for (const auto& mapping : remapped_handles) {
			if (mapping.old_handle == selected) {
				selected = mapping.new_handle;
				break;
			}
		}
		if (selected != 0 && !NodeExists(selected)) return false;

		const HTREEITEM previous_selection = tree_.GetSelectedItem();
		const HTREEITEM target = reinterpret_cast<HTREEITEM>(selected);
		{
			BattlePreviewRefreshSuppression suppress(view_);
			if (target != nullptr && !tree_.SelectItem(target)) {
				if (previous_selection != nullptr)
					tree_.SelectItem(previous_selection);
				return false;
			}
			if (target == nullptr) tree_.SelectItem(nullptr);
			view_.cur_item = target;
		}
		view_.cur_code_ord = state.next_ordinal;
		document_->OnModified(state.custom_modified ? TRUE : FALSE);
		document_->SetModifiedFlag(state.framework_modified ? TRUE : FALSE);
		return true;
	}

private:
	CcczEditor2View& view_;
	CcczEditor2Doc* document_{};
	CTreeCtrl& tree_;
	HTREEITEM revision_section_{};
};

bool native_writeback_evidence_ready(
	const ccz::editor_preview::BattleWritebackEvidence& evidence)
{
	return evidence.field_table_verified && evidence.sample_manifest_verified &&
		evidence.group_order_verified && evidence.native_reload_verified &&
		evidence.kind300_external_acceptance_verified;
}

}

BEGIN_MESSAGE_MAP(CSBattlePreviewPage, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_CAPTURECHANGED()
	ON_WM_MOUSEMOVE()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_CONTROL(EN_CHANGE, kSearchId, &CSBattlePreviewPage::OnSearchChanged)
	ON_CONTROL(LBN_SELCHANGE, kCharacterListId,
		&CSBattlePreviewPage::OnCharacterSelectionChanged)
	ON_CONTROL(CBN_SELCHANGE, kDeploymentComboId,
		&CSBattlePreviewPage::OnDeploymentSelectionChanged)
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(kCharacterTabId, &CSBattlePreviewPage::OnCharacterTab)
	ON_BN_CLICKED(kBrushTabId, &CSBattlePreviewPage::OnBrushTab)
	ON_BN_CLICKED(kHideSidebarId, &CSBattlePreviewPage::OnHideSidebar)
	ON_BN_CLICKED(kShowSidebarId, &CSBattlePreviewPage::OnShowSidebar)
	ON_BN_CLICKED(kPreviousPageId,
		&CSBattlePreviewPage::OnPreviousCharacterPage)
	ON_BN_CLICKED(kNextPageId, &CSBattlePreviewPage::OnNextCharacterPage)
	ON_BN_CLICKED(kUndoId, &CSBattlePreviewPage::OnUndo)
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	ON_BN_CLICKED(kQaWritebackId, &CSBattlePreviewPage::OnQaWriteback)
#endif
END_MESSAGE_MAP()

CSBattlePreviewPage::~CSBattlePreviewPage()
{
	ClearDocument();
}

BOOL CSBattlePreviewPage::CreatePage(CWnd* parent, UINT control_id)
{
	const CString windowClass = AfxRegisterWndClass(
		CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
	return CreateEx(0, windowClass, _T("S 战场预览"),
		WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
			WS_HSCROLL | WS_VSCROLL,
		CRect(0, 0, 1, 1), parent, control_id);
}

int CSBattlePreviewPage::OnCreate(LPCREATESTRUCT create)
{
	if (CWnd::OnCreate(create) == -1) return -1;
	ShowScrollBar(SB_HORZ, FALSE);
	ShowScrollBar(SB_VERT, FALSE);
	CreateEditorControls();
	LayoutEditorControls();
	return 0;
}

void CSBattlePreviewPage::CreateEditorControls()
{
	const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
	const DWORD controlStyle = WS_CHILD | WS_VISIBLE;
	if (!m_characterTabButton.Create(_T("角色列表"), buttonStyle,
		CRect(0, 0, 1, 1), this, kCharacterTabId) ||
		!m_brushTabButton.Create(_T("小兵笔刷"), buttonStyle,
			CRect(0, 0, 1, 1), this, kBrushTabId) ||
		!m_hideSidebarButton.Create(_T("隐藏"), buttonStyle,
			CRect(0, 0, 1, 1), this, kHideSidebarId) ||
		!m_showSidebarButton.Create(_T("角色/笔刷"), buttonStyle,
			CRect(0, 0, 1, 1), this, kShowSidebarId) ||
		!m_previousPageButton.Create(_T("上一页"), buttonStyle,
			CRect(0, 0, 1, 1), this, kPreviousPageId) ||
		!m_nextPageButton.Create(_T("下一页"), buttonStyle,
			CRect(0, 0, 1, 1), this, kNextPageId) ||
		!m_undoButton.Create(_T("撤回"), buttonStyle,
			CRect(0, 0, 1, 1), this, kUndoId) ||
		!m_searchEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
			CRect(0, 0, 1, 1), this, kSearchId) ||
		!m_deploymentCombo.Create(controlStyle | WS_VSCROLL | CBS_DROPDOWNLIST,
			CRect(0, 0, 1, 1), this, kDeploymentComboId) ||
		!m_characterList.Create(controlStyle | WS_BORDER | WS_VSCROLL |
			LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
			CRect(0, 0, 1, 1), this, kCharacterListId) ||
		!m_pageLabel.Create(_T("0 / 0"), controlStyle | SS_CENTER,
			CRect(0, 0, 1, 1), this, kPageLabelId)) {
		AppendDiagnostic(L"S 战场角色侧栏控件创建失败");
		return;
	}
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	if (!m_qaWritebackButton.Create(_T("QA写回"), buttonStyle,
		CRect(0, 0, 1, 1), this, kQaWritebackId)) {
		AppendDiagnostic(L"S 战场 QA 写回控件创建失败");
		return;
	}
#endif
	m_showSidebarButton.ShowWindow(SW_HIDE);
	if (CFont* font = GetFont()) {
		m_characterTabButton.SetFont(font);
		m_brushTabButton.SetFont(font);
		m_hideSidebarButton.SetFont(font);
		m_showSidebarButton.SetFont(font);
		m_previousPageButton.SetFont(font);
		m_nextPageButton.SetFont(font);
		m_undoButton.SetFont(font);
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
		m_qaWritebackButton.SetFont(font);
#endif
		m_searchEdit.SetFont(font);
		m_deploymentCombo.SetFont(font);
		m_characterList.SetFont(font);
		m_pageLabel.SetFont(font);
	}
	m_controlsCreated = true;
	RefreshEditorControls();
}

void CSBattlePreviewPage::LayoutEditorControls()
{
	if (GetSafeHwnd() == nullptr) return;
	CRect client;
	GetClientRect(&client);
	const int panelWidth = m_sidebarVisible
		? (std::min)(kSidebarWidth, (std::max)(0, client.Width())) : 0;
	m_canvasRect.SetRect(0, 0,
		(std::max)(0, client.Width() - panelWidth), client.Height());
	if (!m_controlsCreated) {
		UpdateViewport();
		Invalidate(FALSE);
		return;
	}
	if (m_sidebarVisible) {
		const int left = m_canvasRect.right + 4;
		const int right = (std::max)(left,
			static_cast<int>(client.right) - 4);
		const int width = right - left;
		const int tabGap = 3;
		const int roleWidth = (std::max)(48, (width - 2 * tabGap) * 36 / 100);
		const int brushWidth = (std::max)(48, (width - 2 * tabGap) * 36 / 100);
		const int hideWidth = (std::max)(32, width - roleWidth - brushWidth - 2 * tabGap);
		int y = 6;
		m_characterTabButton.MoveWindow(left, y, roleWidth, 24);
		m_brushTabButton.MoveWindow(left + roleWidth + tabGap, y,
			brushWidth, 24);
		m_hideSidebarButton.MoveWindow(left + roleWidth + tabGap + brushWidth + tabGap,
			y, hideWidth, 24);
		y += 30;
		m_searchEdit.MoveWindow(left, y, width, 22);
		y += 26;
		m_deploymentCombo.MoveWindow(left, y, width, 160);
		y += 28;
		const int bottom = client.bottom - 8;
		const int undoY = (std::max)(y, bottom - 54);
		m_characterList.MoveWindow(left, y, width, (std::max)(0, undoY - y - 3));
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
		const int qaWidth = (std::min)(96, width);
		const int undoWidth = (std::max)(0, width - qaWidth - tabGap);
		m_undoButton.MoveWindow(left, undoY, undoWidth, 24);
		m_qaWritebackButton.MoveWindow(left + undoWidth + tabGap, undoY,
			qaWidth, 24);
		m_qaWritebackButton.ShowWindow(SW_SHOW);
#else
		m_undoButton.MoveWindow(left, undoY, width, 24);
#endif
		const int pageY = bottom - 26;
		const int pageGap = 3;
		const int pageButtonWidth = (std::max)(38, (width - 2 * pageGap - 70) / 2);
		m_previousPageButton.MoveWindow(left, pageY, pageButtonWidth, 22);
		m_pageLabel.MoveWindow(left + pageButtonWidth + pageGap, pageY,
			70, 22);
		m_nextPageButton.MoveWindow(left + pageButtonWidth + pageGap + 70 + pageGap,
			pageY, (std::max)(0, width - pageButtonWidth - 70 - 2 * pageGap), 22);
		m_showSidebarButton.ShowWindow(SW_HIDE);
		for (CWnd* control : {static_cast<CWnd*>(&m_characterTabButton),
			static_cast<CWnd*>(&m_brushTabButton),
			static_cast<CWnd*>(&m_hideSidebarButton),
			static_cast<CWnd*>(&m_previousPageButton),
			static_cast<CWnd*>(&m_nextPageButton),
			static_cast<CWnd*>(&m_undoButton),
			static_cast<CWnd*>(&m_searchEdit), static_cast<CWnd*>(&m_deploymentCombo),
			static_cast<CWnd*>(&m_characterList), static_cast<CWnd*>(&m_pageLabel)}) {
			if (control->GetSafeHwnd() != nullptr) control->ShowWindow(SW_SHOW);
		}
	} else {
		for (CWnd* control : {static_cast<CWnd*>(&m_characterTabButton),
			static_cast<CWnd*>(&m_brushTabButton),
			static_cast<CWnd*>(&m_hideSidebarButton),
			static_cast<CWnd*>(&m_previousPageButton),
			static_cast<CWnd*>(&m_nextPageButton),
			static_cast<CWnd*>(&m_undoButton),
			static_cast<CWnd*>(&m_searchEdit), static_cast<CWnd*>(&m_deploymentCombo),
			static_cast<CWnd*>(&m_characterList), static_cast<CWnd*>(&m_pageLabel)}) {
			if (control->GetSafeHwnd() != nullptr) control->ShowWindow(SW_HIDE);
		}
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
		m_qaWritebackButton.ShowWindow(SW_HIDE);
#endif
		const int buttonWidth = 96;
		m_showSidebarButton.MoveWindow(
			(std::max)(0, static_cast<int>(client.right) - buttonWidth - 8), 6,
			buttonWidth, 24);
		m_showSidebarButton.ShowWindow(SW_SHOW);
	}
	UpdateViewport();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::RefreshEditorControls()
{
	if (!m_controlsCreated) return;
	m_characterTabButton.SetWindowText(m_brushTabActive
		? _T("角色列表") : _T("角色列表 •"));
	m_brushTabButton.SetWindowText(m_brushTabActive
		? _T("小兵笔刷 •") : _T("小兵笔刷"));
	RefreshCharacterList();
	RefreshDeploymentList();
	RefreshSelectedUnitIndex();
	const std::size_t pages = m_editorOps
		? m_editorOps->CharacterPageCount() : 0;
	CString pageText;
	if (m_brushTabActive) {
		pageText.Format(_T("%u 组"), static_cast<unsigned>(
			m_editorState.minion_brushes.size()));
	} else {
		const std::size_t current = pages == 0 ? 0
			: m_editorState.characters.current_page + 1;
		pageText.Format(_T("%u / %u"), static_cast<unsigned>(current),
			static_cast<unsigned>(pages));
	}
	m_pageLabel.SetWindowText(pageText);
	m_previousPageButton.EnableWindow(!m_brushTabActive &&
		m_editorState.characters.current_page > 0);
	m_nextPageButton.EnableWindow(!m_brushTabActive && pages > 0 &&
		m_editorState.characters.current_page + 1 < pages);
	m_undoButton.EnableWindow(m_nativeWritebackTransaction.undo_available() ||
		(m_editorOps && m_editorOps->CanUndo()));
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	m_qaWritebackButton.EnableWindow(m_editorOps != nullptr && IsCurrentQaCopy());
#endif
	m_deploymentCombo.EnableWindow(m_editorOps != nullptr);
	LayoutEditorControls();
}

void CSBattlePreviewPage::RefreshCharacterList()
{
	if (!m_controlsCreated) return;
	m_characterList.SetRedraw(FALSE);
	m_characterList.ResetContent();
	const std::wstring query = m_editorState.characters.query;
	if (m_brushTabActive) {
		for (const auto& brush : m_editorState.minion_brushes) {
			std::wostringstream label;
			label << brush.name << L"  (" << brush.first_data_id << L"-"
				<< brush.last_data_id << L")";
			if (!contains_ignore_case(label.str(), query)) continue;
			const int index = m_characterList.AddString(label.str().c_str());
			if (index != LB_ERR && index != LB_ERRSPACE) {
				m_characterList.SetItemData(index,
					static_cast<DWORD_PTR>(brush.first_data_id));
				if (m_editorState.selected_minion_brush &&
					m_editorState.selected_minion_brush->first_data_id ==
					brush.first_data_id) m_characterList.SetCurSel(index);
			}
		}
	} else if (m_editorOps) {
		for (const auto& character : m_editorOps->VisibleCharacters()) {
			std::wostringstream label;
			label << character.name << L"  (Data " << character.data_id << L")";
			const int index = m_characterList.AddString(label.str().c_str());
			if (index != LB_ERR && index != LB_ERRSPACE) {
				m_characterList.SetItemData(index,
					static_cast<DWORD_PTR>(character.data_id));
				if (m_editorState.characters.selected_data_id == character.data_id)
					m_characterList.SetCurSel(index);
			}
		}
	}
	m_characterList.SetRedraw(TRUE);
	m_characterList.Invalidate(FALSE);
}

void CSBattlePreviewPage::RefreshBrushList()
{
	RefreshCharacterList();
}

void CSBattlePreviewPage::RefreshDeploymentList()
{
	if (!m_controlsCreated) return;
	const std::uint16_t active = m_editorState.placement.instruction_id;
	m_deploymentCombo.ResetContent();
	const struct DeploymentChoice { LPCTSTR label; std::uint16_t instruction; } choices[] = {
		{ _T("我军出场 (0x4B)"), 0x4b },
		{ _T("友军出场 (0x46)"), 0x46 },
		{ _T("敌军出场 (0x47)"), 0x47 },
	};
	int selected = CB_ERR;
	for (const auto& choice : choices) {
		const int item = m_deploymentCombo.AddString(choice.label);
		if (item == CB_ERR || item == CB_ERRSPACE) continue;
		m_deploymentCombo.SetItemData(item, choice.instruction);
		if (choice.instruction == active) selected = item;
	}
	if (selected != CB_ERR) m_deploymentCombo.SetCurSel(selected);
}

void CSBattlePreviewPage::RefreshSelectedUnitIndex()
{
	m_selectedUnit.reset();
	if (!m_editorState.selection) return;
	for (std::size_t index = 0; index < m_editorState.battle.units.size(); ++index) {
		const auto& unit = m_editorState.battle.units[index];
		if (unit.editor_pending_delete ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
		const bool byCache = m_editorState.selection->cache_id != 0 &&
			unit.editor_cache_id == m_editorState.selection->cache_id;
		const bool bySource = m_editorState.selection->cache_id == 0 &&
			unit.source_key == m_editorState.selection->source_key &&
			unit.record_index == m_editorState.selection->record_index &&
			static_cast<std::size_t>(unit.page) == m_editorState.selection->page;
		if (byCache || bySource) {
			m_selectedUnit = index;
			return;
		}
	}
}

void CSBattlePreviewPage::BindSelectedDeployment()
{
	if (!m_editorOps) return;
	const int selected = m_deploymentCombo.GetCurSel();
	if (selected == CB_ERR) return;
	const auto result = m_editorOps->SetDeploymentInstruction(
		static_cast<std::uint16_t>(m_deploymentCombo.GetItemData(selected)));
	ShowOperationResult(result);
}

void CSBattlePreviewPage::ShowOperationResult(
	const ccz::editor_preview::BattleEditorResult& result)
{
	if (!result.diagnostic.empty()) m_diagnostic = result.diagnostic.c_str();
	RefreshSelectedUnitIndex();
	if (m_controlsCreated) RefreshEditorControls();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::StampAt(
	const ccz::preview::BattleCoord& coordinate)
{
	if (!m_editorOps || !m_leftStampActive ||
		m_editorState.mode != ccz::editor_preview::BattleEditorMode::Place ||
		m_lastStampCell == coordinate)
		return;
	m_lastStampCell = coordinate;
	const auto preview = m_editorOps->PreviewPlacement(coordinate);
	if (!preview.valid) {
		m_diagnostic = preview.diagnostic.c_str();
		Invalidate(FALSE);
		return;
	}
	ShowOperationResult(m_editorOps->Place(coordinate));
}

void CSBattlePreviewPage::SetSidebarVisible(bool visible)
{
	if (m_sidebarVisible == visible) return;
	m_sidebarVisible = visible;
	LayoutEditorControls();
}

void CSBattlePreviewPage::OnDestroy()
{
	if (m_panActive) ReleaseCapture();
	if (m_leftStampActive) ReleaseCapture();
	m_panActive = false;
	m_leftStampActive = false;
	ClearDocument();
	CWnd::OnDestroy();
}

void CSBattlePreviewPage::RequestPreviewRefresh(
	CTreeCtrl& tree, HTREEITEM selectedItem, const wchar_t* documentPath)
{
	ClearDocument();
	m_documentPath = documentPath ? documentPath : L"";
	ccz::editor_preview::CapturedEditorTree snapshot;
	std::wstring diagnostic;
	if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree, selectedItem,
			std::wstring(m_documentPath.GetString()), snapshot, diagnostic)) {
		AppendDiagnostic(diagnostic.empty()
			? L"无法读取 S 剧本结构" : diagnostic);
		Invalidate(FALSE);
		return;
	}
	LoadCapturedBattle(snapshot);
	Invalidate(FALSE);
}

bool CSBattlePreviewPage::BuildPendingWritebackPlan(
	const ccz::editor_preview::CapturedEditorTree& fresh_snapshot,
	ccz::editor_preview::BattleWritebackPlanBuild& plan,
	std::wstring& diagnostic,
	ccz::editor_preview::BattleWritebackNativeBatch* native_batch) const
{
	plan = {};
	diagnostic.clear();
	if (native_batch != nullptr) *native_batch = {};
	if (!m_writebackBaselineCapture.ok) {
		diagnostic = L"战场写回预检失败：当前 S 布置缓存没有有效加载基线";
		return false;
	}

	const auto fresh_capture =
		ccz::editor_preview::BuildBattlePreviewInput(fresh_snapshot);
	if (!fresh_capture.ok) {
		diagnostic = fresh_capture.diagnostic.empty()
			? L"战场写回预检失败：无法重新捕获当前剧本树"
			: fresh_capture.diagnostic;
		return false;
	}

	ccz::editor_preview::BattleWritebackSectionSnapshot section_snapshot;
	if (!ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
		m_writebackBaselineCapture, fresh_capture, section_snapshot,
		diagnostic)) {
		return false;
	}

	plan = ccz::editor_preview::BuildBattleWritebackPlan(
		m_editorState.battle, m_writebackBaselineCapture.input.scope_key,
		section_snapshot);
	diagnostic = plan.diagnostic;
	if (!plan.ok) return false;
	if (native_batch != nullptr) {
		*native_batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
			plan, fresh_capture, m_writebackBaselineCapture.input.scope_key);
		if (!native_batch->ok) {
			diagnostic = native_batch->diagnostic;
			return false;
		}
	}
	return true;
}

bool CSBattlePreviewPage::ApplyPendingNativeWritebackForEvidence(
	CcczEditor2View& view, HTREEITEM selectedItem,
	const ccz::editor_preview::BattleWritebackEvidence& evidence,
	std::wstring& diagnostic)
{
	diagnostic.clear();
	if (!native_writeback_evidence_ready(evidence)) {
		diagnostic = L"S 战场原生写回仍关闭：字段表、样本顺序、原生重载和外部接受性证据尚未全部闭合";
		return false;
	}
	return ApplyPendingNativeWritebackTransaction(view, selectedItem,
		diagnostic, false);
}

bool CSBattlePreviewPage::ApplyPendingNativeWritebackTransaction(
	CcczEditor2View& view, HTREEITEM selectedItem,
	std::wstring& diagnostic, bool requireNonemptyBatch)
{
	diagnostic.clear();
	CcczEditor2Doc* document = view.GetDocument();
	if (document == nullptr || document->pathName == nullptr ||
		m_documentPath.IsEmpty() ||
		CString(document->pathName).CompareNoCase(m_documentPath) != 0) {
		diagnostic = L"S 战场原生写回拒绝：预览与活动剧本文档不一致";
		return false;
	}
	CTreeCtrl& tree = view.GetTreeCtrl();
	ccz::editor_preview::CapturedEditorTree snapshot;
	if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree, selectedItem,
		std::wstring(document->pathName), snapshot, diagnostic)) {
		if (diagnostic.empty()) diagnostic = L"S 战场原生写回拒绝：无法捕获活动剧本";
		return false;
	}
	const auto capture =
		ccz::editor_preview::BuildBattlePreviewInput(snapshot);
	if (!capture.ok ||
		capture.input.document_key != m_writebackBaselineCapture.input.document_key ||
		capture.input.scope_key != m_writebackBaselineCapture.input.scope_key) {
		diagnostic = capture.diagnostic.empty()
			? L"S 战场原生写回拒绝：活动文档或 Section 已偏离布置缓存基线"
			: capture.diagnostic;
		return false;
	}

	ccz::editor_preview::BattleWritebackPlanBuild plan;
	ccz::editor_preview::BattleWritebackNativeBatch batch;
	if (!BuildPendingWritebackPlan(snapshot, plan, diagnostic, &batch))
		return false;
	if (requireNonemptyBatch && batch.changes.empty()) {
		diagnostic = L"S 战场写回预检通过，但当前没有需要提交的布阵改动";
		return false;
	}
	const HTREEITEM revisionSection = reinterpret_cast<HTREEITEM>(
		m_writebackBaselineCapture.snapshot.section_root.runtime_handle);
	MfcBattleWritebackNativeHost host(view, revisionSection);
	BattlePreviewRefreshSuppression suppress(view);
	const bool applied = m_nativeWritebackTransaction.Apply(
		batch, host, diagnostic);
	RefreshEditorControls();
	Invalidate(FALSE);
	return applied;
}

#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
bool CSBattlePreviewPage::IsCurrentQaCopy() const
{
	const std::wstring root = battle_writeback_qa_copy_root();
	return !root.empty() &&
		ccz::editor_preview::IsBattleWritebackQaCopy(
			std::wstring(m_documentPath.GetString()), root);
}

bool CSBattlePreviewPage::ApplyPendingNativeWritebackForQaCopy(
	CcczEditor2View& view, std::wstring& diagnostic)
{
	diagnostic.clear();
	if (!IsCurrentQaCopy()) {
		diagnostic = L"QA 写回拒绝：活动剧本不是可执行文件旁标记的 S 副本";
		return false;
	}
	if (!m_writebackBaselineCapture.ok ||
		m_writebackBaselineCapture.snapshot.section_root.runtime_handle == 0) {
		diagnostic = L"QA 写回拒绝：当前没有有效的 S Section 布置基线";
		return false;
	}
	const HTREEITEM section = reinterpret_cast<HTREEITEM>(
		m_writebackBaselineCapture.snapshot.section_root.runtime_handle);
	return ApplyPendingNativeWritebackTransaction(view, section, diagnostic, true);
}
#endif

bool CSBattlePreviewPage::UndoNativeWriteback(
	CcczEditor2View& view, std::wstring& diagnostic)
{
	diagnostic.clear();
	if (!m_nativeWritebackTransaction.undo_available()) {
		diagnostic = L"当前没有可撤回的 S 战场原生写回事务";
		return false;
	}
	CcczEditor2Doc* document = view.GetDocument();
	if (document == nullptr || document->pathName == nullptr ||
		CString(document->pathName).CompareNoCase(m_documentPath) != 0) {
		diagnostic = L"S 战场撤回拒绝：活动剧本文档与写回事务不一致";
		return false;
	}
	const HTREEITEM revisionSection = reinterpret_cast<HTREEITEM>(
		m_writebackBaselineCapture.snapshot.section_root.runtime_handle);
	MfcBattleWritebackNativeHost host(view, revisionSection);
	BattlePreviewRefreshSuppression suppress(view);
	const bool undone = m_nativeWritebackTransaction.Undo(host, diagnostic);
	RefreshEditorControls();
	Invalidate(FALSE);
	return undone;
}

void CSBattlePreviewPage::ClearDocument()
{
	if (m_panActive && GetSafeHwnd() != nullptr) ReleaseCapture();
	m_panActive = false;
	m_panStartPoint = CPoint(-1, -1);
	m_zoom = kDefaultZoom;
	m_scrollX = m_scrollY = 0;
	m_selectedUnit.reset();
	m_hoverCell.reset();
	m_lastStampCell.reset();
	m_writebackBaselineCapture = {};
	m_nativeWritebackTransaction.Clear();
	m_editorState = {};
	m_editorOps.reset();
	m_assets.Clear();
	m_documentPath.Empty();
	m_diagnostic.Empty();
	m_cachedMapId = -1;
	delete m_mapBitmap;
	m_mapBitmap = nullptr;
	ClearUnitSprites();
	if (m_controlsCreated) RefreshEditorControls();
	UpdateViewport();
	if (GetSafeHwnd() != nullptr) Invalidate(FALSE);
}

void CSBattlePreviewPage::LoadCapturedBattle(
	const ccz::editor_preview::CapturedEditorTree& snapshot)
{
	const auto capture =
		ccz::editor_preview::BuildBattlePreviewInput(snapshot);
	if (!capture.ok) {
		AppendDiagnostic(capture.diagnostic);
		return;
	}
	// Keep the exact S-section/document snapshot used to build the placement
	// cache. A future native writeback must compare against a fresh capture;
	// it must never infer source nodes from the mutable placement cache.
	m_writebackBaselineCapture = capture;
	ccz::preview::BattleParseOptions options;
	options.scope_root_key = capture.input.scope_key;
	const auto parsed = ccz::preview::parse_battle_instructions(
		capture.input.document_path, capture.input.nodes, options);
	m_editorState = {};
	m_editorState.battle = parsed.state;
	m_editorState.scope_root = capture.input.scope_key;
	m_editorState.characters.page_size = 20;
	for (auto& unit : m_editorState.battle.units) {
		if (!unit.fields_decoded ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
		unit.editor_deployment_instruction = unit.source_instruction;
		unit.editor_origin_fields = unit.raw_fields;
	}
	if (!parsed.field_table_complete)
		AppendDiagnostic(L"部分战场单位字段尚未解码");
	for (const auto& item : parsed.diagnostics) AppendDiagnostic(item);

	std::wstring assetsDiagnostic;
	m_assets.LoadForDocument(capture.input.document_path,
		m_editorState.battle.map.map_id, &assetsDiagnostic);
	AppendDiagnostic(assetsDiagnostic);
	const auto catalog = ccz::editor_preview::ParseBattleCharacterCatalog(
		m_assets.data_e5());
	if (catalog.ok) {
		m_editorState.characters.entries = catalog.characters;
		m_editorState.minion_brushes = catalog.brushes;
	} else {
		AppendDiagnostic(catalog.diagnostic);
	}
	const bool mapLoaded = LoadMapImage();
	std::vector<std::uint8_t> decodedTerrain;
	if (!mapLoaded || !m_editorState.battle.map.projection_valid ||
		!ccz::preview::decode_battle_hexzmap(
			m_assets.hexzmap_e5(), m_editorState.battle.map.map_id,
			m_editorState.battle.map.width, m_editorState.battle.map.height,
			decodedTerrain)) {
		m_editorState.battle.terrain.clear();
		AppendDiagnostic(L"Hexzmap 地形未加载或与战场 JPG 网格不匹配");
	} else {
		m_editorState.battle.terrain.swap(decodedTerrain);
	}
	m_editorOps = std::make_unique<ccz::editor_preview::BattleEditorOps>(
		m_editorState);
	UpdateViewport();
	RefreshEditorControls();
}

bool CSBattlePreviewPage::LoadMapImage()
{
	if (!m_editorState.battle.map.valid || m_editorState.battle.map.map_id < 0 ||
		m_assets.map_jpg_path().empty()) {
		AppendDiagnostic(L"S 战场 JPG 不可用");
		return false;
	}
	if (m_cachedMapId == m_editorState.battle.map.map_id && m_mapBitmap != nullptr) {
		const int width = static_cast<int>(m_mapBitmap->GetWidth());
		const int height = static_cast<int>(m_mapBitmap->GetHeight());
		if (width < kCellPixels || height < kCellPixels ||
			width % kCellPixels != 0 || height % kCellPixels != 0) {
			m_editorState.battle.map.projection_valid = false;
			return true;
		}
		m_editorState.battle.map.width = width / kCellPixels;
		m_editorState.battle.map.height = height / kCellPixels;
		m_editorState.battle.map.offset_x = m_editorState.battle.map.offset_y = 0;
		m_editorState.battle.map.projection_valid = true;
		return true;
	}
	Gdiplus::Bitmap* source = Gdiplus::Bitmap::FromFile(
		m_assets.map_jpg_path().c_str(), FALSE);
	if (source == nullptr || source->GetLastStatus() != Gdiplus::Ok) {
		delete source;
		AppendDiagnostic(L"S 战场 JPG 解码失败");
		return false;
	}
	const Gdiplus::Rect bounds(0, 0,
		static_cast<INT>(source->GetWidth()),
		static_cast<INT>(source->GetHeight()));
	Gdiplus::Bitmap* loaded = source->Clone(bounds, PixelFormat32bppARGB);
	delete source;
	if (loaded == nullptr || loaded->GetLastStatus() != Gdiplus::Ok) {
		delete loaded;
		AppendDiagnostic(L"S 战场 JPG 像素格式转换失败");
		return false;
	}
	delete m_mapBitmap;
	m_mapBitmap = loaded;
	m_cachedMapId = m_editorState.battle.map.map_id;
	const int width = static_cast<int>(loaded->GetWidth());
	const int height = static_cast<int>(loaded->GetHeight());
	if (width < kCellPixels || height < kCellPixels ||
		width % kCellPixels != 0 || height % kCellPixels != 0) {
		m_editorState.battle.map.projection_valid = false;
		AppendDiagnostic(L"JPG 尺寸无法证明 48 像素逻辑格投影");
		return true;
	}
	m_editorState.battle.map.width = width / kCellPixels;
	m_editorState.battle.map.height = height / kCellPixels;
	m_editorState.battle.map.offset_x = m_editorState.battle.map.offset_y = 0;
	m_editorState.battle.map.projection_valid = true;
	return true;
}

void CSBattlePreviewPage::AppendDiagnostic(const std::wstring& value)
{
	if (value.empty()) return;
	if (!m_diagnostic.IsEmpty()) m_diagnostic += _T(" | ");
	m_diagnostic += value.c_str();
}

void CSBattlePreviewPage::ClearUnitSprites()
{
	for (auto& item : m_unitSprites) delete item.second;
	m_unitSprites.clear();
	m_unitSpritePath.clear();
}

Gdiplus::Bitmap* CSBattlePreviewPage::GetUnitSprite(
	const ccz::preview::BattleUnit& unit, int rawDirection,
	ccz::editor_preview::BattleAssetCache::UnitSpriteRef* spriteRef)
{
	if (spriteRef == nullptr) return nullptr;
	*spriteRef = {};
	if (!m_assets.ResolveUnitSprite(unit.data_id, unit.unit_type,
			rawDirection, spriteRef)) return nullptr;
	if (m_unitSpritePath != m_assets.unit_mov_path()) {
		ClearUnitSprites();
		m_unitSpritePath = m_assets.unit_mov_path();
	}
	const int key = spriteRef->resource_index * 16 + spriteRef->frame;
	const auto found = m_unitSprites.find(key);
	if (found != m_unitSprites.end()) return found->second;
	if (m_unitSpritePath.empty()) {
		m_unitSprites[key] = nullptr;
		return nullptr;
	}
	CString path(m_unitSpritePath.c_str());
	CString diagnostic;
	Gdiplus::Bitmap* sheet = LoadBitmapFromE5(
		path, spriteRef->resource_index, diagnostic, false);
	if (sheet == nullptr || sheet->GetLastStatus() != Gdiplus::Ok) {
		delete sheet;
		m_unitSprites[key] = nullptr;
		if (!diagnostic.IsEmpty())
			AppendDiagnostic(std::wstring(L"Unit_mov 读取失败：") +
				diagnostic.GetString());
		return nullptr;
	}
	const int frameCount = static_cast<int>(sheet->GetHeight()) / kCellPixels;
	if (sheet->GetWidth() < kCellPixels || spriteRef->frame < 0 ||
		spriteRef->frame >= frameCount) {
		delete sheet;
		m_unitSprites[key] = nullptr;
		AppendDiagnostic(L"Unit_mov 帧尺寸无效");
		return nullptr;
	}
	auto* frame = new Gdiplus::Bitmap(
		kCellPixels, kCellPixels, PixelFormat32bppARGB);
	if (frame->GetLastStatus() != Gdiplus::Ok) {
		delete frame;
		delete sheet;
		m_unitSprites[key] = nullptr;
		return nullptr;
	}
	Gdiplus::Graphics frameGraphics(frame);
	frameGraphics.Clear(Gdiplus::Color(0, 0, 0, 0));
	frameGraphics.DrawImage(sheet,
		Gdiplus::Rect(0, 0, kCellPixels, kCellPixels), 0,
		spriteRef->frame * kCellPixels, kCellPixels, kCellPixels,
		Gdiplus::UnitPixel);
	delete sheet;
	ApplyMagentaTransparency(frame);
	m_unitSprites[key] = frame;
	return frame;
}

void CSBattlePreviewPage::ApplyMagentaTransparency(Gdiplus::Bitmap* bitmap)
{
	if (bitmap == nullptr || bitmap->GetWidth() == 0 ||
		bitmap->GetHeight() == 0) return;
	Gdiplus::BitmapData data{};
	const Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap->GetWidth()),
		static_cast<INT>(bitmap->GetHeight()));
	if (bitmap->LockBits(&rect,
			Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
			PixelFormat32bppARGB, &data) != Gdiplus::Ok) return;
	for (int y = 0; y < rect.Height; ++y) {
		auto* row = reinterpret_cast<DWORD*>(
			reinterpret_cast<BYTE*>(data.Scan0) + y * data.Stride);
		for (int x = 0; x < rect.Width; ++x) {
			const DWORD pixel = row[x];
			const BYTE alpha = static_cast<BYTE>((pixel >> 24) & 0xff);
			const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xff);
			const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xff);
			const BYTE blue = static_cast<BYTE>(pixel & 0xff);
			if (alpha && red >= 242 && green <= 16 && blue >= 230)
				row[x] = 0;
		}
	}
	bitmap->UnlockBits(&data);
}

void CSBattlePreviewPage::UpdateViewport()
{
	if (GetSafeHwnd() == nullptr || m_viewportUpdating) return;
	m_viewportUpdating = true;
	const int imageWidth = m_mapBitmap
		? static_cast<int>(m_mapBitmap->GetWidth())
		: m_editorState.battle.map.width * kCellPixels;
	const int imageHeight = m_mapBitmap
		? static_cast<int>(m_mapBitmap->GetHeight())
		: m_editorState.battle.map.height * kCellPixels;
	const int contentWidth = static_cast<int>(std::lround(imageWidth * m_zoom));
	const int contentHeight = static_cast<int>(std::lround(imageHeight * m_zoom));
	if (imageWidth <= 0 || imageHeight <= 0) {
		m_scrollX = m_scrollY = 0;
		ShowScrollBar(SB_HORZ, FALSE);
		ShowScrollBar(SB_VERT, FALSE);
		m_viewportUpdating = false;
		return;
	}
	for (int pass = 0; pass < 3; ++pass) {
		CRect client;
		GetClientRect(&client);
		const int availableWidth = (std::max)(0, client.Width() -
			(m_sidebarVisible ? kSidebarWidth : 0));
		ShowScrollBar(SB_HORZ, contentWidth > availableWidth);
		ShowScrollBar(SB_VERT, contentHeight > client.Height());
	}
	// Showing a native scrollbar changes the non-client size. Reposition the
	// child sidebar once more so it remains outside the map viewport.
	LayoutEditorControls();
	ClampViewport();
	CRect client;
	GetClientRect(&client);
	SCROLLINFO horizontal{};
	horizontal.cbSize = sizeof(horizontal);
	horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	horizontal.nMin = 0;
	horizontal.nMax = (std::max)(0, contentWidth - 1);
	horizontal.nPage = static_cast<UINT>((std::max)(1, m_canvasRect.Width()));
	horizontal.nPos = m_scrollX;
	SetScrollInfo(SB_HORZ, &horizontal, TRUE);
	SCROLLINFO vertical{};
	vertical.cbSize = sizeof(vertical);
	vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	vertical.nMin = 0;
	vertical.nMax = (std::max)(0, contentHeight - 1);
	vertical.nPage = static_cast<UINT>((std::max)(1, m_canvasRect.Height()));
	vertical.nPos = m_scrollY;
	SetScrollInfo(SB_VERT, &vertical, TRUE);
	m_viewportUpdating = false;
}

void CSBattlePreviewPage::ClampViewport()
{
	const int width = m_mapBitmap ? static_cast<int>(m_mapBitmap->GetWidth())
		: m_editorState.battle.map.width * kCellPixels;
	const int height = m_mapBitmap ? static_cast<int>(m_mapBitmap->GetHeight())
		: m_editorState.battle.map.height * kCellPixels;
	m_scrollX = (std::max)(0, (std::min)(m_scrollX,
		(std::max)(0, static_cast<int>(width * m_zoom) - m_canvasRect.Width())));
	m_scrollY = (std::max)(0, (std::min)(m_scrollY,
		(std::max)(0, static_cast<int>(height * m_zoom) - m_canvasRect.Height())));
}

void CSBattlePreviewPage::SetViewportScroll(int x, int y, bool invalidate)
{
	m_scrollX = x;
	m_scrollY = y;
	UpdateViewport();
	if (invalidate) Invalidate(FALSE);
}

void CSBattlePreviewPage::ZoomAt(CPoint point, float zoom)
{
	const float next = (std::max)(kMinZoom, (std::min)(kMaxZoom, zoom));
	if (std::abs(next - m_zoom) < 0.001f) return;
	const int localX = point.x - m_canvasRect.left;
	const int localY = point.y - m_canvasRect.top;
	const float logicalX = (localX + m_scrollX) / m_zoom;
	const float logicalY = (localY + m_scrollY) / m_zoom;
	m_zoom = next;
	m_scrollX = static_cast<int>(std::lround(logicalX * next - localX));
	m_scrollY = static_cast<int>(std::lround(logicalY * next - localY));
	UpdateViewport();
	Invalidate(FALSE);
}

std::optional<ccz::preview::BattleCoord>
CSBattlePreviewPage::BattleCoordFromScreen(CPoint point) const
{
	if (m_mapBitmap == nullptr || !m_editorState.battle.map.projection_valid ||
		m_zoom <= 0 || !m_canvasRect.PtInRect(point))
		return std::nullopt;
	const float x = static_cast<float>(point.x - m_canvasRect.left + m_scrollX);
	const float y = static_cast<float>(point.y - m_canvasRect.top + m_scrollY);
	if (x < 0 || y < 0 || x >= m_mapBitmap->GetWidth() * m_zoom ||
		y >= m_mapBitmap->GetHeight() * m_zoom) return std::nullopt;
	const ccz::preview::BattleCoord coordinate{
		static_cast<int>(std::floor(x / (kCellPixels * m_zoom))),
		static_cast<int>(std::floor(y / (kCellPixels * m_zoom)))};
	return ccz::preview::is_battle_coord_in_bounds(coordinate,
		m_editorState.battle.map.width, m_editorState.battle.map.height)
		? std::optional<ccz::preview::BattleCoord>(coordinate)
		: std::nullopt;
}

std::optional<std::size_t> CSBattlePreviewPage::HitTestUnit(
	const ccz::preview::BattleCoord& coordinate) const
{
	std::optional<std::size_t> hit;
	for (std::size_t index = 0;
		index < m_editorState.battle.units.size(); ++index) {
		const auto& unit = m_editorState.battle.units[index];
		if (!unit.fields_decoded || unit.editor_pending_delete ||
			(!unit.editor_staged_new &&
			 !in_scope(unit.source_key, m_editorState.scope_root)) ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit) ||
			unit.position != coordinate) continue;
		if (hit) return std::nullopt;
		hit = index;
	}
	return hit;
}

void CSBattlePreviewPage::DrawCanvas(
	Gdiplus::Graphics& graphics, int width, int height)
{
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	graphics.SetClip(Gdiplus::Rect(0, 0, width, height),
		Gdiplus::CombineModeReplace);
	Gdiplus::SolidBrush background(Gdiplus::Color(255, 32, 32, 32));
	graphics.FillRectangle(&background, 0, 0, width, height);
	const int ox = m_canvasRect.left - m_scrollX;
	const int oy = m_canvasRect.top - m_scrollY;
	const float cell = kCellPixels * m_zoom;
	const bool projection = m_mapBitmap &&
		m_editorState.battle.map.projection_valid &&
		m_editorState.battle.map.width > 0 &&
		m_editorState.battle.map.height > 0;
	graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
		m_canvasRect.Width(), m_canvasRect.Height()), Gdiplus::CombineModeReplace);
	if (m_mapBitmap) {
		graphics.DrawImage(m_mapBitmap,
			Gdiplus::Rect(ox, oy,
				static_cast<int>(std::lround(m_mapBitmap->GetWidth() * m_zoom)),
				static_cast<int>(std::lround(m_mapBitmap->GetHeight() * m_zoom))),
			0, 0, static_cast<INT>(m_mapBitmap->GetWidth()),
			static_cast<INT>(m_mapBitmap->GetHeight()), Gdiplus::UnitPixel);
	}
	if (projection) {
		Gdiplus::Pen grid(Gdiplus::Color(105, 255, 255, 255), 1.0f);
		for (int x = 0; x <= m_editorState.battle.map.width; ++x) {
			const float px = ox + x * cell;
			graphics.DrawLine(&grid, px, static_cast<float>(oy), px,
				oy + m_editorState.battle.map.height * cell);
		}
		for (int y = 0; y <= m_editorState.battle.map.height; ++y) {
			const float py = oy + y * cell;
			graphics.DrawLine(&grid, static_cast<float>(ox), py,
				ox + m_editorState.battle.map.width * cell, py);
		}
		if (m_hoverCell) {
			Gdiplus::SolidBrush hover(Gdiplus::Color(60, 255, 235, 70));
			graphics.FillRectangle(&hover, ox + m_hoverCell->x * cell,
				oy + m_hoverCell->y * cell, cell, cell);
		}
	}
	int decoded = 0;
	if (projection) for (std::size_t i = 0;
		i < m_editorState.battle.units.size(); ++i) {
		const auto& unit = m_editorState.battle.units[i];
		if (!unit.fields_decoded || unit.editor_pending_delete ||
			(!unit.editor_staged_new &&
			 !in_scope(unit.source_key, m_editorState.scope_root)) ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit) ||
			(m_editorState.mode ==
				ccz::editor_preview::BattleEditorMode::AdjustPosition &&
			 unit.editor_cache_id == m_editorState.adjusting_cache_id) ||
			!ccz::preview::is_battle_coord_in_bounds(
				unit.position, m_editorState.battle.map.width,
				m_editorState.battle.map.height)) continue;
		++decoded;
		const float cx = ox + (unit.position.x + 0.5f) * cell;
		const float by = oy + (unit.position.y + 1.0f) * cell;
		const int size = (std::max)(8,
			static_cast<int>(std::lround(kCellPixels * m_zoom)));
		const int dx = static_cast<int>(std::lround(cx)) - size / 2;
		const int dy = static_cast<int>(std::lround(by)) - size;
		ccz::editor_preview::BattleAssetCache::UnitSpriteRef spriteRef;
		Gdiplus::Bitmap* sprite = GetUnitSprite(
			unit, unit.direction, &spriteRef);
		if (sprite && sprite->GetLastStatus() == Gdiplus::Ok) {
			Gdiplus::ColorMatrix matrix = {
				1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
				0,0,0,unit.hidden ? 0.42f : 1.0f,0, 0,0,0,0,1};
			Gdiplus::ImageAttributes attributes;
			attributes.SetColorMatrix(&matrix);
			if (spriteRef.flip_x) {
				Gdiplus::Matrix oldTransform;
				graphics.GetTransform(&oldTransform);
				Gdiplus::Matrix flip;
				flip.Translate(static_cast<float>(dx + size),
					static_cast<float>(dy));
				flip.Scale(-1.0f, 1.0f);
				graphics.SetTransform(&flip);
				graphics.DrawImage(sprite, Gdiplus::Rect(0, 0, size, size),
					0, 0, 48, 48, Gdiplus::UnitPixel, &attributes);
				graphics.SetTransform(&oldTransform);
			} else {
				graphics.DrawImage(sprite, Gdiplus::Rect(dx, dy, size, size),
					0, 0, 48, 48, Gdiplus::UnitPixel, &attributes);
			}
			} else {
				if (ccz::preview::is_battle_deployment_slot_marker(unit)) {
					draw_deployment_slot_marker(graphics, cx, by, cell,
						unit.deployment_index, 1.0f, unit.hidden);
				} else {
					const int radius = (std::max)(5,
						static_cast<int>(cell * 0.28f));
					Gdiplus::SolidBrush unitBrush(Gdiplus::Color(
						unit.hidden ? 105 : 235, 35, 110, 225));
					graphics.FillEllipse(&unitBrush,
						static_cast<int>(cx) - radius,
						static_cast<int>(by) - radius * 2,
						radius * 2, radius * 2);
				}
			}
		if (m_selectedUnit == i) {
			Gdiplus::Pen selected(Gdiplus::Color(255,255,220,40), 2.0f);
			graphics.DrawRectangle(&selected,
				static_cast<int>(cx-cell*0.46f),
				static_cast<int>(by-cell*0.96f),
				(std::max)(1, static_cast<int>(cell*0.92f)),
				(std::max)(1, static_cast<int>(cell*0.92f)));
		}
	}
	if (m_editorOps && m_hoverCell &&
		(m_editorState.mode == ccz::editor_preview::BattleEditorMode::Place ||
		 m_editorState.mode ==
			ccz::editor_preview::BattleEditorMode::AdjustPosition)) {
		const auto placement = m_editorState.mode ==
			ccz::editor_preview::BattleEditorMode::Place
			? m_editorOps->PreviewPlacement(*m_hoverCell)
			: m_editorOps->PreviewPositionAdjustment(*m_hoverCell);
		const bool adjusting = m_editorState.mode ==
			ccz::editor_preview::BattleEditorMode::AdjustPosition;
		if (placement.valid || (adjusting && placement.unit.fields_decoded)) {
			const float centerX = ox + (m_hoverCell->x + 0.5f) * cell;
			const float baseY = oy + (m_hoverCell->y + 1.0f) * cell;
			const int size = (std::max)(8, static_cast<int>(
				std::lround(kCellPixels * m_zoom)));
			ccz::editor_preview::BattleAssetCache::UnitSpriteRef spriteRef;
			Gdiplus::Bitmap* sprite = GetUnitSprite(
				placement.unit, placement.unit.direction, &spriteRef);
			if (!placement.valid && adjusting) {
				Gdiplus::Pen invalidTarget(Gdiplus::Color(230, 235, 45, 45), 2.0f);
				graphics.DrawRectangle(&invalidTarget,
					static_cast<int>(ox + m_hoverCell->x * cell),
					static_cast<int>(oy + m_hoverCell->y * cell),
					(std::max)(1, static_cast<int>(cell)),
					(std::max)(1, static_cast<int>(cell)));
			}
			if (sprite && sprite->GetLastStatus() == Gdiplus::Ok) {
				Gdiplus::ColorMatrix ghostMatrix = {
					1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
					0,0,0,placement.valid ? 0.55f : 0.35f,0, 0,0,0,0,1};
				Gdiplus::ImageAttributes ghostAttributes;
				ghostAttributes.SetColorMatrix(&ghostMatrix);
				const int dx = static_cast<int>(std::lround(centerX)) - size / 2;
				const int dy = static_cast<int>(std::lround(baseY)) - size;
				if (spriteRef.flip_x) {
					Gdiplus::Matrix oldTransform;
					graphics.GetTransform(&oldTransform);
					Gdiplus::Matrix flip;
					flip.Translate(static_cast<float>(dx + size),
						static_cast<float>(dy));
					flip.Scale(-1.0f, 1.0f);
					graphics.SetTransform(&flip);
					graphics.DrawImage(sprite, Gdiplus::Rect(0, 0, size, size),
						0, 0, 48, 48, Gdiplus::UnitPixel, &ghostAttributes);
					graphics.SetTransform(&oldTransform);
				} else {
					graphics.DrawImage(sprite, Gdiplus::Rect(dx, dy, size, size),
						0, 0, 48, 48, Gdiplus::UnitPixel, &ghostAttributes);
				}
			} else if (ccz::preview::is_battle_deployment_slot_marker(
				placement.unit)) {
				draw_deployment_slot_marker(graphics, centerX, baseY, cell,
					placement.unit.deployment_index,
					placement.valid ? 0.55f : 0.35f,
					placement.unit.hidden);
			}
		}
	}
	Gdiplus::SolidBrush banner(Gdiplus::Color(220, 20, 20, 20));
	Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
	Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 225, 170));
	CString title;
	if (projection) title.Format(
		_T("S 战场预览 地图:%d 网格:%dx%d 全单位 缩放:%.2fx 已解析:%d"),
		m_editorState.battle.map.map_id,
		m_editorState.battle.map.width, m_editorState.battle.map.height,
		m_zoom, decoded);
	else title.Format(_T("S 战场预览 地图:%d 网格投影未闭合 缩放:%.2fx"),
		m_editorState.battle.map.map_id, m_zoom);
	graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
		m_canvasRect.Width(), m_canvasRect.Height()), Gdiplus::CombineModeReplace);
	graphics.FillRectangle(&banner, m_canvasRect.left, 0,
		m_canvasRect.Width(), 54);
	graphics.DrawString(title, -1, &font,
		Gdiplus::PointF(static_cast<float>(m_canvasRect.left + 8), 6.0f), &text);
	if (m_sidebarVisible) {
		graphics.SetClip(Gdiplus::Rect(0, 0, width, height),
			Gdiplus::CombineModeReplace);
		Gdiplus::SolidBrush sidebar(Gdiplus::Color(255, 40, 40, 40));
		graphics.FillRectangle(&sidebar, m_canvasRect.right, 0,
			width - m_canvasRect.right, height);
	}
	if (!m_diagnostic.IsEmpty()) {
		Gdiplus::SolidBrush panel(Gdiplus::Color(210, 20, 20, 20));
		graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
			m_canvasRect.Width(), m_canvasRect.Height()),
			Gdiplus::CombineModeReplace);
		graphics.FillRectangle(&panel, m_canvasRect.left,
			(std::max)(0, height-26), m_canvasRect.Width(), 26);
		graphics.DrawString(m_diagnostic, -1, &font,
			Gdiplus::PointF(8.0f, static_cast<float>(
				(std::max)(0, height-22))), &text);
	}
}

void CSBattlePreviewPage::OnSize(UINT type, int cx, int cy)
{
	CWnd::OnSize(type, cx, cy);
	LayoutEditorControls();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnPaint()
{
	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	if (client.IsRectEmpty()) return;
	CDC memory;
	memory.CreateCompatibleDC(&dc);
	CBitmap buffer;
	buffer.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
	CBitmap* old = memory.SelectObject(&buffer);
	{
		Gdiplus::Graphics graphics(memory.m_hDC);
		DrawCanvas(graphics, client.Width(), client.Height());
	}
	dc.BitBlt(0, 0, client.Width(), client.Height(),
		&memory, 0, 0, SRCCOPY);
	memory.SelectObject(old);
}

BOOL CSBattlePreviewPage::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void CSBattlePreviewPage::OnLButtonDown(UINT, CPoint point)
{
	SetFocus();
	const auto coordinate = BattleCoordFromScreen(point);
	if (!coordinate) return;
	if (m_editorOps && m_editorState.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->ConfirmPositionAdjustment(*coordinate));
		return;
	}
	if (m_editorOps && m_editorState.mode ==
		ccz::editor_preview::BattleEditorMode::Place) {
		m_leftStampActive = true;
		m_lastStampCell.reset();
		SetCapture();
		StampAt(*coordinate);
		return;
	}
	m_selectedUnit = HitTestUnit(*coordinate);
	if (m_selectedUnit && m_editorOps) {
		ShowOperationResult(m_editorOps->SelectAt(*coordinate));
	} else {
		m_editorState.selection.reset();
		Invalidate(FALSE);
	}
}

void CSBattlePreviewPage::OnLButtonUp(UINT, CPoint)
{
	if (!m_leftStampActive) return;
	m_leftStampActive = false;
	m_lastStampCell.reset();
	if (GetCapture() == this) ReleaseCapture();
}

void CSBattlePreviewPage::OnRButtonDown(UINT, CPoint point)
{
	SetFocus();
	if (m_editorOps && m_editorState.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->CancelPositionAdjustment());
		return;
	}
	const auto coordinate = BattleCoordFromScreen(point);
	if (!coordinate || !m_editorOps) return;
	m_hoverCell = coordinate;
	const auto hit = HitTestUnit(*coordinate);
	if (!hit) {
		m_diagnostic = _T("目标格没有唯一的可操作单位");
		Invalidate(FALSE);
		return;
	}
	const std::uint64_t cacheId =
		m_editorState.battle.units[*hit].editor_cache_id;
	ShowOperationResult(m_editorOps->SelectAt(*coordinate));
	CMenu menu;
	if (!menu.CreatePopupMenu()) return;
	constexpr UINT kAdjustPositionCommand = 1;
	constexpr UINT kDeleteUnitCommand = 2;
	menu.AppendMenu(MF_STRING, kAdjustPositionCommand, _T("调整位置"));
	menu.AppendMenu(MF_STRING, kDeleteUnitCommand, _T("删除"));
	CPoint screenPoint = point;
	ClientToScreen(&screenPoint);
	const UINT command = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		screenPoint.x, screenPoint.y, this);
	if (command == kAdjustPositionCommand) {
		ShowOperationResult(m_editorOps->BeginPositionAdjustment(cacheId));
	} else if (command == kDeleteUnitCommand) {
		ShowOperationResult(m_editorOps->DeleteAt(*coordinate));
	}
}

void CSBattlePreviewPage::OnKeyDown(UINT character, UINT repeat_count,
	UINT flags)
{
	if (character == VK_ESCAPE && m_editorOps && m_editorState.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->CancelPositionAdjustment());
		return;
	}
	CWnd::OnKeyDown(character, repeat_count, flags);
}

void CSBattlePreviewPage::OnCaptureChanged(CWnd* window)
{
	if (m_leftStampActive) {
		m_leftStampActive = false;
		m_lastStampCell.reset();
	}
	if (m_panActive) {
		m_panActive = false;
		m_panStartPoint = CPoint(-1, -1);
	}
	CWnd::OnCaptureChanged(window);
}

void CSBattlePreviewPage::OnSearchChanged()
{
	if (!m_editorOps) return;
	CString query;
	m_searchEdit.GetWindowText(query);
	const auto result = m_editorOps->SetCharacterSearch(
		std::wstring(query.GetString()));
	ShowOperationResult(result);
}

void CSBattlePreviewPage::OnCharacterSelectionChanged()
{
	if (!m_editorOps) return;
	const int selected = m_characterList.GetCurSel();
	if (selected == LB_ERR) return;
	const int dataId = static_cast<int>(m_characterList.GetItemData(selected));
	const auto result = m_brushTabActive
		? m_editorOps->SelectMinionBrush(dataId)
		: m_editorOps->SelectCharacter(dataId);
	ShowOperationResult(result);
}

void CSBattlePreviewPage::OnDeploymentSelectionChanged()
{
	BindSelectedDeployment();
}

void CSBattlePreviewPage::OnCharacterTab()
{
	m_brushTabActive = false;
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnBrushTab()
{
	m_brushTabActive = true;
	RefreshBrushList();
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnPreviousCharacterPage()
{
	if (!m_editorOps || m_brushTabActive ||
		m_editorState.characters.current_page == 0) return;
	ShowOperationResult(m_editorOps->SetCharacterPage(
		m_editorState.characters.current_page - 1));
}

void CSBattlePreviewPage::OnNextCharacterPage()
{
	if (!m_editorOps || m_brushTabActive) return;
	ShowOperationResult(m_editorOps->SetCharacterPage(
		m_editorState.characters.current_page + 1));
}

void CSBattlePreviewPage::OnUndo()
{
	if (m_nativeWritebackTransaction.undo_available()) {
		CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
		CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
		CcczEditor2View* view = child != nullptr
			? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
		std::wstring diagnostic;
		if (view == nullptr) {
			diagnostic = L"S 战场撤回拒绝：没有活动的原生剧本文档";
		} else {
			UndoNativeWriteback(*view, diagnostic);
		}
		AppendDiagnostic(diagnostic);
		RefreshEditorControls();
		Invalidate(FALSE);
		return;
	}
	if (m_editorOps) ShowOperationResult(m_editorOps->Undo());
}

#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
void CSBattlePreviewPage::OnQaWriteback()
{
	std::wstring diagnostic;
	if (!IsCurrentQaCopy()) {
		diagnostic = L"QA 写回拒绝：请先打开 QA 副本目录中带标记的 S 剧本";
		AppendDiagnostic(diagnostic);
		return;
	}
	if (AfxMessageBox(
		L"仅限 Debug 测试副本。确认后会把当前布阵事务写入编辑器内存中的 S 剧本；不会直接改写磁盘文件。之后请使用编辑器正常保存，并重新打开副本核验。继续吗？",
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
		return;
	}

	CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
	CcczEditor2View* view = child != nullptr
		? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
	if (view == nullptr) {
		diagnostic = L"QA 写回拒绝：没有活动的原生剧本文档";
	} else if (ApplyPendingNativeWritebackForQaCopy(*view, diagnostic)) {
		m_diagnostic = L"QA 写回成功：已提交到测试剧本内存；保存并重新打开后验证";
		AfxMessageBox(
			L"QA 原生事务已提交到内存中的测试副本。尚未写入磁盘；请用编辑器正常保存，然后关闭并重新打开该副本验证。",
			MB_OK | MB_ICONINFORMATION);
	}
	if (!diagnostic.empty()) AppendDiagnostic(diagnostic);
	RefreshEditorControls();
	Invalidate(FALSE);
}
#endif

void CSBattlePreviewPage::OnHideSidebar()
{
	SetSidebarVisible(false);
}

void CSBattlePreviewPage::OnShowSidebar()
{
	SetSidebarVisible(true);
}

void CSBattlePreviewPage::OnMButtonDown(UINT, CPoint point)
{
	SetFocus();
	m_panActive = true;
	m_panStartPoint = point;
	m_panStartScrollX = m_scrollX;
	m_panStartScrollY = m_scrollY;
	SetCapture();
	SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
}

void CSBattlePreviewPage::OnMButtonUp(UINT, CPoint)
{
	if (!m_panActive) return;
	m_panActive = false;
	m_panStartPoint = CPoint(-1, -1);
	ReleaseCapture();
	SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void CSBattlePreviewPage::OnMouseMove(UINT, CPoint point)
{
	if (m_panActive) {
		SetViewportScroll(m_panStartScrollX -
			(point.x - m_panStartPoint.x), m_panStartScrollY -
			(point.y - m_panStartPoint.y));
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
		return;
	}
	const auto cell = BattleCoordFromScreen(point);
	if (m_leftStampActive && cell) StampAt(*cell);
	if (cell != m_hoverCell) {
		m_hoverCell = cell;
		Invalidate(FALSE);
	}
	SetCursor(cell ? LoadCursor(nullptr, IDC_HAND)
		: LoadCursor(nullptr, IDC_ARROW));
}

BOOL CSBattlePreviewPage::OnMouseWheel(
	UINT flags, short delta, CPoint screenPoint)
{
	if (flags & MK_CONTROL) {
		CPoint point = screenPoint;
		ScreenToClient(&point);
		if (m_canvasRect.PtInRect(point))
			ZoomAt(point, m_zoom + (delta > 0 ? kZoomStep : -kZoomStep));
	}
	// The ordinary wheel never scrolls this view; native bars and middle drag
	// are the only panning mechanisms.
	return TRUE;
}

void CSBattlePreviewPage::OnHScroll(UINT code, UINT position, CScrollBar*)
{
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &info);
	int next = info.nPos;
	const int line = (std::max)(1, static_cast<int>(
		std::lround(kCellPixels * m_zoom)));
	const int page = (std::max)(1, static_cast<int>(info.nPage));
	switch (code) {
	case SB_LINELEFT: next -= line; break;
	case SB_LINERIGHT: next += line; break;
	case SB_PAGELEFT: next -= page; break;
	case SB_PAGERIGHT: next += page; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: next = static_cast<int>(position); break;
	case SB_TOP: next = info.nMin; break;
	case SB_BOTTOM: next = info.nMax; break;
	default: return;
	}
	SetViewportScroll(next, m_scrollY);
}

void CSBattlePreviewPage::OnVScroll(UINT code, UINT position, CScrollBar*)
{
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	GetScrollInfo(SB_VERT, &info);
	int next = info.nPos;
	const int line = (std::max)(1, static_cast<int>(
		std::lround(kCellPixels * m_zoom)));
	const int page = (std::max)(1, static_cast<int>(info.nPage));
	switch (code) {
	case SB_LINEUP: next -= line; break;
	case SB_LINEDOWN: next += line; break;
	case SB_PAGEUP: next -= page; break;
	case SB_PAGEDOWN: next += page; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: next = static_cast<int>(position); break;
	case SB_TOP: next = info.nMin; break;
	case SB_BOTTOM: next = info.nMax; break;
	default: return;
	}
	SetViewportScroll(m_scrollX, next);
}
