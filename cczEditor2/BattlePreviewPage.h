#pragma once

#include <afxwin.h>
#include "PreviewIntegration/BattleAssetCache.h"
#include "PreviewIntegration/BattleCharacterCatalog.h"
#include "PreviewIntegration/BattleEditorOps.h"
#include "PreviewIntegration/BattleConditionTreeAdapter.h"
#include "PreviewIntegration/BattlePreviewAdapter.h"
#include "PreviewIntegration/BattleWritebackAdapter.h"
#include "PreviewIntegration/BattlePreviewSession.h"
#include "PreviewIntegration/BattleDynamicMapReplay.h"
#include "PreviewIntegration/BattleObstacleParametersPane.h"
#include "PreviewIntegration/BattleObstacleSelection.h"
#include "PreviewIntegration/BattleObstacleBatchPane.h"
#include "PreviewIntegration/BattleSectionRelations.h"
#include "PreviewIntegration/BattleCommandParametersPane.h"
#include "PreviewIntegration/BattleOwnUnitBindingDialog.h"
#include "PreviewIntegration/BattleSpellAnimation.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include "PreviewIntegration/ConditionTreePane.h"
#include "PreviewIntegration/VariableControlPane.h"
#include "PreviewIntegration/BattleUnitParametersPane.h"
#include "PreviewIntegration/PreviewAssetCache.h"
#include <ccz/preview/battle_preview.h>
#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/execution_model.h>
#include <ccz/preview/portrait_resolver.h>

#include <GdiPlus.h>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include <chrono>
#endif

class CcczEditor2View;
class CcczEditor2Doc;

#include "PreviewPrimaryActionButton.h"

class CBattlePreviewModeButton : public CButton
{
public:
	void SetHoverHandler(std::function<void()> handler)
	{
		m_hoverHandler = std::move(handler);
	}

protected:
	afx_msg void OnMouseMove(UINT flags, CPoint point);
	afx_msg void OnMouseHover(UINT flags, CPoint point);
	afx_msg void OnMouseLeave();
	DECLARE_MESSAGE_MAP()

private:
	std::function<void()> m_hoverHandler;
	bool m_tracking{};
};

class CBattleObstacleButton : public CButton
{
public:
	void SetObstacleType(int type) noexcept { m_obstacleType = type; }
	void SetSelected(bool selected);
	void SetSprite(std::shared_ptr<Gdiplus::Bitmap> sprite) {
		m_sprite = std::move(sprite);
		if (GetSafeHwnd()) Invalidate(FALSE);
	}

protected:
	void DrawItem(LPDRAWITEMSTRUCT draw_item) override;

private:
	std::shared_ptr<Gdiplus::Bitmap> m_sprite;
	int m_obstacleType{};
	bool m_selected{};
};

class CSBattlePreviewPage : public CWnd
{
public:
	CSBattlePreviewPage() noexcept = default;
	virtual ~CSBattlePreviewPage();

	BOOL CreatePage(CWnd* parent, UINT control_id);
	void BindSession(ccz::editor_preview::BattlePreviewDocumentSession& session,
		CcczEditor2Doc& document, CTreeCtrl& tree);
	void UnbindSession();
	bool RequestPreviewRefresh(CTreeCtrl& tree, HTREEITEM selected_item,
		const wchar_t* document_path);
	bool SetQuickModeEnabled(bool enabled, CTreeCtrl& tree,
		HTREEITEM selected_item, const wchar_t* document_path);
	bool ConfirmDiscardTemporaryChanges() const;
	bool ConfirmDiscardSessionChanges(
		const ccz::editor_preview::BattlePreviewDocumentSession& session) const;
	bool IsBoundToTree(HWND tree_window) const;
	void EditorTreeDestroyed(HWND tree_window);
	// Produce a value-only writeback plan after recapturing the active S
	// document. This never mutates the TreeView or enables product writeback.
	bool BuildPendingWritebackPlan(
		const ccz::editor_preview::BattlePreviewCapture& fresh_capture,
		ccz::editor_preview::BattleWritebackPlanBuild& plan,
		std::wstring& diagnostic,
		ccz::editor_preview::BattleWritebackNativeBatch* native_batch = nullptr) const;
	void ClearDocument();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT create);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg void OnLButtonDown(UINT flags, CPoint point);
	afx_msg void OnLButtonUp(UINT flags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
	afx_msg void OnRButtonDown(UINT flags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnCaptureChanged(CWnd* window);
	afx_msg void OnMouseMove(UINT flags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnMButtonDown(UINT flags, CPoint point);
	afx_msg void OnMButtonUp(UINT flags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
	afx_msg void OnHScroll(UINT code, UINT position, CScrollBar* bar);
	afx_msg void OnVScroll(UINT code, UINT position, CScrollBar* bar);
	afx_msg void OnKeyDown(UINT character, UINT repeat_count, UINT flags);
	afx_msg void OnSearchChanged();
	afx_msg void OnCharacterSelectionChanged();
	afx_msg void OnDeploymentSelectionChanged();
	afx_msg void OnCharacterTab();
	afx_msg void OnBrushTab();
	afx_msg void OnPreviousCharacterPage();
	afx_msg void OnNextCharacterPage();
	afx_msg void OnUndo();
	afx_msg void OnQuickModeClicked();
	afx_msg void OnCopyPersonModeClicked();
	afx_msg void OnUnrestrictedTerrainModeClicked();
	afx_msg void OnConditionTreeClicked();
	afx_msg void OnActivationButtonClicked();
	afx_msg void OnVariableControlClicked();
	afx_msg void OnUnitParametersClicked();
	afx_msg void OnPreviewModeClicked();
	afx_msg void OnDynamicPreviewClicked();
	afx_msg LRESULT OnConditionActionMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnWriteback();
	afx_msg void OnObstaclePageClicked();
	afx_msg void OnObstacleTypeClicked(UINT control_id);
	afx_msg void OnClearObstacleDrafts();
	afx_msg void OnObstacleDefaults();
	afx_msg void OnObstacleMultiSelect();
	afx_msg void OnMapCommands();
	afx_msg void OnRegionTool(UINT);
	afx_msg void OnClearRoleDrafts();
	void OpenOwnUnitBindings(int initialSlot = -1);
	bool ToggleObstacleSelectionAt(const ccz::preview::BattleCoord&, bool);
	bool HandleObstacleGroupContext(CPoint);
    bool IsObstacleGroupTargetValid(const ccz::preview::BattleCoord&) const;
	void ShowObstacleGroupParameters();
	bool ApplyObstacleGroup(const std::vector<ccz::editor_preview::BattleObstacleDraft>&, bool);
	bool RequireImmediateInstructionSelection();
	void ApplyHiddenBattleAppear(std::uint64_t, bool);
	void ApplyBatchBattleDirection(std::uint64_t, int, bool group = true);
	void OpenFacingDelaySettings(std::vector<ccz::editor_preview::BattleMapOperationInstruction>);
	void BeginMapCommandSelection(std::uint16_t);
	void OpenBattleCommand(std::uint16_t, bool, std::uint64_t = 0, bool = false, std::optional<ccz::preview::BattleCoord> = {}, std::optional<ccz::preview::BattleCoord> = {});
	void BeginBattleFacing(std::uint64_t, bool, bool);
	bool ApplyBattleFacing(const ccz::preview::BattleUnit*);
	int PreviewBattleFacing(const ccz::preview::BattleUnit&) const;
	void BeginDynamicRoleAdjustment(std::uint64_t, bool);
	bool ConfirmDynamicRoleAdjustment(const ccz::preview::BattleCoord&);
	bool ApplyDynamicUpdateBatch(ccz::editor_preview::BattleWritebackNativeBatch&, std::wstring&);
	void DrawEnhancedBattleOverlays(Gdiplus::Graphics&, const ccz::preview::BattlePreviewState&, int, int, float);
	void SyncBattleSpellAnimations();
	void FinishObstacleStampUndo();
	void ShowDynamicPreviewMenu();
	void AddDynamicPredecessor();
	void ManageDynamicPredecessors();
	void UpdateObstacleDraftControls();
	bool StampObstacleAt(const ccz::preview::BattleCoord& coordinate);
	bool EditObstacle(const ccz::editor_preview::BattleObstacleDraft& replacement);
	void ShowObstacleParameters();
	afx_msg void OnHideSidebar();
	afx_msg void OnShowSidebar();
	DECLARE_MESSAGE_MAP()

private:
	using BattleMapToolMode = ccz::editor_preview::BattleMapToolMode;
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	struct BattlePreviewPerfCounters {
		std::uint64_t full_document_captures{};
		std::uint64_t pinned_section_captures{};
		std::uint64_t battle_input_builds{};
		std::uint64_t battle_loads{};
		std::uint64_t map_decodes{};
		std::uint64_t sprite_decodes{};
		std::uint64_t elapsed_ms{};
	};
	class PerformanceActivationScope {
	public:
		PerformanceActivationScope(CSBattlePreviewPage& page,
			const wchar_t* document_path);
		~PerformanceActivationScope();
		PerformanceActivationScope(const PerformanceActivationScope&) = delete;
		PerformanceActivationScope& operator=(
			const PerformanceActivationScope&) = delete;
	private:
		CSBattlePreviewPage& page_;
	};
	void BeginPerformanceActivation(const wchar_t* document_path);
	void FinishPerformanceActivation();
	void FinishPerformanceActivationOnRequestReturn();
	void RecordFullDocumentCapture();
	void RecordPinnedSectionCapture();
	void RecordBattleInputBuild();
	void RecordBattleLoad();
	void RecordMapDecode();
	void RecordSpriteDecode();
	BattlePreviewPerfCounters m_perfCounters{};
	std::chrono::steady_clock::time_point m_perfStarted{};
	std::wstring m_perfDocumentPath;
	bool m_perfActive{};
	bool m_perfAwaitingFirstPaint{};
	#endif
	void CreateEditorControls();
	void CancelConflictingBattleEditorMode();
	void CancelBattleMapTool(bool preserve_obstacles = true);
	bool ApplyBattleMapOperations(
		const std::vector<ccz::editor_preview::BattleMapOperationInstruction>&
			instructions, std::wstring& diagnostic);
	void ShowPreviewModeMenu();
	void SetParameterPreviewMode(
		ccz::editor_preview::BattleParameterPreviewMode mode);
	bool RefreshDynamicPreview(CTreeCtrl& tree, HTREEITEM selected_item,
		const wchar_t* document_path);
	bool RebuildDynamicProjection();
	void ApplyDynamicProjection();
	void RefreshDynamicToggle();
	void EnsureDynamicObstacleSprites();
	bool DynamicConditionButtonVisible() const noexcept;
	bool DynamicActivationButtonVisible() const noexcept;
	bool DynamicVariableButtonVisible() const noexcept;
	void EnsurePortraitAssets();
	Gdiplus::Bitmap* GetUnitPortrait(int data_id);
	Gdiplus::Bitmap* GetOwnFactionPlaceholderPortrait();
	void DrawHoveredUnitCard(Gdiplus::Graphics& graphics,
		const ccz::preview::BattleCoord& coordinate,
		const CPoint& pointer, float cell);
	ccz::editor_preview::BattlePreviewDocumentSession& ActiveSession() noexcept;
	const ccz::editor_preview::BattlePreviewDocumentSession& ActiveSession() const noexcept;
	void SaveActiveSessionUiState();
	void RestoreActiveSessionUiState();
	bool HasPendingPreviewDrafts() const;
	bool HasPendingMapOperationState() const;
	bool HasPendingPreviewChanges() const;
	void MarkSessionStale(const std::wstring& diagnostic);
	void LayoutEditorControls();
	void LayoutObstacleSidebar(const CRect& client);
	void LayoutPreviewControlToolbar();
	void RefreshEditorControls();
	void RefreshCharacterList();
	void RefreshCharacterListIfDeploymentChanged();
	void RefreshDeploymentList();
	void RefreshCharacterListSelection();
	void RefreshDeploymentSelection();
	void RefreshSelectedUnitIndex();
	void OpenSingleUnitParameters(std::uint64_t cache_id);
	void OpenBatchUnitParameters();
	void BindSelectedDeployment();
	void ShowOperationResult(
		const ccz::editor_preview::BattleEditorResult& result);
	void ShowSelectionOperationResult(
		const ccz::editor_preview::BattleEditorResult& result);
	void StampAt(const ccz::preview::BattleCoord& coordinate);
	void SetSidebarVisible(bool visible);
	bool RequestQuickModeRefresh(CTreeCtrl& tree,
		const wchar_t* document_path);
	bool LoadQuickModeBaselinePreservingDraft(
		ccz::editor_preview::BattlePreviewCapture capture,
		HWND source_tree_window, std::wstring& diagnostic);
	void LoadCapturedBattle(ccz::editor_preview::BattlePreviewCapture capture,
		HWND source_tree_window = nullptr);
	bool ActivateConditionScope(
		const ccz::editor_preview::BattleConditionScope& scope);
	const ccz::preview::BattlePreviewState& EditingBattle() const;
	void SelectBoardCell(const ccz::preview::BattleCoord& coordinate, bool toggle);
	void ClearBoardSelection();
	std::vector<ccz::editor_preview::BattleSelection> m_dynamicSelection;
	bool CaptureRoleOperationUnits(std::uint64_t anchor_cache_id,
		bool include_selection_group,
		std::vector<ccz::preview::BattleUnit>& units,
		std::size_t& anchor_index, std::wstring& diagnostic);
	void RefreshConditionTree();
	void UpdateConditionActivationTarget(
		const ccz::preview::LogicalNodeKey& target_key);
	LRESULT ApplyConditionActionAndRefresh(
		const ccz::preview::LogicalNodeKey& node_key,
		ccz::preview::ConditionTreeAction action,
		std::int32_t integer_value);
	void ApplyVariableActionAndRefresh(
		ccz::preview::VariableControlKind kind, std::int32_t id,
		ccz::preview::VariableControlAction action,
		std::int32_t integer_value);
	bool ApplyPendingNativeWritebackForCurrentSection(
		CcczEditor2View& view, std::wstring& diagnostic);
	bool ApplyPendingNativeWritebackTransaction(
		CcczEditor2View& view, HTREEITEM selected_item,
		std::wstring& diagnostic, bool require_nonempty_batch,
		const std::vector<ccz::editor_preview::BattleMapOperationInstruction>*
			map_operations = nullptr);
	bool RebaseBattlePreviewAfterNativeWriteback(
		CcczEditor2View& view, HTREEITEM selected_item,
		std::wstring& diagnostic,
		ccz::editor_preview::BattlePreviewCapture* committed_capture_pointer = nullptr);
	bool UndoNativeWriteback(CcczEditor2View& view,
		std::wstring& diagnostic);
	bool LoadMapImage();
	void DrawCanvas(Gdiplus::Graphics& graphics, int width, int height);
	void UpdateViewport();
	void ClampViewport();
	void SetViewportScroll(int x, int y, bool invalidate = true);
	void ZoomAt(CPoint point, float zoom);
	std::optional<ccz::preview::BattleCoord> BattleCoordFromScreen(
		CPoint point) const;
	std::optional<std::size_t> HitTestUnit(
		const ccz::preview::BattleCoord& coordinate) const;
	Gdiplus::Bitmap* GetUnitSprite(
		const ccz::preview::BattleUnit& unit, int raw_direction,
		ccz::editor_preview::BattleAssetCache::UnitSpriteRef* sprite_ref);
	void ClearUnitSprites();
	void ReleaseActiveBitmapPins();
	void PinActiveBitmap(
		const ccz::editor_preview::BattleBitmapCacheKey& key);
	void UnpinActiveBitmap(
		const ccz::editor_preview::BattleBitmapCacheKey& key) noexcept;
	void EvictBitmapsToBudget();
	Gdiplus::Bitmap* FindCachedBitmap(
		const ccz::editor_preview::BattleBitmapCacheKey& key);
	Gdiplus::Bitmap* StoreCachedBitmap(
		const ccz::editor_preview::BattleBitmapCacheKey& key,
		Gdiplus::Bitmap* bitmap);
	void UpdateSessionAssetKeys();
	void AppendDiagnostic(const std::wstring& value);
	void ApplyMagentaTransparency(Gdiplus::Bitmap* bitmap);

	ccz::editor_preview::BattleAssetCache m_assets;
	ccz::editor_preview::BattleAssetCache m_dynamicAssets;
	ccz::editor_preview::PreviewAssetCache m_portraitAssets;
	ccz::editor_preview::BattlePreviewDocumentSession m_unboundSession;
	ccz::editor_preview::BattlePreviewDocumentSession* m_activeSession{};
	CcczEditor2Doc* m_activeDocument{};
	ccz::editor_preview::BattleEditorOps* m_editorOps{};
	CButton m_characterTabButton;
	CButton m_brushTabButton;
	CButton m_hideSidebarButton;
	CButton m_showSidebarButton;
	CButton m_previousPageButton;
	CButton m_nextPageButton;
	CBattlePrimaryActionButton m_undoButton;
	CButton m_quickModeButton;
	CButton m_copyPersonModeButton;
	CButton m_unrestrictedTerrainModeButton;
	CButton m_conditionTreeButton;
	CBattlePrimaryActionButton m_activationButton;
	CButton m_variableControlButton;
	CButton m_unitParametersButton;
	CBattlePreviewModeButton m_dynamicPreviewButton;
	CButton m_mapCommandsButton;
	CButton m_regionToolButtons[9];
	CButton m_clearRoleDraftsButton;
	bool m_regionToolsExpanded{};
	CButton m_obstacleMultiSelectButton;
	ccz::editor_preview::BattleObstacleSelection m_obstacleSelection;
	CBattleObstacleBatchPane m_obstacleBatchPane;
	CBattleCommandParametersPane m_commandParametersPane;
	ccz::editor_preview::BattleSectionRelations m_dynamicSectionRelations;
	std::wstring m_dynamicPredecessorBanner;
	ccz::preview::ReplayOptions m_dynamicReplayOptions;
	std::map<std::uint64_t,ccz::editor_preview::BattleDynamicPositionOrigin> m_dynamicAdjustOrigins;
	std::uint64_t m_dynamicAdjustEpoch{}, m_dynamicAdjustGeneration{};
	std::optional<ccz::editor_preview::BattleMapOperationUiState::ObstacleUndo> m_obstacleStampUndo;
	struct SpellPreview { ccz::preview::BattleCoord coordinate; ccz::editor_preview::BattleSpellAnimationAsset asset; };
	std::vector<SpellPreview> m_spellPreviews;
	std::wstring m_spellSignature;
	std::size_t m_spellFrame{};
	CButton m_obstaclePageButton;
	CBattleObstacleButton m_obstacleTypeButtons[3];
	CStatic m_obstaclePageTitle;
	CButton m_clearObstacleDraftsButton;
	CButton m_obstacleDefaultsButton;
	CBattleObstacleParametersPane m_obstacleParametersPane;
	ccz::editor_preview::BattleObstacleDefaults m_obstacleDefaults;
	struct ObstacleEditTarget {
		bool draft{};
		ccz::editor_preview::BattleObstacleDraft value;
		ccz::editor_preview::BattleDynamicObstacle source;
		std::uint64_t document_id{}, tree_generation{}, content_epoch{};
	};
	std::optional<ObstacleEditTarget> m_obstacleEditTarget;
	bool m_obstacleStampActive{};
	std::optional<ccz::preview::BattleCoord> m_obstacleLastStamp;
	CStatic m_mapOperationStatus;
	CBattlePreviewModeButton m_previewModeButton;
	CConditionTreePane m_conditionTreePane;
	CVariableControlPane m_variableControlPane;
	CBattleUnitParametersPane m_unitParametersPane;
	CBattlePrimaryActionButton m_writebackButton;
	CEdit m_searchEdit;
	CComboBox m_deploymentCombo;
	CListBox m_characterList;
	CStatic m_pageLabel;
	CFont m_uiFont;
	CFont m_toolbarFont;
	CFont m_writebackFont;
	CRect m_canvasRect;
	ccz::editor_preview::BattleBitmapCachePolicy m_bitmapCachePolicy;
	std::map<ccz::editor_preview::BattleBitmapCacheKey,
		std::unique_ptr<Gdiplus::Bitmap>> m_decodedBitmaps;
	std::set<ccz::editor_preview::BattleBitmapCacheKey> m_activeBitmapPins;
	std::set<ccz::editor_preview::BattleBitmapCacheKey> m_activeSpritePins;
	bool m_sidebarVisible = true;
	bool m_brushTabActive = false;
	bool m_controlsCreated = false;
	bool m_restoringSessionUi{};
	Gdiplus::Bitmap* m_mapBitmap = nullptr;
	std::unique_ptr<Gdiplus::Bitmap> m_dynamicMapBitmap;
	std::wstring m_dynamicMapImagePath;
	std::uint64_t m_dynamicMapImageRevision{};
	std::array<std::shared_ptr<Gdiplus::Bitmap>, 3> m_dynamicObstacleSprites;
	bool m_obstacleAssetsAttempted{};
	std::wstring m_dynamicObstacleAssetPath;
	std::uint64_t m_dynamicObstacleAssetRevision{};
	std::wstring m_dynamicDiagnostic;
	ccz::editor_preview::BattlePreviewCapture m_dynamicTargetCapture;
	std::shared_ptr<const ccz::preview::SectionExecutionModel>
		m_dynamicConditionModel;
	ccz::preview::ReplayOptions m_dynamicConditionOptions;
	ccz::preview::ScriptVariableIndex m_dynamicVariableIndex;
	ccz::preview::LogicalNodeKey m_dynamicActivationTarget;
	ccz::preview::LogicalNodeKey m_dynamicTargetNode;
	std::set<ccz::preview::LogicalNodeKey> m_dynamicUnknownStatusConditions;
	bool m_dynamicTargetIsScene1{};
	bool m_dynamicTargetIsSectionRoot{};
	ccz::preview::BattlePreviewState m_dynamicScene1Baseline;
	std::vector<ccz::editor_preview::BattleDynamicObstacle>
		m_dynamicScene1Obstacles;
	ccz::editor_preview::BattleDynamicMapPlan m_dynamicScene1Plan;
	bool m_dynamicScene1BaselineComplete{};
	std::wstring m_dynamicBaselineDiagnostic;
	ccz::editor_preview::BattleDynamicMapPlan m_dynamicPlan;
	ccz::editor_preview::BattleDynamicMapProjection m_dynamicProjection;
	CTreeCtrl* m_sourceTreeControl{};
	std::uint64_t m_dynamicContentEpoch{};
	std::uint64_t m_dynamicTreeGeneration{};
	bool m_dynamicPreviewEnabled{};
	bool m_dynamicPreviewReady{};
	bool m_dynamicConditionButtonVisible{};
	bool m_dynamicActivationButtonVisible{};
	bool m_dynamicVariableButtonVisible{};
	ccz::editor_preview::BattleBitmapCacheKey m_activeMapKey;
	std::map<int, Gdiplus::Bitmap*> m_unitSprites;
	std::map<int, std::unique_ptr<Gdiplus::Bitmap>> m_unitPortraits;
	std::set<int> m_failedPortraitDataIds;
	std::unique_ptr<Gdiplus::Bitmap> m_ownFactionPlaceholderPortrait;
	bool m_ownFactionPlaceholderPortraitLoaded{};
	std::wstring m_unitSpritePath;
	std::uint64_t m_unitSpriteRevision{};
	std::wstring m_portraitRootKey;
	std::uint64_t m_portraitDataRevision{};
	std::uint64_t m_portraitFaceRevision{};
	CString m_documentPath;
	CString m_diagnostic;
	CString m_quickModeDiagnostic;
	HWND m_sourceTreeWindow = nullptr;
	float m_zoom = 1.0f;
	int m_wheelRemainder = 0;
	int m_scrollX = 0;
	int m_scrollY = 0;
	bool m_viewportUpdating = false;
	bool m_panActive = false;
	bool m_trackingMouseLeave = false;
	bool m_brushRefreshPending{};
	bool m_leftStampActive = false;
	bool m_selectionGestureActive = false;
	bool m_selectionDragStarted = false;
	std::optional<ccz::preview::BattleCoord> m_lastStampCell;
	std::optional<ccz::preview::BattleCoord> m_selectionPressCell;
	std::optional<ccz::preview::BattleCoord> m_selectionLastCell;
	std::vector<int> m_rosterDeployedDataIds;
	CPoint m_panStartPoint{-1, -1};
	CPoint m_hoverPoint{-1, -1};
	CPoint m_selectionPressPoint{-1, -1};
	int m_panStartScrollX = 0;
	int m_panStartScrollY = 0;
	std::optional<std::size_t> m_selectedUnit;
	std::optional<ccz::preview::BattleCoord> m_hoverCell;
	BattleMapToolMode m_battleMapToolMode{BattleMapToolMode::None};
	std::uint16_t m_mapCommandId{};
	std::optional<ccz::preview::BattleCoord> m_mapCommandStart, m_mapCommandEnd;
	bool m_mapCommandDragging{};
	bool m_obstaclePageActive{};
	std::vector<ccz::preview::BattleUnit> m_roleOperationUnits;
	std::size_t m_roleOperationAnchorIndex{};
	bool m_roleOperationDirectionAdjusted{};
	bool m_roleSelectorBattlefield{};
	int m_roleOperationDirection{-1};
	int m_selectedObstacleType{};
	ccz::editor_preview::BattleObstacleDraftQueue m_obstacleDrafts;
};
