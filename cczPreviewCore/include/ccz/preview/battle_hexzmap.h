#pragma once

#include <cstdint>
#include <vector>

namespace ccz::preview {

// Decode one S-battle terrain grid from the Mod-root Hexzmap.e5 archive.
// Expected dimensions are logical cells, as derived from the matching map JPG.
// On failure, terrain is cleared so callers cannot accidentally reuse stale
// map data.
bool decode_battle_hexzmap(const std::vector<std::uint8_t>& archive,
                           int map_id, int expected_width,
                           int expected_height,
                           std::vector<std::uint8_t>& terrain);

}  // namespace ccz::preview
