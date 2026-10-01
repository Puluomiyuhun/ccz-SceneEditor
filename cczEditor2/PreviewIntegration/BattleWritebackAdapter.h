#pragma once

#include <ccz/preview/battle_preview.h>

#include "BattlePreviewAdapter.h"
#include "BattleMapOperations.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

// Facts required before a value-only battle build may become a real EEX
// mutation.  The current manifest deliberately leaves the last two facts
// false; callers must keep the product writeback path disabled until a real
// editor/native reload has closed them.
struct BattleWritebackEvidence {
    bool field_table_verified{};
    bool sample_manifest_verified{};
    bool group_order_verified{};
    bool native_reload_verified{};
    bool kind300_external_acceptance_verified{};
};

struct BattleWritebackRecord {
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey source_key;
    std::size_t page{};
    std::vector<std::int32_t> fields;
    // Includes the instruction id, then editor field-type/value pairs in
    // little-endian order.  It is diagnostic data only, never an EEX write.
    std::vector<std::uint8_t> bytes;
};

struct BattleWritebackBuild {
    bool ok{};
    bool product_writeback_allowed{};
    std::vector<BattleWritebackRecord> records;
    std::wstring diagnostic;
};

enum class BattleWritebackChangeKind {
    UpdateInstruction,
    DeleteInstruction,
    InsertInstruction,
    InsertFourBGroup,
};

struct BattleWritebackSourceInstruction {
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey logical_key;
    // The preview cache is keyed by logical_key from its baseline. The native
    // target may have a new positional key after ordinary edits in the same
    // Section; runtime_handle bridges them within the same tree generation.
    ccz::preview::LogicalNodeKey current_logical_key;
    std::uintptr_t runtime_handle{};
    int ordinal{};
    // origin_int_data is the snapshot captured when the Battle cache loaded;
    // current_int_data is freshly captured immediately before planning.
    std::vector<std::int32_t> origin_int_data;
    std::vector<std::int32_t> current_int_data;
};

struct BattleWritebackSectionSnapshot {
    ccz::preview::LogicalNodeKey scope_key;
    // Legacy/default target used only when the active branch has no reachable
    // deployment source from which to infer a type-specific child event.
    ccz::preview::LogicalNodeKey insert_parent_key;
    ccz::preview::LogicalNodeKey insert_before_key;
    std::vector<BattleDeploymentInsertionAnchor> insertion_anchors;
    bool has_current_own_deployment_group{};
    std::vector<BattleWritebackSourceInstruction> instructions;
};

struct BattleWritebackReachabilityScope {
    // When true, source deployments are limited to this complete, current
    // condition-tree projection. An empty key list is a valid empty branch.
    bool filter_deployments{};
    std::vector<ccz::preview::LogicalNodeKey> reachable_deployment_keys;
    std::vector<std::uintptr_t> reachable_deployment_handles;
    ccz::preview::LogicalNodeKey insert_parent_key;
    ccz::preview::LogicalNodeKey insert_before_key;
    std::vector<BattleDeploymentInsertionAnchor> insertion_anchors;
};

enum class BattleWritebackRevisionScope {
    WholeDocument,
    PinnedSection,
};

struct BattleWritebackChange {
    BattleWritebackChangeKind kind{BattleWritebackChangeKind::UpdateInstruction};
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey source_key;
    ccz::preview::LogicalNodeKey insert_parent_key;
    ccz::preview::LogicalNodeKey insert_before_key;
    int source_ordinal{};
    std::vector<std::int32_t> int_data;
    std::wstring group_label;
    std::vector<std::vector<std::int32_t>> group_records;
    std::vector<std::uint64_t> affected_cache_ids;
};

struct BattleWritebackBinding {
    std::uint64_t cache_id{};
    std::size_t change_index{};
    std::size_t record_index{};
};

struct BattleWritebackPlanBuild {
    bool ok{};
    // This pure plan is intentionally never a product/native commit signal.
    bool product_writeback_allowed{};
    std::vector<BattleWritebackChange> changes;
    std::vector<BattleWritebackBinding> bindings;
    std::wstring diagnostic;
};

struct BattleWritebackNativeChange {
    BattleWritebackChangeKind kind{BattleWritebackChangeKind::UpdateInstruction};
    std::uint16_t instruction_id{};
    std::uintptr_t item_handle{};
    std::uintptr_t parent_handle{};
    // TreeView inserts after a sibling, whereas the value plan stores an
    // insert-before terminal anchor. Zero means TVI_FIRST at the native edge.
    std::uintptr_t insert_after_handle{};
    std::uintptr_t insert_before_handle{};
    std::uintptr_t insert_terminal_handle{};
    // Present only for 0x32/0x55/0x58 insertions; it lets the native
    // transaction repeat the map-bound check before touching the TreeView.
    int map_width{};
    int map_height{};
    int source_ordinal{-1};
    // Full preimage captured at the same time as the runtime handle. It lets
    // the transaction reject stale targets and is also its exact rollback data.
    std::vector<std::int32_t> original_int_data;
    std::vector<std::int32_t> int_data;
    std::wstring group_label;
    std::vector<std::vector<std::int32_t>> group_records;
};

struct BattleWritebackNativeBatch {
    bool ok{};
    std::vector<BattleWritebackNativeChange> changes;
    std::wstring diagnostic;
};

// Resolve every logical source/insert anchor to the current TreeView handles
// before the native transaction mutates anything. The returned batch is still
// inert; callers must separately pass the runtime evidence gate.
BattleWritebackNativeBatch BuildBattleWritebackNativeBatch(
    const BattleWritebackPlanBuild& plan,
    const BattlePreviewCapture& current,
    const ccz::preview::LogicalNodeKey& scope_key);

// Build ordered insert-only changes after the editor's current selected
// ordinary S instruction. The full document snapshot is used for the anchor,
// so a quick-mode Scene1 preview may still insert after a command in another
// Scene without changing the preview's pinned map baseline.
BattleWritebackNativeBatch BuildBattleWritebackInsertBatchAfterSelection(
    const BattlePreviewCapture& current,
    const std::vector<BattleMapOperationInstruction>& instructions);

struct BattleWritebackNativeNodeSnapshot {
    std::uintptr_t handle{};
    std::uintptr_t parent_handle{};
    std::uintptr_t previous_sibling_handle{};
    std::uintptr_t next_sibling_handle{};
    std::uint16_t instruction_id{};
    int ordinal{};
    std::vector<std::int32_t> int_data;
    std::wstring long_text;
    bool has_children{};
    bool has_long_text{};
    bool has_structural_links{};
};

// Inserted 0x4B/0x46/0x47 nodes must all be removable during transaction
// rollback and undo. Source-instruction deletion remains planner-restricted.
bool CanDeleteBattleWritebackNativeNode(
    const BattleWritebackNativeNodeSnapshot& node) noexcept;

struct BattleWritebackNativeDocumentState {
    std::uintptr_t selected_handle{};
    int next_ordinal{};
    bool custom_modified{};
    bool framework_modified{};
    // Content-only revision for transaction and one-step undo safety. The MFC
    // bridge derives it from the full document so undo cannot clear later edits.
    std::uint64_t document_revision{};
};

// Ensure the editor's document-global insertion counter cannot assign an
// ordinal already present anywhere in the captured native tree. Gaps are
// allowed; missing instruction ordinals and stale/colliding counters fail.
bool ValidateBattleWritebackNextOrdinal(
    const CapturedEditorTree& document_snapshot, int next_ordinal,
    std::wstring& diagnostic);

struct BattleWritebackNativeHandleMapping {
    std::uintptr_t old_handle{};
    std::uintptr_t new_handle{};
};

class BattleWritebackNativeHost {
public:
    virtual ~BattleWritebackNativeHost() = default;
    virtual bool CaptureDocumentState(
        BattleWritebackNativeDocumentState& state) const = 0;
    virtual bool CaptureNode(std::uintptr_t handle,
        BattleWritebackNativeNodeSnapshot& node) const = 0;
    virtual bool NodeExists(std::uintptr_t handle) const = 0;
    // Each mutator is all-or-nothing: false means neither document nor caller
    // state changed. On success, SwapNodeIntDataAndRefresh exchanges int_data
    // with the supplied value and refreshes the native display.
    virtual bool SwapNodeIntDataAndRefresh(
        std::uintptr_t handle, std::vector<std::int32_t>& int_data) = 0;
    virtual bool InsertBattleNode(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, std::uint16_t instruction_id,
        const std::vector<std::int32_t>& int_data, int ordinal,
        std::uintptr_t& inserted_handle) = 0;
    virtual bool InsertBattleFourBGroup(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, const std::wstring& label,
        const std::vector<std::vector<std::int32_t>>& records,
        int first_ordinal, std::uintptr_t& marker_handle,
        std::uintptr_t& group_handle,
        std::vector<std::uintptr_t>& record_handles,
        std::uintptr_t& terminal_handle) = 0;
    virtual bool DeleteBattleNode(std::uintptr_t handle) = 0;
    virtual bool DeleteBattleFourBGroup(std::uintptr_t marker_handle,
        std::uintptr_t group_handle) = 0;
    virtual bool RestoreBattleFourBGroupBefore(std::uintptr_t parent_handle,
        std::uintptr_t after_handle, std::uintptr_t before_handle,
        const std::wstring& label,
        const std::vector<std::vector<std::int32_t>>& records,
        int first_ordinal, std::uintptr_t& marker_handle,
        std::uintptr_t& group_handle,
        std::vector<std::uintptr_t>& record_handles,
        std::uintptr_t& terminal_handle) = 0;
    virtual bool RestoreBattleNodeBefore(
        const BattleWritebackNativeNodeSnapshot& node,
        std::uintptr_t before_handle, std::uintptr_t& restored_handle) = 0;
    virtual bool MarkDocumentModified() = 0;
    virtual bool RestoreDocumentState(
        const BattleWritebackNativeDocumentState& state,
        const std::vector<BattleWritebackNativeHandleMapping>& remapped_handles) = 0;
};

// S-only native document transactions with a LIFO undo record per commit.
// It accepts only a fully pre-resolved batch; no logical lookup happens after
// the first mutation starts.
class BattleWritebackNativeTransaction {
public:
    bool Apply(const BattleWritebackNativeBatch& batch,
        BattleWritebackNativeHost& host, std::wstring& diagnostic);
    bool Undo(BattleWritebackNativeHost& host, std::wstring& diagnostic);
    void Clear() noexcept;
    bool undo_available() const noexcept { return !undo_stack_.empty(); }

private:
    struct AppliedOperation {
        BattleWritebackChangeKind kind{BattleWritebackChangeKind::UpdateInstruction};
        // For updates/deletes this is the pre-transaction node. For inserts it
        // is the inserted node snapshot used to remove or restore it on undo.
        BattleWritebackNativeNodeSnapshot node;
        std::uintptr_t current_handle{};
        std::uintptr_t group_marker_handle{};
        std::uintptr_t group_handle{};
        std::uintptr_t group_terminal_handle{};
        std::uintptr_t group_parent_handle{};
        std::uintptr_t group_after_handle{};
        std::uintptr_t group_before_handle{};
        std::vector<std::uintptr_t> group_record_handles;
        std::vector<std::vector<std::int32_t>> group_records;
        std::wstring group_label;
        std::vector<std::int32_t> after_int_data;
        // Apply-time swap scratch. It receives the exact native preimage and
        // is preallocated before any mutation, so rollback does not allocate.
        std::vector<std::int32_t> rollback_values;
    };
    struct UndoRecord {
        BattleWritebackNativeDocumentState before_state;
        std::uint64_t committed_revision{};
        // Kept in commit order so rollback/undo can replay the exact inverse
        // sequence, including deletes that anchor later insertions.
        std::vector<AppliedOperation> operations;
    };

    std::vector<UndoRecord> undo_stack_;
};

// Create an immutable, value-owned Section delta.  The supplied source
// snapshot must include both the cache-load baseline and a fresh pre-commit
// capture.  This function does not mutate a document or enable native writes.
BattleWritebackPlanBuild BuildBattleWritebackPlan(
    const ccz::preview::BattlePreviewState& state,
    const ccz::preview::LogicalNodeKey& scope_key,
    const BattleWritebackSectionSnapshot& snapshot);

// Pair the cache-load Battle capture with a fresh, immediately-before-write
// capture.  This remains a value-only bridge: no native tree or document is
// modified, and unknown insertion structure is left without anchors so the
// planner can fail closed when a new instruction is required.
bool BuildBattleWritebackSectionSnapshot(
    const BattlePreviewCapture& baseline,
    const BattlePreviewCapture& current,
    BattleWritebackSectionSnapshot& output,
    std::wstring& diagnostic,
    BattleWritebackRevisionScope revision_scope =
        BattleWritebackRevisionScope::WholeDocument,
    const BattleWritebackReachabilityScope* reachability_scope = nullptr);

// Build a value-owned, inspectable representation of the supported battle
// records.  It refuses incomplete evidence and never mutates an editor
// document, TreeView, or original byte buffer.
BattleWritebackBuild BuildBattleWriteback(
    const ccz::preview::BattlePreviewState& state,
    const BattleWritebackEvidence& evidence);

struct BattleWritebackSelectionSnapshot {
    ccz::preview::LogicalNodeKey source_key;
    std::size_t record_index{};
    std::size_t page{};
    std::uint64_t cache_id{};
};

struct BattleWritebackMemoryState {
    ccz::preview::BattlePreviewState battle;
    bool document_modified{};
    BattleWritebackSelectionSnapshot selection;
    std::vector<std::uint64_t> selection_group_cache_ids;
};

// Fixed battle rows use the original editor's empty-row sentinel.  This value
// helper preserves the row framing and does not infer any missing fields.
void ClearBattleFixedEmptySlot(ccz::preview::BattleUnit& unit);

// Value-only transaction used by later UI wiring.  It captures every value
// that a document bridge must restore (battle data, modified flag, selection)
// but intentionally has no MFC or EEX dependency.
class BattleWritebackTransaction {
public:
    bool Begin(const BattleWritebackMemoryState& current);
    bool Stage(const BattleWritebackMemoryState& proposed);
    bool CommitToMemory(BattleWritebackMemoryState& current);
    bool Rollback(BattleWritebackMemoryState& current);
    bool UndoToMemory(BattleWritebackMemoryState& current);
    void Clear() noexcept;
    bool active() const noexcept { return active_; }

private:
    bool active_{};
    bool staged_{};
    bool undo_available_{};
    BattleWritebackMemoryState original_;
    BattleWritebackMemoryState proposed_;
    BattleWritebackMemoryState committed_undo_;
};

}  // namespace ccz::editor_preview
