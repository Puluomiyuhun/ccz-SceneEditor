#pragma once

#include <ccz/preview/battle_preview.h>

#include "BattleCharacterCatalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccz::editor_preview {

// Battle resources are kept separate from the R/portrait cache.  This class
// owns immutable bytes, proven S-unit mapping metadata and path decisions; it
// never mutates an EEX document.
class BattleAssetCache {
public:
    struct UnitSpriteRef {
        int resource_index{-1};
        int frame{-1};
        bool flip_x{false};
    };

    bool LoadForDocument(const std::wstring& document_path, int map_id,
                         std::wstring* diagnostic);
    void Clear();

    const std::vector<std::uint8_t>& data_e5() const noexcept { return data_e5_; }
    const std::vector<std::uint8_t>& hexzmap_e5() const noexcept { return hexzmap_e5_; }
    const BattleCharacterCatalog& character_catalog() const noexcept {
        return character_catalog_;
    }
    // S battle maps are loose JPG files under the Mod's map directory.  They
    // are deliberately kept separate from the R-scene Mmap.e5 cache.
    const std::wstring& map_jpg_path() const noexcept { return map_jpg_path_; }
    std::uint64_t map_jpg_revision() const noexcept {
        return map_jpg_revision_;
    }
    const std::wstring& unit_mov_path() const noexcept { return unit_mov_path_; }
    std::uint64_t unit_mov_revision() const noexcept {
        return unit_mov_revision_;
    }
    const std::wstring& diagnostic() const noexcept { return diagnostic_; }
    bool valid() const noexcept { return !data_e5_.empty() || !hexzmap_e5_.empty(); }

    // Resolve an S-battle unit to a Unit_mov.e5 resource and standing frame.
    // The arm_kind argument is the visual faction derived from the effective
    // deployment instruction (0x4B own=0, 0x46 ally=1, 0x47 enemy=2), not
    // BattleUnit::unit_type. Only appearance-0 units with Data ID >= 30 use it;
    // callers must not use the R-scene portrait/character image table here.
    // direction is the raw source direction stored in 0x46/0x47/0x4B.  The
    // visual resolver applies Pro's painter-side 3 - raw-direction mapping;
    // parser and writeback code intentionally keep the raw value unchanged.
    bool ResolveUnitSprite(int data_id, int arm_kind, int direction,
                           UnitSpriteRef* result) const noexcept;
    bool unit_sprite_ready() const noexcept { return unit_sprite_ready_; }

private:
    struct E5Entry {
        std::uint32_t size{};
        std::uint32_t offset{};
    };

    bool LoadUnitSpriteAssets(const std::wstring& root,
                              std::wstring& diagnostic);

    std::wstring root_key_;
    std::uint64_t root_revision_{};
    int map_id_{-1};
    std::wstring map_jpg_path_;
    std::uint64_t map_jpg_revision_{};
    std::wstring unit_mov_path_;
    std::uint64_t unit_mov_revision_{};
    std::vector<std::uint8_t> data_e5_;
    std::vector<std::uint8_t> hexzmap_e5_;
    std::vector<std::uint8_t> unit_mov_e5_;
    BattleCharacterCatalog character_catalog_;
    std::vector<std::uint8_t> arms_;
    std::vector<std::uint16_t> appearance_;
    std::vector<E5Entry> unit_mov_entries_;
    bool unit_sprite_ready_{false};
    std::wstring root_diagnostic_;
    std::wstring diagnostic_;
};

}  // namespace ccz::editor_preview
