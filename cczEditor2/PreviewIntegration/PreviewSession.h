#pragma once

#include "EditorPreviewAdapter.h"

#include <ccz/preview/condition_tree_state.h>
#include <ccz/preview/dialogue_overlay_model.h>
#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/face_archive.h>
#include <ccz/preview/instruction_catalog.h>
#include <ccz/preview/native_replay_plan.h>
#include <ccz/preview/script_variable_index.h>
#include <ccz/preview/variable_control_state.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(CCZ_PREVIEW_SESSION_TESTING)
#include <functional>
#endif

namespace ccz::editor_preview {

struct PreviewAssetsView {
    const std::vector<std::uint8_t>* data_e5{};
    const std::vector<std::uint8_t>* face_e5{};
    const ccz::preview::FaceArchive* face_archive{};
    // Optional document-level asset diagnostic.  Existing three-field
    // aggregate callers remain valid; the cache owns the pointed-to string.
    const std::wstring* diagnostic{};
};

struct PreviewFrame {
    std::shared_ptr<const ccz::preview::SectionExecutionModel> model;
    ccz::preview::NativeReplayPlan native_plan;
    ccz::preview::DialogueOverlayModel dialogue;
    ccz::preview::ScriptVariableIndex variable_index;
    ccz::preview::LogicalNodeKey active_node;
    std::vector<std::wstring> diagnostics;
};

enum class RefreshStatus {
    Updated,
    Unchanged,
    Idle,
    InvalidInput,
    ReentrantRejected,
};

struct RefreshResult {
    RefreshStatus status{RefreshStatus::Idle};
    bool frame_changed{};
    std::wstring diagnostic;
};

// Shared action semantics for R and S condition-tree projections. This
// mutates only the supplied value-owned ReplayOptions object.
bool ApplyConditionTreeActionToOptions(
    ccz::preview::ReplayOptions& options,
    const ccz::preview::SectionExecutionModel& model,
    const ccz::preview::LogicalNodeKey& node_key,
    ccz::preview::ConditionTreeAction action,
    std::int32_t integer_value,
    std::wstring* diagnostic);

class PreviewSession {
public:
    RefreshResult Refresh(
        const EditorPreviewInput& input,
        const ccz::preview::InstructionCatalog& catalog,
        const ccz::preview::DialogueProfile& profile,
        const PreviewAssetsView& assets);

    bool ApplyConditionAction(
        const ccz::preview::LogicalNodeKey& node_key,
        ccz::preview::ConditionTreeAction action,
        std::int32_t integer_value,
        std::wstring* diagnostic);

    // Apply the phase2 "激活子事件" operation to the current target.  The
    // core computes the complete predicate/Choice path, then this method
    // commits it as one session transaction so the normal refresh path runs
    // exactly once.
    bool ActivateTarget(const ccz::preview::LogicalNodeKey& target_key,
                        std::wstring* diagnostic);

    // The variable index controls which rows the editor displays; it is not
    // a write-admission list. This method validates the requested
    // namespace/action only, so forward-referenced IDs can be simulated
    // without merging Boolean and integer state.
    bool ApplyVariableAction(
        ccz::preview::VariableControlKind kind,
        std::int32_t id,
        ccz::preview::VariableControlAction action,
        std::int32_t integer_value,
        std::wstring* diagnostic);

    void ResetForDocument(const ccz::preview::LogicalNodeKey& document_key);

    const PreviewFrame& frame() const noexcept;
    const ccz::preview::ReplayOptions& options() const noexcept;

#if defined(CCZ_PREVIEW_SESSION_TESTING)
    void SetRefreshProbeForTesting(std::function<void()> probe);
#endif

private:
    ccz::preview::ReplayOptions options_;
    PreviewFrame frame_;
    ccz::preview::LogicalNodeKey document_key_;
    ccz::preview::LogicalNodeKey section_key_;
    std::uint64_t document_revision_{};
    bool refreshing_{};

#if defined(CCZ_PREVIEW_SESSION_TESTING)
    std::function<void()> refresh_probe_;
#endif
};

PreviewFrame BuildPreviewFrame(
    const EditorPreviewInput& input,
    const ccz::preview::SectionExecutionModel& model,
    const ccz::preview::ReplayOptions& options,
    const ccz::preview::InstructionCatalog& catalog,
    const ccz::preview::DialogueProfile& profile,
    const PreviewAssetsView& assets,
    std::optional<std::size_t> dialogue_segment_index = std::nullopt);

}  // namespace ccz::editor_preview
