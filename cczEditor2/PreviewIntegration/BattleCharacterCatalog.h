#pragma once

#include "BattleEditorOps.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

struct BattleCharacterCatalog {
    bool ok{};
    std::vector<BattleCharacterEntry> characters;
    std::vector<BattleMinionBrush> brushes;
    std::wstring diagnostic;
};

// Parse the Pro-compatible S Mod Data.e5 layout. Brush names use an 8-byte
// CP936/NUL field; no R character table is consulted.
BattleCharacterCatalog ParseBattleCharacterCatalog(
    const std::vector<std::uint8_t>& data_e5);

// The Pro brush key removes ASCII digits only. It does not alter catalog
// display names and is exposed to keep the grouping rule independently
// testable.
std::wstring NormalizeBattleBrushName(const std::wstring& name);

} // namespace ccz::editor_preview
