#include <ccz/preview/dialogue_profile.h>

#include <cctype>
#include <limits>
#include <unordered_set>

namespace ccz::preview {
namespace {

bool is_sha256(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (const unsigned char character : value) {
        if (std::isxdigit(character) == 0) {
            return false;
        }
    }
    return true;
}

bool valid_offset_width(std::uint32_t offset, std::uint32_t width) {
    return width != 0 && offset <= std::numeric_limits<std::uint32_t>::max() - width;
}

}  // namespace

bool validate_dialogue_profile(const DialogueProfile& profile) {
    if (profile.architecture != "x86" || !is_sha256(profile.exe_sha256)) {
        return false;
    }
    if (profile.data_record_stride == 0 || profile.data_byte_order != "little_endian") {
        return false;
    }
    if (!valid_offset_width(profile.avatar_offset, profile.avatar_width) ||
        !valid_offset_width(profile.r_image_offset, profile.r_image_width)) {
        return false;
    }
    if (profile.avatar_width != 2 || profile.r_image_width != 2) {
        return false;
    }
    if (profile.face_index_table_offset == 0 || profile.face_entry_size == 0 ||
        profile.face_index_base != 0) {
        return false;
    }

    std::unordered_set<std::uint16_t> seen_ids;
    for (const DialogueInstructionSpec& entry : profile.dialogue_ids) {
        if (!seen_ids.insert(entry.instruction_id).second) {
            return false;
        }
        // Narration-like commands (信息、旁白、地图文字显示等) are valid
        // dialogue entries without a speaker or portrait.  They must not be
        // rejected merely because the profile deliberately marks them plain.
        if (!entry.portrait_required &&
            (entry.parse_speaker_prefix ||
             entry.person_field_encoding != PersonFieldEncoding::None ||
             !entry.person_fields.empty())) {
            return false;
        }
        if (entry.person_field_encoding == PersonFieldEncoding::Per2Code &&
            entry.person_fields.empty()) {
            return false;
        }
        if (entry.person_field_encoding != PersonFieldEncoding::Per2Code &&
            !entry.person_fields.empty()) {
            return false;
        }
    }

    std::unordered_set<std::uint64_t> seen_mappings;
    for (const SpecialFaceMapping& mapping : profile.special_state_map) {
        if (mapping.person_id < 0) {
            return false;
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(mapping.person_id)) << 16) |
            mapping.face_state;
        if (!seen_mappings.insert(key).second) {
            return false;
        }
    }
    return true;
}

const DialogueInstructionSpec* find_dialogue_spec(
    const DialogueProfile& profile, std::uint16_t instruction_id) {
    for (const DialogueInstructionSpec& entry : profile.dialogue_ids) {
        if (entry.instruction_id == instruction_id) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace ccz::preview

