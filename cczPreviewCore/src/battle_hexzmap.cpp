#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/e5_archive.h>

#include <cstddef>
#include <cstdint>

namespace ccz::preview {
namespace {

constexpr int kMaximumGridDimension = 1024;

}  // namespace

bool decode_battle_hexzmap(const std::vector<std::uint8_t>& archive,
                           int map_id, int expected_width,
                           int expected_height,
                           std::vector<std::uint8_t>& terrain) {
    terrain.clear();
    if (map_id < 0 || expected_width <= 0 || expected_height <= 0 ||
        expected_width > kMaximumGridDimension ||
        expected_height > kMaximumGridDimension) {
        return false;
    }

    // E5 indexes use 12-byte entries from 0x110.  The first resource begins
    // after the table and a four-byte gap; its full 32-bit big-endian offset
    // determines how many entries are present.  Pro's original reader only
    // retained the low 16 bits, but larger Mod archives legitimately exceed
    // 64 KiB, so the editor keeps the complete E5 offset.
    E5ResourceView resource;
    if (!find_e5_resource(archive, static_cast<std::size_t>(map_id), 300,
                          resource) || resource.size < 2) return false;

    const std::uint8_t pixel_width = resource.data[0];
    const std::uint8_t pixel_height = resource.data[1];
    if (pixel_width == 0 || pixel_height == 0 || pixel_width % 3 != 0 ||
        pixel_height % 3 != 0 || pixel_width / 3 != expected_width ||
        pixel_height / 3 != expected_height) {
        return false;
    }

    const std::size_t cell_count =
        static_cast<std::size_t>(expected_width) *
        static_cast<std::size_t>(expected_height);
    const std::size_t terrain_offset = 2;
    if (resource.size < cell_count + 2) return false;

    // Pro's LoadHexz loops y first and x second, storing map_hexz[x, y].
    // The editor's flat terrain array uses the same row-major byte order.
    terrain.assign(resource.data + terrain_offset,
                   resource.data + terrain_offset + cell_count);
    return true;
}

}  // namespace ccz::preview
