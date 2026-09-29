#pragma once

#include <ccz/preview/battle_preview.h>
#include <ccz/preview/execution_model.h>

#include "BattlePreviewPredicates.h"
#include "BattleWritebackAdapter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccz::editor_preview {

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
};

struct BattleCharacterListState {
    std::vector<BattleCharacterEntry> entries;
    std::size_t page_size{20};
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
    int unit_type{};
    bool reinforcement{};
    int ai_policy{-1};
    int target{-1};
    int target_x{-1};
    int target_y{-1};
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
    ccz::preview::LogicalNodeKey scope_root;
    BattleCharacterListState characters;
    std::vector<BattleMinionBrush> minion_brushes;
    std::optional<BattleMinionBrush> selected_minion_brush;
    std::uint64_t next_minion_brush_batch_id{1};
    std::uint64_t selected_minion_brush_batch_id{};
    BattlePlacementContext placement;
    BattlePlacementDefaults placement_defaults;
    BattleEditorMode mode{BattleEditorMode::Select};
    BattleEditorMode mode_before_position_adjustment{BattleEditorMode::Select};
    BattleViewportState viewport;
    std::optional<BattleSelection> selection;
    std::uint64_t next_cache_id{1};
    std::uint64_t adjusting_cache_id{};
    std::optional<ccz::preview::BattleCoord> hover;
    bool direction_locked{};
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
    BattleEditorResult SetCharacterSearch(const std::wstring& query);
    BattleEditorResult SetCharacterPage(std::size_t page);
    // Compatibility API: only page 0 is accepted; no visible page changes.
    BattleEditorResult SetBattlePage(std::size_t page);
    BattleEditorResult SelectCharacter(int data_id);
    BattleEditorResult SelectMinionBrush(int first_data_id);
    BattleEditorResult SetPlacementParameters(
        const BattlePlacementDefaults& parameters);
    BattleEditorResult SetPlacementContext(
        const BattlePlacementContext& context);
    // Sets the unified deployment brush independently of any existing EEX node.
    BattleEditorResult SetDeploymentInstruction(std::uint16_t instruction_id);
    BattleEditorResult SetMode(BattleEditorMode mode);
    BattleEditorResult CancelPlacementBrush();

    BattleEditorResult SetDirectionLock(bool enabled);
    BattleEditorResult CycleDirection(int delta);

    BattlePlacementPreview PreviewPlacement(
        const ccz::preview::BattleCoord& coordinate) const;
    BattleEditorResult Place(const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult SelectAt(const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult BeginPositionAdjustment(std::uint64_t cache_id);
    BattlePlacementPreview PreviewPositionAdjustment(
        const ccz::preview::BattleCoord& coordinate) const;
    BattleEditorResult ConfirmPositionAdjustment(
        const ccz::preview::BattleCoord& coordinate);
    BattleEditorResult CancelPositionAdjustment();
    BattleEditorResult ModifyAt(const ccz::preview::BattleCoord& coordinate,
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
    int NextAvailableBrushDataId(const BattleMinionBrush& brush) const;
    bool RefreshSelectedBrushDataId(bool reset_direction = false);
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

// Rebase only user-edited preview values onto a fresh source parse. Clean
// source rows always come from the fresh parse; overlapping edits are reported
// with their logical source identity so callers can preserve the old draft.
BattleEditorDraftRebaseResult RebaseBattleEditorDraft(
    const BattleEditorState& draft,
    const BattleEditorState& fresh_baseline);

}  // namespace ccz::editor_preview
