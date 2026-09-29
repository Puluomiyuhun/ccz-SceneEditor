#pragma once

#include <afxwin.h>
#include "PreviewIntegration/BattleAssetCache.h"
#include "PreviewIntegration/BattleCharacterCatalog.h"
#include "PreviewIntegration/BattleEditorOps.h"
#include "PreviewIntegration/BattleConditionTreeAdapter.h"
#include "PreviewIntegration/BattlePreviewAdapter.h"
#include "PreviewIntegration/BattleWritebackAdapter.h"
#include "PreviewIntegration/BattlePreviewSession.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include "PreviewIntegration/ConditionTreePane.h"
#include <ccz/preview/battle_preview.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/execution_model.h>

#include <GdiPlus.h>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include <chrono>
#endif

class CcczEditor2View;
class CcczEditor2Doc;

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
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg void OnLButtonDown(UINT flags, CPoint point);
	afx_msg void OnLButtonUp(UINT flags, CPoint point);
	afx_msg void OnRButtonDown(UINT flags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnCaptureChanged(CWnd* window);
	afx_msg void OnMouseMove(UINT flags, CPoint point);
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
	afx_msg void OnConditionTreeClicked();
	afx_msg LRESULT OnConditionActionMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnWriteback();
	afx_msg void OnHideSidebar();
	afx_msg void OnShowSidebar();
	DECLARE_MESSAGE_MAP()

private:
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
	ccz::editor_preview::BattlePreviewDocumentSession& ActiveSession() noexcept;
	const ccz::editor_preview::BattlePreviewDocumentSession& ActiveSession() const noexcept;
	void SaveActiveSessionUiState();
	void RestoreActiveSessionUiState();
	bool HasPendingPreviewDrafts() const;
	bool HasPendingPreviewChanges() const;
	void MarkSessionStale(const std::wstring& diagnostic);
	void LayoutEditorControls();
	void RefreshEditorControls(bool refreshPlacementChoices = true);
	void RefreshCharacterList();
	void RefreshBrushList();
	void RefreshDeploymentList();
	void RefreshSelectedUnitIndex();
	void BindSelectedDeployment();
	void ShowOperationResult(
		const ccz::editor_preview::BattleEditorResult& result,
		bool refreshPlacementChoices = true);
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
	void RefreshConditionTree();
	LRESULT ApplyConditionActionAndRefresh(
		const ccz::preview::LogicalNodeKey& node_key,
		ccz::preview::ConditionTreeAction action,
		std::int32_t integer_value);
	bool ApplyPendingNativeWritebackForCurrentSection(
		CcczEditor2View& view, std::wstring& diagnostic);
	bool ApplyPendingNativeWritebackTransaction(
		CcczEditor2View& view, HTREEITEM selected_item,
		std::wstring& diagnostic, bool require_nonempty_batch);
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
	CButton m_undoButton;
	CButton m_quickModeButton;
	CButton m_conditionTreeButton;
	CConditionTreePane m_conditionTreePane;
	CButton m_writebackButton;
	CEdit m_searchEdit;
	CComboBox m_deploymentCombo;
	CListBox m_characterList;
	CStatic m_pageLabel;
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
	ccz::editor_preview::BattleBitmapCacheKey m_activeMapKey;
	std::map<int, Gdiplus::Bitmap*> m_unitSprites;
	std::wstring m_unitSpritePath;
	std::uint64_t m_unitSpriteRevision{};
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
	bool m_leftStampActive = false;
	std::optional<ccz::preview::BattleCoord> m_lastStampCell;
	CPoint m_panStartPoint{-1, -1};
	int m_panStartScrollX = 0;
	int m_panStartScrollY = 0;
	std::optional<std::size_t> m_selectedUnit;
	std::optional<ccz::preview::BattleCoord> m_hoverCell;
};
