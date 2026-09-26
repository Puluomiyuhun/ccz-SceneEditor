#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccz::preview {

enum class DialogueKind : std::uint8_t {
    Standard,
    Alternate,
    Solo,
    Narration,
    Extended,
    Diagnostic,
};

enum class PersonFieldEncoding : std::uint8_t {
    None,
    Per2Code,
    SpeakerPrefixLabel,
};

enum class PersonReferenceKind : std::uint8_t {
    DataPerson,
    SpecialSelector,
    BattleUnit,
    Variable,
    None,
    Invalid,
};

struct PersonReference {
    std::int32_t raw_code{};
    PersonReferenceKind kind{PersonReferenceKind::Invalid};
    std::int32_t value{};
};

struct DialogueInstructionSpec {
    std::uint16_t instruction_id{};
    DialogueKind kind{DialogueKind::Diagnostic};
    bool portrait_required{};
    std::vector<std::uint8_t> person_fields;
    PersonFieldEncoding person_field_encoding{PersonFieldEncoding::None};
    bool parse_speaker_prefix{};
};

struct SpecialFaceMapping {
    std::int32_t person_id{};
    std::uint16_t face_state{};
    std::uint32_t zero_based_face_index{};
};

struct DialogueProfile {
    std::string architecture{"x86"};
    std::string exe_sha256;
    std::vector<DialogueInstructionSpec> dialogue_ids;
    std::uint16_t face_state_instruction_id{32};

    std::uint32_t data_record_base{};
    std::uint32_t data_record_stride{};
    std::uint32_t avatar_offset{};
    std::uint32_t avatar_width{};
    std::uint32_t r_image_offset{};
    std::uint32_t r_image_width{};
    std::string data_byte_order{"little_endian"};

    std::uint32_t face_index_table_offset{0x110};
    std::uint32_t face_entry_size{12};
    std::uint32_t face_index_base{};
    std::optional<std::int32_t> face_index_bias;
    std::vector<SpecialFaceMapping> special_state_map;
};

bool validate_dialogue_profile(const DialogueProfile& profile);

const DialogueInstructionSpec* find_dialogue_spec(
    const DialogueProfile& profile, std::uint16_t instruction_id);

}  // namespace ccz::preview


