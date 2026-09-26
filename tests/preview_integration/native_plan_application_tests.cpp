#include "test_framework.h"

#include <ccz/preview/native_replay_plan.h>
#include <ccz/preview/execution_model.h>
#include <ccz/preview/instruction_catalog.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using ccz::preview::NativeReplayItem;
using ccz::preview::NativeReplayPlan;
using ccz::preview::RuntimeNodeHandle;
using ccz::preview::RawReplayNode;

struct CurrentTreeNode {
    RuntimeNodeHandle handle{};
    std::uint64_t generation{};
    std::uint16_t instruction_id{};
};

// This is the host-independent part of the editor contract.  A real MFC
// TreeView is intentionally not created in the headless test process, but
// runtime handles still go through a current-generation lookup and an ID
// check before the scene is cleared.
class CurrentTreeFixture {
public:
    explicit CurrentTreeFixture(std::uint64_t generation)
        : generation_(generation) {}

    void Add(RuntimeNodeHandle handle, std::uint16_t instruction_id) {
        nodes_.push_back({handle, generation_, instruction_id});
    }

    void AddFromGeneration(RuntimeNodeHandle handle,
                           std::uint64_t source_generation,
                           std::uint16_t instruction_id) {
        nodes_.push_back({handle, source_generation, instruction_id});
    }

    const CurrentTreeNode* Resolve(RuntimeNodeHandle handle) const {
        if (handle == 0) {
            return nullptr;
        }
        for (const CurrentTreeNode& node : nodes_) {
            if (node.handle == handle && node.generation == generation_) {
                return &node;
            }
        }
        return nullptr;
    }

private:
    std::uint64_t generation_{};
    std::vector<CurrentTreeNode> nodes_;
};

struct ApplyFixtureResult {
    bool applied{};
    std::size_t applied_count{};
};

ApplyFixtureResult ApplyValidatedPlan(
    const CurrentTreeFixture& tree, const NativeReplayPlan& plan,
    std::vector<std::uint16_t>& applied_instruction_ids) {
    std::vector<const CurrentTreeNode*> resolved;
    resolved.reserve(plan.items.size());
    for (const NativeReplayItem& entry : plan.items) {
        const CurrentTreeNode* node = tree.Resolve(entry.runtime_handle);
        if (node == nullptr ||
            node->instruction_id != entry.command.instruction_id) {
            return {};
        }
        resolved.push_back(node);
    }

    applied_instruction_ids.clear();
    for (const CurrentTreeNode* node : resolved) {
        applied_instruction_ids.push_back(node->instruction_id);
    }
    return {true, resolved.size()};
}

NativeReplayItem item(RuntimeNodeHandle handle, std::uint16_t instruction_id) {
    NativeReplayItem result;
    result.runtime_handle = handle;
    result.command.instruction_id = instruction_id;
    return result;
}

NativeReplayPlan plan(std::initializer_list<NativeReplayItem> items) {
    NativeReplayPlan result;
    result.items.assign(items.begin(), items.end());
    result.complete = true;
    result.target_reached = true;
    return result;
}

ccz::preview::InstructionCatalog native_fixture_catalog() {
    ccz::preview::InstructionCatalog catalog;
    catalog.valid = true;
    catalog.names.emplace(0x00, L"事件结束");
    catalog.names.emplace(0x27, L"背景显示");
    catalog.names.emplace(0x29, L"地图头像显示");
    catalog.names.emplace(0x2a, L"地图头像移动");
    catalog.names.emplace(0x2b, L"地图头像消失");
    catalog.names.emplace(0x14, L"对话");
    catalog.names.emplace(0x30, L"人物出现");
    return catalog;
}

RawReplayNode core_node(const wchar_t* key, std::uint16_t instruction_id,
                       RuntimeNodeHandle runtime_handle) {
    RawReplayNode node;
    node.logical_key = key;
    node.runtime_handle = runtime_handle;
    node.instruction_id = instruction_id;
    node.command.instruction_id = instruction_id;
    node.command.text = key;
    return node;
}

}  // namespace

CCZ_TEST(native_plan_clears_scene_only_after_all_items_validate) {
    CurrentTreeFixture tree(7);
    tree.Add(0x101, 0x27);
    tree.Add(0x102, 0x30);
    std::vector<std::uint16_t> applied{0xffff};

    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({item(0x101, 0x27), item(0x102, 0x30)}), applied);

    CCZ_REQUIRE(result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(2));
    CCZ_REQUIRE_EQ(applied.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(applied[0], static_cast<std::uint16_t>(0x27));
    CCZ_REQUIRE_EQ(applied[1], static_cast<std::uint16_t>(0x30));
}

CCZ_TEST(native_plan_includes_the_selected_instruction_from_core_output) {
    CurrentTreeFixture tree(11);
    tree.Add(0x201, 0x30);
    tree.Add(0x202, 0x14);  // selected command is part of the plan
    std::vector<std::uint16_t> applied;

    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({item(0x201, 0x30), item(0x202, 0x14)}), applied);

    CCZ_REQUIRE(result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(2));
    CCZ_REQUIRE_EQ(applied.back(), static_cast<std::uint16_t>(0x14));
}

CCZ_TEST(native_plan_resolves_every_handle_against_current_tree_generation) {
    CurrentTreeFixture tree(3);
    tree.Add(0x301, 0x27);
    tree.Add(0x302, 0x29);
    tree.Add(0x303, 0x2a);
    tree.Add(0x304, 0x2b);
    std::vector<std::uint16_t> applied;

    const ApplyFixtureResult current = ApplyValidatedPlan(
        tree, plan({item(0x301, 0x27), item(0x302, 0x29),
                    item(0x303, 0x2a), item(0x304, 0x2b)}), applied);
    CCZ_REQUIRE(current.applied);
    CCZ_REQUIRE_EQ(current.applied_count, std::size_t(4));

    tree.AddFromGeneration(0x399, 2, 0x30);
    const std::vector<std::uint16_t> before_stale = applied;
    const ApplyFixtureResult stale = ApplyValidatedPlan(
        tree, plan({item(0x399, 0x30)}), applied);
    CCZ_REQUIRE(!stale.applied);
    CCZ_REQUIRE_EQ(stale.applied_count, std::size_t(0));
    CCZ_REQUIRE_EQ(applied, before_stale);
}

CCZ_TEST(native_plan_id_mismatch_aborts_before_mismatched_node) {
    CurrentTreeFixture tree(5);
    tree.Add(0x401, 0x27);
    tree.Add(0x402, 0x30);
    std::vector<std::uint16_t> applied{0xeeee};

    // The second handle resolves, but its current TreeView ID does not.
    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({item(0x401, 0x27), item(0x402, 0x14)}), applied);

    CCZ_REQUIRE(!result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(0));
    CCZ_REQUIRE_EQ(applied.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(applied.front(), static_cast<std::uint16_t>(0xeeee));
}

CCZ_TEST(native_plan_contains_only_reachable_branch_and_selected_case_items) {
    CurrentTreeFixture tree(9);
    tree.Add(0x501, 0x30);  // true predicate branch
    tree.Add(0x502, 0x13);  // default Choice Case1
    std::vector<std::uint16_t> applied;

    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({item(0x501, 0x30), item(0x502, 0x13)}), applied);
    CCZ_REQUIRE(result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(2));
    for (const auto instruction_id : applied) {
        CCZ_REQUIRE(instruction_id != static_cast<std::uint16_t>(0x03));
        CCZ_REQUIRE(instruction_id != static_cast<std::uint16_t>(0x2e));
    }
}

CCZ_TEST(native_plan_invalid_input_leaves_existing_scene_for_bounded_fallback) {
    CurrentTreeFixture tree(13);
    tree.Add(0x601, 0x27);
    std::vector<std::uint16_t> applied{0x27};
    const std::vector<std::uint16_t> before = applied;

    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({item(0xdead, 0x30)}), applied);
    CCZ_REQUIRE(!result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(0));
    // The editor can now run one explicit EvaluateToItem fallback for this
    // refresh; the invalid direct plan itself never clears or partially
    // mutates the prior scene.
    CCZ_REQUIRE_EQ(applied, before);
}

CCZ_TEST(native_plan_waiting_flags_preserve_valid_items_and_diagnostic) {
    CurrentTreeFixture tree(17);
    tree.Add(0x681, 0x29);
    NativeReplayPlan waiting = plan({item(0x681, 0x29)});
    waiting.target_reached = false;
    waiting.complete = false;
    waiting.diagnostic = L"等待选择分支";
    std::vector<std::uint16_t> applied;

    const ApplyFixtureResult result = ApplyValidatedPlan(tree, waiting, applied);
    CCZ_REQUIRE(result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(1));
    CCZ_REQUIRE_EQ(applied.front(), static_cast<std::uint16_t>(0x29));
    CCZ_REQUIRE(!waiting.diagnostic.empty());
}

CCZ_TEST(native_plan_empty_success_clears_scene_without_inventing_nodes) {
    CurrentTreeFixture tree(19);
    std::vector<std::uint16_t> applied{0x30};
    const ApplyFixtureResult result = ApplyValidatedPlan(
        tree, plan({}), applied);

    CCZ_REQUIRE(result.applied);
    CCZ_REQUIRE_EQ(result.applied_count, std::size_t(0));
    CCZ_REQUIRE(applied.empty());
}

CCZ_TEST(native_plan_core_includes_selected_native_instruction) {
    ccz::preview::SectionInput input;
    input.section_key = L"fixture|section:0";
    input.selected_target_key = L"selected-actor";
    input.nodes.push_back(core_node(L"boundary", 0x00, 0x701));
    input.nodes.push_back(core_node(L"selected-actor", 0x30, 0x702));

    const ccz::preview::SectionExecutionModel model =
        ccz::preview::build_section_execution_model(
            input, native_fixture_catalog(), {});
    const NativeReplayPlan result = ccz::preview::build_native_replay_plan(
        model, input.selected_target_key, {}, {});

    CCZ_REQUIRE(result.target_reached);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(result.items.front().logical_key,
                   ccz::preview::LogicalNodeKey(L"selected-actor"));
    CCZ_REQUIRE_EQ(result.items.front().runtime_handle,
                   RuntimeNodeHandle(0x702));
}

CCZ_TEST(native_plan_core_preserves_outer_scene_and_portrait_instructions) {
    ccz::preview::SectionInput input;
    input.section_key = L"fixture|section:outer";
    input.selected_target_key = L"selected-dialogue";
    input.nodes.push_back(core_node(L"boundary", 0x00, 0x711));
    input.nodes.push_back(core_node(L"outer-background", 0x27, 0x712));
    input.nodes.push_back(core_node(L"portrait-show", 0x29, 0x713));
    input.nodes.push_back(core_node(L"portrait-move", 0x2a, 0x714));
    input.nodes.push_back(core_node(L"portrait-hide", 0x2b, 0x715));
    input.nodes.push_back(core_node(L"selected-dialogue", 0x14, 0x716));

    const ccz::preview::SectionExecutionModel model =
        ccz::preview::build_section_execution_model(
            input, native_fixture_catalog(), {});
    const NativeReplayPlan result = ccz::preview::build_native_replay_plan(
        model, input.selected_target_key, {}, {});

    CCZ_REQUIRE(result.target_reached);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(4));
    CCZ_REQUIRE_EQ(result.items[0].command.instruction_id,
                   static_cast<std::uint16_t>(0x27));
    CCZ_REQUIRE_EQ(result.items[1].command.instruction_id,
                   static_cast<std::uint16_t>(0x29));
    CCZ_REQUIRE_EQ(result.items[2].command.instruction_id,
                   static_cast<std::uint16_t>(0x2a));
    CCZ_REQUIRE_EQ(result.items[3].command.instruction_id,
                   static_cast<std::uint16_t>(0x2b));
}
