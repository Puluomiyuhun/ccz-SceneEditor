#pragma once

#include <ccz/preview/battle_field_spec.h>
#include <ccz/preview/battle_preview.h>

#include <cstddef>
#include <optional>
#include <string>

namespace ccz::preview {

struct BattleInteractionResult {
    bool ok{};
    bool changed{};
    std::wstring diagnostic;
    std::wstring before_summary;
    std::wstring after_summary;
};

// Temporary, in-memory editing for the Battle preview.  This class deliberately
// has no EEX/tree dependency: the caller owns the copied BattlePreviewState and
// can discard it when the preview scope changes.
class BattleInteraction {
public:
    BattleInteractionResult Select(const BattlePreviewState& state,
                                   const LogicalNodeKey& source_key,
                                   std::size_t record_index);
    BattleInteractionResult Move(BattlePreviewState& state,
                                 BattleCoord target);
    BattleInteractionResult CycleDirection(BattlePreviewState& state,
                                            int delta,
                                            bool direction_locked);
    BattleInteractionResult SetHidden(BattlePreviewState& state, bool hidden);
    BattleInteractionResult SetLevel(BattlePreviewState& state, int level);
    BattleInteractionResult SetUnitType(BattlePreviewState& state,
                                        int unit_type);
    BattleInteractionResult SetTarget(BattlePreviewState& state, int data_id,
                                      int x, int y);

    void CaptureScriptState(const BattlePreviewState& state);
    void ClearScriptState() noexcept;
    bool ResetToScriptState(BattlePreviewState& state);

    void ResetSelection() noexcept;
    bool HasSelection() const noexcept { return selected_.has_value(); }

private:
    struct Selection {
        LogicalNodeKey source_key;
        std::size_t record_index{};
    };

    std::optional<Selection> selected_;
    std::optional<BattlePreviewState> baseline_;

    BattleInteractionResult ResolveSelected(BattlePreviewState& state,
                                             BattleUnit*& unit);
    BattleInteractionResult RequireSelectedRole(BattlePreviewState& state,
                                                BattleFieldRole role,
                                                BattleUnit*& unit);
    static bool HasRole(const BattleUnit& unit, BattleFieldRole role);
    static std::wstring Summary(const BattleUnit& unit);
    static BattleInteractionResult ChangedResult(const std::wstring& before,
                                                 const std::wstring& after);
};

}  // namespace ccz::preview
