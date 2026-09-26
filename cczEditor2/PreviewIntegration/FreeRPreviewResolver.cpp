#include "FreeRPreviewResolver.h"

#include <cstddef>
#include <vector>

namespace ccz::editor_preview {
namespace {

constexpr int kSceneContainerId = -1;
constexpr int kSectionContainerId = -2;
constexpr int kDeployTestId = 0x07;
constexpr int kMenuProcessId = 0x08;
constexpr int kFreeRStartId = 0x28;
constexpr int kClickTestId = 0x2d;
constexpr int kAppearId = 0x30;

struct SceneRecord {
    const CapturedEditorNode* node{};
    std::size_t index{};
};

struct SectionRecord {
    const CapturedEditorNode* node{};
    const CapturedEditorNode* scene{};
    std::size_t scene_index{};
};

struct AppearanceCandidate {
    std::uintptr_t section_handle{};
    std::uintptr_t instruction_handle{};
};

bool has_section_candidate(
    const std::vector<AppearanceCandidate>& candidates,
    std::uintptr_t section_handle) {
    for (const AppearanceCandidate& candidate : candidates) {
        if (candidate.section_handle == section_handle) {
            return true;
        }
    }
    return false;
}

void append_diagnostic(std::wstring& destination, const wchar_t* message) {
    if (message == nullptr || *message == L'\0') {
        return;
    }
    if (!destination.empty()) {
        destination += L"；";
    }
    destination += message;
}

void collect_scenes(const CapturedEditorNode& document_root,
                   std::vector<SceneRecord>& scenes) {
    scenes.clear();
    for (const CapturedEditorNode& child : document_root.children) {
        if (child.instruction_id != kSceneContainerId) {
            continue;
        }
        scenes.push_back({&child, scenes.size()});
    }
}

void collect_sections(const SceneRecord& scene,
                      std::vector<SectionRecord>& sections) {
    sections.clear();
    for (const CapturedEditorNode& child : scene.node->children) {
        if (child.instruction_id != kSectionContainerId) {
            continue;
        }
        sections.push_back({&child, scene.node, scene.index});
    }
}

bool find_trigger_section(
    const std::vector<SceneRecord>& scenes,
    std::uintptr_t trigger_section_handle, SectionRecord& result) {
    for (const SceneRecord& scene : scenes) {
        std::vector<SectionRecord> sections;
        collect_sections(scene, sections);
        for (const SectionRecord& section : sections) {
            if (section.node->runtime_handle == trigger_section_handle) {
                result = section;
                return true;
            }
        }
    }
    return false;
}

bool has_marker(const CapturedEditorNode& node, int instruction_id,
                bool require_true) {
    if (node.instruction_id == instruction_id &&
        (!require_true || (!node.int_data.empty() && node.int_data[0] != 0))) {
        return true;
    }
    for (const CapturedEditorNode& child : node.children) {
        if (has_marker(child, instruction_id, require_true)) {
            return true;
        }
    }
    return false;
}

// Mirrors the source editor's code_test[] entries whose value is 2 (a
// condition that can gate a Section), except 0x02.  0x02 is the editor's
// internal-info container even though the legacy table marks it as nestable.
bool is_section_condition_id(int instruction_id) {
    switch (instruction_id) {
    case 0x04: // 询问测试
    case 0x05: // 变量测试
    case 0x07: // 出战测试
    case 0x25: // 武将进入指定地点测试
    case 0x26: // 武将进入指定区域测试
    case 0x2d: // 武将点击测试
    case 0x2e: // 武将相邻测试
    case 0x36: // 武将状态测试
    case 0x37: // 钱/章节/忠奸测试
    case 0x3c: // 武将加入测试
    case 0x3f: // 回合测试
    case 0x40: // 行动方测试
    case 0x41: // 战场人数测试
    case 0x42: // 战斗胜利测试
    case 0x43: // 战斗失败测试
    case 0x5e: // 武将不同测试
    case 0x6e: // 概率测试
    case 0x73: // 人物五围和测试
    case 0x79: // 变量测试
    case 0x7b: // 信息传送测试
        return true;
    default:
        return false;
    }
}

bool section_has_special_condition_header(
    const CapturedEditorNode& section, bool include_deployment_test) {
    // CreateFileTree() switches from the Section header to the body at the
    // first direct 0x00 node.  Only direct header commands are considered;
    // descendants of that boundary (including 0x11/0x76 jumps) are body
    // content and cannot classify the Section.
    for (const CapturedEditorNode& child : section.children) {
        if (child.instruction_id == 0) {
            break;
        }
        if (child.instruction_id == kDeployTestId &&
            !include_deployment_test) {
            continue;
        }
        if (is_section_condition_id(child.instruction_id)) {
            return true;
        }
    }
    return false;
}

bool scene_has_special_sections(const SceneRecord& scene,
                                bool include_deployment_test) {
    std::vector<SectionRecord> sections;
    collect_sections(scene, sections);
    for (const SectionRecord& section : sections) {
        if (section_has_special_condition_header(*section.node,
                                                  include_deployment_test)) {
            return true;
        }
    }
    return false;
}

bool section_has_header_instruction(const CapturedEditorNode& section,
                                    int instruction_id) {
    // Keep format classification inside the Section header.  The first
    // direct 0x00 is the editor's boundary between header and body; nested
    // body instructions must not identify the Section type.
    for (const CapturedEditorNode& child : section.children) {
        if (child.instruction_id == 0) {
            break;
        }
        if (child.instruction_id == instruction_id) {
            return true;
        }
    }
    return false;
}

bool last_section_has_header_instruction(const SceneRecord& scene,
                                         int instruction_id) {
    std::vector<SectionRecord> sections;
    collect_sections(scene, sections);
    if (sections.empty()) {
        return false;
    }
    return section_has_header_instruction(*sections.back().node,
                                          instruction_id);
}

bool contains_runtime_handle(const CapturedEditorNode& node,
                             std::uintptr_t handle) {
    if (node.runtime_handle == handle) {
        return true;
    }
    for (const CapturedEditorNode& child : node.children) {
        if (contains_runtime_handle(child, handle)) {
            return true;
        }
    }
    return false;
}

// Resolve the click gate in source order across the complete trigger Scene.
// A 0x2D in a Section header controls the body of that Section and, in the
// editor's Free-R layout, may be followed by another Section containing the
// selected dialogue/effect.  We therefore stop only at the selected node,
// not at the current Section boundary.  Body descendants after the first
// top-level 0x00 are deliberately ignored as format metadata; they must not
// create a second, accidental click gate.
bool find_click_target_in_trigger_scene(
    const CapturedEditorNode& scene, std::uintptr_t selected_handle,
    int& target_person_id, bool& selected_reached) {
    for (const CapturedEditorNode& section : scene.children) {
        if (section.instruction_id != kSectionContainerId) {
            continue;
        }
        if (section.runtime_handle == selected_handle) {
            selected_reached = true;
            return target_person_id >= 0;
        }

        bool in_header = true;
        for (const CapturedEditorNode& child : section.children) {
            if (in_header && child.instruction_id == kClickTestId &&
                child.int_data.size() >= 1) {
                target_person_id = child.int_data[0];
            }
            if (in_header && child.instruction_id == 0) {
                in_header = false;
            }

            // The selected node may be nested in an ordinary command.  Once
            // its subtree is reached, no later Section/header may influence
            // the pairing, but the header click test immediately before the
            // body remains valid.
            if (contains_runtime_handle(child, selected_handle)) {
                selected_reached = true;
                return target_person_id >= 0;
            }
        }
    }
    return false;
}

void collect_trigger_sections_before_selected(
    const SceneRecord& scene, std::uintptr_t selected_handle,
    std::vector<std::uintptr_t>& result) {
    result.clear();
    std::vector<SectionRecord> sections;
    collect_sections(scene, sections);
    for (const SectionRecord& section : sections) {
        if (section.node->runtime_handle == selected_handle ||
            contains_runtime_handle(*section.node, selected_handle)) {
            break;
        }
        result.push_back(section.node->runtime_handle);
    }
}

void collect_matching_appearances(
    const CapturedEditorNode& node, int target_person_id,
    const CapturedEditorNode* containing_section,
    std::vector<AppearanceCandidate>& candidates) {
    const CapturedEditorNode* section = containing_section;
    if (node.instruction_id == kSectionContainerId) {
        section = &node;
    }
    if (node.instruction_id == kAppearId && node.int_data.size() >= 1 &&
        node.int_data[0] == target_person_id) {
        if (section != nullptr && section->runtime_handle != 0 &&
            !has_section_candidate(candidates, section->runtime_handle)) {
            candidates.push_back({section->runtime_handle,
                                  node.runtime_handle});
        }
    }
    for (const CapturedEditorNode& child : node.children) {
        collect_matching_appearances(child, target_person_id, section,
                                     candidates);
    }
}

void collect_matching_appearances_in_scene(
    const SceneRecord& scene_record, int target_person_id,
    std::vector<AppearanceCandidate>& candidates) {
    std::vector<SectionRecord> sections;
    collect_sections(scene_record, sections);
    for (const SectionRecord& section : sections) {
        collect_matching_appearances(*section.node, target_person_id,
                                     section.node, candidates);
    }
}

void collect_matching_appearances_before_selected(
    const SceneRecord& scene_record, std::uintptr_t selected_handle,
    int target_person_id, std::vector<AppearanceCandidate>& candidates) {
    candidates.clear();
    std::vector<SectionRecord> sections;
    collect_sections(scene_record, sections);
    for (const SectionRecord& section : sections) {
        if (section.node->runtime_handle == selected_handle ||
            contains_runtime_handle(*section.node, selected_handle)) {
            break;
        }
        collect_matching_appearances(*section.node, target_person_id,
                                     section.node, candidates);
    }
}

}  // namespace

ScenePreviewResolution ResolveScenePreview(
    const CapturedEditorNode& document_root,
    std::uintptr_t trigger_section_handle,
    std::uintptr_t selected_handle) {
    FreeRPreviewResolution result;
    result.trigger_section_handle = trigger_section_handle;

    std::vector<SceneRecord> scenes;
    collect_scenes(document_root, scenes);

    SectionRecord trigger_section;
    if (!find_trigger_section(scenes, trigger_section_handle,
                              trigger_section)) {
        append_diagnostic(result.diagnostic, L"未找到当前触发 Section");
        return result;
    }
    result.trigger_scene_handle = trigger_section.scene->runtime_handle;

    if (trigger_section.scene_index == 0) {
        return result;
    }

    const SceneRecord& initial_scene =
        scenes[trigger_section.scene_index - 1];
    const SceneRecord trigger_scene{trigger_section.scene,
                                    trigger_section.scene_index};
    result.initial_scene_handle = initial_scene.node->runtime_handle;

    const bool free_r_marker = has_marker(*initial_scene.node, kFreeRStartId,
                                          true);
    const bool menu_marker = has_marker(*initial_scene.node, kMenuProcessId,
                                        true);
    // Do not use 0x07 to recognize Free-R: its exit path may be a 0x11/0x76
    // jump and the deployment-only marker must not become a Free-R criterion.
    const bool free_r_group = scene_has_special_sections(trigger_scene, false);
    const bool deployment_group =
        scene_has_special_sections(trigger_scene, true);
    const bool deployment_shape =
        deployment_group && menu_marker &&
        last_section_has_header_instruction(trigger_scene, kDeployTestId);
    const bool free_r_shape = free_r_group && free_r_marker;

    // Free-R takes priority when both marker families appear in a pair.  A
    // Free-R scene is not required to contain 0x07 出战测试.
    if (free_r_shape) {
        result.kind = ScenePreviewKind::FreeR;
        result.is_free_r = true;
    } else if (deployment_shape) {
        result.kind = ScenePreviewKind::Deployment;
    } else {
        return result;
    }

    result.resolved = true;
    if (result.kind != ScenePreviewKind::FreeR) {
        return result;
    }

    int target_person_id = -1;
    bool selected_reached = false;
    find_click_target_in_trigger_scene(*trigger_section.scene, selected_handle,
                                       target_person_id, selected_reached);
    if (!selected_reached) {
        result.resolved = false;
        append_diagnostic(result.diagnostic, L"选中节点不在当前触发 Scene 内");
        return result;
    }

    result.has_click_target = target_person_id >= 0;
    collect_trigger_sections_before_selected(
        trigger_scene, selected_handle,
        result.trigger_section_handles_before_selected);
    if (!result.has_click_target) {
        // Exit/jump Sections and other non-click branches remain valid Free-R
        // targets; they use the paired initial Scene as-is.
        return result;
    }
    result.target_person_id = target_person_id;

    // Some Free-R files use the second (trigger) Scene itself as a branch
    // setup table: an earlier click-controlled Section lays out the scene,
    // and a later click-controlled Section contains the dialogue/effect.  In
    // that shape the matching 0x30 is not in the preceding Scene at all.
    // Prefer the latest matching appearance before the selected Section so
    // the source-order state is the one inherited by the later branch.
    std::vector<AppearanceCandidate> trigger_appearances;
    collect_matching_appearances_before_selected(
        trigger_scene, selected_handle, target_person_id, trigger_appearances);
    if (!trigger_appearances.empty()) {
        const AppearanceCandidate& candidate = trigger_appearances.back();
        result.appearance_scene_handle = trigger_scene.node->runtime_handle;
        result.appearance_section_handle = candidate.section_handle;
        result.appearance_instruction_handle = candidate.instruction_handle;
        return result;
    }

    std::vector<AppearanceCandidate> appearance_candidates;
    collect_matching_appearances_in_scene(initial_scene, target_person_id,
                                          appearance_candidates);
    if (appearance_candidates.empty()) {
        result.resolved = false;
        append_diagnostic(result.diagnostic,
                          L"配对 Scene 内未找到匹配的武将出现 Section");
        return result;
    }
    if (appearance_candidates.size() != 1) {
        result.resolved = false;
        append_diagnostic(result.diagnostic,
                          L"配对 Scene 内存在多个匹配的武将出现 Section，无法安全选择");
        return result;
    }

    result.appearance_scene_handle = initial_scene.node->runtime_handle;
    result.appearance_section_handle =
        appearance_candidates.front().section_handle;
    result.appearance_instruction_handle =
        appearance_candidates.front().instruction_handle;
    return result;
}

}  // namespace ccz::editor_preview
