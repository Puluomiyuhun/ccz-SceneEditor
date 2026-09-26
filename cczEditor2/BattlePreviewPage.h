#pragma once

#include <afxwin.h>
#include "PreviewIntegration/BattleAssetCache.h"
#include "PreviewIntegration/BattleCharacterCatalog.h"
#include "PreviewIntegration/BattleEditorOps.h"
#include "PreviewIntegration/BattlePreviewAdapter.h"
#include "PreviewIntegration/BattleWritebackAdapter.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include <ccz/preview/battle_preview.h>

#include <GdiPlus.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CcczEditor2View;

class CSBattlePreviewPage : public CWnd
{
public:
	CSBattlePreviewPage() noexcept = default;
	virtual ~CSBattlePreviewPage();

	BOOL CreatePage(CWnd* parent, UINT control_id);
	void RequestPreviewRefresh(CTreeCtrl& tree, HTREEITEM selected_item,
		const wchar_t* document_path);
	// Produce a value-only writeback plan after recapturing the active S
	// document. This never mutates the TreeView or enables product writeback.
	bool BuildPendingWritebackPlan(
		const ccz::editor_preview::CapturedEditorTree& fresh_snapshot,
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
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	afx_msg void OnQaWriteback();
	#endif
	afx_msg void OnHideSidebar();
	afx_msg void OnShowSidebar();
	DECLARE_MESSAGE_MAP()

private:
	void CreateEditorControls();
	void LayoutEditorControls();
	void RefreshEditorControls();
	void RefreshCharacterList();
	void RefreshBrushList();
	void RefreshDeploymentList();
	void RefreshSelectedUnitIndex();
	void BindSelectedDeployment();
	void ShowOperationResult(
		const ccz::editor_preview::BattleEditorResult& result);
	void StampAt(const ccz::preview::BattleCoord& coordinate);
	void SetSidebarVisible(bool visible);
	void LoadCapturedBattle(const ccz::editor_preview::CapturedEditorTree& snapshot);
	bool ApplyPendingNativeWritebackForEvidence(
		CcczEditor2View& view, HTREEITEM selected_item,
		const ccz::editor_preview::BattleWritebackEvidence& evidence,
		std::wstring& diagnostic);
	bool ApplyPendingNativeWritebackTransaction(
		CcczEditor2View& view, HTREEITEM selected_item,
		std::wstring& diagnostic, bool require_nonempty_batch);
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	bool ApplyPendingNativeWritebackForQaCopy(
		CcczEditor2View& view, std::wstring& diagnostic);
	bool IsCurrentQaCopy() const;
	#endif
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
	void AppendDiagnostic(const std::wstring& value);
	void ApplyMagentaTransparency(Gdiplus::Bitmap* bitmap);

	ccz::editor_preview::BattleAssetCache m_assets;
	ccz::editor_preview::BattlePreviewCapture m_writebackBaselineCapture;
	ccz::editor_preview::BattleWritebackNativeTransaction
		m_nativeWritebackTransaction;
	ccz::editor_preview::BattleEditorState m_editorState;
	std::unique_ptr<ccz::editor_preview::BattleEditorOps> m_editorOps;
	CButton m_characterTabButton;
	CButton m_brushTabButton;
	CButton m_hideSidebarButton;
	CButton m_showSidebarButton;
	CButton m_previousPageButton;
	CButton m_nextPageButton;
	CButton m_undoButton;
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_WRITEBACK_QA)
	CButton m_qaWritebackButton;
	#endif
	CEdit m_searchEdit;
	CComboBox m_deploymentCombo;
	CListBox m_characterList;
	CStatic m_pageLabel;
	CRect m_canvasRect;
	bool m_sidebarVisible = true;
	bool m_brushTabActive = false;
	bool m_controlsCreated = false;
	Gdiplus::Bitmap* m_mapBitmap = nullptr;
	int m_cachedMapId = -1;
	std::map<int, Gdiplus::Bitmap*> m_unitSprites;
	std::wstring m_unitSpritePath;
	CString m_documentPath;
	CString m_diagnostic;
	float m_zoom = 1.0f;
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
