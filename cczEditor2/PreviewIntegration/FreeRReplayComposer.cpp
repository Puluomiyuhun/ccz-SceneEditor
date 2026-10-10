#include "FreeRReplayComposer.h"

#include <utility>

namespace ccz::editor_preview {
namespace {

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (message.empty() || destination.find(message) != std::wstring::npos) {
        return;
    }
    if (!destination.empty()) {
        destination += L"；";
    }
    destination += message;
}

}  // namespace

ccz::preview::NativeReplayPlan ComposeFreeRReplayPlan(
    const ccz::preview::NativeReplayPlan& appearance_prefix,
    const ccz::preview::NativeReplayPlan& trigger_plan) {
    return ComposeFreeRReplayPlan(appearance_prefix, {}, trigger_plan);
}

ccz::preview::NativeReplayPlan ComposeFreeRReplayPlan(
    const ccz::preview::NativeReplayPlan& appearance_prefix,
    const std::vector<ccz::preview::NativeReplayPlan>& trigger_prefixes,
    const ccz::preview::NativeReplayPlan& trigger_plan) {
    using ccz::preview::NativeReplayPlan;
    NativeReplayPlan result = trigger_plan;
    if (!trigger_plan.complete) {
        return result;
    }
    if (!appearance_prefix.complete) {
        result.items.clear();
        result.complete = false;
        result.target_reached = false;
        append_diagnostic(result.diagnostic,
                          appearance_prefix.diagnostic.empty()
                              ? L"自由R前置场景推演失败"
                              : appearance_prefix.diagnostic);
        return result;
    }

    for (const NativeReplayPlan& prefix : trigger_prefixes) {
        if (prefix.complete) {
            continue;
        }
        result.items.clear();
        result.complete = false;
        result.target_reached = false;
        append_diagnostic(result.diagnostic,
                          prefix.diagnostic.empty()
                              ? L"自由R前置 Section 推演失败"
                              : prefix.diagnostic);
        return result;
    }

    result.items.clear();
    std::vector<ccz::preview::NativeReplayItem> trigger_items;
    for (const NativeReplayPlan& prefix : trigger_prefixes) {
        trigger_items.insert(trigger_items.end(), prefix.items.begin(),
                             prefix.items.end());
    }
    trigger_items.insert(trigger_items.end(), trigger_plan.items.begin(),
                         trigger_plan.items.end());

    auto reset = trigger_items.end();
    for (auto it = trigger_items.begin(); it != trigger_items.end(); ++it) {
        if (it->command.instruction_id == 0x27) {
            reset = it;
        }
    }
    if (reset == trigger_items.end()) {
        result.items = appearance_prefix.items;
        result.items.insert(result.items.end(), trigger_items.begin(),
                            trigger_items.end());
    } else {
        // A new background resets the inherited scene.  Keep the background
        // itself and everything after it, including later Section plans.
        result.items.insert(result.items.end(), reset, trigger_items.end());
    }
    append_diagnostic(result.diagnostic, appearance_prefix.diagnostic);
    for (const NativeReplayPlan& prefix : trigger_prefixes) {
        append_diagnostic(result.diagnostic, prefix.diagnostic);
    }
    return result;
}

}  // namespace ccz::editor_preview
