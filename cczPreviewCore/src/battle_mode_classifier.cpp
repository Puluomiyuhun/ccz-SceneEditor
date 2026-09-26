#include <ccz/preview/battle_mode_classifier.h>

#include <algorithm>
#include <cwctype>

namespace ccz::preview {
namespace {

bool IsBattleMarker(std::uint16_t instruction_id) {
    switch (instruction_id) {
    case 0x44:
    case 0x46:
    case 0x47:
    case 0x4b:
        return true;
    default:
        return false;
    }
}

bool StartsWith(const std::wstring& value, const wchar_t* prefix) {
    const std::wstring expected(prefix);
    return value.size() >= expected.size() &&
           value.compare(0, expected.size(), expected) == 0;
}

std::wstring NormalizedFileName(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    const std::size_t separator = path.find_last_of(L'\\');
    if (separator != std::wstring::npos) {
        path.erase(0, separator + 1);
    }
    std::transform(path.begin(), path.end(), path.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towupper(character));
                   });
    return path;
}

bool ContainsBattleMarker(const std::vector<RawReplayNode>& nodes) {
    for (const RawReplayNode& node : nodes) {
        if (IsBattleMarker(node.instruction_id) ||
            ContainsBattleMarker(node.children)) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool contains_battle_marker(const std::vector<RawReplayNode>& nodes) {
    return ContainsBattleMarker(nodes);
}

BattleModeDecision classify_preview_mode(
    const std::wstring& document_path,
    const std::vector<RawReplayNode>& document_nodes) {
    BattleModeDecision result;
    const std::wstring file_name = NormalizedFileName(document_path);
    const bool battle_candidate = StartsWith(file_name, L"S_") ||
                                  StartsWith(file_name, L"S-");
    const bool ordinary_candidate = StartsWith(file_name, L"R_") ||
                                    StartsWith(file_name, L"R-");

    result.prefix_candidate = battle_candidate;
    result.marker_valid = ContainsBattleMarker(document_nodes);

    if (battle_candidate) {
        if (result.marker_valid) {
            result.mode = PreviewMode::Battle;
            return result;
        }
        result.diagnostic = L"S 文档缺少可验证战场标记（0x44/0x46/0x47/0x4B）";
        return result;
    }

    if (ordinary_candidate) {
        // R documents remain on the existing route even if a future command
        // happens to use an id that is meaningful to Battle preview.
        result.mode = PreviewMode::Ordinary;
        result.prefix_candidate = false;
        return result;
    }

    if (result.marker_valid) {
        result.diagnostic = L"文档前缀无法确认，拒绝推断为战场剧本";
    } else {
        result.diagnostic = L"文档前缀和战场标记均无法确认";
    }
    return result;
}

}  // namespace ccz::preview
