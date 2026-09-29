#include "BattleCharacterCatalog.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace ccz::editor_preview {
namespace {

constexpr std::size_t kDataHeader = 396;
constexpr std::size_t kDataRecordStride = 32;
constexpr std::size_t kNameFieldWidth = 8;
constexpr std::size_t kArmsOffset = 26;
constexpr std::size_t kSmallRecordCount = 512;
constexpr std::size_t kLargeRecordCount = 1024;
constexpr std::size_t kLargeCatalogThreshold = 46000;

bool decode_cp936_name(const std::uint8_t* bytes, std::size_t size,
                       std::wstring& result) {
    result.clear();
    if (bytes == nullptr || size == 0 || size > kNameFieldWidth) return false;
    const auto* terminator = std::find(bytes, bytes + size, 0);
    const int byte_count = static_cast<int>(terminator - bytes);
    if (byte_count == 0) return true;
    const int char_count = MultiByteToWideChar(
        936, 0, reinterpret_cast<const char*>(bytes), byte_count,
        nullptr, 0);
    if (char_count <= 0) return false;
    result.resize(static_cast<std::size_t>(char_count));
    if (MultiByteToWideChar(936, 0,
            reinterpret_cast<const char*>(bytes), byte_count,
            result.data(), char_count) != char_count) {
        result.clear();
        return false;
    }
    return true;
}

std::size_t record_count_for_size(std::size_t size) {
    if (size >= kLargeCatalogThreshold) return kLargeRecordCount;
    const std::size_t small_end =
        kDataHeader + kSmallRecordCount * kDataRecordStride;
    if (size >= small_end) return kSmallRecordCount;
    return 0;
}

void append_brush_run(const std::vector<BattleCharacterEntry>& characters,
                      std::size_t begin, std::size_t end,
                      const std::wstring& normalized,
                      std::vector<BattleMinionBrush>& brushes) {
    if (normalized.empty() || end - begin < 2) return;
    brushes.push_back(BattleMinionBrush{
        normalized,
        characters[begin].data_id,
        characters[end - 1].data_id});
}

std::vector<BattleMinionBrush> build_brushes(
    const std::vector<BattleCharacterEntry>& characters) {
    std::vector<BattleMinionBrush> brushes;
    std::size_t begin = 0;
    std::wstring key;
    for (std::size_t index = 0; index <= characters.size(); ++index) {
        const bool at_end = index == characters.size();
        const std::wstring next_key = at_end || !characters[index].has_name
            ? std::wstring{}
            : NormalizeBattleBrushName(characters[index].name);
        const bool contiguous = !at_end && index > begin &&
            characters[index].data_id == characters[index - 1].data_id + 1;
        if (index == begin) {
            key = next_key;
            continue;
        }
        if (!at_end && contiguous && next_key == key) continue;

        append_brush_run(characters, begin, index, key, brushes);
        begin = index;
        key = next_key;
    }
    return brushes;
}

} // namespace

std::wstring NormalizeBattleBrushName(const std::wstring& name) {
    std::wstring normalized;
    normalized.reserve(name.size());
    for (wchar_t character : name) {
        if (character < L'0' || character > L'9')
            normalized.push_back(character);
    }
    return normalized;
}

BattleCharacterCatalog ParseBattleCharacterCatalog(
    const std::vector<std::uint8_t>& data_e5) {
    BattleCharacterCatalog result;
    const std::size_t record_count = record_count_for_size(data_e5.size());
    if (record_count == 0) {
        result.diagnostic =
            L"Data.e5 不足 512 条 S 角色记录（至少需要 16780 字节）";
        return result;
    }

    result.characters.reserve(record_count);
    for (std::size_t data_id = 0; data_id < record_count; ++data_id) {
        const std::size_t offset = kDataHeader + data_id * kDataRecordStride;
        if (offset > data_e5.size() ||
            data_e5.size() - offset < kDataRecordStride ||
            kNameFieldWidth > kDataRecordStride || kArmsOffset >= kDataRecordStride) {
            result.characters.clear();
            result.diagnostic = L"Data.e5 S 角色记录越界或截断";
            return result;
        }

        std::wstring name;
        if (!decode_cp936_name(data_e5.data() + offset,
                               kNameFieldWidth, name)) {
            result.characters.clear();
            result.diagnostic = L"Data.e5 角色名称不是有效的 CP936 字段";
            return result;
        }
        const bool has_name = !name.empty();
        if (name.empty()) name = L"Data " + std::to_wstring(data_id);

        BattleCharacterEntry entry;
        entry.data_id = static_cast<int>(data_id);
        entry.has_name = has_name;
        entry.name = std::move(name);
        entry.appearance = -1;
        entry.default_direction = 0;
        entry.level = 0;
        // Pro's brush arm_kind is an independent selection value and defaults
        // to zero. Data.e5's arms byte belongs to the asset mapping table.
        entry.unit_type = 0;
        entry.reinforcement = false;
        entry.ai_policy = -1;
        result.characters.push_back(std::move(entry));
    }

    result.brushes = build_brushes(result.characters);
    result.ok = true;
    return result;
}

} // namespace ccz::editor_preview
