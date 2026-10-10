#include <ccz/preview/face_archive.h>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace ccz::preview {
namespace {

constexpr std::uint8_t kPngSignature[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
};

bool can_read(const std::vector<std::uint8_t>& bytes, std::size_t offset,
              std::size_t count) {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset + 0]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

void fail(FaceArchive& archive, const wchar_t* message) {
    archive.valid = false;
    archive.entries.clear();
    archive.table_entry_count = 0;
    archive.diagnostic = message;
}

bool is_zero_padding(const std::vector<std::uint8_t>& bytes,
                     std::size_t offset, std::size_t count) {
    if (!can_read(bytes, offset, count)) {
        return false;
    }
    return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + count),
                       [](std::uint8_t value) { return value == 0; });
}

}  // namespace

FaceArchive parse_face_archive(
    const std::vector<std::uint8_t>& archive_bytes,
    const DialogueProfile& profile) {
    FaceArchive archive;
    const std::size_t table_offset = profile.face_index_table_offset;
    const std::size_t entry_size = profile.face_entry_size;

    if (entry_size < 12) {
        fail(archive, L"Face.e5 索引项小于 12 字节");
        return archive;
    }
    if (!can_read(archive_bytes, table_offset, entry_size)) {
        fail(archive, L"Face.e5 索引表起点或长度越界");
        return archive;
    }

    const std::uint32_t first_data_offset =
        read_be32(archive_bytes, table_offset + 8);
    if (first_data_offset <= table_offset ||
        !can_read(archive_bytes, first_data_offset, 1)) {
        fail(archive, L"Face.e5 首个图像偏移无效");
        return archive;
    }

    const std::size_t table_span =
        static_cast<std::size_t>(first_data_offset) - table_offset;
    std::size_t entry_count = 0;
    std::size_t table_end = 0;

    if (table_span % entry_size == 0) {
        entry_count = table_span / entry_size;
        table_end = table_offset + entry_count * entry_size;
    } else if (table_span >= 4 && (table_span - 4) % entry_size == 0 &&
               is_zero_padding(archive_bytes, first_data_offset - 4, 4)) {
        entry_count = (table_span - 4) / entry_size;
        table_end = table_offset + entry_count * entry_size;
    } else {
        fail(archive, L"Face.e5 索引表长度无法按索引项对齐");
        return archive;
    }

    if (entry_count == 0 || entry_count > std::numeric_limits<std::uint32_t>::max() ||
        table_end > first_data_offset) {
        fail(archive, L"Face.e5 索引项数量无效");
        return archive;
    }

    archive.data_start = first_data_offset;
    archive.table_entry_count = static_cast<std::uint32_t>(entry_count);
    archive.entries.reserve(entry_count);

    std::uint32_t previous_offset = first_data_offset;
    for (std::size_t index = 0; index < entry_count; ++index) {
        const std::size_t entry_offset = table_offset + index * entry_size;
        if (!can_read(archive_bytes, entry_offset, 12)) {
            fail(archive, L"Face.e5 索引项越界");
            return archive;
        }

        const std::uint32_t stored_size = read_be32(archive_bytes, entry_offset);
        const std::uint32_t decoded_size =
            read_be32(archive_bytes, entry_offset + 4);
        const std::uint32_t data_offset =
            read_be32(archive_bytes, entry_offset + 8);

        if (stored_size == 0 || decoded_size == 0 ||
            data_offset < first_data_offset || data_offset < previous_offset ||
            !can_read(archive_bytes, data_offset, stored_size)) {
            fail(archive, L"Face.e5 图像索引项的数据范围无效");
            return archive;
        }

        FaceArchiveEntry entry;
        entry.zero_based_index =
            static_cast<std::uint32_t>(index + profile.face_index_base);
        entry.stored_size = stored_size;
        entry.decoded_size = decoded_size;
        entry.data_offset = data_offset;
        entry.looks_like_png =
            stored_size >= sizeof(kPngSignature) &&
            std::equal(std::begin(kPngSignature), std::end(kPngSignature),
                       archive_bytes.begin() +
                           static_cast<std::ptrdiff_t>(data_offset));
        archive.entries.push_back(entry);
        previous_offset = data_offset;
    }

    archive.valid = true;
    return archive;
}

const FaceArchiveEntry* find_face_entry(
    const FaceArchive& archive, std::uint32_t zero_based_index) {
    if (!archive.valid) {
        return nullptr;
    }
    for (const FaceArchiveEntry& entry : archive.entries) {
        if (entry.zero_based_index == zero_based_index) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> copy_face_payload(
    const std::vector<std::uint8_t>& archive_bytes,
    const FaceArchiveEntry& entry) {
    if (!can_read(archive_bytes, entry.data_offset, entry.stored_size)) {
        return {};
    }
    return std::vector<std::uint8_t>(
        archive_bytes.begin() + static_cast<std::ptrdiff_t>(entry.data_offset),
        archive_bytes.begin() +
            static_cast<std::ptrdiff_t>(entry.data_offset + entry.stored_size));
}

}  // namespace ccz::preview


