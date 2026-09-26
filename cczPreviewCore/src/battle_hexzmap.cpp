#include <ccz/preview/battle_hexzmap.h>

#include <cstddef>
#include <cstdint>

namespace ccz::preview {
namespace {

constexpr std::size_t kE5IndexOffset = 0x110;
constexpr std::size_t kE5IndexEntrySize = 12;
constexpr std::size_t kE5IndexPadding = 4;
constexpr std::size_t kMaximumMapEntries = 300;
constexpr int kMaximumGridDimension = 1024;

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

}  // namespace

bool decode_battle_hexzmap(const std::vector<std::uint8_t>& archive,
                           int map_id, int expected_width,
                           int expected_height,
                           std::vector<std::uint8_t>& terrain) {
    terrain.clear();
    if (map_id < 0 || expected_width <= 0 || expected_height <= 0 ||
        expected_width > kMaximumGridDimension ||
        expected_height > kMaximumGridDimension ||
        archive.size() < kE5IndexOffset + kE5IndexEntrySize) {
        return false;
    }

    // E5 indexes use 12-byte entries from 0x110.  The first resource begins
    // after the table and a four-byte gap; its full 32-bit big-endian offset
    // determines how many entries are present.  Pro's original reader only
    // retained the low 16 bits, but larger Mod archives legitimately exceed
    // 64 KiB, so the editor keeps the complete E5 offset.
    const std::uint32_t first_resource_offset =
        read_be32(archive, kE5IndexOffset + 8);
    const std::uint64_t minimum_first_resource =
        static_cast<std::uint64_t>(kE5IndexOffset) + kE5IndexPadding;
    if (first_resource_offset < minimum_first_resource ||
        first_resource_offset > archive.size()) {
        return false;
    }

    const std::size_t table_span =
        static_cast<std::size_t>(first_resource_offset) -
        kE5IndexOffset - kE5IndexPadding;
    if (table_span % kE5IndexEntrySize != 0) return false;
    const std::size_t entry_count = table_span / kE5IndexEntrySize;
    if (entry_count == 0 || entry_count > kMaximumMapEntries ||
        static_cast<std::size_t>(map_id) >= entry_count) {
        return false;
    }

    const std::size_t entry_offset =
        kE5IndexOffset + static_cast<std::size_t>(map_id) *
                             kE5IndexEntrySize;
    const std::uint32_t resource_size = read_be32(archive, entry_offset);
    const std::uint32_t resource_offset = read_be32(archive, entry_offset + 8);
    if (resource_offset < first_resource_offset || resource_size < 2 ||
        resource_offset > archive.size() ||
        resource_size > archive.size() - resource_offset) {
        return false;
    }

    const std::uint8_t pixel_width = archive[resource_offset];
    const std::uint8_t pixel_height = archive[resource_offset + 1];
    if (pixel_width == 0 || pixel_height == 0 || pixel_width % 3 != 0 ||
        pixel_height % 3 != 0 || pixel_width / 3 != expected_width ||
        pixel_height / 3 != expected_height) {
        return false;
    }

    const std::size_t cell_count =
        static_cast<std::size_t>(expected_width) *
        static_cast<std::size_t>(expected_height);
    const std::size_t terrain_offset =
        static_cast<std::size_t>(resource_offset) + 2;
    if (resource_size < cell_count + 2 ||
        cell_count > archive.size() - terrain_offset) {
        return false;
    }

    // Pro's LoadHexz loops y first and x second, storing map_hexz[x, y].
    // The editor's flat terrain array uses the same row-major byte order.
    terrain.assign(archive.begin() + terrain_offset,
                   archive.begin() + terrain_offset + cell_count);
    return true;
}

}  // namespace ccz::preview
