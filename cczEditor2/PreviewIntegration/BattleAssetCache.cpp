#include "BattleAssetCache.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace ccz::editor_preview {
namespace {

constexpr std::uint64_t kMaximumAssetBytes = 128ull * 1024ull * 1024ull;

std::wstring source_root_from_document_path(const std::wstring& path) {
    if (path.empty()) return {};
    for (std::size_t index = path.size(); index-- > 0;) {
        if (path[index] != L'\\' && path[index] != L'/') continue;
        if (index >= 2 && (path[index - 1] == L'S' || path[index - 1] == L's') &&
            (path[index - 2] == L'R' || path[index - 2] == L'r')) continue;
        return path.substr(0, index + 1);
    }
    return {};
}

std::wstring normalize_root(std::wstring value) {
    std::replace(value.begin(), value.end(), L'/', L'\\');
    while (value.size() > 3 && !value.empty() && value.back() == L'\\') value.pop_back();
    for (wchar_t& c : value) c = std::towlower(c);
    return value;
}

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& bytes,
               std::wstring& diagnostic) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        diagnostic = L"资源不存在或无法打开：" + path;
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaximumAssetBytes) {
        CloseHandle(file);
        diagnostic = L"资源大小超出安全上限：" + path;
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>((std::min)(
            bytes.size() - offset, static_cast<std::size_t>(1u << 20)));
        DWORD received = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &received, nullptr) ||
            received == 0) {
            bytes.clear();
            CloseHandle(file);
            diagnostic = L"读取资源失败：" + path;
            return false;
        }
        offset += received;
    }
    CloseHandle(file);
    return true;
}

void append_diagnostic(std::wstring& target, const std::wstring& value) {
    if (value.empty()) return;
    if (!target.empty()) target += L"；";
    target += value;
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
    if (offset + 4 > bytes.size()) return 0;
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint16_t read_le16(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
    if (offset + 2 > bytes.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

}  // namespace

bool BattleAssetCache::LoadForDocument(const std::wstring& document_path,
                                       int map_id,
                                       std::wstring* diagnostic) {
    const std::wstring root = source_root_from_document_path(document_path);
    if (root.empty()) {
        Clear();
        diagnostic_ = L"无法从当前剧本路径确定 Mod 根目录";
        if (diagnostic) *diagnostic = diagnostic_;
        return false;
    }
    const std::wstring key = normalize_root(root);
    if (key == root_key_ && map_id == map_id_) {
        if (diagnostic) *diagnostic = diagnostic_;
        return valid();
    }

    Clear();
    root_key_ = key;
    map_id_ = map_id;

    std::wstring read_diagnostic;
    const std::wstring data_path = root + L"Data.e5";
    if (!read_file(data_path, data_e5_, read_diagnostic))
        append_diagnostic(diagnostic_, L"Data.e5 未加载");

    read_diagnostic.clear();
    // Hexzmap.e5 is a Mod-root resource in the editor/game directory layout.
    // It is independent from the loose JPG used for the S battle background.
    const std::wstring hexz_path = root + L"Hexzmap.e5";
    if (!read_file(hexz_path, hexzmap_e5_, read_diagnostic))
        append_diagnostic(diagnostic_, L"Hexzmap.e5 未加载");

    // S battle backgrounds are loose JPGs in <ModRoot>\map.  Keep the
    // three-digit native filename convention and let Windows handle the case
    // of the on-disk .JPG extension.
    if (map_id >= 0) {
        std::wstringstream filename;
        filename << root << L"map\\m" << std::setfill(L'0') << std::setw(3)
                 << map_id << L".jpg";
        map_jpg_path_ = filename.str();
        if (!file_exists(map_jpg_path_)) {
            append_diagnostic(diagnostic_, L"战场地图 JPG 未找到：" + map_jpg_path_);
            map_jpg_path_.clear();
        }
    } else {
        append_diagnostic(diagnostic_, L"地图编号未解析");
    }

    std::wstring unit_diagnostic;
    if (!LoadUnitSpriteAssets(root, unit_diagnostic))
        append_diagnostic(diagnostic_, unit_diagnostic);
    if (diagnostic) *diagnostic = diagnostic_;
    return valid();
}

void BattleAssetCache::Clear() {
    root_key_.clear();
    map_id_ = -1;
    map_jpg_path_.clear();
    unit_mov_path_.clear();
    data_e5_.clear();
    hexzmap_e5_.clear();
    unit_mov_e5_.clear();
    arms_.clear();
    appearance_.clear();
    unit_mov_entries_.clear();
    unit_sprite_ready_ = false;
    diagnostic_.clear();
}

bool BattleAssetCache::LoadUnitSpriteAssets(const std::wstring& root,
                                            std::wstring& diagnostic) {
    unit_sprite_ready_ = false;
    unit_mov_entries_.clear();
    arms_.clear();
    appearance_.clear();
    unit_mov_e5_.clear();
    unit_mov_path_ = root + L"Unit_mov.e5";

    std::wstring read_diagnostic;
    if (!read_file(unit_mov_path_, unit_mov_e5_, read_diagnostic)) {
        diagnostic = L"Unit_mov.e5 未加载";
        return false;
    }

    constexpr std::size_t kIndexOffset = 0x110;
    if (unit_mov_e5_.size() < kIndexOffset + 12) {
        diagnostic = L"Unit_mov.e5 索引表截断";
        return false;
    }
    const std::uint32_t first_resource_offset =
        read_be32(unit_mov_e5_, kIndexOffset + 8);
    if (first_resource_offset <= kIndexOffset ||
        first_resource_offset > unit_mov_e5_.size()) {
        diagnostic = L"Unit_mov.e5 首资源偏移无效";
        return false;
    }
    const std::size_t entry_count =
        (static_cast<std::size_t>(first_resource_offset) - kIndexOffset) / 12;
    if (entry_count == 0 || entry_count > 4096) {
        diagnostic = L"Unit_mov.e5 资源索引数量无效";
        return false;
    }
    unit_mov_entries_.reserve(entry_count);
    for (std::size_t index = 0; index < entry_count; ++index) {
        const std::size_t table_offset = kIndexOffset + index * 12;
        if (table_offset + 12 > unit_mov_e5_.size()) {
            unit_mov_entries_.clear();
            diagnostic = L"Unit_mov.e5 索引项截断";
            return false;
        }
        const std::uint32_t size = read_be32(unit_mov_e5_, table_offset);
        const std::uint32_t offset = read_be32(unit_mov_e5_, table_offset + 8);
        if (size == 0 || offset > unit_mov_e5_.size() ||
            static_cast<std::uint64_t>(offset) + size > unit_mov_e5_.size()) {
            unit_mov_entries_.clear();
            diagnostic = L"Unit_mov.e5 存在越界资源项";
            return false;
        }
        unit_mov_entries_.push_back({size, offset});
    }

    // Pro's large Data.e5 branch reads 32-byte records after a 396-byte
    // header; arms[] is the byte at record offset +26.  The current engine
    // uses 1024 records, which is also the only branch for which the EXE
    // appearance table is two bytes per data id.
    constexpr std::size_t kDataHeader = 396;
    constexpr std::size_t kRecordSize = 32;
    constexpr std::size_t kDataCount = 1024;
    constexpr std::size_t kArmsOffset = 26;
    if (data_e5_.size() < kDataHeader + kDataCount * kRecordSize) {
        diagnostic = L"Data.e5 角色记录不足 1024 条，暂不启用 S 形象映射";
        return false;
    }
    arms_.resize(kDataCount);
    for (std::size_t data_id = 0; data_id < kDataCount; ++data_id) {
        arms_[data_id] = data_e5_[kDataHeader + data_id * kRecordSize +
                                  kArmsOffset];
    }

    std::vector<std::uint8_t> exe_bytes;
    const std::wstring exe_path = root + L"Ekd5.exe";
    if (!read_file(exe_path, exe_bytes, read_diagnostic)) {
        diagnostic = L"Ekd5.exe 未加载，无法读取 S 形象外观表";
        return false;
    }
    constexpr std::size_t kAppearanceOffset = 0xD2800;
    if (exe_bytes.size() < kAppearanceOffset + kDataCount * 2) {
        diagnostic = L"Ekd5.exe 外观表（0xD2800）截断";
        return false;
    }
    appearance_.resize(kDataCount);
    for (std::size_t data_id = 0; data_id < kDataCount; ++data_id) {
        appearance_[data_id] =
            read_le16(exe_bytes, kAppearanceOffset + data_id * 2);
    }

    unit_sprite_ready_ = true;
    return true;
}

bool BattleAssetCache::ResolveUnitSprite(int data_id, int arm_kind,
                                         int direction,
                                         UnitSpriteRef* result) const noexcept {
    if (result == nullptr) return false;
    *result = {};
    if (!unit_sprite_ready_ || data_id < 0 ||
        static_cast<std::size_t>(data_id) >= arms_.size() || arm_kind < 0 ||
        arm_kind > 255 || static_cast<std::size_t>(data_id) >= appearance_.size()) {
        return false;
    }

    const int appearance = appearance_[static_cast<std::size_t>(data_id)];
    int resource_index = -1;
    if (appearance == 0) {
        resource_index = 3 * static_cast<int>(
            arms_[static_cast<std::size_t>(data_id)]);
        if (data_id >= 30) resource_index += arm_kind;
    } else if (appearance >= 1 && appearance <= 32) {
        resource_index = 240 + 3 * (appearance - 1);
    } else {
        resource_index = appearance + 303;
    }
    if (resource_index < 0 ||
        static_cast<std::size_t>(resource_index) >= unit_mov_entries_.size()) {
        return false;
    }

    // Pro's MyButton2.onPress converts the raw EEX direction before calling
    // Piece.Init: visual_direction = 3 - raw_direction.  Keep this mapping
    // in the visual resolver only; the parser/writeback contract remains the
    // raw 0x46/0x47/0x4B field value. Piece.Init maps visual 0 to frame pair
    // (4/5) without flipping, visual 1 to (0/1), visual 2 to (4/5) with a
    // horizontal flip, and visual 3 to (2/3). Keep all four cases distinct:
    // visual 0 and visual 3 use different frame pairs even though neither is
    // flipped.
    const int visual_direction =
        direction >= 0 && direction < 4 ? 3 - direction : 3;
    switch (visual_direction) {
    case 0:
        result->frame = 4;
        result->flip_x = false;
        break;
    case 1:
        result->frame = 0;
        result->flip_x = false;
        break;
    case 2:
        result->frame = 4;
        result->flip_x = true;
        break;
    case 3:
        result->frame = 2;
        result->flip_x = false;
        break;
    default:
        return false;
    }
    result->resource_index = resource_index;
    return true;
}

}  // namespace ccz::editor_preview
