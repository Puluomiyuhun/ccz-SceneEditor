#pragma once

#include <ccz/preview/battle_field_spec.h>
#include <ccz/preview/battle_preview.h>

#include <cstddef>
#include <cstdint>

namespace ccz::editor_preview {
// Read-only display predicate shared by the S canvas and Battle editing model.
// It does not stage, mutate, serialize, or write any record.
inline bool IsBattleFixedEmptySlot(const ccz::preview::BattleUnit& unit) {
    const ccz::preview::BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(unit.source_instruction);
    if (spec == nullptr || spec->record_capacity == 0 ||
        !unit.fields_decoded ||
        unit.raw_fields.size() != spec->record_stride ||
        !unit.dirty_fields.empty() || unit.data_id != -1) {
        return false;
    }
    return true;
}

}  // namespace ccz::editor_preview
