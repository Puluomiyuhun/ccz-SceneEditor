#pragma once

namespace ccz::editor_preview {

// The native editor classifies 0 (事件结束) as a top-level, non-nesting event
// boundary and 1 (子事件设定) as a nesting marker. Preview writeback must not
// insert a sibling immediately after either structural instruction.
constexpr bool IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(
    int previous_instruction_id) noexcept {
    return previous_instruction_id == 0 || previous_instruction_id == 1;
}

} // namespace ccz::editor_preview
