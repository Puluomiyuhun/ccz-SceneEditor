#include "pch.h"
#include "resource.h"
#include "ScenePreviewWnd.h"
#include "SceneBackgroundResolver.h"
#include "SceneMultiSelectOps.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"
#include "LongCharData.h"
#include "PreviewIntegration/EditorPreviewAdapter.h"
#include "PreviewIntegration/EditorPreviewCatalog.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include "PreviewIntegration/E5ImageLoader.h"
#include "PreviewIntegration/FreeRPreviewResolver.h"
#include "PreviewIntegration/FreeRReplayComposer.h"
#include "PreviewIntegration/DialogueOverlayRenderer.h"
#include "MainFrm.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr UINT kPreviewRefreshMessage = WM_APP + 0x341;
constexpr UINT kConditionActionMessage = WM_APP + 0x342;
constexpr UINT kVariableActionMessage = WM_APP + 0x343;
constexpr UINT kConditionButtonId = 0x7F10;
constexpr UINT kActivationButtonId = 0x7F11;
constexpr UINT kVariableButtonId = 0x7F12;
constexpr UINT kUndoButtonId = 0x7F13;
constexpr UINT kPlaybackStartTargetButtonId = 0x7F20;
constexpr UINT kPlaybackTargetEndButtonId = 0x7F21;
constexpr UINT kPlaybackResetButtonId = 0x7F22;
constexpr UINT kPlaybackPlayButtonId = 0x7F23;
constexpr UINT kPlaybackStopButtonId = 0x7F24;
constexpr UINT kPlaybackStepBackButtonId = 0x7F25;
constexpr UINT kPlaybackStepForwardButtonId = 0x7F26;
constexpr UINT kPlaybackSpeedButtonId = 0x7F27;
constexpr int kConditionButtonHeight = 24;
constexpr int kConditionButtonWidth = 96;
constexpr int kConditionButtonGap = 4;
constexpr int kConditionButtonMargin = 8;
constexpr int kPlaybackButtonSize = 28;
constexpr int kPlaybackSpeedButtonWidth = 42;
constexpr int kPlaybackGap = 3;
constexpr int kPlaybackProgressWidth = 100;
constexpr int kPlaybackCounterWidth = 56;
constexpr int kPlaybackBarHeight = 28;

ccz::preview::DialogueProfile MakeEditorDialogueProfile()
{
	using namespace ccz::preview;
	DialogueProfile profile;
	profile.architecture = "x86";
	profile.dialogue_ids = {
		{20, DialogueKind::Standard, true, {},
		 PersonFieldEncoding::SpeakerPrefixLabel, true},
		{21, DialogueKind::Alternate, true, {0, 1},
		 PersonFieldEncoding::Per2Code, false},
		{22, DialogueKind::Narration, false, {},
		 PersonFieldEncoding::None, false},
		{0x2c, DialogueKind::Narration, false, {},
		 PersonFieldEncoding::None, false},
		{99, DialogueKind::Solo, true, {}, PersonFieldEncoding::None, false},
		{0x69, DialogueKind::Narration, false, {},
		 PersonFieldEncoding::None, false},
		{122, DialogueKind::Extended, true, {},
		 PersonFieldEncoding::SpeakerPrefixLabel, true},
	};
	profile.face_state_instruction_id = 32;
	profile.data_record_base = 0x18c;
	profile.data_record_stride = 0x20;
	profile.avatar_offset = 0x0d;
	profile.avatar_width = 2;
	profile.r_image_offset = 0x0f;
	profile.r_image_width = 2;
	profile.face_index_table_offset = 0x110;
	profile.face_entry_size = 12;
	profile.face_index_base = 0;
	profile.face_index_bias = 7;
	return profile;
}

struct ResolvedNativeReplayItem {
	HTREEITEM item{};
	ItemData* data{};
};

// HTREEITEM is an opaque TreeView-generation handle.  Do not pass a replay
// handle straight to GetItemData: first prove that the handle is still an
// item in this live TreeView, then perform the MFC data lookup and ID check.
HTREEITEM FindCurrentTreeItem(CTreeCtrl& tree, HTREEITEM expected)
{
	if (expected == nullptr || tree.GetSafeHwnd() == nullptr ||
		!::IsWindow(tree.GetSafeHwnd())) {
		return nullptr;
	}

	std::vector<HTREEITEM> pending;
	for (HTREEITEM root = tree.GetRootItem(); root != nullptr;
		root = tree.GetNextSiblingItem(root)) {
		pending.push_back(root);
	}
	while (!pending.empty()) {
		const HTREEITEM current = pending.back();
		pending.pop_back();
		if (current == expected) {
			TVITEM itemInfo{};
			itemInfo.mask = TVIF_HANDLE;
			itemInfo.hItem = current;
			if (!TreeView_GetItem(tree.GetSafeHwnd(), &itemInfo)) {
				return nullptr;
			}
			return current;
		}
		for (HTREEITEM child = tree.GetChildItem(current); child != nullptr;
			child = tree.GetNextSiblingItem(child)) {
			pending.push_back(child);
		}
	}
	return nullptr;
}

bool IsLiveTreeItem(HWND treeWindow, HTREEITEM item)
{
	if (item == nullptr) return true;
	if (treeWindow == nullptr || !::IsWindow(treeWindow)) return false;
	TVITEM itemInfo{};
	itemInfo.mask = TVIF_HANDLE;
	itemInfo.hItem = item;
	return TreeView_GetItem(treeWindow, &itemInfo) != FALSE;
}

ccz::editor_preview::RDocumentActivationSnapshot MakeRActivationSnapshot(
	const CcczEditor2Doc& document, CTreeCtrl& tree, HTREEITEM selectedItem)
{
	ccz::editor_preview::RDocumentActivationSnapshot snapshot;
	snapshot.document_id = document.GetPreviewDocumentId();
	snapshot.normalized_path_key =
		ccz::editor_preview::NormalizeRPreviewDocumentPath(
			document.pathName != nullptr ? document.pathName : L"");
	snapshot.content_epoch = document.GetPreviewContentEpoch();
	snapshot.tree_generation = document.GetPreviewTreeGeneration();
	snapshot.tree_window = reinterpret_cast<std::uintptr_t>(tree.GetSafeHwnd());
	snapshot.selected_target = reinterpret_cast<std::uintptr_t>(selectedItem);
	return snapshot;
}

void AppendNativePlanDiagnostic(std::wstring& destination,
	const std::wstring& message)
{
	if (message.empty()) return;
	if (!destination.empty()) destination += L"；";
	destination += message;
}
}

static CcczEditor2View* GetActiveEditorView();

BEGIN_MESSAGE_MAP(CRScenePreviewPage, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_MOUSEWHEEL()
	ON_WM_TIMER()
	ON_MESSAGE(WM_APP + 0x341, &CRScenePreviewPage::OnPreviewRefreshMessage)
	ON_MESSAGE(WM_APP + 0x342, &CRScenePreviewPage::OnConditionActionMessage)
	ON_MESSAGE(WM_APP + 0x343, &CRScenePreviewPage::OnVariableActionMessage)
	ON_BN_CLICKED(kConditionButtonId, &CRScenePreviewPage::OnConditionButtonClicked)
	ON_BN_CLICKED(kActivationButtonId, &CRScenePreviewPage::OnActivationButtonClicked)
	ON_BN_CLICKED(kVariableButtonId, &CRScenePreviewPage::OnVariableButtonClicked)
	ON_BN_CLICKED(kUndoButtonId, &CRScenePreviewPage::OnUndoButtonClicked)
	ON_BN_CLICKED(kPlaybackStartTargetButtonId,
		&CRScenePreviewPage::OnPlaybackStartTargetClicked)
	ON_BN_CLICKED(kPlaybackTargetEndButtonId,
		&CRScenePreviewPage::OnPlaybackTargetEndClicked)
	ON_BN_CLICKED(kPlaybackResetButtonId,
		&CRScenePreviewPage::OnPlaybackResetClicked)
	ON_BN_CLICKED(kPlaybackPlayButtonId,
		&CRScenePreviewPage::OnPlaybackPlayClicked)
	ON_BN_CLICKED(kPlaybackStopButtonId,
		&CRScenePreviewPage::OnPlaybackStopClicked)
	ON_BN_CLICKED(kPlaybackStepBackButtonId,
		&CRScenePreviewPage::OnPlaybackStepBackClicked)
	ON_BN_CLICKED(kPlaybackStepForwardButtonId,
		&CRScenePreviewPage::OnPlaybackStepForwardClicked)
	ON_BN_CLICKED(kPlaybackSpeedButtonId,
		&CRScenePreviewPage::OnPlaybackSpeedClicked)
	ON_UPDATE_COMMAND_UI(kConditionButtonId,
		&CRScenePreviewPage::OnUpdateConditionButton)
	ON_UPDATE_COMMAND_UI(kActivationButtonId,
		&CRScenePreviewPage::OnUpdateActivationButton)
	ON_UPDATE_COMMAND_UI(kVariableButtonId,
		&CRScenePreviewPage::OnUpdateVariableButton)
	ON_UPDATE_COMMAND_UI(kUndoButtonId,
		&CRScenePreviewPage::OnUpdateUndoButton)
	ON_UPDATE_COMMAND_UI(kPlaybackStartTargetButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackTargetEndButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackResetButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackPlayButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackStopButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackStepBackButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackStepForwardButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_UPDATE_COMMAND_UI(kPlaybackSpeedButtonId,
		&CRScenePreviewPage::OnUpdatePlaybackButton)
	ON_EN_CHANGE(IDC_SCENE_CHAR_SEARCH, &CRScenePreviewPage::OnSearchChange)
	ON_LBN_SELCHANGE(IDC_SCENE_CHAR_LIST, &CRScenePreviewPage::OnCharListSelChange)
END_MESSAGE_MAP()

CRScenePreviewPage::CRScenePreviewPage() noexcept
{
	m_dialogueProfile = MakeEditorDialogueProfile();
	m_previewCatalog = ccz::editor_preview::MakeEditorPreviewCatalog();
}

CRScenePreviewPage::~CRScenePreviewPage()
{
	InvalidatePendingPreviewRefresh();
	ClearRBitmapCaches();
}

BOOL CRScenePreviewPage::CreatePage(CWnd* parent, UINT control_id)
{
	const CString window_class = AfxRegisterWndClass(
		CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
	return CreateEx(0, window_class, _T("R 场景预览"),
		WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 1, 1), parent, control_id);
}

void CRScenePreviewPage::InvalidatePendingPreviewRefresh()
{
	StopDynamicPlayback(false);
	m_pendingPreviewRequest.clear();
	m_previewRefreshState.Invalidate();
	++m_previewTreeGeneration;
	ClearNativePreviewState();
	m_previewSession = {};
	m_documentSessions.clear();
	m_activeDocumentId = 0;
	m_activeActivation = {};
	m_hasActiveBaseline = false;
}

bool CRScenePreviewPage::ActivateDocument(
	CTreeCtrl& tree, HTREEITEM selectedItem, CcczEditor2Doc& document,
	bool& reusedSession)
{
	using ccz::editor_preview::DecideRDocumentActivation;
	using ccz::editor_preview::RDocumentActivationDecision;
	reusedSession = false;
	const auto current = MakeRActivationSnapshot(document, tree, selectedItem);
	if (current.document_id == 0 || current.normalized_path_key.empty() ||
		tree.GetSafeHwnd() == nullptr || !::IsWindow(tree.GetSafeHwnd())) {
		return false;
	}

	const bool selectedItemIsLive = IsLiveTreeItem(
		tree.GetSafeHwnd(), selectedItem) && tree.GetSelectedItem() == selectedItem;
	if (m_activeDocumentId == current.document_id) {
		if (m_hasActiveBaseline && selectedItemIsLive &&
			DecideRDocumentActivation(&m_activeActivation, current) ==
				RDocumentActivationDecision::Reuse) {
			m_pTreeCtrl = &tree;
			m_hCurItem = selectedItem;
			reusedSession = true;
			return true;
		}
		if (m_pendingPreviewRequest.valid() && selectedItemIsLive &&
			DecideRDocumentActivation(
				&m_pendingPreviewRequest.activation, current) ==
				RDocumentActivationDecision::Reuse) {
			reusedSession = true;
			return true;
		}
		// A changed selection/content stays on the current logical session and
		// will be consumed by the ordinary forced refresh path.
		m_activeActivation = current;
		m_hasActiveBaseline = false;
		return true;
	}

	ParkActiveDocument();
	auto cached = m_documentSessions.find(current.document_id);
	bool preservedSameDocumentSession = false;
	if (cached != m_documentSessions.end()) {
		const bool sameDocument =
			cached->second.activation.document_id == current.document_id &&
			cached->second.activation.normalized_path_key ==
				current.normalized_path_key;
		const auto decision = DecideRDocumentActivation(
			cached->second.has_baseline ? &cached->second.activation : nullptr,
			current);
		if (decision == RDocumentActivationDecision::Reuse &&
			selectedItemIsLive) {
			RDocumentPreviewSession session = std::move(cached->second);
			m_documentSessions.erase(cached);

			const CString newPath(document.pathName);
			const CString oldRoot = GetRootPath(m_modPath);
			const CString newRoot = GetRootPath(newPath);
			if (newRoot.CompareNoCase(oldRoot) != 0) {
				ClearRBitmapCaches();
			}
			m_modPath = newPath;
			std::wstring assetDiagnostic;
			if (newPath.IsEmpty()) {
				m_previewAssetCache.Clear();
			} else {
				m_previewAssetCache.LoadForDocument(
					std::wstring(newPath.GetString()), m_dialogueProfile,
					&assetDiagnostic);
			}
			m_previewCatalog = ccz::editor_preview::MakeEditorPreviewCatalog();
			const auto& documentCatalog = m_previewAssetCache.instruction_catalog();
			if (documentCatalog.valid) {
				for (const auto& entry : documentCatalog.names)
					m_previewCatalog.names[entry.first] = entry.second;
				m_previewCatalog.valid = true;
			}

			m_state = std::move(session.state);
			m_previewSession = std::move(session.preview_session);
			m_lastPreviewInput = std::move(session.last_preview_input);
			m_lastEditorTreeSnapshot =
				std::move(session.last_editor_tree_snapshot);
			m_hasEditorTreeSnapshot = session.has_editor_tree_snapshot;
			m_cachedSceneResolution =
				std::move(session.cached_scene_resolution);
			m_cachedSceneResolutionValid =
				session.cached_scene_resolution_valid;
			m_cachedFreeRPrefixPlan =
				std::move(session.cached_free_r_prefix_plan);
			m_cachedFreeRPrefixValid = session.cached_free_r_prefix_valid;
			m_debugInfo = session.debug_info.c_str();
			m_internalizationFallbackActive =
				session.internalization_fallback_active;
			m_conditionButtonVisible = session.condition_button_visible;
			m_activationButtonVisible = session.activation_button_visible;
			m_conditionPaneVisible = session.condition_pane_visible;
			m_variableButtonVisible = session.variable_button_visible;
			m_variablePaneVisible = session.variable_pane_visible;
			m_playbackVisible = session.playback_visible;
			m_selectedCharIDs = std::move(session.selected_character_ids);
			m_primaryCharID = session.primary_character_id;
			m_previewUndoStack = std::move(session.undo_stack);
			m_dynamicTimeline = std::move(session.dynamic_timeline);
			m_dynamicStaticProgressIndex =
				session.dynamic_static_progress_index;
			m_dynamicPlaybackRange = session.dynamic_playback_range;
			m_dynamicPlaybackSession.Stop();
			m_dynamicPlaybackSession.SetSpeed(session.dynamic_playback_speed);
			m_dynamicPlaybackActive = false;
			m_dynamicPlaybackPrepared = false;
			m_dynamicVisualProgress = 0.0;
			m_visibleDialogueOverlay = m_previewSession.frame().dialogue;
			m_pTreeCtrl = &tree;
			m_hCurItem = selectedItem;
			m_activeDocumentId = current.document_id;
			m_activeActivation = current;
			m_hasActiveBaseline = true;

			EnsureDynamicAssets();
			ReconcileCharacterSelection();
			UpdateConditionPaneFromFrame();
			UpdateVariablePaneFromFrame();
			UpdatePlaybackControls();
			UpdateUndoButton();
			m_conditionTreePane.ShowPane(m_conditionPaneVisible);
			m_variableControlPane.ShowPane(m_variablePaneVisible);
			CRect clientRect;
			GetClientRect(&clientRect);
			LayoutConditionUi(clientRect.Width(), clientRect.Height());
			Invalidate(FALSE);
			reusedSession = true;
			return true;
		}

		// Keep only the same document's condition/variable simulation when its
		// source changed.  Every captured tree handle and rendered scene value is
		// discarded before the forced refresh.
		if (sameDocument) {
			m_previewSession = std::move(cached->second.preview_session);
			preservedSameDocumentSession = true;
		}
		m_documentSessions.erase(cached);
	} else {
		m_previewSession = {};
	}

	ClearNativePreviewState();
	if (!preservedSameDocumentSession) {
		// The cache entry may have been erased above; this assignment also
		// guarantees that a different open document cannot inherit R options.
		m_previewSession = {};
	}
	m_pTreeCtrl = &tree;
	m_hCurItem = selectedItem;
	m_activeDocumentId = current.document_id;
	m_activeActivation = current;
	m_hasActiveBaseline = false;
	return true;
}

void CRScenePreviewPage::ParkActiveDocument()
{
	if (m_activeDocumentId == 0) return;
	const auto documentId = m_activeDocumentId;
	RDocumentPreviewSession session;
	session.activation = m_activeActivation;
	session.has_baseline = m_hasActiveBaseline;
	session.dynamic_timeline = std::move(m_dynamicTimeline);
	session.dynamic_static_progress_index = m_dynamicStaticProgressIndex;
	session.dynamic_playback_range = m_dynamicPlaybackRange;
	session.dynamic_playback_speed = m_dynamicPlaybackSession.speed();
	StopDynamicPlayback(false);
	session.state = std::move(m_state);
	session.preview_session = std::move(m_previewSession);
	session.last_preview_input = std::move(m_lastPreviewInput);
	session.last_editor_tree_snapshot = std::move(m_lastEditorTreeSnapshot);
	session.has_editor_tree_snapshot = m_hasEditorTreeSnapshot;
	session.cached_scene_resolution = std::move(m_cachedSceneResolution);
	session.cached_scene_resolution_valid = m_cachedSceneResolutionValid;
	session.cached_free_r_prefix_plan = std::move(m_cachedFreeRPrefixPlan);
	session.cached_free_r_prefix_valid = m_cachedFreeRPrefixValid;
	session.debug_info.assign(m_debugInfo.GetString(), m_debugInfo.GetLength());
	session.internalization_fallback_active = m_internalizationFallbackActive;
	session.condition_button_visible = m_conditionButtonVisible;
	session.activation_button_visible = m_activationButtonVisible;
	session.condition_pane_visible = m_conditionPaneVisible;
	session.variable_button_visible = m_variableButtonVisible;
	session.variable_pane_visible = m_variablePaneVisible;
	session.playback_visible = m_playbackVisible;
	session.selected_character_ids = std::move(m_selectedCharIDs);
	session.primary_character_id = m_primaryCharID;
	session.undo_stack = std::move(m_previewUndoStack);
	m_documentSessions.insert_or_assign(documentId, std::move(session));

	m_pendingPreviewRequest.clear();
	m_previewRefreshState.Invalidate();
	++m_previewTreeGeneration;
	if ((m_bPlaceMode || m_bPortraitPlaceMode || m_bPortraitMoveMode) &&
		::GetCapture() == GetSafeHwnd()) {
		ReleaseCapture();
	}
	m_bPlaceMode = false;
	m_bMoveMode = false;
	m_bMultiMoveMode = false;
	m_bPointMoveMode = false;
	m_bAdjustMode = false;
	m_bPortraitAdjustMode = false;
	m_bPortraitPlaceMode = false;
	m_bPortraitMoveMode = false;
	m_selectedCharID = -1;
	m_hitCharID = -1;
	m_moveCharID = -1;
	m_adjustCharID = -1;
	m_adjustPortraitPersonID = -1;
	m_pTreeCtrl = nullptr;
	m_hCurItem = nullptr;
	m_previewSession = {};
	m_state = {};
	m_lastPreviewInput = {};
	m_lastEditorTreeSnapshot = {};
	m_hasEditorTreeSnapshot = false;
	m_cachedSceneResolution = {};
	m_cachedSceneResolutionValid = false;
	m_cachedFreeRPrefixPlan = {};
	m_cachedFreeRPrefixValid = false;
	m_previewUndoStack.clear();
	m_activeDocumentId = 0;
	m_activeActivation = {};
	m_hasActiveBaseline = false;
}

bool CRScenePreviewPage::PrepareForTreeRebuild(CcczEditor2Doc& document)
{
	InvalidateDocumentPreview(document.GetPreviewDocumentId());
	return m_activeDocumentId == document.GetPreviewDocumentId();
}

bool CRScenePreviewPage::EditorTreeDestroyed(
	HWND treeWindow, CcczEditor2Doc& document)
{
	const auto documentId = document.GetPreviewDocumentId();
	// This callback is the owning document view's destruction boundary. Even
	// when the cached handle no longer matches after a failed tree rebuild, the
	// document session must not retain tree-backed state.
	m_documentSessions.erase(documentId);
	if (m_activeDocumentId != documentId ||
		m_activeActivation.tree_window !=
			reinterpret_cast<std::uintptr_t>(treeWindow)) {
		return false;
	}
	InvalidateDocumentPreview(documentId);
	m_activeDocumentId = 0;
	m_activeActivation = {};
	m_hasActiveBaseline = false;
	return true;
}

bool CRScenePreviewPage::CloseDocument(
	ccz::editor_preview::PreviewDocumentId documentId)
{
	m_documentSessions.erase(documentId);
	if (m_activeDocumentId != documentId) return false;
	InvalidateDocumentPreview(documentId);
	m_activeDocumentId = 0;
	m_activeActivation = {};
	m_hasActiveBaseline = false;
	return true;
}

void CRScenePreviewPage::InvalidateDocumentPreview(
	ccz::editor_preview::PreviewDocumentId documentId)
{
	m_documentSessions.erase(documentId);
	if (m_activeDocumentId != documentId) return;
	StopDynamicPlayback(false);
	m_pendingPreviewRequest.clear();
	m_previewRefreshState.Invalidate();
	++m_previewTreeGeneration;
	ClearNativePreviewState();
	m_previewSession = {};
	m_activeActivation = {};
	m_hasActiveBaseline = false;
}

void CRScenePreviewPage::ClearNativePreviewState()
{
	StopDynamicPlayback(false);
	if (m_bPortraitPlaceMode || m_bPortraitMoveMode)
		ReleaseCapture();
	m_bPortraitPlaceMode = false;
	m_bPortraitMoveMode = false;
	m_portraitPlacePersonID = -1;
	m_movePortraitPersonID = -1;
	m_portraitPlaceCursorPos = { -1, -1 };
	m_portraitMoveCursorPos = { -1, -1 };
	ClearPreviewUndo();
	// Clearing the typed model is part of the native-state reset.  The pane
	// deletes TreeView items before releasing bindings and selection state.
	if (m_conditionTreePane.GetSafeHwnd() != nullptr) {
		m_conditionTreePane.UpdateModel(nullptr, {}, {});
	}
	if (m_variableControlPane.GetSafeHwnd() != nullptr) {
		m_variableControlPane.UpdateRows({}, {}, {});
	}
	m_conditionPaneVisible = false;
	m_conditionButtonVisible = false;
	m_activationButtonVisible = false;
	m_variablePaneVisible = false;
	m_variableButtonVisible = false;
	m_undoButtonVisible = false;
	m_playbackVisible = false;
	m_lastPreviewInput = {};
	m_lastEditorTreeSnapshot = {};
	m_hasEditorTreeSnapshot = false;
	m_cachedSceneResolution = {};
	m_cachedSceneResolutionValid = false;
	m_cachedFreeRPrefixPlan = {};
	m_cachedFreeRPrefixValid = false;
	m_dynamicTimeline = {};
	m_dynamicStaticProgressIndex = 0;
	m_dynamicPlaybackPrepared = false;
	m_dynamicInitialState = {};
	m_dynamicInitialDialogue = {};
	m_dynamicFromState = {};
	m_dynamicToState = {};
	m_dynamicFromDialogue = {};
	m_dynamicToDialogue = {};
	if (m_conditionTreePane.GetSafeHwnd() != nullptr) {
		m_conditionTreePane.ShowPane(false);
	}
	if (m_conditionButton.GetSafeHwnd() != nullptr) {
		m_conditionButton.ShowWindow(SW_HIDE);
		// Hidden is a visibility state, not a disabled state.  Keeping the
		// operation controls enabled prevents a stale refresh from exposing a
		// grey button before the next frame publishes its capability flags.
		m_conditionButton.EnableWindow(TRUE);
	}
	if (m_activationButton.GetSafeHwnd() != nullptr) {
		m_activationButton.ShowWindow(SW_HIDE);
		m_activationButton.EnableWindow(TRUE);
	}
	if (m_variableControlPane.GetSafeHwnd() != nullptr) {
		m_variableControlPane.ShowPane(false);
	}
	if (m_variableButton.GetSafeHwnd() != nullptr) {
		m_variableButton.ShowWindow(SW_HIDE);
		m_variableButton.EnableWindow(TRUE);
	}
	if (m_undoButton.GetSafeHwnd() != nullptr) {
		m_undoButton.ShowWindow(SW_HIDE);
		m_undoButton.EnableWindow(TRUE);
	}
	CWnd* playbackControls[] = {
		&m_playbackStartTargetButton, &m_playbackTargetEndButton,
		&m_playbackResetButton, &m_playbackPlayButton, &m_playbackStopButton,
		&m_playbackStepBackButton, &m_playbackStepForwardButton,
		&m_playbackSpeedButton, &m_playbackProgress, &m_playbackCounter};
	for (CWnd* control : playbackControls) {
		if (control != nullptr && control->GetSafeHwnd() != nullptr) {
			control->ShowWindow(SW_HIDE);
		}
	}
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
	}
	m_state.Clear();
	m_visibleDialogueOverlay = {};
	m_internalizationFallbackActive = false;
	m_pMapBitmap = nullptr;
	m_cachedMapID = -1;
	m_debugInfo.Empty();
	m_pTreeCtrl = nullptr;
	m_hCurItem = nullptr;
	m_selectedCharIDs.clear();
	m_primaryCharID = -1;
	m_bMoveMode = false;
	m_bMultiMoveMode = false;
	m_bPointMoveMode = false;
	m_bAdjustMode = false;
	m_bPortraitAdjustMode = false;
	m_bPortraitPlaceMode = false;
	m_bPortraitMoveMode = false;
	m_bPlaceMode = false;
	m_moveCharID = -1;
	m_adjustCharID = -1;
	m_adjustPortraitPersonID = -1;
	m_portraitPlacePersonID = -1;
	m_movePortraitPersonID = -1;
	m_selectedCharID = -1;
	m_moveCursorPos = { -1, -1 };
	m_multiMoveCursorPos = { -1, -1 };
	m_pointMoveCursorPos = { -1, -1 };
	m_portraitAdjustCursorPos = { -1, -1 };
	m_portraitPlaceCursorPos = { -1, -1 };
	m_portraitMoveCursorPos = { -1, -1 };
	m_placeCursorPos = { -1, -1 };
	m_hitCharID = -1;
	if (GetSafeHwnd() != nullptr) {
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::ClearRBitmapCaches()
{
	for (auto& entry : m_mapBitmapCache) delete entry.second;
	m_mapBitmapCache.clear();
	m_pMapBitmap = nullptr;
	m_cachedMapID = -1;
	for (auto& entry : m_spriteCache) delete entry.second;
	m_spriteCache.clear();
	m_spriteDataLoaded = false;
	memset(m_charImageIDs, 0, sizeof(m_charImageIDs));
	for (auto& entry : m_portraitCache) delete entry.second;
	m_portraitCache.clear();
	m_portraitFaceIndices.clear();
	m_portraitDataLoaded = false;
}

bool CRScenePreviewPage::BuildFreeRPrefixPlan(
	CTreeCtrl& tree,
	const ccz::editor_preview::ScenePreviewResolution& resolution,
	ccz::preview::NativeReplayPlan& prefix)
{
	(void)tree;
	if (!resolution.resolved || !resolution.has_click_target ||
		resolution.appearance_section_handle == 0 ||
		resolution.appearance_instruction_handle == 0 ||
		!m_hasEditorTreeSnapshot) {
		prefix = {};
		prefix.diagnostic = L"自由R缺少可用的武将出现 Section 快照";
		return false;
	}

	// A Free-R trigger Scene can contain its own branch-local setup Sections.
	// Those Sections are replayed by BuildFreeRTriggerSectionPrefixPlans; do
	// not replay the matching appearance Section a second time as an inherited
	// prefix, or commands such as 0x30/0x31 would be duplicated.
	if (resolution.appearance_scene_handle ==
		resolution.trigger_scene_handle) {
		prefix = {};
		prefix.complete = true;
		prefix.target_reached = true;
		prefix.stop_reason = ccz::preview::ReplayStopReason::ReachedEnd;
		return true;
	}

	if (m_cachedFreeRPrefixValid) {
		prefix = m_cachedFreeRPrefixPlan;
		return prefix.complete;
	}

	const auto appearance_capture =
		ccz::editor_preview::BuildEditorPreviewInputForSection(
			m_lastEditorTreeSnapshot,
			resolution.appearance_section_handle,
			resolution.appearance_instruction_handle);
	if (!appearance_capture.ok || appearance_capture.idle) {
		prefix = {};
		prefix.diagnostic = appearance_capture.diagnostic.empty()
			? L"自由R武将出现 Section 无法转换为预览模型"
			: appearance_capture.diagnostic;
		m_cachedFreeRPrefixPlan = prefix;
		m_cachedFreeRPrefixValid = true;
		return false;
	}

	ccz::preview::ReplayOptions appearance_options =
		m_previewSession.options();
	ccz::preview::SectionExecutionModel appearance_model =
		ccz::preview::build_section_execution_model(
			appearance_capture.input.section, m_previewCatalog,
			appearance_options);
	if (ccz::preview::initialize_default_choice_selections(
			appearance_model, appearance_options)) {
		appearance_model = ccz::preview::build_section_execution_model(
			appearance_capture.input.section, m_previewCatalog,
			appearance_options);
	}
	prefix = ccz::preview::build_native_replay_plan_to_end(
		appearance_model, m_dialogueProfile, appearance_options);
	if (!prefix.complete && prefix.diagnostic.empty()) {
		prefix.diagnostic = L"自由R武将出现 Section 未能完成条件树推演";
	}
	m_cachedFreeRPrefixPlan = prefix;
	m_cachedFreeRPrefixValid = true;
	return prefix.complete;
}

bool CRScenePreviewPage::BuildFreeRTriggerSectionPrefixPlans(
	CTreeCtrl& tree,
	const ccz::editor_preview::ScenePreviewResolution& resolution,
	std::vector<ccz::preview::NativeReplayPlan>& prefixes)
{
	(void)tree;
	prefixes.clear();
	if (!resolution.resolved || !resolution.has_click_target ||
		!m_hasEditorTreeSnapshot) {
		return true;
	}

	// Every Section before the selected one is replayed to its reachable end.
	// This is the missing link in the old snapshot path: a Free-R trigger Scene
	// is a sequence of Section-level events, not one flattened dialogue node.
	for (const std::uintptr_t section_handle :
		 resolution.trigger_section_handles_before_selected) {
		const auto capture =
			ccz::editor_preview::BuildEditorPreviewInputForSectionToEnd(
				m_lastEditorTreeSnapshot, section_handle);
		if (!capture.ok || capture.idle) {
			ccz::preview::NativeReplayPlan failed;
			failed.diagnostic = capture.diagnostic.empty()
				? L"自由R前置 Section 无法转换为预览模型"
				: capture.diagnostic;
			prefixes.push_back(std::move(failed));
			return false;
		}

		ccz::preview::ReplayOptions options = m_previewSession.options();
		ccz::preview::SectionExecutionModel model =
			ccz::preview::build_section_execution_model(
				capture.input.section, m_previewCatalog, options);
		if (ccz::preview::initialize_default_choice_selections(model, options)) {
			model = ccz::preview::build_section_execution_model(
				capture.input.section, m_previewCatalog, options);
		}
		ccz::preview::NativeReplayPlan plan =
			ccz::preview::build_native_replay_plan_to_end(
				model, m_dialogueProfile, options);
		if (!plan.complete && plan.diagnostic.empty()) {
			plan.diagnostic = L"自由R前置 Section 未能完成条件树推演";
		}
		prefixes.push_back(std::move(plan));
		if (!prefixes.back().complete) {
			return false;
		}
	}
	return true;
}

bool CRScenePreviewPage::ComposeSpecialReplayPlan(
	CTreeCtrl& tree, const ccz::preview::NativeReplayPlan& current,
	ccz::preview::NativeReplayPlan& composed)
{
	composed = current;
	// An unresolved/inactive target is intentionally left to the existing
	// condition-tree boundary.  Do not manufacture a complete special plan
	// from an empty native list.
	if (!current.complete || !m_hasEditorTreeSnapshot ||
		m_hCurItem == nullptr) {
		return true;
	}

	HTREEITEM section = m_hCurItem;
	while (section != nullptr) {
		ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(section));
		if (data != nullptr && data->id == -2) break;
		section = tree.GetParentItem(section);
	}
	if (section == nullptr) {
		return true;
	}

	if (!m_cachedSceneResolutionValid) {
		if (m_lastEditorTreeSnapshot.document_roots.empty()) {
			return true;
		}
		m_cachedSceneResolution = ccz::editor_preview::ResolveScenePreview(
			m_lastEditorTreeSnapshot.document_roots.front(),
			reinterpret_cast<std::uintptr_t>(section),
			reinterpret_cast<std::uintptr_t>(m_hCurItem));
		m_cachedSceneResolutionValid = true;
	}

	const auto& resolution = m_cachedSceneResolution;
	if (resolution.kind != ccz::editor_preview::ScenePreviewKind::FreeR ||
		!resolution.resolved || !resolution.has_click_target) {
		return true;
	}

	ccz::preview::NativeReplayPlan prefix;
	if (!BuildFreeRPrefixPlan(tree, resolution, prefix)) {
		composed = {};
		composed.complete = false;
		composed.diagnostic = prefix.diagnostic.empty()
			? L"自由R前置场景推演失败"
			: prefix.diagnostic;
		return true;
	}

	std::vector<ccz::preview::NativeReplayPlan> trigger_prefixes;
	if (!BuildFreeRTriggerSectionPrefixPlans(tree, resolution,
		trigger_prefixes)) {
		composed = {};
		composed.complete = false;
		composed.diagnostic = L"自由R前置 Section 推演失败";
		for (const auto& plan : trigger_prefixes) {
			if (!plan.diagnostic.empty()) {
				composed.diagnostic = plan.diagnostic;
				break;
			}
		}
		return true;
	}
	composed = ccz::editor_preview::ComposeFreeRReplayPlan(
		prefix, trigger_prefixes, current);
	return true;
}

bool CRScenePreviewPage::ApplyFreeRPrefixOnly(CTreeCtrl& tree)
{
	if (!m_cachedSceneResolutionValid ||
		m_cachedSceneResolution.kind !=
			ccz::editor_preview::ScenePreviewKind::FreeR ||
		!m_cachedSceneResolution.resolved ||
		!m_cachedSceneResolution.has_click_target) {
		return false;
	}
	ccz::preview::NativeReplayPlan prefix;
	if (!BuildFreeRPrefixPlan(tree, m_cachedSceneResolution, prefix) ||
		!prefix.complete) {
		if (!prefix.diagnostic.empty()) {
			AppendDiagnostic(CString(prefix.diagnostic.c_str()));
		}
		return false;
	}
	std::vector<ccz::preview::NativeReplayPlan> trigger_prefixes;
	if (!BuildFreeRTriggerSectionPrefixPlans(
			tree, m_cachedSceneResolution, trigger_prefixes)) {
		for (const auto& plan : trigger_prefixes) {
			if (!plan.diagnostic.empty()) {
				AppendDiagnostic(CString(plan.diagnostic.c_str()));
			}
		}
		return false;
	}
	ccz::preview::NativeReplayPlan empty_current;
	empty_current.complete = true;
	empty_current.target_reached = true;
	empty_current.stop_reason = ccz::preview::ReplayStopReason::ReachedEnd;
	const ccz::preview::NativeReplayPlan inherited =
		ccz::editor_preview::ComposeFreeRReplayPlan(
			prefix, trigger_prefixes, empty_current);
	if (!inherited.complete) {
		if (!inherited.diagnostic.empty()) {
			AppendDiagnostic(CString(inherited.diagnostic.c_str()));
		}
		return false;
	}

	// Resolve the entire inherited prefix before clearing the visible scene.
	// This keeps a stale TreeView handle from leaving a partially applied
	// Free-R snapshot when a document refresh races with playback start.
	std::vector<ResolvedNativeReplayItem> resolved;
	resolved.reserve(inherited.items.size());
	for (const ccz::preview::NativeReplayItem& entry : inherited.items) {
		const HTREEITEM item = FindCurrentTreeItem(
			tree, reinterpret_cast<HTREEITEM>(entry.runtime_handle));
		if (item == nullptr) {
			AppendDiagnostic(_T("自由R前置场景节点已失效"));
			return false;
		}
		ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(item));
		if (data == nullptr ||
			data->id != static_cast<int>(entry.command.instruction_id)) {
			AppendDiagnostic(_T("自由R前置场景节点与重放计划不一致"));
			return false;
		}
		resolved.push_back({item, data});
	}

	m_state.Clear();
	// The inherited Free-R plan is now a real Section-level replay plan.  Keep
	// dynamic actor commands enabled so 0x31/0x32/0x33/0x34 settle the same
	// final state as the source Scene instead of leaving a stale snapshot.
	m_limitedFreeRReplay = false;
	m_pMapBitmap = nullptr;
	m_cachedMapID = -1;
	for (const ResolvedNativeReplayItem& entry : resolved) {
		ProcessInstruction(entry.data, entry.item);
	}
	EnsureDynamicAssets();
	return true;
}

NativePlanApplyResult CRScenePreviewPage::ApplyNativeReplayPlan(
    CTreeCtrl& tree, const ccz::preview::NativeReplayPlan& plan)
{
	NativePlanApplyResult result;
	ccz::preview::NativeReplayPlan effectivePlan = plan;
	const bool specialPlanHandled = ComposeSpecialReplayPlan(
		tree, plan, effectivePlan);
	if (!specialPlanHandled) {
		result.diagnostic = effectivePlan.diagnostic;
		result.applied = false;
		return result;
	}
	result.diagnostic = effectivePlan.diagnostic;
	const bool freeRComposite =
		m_cachedSceneResolutionValid &&
		m_cachedSceneResolution.kind ==
			ccz::editor_preview::ScenePreviewKind::FreeR &&
		m_cachedSceneResolution.has_click_target &&
		m_cachedSceneResolution.resolved;

	// Resolve every opaque handle before touching m_state.  This is a
	// transaction boundary: one stale item rejects the whole plan, so valid
	// entries before it cannot leave a half-cleared or half-replayed scene.
	std::vector<ResolvedNativeReplayItem> resolved;
	resolved.reserve(effectivePlan.items.size());
	for (const ccz::preview::NativeReplayItem& entry : effectivePlan.items) {
		const HTREEITEM item = FindCurrentTreeItem(
			tree, reinterpret_cast<HTREEITEM>(entry.runtime_handle));
		if (item == nullptr) {
			AppendNativePlanDiagnostic(result.diagnostic,
				L"预览节点已刷新，运行时句柄不属于当前 TreeView");
			result.applied = false;
			result.applied_count = 0;
			return result;
		}

		ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(item));
		if (data == nullptr) {
			AppendNativePlanDiagnostic(result.diagnostic,
				L"预览节点缺少当前 TreeView 数据");
			result.applied = false;
			result.applied_count = 0;
			return result;
		}
		if (data->id != static_cast<int>(entry.command.instruction_id)) {
			AppendNativePlanDiagnostic(result.diagnostic,
				L"预览节点指令编号与重放计划不一致");
			result.applied = false;
			result.applied_count = 0;
			return result;
		}
		resolved.push_back({ item, data });
	}

	// target_reached/complete describe the core's explicit waiting or
	// unsupported result; they are not handle validity signals.  A valid empty
	// plan is therefore still an applied plan and clears the native scene.
	if (freeRComposite && !effectivePlan.complete) {
		m_state.Clear();
		result.applied = true;
		result.applied_count = 0;
		return result;
	}
	// A composed Free-R plan contains the complete ordered native command
	// stream (including actor disappearance/movement/turn/action).  The former
	// limited mode skipped those commands and was the source of stale actors.
	m_limitedFreeRReplay = false;
	m_state.Clear();
	m_pMapBitmap = nullptr;
	m_cachedMapID = -1;
	// A valid native plan represents only the current Section.  Special
	// adjacent Scene pairs still need their bounded initial-Scene prefix, but
	// that prefix must be composed before this Section's plan is replayed.
	HTREEITEM hSection = m_hCurItem;
	while (hSection != nullptr) {
		ItemData* sectionData = reinterpret_cast<ItemData*>(
			tree.GetItemData(hSection));
		if (sectionData != nullptr && sectionData->id == -2) break;
		hSection = tree.GetParentItem(hSection);
	}
	SpecialScenePrefixResult prefixResult =
		SpecialScenePrefixResult::NotSpecial;
	if (!freeRComposite && hSection != nullptr && m_hCurItem != nullptr) {
		prefixResult = PrepareSpecialScenePrefix(
			tree, hSection, m_hCurItem,
			m_modPath.IsEmpty() ? nullptr : (LPCWSTR)m_modPath);
	}
	if (prefixResult == SpecialScenePrefixResult::SpecialUnresolved) {
		// The legacy resolver recognized a bounded special Scene but could not
		// resolve its paired prefix.  Keep the cleared state and Free-R limit,
		// publish the diagnostic, and do not replay this Section by accident.
		AppendNativePlanDiagnostic(result.diagnostic,
			L"特殊 Scene 前缀未解析，已跳过当前 Section 原生回放");
		result.applied = true;
		result.applied_count = 0;
		return result;
	}
	for (const ResolvedNativeReplayItem& entry : resolved) {
		ProcessInstruction(entry.data, entry.item);
	}
	result.applied = true;
	result.applied_count = resolved.size();
	return result;
}

void CRScenePreviewPage::HideInternalizedEnhancementViews(
	const std::wstring& diagnostic)
{
	// The legacy fallback is bounded to this refresh.  Keep its native scene,
	// but make the old condition/variable/dialogue outputs invisible so the
	// user cannot see two different replay states at once.
	m_internalizationFallbackActive = true;
	m_visibleDialogueOverlay = {};
	if (m_conditionTreePane.GetSafeHwnd() != nullptr) {
		m_conditionTreePane.UpdateModel(nullptr, {}, {});
		m_conditionTreePane.ShowPane(false);
	}
	if (m_variableControlPane.GetSafeHwnd() != nullptr) {
		m_variableControlPane.UpdateRows({}, {}, {});
		m_variableControlPane.ShowPane(false);
	}
	m_conditionPaneVisible = false;
	m_conditionButtonVisible = false;
	m_activationButtonVisible = false;
	m_variablePaneVisible = false;
	m_variableButtonVisible = false;
	if (m_conditionButton.GetSafeHwnd() != nullptr) {
		m_conditionButton.ShowWindow(SW_HIDE);
		m_conditionButton.EnableWindow(TRUE);
	}
	if (m_activationButton.GetSafeHwnd() != nullptr) {
		m_activationButton.ShowWindow(SW_HIDE);
		m_activationButton.EnableWindow(TRUE);
	}
	if (m_variableButton.GetSafeHwnd() != nullptr) {
		m_variableButton.ShowWindow(SW_HIDE);
		m_variableButton.EnableWindow(TRUE);
	}
	if (!diagnostic.empty()) {
		AppendDiagnostic(CString(diagnostic.c_str()));
	}
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::UpdateVariablePaneFromFrame()
{
	if (m_internalizationFallbackActive) {
		return;
	}
	if (m_variableControlPane.GetSafeHwnd() == nullptr) {
		return;
	}
	const auto& frame = m_previewSession.frame();
	const auto section_key = frame.model != nullptr
		? frame.model->section_key : ccz::preview::LogicalNodeKey();
	m_variableControlPane.UpdateRows(frame.variable_index,
		m_previewSession.options().simulated_variables, section_key);
	const bool show_button = frame.model != nullptr &&
		frame.variable_index.valid && m_variableControlPane.HasRows();
	m_variableButtonVisible = show_button;
	if (m_variableButton.GetSafeHwnd() != nullptr) {
		m_variableButton.ShowWindow(show_button ? SW_SHOW : SW_HIDE);
		m_variableButton.EnableWindow(TRUE);
	}
	if (!show_button) {
		m_variablePaneVisible = false;
		m_variableControlPane.ShowPane(false);
	}
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::UpdateConditionPaneFromFrame()
{
	if (m_internalizationFallbackActive) {
		UpdateActivationButtonFromFrame();
		return;
	}
	if (m_conditionTreePane.GetSafeHwnd() == nullptr) {
		UpdateActivationButtonFromFrame();
		return;
	}
	const auto& frame = m_previewSession.frame();
	m_conditionTreePane.UpdateModel(frame.model, frame.active_node,
		m_previewSession.options());
	const bool show_button = frame.model != nullptr &&
		m_conditionTreePane.HasControllableNodes();
	m_conditionButtonVisible = show_button;
	if (m_conditionButton.GetSafeHwnd() != nullptr) {
		m_conditionButton.ShowWindow(show_button ? SW_SHOW : SW_HIDE);
		m_conditionButton.EnableWindow(TRUE);
	}
	if (!show_button) {
		m_conditionPaneVisible = false;
		m_conditionTreePane.ShowPane(false);
	}
	UpdateActivationButtonFromFrame();
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::UpdateActivationButtonFromFrame()
{
	if (m_internalizationFallbackActive) {
		m_activationButtonVisible = false;
		if (m_activationButton.GetSafeHwnd() != nullptr) {
			m_activationButton.ShowWindow(SW_HIDE);
		}
		return;
	}
	const auto& frame = m_previewSession.frame();
	const bool show_button = frame.model != nullptr &&
		ccz::preview::condition_tree_model_can_activate_target(
			*frame.model, frame.active_node, m_previewSession.options());
	m_activationButtonVisible = show_button;
	if (m_activationButton.GetSafeHwnd() != nullptr) {
		m_activationButton.ShowWindow(show_button ? SW_SHOW : SW_HIDE);
		m_activationButton.EnableWindow(TRUE);
	}
	if (!show_button && m_conditionPaneVisible == false &&
		m_variablePaneVisible == false) {
		// LayoutConditionUi will collapse the empty top strip below.
	}
}

void CRScenePreviewPage::OnUpdateConditionButton(CCmdUI* pCmdUI)
{
	// These are child buttons managed by the preview pane itself.  MFC's
	// idle command update would otherwise disable a custom command ID when it
	// cannot find a document-level command handler.
	if (pCmdUI != nullptr) {
		pCmdUI->Enable(TRUE);
	}
}

void CRScenePreviewPage::OnUpdateActivationButton(CCmdUI* pCmdUI)
{
	if (pCmdUI != nullptr) {
		pCmdUI->Enable(TRUE);
	}
}

void CRScenePreviewPage::OnUpdateVariableButton(CCmdUI* pCmdUI)
{
	if (pCmdUI != nullptr) {
		pCmdUI->Enable(TRUE);
	}
}

void CRScenePreviewPage::OnUpdateUndoButton(CCmdUI* pCmdUI)
{
	if (pCmdUI != nullptr) {
		pCmdUI->Enable(!m_previewUndoStack.empty());
	}
}

void CRScenePreviewPage::OnUpdatePlaybackButton(CCmdUI* pCmdUI)
{
	if (pCmdUI != nullptr) {
		// These are child controls owned by the preview pane, not document
		// commands.  MFC disables custom command IDs that have no idle-update
		// handler, which made the visible player appear dead.  Mirror the
		// visibility contract here so a visible player remains clickable while
		// a hidden player is still treated as unavailable.
		pCmdUI->Enable(m_playbackVisible &&
			m_previewSession.frame().model != nullptr);
	}
}

int CRScenePreviewPage::ConditionUiHeight(int client_height) const noexcept
{
	// The condition tree and variable control are owned popup windows.  The
	// preview canvas must never reserve a bottom strip for either page; only
	// the small operation-button row is painted as an overlay above dialogue.
	(void)client_height;
	return 0;
}

CRect CRScenePreviewPage::SceneCanvasRect(const CRect& client_rect) const
{
	CRect canvas_rect = client_rect;
	const int client_top = static_cast<int>(client_rect.top);
	const int client_bottom = static_cast<int>(client_rect.bottom);
	canvas_rect.top = (std::min)(client_bottom,
		(std::max)(client_top, m_canvasTop));
	canvas_rect.bottom = (std::max)(static_cast<int>(canvas_rect.top),
		client_bottom - ConditionUiHeight(static_cast<int>(client_rect.Height())));
	return canvas_rect;
}

void CRScenePreviewPage::LayoutConditionUi(int width, int height)
{
	const int available = (std::max)(0, height - m_canvasTop);
	const int button_height = (std::min)(kConditionButtonHeight, available);
	const bool show_playback = m_playbackVisible && available > 0;
	const bool show_actions = m_conditionButtonVisible ||
		m_activationButtonVisible || m_variableButtonVisible ||
		m_undoButtonVisible;
	if ((!show_playback && !show_actions) || available <= 0) {
		if (m_conditionButton.GetSafeHwnd() != nullptr) {
			m_conditionButton.ShowWindow(SW_HIDE);
			m_conditionButton.EnableWindow(TRUE);
		}
		if (m_activationButton.GetSafeHwnd() != nullptr) {
			m_activationButton.ShowWindow(SW_HIDE);
			m_activationButton.EnableWindow(TRUE);
		}
		if (m_variableButton.GetSafeHwnd() != nullptr) {
			m_variableButton.ShowWindow(SW_HIDE);
			m_variableButton.EnableWindow(TRUE);
		}
		if (m_undoButton.GetSafeHwnd() != nullptr) {
			m_undoButton.ShowWindow(SW_HIDE);
			m_undoButton.EnableWindow(TRUE);
		}
		CWnd* playbackControls[] = {
			&m_playbackStartTargetButton, &m_playbackTargetEndButton,
			&m_playbackResetButton, &m_playbackPlayButton, &m_playbackStopButton,
			&m_playbackStepBackButton, &m_playbackStepForwardButton,
			&m_playbackSpeedButton, &m_playbackProgress, &m_playbackCounter};
		for (CWnd* control : playbackControls) {
			if (control != nullptr && control->GetSafeHwnd() != nullptr)
				control->ShowWindow(SW_HIDE);
		}
		return;
	}
	const int button_count = (m_conditionButtonVisible ? 1 : 0) +
		(m_activationButtonVisible ? 1 : 0) +
		(m_variableButtonVisible ? 1 : 0) +
		(m_undoButtonVisible ? 1 : 0);
	const int total_width = button_count * kConditionButtonWidth +
		(button_count > 0 ? (button_count - 1) * kConditionButtonGap : 0);
	// Calculate the same local dialogue panel used by OnPaint.  The buttons
	// sit immediately above it and fall back to the bottom edge only when the
	// client is too small to contain a complete dialogue panel.
	int top = height - kConditionButtonMargin - button_height;
	const CRect overlay_client(0, 0, width, available);
	ccz::editor_preview::OverlayLayout overlay_layout;
	if (ccz::editor_preview::CalculateDialogueOverlayLayout(
		overlay_client, overlay_layout)) {
		top = m_canvasTop + overlay_layout.panel.top -
			button_height - kConditionButtonMargin;
	}
	top = (std::max)(m_canvasTop,
		(std::min)(height - button_height, top));
	LayoutPlaybackUi(width, top, button_height);
	const int action_right = (std::max)(0, width - kConditionButtonMargin);
	const int action_left = (std::max)(0,
		action_right - total_width);
	int button_index = 0;
	if (m_conditionButton.GetSafeHwnd() != nullptr) {
		if (m_conditionButtonVisible) {
			m_conditionButton.MoveWindow(
				action_left + button_index * (kConditionButtonWidth +
					kConditionButtonGap), top, kConditionButtonWidth,
				button_height);
			m_conditionButton.ShowWindow(SW_SHOW);
			m_conditionButton.EnableWindow(TRUE);
			++button_index;
		} else {
			m_conditionButton.ShowWindow(SW_HIDE);
			m_conditionButton.EnableWindow(TRUE);
		}
	}
	if (m_activationButton.GetSafeHwnd() != nullptr) {
		if (m_activationButtonVisible) {
			m_activationButton.MoveWindow(
				action_left + button_index * (kConditionButtonWidth +
					kConditionButtonGap), top, kConditionButtonWidth,
				button_height);
			m_activationButton.ShowWindow(SW_SHOW);
			m_activationButton.EnableWindow(TRUE);
			++button_index;
		} else {
			m_activationButton.ShowWindow(SW_HIDE);
			m_activationButton.EnableWindow(TRUE);
		}
	}
	if (m_variableButton.GetSafeHwnd() != nullptr) {
		if (m_variableButtonVisible) {
			m_variableButton.MoveWindow(
				action_left + button_index * (kConditionButtonWidth +
					kConditionButtonGap), top, kConditionButtonWidth,
				button_height);
			m_variableButton.ShowWindow(SW_SHOW);
			m_variableButton.EnableWindow(TRUE);
			++button_index;
		} else {
			m_variableButton.ShowWindow(SW_HIDE);
			m_variableButton.EnableWindow(TRUE);
		}
	}
	if (m_undoButton.GetSafeHwnd() != nullptr) {
		if (m_undoButtonVisible) {
			m_undoButton.MoveWindow(
				action_left + button_index * (kConditionButtonWidth +
					kConditionButtonGap), top, kConditionButtonWidth,
				button_height);
			m_undoButton.ShowWindow(SW_SHOW);
			m_undoButton.EnableWindow(TRUE);
			++button_index;
		} else {
			m_undoButton.ShowWindow(SW_HIDE);
			m_undoButton.EnableWindow(TRUE);
		}
	}
}

int CRScenePreviewPage::PlaybackUiHeight() const noexcept
{
	return m_playbackVisible ? kPlaybackBarHeight : 0;
}

void CRScenePreviewPage::LayoutPlaybackUi(int width, int top, int height)
{
	CWnd* controls[] = {
		&m_playbackStartTargetButton, &m_playbackTargetEndButton,
		&m_playbackResetButton, &m_playbackPlayButton, &m_playbackStopButton,
		&m_playbackStepBackButton, &m_playbackStepForwardButton,
		&m_playbackSpeedButton, &m_playbackProgress, &m_playbackCounter};
	if (!m_playbackVisible || height <= 0) {
		for (CWnd* control : controls) {
			if (control != nullptr && control->GetSafeHwnd() != nullptr)
				control->ShowWindow(SW_HIDE);
		}
		return;
	}

	const int buttonHeight = (std::min)(height, kPlaybackButtonSize);
	const int y = top + (height - buttonHeight) / 2;
	int x = kConditionButtonMargin;
	auto placeButton = [&](CButton& button) {
		if (button.GetSafeHwnd() != nullptr) {
			button.MoveWindow(x, y, kPlaybackButtonSize, buttonHeight);
			button.ShowWindow(SW_SHOW);
			button.EnableWindow(TRUE);
		}
		x += kPlaybackButtonSize + kPlaybackGap;
	};
	placeButton(m_playbackStartTargetButton);
	placeButton(m_playbackTargetEndButton);
	placeButton(m_playbackResetButton);
	placeButton(m_playbackPlayButton);
	placeButton(m_playbackStopButton);
	placeButton(m_playbackStepBackButton);
	placeButton(m_playbackStepForwardButton);
	if (m_playbackSpeedButton.GetSafeHwnd() != nullptr) {
		m_playbackSpeedButton.MoveWindow(x, y, kPlaybackSpeedButtonWidth,
			buttonHeight);
		m_playbackSpeedButton.ShowWindow(SW_SHOW);
		m_playbackSpeedButton.EnableWindow(TRUE);
		x += kPlaybackSpeedButtonWidth + kPlaybackGap;
	}
	if (m_playbackProgress.GetSafeHwnd() != nullptr) {
		m_playbackProgress.MoveWindow(x, y + 5, kPlaybackProgressWidth,
			(std::max)(8, buttonHeight - 10));
		m_playbackProgress.ShowWindow(SW_SHOW);
		m_playbackProgress.EnableWindow(TRUE);
		x += kPlaybackProgressWidth + kPlaybackGap;
	}
	if (m_playbackCounter.GetSafeHwnd() != nullptr) {
		m_playbackCounter.MoveWindow(x, y, kPlaybackCounterWidth, buttonHeight);
		m_playbackCounter.ShowWindow(SW_SHOW);
		m_playbackCounter.EnableWindow(TRUE);
	}
	// If the pane is too narrow, the right-side condition buttons take
	// precedence.  The playback bar is still useful in the normal dock width;
	// clipping is preferable to moving controls over the dialogue panel.
	(void)width;
}

CTreeCtrl* CRScenePreviewPage::ResolvePreviewTree(
	const PendingPreviewRequest& request)
{
	if (!request.valid() || request.treeGeneration != m_previewTreeGeneration ||
		!::IsWindow(request.treeWindow)) {
		return nullptr;
	}

	CcczEditor2View* view = GetActiveEditorView();
	if (!view || !view->GetDocument()) return nullptr;
	CcczEditor2Doc* document = view->GetDocument();
	CTreeCtrl& tree = view->GetTreeCtrl();
	if (tree.GetSafeHwnd() != request.treeWindow) return nullptr;
	const CString currentPath = document->pathName;
	if (currentPath.CompareNoCase(request.documentPath) != 0) return nullptr;
	if (document->GetPreviewDocumentId() != request.activation.document_id ||
		document->GetPreviewContentEpoch() != request.activation.content_epoch ||
		document->GetPreviewTreeGeneration() !=
			request.activation.tree_generation ||
		tree.GetSelectedItem() != request.selectedItem) return nullptr;

	// HTREEITEM is only an opaque value.  Validate it against the current
	// TreeView before passing it to MFC or the native replay adapter.
	if (request.selectedItem != nullptr) {
		TVITEM itemInfo{};
		itemInfo.mask = TVIF_HANDLE;
		itemInfo.hItem = request.selectedItem;
		if (!TreeView_GetItem(request.treeWindow, &itemInfo)) return nullptr;
	}
	return &tree;
}

void CRScenePreviewPage::ExecutePreviewRefresh(
	const PendingPreviewRequest& request)
{
	StopDynamicPlayback(false);
	CTreeCtrl* requestTree = ResolvePreviewTree(request);
	const std::wstring documentPath(request.documentPath.GetString());
	if (requestTree != nullptr) {
		m_pTreeCtrl = requestTree;
		m_hCurItem = request.selectedItem;
	}
	if (documentPath.empty()) {
		m_previewAssetCache.Clear();
	} else {
		std::wstring assetDiagnostic;
		m_previewAssetCache.LoadForDocument(documentPath, m_dialogueProfile,
			&assetDiagnostic);
	}
	// The native editor resolves instruction labels from the editor package's
	// CczString.ini (the asset cache uses the running EXE directory).  Keep the
	// built-in fallback for a missing/invalid package file, then merge a valid
	// catalog so custom instruction names are not silently reduced to the
	// standard subset.
	m_previewCatalog = ccz::editor_preview::MakeEditorPreviewCatalog();
	const auto& documentCatalog = m_previewAssetCache.instruction_catalog();
	if (documentCatalog.valid) {
		for (const auto& entry : documentCatalog.names) {
			m_previewCatalog.names[entry.first] = entry.second;
		}
		m_previewCatalog.valid = true;
	}
	if (!requestTree) {
		m_hasActiveBaseline = false;
		m_previewSession.ResetForDocument(documentPath);
		ClearNativePreviewState();
		return;
	}

	const ccz::editor_preview::EditorPreviewCapture capture =
		ccz::editor_preview::CaptureEditorPreviewInput(
			*requestTree, request.selectedItem, documentPath);
	if (capture.ok && !capture.idle) {
		// S battle scripts are handled only by CSBattlePreviewPage.  This page
		// intentionally has no Battle classifier/parser fallback.
		m_lastPreviewInput = capture.input;
		m_lastEditorTreeSnapshot = capture.snapshot;
		m_hasEditorTreeSnapshot = !capture.snapshot.document_roots.empty();
		m_cachedSceneResolution = {};
		m_cachedSceneResolutionValid = false;
		m_cachedFreeRPrefixPlan = {};
		m_cachedFreeRPrefixValid = false;
		const ccz::editor_preview::RefreshResult result = m_previewSession.Refresh(
			capture.input, m_previewCatalog, m_dialogueProfile,
			m_previewAssetCache.view());
		const auto action = ccz::editor_preview::DecidePreviewRefreshAction(
			result.status, result.frame_changed, true);
		if (action == ccz::editor_preview::PreviewRefreshAction::ClearAndRepaint) {
			m_hasActiveBaseline = false;
			m_previewSession.ResetForDocument(documentPath);
			ClearNativePreviewState();
			return;
		}
		if (action == ccz::editor_preview::PreviewRefreshAction::SkipRepaint) {
			m_activeActivation = request.activation;
			m_activeDocumentId = request.activation.document_id;
			m_hasActiveBaseline = true;
			return;
		}
	} else {
		// Idle/root selections and failed captures must not leave the prior
		// document frame visible in the shared session.
		m_hasActiveBaseline = capture.ok && capture.idle;
		m_activeActivation = request.activation;
		m_activeDocumentId = request.activation.document_id;
		m_previewSession.ResetForDocument(documentPath);
		ClearNativePreviewState();
		return;
	}

	// Keep the existing native renderer as the final display/writeback path;
	// the value/session capture above is the new refresh coordination boundary.
	UpdatePreview(*requestTree, request.selectedItem,
		request.documentPath.IsEmpty() ? nullptr : request.documentPath.GetString());
	UpdateConditionPaneFromFrame();
	UpdateVariablePaneFromFrame();
	RefreshDynamicTimelineForCurrentTarget();
	m_playbackVisible = m_previewSession.frame().model != nullptr;
	UpdatePlaybackControls();
	m_activeActivation = request.activation;
	m_activeDocumentId = request.activation.document_id;
	m_hasActiveBaseline = true;
}

void CRScenePreviewPage::ExecutePendingPreviewRefresh(
	const ccz::editor_preview::PreviewRefreshStateMachine::Request&)
{
	if (!m_pendingPreviewRequest.valid()) return;
	PendingPreviewRequest request = m_pendingPreviewRequest;
	m_pendingPreviewRequest.clear();
	ExecutePreviewRefresh(request);
}

void CRScenePreviewPage::RequestPreviewRefresh(
	CTreeCtrl& tree, HTREEITEM selected_item, CcczEditor2Doc& document)
{
	const HWND treeWindow = tree.GetSafeHwnd();
	if (treeWindow == nullptr || !::IsWindow(treeWindow)) return;
	if (m_activeDocumentId != document.GetPreviewDocumentId()) {
		bool reusedSession = false;
		if (!ActivateDocument(tree, selected_item, document, reusedSession)) return;
	}
	const auto activation = MakeRActivationSnapshot(
		document, tree, selected_item);
	m_pendingPreviewRequest.treeWindow = treeWindow;
	m_pendingPreviewRequest.selectedItem = selected_item;
	m_pendingPreviewRequest.documentPath = document.pathName ? document.pathName : L"";
	m_pendingPreviewRequest.activation = activation;
	m_pendingPreviewRequest.treeGeneration = m_previewTreeGeneration;
	m_activeDocumentId = activation.document_id;
	m_activeActivation = activation;
	m_hasActiveBaseline = false;

	const auto requestKey = ++m_previewRefreshSequence;
	const ccz::editor_preview::PreviewRefreshStateMachine::Request stateRequest{
		requestKey, m_previewRefreshState.generation()};
	m_previewRefreshState.Submit(
		stateRequest,
		[this](const ccz::editor_preview::PreviewRefreshStateMachine::Request& request) {
			ExecutePendingPreviewRefresh(request);
		},
		[this]() {
			return PostMessage(kPreviewRefreshMessage, 0, 0) != FALSE;
		});
}

void CRScenePreviewPage::RequestPreviewRefreshForCurrentDocument(
	CTreeCtrl& tree, HTREEITEM selectedItem)
{
	CcczEditor2View* view = GetActiveEditorView();
	if (view != nullptr && view->GetDocument() != nullptr &&
		view->GetTreeCtrl().GetSafeHwnd() == tree.GetSafeHwnd()) {
		RequestPreviewRefresh(tree, selectedItem, *view->GetDocument());
	}
}

LRESULT CRScenePreviewPage::OnPreviewRefreshMessage(WPARAM, LPARAM)
{
	m_previewRefreshState.OnPostedMessage(
		[this](const ccz::editor_preview::PreviewRefreshStateMachine::Request& request) {
			ExecutePendingPreviewRefresh(request);
		});
	return 0;
}

LRESULT CRScenePreviewPage::OnConditionActionMessage(WPARAM, LPARAM)
{
	RequestConditionActionRefresh();
	return 0;
}

LRESULT CRScenePreviewPage::OnVariableActionMessage(WPARAM, LPARAM)
{
	RequestVariableActionRefresh();
	return 0;
}

void CRScenePreviewPage::RequestConditionActionRefresh()
{
	if (m_pTreeCtrl != nullptr && m_pTreeCtrl->GetSafeHwnd() != nullptr &&
		m_hCurItem != nullptr) {
		RequestPreviewRefreshForCurrentDocument(*m_pTreeCtrl, m_hCurItem);
	}
}

void CRScenePreviewPage::RequestVariableActionRefresh()
{
	if (m_pTreeCtrl != nullptr && m_pTreeCtrl->GetSafeHwnd() != nullptr &&
		m_hCurItem != nullptr) {
		RequestPreviewRefreshForCurrentDocument(*m_pTreeCtrl, m_hCurItem);
	}
}

void CRScenePreviewPage::OnConditionButtonClicked()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	if (m_conditionTreePane.GetSafeHwnd() != nullptr &&
		m_conditionTreePane.HasControllableNodes()) {
		m_variablePaneVisible = false;
		m_variableControlPane.ShowPane(false);
		m_conditionPaneVisible = true;
		m_conditionTreePane.ShowPane(true);
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::OnActivationButtonClicked()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	const auto& frame = m_previewSession.frame();
	if (frame.model == nullptr || frame.active_node.empty()) {
		return;
	}
	std::wstring diagnostic;
	if (!m_previewSession.ActivateTarget(frame.active_node, &diagnostic)) {
		if (!diagnostic.empty()) {
			AppendDiagnostic(CString(diagnostic.c_str()));
		}
		return;
	}
	if (PostMessage(kConditionActionMessage, 0, 0) == FALSE) {
		RequestConditionActionRefresh();
	}
}

void CRScenePreviewPage::OnVariableButtonClicked()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	if (m_variableControlPane.GetSafeHwnd() != nullptr &&
		m_variableControlPane.HasRows()) {
		m_conditionPaneVisible = false;
		m_conditionTreePane.ShowPane(false);
		m_variablePaneVisible = true;
		m_variableControlPane.ShowPane(true);
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

void CRScenePreviewPage::OnUndoButtonClicked()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	UndoLastPreviewOperation();
}

void CRScenePreviewPage::OnDestroy()
{
	StopDynamicPlayback(false);
	InvalidatePendingPreviewRefresh();
	if (m_conditionTreePane.GetSafeHwnd() != nullptr) {
		m_conditionTreePane.DestroyWindow();
	}
	if (m_variableControlPane.GetSafeHwnd() != nullptr) {
		m_variableControlPane.DestroyWindow();
	}
	CWnd::OnDestroy();
}

int CRScenePreviewPage::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rc;
	GetClientRect(&rc);

	m_editSearch.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		CRect(0, 0, rc.Width(), 24), this, IDC_SCENE_CHAR_SEARCH);
	m_editSearch.SetWindowTextW(L"");

	m_listChars.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
		CRect(0, 24, rc.Width(), 120), this, IDC_SCENE_CHAR_LIST);

	m_listFont.CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, _T("Microsoft YaHei"));
	m_editSearch.SetFont(&m_listFont);
	m_listChars.SetFont(&m_listFont);

	m_conditionButton.Create(L"条件树", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(0, 0, 1, 1), this, kConditionButtonId);
	m_conditionButton.SetFont(&m_listFont);
	// Make the initial state explicit.  ClearNativePreviewState may disable
	// these controls before the first valid frame; a later visible refresh
	// must always start from an enabled child state.
	m_conditionButton.EnableWindow(TRUE);
	m_activationButton.Create(L"激活子事件",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kActivationButtonId);
	m_activationButton.SetFont(&m_listFont);
	m_activationButton.EnableWindow(TRUE);
	m_variableButton.Create(L"变量控制",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kVariableButtonId);
	m_variableButton.SetFont(&m_listFont);
	m_variableButton.EnableWindow(TRUE);
	m_undoButton.Create(L"撤回", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(0, 0, 1, 1), this, kUndoButtonId);
	m_undoButton.SetFont(&m_listFont);
	m_undoButton.EnableWindow(TRUE);

	// Dynamic preview uses icon-only controls so the bar reads like a small
	// scene player rather than a second row of text commands.  Tooltips carry
	// the Chinese explanation without consuming permanent layout width.
	m_playbackFont.CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, _T("Segoe UI Symbol"));
	auto createPlaybackButton = [&](CButton& button, const wchar_t* glyph,
		UINT id) {
		button.Create(glyph, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER,
			CRect(0, 0, 1, 1), this, id);
		button.SetFont(&m_playbackFont);
		button.EnableWindow(TRUE);
	};
	createPlaybackButton(m_playbackStartTargetButton, L"◀▶●",
		kPlaybackStartTargetButtonId);
	createPlaybackButton(m_playbackTargetEndButton, L"●▶▶",
		kPlaybackTargetEndButtonId);
	createPlaybackButton(m_playbackResetButton, L"↺", kPlaybackResetButtonId);
	createPlaybackButton(m_playbackPlayButton, L"▶",
		kPlaybackPlayButtonId);
	createPlaybackButton(m_playbackStopButton, L"■",
		kPlaybackStopButtonId);
	createPlaybackButton(m_playbackStepBackButton, L"◀|",
		kPlaybackStepBackButtonId);
	createPlaybackButton(m_playbackStepForwardButton, L"|▶",
		kPlaybackStepForwardButtonId);
	createPlaybackButton(m_playbackSpeedButton, L"1x",
		kPlaybackSpeedButtonId);
	m_playbackProgress.Create(WS_CHILD | PBS_SMOOTH, CRect(0, 0, 1, 1),
		this, kPlaybackSpeedButtonId + 1);
	m_playbackCounter.Create(L"0/0", WS_CHILD | SS_CENTER,
		CRect(0, 0, 1, 1), this, kPlaybackSpeedButtonId + 2);
	m_playbackCounter.SetFont(&m_playbackFont);
	m_playbackToolTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
	if (m_playbackToolTip.GetSafeHwnd() != nullptr) {
		m_playbackToolTip.SetMaxTipWidth(260);
		m_playbackToolTip.AddTool(&m_playbackStartTargetButton,
			L"从开头播放到当前选定节点");
		m_playbackToolTip.AddTool(&m_playbackTargetEndButton,
			L"从当前选定节点播放到结尾");
		m_playbackToolTip.AddTool(&m_playbackResetButton,
			L"回到当前播放模式起点");
		m_playbackToolTip.AddTool(&m_playbackPlayButton, L"播放/暂停");
		m_playbackToolTip.AddTool(&m_playbackStopButton, L"停止并恢复静态预览");
		m_playbackToolTip.AddTool(&m_playbackStepBackButton, L"后退一步");
		m_playbackToolTip.AddTool(&m_playbackStepForwardButton, L"推进一步");
		m_playbackToolTip.AddTool(&m_playbackSpeedButton, L"切换播放速度");
	}
	const bool condition_pane_created = m_conditionTreePane.CreatePane(this,
		[this](const ccz::preview::LogicalNodeKey& node_key,
			ccz::preview::ConditionTreeAction action,
			std::int32_t integer_value) {
			if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
			std::wstring diagnostic;
			if (m_previewSession.ApplyConditionAction(
				node_key, action, integer_value, &diagnostic)) {
				// The pane callback ends after the session transaction.  The
				// existing event-driven refresh boundary runs once afterward.
				if (PostMessage(kConditionActionMessage, 0, 0) == FALSE) {
					// A failed asynchronous handoff must not lose the session
					// mutation.  Use the same refresh state machine immediately.
					RequestConditionActionRefresh();
				}
			}
		});
	const bool variable_pane_created = m_variableControlPane.CreatePane(this,
		[this](ccz::preview::VariableControlKind kind,
			std::int32_t id,
			ccz::preview::VariableControlAction action,
			std::int32_t integer_value) {
			if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
			std::wstring diagnostic;
			if (m_previewSession.ApplyVariableAction(
				kind, id, action, integer_value, &diagnostic)) {
				// The pane callback ends after the Session transaction.  Exactly
				// one event-driven refresh consumes the shared state snapshot.
				if (PostMessage(kVariableActionMessage, 0, 0) == FALSE) {
					RequestVariableActionRefresh();
				}
			} else if (!diagnostic.empty()) {
				AppendDiagnostic(CString(diagnostic.c_str()));
			}
		});
	m_variableControlPane.SetDiagnosticHandler(
		[this](const std::wstring& diagnostic) {
			if (!diagnostic.empty()) {
				AppendDiagnostic(CString(diagnostic.c_str()));
			}
		});
	m_conditionButton.ShowWindow(SW_HIDE);
	m_activationButton.ShowWindow(SW_HIDE);
	m_variableButton.ShowWindow(SW_HIDE);
	m_undoButton.ShowWindow(SW_HIDE);
	m_playbackStartTargetButton.ShowWindow(SW_HIDE);
	m_playbackTargetEndButton.ShowWindow(SW_HIDE);
	m_playbackResetButton.ShowWindow(SW_HIDE);
	m_playbackPlayButton.ShowWindow(SW_HIDE);
	m_playbackStopButton.ShowWindow(SW_HIDE);
	m_playbackStepBackButton.ShowWindow(SW_HIDE);
	m_playbackStepForwardButton.ShowWindow(SW_HIDE);
	m_playbackSpeedButton.ShowWindow(SW_HIDE);
	m_playbackProgress.ShowWindow(SW_HIDE);
	m_playbackCounter.ShowWindow(SW_HIDE);
	if (!condition_pane_created) {
		AppendDiagnostic(_T("条件树面板创建失败"));
	}
	if (!variable_pane_created) {
		AppendDiagnostic(_T("变量控制面板创建失败"));
	}

	return 0;
}

void CRScenePreviewPage::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);

	const int SEARCH_H = 24;
	const int LIST_H = 160;
	m_canvasTop = SEARCH_H + LIST_H;

	if (m_editSearch.GetSafeHwnd())
		m_editSearch.MoveWindow(0, 0, cx, SEARCH_H);
	if (m_listChars.GetSafeHwnd())
		m_listChars.MoveWindow(0, SEARCH_H, cx, LIST_H);
	LayoutConditionUi(cx, cy);

	Invalidate();
}

void CRScenePreviewPage::UpdatePlaybackControls()
{
	const bool show = m_playbackVisible &&
		m_previewSession.frame().model != nullptr;
	CWnd* controls[] = {
		&m_playbackStartTargetButton, &m_playbackTargetEndButton,
		&m_playbackResetButton, &m_playbackPlayButton, &m_playbackStopButton,
		&m_playbackStepBackButton, &m_playbackStepForwardButton,
		&m_playbackSpeedButton, &m_playbackProgress, &m_playbackCounter};
	for (CWnd* control : controls) {
		if (control != nullptr && control->GetSafeHwnd() != nullptr) {
			control->ShowWindow(show ? SW_SHOW : SW_HIDE);
			control->EnableWindow(show ? TRUE : FALSE);
		}
	}
	if (!show) {
		m_playbackVisible = false;
	}
	if (m_playbackPlayButton.GetSafeHwnd() != nullptr) {
		const auto state = m_dynamicPlaybackSession.state();
		m_playbackPlayButton.SetWindowTextW(
			state == ccz::editor_preview::DynamicPlaybackState::Playing
				? L"Ⅱ" : L"▶");
	}
	if (m_playbackSpeedButton.GetSafeHwnd() != nullptr) {
		const double speed = m_dynamicPlaybackSession.speed();
		CString speed_text;
		if (std::fabs(speed - std::round(speed)) < 0.001) {
			speed_text.Format(L"%.0fx", speed);
		} else {
			speed_text.Format(L"%.1fx", speed);
		}
		m_playbackSpeedButton.SetWindowTextW(speed_text);
	}
	if (m_playbackProgress.GetSafeHwnd() != nullptr) {
		const int total = static_cast<int>(m_dynamicTimeline.events.size());
		m_playbackProgress.SetRange32(0, (std::max)(0, total));
		const auto sessionState = m_dynamicPlaybackSession.state();
		const bool hasDynamicCursor = m_dynamicPlaybackActive ||
			m_dynamicPlaybackPrepared ||
			sessionState != ccz::editor_preview::DynamicPlaybackState::Stopped;
		const std::size_t current = hasDynamicCursor
			? m_dynamicPlaybackSession.current_event_index()
			: m_dynamicStaticProgressIndex;
		m_playbackProgress.SetPos(static_cast<int>((std::min)(
			current, static_cast<std::size_t>((std::max)(0, total)))));
	}
	if (m_playbackCounter.GetSafeHwnd() != nullptr) {
		CString counter;
		const int total = static_cast<int>(m_dynamicTimeline.events.size());
		const auto sessionState = m_dynamicPlaybackSession.state();
		const bool hasDynamicCursor = m_dynamicPlaybackActive ||
			m_dynamicPlaybackPrepared ||
			sessionState != ccz::editor_preview::DynamicPlaybackState::Stopped;
		const std::size_t cursor = hasDynamicCursor
			? m_dynamicPlaybackSession.current_event_index()
			: m_dynamicStaticProgressIndex;
		const int current = static_cast<int>((std::min)(
			cursor,
			static_cast<std::size_t>((std::max)(0, total))));
		counter.Format(L"%d/%d", current, total);
		m_playbackCounter.SetWindowTextW(counter);
	}
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
	}
}

bool CRScenePreviewPage::RefreshDynamicTimelineForCurrentTarget()
{
	const auto& frame = m_previewSession.frame();
	m_dynamicStaticProgressIndex = 0;
	if (frame.model == nullptr || frame.active_node.empty()) {
		m_dynamicTimeline = {};
		return false;
	}

	const auto timeline = ccz::preview::build_playback_timeline(
		*frame.model, frame.active_node, m_dynamicPlaybackRange,
		m_previewSession.options());
	if (!timeline.complete) {
		m_dynamicTimeline = {};
		return false;
	}
	m_dynamicTimeline = timeline;
	if (m_dynamicTimeline.events.empty()) return true;
	if (m_dynamicPlaybackRange == ccz::preview::PlaybackRange::StartToTarget) {
		m_dynamicStaticProgressIndex = (std::min)(
			m_dynamicTimeline.events.size(),
			m_dynamicTimeline.target_end_index);
	} else {
		// Target-to-end playback starts immediately before the selected event,
		// but the persistent progress bar is absolute to the Section origin.
		m_dynamicStaticProgressIndex = m_dynamicTimeline.target_index;
	}
	return true;
}

void CRScenePreviewPage::EnsureDynamicAssets()
{
	if (!m_modPath.IsEmpty() && m_state.mapImageIndex >= 0 &&
		m_state.mapImageIndex != m_cachedMapID) {
		LoadMapImage(m_state.mapImageIndex, m_modPath);
	}
	if (!m_modPath.IsEmpty() && !m_spriteDataLoaded) {
		LoadSpriteData(m_modPath);
	}
	if (m_modPath.IsEmpty()) return;
	for (const auto& character : m_state.characters) {
		GetSpriteBitmap(character.id, m_modPath);
	}
	for (const auto& portrait : m_state.mapPortraits.items()) {
		GetPortraitBitmap(portrait.personID, m_modPath);
	}
}

bool CRScenePreviewPage::BuildDynamicFrameAt(
    const ccz::preview::LogicalNodeKey& target,
    ccz::editor_preview::PreviewFrame& frame,
    std::optional<std::size_t> dialogue_segment_index) const
{
	const auto& current = m_previewSession.frame();
	if (current.model == nullptr || target.empty() ||
		m_lastPreviewInput.document_key.empty()) {
		return false;
	}
	ccz::editor_preview::EditorPreviewInput input = m_lastPreviewInput;
	input.section.selected_target_key = target;
	frame = ccz::editor_preview::BuildPreviewFrame(
		input, *current.model, m_previewSession.options(), m_previewCatalog,
		m_dialogueProfile, m_previewAssetCache.view(), dialogue_segment_index);
	return frame.native_plan.complete;
}

bool CRScenePreviewPage::PublishDynamicFrame(
	const ccz::editor_preview::PreviewFrame& frame)
{
	if (m_pTreeCtrl == nullptr || m_pTreeCtrl->GetSafeHwnd() == nullptr) {
		return false;
	}
	const NativePlanApplyResult applied =
		ApplyNativeReplayPlan(*m_pTreeCtrl, frame.native_plan);
	if (!applied.applied) {
		if (!applied.diagnostic.empty()) {
			AppendDiagnostic(CString(applied.diagnostic.c_str()));
		}
		return false;
	}
	m_visibleDialogueOverlay = frame.dialogue;
	EnsureDynamicAssets();
	return true;
}

SceneState CRScenePreviewPage::DynamicPaintState() const
{
	if (!m_dynamicPlaybackActive || !m_dynamicPlaybackPrepared ||
		(m_dynamicSegmentKind !=
			ccz::preview::PlaybackSegmentKind::CharacterMove &&
		 m_dynamicSegmentKind !=
			ccz::preview::PlaybackSegmentKind::PortraitMove)) {
		return m_state;
	}
	SceneState result = m_dynamicFromState;
	const double progress = (std::max)(0.0,
		(std::min)(1.0, m_dynamicVisualProgress));
	if (m_dynamicSegmentKind ==
		ccz::preview::PlaybackSegmentKind::CharacterMove) {
		for (auto& character : result.characters) {
			const auto found = std::find_if(m_dynamicToState.characters.begin(),
				m_dynamicToState.characters.end(),
				[&character](const CharacterState& candidate) {
					return candidate.id == character.id;
				});
			if (found == m_dynamicToState.characters.end()) continue;
			const ccz::editor_preview::PlaybackPose from{
				{character.x, character.y}, character.direction, character.action};
			const ccz::editor_preview::PlaybackPose to{
				{found->x, found->y}, found->direction, found->action};
			const auto sampled = ccz::editor_preview::sample_playback_pose(
				from, to, progress);
			character.x = sampled.position.x;
			character.y = sampled.position.y;
			character.direction = sampled.direction;
			character.action = sampled.action;
		}
	} else {
		for (auto& portrait : result.mapPortraits.items()) {
			const auto found = m_dynamicToState.mapPortraits.find(
				portrait.personID);
			if (found == nullptr) continue;
			portrait.x = static_cast<int>(std::lround(
				static_cast<double>(portrait.x) +
				(static_cast<double>(found->x - portrait.x) * progress)));
			portrait.y = static_cast<int>(std::lround(
				static_cast<double>(portrait.y) +
				(static_cast<double>(found->y - portrait.y) * progress)));
		}
	}
	return result;
}

void CRScenePreviewPage::PrepareDynamicPlaybackEvent(std::size_t event_index)
{
	if (event_index >= m_dynamicTimeline.events.size()) {
		m_dynamicPlaybackPrepared = false;
		return;
	}
	const auto& event = m_dynamicTimeline.events[event_index];
	const SceneState from_state = m_state;
	const auto from_dialogue = m_visibleDialogueOverlay;
	ccz::editor_preview::PreviewFrame target_frame;
	const std::optional<std::size_t> dialogue_segment_index =
		(event.command.instruction_id == 0x14 &&
		 event.dialogue_segment_count > 0)
			? std::optional<std::size_t>(event.dialogue_segment_index)
			: std::nullopt;
	if (!BuildDynamicFrameAt(event.logical_key, target_frame,
			dialogue_segment_index) ||
		!PublishDynamicFrame(target_frame)) {
		m_dynamicPlaybackPrepared = false;
		m_dynamicPlaybackSession.Stop();
		m_dynamicPlaybackActive = false;
		KillTimer(kDynamicPlaybackTimerId);
		return;
	}
	m_dynamicFromState = from_state;
	m_dynamicFromDialogue = from_dialogue;
	m_dynamicToState = m_state;
	m_dynamicToDialogue = target_frame.dialogue;
	m_state = from_state;
	// Dialogue is a held segment: publish its text at segment start, while
	// movement/instant segments keep the preceding overlay until arrival.
	m_visibleDialogueOverlay =
		event.kind == ccz::preview::PlaybackSegmentKind::Hold
			? target_frame.dialogue : from_dialogue;
	EnsureDynamicAssets();
	m_dynamicPreparedIndex = event_index;
	m_dynamicSegmentKind = event.kind;
	m_dynamicVisualProgress = 0.0;
	m_dynamicPlaybackPrepared = true;
	// Keep the editor tree anchored to the same logical event as the preview,
	// including automatic playback and both step directions.  The one-shot
	// guard prevents this programmatic selection from restarting playback.
	SyncPlaybackTreeSelection(true);
}

void CRScenePreviewPage::CommitDynamicPlaybackEvent()
{
	if (!m_dynamicPlaybackPrepared) return;
	m_state = m_dynamicToState;
	m_visibleDialogueOverlay = m_dynamicToDialogue;
	m_dynamicVisualProgress = 1.0;
	EnsureDynamicAssets();
}

bool CRScenePreviewPage::RewindDynamicPlaybackOneEvent()
{
	using ccz::editor_preview::DynamicPlaybackState;
	if (m_dynamicTimeline.events.empty() ||
		m_dynamicPlaybackSession.state() == DynamicPlaybackState::Stopped) {
		return false;
	}
	const std::size_t range_begin = m_dynamicTimeline.range_begin;
	const std::size_t range_end = m_dynamicTimeline.range_end;
	if (range_begin >= range_end ||
		range_end > m_dynamicTimeline.events.size()) {
		return false;
	}
	const std::size_t current =
		m_dynamicPlaybackSession.current_event_index();
	if (current <= range_begin &&
		m_dynamicPlaybackSession.state() != DynamicPlaybackState::Finished) {
		return false;
	}
	const std::size_t desired = current >= range_end
		? range_end - 1 : current - 1;

	// Build the state immediately before the desired event.  This is a direct
	// state transition: no visible replay is performed from the Section origin.
	if (desired == range_begin) {
		m_state = m_dynamicInitialState;
		m_visibleDialogueOverlay = m_dynamicInitialDialogue;
		EnsureDynamicAssets();
	} else {
		const auto& before_event = m_dynamicTimeline.events[desired - 1];
		const std::optional<std::size_t> dialogue_segment_index =
			(before_event.command.instruction_id == 0x14 &&
			 before_event.dialogue_segment_count > 0)
				? std::optional<std::size_t>(before_event.dialogue_segment_index)
				: std::nullopt;
		ccz::editor_preview::PreviewFrame before_frame;
		if (!BuildDynamicFrameAt(before_event.logical_key, before_frame,
				dialogue_segment_index) ||
			!PublishDynamicFrame(before_frame)) {
			return false;
		}
	}

	m_dynamicPlaybackPrepared = false;
	if (!m_dynamicPlaybackSession.StepBackward()) return false;
	// A finished session is made live again by a backward step.
	m_dynamicPlaybackActive = true;
	PrepareDynamicPlaybackEvent(desired);
	return m_dynamicPlaybackPrepared;
}

bool CRScenePreviewPage::StartDynamicPlayback(
	ccz::preview::PlaybackRange range)
{
	StopDynamicPlayback(false);
	const auto& frame = m_previewSession.frame();
	if (frame.model == nullptr || frame.active_node.empty() ||
		m_pTreeCtrl == nullptr || m_lastPreviewInput.document_key.empty()) {
		return false;
	}
	m_dynamicTimeline = ccz::preview::build_playback_timeline(
		*frame.model, frame.active_node, range,
		m_previewSession.options());
	if (!m_dynamicTimeline.complete) {
		if (!m_dynamicTimeline.diagnostic.empty()) {
			AppendDiagnostic(CString(m_dynamicTimeline.diagnostic.c_str()));
		}
		UpdatePlaybackControls();
		return false;
	}
	m_dynamicPlaybackRange = range;
	if (!m_dynamicPlaybackSession.Start(m_dynamicTimeline, range)) {
		return false;
	}
	// Establish the state before the first event.  For target-to-end playback
	// the core records the last event before the target.  FreeR click branches
	// additionally inherit the completed appearance Section even when the
	// trigger Section has no preceding event.
	const bool freeRComposite =
		m_cachedSceneResolutionValid &&
		m_cachedSceneResolution.kind ==
			ccz::editor_preview::ScenePreviewKind::FreeR &&
		m_cachedSceneResolution.resolved &&
		m_cachedSceneResolution.has_click_target;
	if (range == ccz::preview::PlaybackRange::TargetToEnd &&
		!m_dynamicTimeline.pre_target_key.empty()) {
		ccz::editor_preview::PreviewFrame before_target;
		if (!BuildDynamicFrameAt(m_dynamicTimeline.pre_target_key, before_target) ||
			!PublishDynamicFrame(before_target)) {
			m_dynamicPlaybackSession.Stop();
			return false;
		}
	} else if (freeRComposite) {
		if (!ApplyFreeRPrefixOnly(*m_pTreeCtrl)) {
			m_dynamicPlaybackSession.Stop();
			return false;
		}
		m_visibleDialogueOverlay = {};
	} else {
		m_state.Clear();
		m_visibleDialogueOverlay = {};
		m_pMapBitmap = nullptr;
		m_cachedMapID = -1;
	}
	m_dynamicInitialState = m_state;
	m_dynamicInitialDialogue = m_visibleDialogueOverlay;
	m_dynamicPlaybackActive = true;
	m_dynamicPlaybackPrepared = false;
	m_dynamicLastTickMs = ::GetTickCount64();
	PrepareDynamicPlaybackEvent(m_dynamicTimeline.range_begin);
	if (!m_dynamicPlaybackActive) return false;
	SetTimer(kDynamicPlaybackTimerId, 16, nullptr);
	UpdatePlaybackControls();
	Invalidate(FALSE);
	return true;
}

void CRScenePreviewPage::StopDynamicPlayback(bool restore_static)
{
	const bool was_active = m_dynamicPlaybackActive ||
		m_dynamicPlaybackPrepared ||
		m_dynamicPlaybackSession.state() !=
			ccz::editor_preview::DynamicPlaybackState::Stopped;
	KillTimer(kDynamicPlaybackTimerId);
	m_dynamicPlaybackSession.Stop();
	m_dynamicPlaybackActive = false;
	m_dynamicPlaybackPrepared = false;
	m_dynamicVisualProgress = 0.0;
	m_dynamicTimeline = {};
	m_dynamicStaticProgressIndex = 0;
	if (restore_static && was_active && m_pTreeCtrl != nullptr &&
		m_hCurItem != nullptr && !m_modPath.IsEmpty()) {
		UpdatePreview(*m_pTreeCtrl, m_hCurItem, m_modPath);
		RefreshDynamicTimelineForCurrentTarget();
	}
	if (was_active) {
		m_dynamicInitialState = {};
		m_dynamicInitialDialogue = {};
		m_dynamicFromState = {};
		m_dynamicToState = {};
		m_dynamicFromDialogue = {};
		m_dynamicToDialogue = {};
	}
	UpdatePlaybackControls();
}

HTREEITEM CRScenePreviewPage::CurrentPlaybackTreeItem() const
{
	if (m_pTreeCtrl == nullptr || m_pTreeCtrl->GetSafeHwnd() == nullptr ||
		m_dynamicTimeline.events.empty() ||
		m_dynamicPlaybackSession.state() ==
			ccz::editor_preview::DynamicPlaybackState::Stopped) {
		return nullptr;
	}
	const std::size_t range_begin = m_dynamicTimeline.range_begin;
	const std::size_t range_end = m_dynamicTimeline.range_end;
	if (range_begin >= range_end || range_end > m_dynamicTimeline.events.size()) {
		return nullptr;
	}
	std::size_t index = m_dynamicPlaybackSession.current_event_index();
	if (index < range_begin) index = range_begin;
	if (index >= range_end) index = range_end - 1;
	const auto runtime_handle = m_dynamicTimeline.events[index].runtime_handle;
	if (runtime_handle == 0) return nullptr;
	return FindCurrentTreeItem(*m_pTreeCtrl,
		reinterpret_cast<HTREEITEM>(runtime_handle));
}

bool CRScenePreviewPage::SyncPlaybackTreeSelection(bool suppress_preview_refresh)
{
	if (m_pTreeCtrl == nullptr || m_pTreeCtrl->GetSafeHwnd() == nullptr) {
		return false;
	}
	const HTREEITEM item = CurrentPlaybackTreeItem();
	if (item == nullptr) return false;
	CcczEditor2View* view = GetActiveEditorView();
	if (view == nullptr || &view->GetTreeCtrl() != m_pTreeCtrl) return false;
	m_hCurItem = item;
	view->SelectPlaybackItem(item, suppress_preview_refresh);
	return true;
}

void CRScenePreviewPage::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kDynamicPlaybackTimerId || !m_dynamicPlaybackActive) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	const std::uint64_t now = ::GetTickCount64();
	const std::int64_t elapsed = static_cast<std::int64_t>(
		(std::min<std::uint64_t>)(now - m_dynamicLastTickMs, 250));
	m_dynamicLastTickMs = now;
	const auto tick = m_dynamicPlaybackSession.Tick(elapsed);
	for (const std::size_t completed : tick.completed_events) {
		if (m_dynamicPlaybackPrepared &&
			m_dynamicPreparedIndex == completed) {
			CommitDynamicPlaybackEvent();
		}
		if (m_dynamicPlaybackSession.state() !=
			ccz::editor_preview::DynamicPlaybackState::Finished &&
			m_dynamicPlaybackSession.current_event_index() <
			m_dynamicTimeline.events.size()) {
			PrepareDynamicPlaybackEvent(
				m_dynamicPlaybackSession.current_event_index());
		}
	}
	if (m_dynamicPlaybackSession.state() ==
		ccz::editor_preview::DynamicPlaybackState::Finished) {
		m_dynamicPlaybackActive = false;
		m_dynamicPlaybackPrepared = false;
		KillTimer(kDynamicPlaybackTimerId);
	} else if (m_dynamicPlaybackPrepared) {
		m_dynamicVisualProgress = tick.progress;
	}
	if (!tick.completed_events.empty()) {
		SyncPlaybackTreeSelection(true);
	}
	UpdatePlaybackControls();
	Invalidate(FALSE);
}

void CRScenePreviewPage::OnPlaybackStartTargetClicked()
{
	StartDynamicPlayback(ccz::preview::PlaybackRange::StartToTarget);
}

void CRScenePreviewPage::OnPlaybackTargetEndClicked()
{
	StartDynamicPlayback(ccz::preview::PlaybackRange::TargetToEnd);
}

void CRScenePreviewPage::OnPlaybackResetClicked()
{
	StartDynamicPlayback(m_dynamicPlaybackRange);
}

void CRScenePreviewPage::OnPlaybackPlayClicked()
{
	using ccz::editor_preview::DynamicPlaybackState;
	const DynamicPlaybackState state = m_dynamicPlaybackSession.state();
	if (!m_dynamicPlaybackActive &&
		(state == DynamicPlaybackState::Stopped ||
		 state == DynamicPlaybackState::Finished)) {
		StartDynamicPlayback(m_dynamicPlaybackRange);
		return;
	}
	if (state == DynamicPlaybackState::Paused) {
		SyncPlaybackTreeSelection(true);
		m_dynamicPlaybackSession.Resume();
		m_dynamicPlaybackActive = true;
		m_dynamicLastTickMs = ::GetTickCount64();
		SetTimer(kDynamicPlaybackTimerId, 16, nullptr);
	} else if (state == DynamicPlaybackState::Playing) {
		m_dynamicPlaybackSession.Pause();
		KillTimer(kDynamicPlaybackTimerId);
		SyncPlaybackTreeSelection(true);
	}
	UpdatePlaybackControls();
}

void CRScenePreviewPage::OnPlaybackStopClicked()
{
	if (!SyncPlaybackTreeSelection(true)) {
		StopDynamicPlayback(true);
		return;
	}
	StopDynamicPlayback(false);
	if (m_pTreeCtrl != nullptr && m_hCurItem != nullptr) {
		RequestPreviewRefreshForCurrentDocument(*m_pTreeCtrl, m_hCurItem);
	}
}

void CRScenePreviewPage::OnPlaybackStepForwardClicked()
{
	if (!m_dynamicPlaybackActive) {
		// A first forward step is anchored at the selected instruction and
		// advances exactly one event toward the end.  Starting and pausing at
		// the anchor would make the first click appear to do nothing.
		if (!StartDynamicPlayback(ccz::preview::PlaybackRange::TargetToEnd)) return;
		m_dynamicPlaybackSession.Pause();
		KillTimer(kDynamicPlaybackTimerId);
		if (m_dynamicPlaybackPrepared) {
			CommitDynamicPlaybackEvent();
		}
		if (m_dynamicPlaybackSession.StepForward()) {
			if (m_dynamicPlaybackSession.state() ==
				ccz::editor_preview::DynamicPlaybackState::Finished) {
				m_dynamicPlaybackActive = false;
				m_dynamicPlaybackPrepared = false;
			} else {
				PrepareDynamicPlaybackEvent(
					m_dynamicPlaybackSession.current_event_index());
			}
		}
		SyncPlaybackTreeSelection(true);
		UpdatePlaybackControls();
		Invalidate(FALSE);
		return;
	}
	m_dynamicPlaybackSession.Pause();
	KillTimer(kDynamicPlaybackTimerId);
	if (m_dynamicPlaybackPrepared) CommitDynamicPlaybackEvent();
	if (m_dynamicPlaybackSession.StepForward()) {
		if (m_dynamicPlaybackSession.state() ==
			ccz::editor_preview::DynamicPlaybackState::Finished) {
			m_dynamicPlaybackActive = false;
			m_dynamicPlaybackPrepared = false;
		} else {
			m_dynamicPlaybackActive = true;
			PrepareDynamicPlaybackEvent(
				m_dynamicPlaybackSession.current_event_index());
		}
	}
	SyncPlaybackTreeSelection(true);
	UpdatePlaybackControls();
	Invalidate(FALSE);
}

void CRScenePreviewPage::OnPlaybackStepBackClicked()
{
	using ccz::editor_preview::DynamicPlaybackState;
	const bool has_session =
		!m_dynamicTimeline.events.empty() &&
		m_dynamicPlaybackSession.state() != DynamicPlaybackState::Stopped;
	if (!m_dynamicPlaybackActive && !has_session) {
		// A first backward step is anchored at the selected instruction and
		// retreats one event toward the beginning of the section.
		if (!StartDynamicPlayback(ccz::preview::PlaybackRange::StartToTarget)) return;
		m_dynamicPlaybackSession.Pause();
		KillTimer(kDynamicPlaybackTimerId);
		const std::size_t target = m_dynamicTimeline.target_index;
		const std::size_t range_begin = m_dynamicTimeline.range_begin;
		// Move the core cursor to the selected event without rendering the
		// intermediate prefix.  The actual one-event rewind below restores the
		// immediately preceding state directly.
		while (m_dynamicPlaybackSession.current_event_index() < target) {
			if (!m_dynamicPlaybackSession.StepForward()) break;
		}
		if (target > range_begin) {
			m_dynamicPlaybackPrepared = false;
			if (!RewindDynamicPlaybackOneEvent()) {
				StopDynamicPlayback(true);
				return;
			}
		}
		SyncPlaybackTreeSelection(true);
		UpdatePlaybackControls();
		Invalidate(FALSE);
		return;
	}
	m_dynamicPlaybackSession.Pause();
	KillTimer(kDynamicPlaybackTimerId);
	RewindDynamicPlaybackOneEvent();
	SyncPlaybackTreeSelection(true);
	UpdatePlaybackControls();
	Invalidate(FALSE);
}

void CRScenePreviewPage::OnPlaybackSpeedClicked()
{
	const double current = m_dynamicPlaybackSession.speed();
	const double next = current < 0.75 ? 1.0 : current < 1.5 ? 2.0
		: current < 3.0 ? 4.0 : 0.5;
	m_dynamicPlaybackSession.SetSpeed(next);
	UpdatePlaybackControls();
}

BOOL CRScenePreviewPage::PreTranslateMessage(MSG* pMsg)
{
	if (m_playbackToolTip.GetSafeHwnd() != nullptr && pMsg != nullptr) {
		m_playbackToolTip.RelayEvent(pMsg);
	}
	if (pMsg != nullptr && pMsg->message == WM_KEYDOWN &&
		pMsg->wParam == VK_ESCAPE && m_dynamicPlaybackActive) {
		OnPlaybackStopClicked();
		return TRUE;
	}
	return CWnd::PreTranslateMessage(pMsg);
}

BOOL CRScenePreviewPage::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

bool CRScenePreviewPage::IsCharacterSelected(int charID) const
{
	return std::find(m_selectedCharIDs.begin(), m_selectedCharIDs.end(), charID) !=
		m_selectedCharIDs.end();
}

bool CRScenePreviewPage::IsPrimaryCharacter(int charID) const
{
	return m_primaryCharID == charID && IsCharacterSelected(charID);
}

void CRScenePreviewPage::SetPrimaryCharacter(int charID)
{
	if (IsCharacterSelected(charID)) {
		m_primaryCharID = charID;
		Invalidate();
	}
}

void CRScenePreviewPage::ToggleCharacterSelection(int charID)
{
	if (charID < 0) return;

	auto it = std::find(m_selectedCharIDs.begin(), m_selectedCharIDs.end(), charID);
	if (it == m_selectedCharIDs.end()) {
		m_selectedCharIDs.push_back(charID);
		if (m_primaryCharID < 0)
			m_primaryCharID = charID;
	} else {
		m_selectedCharIDs.erase(it);
		if (m_primaryCharID == charID) {
			m_primaryCharID = m_selectedCharIDs.empty() ? -1 : m_selectedCharIDs.front();
		}
	}
	Invalidate();
}

void CRScenePreviewPage::RemoveCharacterSelection(int charID)
{
	if (charID < 0) return;
	auto it = std::find(m_selectedCharIDs.begin(), m_selectedCharIDs.end(), charID);
	if (it == m_selectedCharIDs.end()) return;
	m_selectedCharIDs.erase(it);
	if (m_primaryCharID == charID) {
		m_primaryCharID = m_selectedCharIDs.empty() ? -1 : m_selectedCharIDs.front();
	}
	Invalidate();
}

void CRScenePreviewPage::ClearCharacterSelection()
{
	if (m_selectedCharIDs.empty() && m_primaryCharID < 0) return;
	m_selectedCharIDs.clear();
	m_primaryCharID = -1;
	Invalidate();
}

void CRScenePreviewPage::ReconcileCharacterSelection()
{
	m_selectedCharIDs.erase(
		std::remove_if(m_selectedCharIDs.begin(), m_selectedCharIDs.end(),
			[this](int charID) {
				return std::find_if(m_state.characters.begin(), m_state.characters.end(),
					[charID](const CharacterState& character) {
						return character.id == charID;
					}) == m_state.characters.end();
			}),
		m_selectedCharIDs.end());

	if (m_primaryCharID < 0 || !IsCharacterSelected(m_primaryCharID))
		m_primaryCharID = m_selectedCharIDs.empty() ? -1 : m_selectedCharIDs.front();
}

PositionWritebackSource CRScenePreviewPage::MakePositionSource(
	int instructionId, int subjectId, HTREEITEM sourceItem) const
{
	PositionWritebackSource result;
	ccz::scene_preview::PositionSourceSpec spec =
		ccz::scene_preview::actor_position_source_spec(instructionId);
	if (!spec.valid()) {
		spec = ccz::scene_preview::portrait_position_source_spec(instructionId);
	}
	if (!spec.valid()) return result;
	result.source = {
		spec.kind, spec.instructionId, spec.xIndex, spec.yIndex, subjectId, -1
	};
	result.item = sourceItem;
	return result;
}

bool CRScenePreviewPage::CommitCharacterStates(
	const std::vector<CharacterState>& states)
{
	return CommitCharacterStates(states, L"调整位置", false);
}

bool CRScenePreviewPage::CommitCharacterStates(
	const std::vector<CharacterState>& states, bool writeDirection)
{
	return CommitCharacterStates(states, L"调整位置", writeDirection);
}

bool CRScenePreviewPage::CommitCharacterStates(
	const std::vector<CharacterState>& states, const wchar_t* undoLabel)
{
	return CommitCharacterStates(states, undoLabel, false);
}

bool CRScenePreviewPage::CommitCharacterStates(
	const std::vector<CharacterState>& states, const wchar_t* undoLabel,
	bool writeDirection)
{
	if (!m_pTreeCtrl || !m_hCurItem || states.empty()) return false;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return false;

	struct PendingState {
		HTREEITEM item;
		PositionWritebackSource source;
		const CharacterState* state;
	};
	std::vector<PendingState> pending;
	pending.reserve(states.size());
	std::vector<CoordinateUndoEntry> undoEntries;
	undoEntries.reserve(states.size());

	// 先验证所有来源节点，确保批量写回不会出现半批量结果。
	for (const auto& state : states) {
		PositionWritebackSource source = state.positionSource;
		// 兼容旧状态/异常回放：没有 provenance 时保留原始 0x30 回退路径。
		if (!source.valid()) {
			HTREEITEM hAppear = FindAppearInstruction(state.id);
			if (!hAppear) return false;
			source = MakePositionSource(48, state.id, hAppear);
		}
		ItemData* data = (ItemData*)m_pTreeCtrl->GetItemData(source.item);
		const int directionIndex = writeDirection
			? ccz::scene_preview::position_direction_index(source.source.kind) : -1;
		if (!data || data->id != source.source.instructionId ||
			source.source.subjectId != state.id ||
			source.source.xIndex < 0 || source.source.yIndex < 0 ||
			!ccz::scene_preview::PositionSourceMatchesInstruction(
				source.source, data->int_data) ||
			(int)data->int_data.size() <= source.source.xIndex ||
			(int)data->int_data.size() <= source.source.yIndex ||
			(writeDirection && (directionIndex < 0 ||
				(int)data->int_data.size() <= directionIndex ||
				state.direction < 0 || state.direction >= 4)))
			return false;
		pending.push_back({ source.item, source, &state });
	}

	for (const auto& entry : pending) {
		ItemData* data = (ItemData*)m_pTreeCtrl->GetItemData(entry.item);
		const int oldX = data->int_data[entry.source.source.xIndex];
		const int oldY = data->int_data[entry.source.source.yIndex];
		const int directionIndex = writeDirection
			? ccz::scene_preview::position_direction_index(entry.source.source.kind) : -1;
		const int oldDirection = writeDirection
			? data->int_data[directionIndex] : 0;
		data->int_data[entry.source.source.xIndex] = entry.state->x;
		data->int_data[entry.source.source.yIndex] = entry.state->y;
		if (writeDirection)
			data->int_data[directionIndex] = entry.state->direction;
		pView->UpdateShow(entry.item);
		if (oldX != entry.state->x || oldY != entry.state->y ||
			(writeDirection && oldDirection != entry.state->direction)) {
			CoordinateUndoEntry undoEntry{
				entry.item, entry.source.source, oldX, oldY,
				entry.state->x, entry.state->y};
			undoEntry.directionChanged = writeDirection &&
				oldDirection != entry.state->direction;
			undoEntry.oldDirection = oldDirection;
			undoEntry.newDirection = entry.state->direction;
			undoEntries.push_back(undoEntry);
		}
	}

	if (!undoEntries.empty())
		pView->GetDocument()->MarkEditorContentChanged();
	pView->GetDocument()->SetModifiedFlag(TRUE);
	if (!undoEntries.empty()) {
		RecordCoordinateUndo(undoLabel ? undoLabel : L"调整位置",
			std::move(undoEntries), pView->cur_item);
	}
	RequestPreviewRefreshForCurrentDocument(*m_pTreeCtrl, m_hCurItem);
	return true;
}

bool CRScenePreviewPage::CommitMapPortraitPosition(int personID, int x, int y)
{
	if (!m_pTreeCtrl || !m_hCurItem) return false;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return false;

	auto sourceIt = m_state.mapPortraitPositionSources.find(personID);
	if (sourceIt == m_state.mapPortraitPositionSources.end() ||
		!sourceIt->second.valid()) return false;

	const PositionWritebackSource& source = sourceIt->second;
	ItemData* data = (ItemData*)m_pTreeCtrl->GetItemData(source.item);
	if (!data || data->id != source.source.instructionId ||
		source.source.subjectId != personID ||
		(source.source.kind != ccz::scene_preview::PositionSourceKind::PortraitShow &&
			source.source.kind != ccz::scene_preview::PositionSourceKind::PortraitMove) ||
		!ccz::scene_preview::PositionSourceMatchesInstruction(
			source.source, data->int_data) ||
		(int)data->int_data.size() <= source.source.yIndex)
		return false;

	const int oldX = data->int_data[source.source.xIndex];
	const int oldY = data->int_data[source.source.yIndex];
	data->int_data[source.source.xIndex] = x;
	data->int_data[source.source.yIndex] = y;
	pView->UpdateShow(source.item);
	if (oldX != x || oldY != y)
		pView->GetDocument()->MarkEditorContentChanged();
	pView->GetDocument()->SetModifiedFlag(TRUE);
	if (oldX != x || oldY != y) {
		RecordCoordinateUndo(L"地图头像调整位置",
			{{source.item, source.source, oldX, oldY, x, y}},
			pView->cur_item);
	}
	RequestPreviewRefreshForCurrentDocument(*m_pTreeCtrl, m_hCurItem);
	return true;
}

void CRScenePreviewPage::RecordCoordinateUndo(
	const wchar_t* label, std::vector<CoordinateUndoEntry> entries,
	HTREEITEM restoreSelection)
{
	if (entries.empty()) return;
	PreviewUndoTransaction transaction;
	transaction.kind = PreviewUndoTransaction::Kind::Coordinate;
	transaction.label = label ? label : L"调整位置";
	transaction.coordinates = std::move(entries);
	transaction.restoreSelection = restoreSelection;
	transaction.treeGeneration = m_previewTreeGeneration;
	m_previewUndoStack.push_back(std::move(transaction));
	UpdateUndoButton();
}

void CRScenePreviewPage::RecordInsertedUndo(
	const wchar_t* label, std::vector<HTREEITEM> items,
	HTREEITEM restoreSelection)
{
	items.erase(std::remove(items.begin(), items.end(), nullptr), items.end());
	if (items.empty()) return;
	PreviewUndoTransaction transaction;
	transaction.kind = PreviewUndoTransaction::Kind::Inserted;
	transaction.label = label ? label : L"预览写回";
	transaction.insertedItems = std::move(items);
	transaction.restoreSelection = restoreSelection;
	transaction.treeGeneration = m_previewTreeGeneration;
	m_previewUndoStack.push_back(std::move(transaction));
	UpdateUndoButton();
}

void CRScenePreviewPage::ClearPreviewUndo()
{
	m_previewUndoStack.clear();
	m_undoButtonVisible = false;
	if (m_undoButton.GetSafeHwnd() != nullptr) {
		m_undoButton.ShowWindow(SW_HIDE);
		m_undoButton.EnableWindow(TRUE);
	}
}

void CRScenePreviewPage::UpdateUndoButton()
{
	m_undoButtonVisible = !m_previewUndoStack.empty();
	if (m_undoButton.GetSafeHwnd() != nullptr) {
		m_undoButton.ShowWindow(m_undoButtonVisible ? SW_SHOW : SW_HIDE);
		m_undoButton.EnableWindow(TRUE);
	}
	if (GetSafeHwnd() != nullptr) {
		CRect client_rect;
		GetClientRect(&client_rect);
		LayoutConditionUi(client_rect.Width(), client_rect.Height());
		Invalidate(FALSE);
	}
}

bool CRScenePreviewPage::UndoLastPreviewOperation()
{
	if (m_previewUndoStack.empty() || m_pTreeCtrl == nullptr ||
		m_pTreeCtrl->GetSafeHwnd() == nullptr) {
		return false;
	}
	CcczEditor2View* pView = GetActiveEditorView();
	if (pView == nullptr || &pView->GetTreeCtrl() != m_pTreeCtrl) {
		return false;
	}

	PreviewUndoTransaction& transaction = m_previewUndoStack.back();
	if (transaction.treeGeneration != m_previewTreeGeneration) {
		AppendDiagnostic(_T("撤回失败：预览树已刷新，请重新执行操作"));
		return false;
	}
	CTreeCtrl& tree = *m_pTreeCtrl;

	// Validate the complete transaction before changing any node.  This keeps
	// a later manual edit from being silently overwritten by an older preview
	// operation.
	if (transaction.kind == PreviewUndoTransaction::Kind::Coordinate) {
		for (const CoordinateUndoEntry& entry : transaction.coordinates) {
			const HTREEITEM item = FindCurrentTreeItem(tree, entry.item);
			ItemData* data = item != nullptr
				? reinterpret_cast<ItemData*>(tree.GetItemData(item)) : nullptr;
			const int directionIndex = entry.directionChanged
				? ccz::scene_preview::position_direction_index(entry.source.kind) : -1;
			if (item == nullptr || data == nullptr ||
				data->id != entry.source.instructionId ||
				entry.source.xIndex < 0 || entry.source.yIndex < 0 ||
				static_cast<int>(data->int_data.size()) <= entry.source.xIndex ||
				static_cast<int>(data->int_data.size()) <= entry.source.yIndex ||
				!ccz::scene_preview::PositionSourceMatchesInstruction(
					entry.source, data->int_data) ||
				data->int_data[entry.source.xIndex] != entry.newX ||
				data->int_data[entry.source.yIndex] != entry.newY ||
				(entry.directionChanged && (directionIndex < 0 ||
					static_cast<int>(data->int_data.size()) <= directionIndex ||
					data->int_data[directionIndex] != entry.newDirection))) {
				AppendDiagnostic(_T("撤回失败：目标指令已被其他编辑改变"));
				return false;
			}
		}
	} else {
		for (HTREEITEM item : transaction.insertedItems) {
			if (FindCurrentTreeItem(tree, item) == nullptr ||
				tree.GetItemData(item) == 0) {
				AppendDiagnostic(_T("撤回失败：待删除的预览指令已不存在"));
				return false;
			}
		}
	}

	const HTREEITEM restoreSelection = FindCurrentTreeItem(
		tree, transaction.restoreSelection);
	if (transaction.kind == PreviewUndoTransaction::Kind::Coordinate) {
		for (const CoordinateUndoEntry& entry : transaction.coordinates) {
			ItemData* data = reinterpret_cast<ItemData*>(
				tree.GetItemData(entry.item));
			data->int_data[entry.source.xIndex] = entry.oldX;
			data->int_data[entry.source.yIndex] = entry.oldY;
			if (entry.directionChanged) {
				const int directionIndex =
					ccz::scene_preview::position_direction_index(entry.source.kind);
				data->int_data[directionIndex] = entry.oldDirection;
			}
			pView->UpdateShow(entry.item);
		}
	} else {
		for (auto it = transaction.insertedItems.rbegin();
			it != transaction.insertedItems.rend(); ++it) {
			RemoveInsertedInstruction(*it);
		}
	}

	m_previewUndoStack.pop_back();
	if (restoreSelection != nullptr) {
		tree.SelectItem(restoreSelection);
		pView->cur_item = restoreSelection;
		m_hCurItem = restoreSelection;
	}
	pView->GetDocument()->MarkEditorContentChanged();
	pView->GetDocument()->SetModifiedFlag(TRUE);
	UpdateUndoButton();
	const HTREEITEM refreshItem = restoreSelection != nullptr
		? restoreSelection : m_hCurItem;
	if (refreshItem != nullptr) {
		RequestPreviewRefreshForCurrentDocument(tree, refreshItem);
	}
	return true;
}

// ========== 坐标转换 ==========

CPoint CRScenePreviewPage::GameToPixel(int X, int Y)
{
	int px = 8 * (X - Y + 42);
	int py = 4 * (X + Y - 50);
	return CPoint(px, py);
}

// ========== 方向箭头绘制 ==========

void CRScenePreviewPage::DrawDirectionArrow(Gdiplus::Graphics& g, int cx, int cy, int direction)
{
	Gdiplus::SolidBrush arrowBrush(Gdiplus::Color(255, 255, 200, 0));
	Gdiplus::Point pts[3];
	int r = 14;

	switch (direction) {
	case 0: // 北
		pts[0] = Gdiplus::Point(cx, cy - r - 6);
		pts[1] = Gdiplus::Point(cx - 4, cy - r);
		pts[2] = Gdiplus::Point(cx + 4, cy - r);
		break;
	case 1: // 东
		pts[0] = Gdiplus::Point(cx + r + 6, cy);
		pts[1] = Gdiplus::Point(cx + r, cy - 4);
		pts[2] = Gdiplus::Point(cx + r, cy + 4);
		break;
	case 2: // 南
		pts[0] = Gdiplus::Point(cx, cy + r + 6);
		pts[1] = Gdiplus::Point(cx - 4, cy + r);
		pts[2] = Gdiplus::Point(cx + 4, cy + r);
		break;
	case 3: // 西
		pts[0] = Gdiplus::Point(cx - r - 6, cy);
		pts[1] = Gdiplus::Point(cx - r, cy - 4);
		pts[2] = Gdiplus::Point(cx - r, cy + 4);
		break;
	default:
		return;
	}
	g.FillPolygon(&arrowBrush, pts, 3);
}

// ========== 推演逻辑 ==========

void CRScenePreviewPage::ProcessInstruction(ItemData* data, HTREEITEM sourceItem)
{
	if (!data || data->int_data.empty()) return;
	switch (data->id) {
	case 41: // 0x29: 地图头像显示 [Data人物ID, 像素X, 像素Y]
		if (data->int_data.size() >= 3) {
			const int personID = data->int_data[0];
			if (!ccz::scene_preview::is_direct_data_portrait_target(personID)) {
				CString msg;
				msg.Format(_T("map portrait show unsupported target=%d"), personID);
				AppendDiagnostic(msg);
				break;
			}
			m_state.mapPortraits.show(personID, data->int_data[1], data->int_data[2]);
			m_state.mapPortraitPositionSources[personID] = MakePositionSource(
				data->id, personID, sourceItem);
			Gdiplus::Bitmap* portrait = GetPortraitBitmap(personID, m_modPath.GetString());
			if (auto* placement = m_state.mapPortraits.find(personID)) {
				auto faceIt = m_portraitFaceIndices.find(personID);
				placement->faceImageIndex = (faceIt != m_portraitFaceIndices.end()) ?
					faceIt->second : -1;
			}
			if (!portrait && m_debugInfo.IsEmpty()) {
				CString msg;
				msg.Format(_T("map portrait show failed person=%d"), personID);
				AppendDiagnostic(msg);
			}
		}
		break;

	case 42: // 0x2A: 地图头像移动 [Data人物ID, 像素X, 像素Y]
		if (data->int_data.size() >= 3) {
			const int personID = data->int_data[0];
			if (!ccz::scene_preview::is_direct_data_portrait_target(personID)) {
				CString msg;
				msg.Format(_T("map portrait move unsupported target=%d"), personID);
				AppendDiagnostic(msg);
				break;
			}
			if (!m_state.mapPortraits.move(personID, data->int_data[1], data->int_data[2])) {
				CString msg;
				msg.Format(_T("map portrait move target not visible=%d"), personID);
				AppendDiagnostic(msg);
			} else {
				m_state.mapPortraitPositionSources[personID] = MakePositionSource(
					data->id, personID, sourceItem);
			}
		}
		break;

	case 43: // 0x2B: 地图头像消失 [Data人物ID]
		if (!data->int_data.empty()) {
			const int personID = data->int_data[0];
			if (!ccz::scene_preview::is_direct_data_portrait_target(personID)) {
				CString msg;
				msg.Format(_T("map portrait hide unsupported target=%d"), personID);
				AppendDiagnostic(msg);
				break;
			}
			m_state.mapPortraits.hide(personID);
			m_state.mapPortraitPositionSources.erase(personID);
		}
		break;

	case 47: // 0x2F: 清除人物
		m_state.characters.clear();
		break;

	case 39: // 0x27: 背景显示
		{
			int imageIndex = -1;
			if (!ccz::scene_preview::try_resolve_background_image_index(
					data->int_data, imageIndex)) {
				break;
			}
			m_state.mapID = data->int_data[0] == 0 ? data->int_data[1]
										: data->int_data[3];
			m_state.mapImageIndex = imageIndex;
			m_state.coordinateSpace = ccz::scene_preview::
				coordinate_space_for_background_branch(data->int_data[0]);
		}
		break;

	case 48: // 0x30: 武将出现 [人物ID, X, Y, 方向, 动作]
		if (data->int_data.size() >= 5) {
			CharacterState cs;
			cs.id = data->int_data[0];
			cs.x = data->int_data[1];
			cs.y = data->int_data[2];
			cs.direction = data->int_data[3];
			cs.action = data->int_data[4];
			cs.positionSource = MakePositionSource(data->id, cs.id, sourceItem);
			auto& chars = m_state.characters;
			chars.erase(std::remove_if(chars.begin(), chars.end(),
				[&](const CharacterState& c) { return c.id == cs.id; }), chars.end());
			chars.push_back(cs);
		}
		break;

	case 49: // 0x31: 武将消失 [模式, 人物ID, ...]
		if (data->int_data.size() >= 2 && data->int_data[0] == 0) {
			int charID = data->int_data[1];
			auto& chars = m_state.characters;
			chars.erase(std::remove_if(chars.begin(), chars.end(),
				[&](const CharacterState& c) { return c.id == charID; }), chars.end());
		}
		break;

	case 50: // 0x32: 武将移动 [?, 人物ID, ?, 目标X, 目标Y, 方向]
		if (data->int_data.size() >= 6) {
			int charID = data->int_data[1];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.x = data->int_data[3];
					c.y = data->int_data[4];
					c.direction = data->int_data[5];
					c.positionSource = MakePositionSource(data->id, charID, sourceItem);
					break;
				}
			}
		}
		break;

	case 51: // 0x33: 武将转向 [人物ID, 动作, 方向, 缺省, 缺省]
		if (data->int_data.size() >= 3) {
			int charID = data->int_data[0];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.action = data->int_data[1];
					c.direction = data->int_data[2];
					break;
				}
			}
		}
		break;

	case 52: // 0x34: 武将动作 [人物ID, 动作, ?, ?, ?]
		if (data->int_data.size() >= 2) {
			int charID = data->int_data[0];
			for (auto& c : m_state.characters) {
				if (c.id == charID) {
					c.action = data->int_data[1];
					break;
				}
			}
		}
		break;
	}
}

void CRScenePreviewPage::ProcessSceneNode(CTreeCtrl& tree, HTREEITEM item,
	HTREEITEM stopItem, bool stopBefore, bool& reachedStop)
{
	if (!item || reachedStop) return;
	if (stopBefore && item == stopItem) {
		reachedStop = true;
		return;
	}

	ItemData* data = (ItemData*)tree.GetItemData(item);
	if (data) ProcessInstruction(data, item);
	if (!stopBefore && item == stopItem) {
		reachedStop = true;
		return;
	}

	for (HTREEITEM child = tree.GetChildItem(item); child && !reachedStop;
		child = tree.GetNextSiblingItem(child)) {
		ProcessSceneNode(tree, child, stopItem, stopBefore, reachedStop);
	}
}

void CRScenePreviewPage::ProcessSceneSection(CTreeCtrl& tree,
	HTREEITEM section, HTREEITEM stopItem, bool stopBefore, bool& reachedStop)
{
	if (!section || reachedStop) return;
	for (HTREEITEM child = tree.GetChildItem(section);
		child && !reachedStop;
		child = tree.GetNextSiblingItem(child)) {
		ProcessSceneNode(tree, child, stopItem, stopBefore, reachedStop);
	}
}

void CRScenePreviewPage::ProcessSceneToSection(
	CTreeCtrl& tree, HTREEITEM scene, HTREEITEM appearanceSection,
	HTREEITEM appearanceInstruction, bool& reachedAppearance)
{
	if (!scene || !appearanceSection || !appearanceInstruction) return;
	for (HTREEITEM section = tree.GetChildItem(scene);
		section && !reachedAppearance;
		section = tree.GetNextSiblingItem(section)) {
		if (section == appearanceSection) {
			ProcessSceneSection(tree, section, appearanceInstruction, false,
				reachedAppearance);
			return;
		}
		ProcessSceneSection(tree, section, nullptr, false,
			reachedAppearance);
	}
}

void CRScenePreviewPage::ProcessSceneAll(CTreeCtrl& tree, HTREEITEM scene)
{
	if (!scene) return;
	for (HTREEITEM section = tree.GetChildItem(scene);
		section; section = tree.GetNextSiblingItem(section)) {
		bool reachedStop = false;
		ProcessSceneSection(tree, section, nullptr, false, reachedStop);
	}
}

CRScenePreviewPage::SpecialScenePrefixResult
CRScenePreviewPage::PrepareSpecialScenePrefix(
	CTreeCtrl& tree, HTREEITEM hSection, HTREEITEM hSelectedItem,
	const wchar_t* documentPath)
{
	if (!hSection || !hSelectedItem) {
		return SpecialScenePrefixResult::NotSpecial;
	}

	ccz::editor_preview::CapturedEditorTree snapshot;
	std::wstring diagnostic;
	if (!ccz::editor_preview::CaptureEditorTreeSnapshot(
			tree, hSelectedItem,
			documentPath ? std::wstring(documentPath) : std::wstring(),
			snapshot, diagnostic)) {
		if (!diagnostic.empty()) AppendDiagnostic(CString(diagnostic.c_str()));
		return SpecialScenePrefixResult::NotSpecial;
	}
	if (snapshot.section_path.empty() || snapshot.document_roots.empty()) {
		return SpecialScenePrefixResult::NotSpecial;
	}

	const auto resolution = ccz::editor_preview::ResolveScenePreview(
		snapshot.document_roots.front(),
		reinterpret_cast<std::uintptr_t>(hSection),
		reinterpret_cast<std::uintptr_t>(hSelectedItem));
	if (resolution.kind == ccz::editor_preview::ScenePreviewKind::Ordinary) {
		return SpecialScenePrefixResult::NotSpecial;
	}

	// Free-R uses the same ordered native executor as ordinary Sections.  Do
	// not switch to the retired snapshot-only mode here: it silently dropped
	// actor state commands while constructing the paired Scene prefix.
	m_limitedFreeRReplay = false;
	if (!resolution.diagnostic.empty()) {
		AppendDiagnostic(CString(resolution.diagnostic.c_str()));
	}
	if (!resolution.resolved) {
		return SpecialScenePrefixResult::SpecialUnresolved;
	}

	HTREEITEM initialScene =
		reinterpret_cast<HTREEITEM>(resolution.initial_scene_handle);
	if (!initialScene) {
		AppendDiagnostic(_T("特殊 Scene 匹配结果缺少有效初始 Scene 节点"));
		return SpecialScenePrefixResult::SpecialUnresolved;
	}

	if (resolution.kind == ccz::editor_preview::ScenePreviewKind::FreeR &&
		resolution.has_click_target) {
		HTREEITEM appearanceSection =
			reinterpret_cast<HTREEITEM>(resolution.appearance_section_handle);
		HTREEITEM appearanceInstruction =
			reinterpret_cast<HTREEITEM>(resolution.appearance_instruction_handle);
		if (!appearanceSection || !appearanceInstruction) {
			AppendDiagnostic(_T("自由R匹配结果缺少有效武将出现 Section 节点"));
			return SpecialScenePrefixResult::SpecialUnresolved;
		}

		// The appearance Section is an initial-state source, not a stop point.
		// When it belongs to the trigger Scene, the preceding Sections are the
		// branch-local setup stream and must be replayed before the selected
		// Section.  Otherwise replay the complete paired initial Scene so later
		// 0x31/0x32/0x33/0x34 commands settle the snapshot to its final state.
		if (resolution.appearance_scene_handle ==
			resolution.trigger_scene_handle) {
			HTREEITEM triggerScene = reinterpret_cast<HTREEITEM>(
				resolution.trigger_scene_handle);
			for (HTREEITEM section = tree.GetChildItem(triggerScene);
				section && section != hSection;
				section = tree.GetNextSiblingItem(section)) {
				bool reachedStop = false;
				ProcessSceneSection(tree, section, nullptr, false, reachedStop);
			}
		} else {
			ProcessSceneAll(tree, initialScene);
		}
		(void)appearanceSection;
		(void)appearanceInstruction;
	} else {
		// Deployment pairs and non-click Free-R branches both start from the
		// complete paired initial Scene.  The selected trigger Section is still
		// replayed separately below, preserving Section-level boundaries.
		ProcessSceneAll(tree, initialScene);
	}

	return SpecialScenePrefixResult::PrefixApplied;
}

bool CRScenePreviewPage::EvaluateSpecialScene(
	CTreeCtrl& tree, HTREEITEM hSection, HTREEITEM hSelectedItem,
	const wchar_t* documentPath)
{
	const SpecialScenePrefixResult prefixResult =
		PrepareSpecialScenePrefix(tree, hSection, hSelectedItem,
			documentPath);
	if (prefixResult == SpecialScenePrefixResult::NotSpecial) {
		return false;
	}
	if (prefixResult == SpecialScenePrefixResult::SpecialUnresolved) {
		// Preserve the legacy special-scene boundary: once a bounded pair is
		// recognized but cannot be resolved, do not replay the current Section.
		return true;
	}

	bool reachedSelected = false;
	ProcessSceneSection(tree, hSection, hSelectedItem, true,
		reachedSelected);
	return true;
}

void CRScenePreviewPage::EvaluateToItem(CTreeCtrl& tree, HTREEITEM hItem)
{
	m_state.Clear();
	m_limitedFreeRReplay = false;
	if (!hItem) return;

	HTREEITEM hSection = hItem;
	ItemData* pData = (ItemData*)tree.GetItemData(hSection);
	if (pData && (pData->id == -1 || pData->id == -2)) return;

	HTREEITEM hParent = tree.GetParentItem(hItem);
	while (hParent) {
		ItemData* parentData = (ItemData*)tree.GetItemData(hParent);
		if (parentData && parentData->id == -2) {
			hSection = hParent;
			break;
		}
		if (parentData && parentData->id == -1) return;
		hParent = tree.GetParentItem(hParent);
	}

	if (!hParent) return;
	if (EvaluateSpecialScene(tree, hSection, hItem,
		m_modPath.IsEmpty() ? nullptr : (LPCWSTR)m_modPath)) {
		return;
	}

	HTREEITEM hCur = tree.GetChildItem(hSection);
	while (hCur) {
		ItemData* data = (ItemData*)tree.GetItemData(hCur);
		if (data) ProcessInstruction(data, hCur);
		if (hCur == hItem) break;

		HTREEITEM hChild = tree.GetChildItem(hCur);
		if (hChild) {
			HTREEITEM hSub = hChild;
			while (hSub) {
				ItemData* subData = (ItemData*)tree.GetItemData(hSub);
				if (subData) ProcessInstruction(subData, hSub);
				if (hSub == hItem) return;
				hSub = tree.GetNextSiblingItem(hSub);
			}
		}

		hCur = tree.GetNextSiblingItem(hCur);
	}
}

// ========== 路径工具 ==========

CString CRScenePreviewPage::GetRootPath(const wchar_t* modPath)
{
	CString basePath(modPath);
	int len = basePath.GetLength();
	for (int i = len - 1; i >= 0; i--) {
		if (basePath[i] == L'\\' || basePath[i] == L'/') {
			if (i >= 2 && (basePath[i - 1] == L'S' || basePath[i - 1] == L's')
				&& (basePath[i - 2] == L'R' || basePath[i - 2] == L'r')) {
				continue;
			}
			return basePath.Left(i + 1);
		}
	}
	return _T("");
}

void CRScenePreviewPage::AppendDiagnostic(const CString& message)
{
	if (message.IsEmpty()) return;
	if (!m_debugInfo.IsEmpty()) m_debugInfo += _T(" | ");
	m_debugInfo += message;
}

// ========== 256色调色板 (从Pmapobj.BMP提取, BGRA格式) ==========
static const BYTE s_spritePalette[1024] = {
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0x63,0x8C,0xFF,0x00, 0x4A,0x6B,0xFF,0x00,
	0x39,0x4A,0xF7,0x00, 0x29,0x31,0xAD,0x00, 0x5A,0xDE,0xFF,0x00, 0x52,0xCE,0xFF,0x00,
	0x5A,0xBD,0xEF,0x00, 0x52,0x9C,0xBD,0x00, 0xFF,0xFF,0xFF,0x00, 0xEE,0xEE,0xEE,0x00,
	0xDD,0xDD,0xDD,0x00, 0xCC,0xCC,0xCC,0x00, 0xBB,0xBB,0xBB,0x00, 0xAA,0xAA,0xAA,0x00,
	0x99,0x99,0x99,0x00, 0x88,0x88,0x88,0x00, 0x77,0x77,0x77,0x00, 0x66,0x66,0x66,0x00,
	0x55,0x55,0x55,0x00, 0x44,0x44,0x44,0x00, 0x22,0x22,0x22,0x00, 0x00,0x00,0x00,0x00,
	0xE1,0xEF,0xFD,0x00, 0xB4,0xD9,0xFD,0x00, 0x96,0xCA,0xFE,0x00, 0x69,0xB3,0xFE,0x00,
	0x2D,0x95,0xFF,0x00, 0x00,0x7F,0xFF,0x00, 0x00,0x6E,0xFF,0x00, 0x00,0x4C,0xFF,0x00,
	0x00,0x00,0xFF,0x00, 0x00,0x00,0xE7,0x00, 0x00,0x00,0xC3,0x00, 0x00,0x00,0xB7,0x00,
	0x00,0x00,0x9E,0x00, 0x00,0x00,0x86,0x00, 0x00,0x00,0x6E,0x00, 0x00,0x00,0x4A,0x00,
	0xFD,0xFD,0xD2,0x00, 0xFD,0xFD,0xB4,0x00, 0xFD,0xFE,0x96,0x00, 0xFD,0xFE,0x69,0x00,
	0xFF,0xFF,0x00,0x00, 0xFF,0xDD,0x00,0x00, 0xFF,0xBB,0x00,0x00, 0xFF,0x99,0x00,0x00,
	0xFF,0x77,0x00,0x00, 0xFF,0x55,0x00,0x00, 0xFF,0x00,0x00,0x00, 0xE7,0x00,0x00,0x00,
	0xC3,0x00,0x00,0x00, 0xAB,0x00,0x00,0x00, 0x7A,0x00,0x00,0x00, 0x56,0x00,0x00,0x00,
	0xE1,0xFD,0xFD,0x00, 0xB4,0xFD,0xFD,0x00, 0x87,0xFE,0xFE,0x00, 0x5A,0xFE,0xFE,0x00,
	0x2D,0xFF,0xFF,0x00, 0x00,0xFF,0xFF,0x00, 0x00,0xED,0xED,0x00, 0x00,0xDB,0xDB,0x00,
	0x00,0xC9,0xC9,0x00, 0x00,0xB2,0xB2,0x00, 0x00,0x9F,0x9F,0x00, 0x00,0x8B,0x8B,0x00,
	0x00,0x78,0x78,0x00, 0x00,0x65,0x65,0x00, 0x00,0x51,0x51,0x00, 0x00,0x3E,0x3E,0x00,
	0xE1,0xFD,0xE1,0x00, 0xC1,0xFD,0xC1,0x00, 0xA1,0xFE,0xA1,0x00, 0x81,0xFE,0x81,0x00,
	0x60,0xFE,0x60,0x00, 0x00,0xFF,0x00,0x00, 0x00,0xED,0x00,0x00, 0x00,0xDB,0x00,0x00,
	0x00,0xC9,0x00,0x00, 0x00,0xB7,0x00,0x00, 0x00,0xA5,0x00,0x00, 0x00,0x92,0x00,0x00,
	0x00,0x80,0x00,0x00, 0x00,0x6E,0x00,0x00, 0x00,0x5C,0x00,0x00, 0x00,0x4A,0x00,0x00,
	0x6E,0x89,0xAE,0x00, 0x5E,0x76,0x98,0x00, 0x48,0x62,0x75,0x00, 0x3F,0x52,0x64,0x00,
	0x36,0x40,0x4B,0x00, 0x1E,0x25,0x2E,0x00, 0xA5,0xCE,0xE7,0x00, 0x88,0xC3,0xEE,0x00,
	0x72,0xAD,0xE1,0x00, 0x73,0xA2,0xD9,0x00, 0x6C,0x9A,0xCE,0x00, 0xBD,0xD6,0xE7,0x00,
	0xB8,0xD1,0xE9,0x00, 0xA5,0xC6,0xE7,0x00, 0x94,0xBD,0xDE,0x00, 0x84,0xB5,0xDE,0x00,
	0xFD,0xE1,0xF7,0x00, 0xE6,0xC1,0xDE,0x00, 0xCE,0xA1,0xC4,0x00, 0xB7,0x81,0xAB,0x00,
	0x9F,0x61,0x92,0x00, 0x88,0x41,0x79,0x00, 0x70,0x21,0x5F,0x00, 0x59,0x01,0x46,0x00,
	0xFD,0xE1,0xFD,0x00, 0xFE,0xA9,0xFE,0x00, 0xFD,0x86,0xFD,0x00, 0xFF,0x6A,0xFF,0x00,
	0xFF,0x00,0xFF,0x00, 0xE8,0x17,0xE8,0x00, 0xD1,0x14,0xD1,0x00, 0xBA,0x12,0xBA,0x00,
	0xCE,0xF0,0xF8,0x00, 0xAB,0xD0,0xD9,0x00, 0x88,0xB0,0xBA,0x00, 0x65,0x90,0x9B,0x00,
	0x55,0x7F,0x8A,0x00, 0x45,0x6F,0x7A,0x00, 0x34,0x5E,0x69,0x00, 0x24,0x4D,0x58,0x00,
	0xF2,0xDE,0xAB,0x00, 0xCD,0xB9,0x85,0x00, 0xA9,0x93,0x5E,0x00, 0x84,0x6E,0x38,0x00,
	0x77,0x63,0x30,0x00, 0x6B,0x57,0x28,0x00, 0x5E,0x4C,0x20,0x00, 0x51,0x40,0x18,0x00,
	0x80,0xC3,0x80,0x00, 0x70,0xB4,0x70,0x00, 0x61,0xA5,0x61,0x00, 0x51,0x96,0x51,0x00,
	0x41,0x88,0x41,0x00, 0x31,0x79,0x31,0x00, 0x22,0x6A,0x22,0x00, 0x12,0x5B,0x12,0x00,
	0x5D,0x92,0xBA,0x00, 0x50,0x84,0xAB,0x00, 0x44,0x76,0x9D,0x00, 0x37,0x68,0x8E,0x00,
	0x2A,0x5B,0x80,0x00, 0x1D,0x4D,0x71,0x00, 0x11,0x3F,0x63,0x00, 0x04,0x31,0x54,0x00,
	0x29,0xC6,0xBD,0x00, 0x29,0xBD,0xAD,0x00, 0x4A,0xAD,0xAD,0x00, 0x31,0xAD,0x94,0x00,
	0x31,0x9C,0x8C,0x00, 0x31,0x94,0x73,0x00, 0x31,0x84,0x73,0x00, 0x31,0x7B,0x63,0x00,
	0xBF,0xA0,0x22,0x00, 0xAE,0x80,0x1A,0x00, 0x9D,0x60,0x11,0x00, 0x8C,0x3F,0x09,0x00,
	0x7B,0x1F,0x00,0x00, 0x02,0x55,0xC2,0x00, 0x36,0xAC,0xFF,0x00, 0x21,0x63,0xE7,0x00,
	0x18,0x42,0xD6,0x00, 0x18,0x42,0xAD,0x00, 0x10,0x31,0x8C,0x00, 0x1F,0x24,0x2A,0x00,
	0x58,0x01,0x44,0x00, 0x00,0x01,0x3A,0x00, 0x00,0x00,0x6F,0x00, 0x0E,0x38,0x1F,0x00,
	0x0F,0x42,0x28,0x00, 0x10,0x4A,0x2D,0x00, 0x05,0x30,0x54,0x00, 0x12,0x3F,0x62,0x00,
	0x13,0x50,0x31,0x00, 0x38,0x3F,0x4A,0x00, 0x13,0x56,0x35,0x00, 0x14,0x53,0x33,0x00,
	0x16,0x5D,0x3A,0x00, 0x15,0x5A,0x37,0x00, 0x19,0x63,0x3E,0x00, 0x17,0x61,0x3D,0x00,
	0x1A,0x65,0x40,0x00, 0x1A,0x68,0x42,0x00, 0x3A,0x5F,0x3C,0x00, 0x1B,0x6C,0x45,0x00,
	0x14,0x63,0x4B,0x00, 0x32,0x50,0x5C,0x00, 0x1E,0x4C,0x71,0x00, 0x60,0x6E,0x44,0x00,
	0x55,0x64,0x3E,0x00, 0x59,0x68,0x42,0x00, 0x60,0x71,0x49,0x00, 0x4E,0x6B,0x4B,0x00,
	0x14,0x69,0x51,0x00, 0x14,0x6D,0x54,0x00, 0x16,0x70,0x57,0x00, 0x1D,0x70,0x47,0x00,
	0x15,0x73,0x59,0x00, 0x2B,0x5A,0x7F,0x00, 0x43,0x71,0x4F,0x00, 0x18,0x75,0x5C,0x00,
	0x37,0x61,0x6A,0x00, 0x19,0x78,0x5D,0x00, 0x6B,0x78,0x47,0x00, 0x63,0x74,0x4C,0x00,
	0x67,0x78,0x51,0x00, 0x56,0x74,0x53,0x00, 0x3D,0x78,0x58,0x00, 0x59,0x6F,0x55,0x00,
	0x63,0x7A,0x5A,0x00, 0x6E,0x7F,0x56,0x00, 0x19,0x7B,0x60,0x00, 0x19,0x81,0x65,0x00,
	0x1B,0x85,0x68,0x00, 0x38,0x67,0x8E,0x00, 0x1B,0x7E,0x63,0x00, 0x30,0x72,0x6D,0x00,
	0x26,0x7D,0x6C,0x00, 0x59,0x76,0x5C,0x00, 0x34,0x7D,0x77,0x00, 0x5B,0x7C,0x6A,0x00,
	0x3A,0x76,0x77,0x00, 0x41,0x7D,0x81,0x00, 0x74,0x86,0x5C,0x00, 0x60,0x82,0x73,0x00,
	0x47,0x75,0x9C,0x00, 0x4D,0x83,0x8B,0x00, 0x55,0x84,0xAA,0x00, 0x53,0x9A,0xBB,0x00,
	0x76,0x9F,0xCA,0x00, 0x74,0xB9,0xE1,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00,
	0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00, 0xFF,0x00,0xF7,0x00
};

// 将256色裸像素数据转为ARGB Bitmap，透明色=品红(0xF700FF)
// 宽度固定48，高度由数据量推算；多余字节(头部)自动跳过
static Gdiplus::Bitmap* CreateBitmapFromRaw8bpp(const BYTE* rawData, DWORD dataSize)
{
	const int W = 48;
	DWORD skip = dataSize % W; // 对48取余=头部多余字节
	DWORD pixelBytes = dataSize - skip;
	int H = (int)(pixelBytes / W);
	if (H < 64 || H > 2048) return nullptr;
	rawData += skip;

	Gdiplus::Bitmap* pBmp = new Gdiplus::Bitmap(W, H, PixelFormat32bppARGB);
	if (pBmp->GetLastStatus() != Gdiplus::Ok) { delete pBmp; return nullptr; }

	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, W, H);
	pBmp->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

	// 取左上角第一个像素的色板索引作为透明索引
	BYTE transIdx = rawData[0];

	for (int y = 0; y < H; y++) {
		DWORD* dst = (DWORD*)((BYTE*)bmpData.Scan0 + y * bmpData.Stride);
		const BYTE* src = rawData + y * W;
		for (int x = 0; x < W; x++) {
			BYTE idx = src[x];
			BYTE b = s_spritePalette[idx * 4 + 0];
			BYTE g = s_spritePalette[idx * 4 + 1];
			BYTE r = s_spritePalette[idx * 4 + 2];
			// 品红(B=0xFF,G=0x00,R=0xF7)或与左上角相同色板索引 → 透明
			if ((b == 0xFF && g == 0x00 && r == 0xF7) || idx == transIdx) {
				dst[x] = 0x00000000;
			} else {
				dst[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
			}
		}
	}

	pBmp->UnlockBits(&bmpData);
	return pBmp;
}

// 将Bitmap中与左上角像素相同颜色的像素设为透明
static void ApplyTopLeftTransparency(Gdiplus::Bitmap* pBmp)
{
	if (!pBmp) return;
	int w = (int)pBmp->GetWidth();
	int h = (int)pBmp->GetHeight();
	if (w == 0 || h == 0) return;

	// 读取左上角像素颜色
	Gdiplus::Color topLeft;
	pBmp->GetPixel(0, 0, &topLeft);
	// 如果已经是透明就不用处理
	if (topLeft.GetAlpha() == 0) return;
	BYTE tR = topLeft.GetRed(), tG = topLeft.GetGreen(), tB = topLeft.GetBlue();

	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, w, h);
	pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
		PixelFormat32bppARGB, &bmpData);

	for (int y = 0; y < h; y++) {
		DWORD* row = (DWORD*)((BYTE*)bmpData.Scan0 + y * bmpData.Stride);
		for (int x = 0; x < w; x++) {
			DWORD px = row[x];
			BYTE a = (px >> 24) & 0xFF;
			BYTE r = (px >> 16) & 0xFF;
			BYTE g = (px >> 8) & 0xFF;
			BYTE b = px & 0xFF;
			if (a > 0 && r == tR && g == tG && b == tB) {
				row[x] = 0x00000000;
			}
		}
	}

	pBmp->UnlockBits(&bmpData);
}

// Unit_mov BMPs use the same near-magenta key that the Pro tool removes
// after loading a texture.  Keep this separate from the legacy R/Pmapobj
// transparency helper so the S asset path does not alter existing portraits.
static void ApplyUnitMagentaTransparency(Gdiplus::Bitmap* pBmp)
{
	if (!pBmp || pBmp->GetWidth() == 0 || pBmp->GetHeight() == 0) return;
	Gdiplus::BitmapData bmpData;
	Gdiplus::Rect rect(0, 0, static_cast<INT>(pBmp->GetWidth()),
		static_cast<INT>(pBmp->GetHeight()));
	if (pBmp->LockBits(&rect,
		Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
		PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok) {
		return;
	}
	for (int y = 0; y < rect.Height; ++y) {
		DWORD* row = reinterpret_cast<DWORD*>(
			reinterpret_cast<BYTE*>(bmpData.Scan0) + y * bmpData.Stride);
		for (int x = 0; x < rect.Width; ++x) {
			const DWORD pixel = row[x];
			const BYTE alpha = static_cast<BYTE>((pixel >> 24) & 0xFF);
			const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xFF);
			const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xFF);
			const BYTE blue = static_cast<BYTE>(pixel & 0xFF);
			if (alpha != 0 && red >= 242 && green <= 16 && blue >= 230) {
				row[x] = 0x00000000;
			}
		}
	}
	pBmp->UnlockBits(&bmpData);
}

// ========== E5 文件加载 ==========

Gdiplus::Bitmap* LoadBitmapFromE5(const CString& e5FilePath, int imageIndex,
	CString& debugInfo, bool applyTopLeftTransparency)
{
	CFile file;
	if (!file.Open(e5FilePath, CFile::modeRead | CFile::shareDenyNone)) {
		debugInfo.Format(_T("open fail: %s"), (LPCTSTR)e5FilePath);
		return nullptr;
	}

	ULONGLONG indexOffset = 0x110 + (ULONGLONG)imageIndex * 12;
	if (indexOffset + 12 > file.GetLength()) {
		debugInfo.Format(_T("index out of range: idx=%d, off=%llu, flen=%llu"), imageIndex, indexOffset, file.GetLength());
		file.Close();
		return nullptr;
	}
	file.Seek(indexOffset, CFile::begin);

	BYTE rawBytes[12] = {};
	file.Read(rawBytes, 12);

	// E5 big-endian: 手动从字节组装
	DWORD imageSize = ((DWORD)rawBytes[0] << 24) | ((DWORD)rawBytes[1] << 16) | ((DWORD)rawBytes[2] << 8) | rawBytes[3];
	DWORD dataOffset = ((DWORD)rawBytes[8] << 24) | ((DWORD)rawBytes[9] << 16) | ((DWORD)rawBytes[10] << 8) | rawBytes[11];

	if (imageSize == 0 || (ULONGLONG)dataOffset + imageSize > file.GetLength()) {
		debugInfo.Format(_T("idx=%d raw=%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X sz=%u off=%u flen=%llu"),
			imageIndex,
			rawBytes[0], rawBytes[1], rawBytes[2], rawBytes[3],
			rawBytes[4], rawBytes[5], rawBytes[6], rawBytes[7],
			rawBytes[8], rawBytes[9], rawBytes[10], rawBytes[11],
			imageSize, dataOffset, file.GetLength());
		file.Close();
		return nullptr;
	}

	file.Seek(dataOffset, CFile::begin);
	BYTE* buffer = new BYTE[imageSize];
	UINT bytesRead = file.Read(buffer, imageSize);
	file.Close();

	if (bytesRead < imageSize) {
		debugInfo.Format(_T("read incomplete: got %u, want %u"), bytesRead, imageSize);
		delete[] buffer;
		return nullptr;
	}

	// 先尝试GDI+标准解码(BMP/PNG/JPG)
	bool isStandard = false;
	if (imageSize >= 2 && buffer[0] == 'B' && buffer[1] == 'M') isStandard = true;
	if (imageSize >= 4 && buffer[0] == 0x89 && buffer[1] == 0x50) isStandard = true;
	if (imageSize >= 2 && buffer[0] == 0xFF && buffer[1] == 0xD8) isStandard = true;

	if (isStandard) {
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, imageSize);
		if (hMem) {
			void* pMem = GlobalLock(hMem);
			memcpy(pMem, buffer, imageSize);
			GlobalUnlock(hMem);
			IStream* pStream = nullptr;
			if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
				Gdiplus::Bitmap* pBmp = Gdiplus::Bitmap::FromStream(pStream);
				pStream->Release();
				if (pBmp && pBmp->GetLastStatus() == Gdiplus::Ok) {
					delete[] buffer;
					if (applyTopLeftTransparency) ApplyTopLeftTransparency(pBmp);
					return pBmp;
				}
				if (pBmp) delete pBmp;
			} else {
				GlobalFree(hMem);
			}
		}
	}

	// 非标准格式: 尝试作为256色裸像素数据(48x1280)解析
	Gdiplus::Bitmap* pBmp = CreateBitmapFromRaw8bpp(buffer, imageSize);
	delete[] buffer;

	if (pBmp) return pBmp;

	debugInfo.Format(_T("unknown format idx=%d sz=%u"), imageIndex, imageSize);
	return nullptr;
}

bool CRScenePreviewPage::LoadMapImage(int mapID, const wchar_t* modPath)
{
	if (mapID < 0) { m_debugInfo = _T("mapID < 0"); return false; }
	if (mapID == m_cachedMapID && m_pMapBitmap) return true;
	const auto cached = m_mapBitmapCache.find(mapID);
	if (cached != m_mapBitmapCache.end() && cached->second != nullptr) {
		m_pMapBitmap = cached->second;
		m_cachedMapID = mapID;
		return true;
	}
	m_pMapBitmap = nullptr;
	m_cachedMapID = -1;

	CString rootPath = GetRootPath(modPath);
	if (rootPath.IsEmpty()) { m_debugInfo = _T("path parse fail"); return false; }
	CString e5Path = rootPath + _T("E5\\Mmap.e5");

	m_pMapBitmap = LoadBitmapFromE5(e5Path, mapID, m_debugInfo);
	if (m_pMapBitmap) {
		m_cachedMapID = mapID;
		m_mapBitmapCache.emplace(mapID, m_pMapBitmap);
		return true;
	}
	return false;
}

bool CRScenePreviewPage::LoadSpriteData(const wchar_t* modPath)
{
	if (m_spriteDataLoaded) return true;

	CString rootPath = GetRootPath(modPath);
	if (rootPath.IsEmpty()) return false;

	// 读Ekd5.exe偏移0xE1000处的形象编号表 (1024 * 2字节, 小端序)
	CString exePath = rootPath + _T("Ekd5.exe");
	CFile file;
	if (!file.Open(exePath, CFile::modeRead | CFile::shareDenyNone)) return false;

	ULONGLONG offset = 0xE1000;
	if (offset + 1024 * 2 > file.GetLength()) {
		file.Close();
		return false;
	}
	file.Seek(offset, CFile::begin);
	file.Read(m_charImageIDs, 1024 * 2); // 小端序, 直接读
	file.Close();

	m_spriteDataLoaded = true;
	return true;
}

Gdiplus::Bitmap* CRScenePreviewPage::GetSpriteBitmap(int charID, const wchar_t* modPath)
{
	if (charID < 0 || charID >= 1024) return nullptr;
	if (!LoadSpriteData(modPath)) return nullptr;

	int imageID = m_charImageIDs[charID];
	int frontIdx = imageID * 2;
	int backIdx = imageID * 2 + 1;

	// 确保两张都加载了
	if (m_spriteCache.find(frontIdx) == m_spriteCache.end()) {
		CString rootPath = GetRootPath(modPath);
		CString e5Path = rootPath + _T("Pmapobj.e5");
		CString dbg;
		Gdiplus::Bitmap* pFront = LoadBitmapFromE5(e5Path, frontIdx, dbg);
		m_spriteCache[frontIdx] = pFront;
		if (!pFront) {
			m_debugInfo.Format(_T("char%d img%d front[%d]: %s | %s"),
				charID, imageID, frontIdx, (LPCTSTR)dbg, (LPCTSTR)e5Path);
		}
		Gdiplus::Bitmap* pBack = LoadBitmapFromE5(e5Path, backIdx, dbg);
		m_spriteCache[backIdx] = pBack;
	}

	return nullptr;
}

bool CRScenePreviewPage::LoadPortraitData(const wchar_t* modPath)
{
	if (m_portraitDataLoaded) return true;

	CString rootPath = GetRootPath(modPath);
	if (rootPath.IsEmpty()) {
		AppendDiagnostic(_T("map portrait data path parse fail"));
		return false;
	}

	CString dataPath = rootPath + _T("Data.e5");
	CFile file;
	if (!file.Open(dataPath, CFile::modeRead | CFile::shareDenyNone)) {
		CString msg;
		msg.Format(_T("map portrait Data.e5 open fail: %s"), (LPCTSTR)dataPath);
		AppendDiagnostic(msg);
		return false;
	}

	// Data.e5 人物记录：基址0x18C、步长0x20、头像字段偏移0x0D，LE u16。
	const ULONGLONG firstAvatarOffset = 0x18C + 0x0D;
	const ULONGLONG lastAvatarEnd = 0x18C + 1023ULL * 0x20 + 0x0D + 2;
	if (lastAvatarEnd > static_cast<ULONGLONG>(file.GetLength())) {
		file.Close();
		AppendDiagnostic(_T("map portrait Data.e5 avatar table truncated"));
		return false;
	}

	m_portraitFaceIndices.clear();
	for (int personID = 0; personID < 1024; personID++) {
		const ULONGLONG offset = firstAvatarOffset + static_cast<ULONGLONG>(personID) * 0x20;
		if (file.Seek(static_cast<LONGLONG>(offset), CFile::begin) < 0) {
			m_portraitFaceIndices.clear();
			file.Close();
			AppendDiagnostic(_T("map portrait Data.e5 seek fail"));
			return false;
		}
		BYTE raw[2] = {};
		if (file.Read(raw, 2) != 2) {
			m_portraitFaceIndices.clear();
			file.Close();
			AppendDiagnostic(_T("map portrait Data.e5 avatar read fail"));
			return false;
		}
		const unsigned short avatarCode =
			static_cast<unsigned short>(raw[0] | (static_cast<unsigned short>(raw[1]) << 8));
		const int faceIndex = ccz::scene_preview::face_image_index_for_data_person(
			personID, avatarCode);
		if (faceIndex < 0) {
			m_portraitFaceIndices.clear();
			file.Close();
			AppendDiagnostic(_T("map portrait face index mapping failed"));
			return false;
		}
		m_portraitFaceIndices.emplace(personID, faceIndex);
	}

	file.Close();
	m_portraitDataLoaded = true;
	return true;
}

Gdiplus::Bitmap* CRScenePreviewPage::GetPortraitBitmap(int personID, const wchar_t* modPath)
{
	if (!ccz::scene_preview::is_direct_data_portrait_target(personID)) return nullptr;

	auto cached = m_portraitCache.find(personID);
	if (cached != m_portraitCache.end()) return cached->second;

	if (!LoadPortraitData(modPath)) {
		m_portraitCache.emplace(personID, nullptr);
		return nullptr;
	}

	auto faceIt = m_portraitFaceIndices.find(personID);
	if (faceIt == m_portraitFaceIndices.end()) {
		m_portraitCache.emplace(personID, nullptr);
		return nullptr;
	}

	CString rootPath = GetRootPath(modPath);
	CString facePath = rootPath + _T("E5\\Face.e5");
	CString debugInfo;
	Gdiplus::Bitmap* portrait = LoadBitmapFromE5(
		facePath, faceIt->second, debugInfo, false);
	m_portraitCache.emplace(personID, portrait);
	if (!portrait) {
		CString msg;
		msg.Format(_T("map portrait person=%d face=%d load fail: %s"),
			personID, faceIt->second, (LPCTSTR)debugInfo);
		AppendDiagnostic(msg);
	}
	return portrait;
}

void CRScenePreviewPage::DrawMapPortrait(Gdiplus::Graphics& g,
	const ccz::scene_preview::MapPortraitPlacement& portrait,
	int offsetX, int offsetY, float scale, float alpha)
{
	alpha = (std::max)(0.0f, (std::min)(1.0f, alpha));
	Gdiplus::Bitmap* pPortrait = nullptr;
	if (portrait.personID >= 0 && portrait.personID < 1024 && !m_modPath.IsEmpty()) {
		pPortrait = GetPortraitBitmap(portrait.personID, m_modPath.GetString());
	}

	const int drawX = offsetX + static_cast<int>(std::lround(portrait.x * scale));
	const int drawY = offsetY + static_cast<int>(std::lround(portrait.y * scale));
	const int logicalW = pPortrait ? static_cast<int>(pPortrait->GetWidth()) : 120;
	const int logicalH = pPortrait ? static_cast<int>(pPortrait->GetHeight()) : 120;
	const int drawW = (std::max)(1, static_cast<int>(std::lround(logicalW * scale)));
	const int drawH = (std::max)(1, static_cast<int>(std::lround(logicalH * scale)));

	if (pPortrait) {
		if (alpha >= 0.999f) {
			g.DrawImage(pPortrait, Gdiplus::Rect(drawX, drawY, drawW, drawH),
				0, 0, logicalW, logicalH, Gdiplus::UnitPixel);
		} else {
			Gdiplus::ColorMatrix cm = {
				1, 0, 0, 0, 0,
				0, 1, 0, 0, 0,
				0, 0, 1, 0, 0,
				0, 0, 0, alpha, 0,
				0, 0, 0, 0, 1
			};
			Gdiplus::ImageAttributes ia;
			ia.SetColorMatrix(&cm);
			g.DrawImage(pPortrait, Gdiplus::Rect(drawX, drawY, drawW, drawH),
				0, 0, logicalW, logicalH, Gdiplus::UnitPixel, &ia);
		}
		return;
	}

	// 资源缺失时保留位置占位，避免静默吞掉指令；实际图像可在诊断信息中定位。
	const BYTE placeholderAlpha = static_cast<BYTE>(100.0f * alpha);
	const BYTE outlineAlpha = static_cast<BYTE>(220.0f * alpha);
	Gdiplus::SolidBrush placeholderBrush(
		Gdiplus::Color(placeholderAlpha, 180, 50, 50));
	Gdiplus::Pen placeholderPen(
		Gdiplus::Color(outlineAlpha, 255, 120, 120), 1.0f);
	g.FillRectangle(&placeholderBrush, drawX, drawY, drawW, drawH);
	g.DrawRectangle(&placeholderPen, drawX, drawY, drawW - 1, drawH - 1);
	Gdiplus::Font labelFont(L"Microsoft YaHei", 8);
	Gdiplus::SolidBrush labelBrush(
		Gdiplus::Color(static_cast<BYTE>(230.0f * alpha), 255, 220, 220));
	CString label;
	label.Format(_T("face %d"), portrait.personID);
	g.DrawString(label, -1, &labelFont,
		Gdiplus::PointF(static_cast<float>(drawX + 4), static_cast<float>(drawY + 4)),
		&labelBrush);
}

void CRScenePreviewPage::DrawCharacter(Gdiplus::Graphics& g, const CharacterState& ch,
	int canvasOffX, int canvasOffY, float scale)
{
	if (ch.id < 0 || ch.id >= 1024 || !m_spriteDataLoaded) return;

	int imageID = m_charImageIDs[ch.id];
	int frontIdx = imageID * 2;
	int backIdx = imageID * 2 + 1;

	// 方向决定使用正面/背面以及是否翻转
	// 南(2)=正面, 东(1)=正面翻转, 北(0)=背面, 西(3)=背面翻转
	bool useFront = (ch.direction == 2 || ch.direction == 1);
	bool flipH = (ch.direction == 1 || ch.direction == 3);

	int spriteIdx = useFront ? frontIdx : backIdx;
	auto it = m_spriteCache.find(spriteIdx);
	Gdiplus::Bitmap* pSheet = (it != m_spriteCache.end()) ? it->second : nullptr;

	CPoint gamePx = GameToPixel(ch.x, ch.y);
	int drawX = canvasOffX + (int)(gamePx.x * scale);
	int drawY = canvasOffY + (int)(gamePx.y * scale);
	const bool selected = IsCharacterSelected(ch.id);
	const bool primary = IsPrimaryCharacter(ch.id);
	auto drawSelectionMarker = [&](int markerW, int markerH) {
		if (!selected) return;
		const Gdiplus::Color markerColor = primary
			? Gdiplus::Color(255, 255, 190, 35)
			: Gdiplus::Color(245, 40, 210, 255);
		const float penWidth = (std::max)(2.0f, 2.0f * scale);
		Gdiplus::Pen markerPen(markerColor, penWidth);
		const int pad = (std::max)(3, (int)std::lround(3.0f * scale));
		g.DrawRectangle(&markerPen,
			drawX - markerW / 2 - pad,
			drawY - markerH - pad,
			markerW + pad * 2,
			markerH + pad * 2);
		if (primary) {
			Gdiplus::SolidBrush primaryBrush(markerColor);
			const int markerSize = (std::max)(5, (int)std::lround(6.0f * scale));
			g.FillEllipse(&primaryBrush,
				drawX - markerSize / 2,
				drawY - markerH - pad - markerSize - 2,
				markerSize,
				markerSize);
		}
	};

	if (!pSheet) {
		// fallback: draw circle placeholder with debug info
		int radius = (int)(12 * scale);
		if (radius < 6) radius = 6;
		Gdiplus::SolidBrush circleBrush(Gdiplus::Color(180, 30, 100, 220));
		g.FillEllipse(&circleBrush, drawX - radius, drawY - radius, radius * 2, radius * 2);
		Gdiplus::Pen circlePen(Gdiplus::Color(220, 255, 255, 255), 1.5f);
		g.DrawEllipse(&circlePen, drawX - radius, drawY - radius, radius * 2, radius * 2);
		DrawDirectionArrow(g, drawX, drawY, ch.direction);

		CString label;
		label.Format(_T("c%d/i%d"), ch.id, imageID);
		Gdiplus::Font labelFont(L"Microsoft YaHei", 7);
		Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
		Gdiplus::StringFormat sf;
		sf.SetAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::RectF labelRect((float)(drawX - 30), (float)(drawY - radius),
			60.0f, (float)(radius * 2));
		g.DrawString(label, -1, &labelFont, labelRect, &sf, &textBrush);
		drawSelectionMarker(radius * 2, radius * 2);
		return;
	}

	// 计算帧: 动作0=帧0, 动作N(N>=1)=帧N+2
	const int FRAME_W = 48, FRAME_H = 64;
	int frame = (ch.action == 0) ? 0 : (ch.action + 2);
	int maxFrames = (int)pSheet->GetHeight() / FRAME_H;
	if (frame >= maxFrames) frame = 0;

	int srcY = frame * FRAME_H;

	// 绘制尺寸 (缩放)
	int dstW = (int)(FRAME_W * scale);
	int dstH = (int)(FRAME_H * scale);
	// 角色脚底对齐到坐标点
	int dstX = drawX - dstW / 2;
	int dstY = drawY - dstH;

	if (flipH) {
		// 水平翻转: 用矩阵变换
		Gdiplus::Matrix oldMatrix;
		g.GetTransform(&oldMatrix);

		// 翻转: 先平移到中心, 缩放X为-1, 再平移回来
		Gdiplus::Matrix flipMatrix;
		flipMatrix.Translate((float)(dstX + dstW), (float)dstY);
		flipMatrix.Scale(-1.0f, 1.0f);
		flipMatrix.Translate(0, 0);
		g.SetTransform(&flipMatrix);

		g.DrawImage(pSheet,
			Gdiplus::Rect(0, 0, dstW, dstH),
			0, srcY, FRAME_W, FRAME_H,
			Gdiplus::UnitPixel);

		g.SetTransform(&oldMatrix);
	}
	else {
		g.DrawImage(pSheet,
			Gdiplus::Rect(dstX, dstY, dstW, dstH),
			0, srcY, FRAME_W, FRAME_H,
			Gdiplus::UnitPixel);
	}
	drawSelectionMarker(dstW, dstH);
}

void CRScenePreviewPage::DrawCharacterGhost(Gdiplus::Graphics& g, const CharacterState& ch,
	int gameX, int gameY, int canvasOffX, int canvasOffY, float scale, float alpha)
{
	if (ch.id < 0 || ch.id >= 1024 || !m_spriteDataLoaded) return;

	const int imageID = m_charImageIDs[ch.id];
	const bool useFront = (ch.direction == 2 || ch.direction == 1);
	const bool flipH = (ch.direction == 1 || ch.direction == 3);
	const int spriteIdx = useFront ? imageID * 2 : imageID * 2 + 1;
	auto it = m_spriteCache.find(spriteIdx);
	Gdiplus::Bitmap* pSheet = (it != m_spriteCache.end()) ? it->second : nullptr;

	CPoint gamePx = GameToPixel(gameX, gameY);
	const int drawX = canvasOffX + (int)(gamePx.x * scale);
	const int drawY = canvasOffY + (int)(gamePx.y * scale);
	const int frameW = 48, frameH = 64;
	const int frame = (ch.action == 0) ? 0 : (ch.action + 2);
	const int dstW = (std::max)(1, (int)(frameW * scale));
	const int dstH = (std::max)(1, (int)(frameH * scale));
	const int dstX = drawX - dstW / 2;
	const int dstY = drawY - dstH;

	if (pSheet) {
		int safeFrame = frame;
		const int maxFrames = (int)pSheet->GetHeight() / frameH;
		if (safeFrame >= maxFrames) safeFrame = 0;
		const int srcY = safeFrame * frameH;
		const float safeAlpha = (std::max)(0.0f, (std::min)(1.0f, alpha));
		Gdiplus::ColorMatrix cm = {
			1, 0, 0, 0, 0,
			0, 1, 0, 0, 0,
			0, 0, 1, 0, 0,
			0, 0, 0, safeAlpha, 0,
			0, 0, 0, 0, 1
		};
		Gdiplus::ImageAttributes imageAttributes;
		imageAttributes.SetColorMatrix(&cm);
		if (flipH) {
			Gdiplus::Matrix oldMatrix;
			g.GetTransform(&oldMatrix);
			Gdiplus::Matrix flipMatrix;
			flipMatrix.Translate((float)(dstX + dstW), (float)dstY);
			flipMatrix.Scale(-1.0f, 1.0f);
			g.SetTransform(&flipMatrix);
			g.DrawImage(pSheet, Gdiplus::Rect(0, 0, dstW, dstH),
				0, srcY, frameW, frameH, Gdiplus::UnitPixel, &imageAttributes);
			g.SetTransform(&oldMatrix);
		} else {
			g.DrawImage(pSheet, Gdiplus::Rect(dstX, dstY, dstW, dstH),
				0, srcY, frameW, frameH, Gdiplus::UnitPixel, &imageAttributes);
		}
		return;
	}

	const int radius = (std::max)(6, (int)(12 * scale));
	const BYTE brushAlpha = (BYTE)(90.0f * (std::max)(0.0f, (std::min)(1.0f, alpha)));
	Gdiplus::SolidBrush ghostBrush(Gdiplus::Color(brushAlpha, 30, 100, 220));
	g.FillEllipse(&ghostBrush, drawX - radius, drawY - radius, radius * 2, radius * 2);
}

// ========== 更新入口 ==========

void CRScenePreviewPage::UpdatePreview(CTreeCtrl& tree, HTREEITEM hSelectedItem, const wchar_t* modPath)
{
	CString newModPath = modPath ? modPath : L"";
	m_internalizationFallbackActive = false;
	m_visibleDialogueOverlay = {};

	// 检测mod路径变化 — 切换到不同mod的剧本时清除所有缓存
	CString newRoot = GetRootPath(newModPath);
	CString oldRoot = GetRootPath(m_modPath);
	if (newRoot.CompareNoCase(oldRoot) != 0) {
		// Undo entries contain handles into the current document tree.  A Mod
		// switch may reuse those handles, so invalidate the stack before any
		// asset/cache refresh can publish the new document.
		ClearPreviewUndo();

		ClearRBitmapCaches();
	}

	m_modPath = newModPath;
	m_debugInfo.Empty();
	m_pTreeCtrl = &tree;
	m_hCurItem = hSelectedItem;

	// The current PreviewSession frame is the sole selector for the native
	// scene.  ProcessInstruction remains the editor-owned behavior for every
	// selected native instruction, including backgrounds, branches, movement,
	// and map portraits.
	const auto& frame = m_previewSession.frame();
	const NativePlanApplyResult apply = ApplyNativeReplayPlan(
		tree, frame.native_plan);
	if (!apply.applied) {
		// Explicit safety fallback for invalid internal input only.  The helper
		// immediately suppresses the enhancement views for this refresh.
		EvaluateToItem(tree, hSelectedItem);
		HideInternalizedEnhancementViews(apply.diagnostic);
	} else {
		if (!apply.diagnostic.empty()) {
			AppendDiagnostic(CString(apply.diagnostic.c_str()));
		}
		m_visibleDialogueOverlay = frame.dialogue;
	}
	ReconcileCharacterSelection();

	// `modPath` can alias m_modPath when the playback Stop button restores the
	// static preview.  The assignment above may reallocate m_modPath's CString
	// buffer, so never dereference that original pointer after the assignment;
	// use the pane-owned copy for every asset load below.
	if (m_state.mapImageIndex >= 0 && !m_modPath.IsEmpty()) {
		if (m_state.mapImageIndex != m_cachedMapID) {
			LoadMapImage(m_state.mapImageIndex, m_modPath.GetString());
		}
	}

	// 确保角色形象数据已加载
	if (!m_modPath.IsEmpty() && !m_spriteDataLoaded) {
		LoadSpriteData(m_modPath.GetString());
	}

	// 预加载当前场景中角色的精灵图
	if (!m_modPath.IsEmpty()) {
		for (const auto& ch : m_state.characters) {
			GetSpriteBitmap(ch.id, m_modPath.GetString());
		}
		for (const auto& portrait : m_state.mapPortraits.items()) {
			GetPortraitBitmap(portrait.personID, m_modPath.GetString());
		}
	}

	// HideInternalizedEnhancementViews performs the one invalidation for its
	// bounded fallback refresh; avoid repainting twice in that path.
	if (!m_internalizationFallbackActive) {
		Invalidate();
	}
}

// ========== 渲染 ==========

void CRScenePreviewPage::OnPaint()
{
	CPaintDC dc(this);
	CRect clientRect;
	GetClientRect(&clientRect);
	// The canvas ends before the editor-owned condition controls.  Child
	// windows are therefore never painted over by the scene renderer.
	const CRect canvasRect = SceneCanvasRect(clientRect);
	// OnEraseBkgnd is suppressed. Clear only page bands outside the off-screen
	// canvas, never the canvas itself: clearing the whole client here exposed a
	// blank frame on every mouse-move repaint before the back-buffer BitBlt.
	const CRect uncoveredRects[] = {
		CRect(clientRect.left, clientRect.top, clientRect.right, canvasRect.top),
		CRect(clientRect.left, canvasRect.bottom, clientRect.right, clientRect.bottom),
		CRect(clientRect.left, canvasRect.top, canvasRect.left, canvasRect.bottom),
		CRect(canvasRect.right, canvasRect.top, clientRect.right, canvasRect.bottom)
	};
	for (const auto& uncoveredRect : uncoveredRects) {
		if (uncoveredRect.Width() > 0 && uncoveredRect.Height() > 0)
			dc.FillSolidRect(uncoveredRect, RGB(32, 32, 32));
	}

	if (canvasRect.Width() <= 0 || canvasRect.Height() <= 0) return;

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, canvasRect.Width(), canvasRect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&bmp);

	memDC.FillSolidRect(0, 0, canvasRect.Width(), canvasRect.Height(), RGB(32, 32, 32));

	Gdiplus::Graphics graphics(memDC.m_hDC);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);

	const int MAP_W = 640, MAP_H = 400;
	float scaleX = (float)canvasRect.Width() / MAP_W;
	float scaleY = (float)canvasRect.Height() / MAP_H;
	float scale = min(scaleX, scaleY);

	int canvasW = (int)(MAP_W * scale);
	int canvasH = (int)(MAP_H * scale);
	int offsetX = (canvasRect.Width() - canvasW) / 2;
	int offsetY = (canvasRect.Height() - canvasH) / 2;
	const SceneState paintState = DynamicPaintState();
	const SceneState& renderState = paintState;

	// 保存渲染参数(相对于窗口的实际坐标)
	m_offsetX = offsetX + canvasRect.left;
	m_offsetY = offsetY + canvasRect.top;
	m_scale = scale;

	// 绘制背景地图
	if (m_pMapBitmap) {
		graphics.DrawImage(m_pMapBitmap,
			Gdiplus::Rect(offsetX, offsetY, canvasW, canvasH));
	} else {
		Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 48, 48, 48));
		graphics.FillRectangle(&bgBrush, offsetX, offsetY, canvasW, canvasH);
		Gdiplus::Pen borderPen(Gdiplus::Color(255, 80, 80, 80), 1.0f);
		graphics.DrawRectangle(&borderPen, offsetX, offsetY, canvasW - 1, canvasH - 1);

		if (renderState.mapID >= 0) {
			Gdiplus::Font font(L"Microsoft YaHei", 9);
			Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 200, 150, 150));
			CString msg;
			msg.Format(_T("mapID=%d  %s"), renderState.mapID, (LPCTSTR)m_debugInfo);
			graphics.DrawString(msg, -1, &font,
				Gdiplus::PointF((float)offsetX + 10, (float)offsetY + 10), &textBrush);
		}
	}
	// 绘制等角网格
	if (m_bShowGrid) {
		Gdiplus::Pen gridPen(Gdiplus::Color(80, 200, 200, 200), 1.0f);
		const int G_MIN = 0, G_MAX = 100;
		for (int i = G_MIN; i <= G_MAX; i++) {
			CPoint p1 = GameToPixel(i, G_MIN);
			CPoint p2 = GameToPixel(i, G_MAX);
			graphics.DrawLine(&gridPen,
				offsetX + (int)(p1.x * scale), offsetY + (int)(p1.y * scale),
				offsetX + (int)(p2.x * scale), offsetY + (int)(p2.y * scale));
			p1 = GameToPixel(G_MIN, i);
			p2 = GameToPixel(G_MAX, i);
			graphics.DrawLine(&gridPen,
				offsetX + (int)(p1.x * scale), offsetY + (int)(p1.y * scale),
				offsetX + (int)(p2.x * scale), offsetY + (int)(p2.y * scale));
		}
	}

	// 绘制外场景地图头像：坐标直接使用Mmap.e5逻辑像素，不经过等角网格换算。
	for (const auto& portrait : renderState.mapPortraits.items()) {
		DrawMapPortrait(graphics, portrait, offsetX, offsetY, scale);
	}

	// 地图头像显示/移动模式：复用角色放置/移动的半透明跟随预览，
	// 但头像坐标是外场景像素，锚点保持与 0x29/0x2A 相同的左上角。
	if ((m_bPortraitPlaceMode && m_portraitPlacePersonID >= 0 &&
			m_portraitPlaceCursorPos.x >= 0) ||
		(m_bPortraitMoveMode && m_movePortraitPersonID >= 0 &&
			m_portraitMoveCursorPos.x >= 0)) {
		const int personID = m_bPortraitPlaceMode ?
			m_portraitPlacePersonID : m_movePortraitPersonID;
		ccz::scene_preview::MapPortraitPlacement ghost;
		ghost.personID = personID;
		ghost.x = m_snappedGamePos.x;
		ghost.y = m_snappedGamePos.y;
		DrawMapPortrait(graphics, ghost, offsetX, offsetY, scale, 0.5f);

		const int targetX = offsetX + static_cast<int>(std::lround(ghost.x * scale));
		const int targetY = offsetY + static_cast<int>(std::lround(ghost.y * scale));
		Gdiplus::Pen targetPen(Gdiplus::Color(220, 80, 210, 255), 2.0f);
		const int crossSize = (std::max)(4, static_cast<int>(8 * scale));
		graphics.DrawLine(&targetPen, targetX - crossSize, targetY,
			targetX + crossSize, targetY);
		graphics.DrawLine(&targetPen, targetX, targetY - crossSize,
			targetX, targetY + crossSize);
	}

	// 绘制角色形象
	for (const auto& ch : renderState.characters) {
		DrawCharacter(graphics, ch, offsetX, offsetY, scale);
	}

	// 多选点移动模式：所有选中角色预览在同一个逻辑坐标，统一采用主角色朝向。
	if (m_bPointMoveMode && m_pointMoveCursorPos.x >= 0) {
		const int targetX = m_snappedGamePos.x;
		const int targetY = m_snappedGamePos.y;
		for (const auto& ch : renderState.characters) {
			if (!IsCharacterSelected(ch.id)) continue;
			CharacterState ghost = ch;
			ghost.direction = m_multiMoveDirection;
			DrawCharacterGhost(graphics, ghost, targetX, targetY,
				offsetX, offsetY, scale, 0.5f);
		}

		CPoint targetPx = GameToPixel(targetX, targetY);
		const int screenX = offsetX + (int)(targetPx.x * scale);
		const int screenY = offsetY + (int)(targetPx.y * scale);
		Gdiplus::Pen targetPen(Gdiplus::Color(220, 80, 210, 255), 2.0f);
		const int crossSize = (std::max)(4, (int)(8 * scale));
		graphics.DrawLine(&targetPen, screenX - crossSize, screenY,
			screenX + crossSize, screenY);
		graphics.DrawLine(&targetPen, screenX, screenY - crossSize,
			screenX, screenY + crossSize);
	}

	// 多选移动模式：以主角色为锚点，绘制所有选中角色的整体平移预览。
	if (m_bMultiMoveMode && m_multiMoveCursorPos.x >= 0) {
		const int deltaX = m_snappedGamePos.x - m_multiMoveAnchor.x;
		const int deltaY = m_snappedGamePos.y - m_multiMoveAnchor.y;
		for (const auto& ch : renderState.characters) {
			if (!IsCharacterSelected(ch.id)) continue;
			CharacterState ghost = ch;
			ghost.direction = m_multiMoveDirection;
			DrawCharacterGhost(graphics, ghost, ch.x + deltaX, ch.y + deltaY,
				offsetX, offsetY, scale, 0.5f);
		}

		CPoint targetPx = GameToPixel(m_snappedGamePos.x, m_snappedGamePos.y);
		const int targetX = offsetX + (int)(targetPx.x * scale);
		const int targetY = offsetY + (int)(targetPx.y * scale);
		Gdiplus::Pen targetPen(Gdiplus::Color(220, 255, 210, 50), 2.0f);
		const int crossSize = (std::max)(4, (int)(8 * scale));
		graphics.DrawLine(&targetPen, targetX - crossSize, targetY, targetX + crossSize, targetY);
		graphics.DrawLine(&targetPen, targetX, targetY - crossSize, targetX, targetY + crossSize);
	}

	// 放置/移动模式: 绘制跟随鼠标的角色预览(半透明)
	{
		int ghostCharID = -1;
		CPoint ghostCursor = { -1, -1 };
		int ghostDir = 2, ghostAction = 0;

		if (m_bPlaceMode && m_selectedCharID >= 0 && m_placeCursorPos.x >= 0) {
			ghostCharID = m_selectedCharID;
			ghostCursor = m_placeCursorPos;
			ghostDir = m_placeDir;
			ghostAction = m_placeAction;
		}
		else if (m_bMoveMode && m_moveCharID >= 0 && m_moveCursorPos.x >= 0) {
			ghostCharID = m_moveCharID;
			ghostCursor = m_moveCursorPos;
			// 移动模式用角色当前的朝向和动作
			for (const auto& ch : m_state.characters) {
				if (ch.id == m_moveCharID) {
					ghostDir = ch.direction;
					ghostAction = ch.action;
					break;
				}
			}
		}

		if (ghostCharID >= 0 && ghostCursor.x >= 0) {
			CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含Ctrl吸附)
			Gdiplus::ColorMatrix cm = {
				1, 0, 0, 0, 0,
				0, 1, 0, 0, 0,
				0, 0, 1, 0, 0,
				0, 0, 0, 0.5f, 0,
				0, 0, 0, 0, 1
			};
			Gdiplus::ImageAttributes ia;
			ia.SetColorMatrix(&cm);

			CPoint gamePxForSnap = GameToPixel(game.x, game.y);
			int snapScreenX = offsetX + (int)(gamePxForSnap.x * scale);
			int snapScreenY = offsetY + (int)(gamePxForSnap.y * scale);

			if (m_spriteDataLoaded && ghostCharID >= 0 && ghostCharID < 1024) {
				int imageID = m_charImageIDs[ghostCharID];
				bool useFront = (ghostDir == 2 || ghostDir == 1);
				bool flipH = (ghostDir == 1 || ghostDir == 3);
				int spriteIdx = useFront ? imageID * 2 : imageID * 2 + 1;
				auto it = m_spriteCache.find(spriteIdx);
				Gdiplus::Bitmap* pSheet = (it != m_spriteCache.end()) ? it->second : nullptr;
				if (pSheet) {
					const int FRAME_W = 48, FRAME_H = 64;
					int frame = (ghostAction == 0) ? 0 : (ghostAction + 2);
					int maxFrames = (int)pSheet->GetHeight() / FRAME_H;
					if (frame >= maxFrames) frame = 0;
					int srcY = frame * FRAME_H;
					int dstW = (int)(FRAME_W * scale);
					int dstH = (int)(FRAME_H * scale);
					int dstX = snapScreenX - dstW / 2;
					int dstY = snapScreenY - dstH;
					if (flipH) {
						Gdiplus::Matrix oldM;
						graphics.GetTransform(&oldM);
						Gdiplus::Matrix flipM;
						flipM.Translate((float)(dstX + dstW), (float)dstY);
						flipM.Scale(-1.0f, 1.0f);
						graphics.SetTransform(&flipM);
						graphics.DrawImage(pSheet,
							Gdiplus::Rect(0, 0, dstW, dstH),
							0, srcY, FRAME_W, FRAME_H,
							Gdiplus::UnitPixel, &ia);
						graphics.SetTransform(&oldM);
					} else {
						graphics.DrawImage(pSheet,
							Gdiplus::Rect(dstX, dstY, dstW, dstH),
							0, srcY, FRAME_W, FRAME_H,
							Gdiplus::UnitPixel, &ia);
					}
				} else {
					int radius = (int)(12 * scale);
					if (radius < 6) radius = 6;
					Gdiplus::SolidBrush ghostBrush(Gdiplus::Color(90, 30, 100, 220));
					graphics.FillEllipse(&ghostBrush, snapScreenX - radius, snapScreenY - radius, radius * 2, radius * 2);
				}
			}

			// 吸附时画十字标记
			if (m_bSnapped) {
				Gdiplus::Pen snapPen(Gdiplus::Color(220, 0, 255, 100), 2.0f);
				int crossSize = (int)(8 * scale);
				if (crossSize < 4) crossSize = 4;
				graphics.DrawLine(&snapPen, snapScreenX - crossSize, snapScreenY,
					snapScreenX + crossSize, snapScreenY);
				graphics.DrawLine(&snapPen, snapScreenX, snapScreenY - crossSize,
					snapScreenX, snapScreenY + crossSize);
			}
		}
	}

	// 状态信息
	{
		static const wchar_t* s_dirNames[] = { L"北", L"东", L"南", L"西" };
		static const wchar_t* s_gesNames[] = {
			L"普通",L"下跪",L"脸红",L"举手",L"哭",L"伸手",L"作揖",
			L"盘坐脸红",L"盘坐举手",L"盘坐哭",L"倒下",L"单膝跪地",L"被缚",
			L"挥剑扬起",L"挥剑劈下",L"活埋",L"起身",L"单手举起",L"未知",L"变量",L"无"
		};

		Gdiplus::Font infoFont(L"Microsoft YaHei", 9);
		Gdiplus::SolidBrush infoBrush(Gdiplus::Color(200, 200, 200, 200));

		const bool outerScene = renderState.coordinateSpace ==
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels;
		const bool innerScene = renderState.coordinateSpace ==
			ccz::scene_preview::SceneCoordinateSpace::InnerLogical;
		const wchar_t* sceneLabel = outerScene ? L"外场景" :
			innerScene ? L"内场景" : L"场景未知";
		int displayedMapNumber = -1;
		ccz::scene_preview::try_resolve_background_script_number(
			renderState.mapImageIndex, displayedMapNumber);

		CString info;
		info.Format(_T("%s  地图号:%d  角色:%d  地图头像:%d"), sceneLabel,
			displayedMapNumber,
			(int)renderState.characters.size(),
			(int)renderState.mapPortraits.items().size());
		if (!m_debugInfo.IsEmpty()) {
			info += _T("  | ");
			info += m_debugInfo;
		}

		// 鼠标在画布区域内时显示坐标和角色信息
		if (m_mousePos.y >= m_canvasTop) {
			// 拖拽模式下显示吸附后的坐标
			bool inDragMode = m_bAdjustMode || m_bPortraitAdjustMode ||
				m_bPortraitPlaceMode || m_bPortraitMoveMode || m_bMoveMode ||
				m_bPlaceMode || m_bMultiMoveMode || m_bPointMoveMode;
			CPoint game = inDragMode ? m_snappedGamePos :
				ScenePointFromScreen(m_mousePos.x, m_mousePos.y);
			const bool outerPixels = m_bPortraitAdjustMode ||
				m_bPortraitPlaceMode || m_bPortraitMoveMode ||
				renderState.coordinateSpace == ccz::scene_preview::SceneCoordinateSpace::OuterPixels;
			const wchar_t* coordinateLabel = outerPixels ?
				L"像素坐标" : L"逻辑坐标";
			CString posStr;
			if (m_bSnapped && inDragMode) {
				posStr.Format(_T("  %s:(%d,%d) [吸附]"), coordinateLabel, game.x, game.y);
			} else {
				posStr.Format(_T("  %s:(%d,%d)"), coordinateLabel, game.x, game.y);
			}
			info += posStr;

			// 检查鼠标下方的角色
			int hoverIdx = HitTestCharacter(m_mousePos);
			if (hoverIdx >= 0 && hoverIdx < (int)m_state.characters.size()) {
				const auto& ch = m_state.characters[hoverIdx];
				// 找到角色名称
				CString charName;
				if (m_pPer2) {
					charName = m_pPer2[ch.id];
				}
				if (charName.IsEmpty()) {
					charName.Format(_T("ID:%d"), ch.id);
				}

				const wchar_t* dirName = (ch.direction >= 0 && ch.direction < 4) ?
					s_dirNames[ch.direction] : L"?";
				const wchar_t* gesName = (ch.action >= 0 && ch.action < 21) ?
					s_gesNames[ch.action] : L"?";
				CString charInfo;
				charInfo.Format(_T("  [%s] 朝向:%s 动作:%s 位置:(%d,%d)"),
					(LPCTSTR)charName, dirName, gesName, ch.x, ch.y);
				info += charInfo;
			}
		}

		// 拖拽模式下显示当前朝向和动作
		if (m_bPlaceMode && m_selectedCharID >= 0) {
			const wchar_t* dName = (m_placeDir >= 0 && m_placeDir < 4) ?
				s_dirNames[m_placeDir] : L"?";
			const wchar_t* aName = (m_placeAction >= 0 && m_placeAction < 21) ?
				s_gesNames[m_placeAction] : L"?";
			CString placeInfo;
			placeInfo.Format(_T("  放置:朝%s %s (滚轮换向/Shift+滚轮换动作)"), dName, aName);
			info += placeInfo;
		}
		else if (m_bPortraitPlaceMode && m_portraitPlacePersonID >= 0) {
			info += _T("  放置地图头像 (外场景像素坐标)");
		}
		else if (m_bPortraitMoveMode && m_movePortraitPersonID >= 0) {
			info += _T("  移动地图头像 (外场景像素坐标)");
		}
		else if ((m_bAdjustMode && m_adjustCharID >= 0) ||
				 (m_bMoveMode && m_moveCharID >= 0)) {
			int targetID = m_bAdjustMode ? m_adjustCharID : m_moveCharID;
			for (const auto& ch : m_state.characters) {
				if (ch.id == targetID) {
					const wchar_t* dName = (ch.direction >= 0 && ch.direction < 4) ?
						s_dirNames[ch.direction] : L"?";
					const wchar_t* aName = (ch.action >= 0 && ch.action < 21) ?
						s_gesNames[ch.action] : L"?";
					CString modeInfo;
					modeInfo.Format(_T("  朝%s %s (滚轮换向/Shift+滚轮换动作)"), dName, aName);
					info += modeInfo;
					break;
				}
			}
		}

		graphics.DrawString(info, -1, &infoFont,
			Gdiplus::PointF(5.0f, 5.0f), &infoBrush);
	}

	// Keep the controls visually grouped as a player bar.  The buttons are
	// child windows, while this strip is painted into the scene back buffer so
	// it never leaves a white gap between the scene and dialogue panel.
	if (m_playbackVisible) {
		ccz::editor_preview::OverlayLayout playerLayout;
		const CRect overlayClient(0, 0, canvasRect.Width(), canvasRect.Height());
		int barTop = canvasRect.Height() - kPlaybackBarHeight -
			kConditionButtonMargin;
		if (ccz::editor_preview::CalculateDialogueOverlayLayout(
				overlayClient, playerLayout)) {
			barTop = playerLayout.panel.top - kPlaybackBarHeight -
				kConditionButtonMargin;
		}
		barTop = (std::max)(0, (std::min)(canvasRect.Height() -
			kPlaybackBarHeight, barTop));
		Gdiplus::SolidBrush playerBrush(Gdiplus::Color(230, 24, 26, 31));
		graphics.FillRectangle(&playerBrush, 0, barTop,
			canvasRect.Width(), kPlaybackBarHeight);
	}

	// Dialogue is rendered into the same back buffer as the scene.  Always
	// paint the panel, including the hidden model, so a previous portrait/body
	// is cleared without a follower window or global overlay state.
	{
		const CRect overlayClient(0, 0, canvasRect.Width(), canvasRect.Height());
		ccz::editor_preview::OverlayLayout overlayLayout;
		if (ccz::editor_preview::CalculateDialogueOverlayLayout(
				overlayClient, overlayLayout)) {
			ccz::editor_preview::PaintDialogueOverlay(
				memDC, overlayLayout, m_visibleDialogueOverlay);
		}
	}

	dc.BitBlt(canvasRect.left, canvasRect.top, canvasRect.Width(), canvasRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

// ========== 武将列表 ==========

void CRScenePreviewPage::InitCharList(wchar_t(*pPer2)[30])
{
	if (!pPer2) return;
	// 如果per2指针变了(切换到不同View/mod)，需要重新加载列表
	if (m_charListInited && m_pPer2 == pPer2) {
		return;
	}
	m_pPer2 = pPer2;
	m_charListInited = true;
	RefreshCharList();
}

void CRScenePreviewPage::RefreshCharList()
{
	if (!m_listChars.GetSafeHwnd()) return;
	if (!m_pPer2) return;

	CString filter;
	m_editSearch.GetWindowTextW(filter);
	filter.Trim();

	m_listChars.ResetContent();
	for (int i = 0; i < 1024; i++) {
		if (filter.IsEmpty() || wcsstr(m_pPer2[i], (LPCTSTR)filter) != nullptr) {
			int idx = m_listChars.AddString(m_pPer2[i]);
			m_listChars.SetItemData(idx, (DWORD_PTR)i);
		}
	}
	m_selectedCharID = -1;
}

void CRScenePreviewPage::OnSearchChange()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	RefreshCharList();
}

void CRScenePreviewPage::OnCharListSelChange()
{
	if (m_dynamicPlaybackActive) StopDynamicPlayback(true);
	if (m_bPlaceMode || m_bPortraitPlaceMode) {
		ReleaseCapture();
		m_bPlaceMode = false;
		m_bPortraitPlaceMode = false;
	}
	int sel = m_listChars.GetCurSel();
	if (sel >= 0) {
		m_selectedCharID = (int)m_listChars.GetItemData(sel);
		if (m_state.coordinateSpace ==
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels) {
			// 外场景列表选择复用角色放置流程，但预览/写回对象改为地图头像。
			m_portraitPlacePersonID = m_selectedCharID;
			m_portraitPlaceCursorPos = { -1, -1 };
			m_bPortraitPlaceMode = true;
			m_bPlaceMode = false;
			if (!m_modPath.IsEmpty())
				GetPortraitBitmap(m_portraitPlacePersonID, m_modPath);
		} else {
			// 内场景保持原有 R 形象放置逻辑。
			m_bPlaceMode = true;
			m_placeCursorPos = { -1, -1 };
			m_placeDir = 2;      // 默认朝南
			m_placeAction = 0;   // 默认普通动作
			if (!m_modPath.IsEmpty())
				GetSpriteBitmap(m_selectedCharID, m_modPath);
		}
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	} else {
		m_selectedCharID = -1;
		m_bPlaceMode = false;
		m_bPortraitPlaceMode = false;
		m_portraitPlacePersonID = -1;
	}
}

// ========== 坐标反算 ==========

CPoint CRScenePreviewPage::PixelToGame(int screenX, int screenY)
{
	float cx = (screenX - m_offsetX) / m_scale;
	float cy = (screenY - m_offsetY) / m_scale;
	// px = 8*(X-Y+42), py = 4*(X+Y-50)
	// X-Y = px/8 - 42, X+Y = py/4 + 50
	float xMinusY = cx / 8.0f - 42.0f;
	float xPlusY = cy / 4.0f + 50.0f;
	int gx = (int)roundf((xMinusY + xPlusY) / 2.0f);
	int gy = (int)roundf((xPlusY - xMinusY) / 2.0f);
	return CPoint(gx, gy);
}

CPoint CRScenePreviewPage::ScenePointFromScreen(int screenX, int screenY) const
{
	ccz::scene_preview::SceneCoordinateSpace space = m_state.coordinateSpace;
	// 未加载背景时保留原有内场景坐标提示/拖动行为。
	if (space == ccz::scene_preview::SceneCoordinateSpace::Unknown)
		space = ccz::scene_preview::SceneCoordinateSpace::InnerLogical;
	const ccz::scene_preview::ScenePoint point =
		ccz::scene_preview::screen_to_scene_point(
			space, screenX, screenY, m_offsetX, m_offsetY, m_scale);
	return CPoint(point.x, point.y);
}

CPoint CRScenePreviewPage::SnapToGrid(CPoint gamePos, int excludeCharID)
{
	// 收集所有角色坐标（排除正在操作的角色）
	std::vector<CPoint> pts;
	for (const auto& ch : m_state.characters) {
		if (ch.id == excludeCharID) continue;
		pts.push_back(CPoint(ch.x, ch.y));
	}
	if (pts.empty()) return gamePos;

	// === 第一优先级: 等距延伸候选 ===
	std::vector<CPoint> spacingCands;
	if (pts.size() >= 2) {
		for (int i = 0; i < (int)pts.size(); i++) {
			for (int j = 0; j < (int)pts.size(); j++) {
				if (i == j) continue;
				int dx = pts[j].x - pts[i].x;
				int dy = pts[j].y - pts[i].y;
				if (dx == 0 && dy == 0) continue;

				// 检查i→j之间是否有其他角色（非相邻对则跳过）
				bool hasMiddle = false;
				for (int k = 0; k < (int)pts.size(); k++) {
					if (k == i || k == j) continue;
					int dxik = pts[k].x - pts[i].x;
					int dyik = pts[k].y - pts[i].y;
					if (dx != 0 && dy != 0) {
						if ((long long)dxik * dy == (long long)dyik * dx &&
							dxik * dx > 0 && dyik * dy > 0 &&
							abs(dxik) < abs(dx) && abs(dyik) < abs(dy)) {
							hasMiddle = true; break;
						}
					} else if (dx == 0) {
						if (dxik == 0 && dyik * dy > 0 && abs(dyik) < abs(dy)) {
							hasMiddle = true; break;
						}
					} else {
						if (dyik == 0 && dxik * dx > 0 && abs(dxik) < abs(dx)) {
							hasMiddle = true; break;
						}
					}
				}
				if (hasMiddle) continue;

				// j向同方向延伸1~3步，取最近一个空位
				for (int step = 1; step <= 3; step++) {
					CPoint ext(pts[j].x + dx * step, pts[j].y + dy * step);
					bool occupied = false;
					for (const auto& p : pts) {
						if (p.x == ext.x && p.y == ext.y) { occupied = true; break; }
					}
					if (!occupied) {
						spacingCands.push_back(ext);
						break;
					}
				}
			}
		}
	}

	// 在等距候选中找最近的
	float bestSpacingDist = 1e9f;
	CPoint bestSpacingPt = gamePos;
	for (const auto& c : spacingCands) {
		float fdx = (float)(c.x - gamePos.x);
		float fdy = (float)(c.y - gamePos.y);
		float dist = fdx * fdx + fdy * fdy;
		if (dist < bestSpacingDist) {
			bestSpacingDist = dist;
			bestSpacingPt = c;
		}
	}
	if (bestSpacingDist <= 25.0f) return bestSpacingPt; // 5格距离

	// === 第二优先级: 单轴对齐 ===
	float bestAlignDist = 1e9f;
	CPoint bestAlignPt = gamePos;
	for (const auto& p : pts) {
		float fdx = (float)(p.x - gamePos.x);
		if (fdx * fdx < bestAlignDist) {
			bestAlignDist = fdx * fdx;
			bestAlignPt = CPoint(p.x, gamePos.y);
		}
		float fdy = (float)(p.y - gamePos.y);
		if (fdy * fdy < bestAlignDist) {
			bestAlignDist = fdy * fdy;
			bestAlignPt = CPoint(gamePos.x, p.y);
		}
	}
	if (bestAlignDist <= 9.0f) return bestAlignPt; // 3格距离

	return gamePos;
}

int CRScenePreviewPage::HitTestCharacter(CPoint screenPt)
{
	const int FRAME_W = 48, FRAME_H = 64;
	int bestIdx = -1;
	float bestDist = 1e9f;

	for (int i = 0; i < (int)m_state.characters.size(); i++) {
		const auto& ch = m_state.characters[i];
		CPoint gp = GameToPixel(ch.x, ch.y);
		float sx = m_offsetX + gp.x * m_scale;
		float sy = m_offsetY + gp.y * m_scale;
		float dstW = FRAME_W * m_scale;
		float dstH = FRAME_H * m_scale;
		float left = sx - dstW / 2;
		float top = sy - dstH;
		if (screenPt.x >= left && screenPt.x <= left + dstW &&
			screenPt.y >= top && screenPt.y <= top + dstH) {
			float dx = screenPt.x - sx;
			float dy = screenPt.y - (sy - dstH / 2);
			float dist = dx * dx + dy * dy;
			if (dist < bestDist) {
				bestDist = dist;
				bestIdx = i;
			}
		}
	}
	return bestIdx;
}

int CRScenePreviewPage::HitTestMapPortrait(CPoint screenPt)
{
	// 头像按绘制顺序从后往前命中，重叠时优先最上层头像。
	const auto& portraits = m_state.mapPortraits.items();
	for (auto it = portraits.rbegin(); it != portraits.rend(); ++it) {
		int logicalW = 120;
		int logicalH = 120;
		auto cached = m_portraitCache.find(it->personID);
		if (cached != m_portraitCache.end() && cached->second) {
			logicalW = static_cast<int>(cached->second->GetWidth());
			logicalH = static_cast<int>(cached->second->GetHeight());
		}

		const float left = m_offsetX + it->x * m_scale;
		const float top = m_offsetY + it->y * m_scale;
		const float right = left + logicalW * m_scale;
		const float bottom = top + logicalH * m_scale;
		if (screenPt.x >= left && screenPt.x <= right &&
			screenPt.y >= top && screenPt.y <= bottom)
			return it->personID;
	}
	return -1;
}

// ========== 获取活动View ==========

static CcczEditor2View* GetActiveEditorView()
{
	CMDIFrameWnd* pMain = (CMDIFrameWnd*)AfxGetMainWnd();
	if (!pMain) return nullptr;
	CMDIChildWnd* pChild = pMain->MDIGetActive();
	if (!pChild) return nullptr;
	CView* pView = pChild->GetActiveView();
	if (!pView) return nullptr;
	return dynamic_cast<CcczEditor2View*>(pView);
}

// ========== 点击放置角色 ==========

void CRScenePreviewPage::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_dynamicPlaybackActive) return;
	if (point.y < m_canvasTop) {
		CWnd::OnLButtonDown(nFlags, point);
		return;
	}

	// 地图头像调整位置：只写回当前有效的显示/移动坐标来源。
	if (m_bPortraitPlaceMode && m_portraitPlacePersonID >= 0) {
		m_portraitPlaceCursorPos = point;
		const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
			point.x, point.y, m_offsetX, m_offsetY, m_scale);
		m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
		m_bSnapped = false;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPortraitMoveMode && m_movePortraitPersonID >= 0) {
		m_portraitMoveCursorPos = point;
		const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
			point.x, point.y, m_offsetX, m_offsetY, m_scale);
		m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
		m_bSnapped = false;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPortraitAdjustMode && m_adjustPortraitPersonID >= 0) {
		ReleaseCapture();
		const CPoint scenePoint = m_snappedGamePos;
		CommitMapPortraitPosition(
			m_adjustPortraitPersonID, scenePoint.x, scenePoint.y);
		m_bPortraitAdjustMode = false;
		m_adjustPortraitPersonID = -1;
		m_portraitAdjustCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 调整位置模式：确认时修改当前有效的出现/移动指令坐标和朝向。
	if (m_bAdjustMode && m_adjustCharID >= 0) {
		ReleaseCapture();
		CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)

		// 找到section中该角色的武将出现指令并修改坐标/朝向/动作。
		CharacterState adjustedState{};
		bool foundAdjustedState = false;
		for (const auto& ch : m_state.characters) {
			if (ch.id == m_adjustCharID) {
				adjustedState = ch;
				adjustedState.x = game.x;
				adjustedState.y = game.y;
				foundAdjustedState = true;
				break;
			}
		}
		if (foundAdjustedState)
			CommitCharacterStates({ adjustedState }, true);
		m_bAdjustMode = false;
		m_adjustCharID = -1;
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 多选移动模式：点击目标位置后为选中角色批量插入移动指令。
	if (m_bMultiMoveMode) {
		ReleaseCapture();
		CommitMultiMove();
		m_bMultiMoveMode = false;
		m_multiMoveAnchor = CPoint(-1, -1);
		m_multiMoveCursorPos = CPoint(-1, -1);
		m_multiMoveCharIDs.clear();
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 多选点移动模式：点击目标位置后，把所有选中角色写回同一个逻辑坐标。
	if (m_bPointMoveMode) {
		CPoint game = PixelToGame(point.x, point.y);
		if ((nFlags & MK_CONTROL) != 0) {
			CPoint snapped = SnapToGrid(game, m_primaryCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_pointMoveCursorPos = point;
		m_snappedGamePos = game;
		ReleaseCapture();
		CommitPointMove();
		m_bPointMoveMode = false;
		m_pointMoveCursorPos = CPoint(-1, -1);
		m_pointMoveCharIDs.clear();
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}

	// 地图头像移动模式：沿用角色“移动到…”的跟随/点击流程，写入 0x2A。
	if (m_bPortraitMoveMode && m_movePortraitPersonID >= 0) {
		ReleaseCapture();
		const CPoint pixel = m_snappedGamePos;
		InsertMapPortraitMoveInstruction(
			m_movePortraitPersonID, pixel.x, pixel.y);
		m_bPortraitMoveMode = false;
		m_movePortraitPersonID = -1;
		m_portraitMoveCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 移动模式: 点击目标位置
	if (m_bMoveMode && m_moveCharID >= 0) {
		ReleaseCapture();
		CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)
		InsertMoveInstruction(m_moveCharID, game.x, game.y);
		m_bMoveMode = false;
		m_moveCharID = -1;
		m_moveCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 地图头像放置模式：沿用角色列表放置流程，写入 0x29。
	if (m_bPortraitPlaceMode && m_portraitPlacePersonID >= 0) {
		ReleaseCapture();
		CcczEditor2View* pView = GetActiveEditorView();
		if (pView) {
			const CPoint pixel = m_snappedGamePos;
			const HTREEITEM restoreSelection = pView->cur_item;
			InsertMapPortraitShowInstruction(
				m_portraitPlacePersonID, pixel.x, pixel.y,
				restoreSelection, true);
		}
		m_listChars.SetCurSel(-1);
		m_selectedCharID = -1;
		m_portraitPlacePersonID = -1;
		m_bPortraitPlaceMode = false;
		m_portraitPlaceCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 放置角色模式
	if (m_bPlaceMode && m_selectedCharID >= 0) {
		ReleaseCapture();
		CcczEditor2View* pView = GetActiveEditorView();
		if (pView) {
			CPoint game = m_snappedGamePos; // OnMouseMove中已计算(含吸附)
			const HTREEITEM restoreSelection = pView->cur_item;
			const int intData[5] = { m_selectedCharID, game.x, game.y,
				m_placeDir, m_placeAction };
			const HTREEITEM inserted = pView->InsertInstructionAt(
				48, restoreSelection, intData, 5);
			if (inserted != nullptr) {
				RecordInsertedUndo(L"放置角色", { inserted }, restoreSelection);
			}
		}
		m_listChars.SetCurSel(-1);
		m_selectedCharID = -1;
		m_bPlaceMode = false;
		m_placeCursorPos = { -1, -1 };
		m_bSnapped = false;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return;
	}

	// 空闲画布中的普通点击：切换角色选中状态。
	int hitIdx = HitTestCharacter(point);
	if (hitIdx >= 0 && hitIdx < (int)m_state.characters.size()) {
		m_hitCharID = m_state.characters[hitIdx].id;
		ToggleCharacterSelection(m_hitCharID);
		return;
	}

	CWnd::OnLButtonDown(nFlags, point);
}

void CRScenePreviewPage::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (m_dynamicPlaybackActive) return;
	if (point.y < m_canvasTop) {
		CWnd::OnLButtonDblClk(nFlags, point);
		return;
	}

	// 操作模式下双击不改变当前拖动/放置流程。
	if (m_bPortraitAdjustMode || m_bPortraitPlaceMode || m_bPortraitMoveMode ||
		m_bAdjustMode || m_bMoveMode || m_bPlaceMode ||
		m_bMultiMoveMode || m_bPointMoveMode) {
		CWnd::OnLButtonDblClk(nFlags, point);
		return;
	}

	// 角色选择完全由单击 ToggleCharacterSelection 处理；双击不再重复操作。
	// 双击空白仍保留“取消全部选择”的快捷操作。
	if (HitTestCharacter(point) < 0 && HitTestMapPortrait(point) < 0) {
		ClearCharacterSelection();
		return;
	}

	CWnd::OnLButtonDblClk(nFlags, point);
}

HTREEITEM CRScenePreviewPage::InsertAppearInstruction(
	int charID, int screenX, int screenY, bool record_undo)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;

	CPoint game = PixelToGame(screenX, screenY);
	int intData[5] = { charID, game.x, game.y, 2, 0 };
	const HTREEITEM restoreSelection = pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		48, restoreSelection, intData, 5);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"放置角色", { inserted }, restoreSelection);
	}
	return inserted;
}

// ========== 右键菜单 ==========

void CRScenePreviewPage::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (m_dynamicPlaybackActive) return;
	// 特殊模式下不弹菜单
	if (m_bPortraitAdjustMode || m_bPortraitPlaceMode || m_bPortraitMoveMode ||
		m_bPlaceMode || m_bAdjustMode || m_bMoveMode ||
		m_bMultiMoveMode || m_bPointMoveMode) return;

	// 右键取消模式后屏蔽一次菜单
	if (m_bSuppressContextMenu) {
		m_bSuppressContextMenu = false;
		return;
	}

	CPoint clientPt = point;
	ScreenToClient(&clientPt);

	if (clientPt.y < m_canvasTop) {
		CWnd::OnContextMenu(pWnd, point);
		return;
	}

	int hitIdx = HitTestCharacter(clientPt);
	if (hitIdx < 0) {
		const int portraitID = HitTestMapPortrait(clientPt);
		if (portraitID >= 0) {
			CMenu portraitMenu;
			portraitMenu.CreatePopupMenu();
			portraitMenu.AppendMenuW(MF_STRING, ID_SCENE_ADJUST, L"调整位置");
			portraitMenu.AppendMenuW(MF_STRING, ID_SCENE_MOVE, L"移动到...");
			portraitMenu.AppendMenuW(MF_STRING, ID_SCENE_DISAPPEAR, L"消失");
			const int cmd = (int)portraitMenu.TrackPopupMenu(
				TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this);
			if (cmd == ID_SCENE_ADJUST) {
				m_bPortraitAdjustMode = true;
				m_adjustPortraitPersonID = portraitID;
				m_portraitAdjustCursorPos = clientPt;
				if (const auto* portrait = m_state.mapPortraits.find(portraitID))
					m_snappedGamePos = CPoint(portrait->x, portrait->y);
				else {
					const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
						ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
						clientPt.x, clientPt.y, m_offsetX, m_offsetY, m_scale);
					m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
				}
				m_bSnapped = false;
				SetCapture();
				SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
			} else if (cmd == ID_SCENE_MOVE) {
				m_bPortraitMoveMode = true;
				m_movePortraitPersonID = portraitID;
				m_portraitMoveCursorPos = { -1, -1 };
				m_bSnapped = false;
				SetCapture();
				SetCursor(LoadCursor(nullptr, IDC_CROSS));
			} else if (cmd == ID_SCENE_DISAPPEAR) {
				InsertMapPortraitHideInstruction(portraitID);
			}
			return;
		}
	}

	// 空地右键: 显示网格开关
	if (hitIdx < 0) {
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenuW(MF_STRING | (m_bShowGrid ? MF_CHECKED : 0),
			ID_SCENE_TOGGLE_GRID, L"显示网格");
		int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this);
		if (cmd == ID_SCENE_TOGGLE_GRID) {
			m_bShowGrid = !m_bShowGrid;
			Invalidate();
		}
		return;
	}

	m_hitCharID = m_state.characters[hitIdx].id;
	if (m_selectedCharIDs.size() >= 2 && IsCharacterSelected(m_hitCharID)) {
		ShowMultiContextMenu(point);
		return;
	}

	static const wchar_t* dirNames[] = { L"朝北", L"朝东", L"朝南", L"朝西" };
	static const wchar_t* actionNames[] = {
		L"普通", L"下跪", L"脸红", L"举手", L"哭", L"伸手", L"作揖",
		L"盘坐脸红", L"盘坐举手", L"盘坐哭", L"倒下", L"单膝跪地", L"被缚",
		L"挥剑扬起", L"挥剑劈下", L"活埋", L"起身", L"单手举起", L"未知", L"变量"
	};

	CMenu menuDir;
	menuDir.CreatePopupMenu();
	for (int i = 0; i < 4; i++)
		menuDir.AppendMenuW(MF_STRING, ID_SCENE_DIR_0 + i, dirNames[i]);

	CMenu menuAction;
	menuAction.CreatePopupMenu();
	for (int i = 0; i < 20; i++)
		menuAction.AppendMenuW(MF_STRING, ID_SCENE_ACTION_0 + i, actionNames[i]);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenuW(MF_STRING, ID_SCENE_ADJUST, L"调整位置");
	menu.AppendMenuW(MF_SEPARATOR, 0, (LPCTSTR)nullptr);
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuDir.m_hMenu, L"设置朝向");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuAction.m_hMenu, L"设置动作");
	menu.AppendMenuW(MF_STRING, ID_SCENE_MOVE, L"移动到...");
	menu.AppendMenuW(MF_STRING, ID_SCENE_DISAPPEAR, L"消失");

	int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, point.x, point.y, this);
	if (cmd >= ID_SCENE_DIR_0 && cmd <= ID_SCENE_DIR_3) {
		InsertTurnInstruction(m_hitCharID, cmd - ID_SCENE_DIR_0);
	} else if (cmd >= ID_SCENE_ACTION_0 && cmd <= ID_SCENE_ACTION_19) {
		InsertActionInstruction(m_hitCharID, cmd - ID_SCENE_ACTION_0);
	} else if (cmd == ID_SCENE_DISAPPEAR) {
		InsertDisappearInstruction(m_hitCharID);
	} else if (cmd == ID_SCENE_MOVE) {
		m_bMoveMode = true;
		m_moveCharID = m_hitCharID;
		m_moveCursorPos = { -1, -1 };
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	} else if (cmd == ID_SCENE_ADJUST) {
		m_bAdjustMode = true;
		m_adjustCharID = m_hitCharID;
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	}
}

void CRScenePreviewPage::ShowMultiContextMenu(CPoint screenPoint)
{
	static const wchar_t* dirNames[] = { L"朝北", L"朝东", L"朝南", L"朝西" };
	static const wchar_t* actionNames[] = {
		L"普通", L"下跪", L"脸红", L"举手", L"哭", L"伸手", L"作揖",
		L"盘坐脸红", L"盘坐举手", L"盘坐哭", L"倒下", L"单膝跪地", L"被缚",
		L"挥剑扬起", L"挥剑劈下", L"活埋", L"起身", L"单手举起", L"未知", L"变量"
	};

	CMenu menuDir;
	menuDir.CreatePopupMenu();
	for (int i = 0; i < 4; ++i)
		menuDir.AppendMenuW(MF_STRING, ID_SCENE_DIR_0 + i, dirNames[i]);

	CMenu menuAction;
	menuAction.CreatePopupMenu();
	for (int i = 0; i < 20; ++i)
		menuAction.AppendMenuW(MF_STRING, ID_SCENE_ACTION_0 + i, actionNames[i]);

	CMenu menuPrimaryAlign;
	menuPrimaryAlign.CreatePopupMenu();
	menuPrimaryAlign.AppendMenuW(MF_STRING, ID_SCENE_PRIMARY_ALIGN_X, L"同 X 坐标");
	menuPrimaryAlign.AppendMenuW(MF_STRING, ID_SCENE_PRIMARY_ALIGN_Y, L"同 Y 坐标");

	CMenu menuAlign;
	menuAlign.CreatePopupMenu();
	menuAlign.AppendMenuW(MF_STRING, ID_SCENE_MULTI_ALIGN, L"快速对齐");
	menuAlign.AppendMenuW(MF_POPUP, (UINT_PTR)menuPrimaryAlign.m_hMenu, L"对齐主角色");

	CMenu menuPreciseDistribution;
	menuPreciseDistribution.CreatePopupMenu();
	menuPreciseDistribution.AppendMenuW(MF_STRING, ID_SCENE_PRECISE_DISTRIBUTE_X, L"按 X 坐标");
	menuPreciseDistribution.AppendMenuW(MF_STRING, ID_SCENE_PRECISE_DISTRIBUTE_Y, L"按 Y 坐标");

	CMenu menuDistribution;
	menuDistribution.CreatePopupMenu();
	menuDistribution.AppendMenuW(MF_STRING, ID_SCENE_MULTI_DISTRIBUTE, L"一键等距");
	menuDistribution.AppendMenuW(MF_POPUP, (UINT_PTR)menuPreciseDistribution.m_hMenu, L"精准等距");

	CMenu menuMove;
	menuMove.CreatePopupMenu();
	menuMove.AppendMenuW(MF_STRING, ID_SCENE_MULTI_GROUP_MOVE, L"组移动");
	menuMove.AppendMenuW(MF_STRING, ID_SCENE_MULTI_POINT_MOVE, L"点移动");

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuAlign.m_hMenu, L"对齐");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuDistribution.m_hMenu, L"等距");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuAction.m_hMenu, L"统一动作");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuDir.m_hMenu, L"统一朝向");
	menu.AppendMenuW(MF_POPUP, (UINT_PTR)menuMove.m_hMenu, L"批量移动");
	menu.AppendMenuW(MF_STRING, ID_SCENE_MULTI_DISAPPEAR, L"批量消失");
	menu.AppendMenuW(MF_SEPARATOR, 0, (LPCTSTR)nullptr);
	menu.AppendMenuW(MF_STRING | (IsPrimaryCharacter(m_hitCharID) ? MF_CHECKED : 0),
		ID_SCENE_MULTI_SET_PRIMARY, L"设为主角色");
	menu.AppendMenuW(MF_STRING, ID_SCENE_MULTI_REMOVE_SELECTED, L"取消选择该角色");

	int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD,
		screenPoint.x, screenPoint.y, this);
	if (cmd == ID_SCENE_MULTI_ALIGN) {
		ApplyMultiAlignment();
	} else if (cmd == ID_SCENE_MULTI_DISTRIBUTE) {
		ApplyMultiDistribution();
	} else if (cmd == ID_SCENE_PRIMARY_ALIGN_X) {
		ApplyPrimaryAlignment(true);
	} else if (cmd == ID_SCENE_PRIMARY_ALIGN_Y) {
		ApplyPrimaryAlignment(false);
	} else if (cmd == ID_SCENE_PRECISE_DISTRIBUTE_X) {
		ApplyPreciseDistribution(true);
	} else if (cmd == ID_SCENE_PRECISE_DISTRIBUTE_Y) {
		ApplyPreciseDistribution(false);
	} else if (cmd >= ID_SCENE_DIR_0 && cmd <= ID_SCENE_DIR_3) {
		ApplyMultiDirection(cmd - ID_SCENE_DIR_0);
	} else if (cmd >= ID_SCENE_ACTION_0 && cmd <= ID_SCENE_ACTION_19) {
		ApplyMultiAction(cmd - ID_SCENE_ACTION_0);
	} else if (cmd == ID_SCENE_MULTI_GROUP_MOVE) {
		BeginMultiMove();
	} else if (cmd == ID_SCENE_MULTI_POINT_MOVE) {
		BeginPointMove();
	} else if (cmd == ID_SCENE_MULTI_DISAPPEAR) {
		ApplyMultiDisappear();
	} else if (cmd == ID_SCENE_MULTI_SET_PRIMARY) {
		SetPrimaryCharacter(m_hitCharID);
	} else if (cmd == ID_SCENE_MULTI_REMOVE_SELECTED) {
		RemoveCharacterSelection(m_hitCharID);
	}
}

void CRScenePreviewPage::ApplyMultiAlignment()
{
	std::vector<ccz::scene_preview::LayoutActor> layout;
	std::vector<std::size_t> selected;
	layout.reserve(m_state.characters.size());
	selected.reserve(m_selectedCharIDs.size());

	for (std::size_t i = 0; i < m_state.characters.size(); ++i) {
		const auto& character = m_state.characters[i];
		layout.push_back({ character.id, character.x, character.y });
		if (IsCharacterSelected(character.id))
			selected.push_back(i);
	}

	const auto result = ccz::scene_preview::AlignNearLines(layout, selected);
	if (!result.changed) return;

	std::vector<CharacterState> updates;
	updates.reserve(selected.size());
	for (std::size_t index : selected) {
		CharacterState state = m_state.characters[index];
		state.x = layout[index].x;
		state.y = layout[index].y;
		updates.push_back(state);
	}
	CommitCharacterStates(updates, L"快速对齐");
}

void CRScenePreviewPage::ApplyMultiDistribution()
{
	std::vector<ccz::scene_preview::LayoutActor> layout;
	std::vector<std::size_t> selected;
	layout.reserve(m_state.characters.size());
	selected.reserve(m_selectedCharIDs.size());

	for (std::size_t i = 0; i < m_state.characters.size(); ++i) {
		const auto& character = m_state.characters[i];
		layout.push_back({ character.id, character.x, character.y });
		if (IsCharacterSelected(character.id))
			selected.push_back(i);
	}

	const auto result = ccz::scene_preview::DistributeAxes(layout, selected);
	if (!result.changed) return;

	std::vector<CharacterState> updates;
	updates.reserve(selected.size());
	for (std::size_t index : selected) {
		CharacterState state = m_state.characters[index];
		state.x = layout[index].x;
		state.y = layout[index].y;
		updates.push_back(state);
	}
	CommitCharacterStates(updates, L"一键等距");
}

void CRScenePreviewPage::ApplyPrimaryAlignment(bool alignX)
{
	std::vector<ccz::scene_preview::LayoutActor> layout;
	std::vector<std::size_t> selected;
	layout.reserve(m_state.characters.size());
	selected.reserve(m_selectedCharIDs.size());

	std::size_t primaryIndex = m_state.characters.size();
	for (std::size_t i = 0; i < m_state.characters.size(); ++i) {
		const auto& character = m_state.characters[i];
		layout.push_back({ character.id, character.x, character.y });
		if (IsCharacterSelected(character.id))
			selected.push_back(i);
		if (character.id == m_primaryCharID)
			primaryIndex = i;
	}

	const auto result = ccz::scene_preview::AlignToPrimaryAxis(
		layout, primaryIndex, selected, alignX);
	if (!result.changed) return;

	std::vector<CharacterState> updates;
	updates.reserve(selected.size());
	for (std::size_t index : selected) {
		CharacterState state = m_state.characters[index];
		state.x = layout[index].x;
		state.y = layout[index].y;
		updates.push_back(state);
	}
	CommitCharacterStates(updates, alignX ? L"对齐主角色 X" : L"对齐主角色 Y");
}

void CRScenePreviewPage::ApplyPreciseDistribution(bool distributeX)
{
	std::vector<ccz::scene_preview::LayoutActor> layout;
	std::vector<std::size_t> selected;
	layout.reserve(m_state.characters.size());
	selected.reserve(m_selectedCharIDs.size());

	for (std::size_t i = 0; i < m_state.characters.size(); ++i) {
		const auto& character = m_state.characters[i];
		layout.push_back({ character.id, character.x, character.y });
		if (IsCharacterSelected(character.id))
			selected.push_back(i);
	}

	const auto result = ccz::scene_preview::PreciseDistributeAxis(
		layout, selected, distributeX);
	if (!result.changed) return;

	std::vector<CharacterState> updates;
	updates.reserve(selected.size());
	for (std::size_t index : selected) {
		CharacterState state = m_state.characters[index];
		state.x = layout[index].x;
		state.y = layout[index].y;
		updates.push_back(state);
	}
	CommitCharacterStates(updates, distributeX ? L"精准等距 X" : L"精准等距 Y");
}

void CRScenePreviewPage::ApplyMultiDirection(int direction)
{
	if (m_selectedCharIDs.size() < 2) return;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView || !pView->cur_item) return;
	const std::vector<int> selected = m_selectedCharIDs;
	const HTREEITEM restoreSelection = pView->cur_item;
	const BOOL wasModified = pView->GetDocument()->IsModified();
	HTREEITEM anchor = pView->cur_item;
	std::vector<HTREEITEM> insertedItems;
	insertedItems.reserve(selected.size());
	const bool committed = ccz::scene_preview::ApplyBatchTransaction<int, HTREEITEM>(
		selected,
		[&](int charID) {
			HTREEITEM inserted = InsertTurnInstruction(
				charID, direction, anchor, false);
			if (inserted) anchor = inserted;
			if (inserted) insertedItems.push_back(inserted);
			return inserted;
		},
		[&](HTREEITEM inserted) { RemoveInsertedInstruction(inserted); },
		[](HTREEITEM inserted) { return inserted != nullptr; });
	if (!committed) {
		RestoreAfterBatchFailure(restoreSelection, wasModified);
	} else {
		RecordInsertedUndo(L"统一朝向", std::move(insertedItems),
			restoreSelection);
	}
}

void CRScenePreviewPage::ApplyMultiAction(int action)
{
	if (m_selectedCharIDs.size() < 2) return;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView || !pView->cur_item) return;
	const std::vector<int> selected = m_selectedCharIDs;
	const HTREEITEM restoreSelection = pView->cur_item;
	const BOOL wasModified = pView->GetDocument()->IsModified();
	HTREEITEM anchor = pView->cur_item;
	std::vector<HTREEITEM> insertedItems;
	insertedItems.reserve(selected.size());
	const bool committed = ccz::scene_preview::ApplyBatchTransaction<int, HTREEITEM>(
		selected,
		[&](int charID) {
			HTREEITEM inserted = InsertActionInstruction(
				charID, action, anchor, false);
			if (inserted) anchor = inserted;
			if (inserted) insertedItems.push_back(inserted);
			return inserted;
		},
		[&](HTREEITEM inserted) { RemoveInsertedInstruction(inserted); },
		[](HTREEITEM inserted) { return inserted != nullptr; });
	if (!committed) {
		RestoreAfterBatchFailure(restoreSelection, wasModified);
	} else {
		RecordInsertedUndo(L"统一动作", std::move(insertedItems),
			restoreSelection);
	}
}

void CRScenePreviewPage::ApplyMultiDisappear()
{
	if (m_selectedCharIDs.size() < 2) return;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView || !pView->cur_item) return;
	const std::vector<int> selected = m_selectedCharIDs;
	const HTREEITEM restoreSelection = pView->cur_item;
	const BOOL wasModified = pView->GetDocument()->IsModified();
	HTREEITEM anchor = pView->cur_item;
	std::vector<HTREEITEM> insertedItems;
	insertedItems.reserve(selected.size());
	const bool committed = ccz::scene_preview::ApplyBatchTransaction<int, HTREEITEM>(
		selected,
		[&](int charID) {
			HTREEITEM inserted = InsertDisappearInstruction(
				charID, anchor, false);
			if (inserted) anchor = inserted;
			if (inserted) insertedItems.push_back(inserted);
			return inserted;
		},
		[&](HTREEITEM inserted) { RemoveInsertedInstruction(inserted); },
		[](HTREEITEM inserted) { return inserted != nullptr; });
	if (!committed) {
		RestoreAfterBatchFailure(restoreSelection, wasModified);
		return;
	}
	RecordInsertedUndo(L"批量消失", std::move(insertedItems), restoreSelection);
	ClearCharacterSelection();
}

void CRScenePreviewPage::RemoveInsertedInstruction(HTREEITEM item)
{
	if (!item) return;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	CTreeCtrl& tree = pView->GetTreeCtrl();
	ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(item));
	if (data) {
		ccz::editor::ReleaseLongCharData(
			data->long_char_data,
			data->long_char_data_capacity);
		delete data;
	}
	tree.DeleteItem(item);
}

void CRScenePreviewPage::RestoreAfterBatchFailure(
	HTREEITEM restoreSelection,
	BOOL wasModified)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;
	CTreeCtrl& tree = pView->GetTreeCtrl();
	if (restoreSelection) {
		tree.SelectItem(restoreSelection);
		pView->cur_item = restoreSelection;
		m_hCurItem = restoreSelection;
	}
	pView->GetDocument()->SetModifiedFlag(wasModified);
	RequestPreviewRefreshForCurrentDocument(
		tree, restoreSelection ? restoreSelection : m_hCurItem);
}

void CRScenePreviewPage::BeginMultiMove()
{
	if (m_selectedCharIDs.size() < 2 || m_primaryCharID < 0) return;

	for (const auto& character : m_state.characters) {
		if (character.id != m_primaryCharID) continue;
		m_bMultiMoveMode = true;
		m_multiMoveDirection = character.direction;
		m_multiMoveInitialDirection = character.direction;
		m_multiMoveAnchor = CPoint(character.x, character.y);
		m_multiMoveCursorPos = CPoint(-1, -1);
		m_multiMoveCharIDs = m_selectedCharIDs;
		m_snappedGamePos = m_multiMoveAnchor;
		m_bSnapped = false;
		SetCapture();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
		return;
	}
}

void CRScenePreviewPage::CommitMultiMove()
{
	if (!m_bMultiMoveMode || m_multiMoveCharIDs.empty()) return;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return;

	const int deltaX = m_snappedGamePos.x - m_multiMoveAnchor.x;
	const int deltaY = m_snappedGamePos.y - m_multiMoveAnchor.y;
	if (deltaX == 0 && deltaY == 0 &&
		m_multiMoveDirection == m_multiMoveInitialDirection) return;
	const HTREEITEM restoreSelection = pView->cur_item;
	if (!restoreSelection) return;
	const BOOL wasModified = pView->GetDocument()->IsModified();

	// 先复制原始状态，避免逐条插入触发预览刷新后改变后续目标坐标。
	std::vector<CharacterState> selectedStates;
	selectedStates.reserve(m_multiMoveCharIDs.size());
	for (int charID : m_multiMoveCharIDs) {
		for (const auto& character : m_state.characters) {
			if (character.id == charID) {
				selectedStates.push_back(character);
				break;
			}
		}
	}
	if (selectedStates.size() != m_multiMoveCharIDs.size()) return;

	HTREEITEM anchor = restoreSelection;
	std::vector<HTREEITEM> insertedItems;
	insertedItems.reserve(selectedStates.size());
	const bool committed = ccz::scene_preview::ApplyBatchTransaction<CharacterState, HTREEITEM>(
		selectedStates,
		[&](const CharacterState& character) {
			HTREEITEM inserted = InsertMoveInstruction(
				character.id, character.x + deltaX, character.y + deltaY,
				anchor, m_multiMoveDirection, false);
			if (inserted) anchor = inserted;
			if (inserted) insertedItems.push_back(inserted);
			return inserted;
		},
		[&](HTREEITEM inserted) { RemoveInsertedInstruction(inserted); },
		[](HTREEITEM inserted) { return inserted != nullptr; });
	if (!committed) {
		RestoreAfterBatchFailure(restoreSelection, wasModified);
	} else {
		RecordInsertedUndo(L"组移动", std::move(insertedItems), restoreSelection);
	}
}

void CRScenePreviewPage::BeginPointMove()
{
	if (m_selectedCharIDs.size() < 2) return;

	int primaryDirection = -1;
	for (const auto& character : m_state.characters) {
		if (character.id == m_primaryCharID) {
			primaryDirection = character.direction;
			break;
		}
	}
	if (primaryDirection < 0) return;

	m_bPointMoveMode = true;
	m_multiMoveDirection = primaryDirection;
	m_multiMoveInitialDirection = primaryDirection;
	m_pointMoveCursorPos = CPoint(-1, -1);
	m_pointMoveCharIDs = m_selectedCharIDs;
	m_snappedGamePos = CPoint(-1, -1);
	m_bSnapped = false;
	SetCapture();
	SetCursor(LoadCursor(nullptr, IDC_CROSS));
}

void CRScenePreviewPage::CommitPointMove()
{
	if (!m_bPointMoveMode || m_pointMoveCharIDs.empty()) return;
	if (m_pointMoveCursorPos.x < 0 || m_pointMoveCursorPos.y < 0) return;

	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView || !pView->cur_item) return;

	const int targetX = m_snappedGamePos.x;
	const int targetY = m_snappedGamePos.y;
	const HTREEITEM restoreSelection = pView->cur_item;
	const BOOL wasModified = pView->GetDocument()->IsModified();

	// 先复制原始状态，避免逐条插入刷新预览后影响后续角色的方向/动作读取。
	std::vector<CharacterState> selectedStates;
	selectedStates.reserve(m_pointMoveCharIDs.size());
	for (int charID : m_pointMoveCharIDs) {
		for (const auto& character : m_state.characters) {
			if (character.id == charID) {
				selectedStates.push_back(character);
				break;
			}
		}
	}
	if (selectedStates.size() != m_pointMoveCharIDs.size()) return;

	HTREEITEM anchor = restoreSelection;
	std::vector<HTREEITEM> insertedItems;
	insertedItems.reserve(selectedStates.size());
	const bool committed = ccz::scene_preview::ApplyBatchTransaction<CharacterState, HTREEITEM>(
		selectedStates,
		[&](const CharacterState& character) {
			HTREEITEM inserted = InsertMoveInstruction(
				character.id, targetX, targetY, anchor, m_multiMoveDirection, false);
			if (inserted) anchor = inserted;
			if (inserted) insertedItems.push_back(inserted);
			return inserted;
		},
		[&](HTREEITEM inserted) { RemoveInsertedInstruction(inserted); },
		[](HTREEITEM inserted) { return inserted != nullptr; });
	if (!committed) {
		RestoreAfterBatchFailure(restoreSelection, wasModified);
	} else {
		RecordInsertedUndo(L"点移动", std::move(insertedItems), restoreSelection);
	}
}

HTREEITEM CRScenePreviewPage::InsertTurnInstruction(
	int charID, int direction, HTREEITEM hAfter, bool record_undo)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	int intData[5] = { charID, 0, direction, 0, 0 };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		51, restoreSelection, intData, 5);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"设置朝向", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertActionInstruction(
	int charID, int action, HTREEITEM hAfter, bool record_undo)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	int intData[5] = { charID, action, 0, 0, 0 };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		52, restoreSelection, intData, 5);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"设置动作", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertDisappearInstruction(
	int charID, HTREEITEM hAfter, bool record_undo)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	// 武将消失: [0]=模式(0=单人), [1]=人物ID
	int intData[7] = { 0, charID, 0, 0, 0, 0, 0 };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		49, restoreSelection, intData, 7);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"角色消失", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertMoveInstruction(
	int charID, int gameX, int gameY, HTREEITEM hAfter,
	int directionOverride, bool record_undo)
{
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	// 武将移动: [0]=模式(0=data角色), [1]=人物ID, [2]=0, [3]=目标X, [4]=目标Y, [5]=方向
	int dir = directionOverride;
	if (dir < 0) {
		for (const auto& ch : m_state.characters) {
			if (ch.id == charID) { dir = ch.direction; break; }
		}
	}
	int intData[6] = { 0, charID, 0, gameX, gameY, dir };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		50, restoreSelection, intData, 6);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"移动角色", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertMapPortraitShowInstruction(
	int personID, int pixelX, int pixelY, HTREEITEM hAfter, bool record_undo)
{
	if (!ccz::scene_preview::is_direct_data_portrait_target(personID))
		return nullptr;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	// 地图头像显示: [Data人物ID, 外场景像素X, 外场景像素Y]
	const int intData[3] = { personID, pixelX, pixelY };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		41, restoreSelection, intData, 3);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"地图头像显示", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertMapPortraitMoveInstruction(
	int personID, int pixelX, int pixelY, HTREEITEM hAfter, bool record_undo)
{
	if (!ccz::scene_preview::is_direct_data_portrait_target(personID))
		return nullptr;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	// 地图头像移动: [Data人物ID, 外场景像素X, 外场景像素Y]
	const int intData[3] = { personID, pixelX, pixelY };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		42, restoreSelection, intData, 3);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"地图头像移动", { inserted }, restoreSelection);
	}
	return inserted;
}

HTREEITEM CRScenePreviewPage::InsertMapPortraitHideInstruction(
	int personID, HTREEITEM hAfter, bool record_undo)
{
	if (!ccz::scene_preview::is_direct_data_portrait_target(personID))
		return nullptr;
	CcczEditor2View* pView = GetActiveEditorView();
	if (!pView) return nullptr;
	// 地图头像消失: [Data人物ID]
	const int intData[1] = { personID };
	const HTREEITEM restoreSelection = hAfter ? hAfter : pView->cur_item;
	const HTREEITEM inserted = pView->InsertInstructionAt(
		43, restoreSelection, intData, 1);
	if (inserted != nullptr && record_undo) {
		RecordInsertedUndo(L"地图头像消失", { inserted }, restoreSelection);
	}
	return inserted;
}

// ========== 调整位置模式 ==========

void CRScenePreviewPage::OnMouseMove(UINT nFlags, CPoint point)
{
	m_mousePos = point;
	bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

	// 地图头像放置/移动使用外场景像素坐标；预览时不做内场景网格吸附，
	// 让头像幽灵图随鼠标实时跟随，点击时再由 OnLButtonDown 写回最终坐标。
	if (m_bPortraitPlaceMode && m_portraitPlacePersonID >= 0) {
		m_portraitPlaceCursorPos = point;
		const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
			point.x, point.y, m_offsetX, m_offsetY, m_scale);
		m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
		m_bSnapped = false;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPortraitMoveMode && m_movePortraitPersonID >= 0) {
		m_portraitMoveCursorPos = point;
		const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
			point.x, point.y, m_offsetX, m_offsetY, m_scale);
		m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
		m_bSnapped = false;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPortraitAdjustMode && m_adjustPortraitPersonID >= 0) {
		m_portraitAdjustCursorPos = point;
		const auto pointInPixels = ccz::scene_preview::screen_to_scene_point(
			ccz::scene_preview::SceneCoordinateSpace::OuterPixels,
			point.x, point.y, m_offsetX, m_offsetY, m_scale);
		m_snappedGamePos = CPoint(pointInPixels.x, pointInPixels.y);
		m_bSnapped = false;
		if (auto* portrait = m_state.mapPortraits.find(m_adjustPortraitPersonID)) {
			portrait->x = m_snappedGamePos.x;
			portrait->y = m_snappedGamePos.y;
		}
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	}
	else if (m_bAdjustMode && m_adjustCharID >= 0) {
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_adjustCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		for (auto& ch : m_state.characters) {
			if (ch.id == m_adjustCharID) {
				ch.x = game.x;
				ch.y = game.y;
				break;
			}
		}
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
	}
	else if (m_bMoveMode && m_moveCharID >= 0) {
		m_moveCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_moveCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bMultiMoveMode) {
		m_multiMoveCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_primaryCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPointMoveMode) {
		m_pointMoveCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, m_primaryCharID);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else if (m_bPlaceMode && m_selectedCharID >= 0) {
		m_placeCursorPos = point;
		CPoint game = PixelToGame(point.x, point.y);
		if (ctrlDown) {
			CPoint snapped = SnapToGrid(game, -1);
			m_bSnapped = (snapped.x != game.x || snapped.y != game.y);
			game = snapped;
		} else {
			m_bSnapped = false;
		}
		m_snappedGamePos = game;
		Invalidate();
		SetCursor(LoadCursor(nullptr, IDC_CROSS));
	}
	else {
		m_bSnapped = false;
		if (point.y >= m_canvasTop) {
			Invalidate();
		}
	}
	CWnd::OnMouseMove(nFlags, point);
}

void CRScenePreviewPage::OnRButtonDown(UINT nFlags, CPoint point)
{
	// 右键取消调整/移动/放置模式，并屏蔽随后的右键菜单
	if (m_bPortraitPlaceMode) {
		ReleaseCapture();
		m_bPortraitPlaceMode = false;
		m_portraitPlacePersonID = -1;
		m_portraitPlaceCursorPos = { -1, -1 };
		m_selectedCharID = -1;
		m_listChars.SetCurSel(-1);
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bPortraitMoveMode) {
		ReleaseCapture();
		m_bPortraitMoveMode = false;
		m_movePortraitPersonID = -1;
		m_portraitMoveCursorPos = { -1, -1 };
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bPortraitAdjustMode) {
		ReleaseCapture();
		m_bPortraitAdjustMode = false;
		m_adjustPortraitPersonID = -1;
		m_portraitAdjustCursorPos = { -1, -1 };
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		if (m_pTreeCtrl && m_hCurItem) {
			EvaluateToItem(*m_pTreeCtrl, m_hCurItem);
			Invalidate();
		}
		return;
	}
	if (m_bAdjustMode) {
		ReleaseCapture();
		m_bAdjustMode = false;
		m_adjustCharID = -1;
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		if (m_pTreeCtrl && m_hCurItem) {
			EvaluateToItem(*m_pTreeCtrl, m_hCurItem);
			Invalidate();
		}
		return;
	}
	if (m_bMultiMoveMode) {
		ReleaseCapture();
		m_bMultiMoveMode = false;
		m_multiMoveAnchor = CPoint(-1, -1);
		m_multiMoveCursorPos = CPoint(-1, -1);
		m_multiMoveCharIDs.clear();
		m_bSnapped = false;
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bPointMoveMode) {
		ReleaseCapture();
		m_bPointMoveMode = false;
		m_pointMoveCursorPos = CPoint(-1, -1);
		m_pointMoveCharIDs.clear();
		m_bSnapped = false;
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bMoveMode) {
		ReleaseCapture();
		m_bMoveMode = false;
		m_moveCharID = -1;
		m_moveCursorPos = { -1, -1 };
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	if (m_bPlaceMode) {
		ReleaseCapture();
		m_bPlaceMode = false;
		m_selectedCharID = -1;
		m_placeCursorPos = { -1, -1 };
		m_listChars.SetCurSel(-1);
		m_bSuppressContextMenu = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		Invalidate();
		return;
	}
	CWnd::OnRButtonDown(nFlags, point);
}

BOOL CRScenePreviewPage::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (m_dynamicPlaybackActive) return TRUE;
	bool shiftDown = (nFlags & MK_SHIFT) != 0;
	int delta = (zDelta > 0) ? 1 : -1;

	// 地图头像不包含动作/朝向，调整位置时滚轮不改变任何状态。
	if (m_bPortraitAdjustMode || m_bPortraitPlaceMode || m_bPortraitMoveMode) {
		return TRUE;
	}

	// 放置模式
	if (m_bPlaceMode && m_selectedCharID >= 0) {
		if (shiftDown) {
			m_placeAction = (m_placeAction + delta + 20) % 20;
		} else {
			m_placeDir = (m_placeDir + delta + 4) % 4;
		}
		Invalidate();
		return TRUE;
	}

	// 调整位置模式
	if (m_bAdjustMode && m_adjustCharID >= 0) {
		// Shift+滚轮保留动作指令行为；普通滚轮仅暂存朝向，确认时再写回。
		if (shiftDown) {
			int action = 0;
			for (const auto& ch : m_state.characters) {
				if (ch.id == m_adjustCharID) {
					action = (ch.action + delta + 20) % 20;
					break;
				}
			}
			InsertActionInstruction(m_adjustCharID, action);
		} else {
			for (auto& ch : m_state.characters) {
				if (ch.id == m_adjustCharID) {
					ch.direction = (ch.direction + delta + 4) % 4;
					break;
				}
			}
			Invalidate();
		}
		return TRUE;
	}

	// 移动模式
	if (m_bMoveMode && m_moveCharID >= 0) {
		for (auto& ch : m_state.characters) {
			if (ch.id == m_moveCharID) {
				if (shiftDown) {
					ch.action = (ch.action + delta + 20) % 20;
				} else {
					ch.direction = (ch.direction + delta + 4) % 4;
				}
				break;
			}
		}
		Invalidate();
		return TRUE;
	}

	// 统一移动模式：滚轮只控制主角色朝向，组内角色预览和写回统一采用该方向。
	if (m_bMultiMoveMode || m_bPointMoveMode) {
		if (!shiftDown) {
			m_multiMoveDirection = ccz::scene_preview::CycleDirection(
				m_multiMoveDirection, delta);
		}
		Invalidate();
		return TRUE;
	}

	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

// 从section头遍历到当前指令为止,找到离当前指令最近的该角色的武将出现指令
HTREEITEM CRScenePreviewPage::FindAppearInstruction(int charID)
{
	if (!m_pTreeCtrl || !m_hCurItem) return nullptr;

	// 找到所属Section: 向上找id==-2的节点
	HTREEITEM hSection = m_hCurItem;
	while (hSection) {
		ItemData* d = (ItemData*)m_pTreeCtrl->GetItemData(hSection);
		if (d && d->id == -2) break;
		hSection = m_pTreeCtrl->GetParentItem(hSection);
	}
	if (!hSection) return nullptr;

	// 与EvaluateToItem相同的遍历方式：遍历直接子节点及其子节点
	HTREEITEM hFound = nullptr;
	HTREEITEM hCur = m_pTreeCtrl->GetChildItem(hSection);
	while (hCur) {
		ItemData* d = (ItemData*)m_pTreeCtrl->GetItemData(hCur);
		if (d && d->id == 48 && (int)d->int_data.size() >= 1 && d->int_data[0] == charID)
			hFound = hCur;
		if (hCur == m_hCurItem) break;

		// 遍历子节点（子事件内部）
		HTREEITEM hSub = m_pTreeCtrl->GetChildItem(hCur);
		while (hSub) {
			ItemData* sd = (ItemData*)m_pTreeCtrl->GetItemData(hSub);
			if (sd && sd->id == 48 && (int)sd->int_data.size() >= 1 && sd->int_data[0] == charID)
				hFound = hSub;
			if (hSub == m_hCurItem) return hFound;
			hSub = m_pTreeCtrl->GetNextSiblingItem(hSub);
		}

		hCur = m_pTreeCtrl->GetNextSiblingItem(hCur);
	}
	return hFound;
}
