#include <ccz/preview/e5_archive.h>

#include <cstdint>

namespace ccz::preview {
namespace {

constexpr std::size_t kIndexOffset = 0x110;
constexpr std::size_t kIndexEntrySize = 12;
constexpr std::size_t kIndexPadding = 4;

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

}  // namespace

bool find_e5_resource(const std::vector<std::uint8_t>& archive,
                      std::size_t resource_index,
                      std::size_t maximum_entry_count,
                      E5ResourceView& result) noexcept {
    result = {};
    if (maximum_entry_count == 0 ||
        archive.size() < kIndexOffset + kIndexEntrySize) return false;

    const std::uint32_t first_resource_offset =
        read_be32(archive, kIndexOffset + 8);
    const std::uint64_t minimum_first_resource =
        static_cast<std::uint64_t>(kIndexOffset) + kIndexPadding;
    if (first_resource_offset < minimum_first_resource ||
        first_resource_offset > archive.size()) return false;

    const std::size_t table_span =
        static_cast<std::size_t>(first_resource_offset) -
        kIndexOffset - kIndexPadding;
    if (table_span % kIndexEntrySize != 0) return false;
    const std::size_t entry_count = table_span / kIndexEntrySize;
    if (entry_count == 0 || entry_count > maximum_entry_count ||
        resource_index >= entry_count) return false;

    const std::size_t entry_offset =
        kIndexOffset + resource_index * kIndexEntrySize;
    if (entry_offset > archive.size() ||
        archive.size() - entry_offset < kIndexEntrySize) return false;
    const std::uint32_t resource_size = read_be32(archive, entry_offset);
    const std::uint32_t resource_offset =
        read_be32(archive, entry_offset + 8);
    if (resource_size == 0 || resource_offset < first_resource_offset ||
        resource_offset > archive.size() ||
        resource_size > archive.size() - resource_offset) return false;

    result.data = archive.data() + resource_offset;
    result.size = resource_size;
    return result.valid();
}

}  // namespace ccz::preview
