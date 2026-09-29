#include "BattleQuickModeResolver.h"

#include "BattlePreviewAdapter.h"

#include <ccz/preview/battle_instruction_parser.h>

#include <utility>
#include <vector>

namespace ccz::editor_preview {
namespace {

constexpr int kSceneContainerId = -1;
constexpr int kSectionContainerId = -2;

bool find_section_path(const CapturedEditorNode& parent,
                       std::uintptr_t section_handle,
                       std::vector<std::uint32_t>& path) {
    for (std::size_t index = 0; index < parent.children.size(); ++index) {
        const CapturedEditorNode& child = parent.children[index];
        if (child.instruction_id == kSectionContainerId &&
            child.runtime_handle == section_handle) {
            path.push_back(static_cast<std::uint32_t>(index));
            return true;
        }
        path.push_back(static_cast<std::uint32_t>(index));
        if (find_section_path(child, section_handle, path)) return true;
        path.pop_back();
    }
    return false;
}

const CapturedEditorNode* section_at_path(
    const CapturedEditorNode& document_root,
    const std::vector<std::uint32_t>& section_path) {
    const CapturedEditorNode* current = &document_root;
    for (std::uint32_t index : section_path) {
        if (index >= current->children.size()) return nullptr;
        current = &current->children[index];
    }
    return current->instruction_id == kSectionContainerId ? current : nullptr;
}

const ccz::preview::RawReplayNode* raw_section_at_path(
    const std::vector<ccz::preview::RawReplayNode>& nodes,
    const std::vector<std::uint32_t>& section_path) {
    if (nodes.empty()) return nullptr;
    const ccz::preview::RawReplayNode* current = &nodes.front();
    for (std::uint32_t index : section_path) {
        if (index >= current->children.size()) return nullptr;
        current = &current->children[index];
    }
    // Scene and Section nodes are UI-only containers. ConvertNode maps both
    // negative editor IDs to kInvalidInstructionId, so the encoded ID cannot
    // distinguish a Section here. The caller has already validated this path
    // against a captured Section and checks its runtime handle below.
    return current;
}

ccz::preview::LogicalNodeKey scope_prefix(
    const ccz::preview::RawReplayNode& node) {
    constexpr wchar_t marker[] = L"|instruction:";
    const std::size_t position = node.logical_key.rfind(marker);
    return position == std::wstring::npos
        ? ccz::preview::LogicalNodeKey{}
        : node.logical_key.substr(0, position);
}

bool has_valid_battle_map(
    const BattlePreviewCapture& capture,
    const ccz::preview::RawReplayNode& raw_section,
    const ccz::preview::LogicalNodeKey& scope_key,
    std::wstring& diagnostic) {
    if (!capture.ok) {
        diagnostic = capture.diagnostic.empty()
            ? L"Section 无法建立 S 战场预览输入"
            : capture.diagnostic;
        return false;
    }

    ccz::preview::BattleParseOptions options;
    options.scope_root_key = scope_key;
    // The parser only needs this Section subtree. Passing the full document
    // would walk every node once per candidate Section during auto-resolution.
    const std::vector<ccz::preview::RawReplayNode> section_nodes{raw_section};
    const auto parsed = ccz::preview::parse_battle_instructions(
        capture.input.document_path, section_nodes, options);
    if (!parsed.ok) {
        if (!parsed.diagnostics.empty()) diagnostic = parsed.diagnostics.front();
        return false;
    }
    if (!parsed.state.map.valid || parsed.state.map.map_id < 0) {
        diagnostic = L"Section 没有有效的 S 战场地图记录";
        return false;
    }
    return true;
}

}  // namespace

bool SelectCapturedBattleSection(
    CapturedEditorTree document_snapshot,
    std::uintptr_t section_handle,
    const std::vector<std::uint32_t>& section_path,
    CapturedEditorTree& selected_snapshot,
    std::wstring& diagnostic) {
    selected_snapshot = {};
    diagnostic.clear();
    if (section_handle == 0 || document_snapshot.document_roots.empty()) {
        diagnostic = L"快速模式无法从完整剧本快照定位已绑定的 Section";
        return false;
    }

    const CapturedEditorNode& document_root =
        document_snapshot.document_roots.front();
    std::vector<std::uint32_t> actual_path;
    if (!find_section_path(document_root, section_handle, actual_path) ||
        actual_path != section_path) {
        diagnostic = L"快速模式绑定的 Scene1 Section 已不在当前剧本树中";
        return false;
    }
    const CapturedEditorNode* section =
        section_at_path(document_root, actual_path);
    if (section == nullptr) {
        diagnostic = L"快速模式绑定路径不再指向 Section 节点";
        return false;
    }

    document_snapshot.section_path = actual_path;
    document_snapshot.section_root = *section;
    selected_snapshot = std::move(document_snapshot);
    return true;
}

BattleQuickModeResolution ResolveScene1BattleSection(
    CapturedEditorTree document_snapshot) {
    BattleQuickModeResolution result;
    if (document_snapshot.document_roots.empty()) {
        result.diagnostic = L"快速模式无法定位 Scene1：剧本文档树为空";
        return result;
    }

    const CapturedEditorNode& document_root =
        document_snapshot.document_roots.front();
    const CapturedEditorNode* scene1 = nullptr;
    std::size_t scene_index = 0;
    for (std::size_t index = 0; index < document_root.children.size(); ++index) {
        if (document_root.children[index].instruction_id == kSceneContainerId) {
            scene1 = &document_root.children[index];
            scene_index = index;
            break;
        }
    }
    if (scene1 == nullptr) {
        result.diagnostic = L"快速模式无法定位 Scene1：剧本没有 Scene 容器";
        return result;
    }

    struct Candidate {
        std::uintptr_t section_handle{};
        std::vector<std::uint32_t> section_path;
        ccz::preview::LogicalNodeKey scope_key;
    };
    std::vector<Candidate> candidates;
    std::wstring first_candidate_diagnostic;
    std::size_t first_section_index = scene1->children.size();
    for (std::size_t index = 0; index < scene1->children.size(); ++index) {
        if (scene1->children[index].instruction_id == kSectionContainerId) {
            first_section_index = index;
            break;
        }
    }
    if (first_section_index == scene1->children.size()) {
        result.diagnostic = L"快速模式无法自动定位：Scene1 中没有有效的 S 战场地图 Section";
        return result;
    }

    Candidate first_candidate;
    first_candidate.section_handle =
        scene1->children[first_section_index].runtime_handle;
    first_candidate.section_path = {
        static_cast<std::uint32_t>(scene_index),
        static_cast<std::uint32_t>(first_section_index),
    };
    document_snapshot.section_path = first_candidate.section_path;
    document_snapshot.section_root = scene1->children[first_section_index];

    // Move the one captured document into the conversion result. The raw
    // replay tree then lets us validate every Scene1 Section without making a
    // second complete CapturedEditorTree copy.
    BattlePreviewCapture capture =
        BuildBattlePreviewInput(std::move(document_snapshot));
    if (!capture.ok) {
        first_candidate_diagnostic = capture.diagnostic;
    } else {
        const CapturedEditorNode* captured_scene1 = nullptr;
        if (!capture.snapshot.document_roots.empty() &&
            scene_index < capture.snapshot.document_roots.front().children.size()) {
            captured_scene1 = &capture.snapshot.document_roots.front()
                .children[scene_index];
        }
        if (captured_scene1 == nullptr ||
            captured_scene1->instruction_id != kSceneContainerId) {
            first_candidate_diagnostic =
                L"完整剧本快照中找不到 Scene1 容器";
        } else {
            for (std::size_t section_index = 0;
                 section_index < captured_scene1->children.size();
                 ++section_index) {
                const CapturedEditorNode& section =
                    captured_scene1->children[section_index];
                if (section.instruction_id != kSectionContainerId) continue;

                Candidate candidate;
                candidate.section_handle = section.runtime_handle;
                candidate.section_path = {
                    static_cast<std::uint32_t>(scene_index),
                    static_cast<std::uint32_t>(section_index),
                };

                const auto* raw_section = raw_section_at_path(
                    capture.input.nodes, candidate.section_path);
                if (raw_section == nullptr ||
                    raw_section->runtime_handle != candidate.section_handle) {
                    if (first_candidate_diagnostic.empty())
                        first_candidate_diagnostic =
                            L"完整剧本输入中找不到唯一的 Scene1 Section";
                    continue;
                }
                candidate.scope_key = scope_prefix(*raw_section);
                if (candidate.scope_key.empty()) {
                    if (first_candidate_diagnostic.empty())
                        first_candidate_diagnostic = L"无法建立候选 Section 作用域";
                    continue;
                }
                std::wstring parse_diagnostic;
                if (!has_valid_battle_map(capture, *raw_section,
                        candidate.scope_key, parse_diagnostic)) {
                    if (first_candidate_diagnostic.empty())
                        first_candidate_diagnostic =
                            std::move(parse_diagnostic);
                    continue;
                }
                candidates.push_back(std::move(candidate));
            }
        }
    }

    if (candidates.size() == 1) {
        const Candidate& candidate = candidates.front();
        const CapturedEditorNode* target_section = section_at_path(
            capture.snapshot.document_roots.front(), candidate.section_path);
        if (target_section == nullptr ||
            target_section->runtime_handle != candidate.section_handle) {
            result.diagnostic = L"快速模式绑定的 Scene1 Section 无法从已转换快照重定位";
            return result;
        }
        if (capture.snapshot.section_root.runtime_handle !=
            candidate.section_handle) {
            capture.snapshot.section_root = *target_section;
        }
        capture.snapshot.section_path = candidate.section_path;
        capture.input.scope_key = candidate.scope_key;
        capture.input.section_revision = ComputeBattleSectionRevision(
            capture.input.document_path, *target_section);
        if (capture.input.section_revision == 0) {
            result.diagnostic = L"快速模式无法计算固定 Scene1 Section 的内容版本";
            return result;
        }
        result.resolved = true;
        result.section_handle = candidate.section_handle;
        result.section_path = candidate.section_path;
        result.capture = std::move(capture);
        return result;
    }
    if (candidates.size() > 1) {
        result.diagnostic = L"快速模式无法自动选择：Scene1 中存在多个有效战场 Section";
        return result;
    }

    result.diagnostic = first_candidate_diagnostic.empty()
        ? L"快速模式无法自动定位：Scene1 中没有有效的 S 战场地图 Section"
        : L"快速模式无法自动定位：Scene1 中没有有效战场 Section（" +
              first_candidate_diagnostic + L"）";
    return result;
}

}  // namespace ccz::editor_preview
