#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <optional>
#include <string>
#include <memory>
#include <GdiPlus.h>
#include "MapPortraitState.h"
#include "PreviewPrimaryActionButton.h"
#include "SceneCoordinate.h"
#include "ScenePositionSource.h"
#include "PreviewIntegration/PreviewRefreshStateMachine.h"
#include "PreviewIntegration/PreviewRefreshDecision.h"
#include "PreviewIntegration/PreviewSession.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include "PreviewIntegration/PreviewAssetCache.h"
#include "PreviewIntegration/FreeRPreviewResolver.h"
#include "PreviewIntegration/ConditionTreePane.h"
#include "PreviewIntegration/VariableControlPane.h"
#include "PreviewIntegration/DynamicPlaybackSession.h"
#include "PreviewIntegration/PlaybackInterpolation.h"
#include <ccz/preview/r_document_session.h>
#pragma comment(lib, "gdiplus.lib")

struct ItemData;
class CcczEditor2Doc;

struct PositionWritebackSource {
	ccz::scene_preview::PositionSource source;
	HTREEITEM item = nullptr;

	bool valid() const noexcept
	{
		return item != nullptr && source.valid();
	}
	void clear() noexcept
	{
		source = {};
		item = nullptr;
	}
};

struct CharacterState {
	int id;        // 人物ID
	int x, y;      // 游戏坐标
	int direction; // 0=北, 1=东, 2=南, 3=西
	int action;    // 动作编号
	PositionWritebackSource positionSource;
};

struct SceneState {
	int mapID = -1;
	int mapImageIndex = -1;
	ccz::scene_preview::SceneCoordinateSpace coordinateSpace =
		ccz::scene_preview::SceneCoordinateSpace::Unknown;
	ccz::scene_preview::MapPortraitState mapPortraits;
	std::map<int, PositionWritebackSource> mapPortraitPositionSources;
	std::vector<CharacterState> characters;
	void Clear() {
		mapID = -1;
		mapImageIndex = -1;
		coordinateSpace = ccz::scene_preview::SceneCoordinateSpace::Unknown;
		mapPortraits.clear();
		mapPortraitPositionSources.clear();
		characters.clear();
	}
};

struct NativePlanApplyResult {
	bool applied{};
	std::size_t applied_count{};
	std::wstring diagnostic;
};

class CRScenePreviewPage : public CWnd
{
public:
	CRScenePreviewPage() noexcept;
	virtual ~CRScenePreviewPage();
	BOOL CreatePage(CWnd* parent, UINT control_id);

	void RequestPreviewRefresh(CTreeCtrl& tree, HTREEITEM selected_item,
		CcczEditor2Doc& document);
	bool ActivateDocument(CTreeCtrl& tree, HTREEITEM selected_item,
		CcczEditor2Doc& document, bool& reused_session);
	void ParkActiveDocument();
	bool PrepareForTreeRebuild(CcczEditor2Doc& document);
	bool EditorTreeDestroyed(HWND tree_window, CcczEditor2Doc& document);
	bool CloseDocument(ccz::editor_preview::PreviewDocumentId document_id);
	void InvalidateDocumentPreview(
		ccz::editor_preview::PreviewDocumentId document_id);
	ccz::editor_preview::PreviewDocumentId ActiveDocumentId() const noexcept
	{
		return m_activeDocumentId;
	}
	void InvalidatePendingPreviewRefresh();
	void UpdatePreview(CTreeCtrl& tree, HTREEITEM hSelectedItem, const wchar_t* modPath);
	NativePlanApplyResult ApplyNativeReplayPlan(
		CTreeCtrl& tree, const ccz::preview::NativeReplayPlan& plan);
	void HideInternalizedEnhancementViews(const std::wstring& diagnostic);
	void InitCharList(wchar_t(*pPer2)[30]);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSearchChange();
	afx_msg void OnCharListSelChange();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnPreviewRefreshMessage(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnConditionActionMessage(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnVariableActionMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnConditionButtonClicked();
	afx_msg void OnActivationButtonClicked();
	afx_msg void OnVariableButtonClicked();
	afx_msg void OnUndoButtonClicked();
	afx_msg void OnUpdateConditionButton(CCmdUI* pCmdUI);
	afx_msg void OnUpdateActivationButton(CCmdUI* pCmdUI);
	afx_msg void OnUpdateVariableButton(CCmdUI* pCmdUI);
	afx_msg void OnUpdateUndoButton(CCmdUI* pCmdUI);
	afx_msg void OnUpdatePlaybackButton(CCmdUI* pCmdUI);
	virtual BOOL PreTranslateMessage(MSG* pMsg) override;
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()

private:
	SceneState m_state;
	Gdiplus::Bitmap* m_pMapBitmap = nullptr;
	int m_cachedMapID = -1;
	std::map<int, Gdiplus::Bitmap*> m_mapBitmapCache;
	CString m_modPath;
	CString m_debugInfo;

	// 角色形象相关
	bool m_spriteDataLoaded = false;
	WORD m_charImageIDs[1024] = {};
	std::map<int, Gdiplus::Bitmap*> m_spriteCache;

	// 外场景地图头像相关
	bool m_portraitDataLoaded = false;
	std::map<int, int> m_portraitFaceIndices;
	std::map<int, Gdiplus::Bitmap*> m_portraitCache;

	// 武将列表控件
	CEdit m_editSearch;
	CListBox m_listChars;
	CButton m_conditionButton;
	CBattlePrimaryActionButton m_activationButton;
	CConditionTreePane m_conditionTreePane;
	CButton m_variableButton;
	CVariableControlPane m_variableControlPane;
	CBattlePrimaryActionButton m_undoButton;
	CButton m_playbackStartTargetButton;
	CButton m_playbackTargetEndButton;
	CButton m_playbackResetButton;
	CButton m_playbackPlayButton;
	CButton m_playbackStopButton;
	CButton m_playbackStepBackButton;
	CButton m_playbackStepForwardButton;
	CButton m_playbackSpeedButton;
	CProgressCtrl m_playbackProgress;
	CStatic m_playbackCounter;
	CToolTipCtrl m_playbackToolTip;
	CFont m_playbackFont;
	bool m_conditionButtonVisible = false;
	bool m_activationButtonVisible = false;
	bool m_conditionPaneVisible = false;
	bool m_variableButtonVisible = false;
	bool m_variablePaneVisible = false;
	bool m_undoButtonVisible = false;
	bool m_playbackVisible = false;
	CFont m_listFont;
	CFont m_undoFont;
	wchar_t(*m_pPer2)[30] = nullptr;
	bool m_charListInited = false;
	int m_selectedCharID = -1;

	// 场景角色多选状态（保持点击加入顺序）
	std::vector<int> m_selectedCharIDs;
	int m_primaryCharID = -1;

	// 渲染参数(OnPaint计算,点击复用)
	int m_canvasTop = 0;
	int m_offsetX = 0;
	int m_offsetY = 0;
	float m_scale = 1.0f;

	// 右键命中的角色
	int m_hitCharID = -1;

	// 移动模式: 插入移动指令
	bool m_bMoveMode = false;
	int m_moveCharID = -1;
	CPoint m_moveCursorPos = { -1, -1 };

	// 多选移动模式：以主角色为锚点，保持相对坐标
	bool m_bMultiMoveMode = false;
	CPoint m_multiMoveAnchor = { -1, -1 };
	CPoint m_multiMoveCursorPos = { -1, -1 };
	std::vector<int> m_multiMoveCharIDs;
	int m_multiMoveDirection = -1;
	int m_multiMoveInitialDirection = -1;

	// 多选点移动模式：所有选中角色写回同一个逻辑坐标，使用统一移动朝向
	bool m_bPointMoveMode = false;
	CPoint m_pointMoveCursorPos = { -1, -1 };
	std::vector<int> m_pointMoveCharIDs;

	// 调整位置模式: 角色跟随鼠标,修改已有出现指令
	bool m_bAdjustMode = false;
	int m_adjustCharID = -1;

	// 地图头像调整位置模式：只修改当前有效的显示/移动坐标
	bool m_bPortraitAdjustMode = false;
	int m_adjustPortraitPersonID = -1;
	CPoint m_portraitAdjustCursorPos = { -1, -1 };

	// 地图头像放置/移动模式：复用角色的鼠标交互，但写回外场景头像指令。
	bool m_bPortraitPlaceMode = false;
	int m_portraitPlacePersonID = -1;
	CPoint m_portraitPlaceCursorPos = { -1, -1 };
	bool m_bPortraitMoveMode = false;
	int m_movePortraitPersonID = -1;
	CPoint m_portraitMoveCursorPos = { -1, -1 };

	// 放置角色模式: 角色形象跟随鼠标
	bool m_bPlaceMode = false;
	CPoint m_placeCursorPos = { -1, -1 };
	int m_placeDir = 2;      // 放置时的朝向(默认朝南)
	int m_placeAction = 0;   // 放置时的动作(默认普通)

	// 记住当前树上下文(用于回溯查找指令)
	CTreeCtrl* m_pTreeCtrl = nullptr;
	HTREEITEM m_hCurItem = nullptr;
	ccz::editor_preview::PreviewSession m_previewSession;
	ccz::editor_preview::EditorPreviewInput m_lastPreviewInput;
	ccz::editor_preview::CapturedEditorTree m_lastEditorTreeSnapshot;
	bool m_hasEditorTreeSnapshot = false;
	ccz::editor_preview::ScenePreviewResolution m_cachedSceneResolution;
	bool m_cachedSceneResolutionValid = false;
	ccz::preview::NativeReplayPlan m_cachedFreeRPrefixPlan;
	bool m_cachedFreeRPrefixValid = false;
	ccz::editor_preview::DynamicPlaybackSession m_dynamicPlaybackSession;
	ccz::preview::PlaybackTimeline m_dynamicTimeline;
	// 静态预览也保留当前选定节点在动态时间线中的位置，播放器未启动时
	// 进度条仍能显示正确的锚点，而不是退回 0/0。
	std::size_t m_dynamicStaticProgressIndex = 0;
	ccz::preview::PlaybackRange m_dynamicPlaybackRange =
		ccz::preview::PlaybackRange::StartToTarget;
	bool m_dynamicPlaybackActive = false;
	bool m_dynamicPlaybackPrepared = false;
	std::size_t m_dynamicPreparedIndex = 0;
	std::uint64_t m_dynamicLastTickMs = 0;
	double m_dynamicVisualProgress = 0.0;
	ccz::preview::PlaybackSegmentKind m_dynamicSegmentKind =
		ccz::preview::PlaybackSegmentKind::Instant;
	// Snapshot immediately before the first event in the active range.  A
	// direct one-event rewind restores this baseline instead of replaying the
	// whole Section from its origin.
	SceneState m_dynamicInitialState;
	ccz::preview::DialogueOverlayModel m_dynamicInitialDialogue;
	SceneState m_dynamicFromState;
	SceneState m_dynamicToState;
	ccz::preview::DialogueOverlayModel m_dynamicFromDialogue;
	ccz::preview::DialogueOverlayModel m_dynamicToDialogue;
	static constexpr UINT_PTR kDynamicPlaybackTimerId = 0x7F40;
	// The visible overlay is copied from the typed frame only after a valid
	// native-plan refresh.  The bounded legacy fallback can therefore clear
	// this display model without mutating the session's diagnostic frame.
	ccz::preview::DialogueOverlayModel m_visibleDialogueOverlay;
	bool m_internalizationFallbackActive = false;
	ccz::editor_preview::PreviewAssetCache m_previewAssetCache;
	ccz::preview::DialogueProfile m_dialogueProfile;
	ccz::preview::InstructionCatalog m_previewCatalog;
	ccz::editor_preview::PreviewRefreshStateMachine m_previewRefreshState;
	std::uint64_t m_previewRefreshSequence = 0;
	std::uint64_t m_previewTreeGeneration = 1;
	struct PendingPreviewRequest {
		HWND treeWindow = nullptr;
		HTREEITEM selectedItem = nullptr;
		CString documentPath;
		ccz::editor_preview::RDocumentActivationSnapshot activation;
		std::uint64_t treeGeneration = 0;

		bool valid() const noexcept
		{
			return treeWindow != nullptr && treeGeneration != 0;
		}
		void clear() noexcept
		{
			treeWindow = nullptr;
			selectedItem = nullptr;
			documentPath.Empty();
			activation = {};
			treeGeneration = 0;
		}
	} m_pendingPreviewRequest;
	// 特殊 Scene 预览仍限制在当前相邻两 Scene 配对内；回放本身使用
	// 与普通 Section 相同的完整原生指令流，不再跳过武将动态状态指令。
	bool m_limitedFreeRReplay = false;

	// 网格显示
	bool m_bShowGrid = false;

	// 鼠标跟踪
	CPoint m_mousePos = { -1, -1 };  // 当前鼠标窗口坐标
	bool m_bSuppressContextMenu = false; // 右键取消模式后屏蔽一次菜单

	// Ctrl吸附
	CPoint m_snappedGamePos = { -1, -1 };
	bool m_bSnapped = false;

	void EvaluateToItem(CTreeCtrl& tree, HTREEITEM hItem);
	void ExecutePendingPreviewRefresh(
		const ccz::editor_preview::PreviewRefreshStateMachine::Request& request);
	void RequestPreviewRefreshForCurrentDocument(
		CTreeCtrl& tree, HTREEITEM selected_item);
	enum class SpecialScenePrefixResult {
		NotSpecial,
		SpecialUnresolved,
		PrefixApplied,
	};
	bool EvaluateSpecialScene(CTreeCtrl& tree, HTREEITEM hSection,
		HTREEITEM hSelectedItem, const wchar_t* documentPath);
	SpecialScenePrefixResult PrepareSpecialScenePrefix(
		CTreeCtrl& tree, HTREEITEM hSection,
		HTREEITEM hSelectedItem, const wchar_t* documentPath);
	void ProcessSceneNode(CTreeCtrl& tree, HTREEITEM item,
		HTREEITEM stopItem, bool stopBefore, bool& reachedStop);
	void ProcessSceneSection(CTreeCtrl& tree, HTREEITEM section,
		HTREEITEM stopItem, bool stopBefore, bool& reachedStop);
	void ProcessSceneToSection(CTreeCtrl& tree, HTREEITEM scene,
		HTREEITEM appearanceSection, HTREEITEM appearanceInstruction,
		bool& reachedAppearance);
	void ProcessSceneAll(CTreeCtrl& tree, HTREEITEM scene);
	void ProcessInstruction(ItemData* data, HTREEITEM sourceItem);
	bool LoadMapImage(int mapID, const wchar_t* modPath);
	CPoint GameToPixel(int gameX, int gameY);
	CPoint PixelToGame(int screenX, int screenY);
	CPoint ScenePointFromScreen(int screenX, int screenY) const;
	CPoint SnapToGrid(CPoint gamePos, int excludeCharID);
	void DrawDirectionArrow(Gdiplus::Graphics& g, int cx, int cy, int direction);

	CString GetRootPath(const wchar_t* modPath);
	bool LoadSpriteData(const wchar_t* modPath);
	Gdiplus::Bitmap* GetSpriteBitmap(int charID, const wchar_t* modPath);
	void DrawCharacter(Gdiplus::Graphics& g, const CharacterState& ch, int offsetX, int offsetY, float scale);
	void DrawCharacterGhost(Gdiplus::Graphics& g, const CharacterState& ch,
		int gameX, int gameY, int offsetX, int offsetY, float scale, float alpha);
	bool LoadPortraitData(const wchar_t* modPath);
	Gdiplus::Bitmap* GetPortraitBitmap(int personID, const wchar_t* modPath);
	void DrawMapPortrait(Gdiplus::Graphics& g,
		const ccz::scene_preview::MapPortraitPlacement& portrait,
		int offsetX, int offsetY, float scale, float alpha = 1.0f);
	void AppendDiagnostic(const CString& message);
	void ExecutePreviewRefresh(const PendingPreviewRequest& request);
	CTreeCtrl* ResolvePreviewTree(const PendingPreviewRequest& request);
	void ClearNativePreviewState(bool clearUndo = true);
	void UpdateConditionPaneFromFrame();
	void UpdateActivationButtonFromFrame();
	void UpdateVariablePaneFromFrame();
	int ConditionUiHeight(int client_height) const noexcept;
	CRect SceneCanvasRect(const CRect& client_rect) const;
	void LayoutConditionUi(int width, int height);
	void RequestConditionActionRefresh();
	void RequestVariableActionRefresh();
	int PlaybackUiHeight() const noexcept;
	void LayoutPlaybackUi(int width, int top, int height);
	void UpdatePlaybackControls();
	bool RefreshDynamicTimelineForCurrentTarget();
	bool StartDynamicPlayback(ccz::preview::PlaybackRange range);
	void StopDynamicPlayback(bool restore_static);
	HTREEITEM CurrentPlaybackTreeItem() const;
	bool SyncPlaybackTreeSelection(bool suppress_preview_refresh);
	void PrepareDynamicPlaybackEvent(std::size_t event_index);
	void CommitDynamicPlaybackEvent();
	bool RewindDynamicPlaybackOneEvent();
	bool BuildDynamicFrameAt(const ccz::preview::LogicalNodeKey& target,
		ccz::editor_preview::PreviewFrame& frame,
		std::optional<std::size_t> dialogue_segment_index = std::nullopt) const;
	bool ComposeSpecialReplayPlan(
		CTreeCtrl& tree, const ccz::preview::NativeReplayPlan& current,
		ccz::preview::NativeReplayPlan& composed);
	bool BuildFreeRPrefixPlan(
		CTreeCtrl& tree,
		const ccz::editor_preview::ScenePreviewResolution& resolution,
		ccz::preview::NativeReplayPlan& prefix);
	bool BuildFreeRTriggerSectionPrefixPlans(
		CTreeCtrl& tree,
		const ccz::editor_preview::ScenePreviewResolution& resolution,
		std::vector<ccz::preview::NativeReplayPlan>& prefixes);
	bool ApplyFreeRPrefixOnly(CTreeCtrl& tree);
	bool PublishDynamicFrame(const ccz::editor_preview::PreviewFrame& frame);
	void EnsureDynamicAssets();
	SceneState DynamicPaintState() const;
	void OnPlaybackStartTargetClicked();
	void OnPlaybackTargetEndClicked();
	void OnPlaybackResetClicked();
	void OnPlaybackPlayClicked();
	void OnPlaybackStopClicked();
	void OnPlaybackStepBackClicked();
	void OnPlaybackStepForwardClicked();
	void OnPlaybackSpeedClicked();

	int HitTestCharacter(CPoint screenPt);
	int HitTestMapPortrait(CPoint screenPt);
	bool IsCharacterSelected(int charID) const;
	bool IsPrimaryCharacter(int charID) const;
	void ToggleCharacterSelection(int charID);
	void RemoveCharacterSelection(int charID);
	void ClearCharacterSelection();
	void SetPrimaryCharacter(int charID);
	void ReconcileCharacterSelection();
	bool CommitCharacterStates(const std::vector<CharacterState>& states);
	bool CommitCharacterStates(const std::vector<CharacterState>& states,
		bool writeDirection);
	bool CommitCharacterStates(const std::vector<CharacterState>& states,
		const wchar_t* undoLabel);
	bool CommitCharacterStates(const std::vector<CharacterState>& states,
		const wchar_t* undoLabel, bool writeDirection);
	bool CommitMapPortraitPosition(int personID, int x, int y);
	PositionWritebackSource MakePositionSource(int instructionId, int subjectId,
		HTREEITEM sourceItem) const;
	void RefreshCharList();
	HTREEITEM InsertAppearInstruction(int charID, int gameX, int gameY,
		bool record_undo = true);
	HTREEITEM InsertTurnInstruction(int charID, int direction,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM InsertActionInstruction(int charID, int action,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM InsertDisappearInstruction(int charID,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM InsertMoveInstruction(int charID, int gameX, int gameY,
		HTREEITEM hAfter = nullptr, int directionOverride = -1,
		bool record_undo = true);
	HTREEITEM InsertMapPortraitShowInstruction(int personID, int pixelX, int pixelY,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM InsertMapPortraitMoveInstruction(int personID, int pixelX, int pixelY,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM InsertMapPortraitHideInstruction(int personID,
		HTREEITEM hAfter = nullptr, bool record_undo = true);
	HTREEITEM FindAppearInstruction(int charID);

	struct CoordinateUndoEntry {
		HTREEITEM item = nullptr;
		ccz::scene_preview::PositionSource source{};
		int oldX = 0;
		int oldY = 0;
		int newX = 0;
		int newY = 0;
		bool directionChanged = false;
		int oldDirection = 0;
		int newDirection = 0;
	};
	struct InsertedUndoSnapshot {
		HTREEITEM item = nullptr;
		HTREEITEM parent = nullptr;
		int id = 0;
		std::vector<int> values;
		CString text;
	};
	struct PreviewUndoTransaction {
		enum class Kind { Coordinate, Inserted, Deleted };
		Kind kind = Kind::Coordinate;
		std::wstring label;
		std::vector<CoordinateUndoEntry> coordinates;
		std::vector<HTREEITEM> insertedItems;
		std::vector<InsertedUndoSnapshot> insertedSnapshots;
		HTREEITEM deletedPrevious = nullptr;
		int deletedOrd = 0;
		HTREEITEM restoreSelection = nullptr;
		std::uint64_t treeGeneration = 0;
	};
	struct RDocumentPreviewSession {
		ccz::editor_preview::RDocumentActivationSnapshot activation;
		bool has_baseline{};
		SceneState state;
		ccz::editor_preview::PreviewSession preview_session;
		ccz::editor_preview::EditorPreviewInput last_preview_input;
		ccz::editor_preview::CapturedEditorTree last_editor_tree_snapshot;
		bool has_editor_tree_snapshot{};
		ccz::editor_preview::ScenePreviewResolution cached_scene_resolution;
		bool cached_scene_resolution_valid{};
		ccz::preview::NativeReplayPlan cached_free_r_prefix_plan;
		bool cached_free_r_prefix_valid{};
		std::wstring debug_info;
		bool internalization_fallback_active{};
		bool condition_button_visible{};
		bool activation_button_visible{};
		bool condition_pane_visible{};
		bool variable_button_visible{};
		bool variable_pane_visible{};
		bool playback_visible{};
		std::vector<int> selected_character_ids;
		int primary_character_id{-1};
		std::vector<PreviewUndoTransaction> undo_stack;
		ccz::preview::PlaybackTimeline dynamic_timeline;
		std::size_t dynamic_static_progress_index{};
		ccz::preview::PlaybackRange dynamic_playback_range =
			ccz::preview::PlaybackRange::StartToTarget;
		double dynamic_playback_speed{1.0};
	};
	std::map<ccz::editor_preview::PreviewDocumentId, RDocumentPreviewSession>
		m_documentSessions;
	ccz::editor_preview::PreviewDocumentId m_activeDocumentId{};
	ccz::editor_preview::RDocumentActivationSnapshot m_activeActivation;
	bool m_hasActiveBaseline{};
	std::vector<PreviewUndoTransaction> m_previewUndoStack;
	void RecordCoordinateUndo(const wchar_t* label,
		std::vector<CoordinateUndoEntry> entries, HTREEITEM restoreSelection);
	void RecordInsertedUndo(const wchar_t* label,
		std::vector<HTREEITEM> items, HTREEITEM restoreSelection);
	void ClearPreviewUndo();
	void DeletePreviewCharacter(int charID);
	void ClearRBitmapCaches();
	void UpdateUndoButton();
	bool UndoLastPreviewOperation();

	void ShowMultiContextMenu(CPoint screenPoint);
	void ApplyMultiAlignment();
	void ApplyMultiDistribution();
	void ApplyPrimaryAlignment(bool alignX);
	void ApplyPreciseDistribution(bool distributeX);
	void ApplyMultiDirection(int direction);
	void ApplyMultiAction(int action);
	void ApplyMultiDisappear();
	void RemoveInsertedInstruction(HTREEITEM item);
	void RestoreAfterBatchFailure(HTREEITEM restoreSelection, BOOL wasModified);
	void BeginMultiMove();
	void CommitMultiMove();
	void BeginPointMove();
	void CommitPointMove();
};
