#pragma once

#include "BattleConditionTreeAdapter.h"
#include "BattleBitmapCachePolicy.h"
#include "BattleEditorOps.h"
#include "BattleMapOperations.h"
#include "BattlePreviewAdapter.h"
#include "BattleWritebackAdapter.h"

#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/r_document_session.h>
#include <ccz/preview/script_variable_index.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ccz::editor_preview {

using SPreviewDocumentId = PreviewDocumentId;

struct BattlePreviewDocumentVersion {
	std::uint64_t content_epoch{};
	std::uint64_t tree_generation{};
};

enum class BattleParameterPreviewMode : std::uint8_t {
	None,
	BattlefieldIndex,
	Level,
	UnitType,
	AiPolicy,
	Reinforcement,
};

enum class BattlePreviewActivationCheck {
	InitializeSession,
	ReuseSession,
	RevalidatePinnedSection,
	RebindTree,
};

BattlePreviewActivationCheck CheckBattlePreviewActivation(
	bool has_baseline,
	const BattlePreviewDocumentVersion& saved,
	const BattlePreviewDocumentVersion& current) noexcept;

struct BattlePreviewUiState {
	float zoom{1.0f};
	int scroll_x{};
	int scroll_y{};
	bool sidebar_visible{true};
	bool brush_tab_active{};
	bool quick_mode_enabled{true};
	bool copy_person_mode{};
	bool unrestricted_terrain_mode{};
	BattleParameterPreviewMode parameter_preview_mode{
		BattleParameterPreviewMode::None};
	bool initialized{};
	std::wstring search_text;
	std::uint16_t deployment_instruction{0x4b};
	int selected_character_data_id{-1};
	int selected_brush_data_id{-1};
	std::uint64_t selected_unit_cache_id{};
};

struct BattleMapOperationUiState {
	BattleMapToolMode tool_mode{BattleMapToolMode::None};
	int selected_obstacle_type{};
	BattleObstacleDraftQueue obstacle_drafts;
	std::optional<ccz::preview::BattleUnit> move_operation_unit;
	int revive_operation_data_id{-1};
};

struct BattlePreviewConditionWorkspace {
	BattleEditorState state;
	std::unique_ptr<BattleEditorOps> operations;
	BattleConditionScope scope;
};

// Mutable S preview state belongs to one open document instance. This type is
// deliberately free of MFC windows, GDI objects, and R-preview state.
struct BattlePreviewDocumentSession {
	SPreviewDocumentId document_id{};
	std::wstring document_path;
	BattleBitmapCacheKey map_asset_key;
	BattleBitmapCacheKey unit_mov_asset_key;
	BattlePreviewDocumentVersion observed_version{};
	bool has_baseline{};
	bool stale{};
	BattlePreviewCapture baseline_capture;
	std::uint64_t quick_mode_section_revision{};
	BattleWritebackNativeTransaction native_writeback_transaction;
	BattleEditorState editor_state;
	std::unique_ptr<BattleEditorOps> operations;
	std::map<ccz::preview::LogicalNodeKey,
		std::unique_ptr<BattlePreviewConditionWorkspace>> condition_workspaces;
	ccz::preview::LogicalNodeKey active_workspace_key;
	BattleConditionScope active_condition_scope;
	ccz::preview::ReplayOptions condition_options;
	ccz::preview::InstructionCatalog condition_catalog;
	std::shared_ptr<const ccz::preview::SectionExecutionModel> condition_model;
	ccz::preview::ScriptVariableIndex condition_variable_index;
	ccz::preview::LogicalNodeKey condition_activation_target;
	BattlePreviewUiState ui;
	BattleMapOperationUiState map_operations;
	std::wstring diagnostic;
	std::wstring quick_mode_diagnostic;
	bool native_writeback_rebase_required{};
	bool condition_button_visible{};
	bool activation_button_visible{};
	bool variable_button_visible{};
};

class BattlePreviewSessionRegistry {
public:
	BattlePreviewDocumentSession& GetOrCreate(SPreviewDocumentId document_id);
	BattlePreviewDocumentSession* Find(SPreviewDocumentId document_id) noexcept;
	const BattlePreviewDocumentSession* Find(
		SPreviewDocumentId document_id) const noexcept;
	bool Erase(SPreviewDocumentId document_id) noexcept;
	std::size_t Size() const noexcept { return sessions_.size(); }

private:
	std::map<SPreviewDocumentId,
		std::unique_ptr<BattlePreviewDocumentSession>> sessions_;
};

} // namespace ccz::editor_preview
