#pragma once

#include <ccz/preview/battle_preview.h>
#include <ccz/preview/battle_movement_terrain.h>
#include <ccz/preview/execution_model.h>

#include "BattlePreviewPredicates.h"
#include "BattleWritebackAdapter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ccz::editor_preview {

inline constexpr std::size_t kBattleCharacterPageSize = 50;

// This is the small data contract behind the original Battle arranger page.
// It intentionally has no HWND, TreeView, GDI, or R-scene state.  The native
// window may use it as a safe bridge, while document mutation remains behind
// BattleWritebackAdapter's evidence gate.
enum class BattleEditorMode {
    Select,
    Place,
    Modify,
    AdjustPosition,
    Delete,
};

enum class BattlePainterMode {
    Appearance,
    Hidden,
    Direction,
    Level,
    UnitType,
    Target,
};

enum class BattleTargetKind {
    DataId,
    Coordinate,
};

struct BattleTargetRef {
    BattleTargetKind kind{BattleTargetKind::DataId};
    int data_id{-1};
    ccz::preview::BattleCoord coordinate{};
};

struct BattlePainterValue {
    int integer{};
    bool boolean{};
    BattleTargetRef target{};
};

struct BattleCharacterEntry {
    int data_id{-1};
    std::wstring name;
    int appearance{-1};
    int default_direction{};
    int level{};
    int unit_type{};
    bool reinforcement{};
    int ai_policy{-1};
    bool has_name{true};
    // Raw Data.e5 +26 movement-class code. This is independent of unit_type
    // (the instruction field) and the visual faction arm_kind.
    int movement_class_code{-1};
};

struct BattleCharacterListState {
    std::vector<BattleCharacterEntry> entries;
    std::size_t page_size{kBattleCharacterPageSize};
    std::size_t current_page{};
    std::wstring query;
    int selected_data_id{-1};
};

struct BattleMinionBrush {
    std::wstring name;
    int first_data_id{-1};
    int last_data_id{-1};
};

struct BattlePlacementDefaults {
    int data_id{-1};
    int appearance{-1};
    int direction{};
    bool hidden{};
    int level{};
    int unit_type{2};
    bool reinforcement{};
    int ai_policy{0};
    int target{-1};
    int target_x{-1};
    int target_y{-1};
};

// One shared template using the maximum enemy-deployment field set. Every new
// placement applies only fields supported by its selected deployment kind.
// The template remains local to this document/condition workspace.
struct BattlePlacementProfile {
    std::optional<int> direction;
    std::optional<bool> hidden;
    std::optional<int> level;
    std::optional<int> unit_type;
    std::optional<bool> reinforcement;
    std::optional<int> ai_policy;
    std::optional<int> target;
    std::optional<int> target_x;
    std::optional<int> target_y;
};

struct BattlePlacementContext {
    ccz::preview::LogicalNodeKey source_key;
    std::uint16_t instruction_id{0x4b};
    // Parser source-record group provenance (not a visible battle page).
    std::size_t page{};
};

struct BattleSelection {
    ccz::preview::LogicalNodeKey source_key;
    std::size_t record_index{};
    // Parser source-record group provenance (not a visible battle page).
    std::size_t page{};
    // Stable editor identity.  Source coordinates above are retained only for
    // compatibility with older callers and provenance diagnostics.
    std::uint64_t cache_id{};

    bool valid() const noexcept { return cache_id != 0 || !source_key.empty(); }
};

struct BattleViewportState {
    int x{};
    int y{};
    int width{};
    int height{};
};

struct BattleEditorState {
    ccz::preview::BattlePreviewState battle;
    // Session-local state only keeps a reference to the shared Mod resource
    // cache entry; the 40x30 table itself is preloaded once per Mod revision.
    std::shared_ptr<const ccz::preview::BattleMovementTerrainTable>
        movement_terrain;
    ccz::preview::LogicalNodeKey scope_root;
    BattleCharacterListState characters;
    std::map<int,int> own_unit_bindings;
    std::vector<BattleMinionBrush> minion_brushes;
    std::optional<BattleMinionBrush> selected_minion_brush;
    std::uint64_t next_minion_brush_batch_id{1};
    std::uint64_t selected_minion_brush_batch_id{};
    BattlePlacementContext placement;
    BattlePlacementDefaults placement_defaults;
    std::map<std::uint16_t, BattlePlacementProfile> placement_profiles;
    BattleEditorMode mode{BattleEditorMode::Select};
    BattleEditorMode mode_before_position_adjustment{BattleEditorMode::Select};
    BattleViewportState viewport;
    std::optional<BattleSelection> selection;
    std::vector<BattleSelection> selection_group;
    std::uint64_t next_cache_id{1};
    std::uint64_t adjusting_cache_id{};
    // Position adjustment stages direction for the preview (shared by the
    // selected group when present) and commits it only on confirmation.
    int position_adjustment_direction{-1};
    std::optional<ccz::preview::BattleCoord> hover;
    bool direction_locked{};
    bool copy_person_mode{};
    bool unrestricted_terrain_mode{};
    int mouse_direction{};
    // This flag is carried through Task 5's value transaction for an eventual
    // document bridge.  BattleEditorOps never sets it: no document is touched.
    bool document_modified{};
};

struct BattleEditorDraftRebaseResult {
    bool ok{};
    BattleEditorState state;
    std::vector<std::wstring> conflicts;
};

struct BattleUnitPatch {
    std::optional<int> data_id;
    std::optional<ccz::preview::BattleCoord> position;
    std::optional<int> appearance;
    std::optional<bool> hidden;
    std::optional<int> direction;
    std::optional<int> level;
    std::optional<int> unit_type;
    std::optional<bool> reinforcement;
    std::optional<int> ai_policy;
    std::optional<int> target;
    std::optional<int> target_x;
    std::optional<int> target_y;
};

struct BattlePlacementPreview {
    bool valid{};
    ccz::preview::BattleCoord coordinate{};
    ccz::preview::BattleUnit unit;
    std::vector<ccz::preview::BattleUnit> units;
    std::wstring diagnostic;
};

struct BattleEditorResult {
    bool ok{};
    bool changed{};
    std::size_t affected_count{};
    std::wstring diagnostic;
    std::wstring before_summary;
    std::wstring after_summary;
};

class BattleEditorOps {
public:
    explicit BattleEditorOps(BattleEditorState& state) noexcept;

    std::size_t CharacterPageCount() const noexcept;
    // Compatibility API: returns 0/1 because the battle canvas is a single
    // all-units view.  Record-group provenance remains on BattleUnit::page.
    std::size_t BattlePageCount() const noexcept;
    std::vector<BattleCharacterEntry> VisibleCharacters() const;
    std::vector<int> CurrentDeployedCharacterDataIds() const;
    BattleEditorResult SetCharacterSearch(const std::wstring& query);
    BattleEditorResult SetCharacterPage(std::size_t page);
    // Compatibility API: only page 0 is accepted; no visible page changes.
    BattleEditorResult SetBattlePage(std::size_t page);
    BattleEditorResult SelectCharacter(int data_id);
    BattleEditorResult SelectMinionBrush(int first_data_id);
    BattleEditorResult SetPlacementParameters(
        const BattlePlacementDefaults& parameters);
    BattleEditorResult SetPlacementProfile(
        std::uint16_t instruction_id,
        const BattlePlacementProfile& profile);
    BattleEditorResult ResetPlacementProfile(std::uint16_t instruction_id);
    BattleEditorResult SetPlacementContext(
        const BattlePlacementContext& context);
    // Sets the unified deployment brush independently of any existing EEX node.
    BattleEditorResult SetDeploymentInstruction(std::uint16_t instruction_id);
    BattleEditorResult SetMode(BattleEditorMode mode);
    BattleEditorResult SetCopyPersonMode(bool enabled);
    BattleEditorResult SetUnrestrictedTerrainMode(bool enabled);
    BattleEditorResult CancelPlacementBrush();

    BattleEditorResult SetDirectionLock(bool enabled);
    BattleEditorResult CycleDirection(int delta);

    BattlePlacementPreview PreviewPlacement(
        const ccz::preview::BattleCoord& coordinate) const;
    BattleEditorResult Place(const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult SelectAt(const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult ToggleSelectionAt(
        const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult AddSelectionAt(
        const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult RemoveSelectionAt(
        const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult FilterSelectionByDeployment(
        std::uint16_t instruction_id);
    BattleEditorResult ClearSelectionGroup();
    BattleEditorResult DeleteSelectionGroup();
    BattleEditorResult BeginPositionAdjustment(std::uint64_t cache_id);
    BattlePlacementPreview PreviewPositionAdjustment(
        const ccz::preview::BattleCoord& coordinate) const;
    BattleEditorResult ConfirmPositionAdjustment(
        const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult CancelPositionAdjustment();
    BattleEditorResult ModifyAt(const ccz::preview::BattleCoord& coordinate,
                                const BattleUnitPatch& patch);
    BattleEditorResult ModifySelectionGroup(
        const std::vector<std::uint64_t>& cache_ids,
        const BattleUnitPatch& patch);
    BattleEditorResult DeleteAt(const ccz::preview::BattleCoord& coordinate);

    BattleEditorResult PaintAt(BattlePainterMode mode,
                               const ccz::preview::BattleCoord& coordinate,
                               const BattlePainterValue& value);

    BattleEditorResult SetViewportSize(int width, int height);
    BattleEditorResult Pan(int dx, int dy);
    const ccz::preview::BattleUnit* FindUnitByCacheId(
        std::uint64_t cache_id) const noexcept;

    // Every data mutation above is committed through a Task 5 value-only
    // transaction.  Undo restores BattlePreviewState, selection and the
    // document-modified flag as one snapshot; it never writes an EEX file.
    bool CanUndo() const noexcept;
    BattleEditorResult Undo();
    void ClearUndo() noexcept;

private:
    BattleEditorState* state_{};
    std::vector<BattleWritebackTransaction> undo_stack_;

    static std::vector<std::size_t> MatchingCharacterIndices(
        const BattleCharacterListState& characters);
    static bool InScope(const ccz::preview::LogicalNodeKey& source_key,
                        const ccz::preview::LogicalNodeKey& scope_root);
    static bool HasRole(const ccz::preview::BattleUnit& unit,
                        ccz::preview::BattleFieldRole role);
    static bool IsCurrentUnit(const BattleEditorState& state,
                              const ccz::preview::BattleUnit& unit);
    static std::vector<std::size_t> UnitsAt(
        const BattleEditorState& state,
        const ccz::preview::BattleCoord& coordinate);
    static std::wstring Summary(const BattleEditorState& state);
    static std::wstring UnitSummary(const ccz::preview::BattleUnit& unit);
    static int WrappedDirection(int value);
    static bool CompleteTerrain(const BattleEditorState& state);
    bool TerrainAllows(const ccz::preview::BattleCoord& coordinate,
                       int data_id, std::wstring& diagnostic, int movement_class_override = -1) const;
    bool TerrainAllowsUnit(const ccz::preview::BattleCoord&, const ccz::preview::BattleUnit&, std::wstring&) const;
    int NextAvailableBrushDataId(const BattleMinionBrush& brush) const;
    bool RefreshSelectedBrushDataId(bool reset_direction = false);
    void ApplyPlacementProfileToDefaults();
    static BattleWritebackMemoryState ToMemoryState(
        const BattleEditorState& state);
    static void ApplyMemoryState(BattleEditorState& state,
                                  const BattleWritebackMemoryState& memory);

    BattleEditorResult CommitMutation(const BattleEditorState& proposed,
                                      std::size_t affected_count,
                                      const wchar_t* diagnostic);
    BattlePlacementPreview BuildPlacementPreview(
        const ccz::preview::BattleCoord& coordinate) const;
    bool CanUsePlacementContext(std::wstring& diagnostic) const;
    bool ApplyPatch(ccz::preview::BattleUnit& unit,
                    const BattleUnitPatch& patch,
                    std::wstring& diagnostic) const;
    bool TargetReference(const BattleTargetRef& target,
                         std::size_t& target_index,
                         std::wstring& diagnostic) const;
};

std::vector<ccz::preview::BattleCoord> BattleSelectionPathCells(
    const ccz::preview::BattleCoord& start,
    const ccz::preview::BattleCoord& end);

// Rebase only user-edited preview values onto a fresh source parse. Clean
// source rows always come from the fresh parse; overlapping edits are reported
// with their logical source identity so callers can preserve the old draft.
BattleEditorDraftRebaseResult RebaseBattleEditorDraft(
    const BattleEditorState& draft,
    const BattleEditorState& fresh_baseline);

}  // namespace ccz::editor_preview
