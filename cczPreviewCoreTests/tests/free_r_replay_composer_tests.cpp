#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/FreeRReplayComposer.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

using ccz::editor_preview::ComposeFreeRReplayPlan;
using ccz::preview::NativeReplayItem;
using ccz::preview::NativeReplayPlan;

namespace {

NativeReplayItem item(const wchar_t* key, std::uint16_t instruction_id) {
    NativeReplayItem result;
    result.logical_key = key;
    result.command.instruction_id = instruction_id;
    return result;
}

NativeReplayPlan complete_plan(std::initializer_list<NativeReplayItem> items) {
    NativeReplayPlan result;
    result.items.assign(items.begin(), items.end());
    result.complete = true;
    result.target_reached = true;
    return result;
}

}  // namespace

CCZ_TEST(free_r_composer_inherits_appearance_without_new_background) {
    const NativeReplayPlan prefix = complete_plan({item(L"appearance", 0x30)});
    const NativeReplayPlan trigger = complete_plan({item(L"dialogue-scene", 0x30)});

    const NativeReplayPlan result = ComposeFreeRReplayPlan(prefix, trigger);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(result.items[0].logical_key, std::wstring(L"appearance"));
    CCZ_REQUIRE_EQ(result.items[1].logical_key,
                   std::wstring(L"dialogue-scene"));
}

CCZ_TEST(free_r_composer_resets_inherited_scene_at_last_background) {
    const NativeReplayPlan prefix = complete_plan({item(L"appearance", 0x30)});
    const NativeReplayPlan trigger = complete_plan({
        item(L"old-native", 0x30), item(L"background", 0x27),
        item(L"new-native", 0x30)});

    const NativeReplayPlan result = ComposeFreeRReplayPlan(prefix, trigger);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(result.items[0].logical_key, std::wstring(L"background"));
    CCZ_REQUIRE_EQ(result.items[1].logical_key, std::wstring(L"new-native"));
}

CCZ_TEST(free_r_composer_replays_prior_trigger_sections_in_order) {
    const NativeReplayPlan prefix = complete_plan({item(L"appearance", 0x30)});
    const std::vector<NativeReplayPlan> prior_sections{
        complete_plan({item(L"prior-turn", 0x33), item(L"prior-move", 0x32)})};
    const NativeReplayPlan current =
        complete_plan({item(L"current-dialogue-scene", 0x30)});

    const NativeReplayPlan result =
        ComposeFreeRReplayPlan(prefix, prior_sections, current);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(4));
    CCZ_REQUIRE_EQ(result.items[0].logical_key, std::wstring(L"appearance"));
    CCZ_REQUIRE_EQ(result.items[1].logical_key, std::wstring(L"prior-turn"));
    CCZ_REQUIRE_EQ(result.items[2].logical_key, std::wstring(L"prior-move"));
    CCZ_REQUIRE_EQ(result.items[3].logical_key,
                   std::wstring(L"current-dialogue-scene"));
}

CCZ_TEST(free_r_composer_drops_appearance_before_background_in_prior_section) {
    const NativeReplayPlan prefix = complete_plan({item(L"appearance", 0x30)});
    const std::vector<NativeReplayPlan> prior_sections{
        complete_plan({item(L"old-native", 0x30), item(L"new-background", 0x27),
                       item(L"new-turn", 0x33)})};
    const NativeReplayPlan current =
        complete_plan({item(L"current-dialogue", 0x30)});

    const NativeReplayPlan result =
        ComposeFreeRReplayPlan(prefix, prior_sections, current);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(result.items[0].logical_key,
                   std::wstring(L"new-background"));
    CCZ_REQUIRE_EQ(result.items[1].logical_key, std::wstring(L"new-turn"));
    CCZ_REQUIRE_EQ(result.items[2].logical_key,
                   std::wstring(L"current-dialogue"));
}

CCZ_TEST(free_r_composer_keeps_initial_section_final_state_commands) {
    // A matched appearance Section may contain 0x30 followed by 0x31.  The
    // prefix must keep both in source order so the first snapshot is the
    // final state (the character is not resurrected after disappearing).
    const NativeReplayPlan prefix = complete_plan({
        item(L"initial-appear", 0x30), item(L"initial-disappear", 0x31)});
    const NativeReplayPlan current =
        complete_plan({item(L"current-dialogue", 0x30)});

    const NativeReplayPlan result = ComposeFreeRReplayPlan(prefix, current);
    CCZ_REQUIRE(result.complete);
    CCZ_REQUIRE_EQ(result.items.size(), std::size_t(3));
    CCZ_REQUIRE_EQ(result.items[0].logical_key,
                   std::wstring(L"initial-appear"));
    CCZ_REQUIRE_EQ(result.items[1].logical_key,
                   std::wstring(L"initial-disappear"));
}
