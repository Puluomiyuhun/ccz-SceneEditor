#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/FreeRPreviewResolver.h"

#include <cstdint>
#include <utility>

using ccz::editor_preview::CapturedEditorNode;
using ccz::editor_preview::FreeRPreviewResolution;
using ccz::editor_preview::ResolveScenePreview;
using ccz::editor_preview::ScenePreviewKind;

namespace {

CapturedEditorNode node(std::uintptr_t handle, int instruction_id,
                        std::vector<int> int_data = {},
                        std::vector<CapturedEditorNode> children = {}) {
    CapturedEditorNode result;
    result.runtime_handle = handle;
    result.instruction_id = instruction_id;
    result.int_data = std::move(int_data);
    result.children = std::move(children);
    return result;
}

CapturedEditorNode section(std::uintptr_t handle,
                           std::vector<CapturedEditorNode> children) {
    return node(handle, -2, {}, std::move(children));
}

CapturedEditorNode scene(std::uintptr_t handle,
                         std::vector<CapturedEditorNode> sections) {
    return node(handle, -1, {}, std::move(sections));
}

CapturedEditorNode document(
    std::vector<CapturedEditorNode> scenes) {
    return node(1, -1, {}, std::move(scenes));
}

CapturedEditorNode free_r_control(std::uintptr_t handle,
                                  std::vector<CapturedEditorNode> sections) {
    sections.insert(sections.begin(), section(handle + 1,
                                              {node(handle + 2, 40, {1})}));
    return scene(handle, std::move(sections));
}

CapturedEditorNode deployment_control(
    std::uintptr_t handle, std::vector<CapturedEditorNode> sections) {
    sections.insert(sections.begin(), section(handle + 1,
                                              {node(handle + 2, 8, {1})}));
    return scene(handle, std::move(sections));
}

}  // namespace

CCZ_TEST(free_r_resolver_limits_appearance_lookup_to_the_two_scene_pair) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02),
                             node(212, 0x2d, {7}),
                             node(213, 0),
                             node(214, 36, {}, {node(215, 24)})})});
    const CapturedEditorNode unrelated = scene(
        300, {section(310, {node(311, 0), node(312, 48, {7, 99, 99, 2, 0})})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger, unrelated}), 210, 214);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::FreeR);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.target_person_id, 7);
    CCZ_REQUIRE_EQ(result.appearance_scene_handle, std::uintptr_t(100));
    CCZ_REQUIRE_EQ(result.appearance_section_handle, std::uintptr_t(110));
    CCZ_REQUIRE_EQ(result.appearance_instruction_handle, std::uintptr_t(112));
    CCZ_REQUIRE_EQ(result.trigger_scene_handle, std::uintptr_t(200));
}

CCZ_TEST(free_r_resolver_rejects_trigger_without_pair_local_appearance) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {8, 10, 11, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)})});
    const CapturedEditorNode unrelated = scene(
        300, {section(310, {node(311, 0), node(312, 48, {7, 99, 99, 2, 0})})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger, unrelated}), 210, 214);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(result.diagnostic.find(L"配对 Scene") != std::wstring::npos);
}

CCZ_TEST(free_r_resolver_pairs_click_test_from_prior_section_in_trigger_scene) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0})})});
    // In real Free-R scenes the click gate commonly lives in one Section and
    // the selected dialogue/effect lives in a later Section of the same
    // trigger Scene.  Pairing must be bounded to this Scene, but must not be
    // bounded to the selected Section alone.
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x2d, {7}), node(212, 0)}),
              section(220, {node(221, 0x02), node(222, 0),
                            node(223, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 220, 223);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE(result.has_click_target);
    CCZ_REQUIRE_EQ(result.target_person_id, 7);
    CCZ_REQUIRE_EQ(result.appearance_section_handle, std::uintptr_t(110));
    CCZ_REQUIRE_EQ(result.trigger_section_handles_before_selected.size(),
                   std::size_t(1));
    CCZ_REQUIRE_EQ(result.trigger_section_handles_before_selected[0],
                   std::uintptr_t(210));
}

CCZ_TEST(free_r_resolver_pairs_appearance_from_prior_trigger_scene_setup_section) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0)})});
    // A Free-R trigger Scene may contain branch-local setup Sections before
    // the selected click branch.  The setup Section can introduce the target
    // actor even though it belongs to the trigger Scene rather than the
    // preceding initial Scene.
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x2d, {979}), node(212, 0),
                             node(213, 0x30, {965, 39, 72, 0, 0})}),
              section(220, {node(221, 0x02), node(222, 0x2d, {965}),
                            node(223, 0), node(224, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 220, 224);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE(result.has_click_target);
    CCZ_REQUIRE_EQ(result.target_person_id, 965);
    CCZ_REQUIRE_EQ(result.appearance_scene_handle, std::uintptr_t(200));
    CCZ_REQUIRE_EQ(result.appearance_section_handle, std::uintptr_t(210));
    CCZ_REQUIRE_EQ(result.appearance_instruction_handle, std::uintptr_t(213));
    CCZ_REQUIRE_EQ(result.trigger_section_handles_before_selected.size(),
                   std::size_t(1));
    CCZ_REQUIRE_EQ(result.trigger_section_handles_before_selected[0],
                   std::uintptr_t(210));
}

CCZ_TEST(free_r_resolver_accepts_non_click_node_before_click) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 24),
                             node(213, 0x2d, {7}), node(214, 0)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 212);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE(!result.has_click_target);
}

CCZ_TEST(free_r_resolver_rejects_ambiguous_pair_local_appearance) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0})}),
              section(120, {node(121, 0), node(122, 48, {7, 20, 21, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(result.diagnostic.find(L"多个匹配") != std::wstring::npos);
}

CCZ_TEST(free_r_resolver_deduplicates_repeated_appearance_in_one_section) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0}),
                            node(113, 48, {7, 20, 21, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.appearance_section_handle, std::uintptr_t(110));
    CCZ_REQUIRE_EQ(result.appearance_instruction_handle, std::uintptr_t(112));
}

CCZ_TEST(scene_resolver_does_not_classify_an_ordinary_scene_from_click_test_alone) {
    const CapturedEditorNode initial = scene(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
}

CCZ_TEST(scene_resolver_classifies_deployment_pair_by_menu_and_last_deploy_test) {
    const CapturedEditorNode initial = deployment_control(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02), node(222, 7), node(223, 0),
                             node(224, 8), node(225, 0)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Deployment);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE_EQ(result.initial_scene_handle, std::uintptr_t(100));
    CCZ_REQUIRE_EQ(result.trigger_scene_handle, std::uintptr_t(200));
}

CCZ_TEST(scene_resolver_does_not_classify_body_deploy_test_as_deployment) {
    const CapturedEditorNode initial = deployment_control(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02), node(222, 0),
                             node(223, 0x07)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(!result.is_free_r);
}

CCZ_TEST(scene_resolver_does_not_classify_nested_body_deploy_test_as_deployment) {
    const CapturedEditorNode initial = deployment_control(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02),
                            node(222, 0, {}, {node(223, 0x07)})})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.resolved);
    CCZ_REQUIRE(!result.is_free_r);
}

CCZ_TEST(scene_resolver_accepts_nested_body_free_r_marker) {
    const CapturedEditorNode initial = scene(
        100, {section(110, {node(111, 0x02),
                            node(112, 0, {}, {node(113, 0x28, {1})})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 24),
                             node(213, 0x2d, {7}), node(214, 0)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 212);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::FreeR);
    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE(!result.has_click_target);
}

CCZ_TEST(scene_resolver_ignores_false_nested_body_free_r_marker) {
    const CapturedEditorNode initial = scene(
        100, {section(110, {node(111, 0x02),
                            node(112, 0, {}, {node(113, 0x28, {0})})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 24),
                             node(213, 0x2d, {7}), node(214, 0)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 212);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
}

CCZ_TEST(scene_resolver_accepts_nested_body_menu_marker_for_deployment) {
    const CapturedEditorNode initial = scene(
        100, {section(110, {node(111, 0x02),
                            node(112, 0, {}, {node(113, 0x08, {1})})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02), node(222, 0x07),
                             node(223, 0), node(224, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Deployment);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(result.resolved);
}

CCZ_TEST(scene_resolver_ignores_false_nested_body_menu_marker_for_deployment) {
    const CapturedEditorNode initial = scene(
        100, {section(110, {node(111, 0x02),
                            node(112, 0, {}, {node(113, 0x08, {0})})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02), node(222, 0x07),
                             node(223, 0), node(224, 24)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
}

CCZ_TEST(scene_resolver_free_r_exit_does_not_require_deploy_test) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(220, {node(221, 0x02), node(222, 0x79), node(223, 0),
                             node(224, 0x76, {195})})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 220, 224);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::FreeR);
    CCZ_REQUIRE(result.is_free_r);
    CCZ_REQUIRE(result.resolved);
    CCZ_REQUIRE(!result.has_click_target);
}

CCZ_TEST(scene_resolver_ignores_body_unconditional_jump_for_special_shape) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0)})});
    const CapturedEditorNode trigger = scene(
        200, {section(220, {node(221, 0x02), node(222, 0),
                             node(223, 0x76, {195})})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 220, 223);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::Ordinary);
    CCZ_REQUIRE(!result.is_free_r);
    CCZ_REQUIRE(!result.resolved);
}

CCZ_TEST(scene_resolver_free_r_marker_wins_over_last_deploy_test) {
    const CapturedEditorNode initial = free_r_control(
        100, {section(110, {node(111, 0), node(112, 48, {7, 10, 11, 2, 0})})});
    const CapturedEditorNode trigger = scene(
        200, {section(210, {node(211, 0x02), node(212, 0x2d, {7}),
                             node(213, 0), node(214, 24)}),
              section(220, {node(221, 0x02), node(222, 7), node(223, 0),
                             node(224, 8), node(225, 0)})});

    const FreeRPreviewResolution result = ResolveScenePreview(
        document({initial, trigger}), 210, 214);

    CCZ_REQUIRE_EQ(result.kind, ScenePreviewKind::FreeR);
    CCZ_REQUIRE(result.resolved);
}
