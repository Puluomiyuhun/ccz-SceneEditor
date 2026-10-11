#pragma once

#include <ccz/preview/dialogue_profile.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

struct DialogueCommandView {
    std::uint16_t instruction_id{};
    std::vector<std::int32_t> int_data;
    std::wstring text;
};

struct DialogueState {
    bool visible{};
    DialogueKind kind{DialogueKind::Diagnostic};
    std::uint16_t source_instruction_id{};
    std::vector<PersonReference> person_references;
    std::wstring speaker_label;
    std::wstring body;
    std::uint16_t face_state{};
    bool face_state_known{};
    std::wstring diagnostic;
    bool half_body{};
    unsigned half_body_mask{};
    std::vector<std::wstring> half_body_people;
};

struct DialogueSegment {
    std::wstring speaker_label;
    std::wstring body;
    std::wstring diagnostic;
};

PersonReference decode_per2_code(std::int32_t code);

std::wstring normalize_dialogue_text(const std::wstring& text);

std::vector<DialogueSegment> split_dialogue_segments(
    const std::wstring& text);

std::vector<DialogueState> decode_dialogue_segments(
    const DialogueCommandView& command, const DialogueProfile& profile);

DialogueState decode_dialogue_command(
    const DialogueCommandView& command, const DialogueProfile& profile);

}  // namespace ccz::preview

