#include <ccz/preview/dialogue_model.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ccz::preview {
namespace {

void append_diagnostic(DialogueState& state, const std::wstring& message) {
    if (!state.diagnostic.empty()) {
        state.diagnostic += L"; ";
    }
    state.diagnostic += message;
}

std::optional<std::int32_t> parse_data_speaker_token(
    const std::wstring& speaker) {
    if (speaker.size() < 3 || speaker[0] != L'*' || speaker[1] != L'.') {
        return std::nullopt;
    }

    std::uint32_t value = 0;
    for (std::size_t index = 2; index < speaker.size(); ++index) {
        const wchar_t character = speaker[index];
        if (character < L'0' || character > L'9') {
            return std::nullopt;
        }
        value = value * 10u + static_cast<std::uint32_t>(character - L'0');
        if (value > 1023u) {
            return std::nullopt;
        }
    }
    return static_cast<std::int32_t>(value);
}

std::size_t find_marker(const std::wstring& normalized,
                        std::size_t start) {
    std::size_t position = normalized.find(L'&', start);
    while (position != std::wstring::npos) {
        if (position == 0 || normalized[position - 1] == L'\n') {
            return position;
        }
        position = normalized.find(L'&', position + 1);
    }
    return std::wstring::npos;
}

DialogueSegment split_single_speaker_prefix(const std::wstring& text) {
    const std::wstring normalized = normalize_dialogue_text(text);
    if (normalized.empty() || normalized.front() != L'&') {
        return {L"", normalized, L""};
    }
    const std::size_t separator = normalized.find(L'\n', 1);
    if (separator == std::wstring::npos) {
        return {L"", normalized, L"说话人标记缺少换行分隔符"};
    }
    return {normalized.substr(1, separator - 1),
            normalized.substr(separator + 1), L""};
}

void append_speaker_reference(const std::wstring& speaker,
                              DialogueState& state) {
    const std::optional<std::int32_t> data_person =
        parse_data_speaker_token(speaker);
    if (!data_person.has_value()) return;

    PersonReference reference;
    reference.raw_code = *data_person;
    reference.kind = PersonReferenceKind::DataPerson;
    reference.value = *data_person;
    state.person_references.push_back(reference);
}

void append_person_fields(const DialogueCommandView& command,
                          const DialogueInstructionSpec& spec,
                          DialogueState& state) {
    for (const std::uint8_t field_index : spec.person_fields) {
        if (field_index >= command.int_data.size()) {
            append_diagnostic(state, L"对话人物字段数量不足");
            continue;
        }

        const PersonReference reference = decode_per2_code(command.int_data[field_index]);
        state.person_references.push_back(reference);
        if (reference.kind == PersonReferenceKind::Invalid) {
            append_diagnostic(state, L"对话人物字段包含无效 per2 编码");
        }
    }
}

DialogueState decode_segment(const DialogueCommandView& command,
                             const DialogueInstructionSpec& spec,
                             const DialogueSegment& segment) {
    DialogueState state;
    state.visible = true;
    state.kind = spec.kind;
    state.source_instruction_id = command.instruction_id;
    state.speaker_label = segment.speaker_label;
    state.body = segment.body;
    state.diagnostic = segment.diagnostic;
    if (spec.parse_speaker_prefix) {
        append_speaker_reference(state.speaker_label, state);
    }
    append_person_fields(command, spec, state);
    return state;
}

}  // namespace

PersonReference decode_per2_code(std::int32_t code) {
    PersonReference result;
    result.raw_code = code;

    if (code >= 0 && code <= 1023) {
        result.kind = PersonReferenceKind::DataPerson;
        result.value = code;
    } else if (code >= 1024 && code <= 1027) {
        result.kind = PersonReferenceKind::SpecialSelector;
        result.value = code - 1024;
    } else if (code >= 1028 && code <= 1277) {
        result.kind = PersonReferenceKind::BattleUnit;
        result.value = code - 1028;
    } else if (code >= -4097 && code <= -2) {
        result.kind = PersonReferenceKind::Variable;
        result.value = -code - 2;
    } else if (code == -1) {
        result.kind = PersonReferenceKind::None;
        result.value = 0;
    } else {
        result.kind = PersonReferenceKind::Invalid;
        result.value = 0;
    }
    return result;
}

std::wstring normalize_dialogue_text(const std::wstring& text) {
    std::wstring normalized;
    normalized.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t character = text[index];
        if (character == L'\\' && index + 1 < text.size() && text[index + 1] == L'n') {
            normalized.push_back(L'\n');
            ++index;
        } else if (character == L'\r') {
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(character);
        }
    }
    return normalized;
}

std::vector<DialogueSegment> split_dialogue_segments(
    const std::wstring& text) {
    const std::wstring normalized = normalize_dialogue_text(text);
    if (normalized.empty() || normalized.front() != L'&') {
        return {{L"", normalized, L""}};
    }

    const std::size_t first_separator = normalized.find(L'\n', 1);
    if (first_separator == std::wstring::npos || first_separator == 1) {
        return {{L"", normalized, L"说话人标记缺少有效换行分隔符"}};
    }

    const std::wstring first_speaker =
        normalized.substr(1, first_separator - 1);
    const std::size_t body_begin = first_separator + 1;
    std::vector<DialogueSegment> result;
    std::size_t marker = 0;
    while (marker != std::wstring::npos) {
        const std::size_t separator = normalized.find(L'\n', marker + 1);
        if (separator == std::wstring::npos || separator == marker + 1) {
            return {{first_speaker, normalized.substr(body_begin),
                     L"说话人标记缺少有效换行分隔符"}};
        }

        const std::size_t segment_body_begin = separator + 1;
        const std::size_t next_marker =
            find_marker(normalized, segment_body_begin);
        std::size_t segment_body_end =
            next_marker == std::wstring::npos ? normalized.size() : next_marker;
        // The newline immediately before the next `&speaker` line is the
        // marker's line delimiter, not part of the previous speaker's body.
        if (next_marker != std::wstring::npos && segment_body_end > segment_body_begin &&
            normalized[segment_body_end - 1] == L'\n') {
            --segment_body_end;
        }
        result.push_back({normalized.substr(marker + 1, separator - marker - 1),
                          normalized.substr(segment_body_begin,
                                            segment_body_end - segment_body_begin),
                          L""});
        marker = next_marker;
    }
    return result;
}

std::vector<DialogueState> decode_dialogue_segments(
    const DialogueCommandView& command, const DialogueProfile& profile) {
    const DialogueInstructionSpec* spec =
        find_dialogue_spec(profile, command.instruction_id);
    if (spec == nullptr) {
        DialogueState state;
        state.source_instruction_id = command.instruction_id;
        state.kind = DialogueKind::Diagnostic;
        state.diagnostic = L"未注册的对话指令";
        return {std::move(state)};
    }

    std::vector<DialogueSegment> segments;
    if (spec->parse_speaker_prefix && command.instruction_id == 0x14) {
        segments = split_dialogue_segments(command.text);
    } else if (spec->parse_speaker_prefix) {
        segments = {split_single_speaker_prefix(command.text)};
    } else {
        segments = {{L"", normalize_dialogue_text(command.text), L""}};
    }

    std::vector<DialogueState> result;
    result.reserve(segments.size());
    for (const DialogueSegment& segment : segments) {
        result.push_back(decode_segment(command, *spec, segment));
    }
    return result;
}

DialogueState decode_dialogue_command(
    const DialogueCommandView& command, const DialogueProfile& profile) {
    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, profile);
    if (!segments.empty()) return segments.front();

    DialogueState state;
    state.source_instruction_id = command.instruction_id;
    state.kind = DialogueKind::Diagnostic;
    state.diagnostic = L"对话文本未生成有效段落";
    return state;
}

}  // namespace ccz::preview
