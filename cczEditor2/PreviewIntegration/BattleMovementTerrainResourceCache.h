#pragma once

#include <ccz/preview/battle_movement_terrain.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ccz::editor_preview {

// S-preview Mod resources preload one immutable movement table per normalized
// Mod root and Data.e5 revision. Document sessions only retain a shared const
// reference, so opening another script never copies or reparses the matrix.
class BattleMovementTerrainResourceCache {
public:
    using Table = ccz::preview::BattleMovementTerrainTable;
    using TablePtr = std::shared_ptr<const Table>;

    TablePtr Preload(const std::wstring& normalized_mod_root,
                     std::uint64_t data_e5_revision,
                     const std::vector<std::uint8_t>& data_e5) {
        if (normalized_mod_root.empty() || data_e5.empty()) return {};

        const Key key{normalized_mod_root, data_e5_revision};
        const auto cached = entries_.find(key);
        if (cached != entries_.end()) return cached->second;

        auto table = std::make_shared<Table>();
        ccz::preview::decode_battle_movement_terrain_table(data_e5, *table);
        TablePtr immutable_table = std::move(table);
        entries_.emplace(key, immutable_table);
        return immutable_table;
    }

    void Clear() noexcept { entries_.clear(); }

private:
    using Key = std::pair<std::wstring, std::uint64_t>;
    std::map<Key, TablePtr> entries_;
};

}  // namespace ccz::editor_preview
