#include "test_framework.h"

#include <ccz/preview/dialogue_profile.h>

#include <string>

using namespace ccz::preview;

namespace {

DialogueProfile make_profile() {
    DialogueProfile profile;
    profile.architecture = "x86";
    profile.exe_sha256 = "781A3833B6031D216392591FCE5D5F43D899BAA5B3ABC148A999EC6D2B0B45C8";
    profile.data_record_base = 0x18C;
    profile.data_record_stride = 0x20;
    profile.avatar_offset = 0x0D;
    profile.avatar_width = 2;
    profile.r_image_offset = 0x0F;
    profile.r_image_width = 2;
    profile.face_index_base = 0;
    profile.face_index_bias = 7;

    profile.dialogue_ids = {
        {20, DialogueKind::Standard, true, {}, PersonFieldEncoding::SpeakerPrefixLabel, true},
        {21, DialogueKind::Alternate, true, {0, 1}, PersonFieldEncoding::Per2Code, false},
        {99, DialogueKind::Solo, true, {}, PersonFieldEncoding::None, false},
    };
    return profile;
}

}  // namespace

CCZ_TEST(dialogue_profile_accepts_v024_shape) {
    const DialogueProfile profile = make_profile();
    CCZ_REQUIRE(validate_dialogue_profile(profile));
    CCZ_REQUIRE(find_dialogue_spec(profile, 20) != nullptr);
    CCZ_REQUIRE(find_dialogue_spec(profile, 21) != nullptr);
    CCZ_REQUIRE(find_dialogue_spec(profile, 99) != nullptr);
    CCZ_REQUIRE(find_dialogue_spec(profile, 105) == nullptr);
}

CCZ_TEST(dialogue_profile_rejects_duplicate_instruction_ids) {
    DialogueProfile profile = make_profile();
    profile.dialogue_ids.push_back(profile.dialogue_ids.front());
    CCZ_REQUIRE(!validate_dialogue_profile(profile));
}

CCZ_TEST(dialogue_profile_rejects_wrong_avatar_width) {
    DialogueProfile profile = make_profile();
    profile.avatar_width = 1;
    CCZ_REQUIRE(!validate_dialogue_profile(profile));
}

CCZ_TEST(dialogue_profile_accepts_plain_narration_specs) {
    DialogueProfile profile = make_profile();
    profile.dialogue_ids.push_back(
        {0x16, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    profile.dialogue_ids.push_back(
        {0x2c, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    profile.dialogue_ids.push_back(
        {0x69, DialogueKind::Narration, false, {},
         PersonFieldEncoding::None, false});
    CCZ_REQUIRE(validate_dialogue_profile(profile));
}

CCZ_TEST(dialogue_profile_rejects_plain_spec_with_person_metadata) {
    DialogueProfile profile = make_profile();
    profile.dialogue_ids.push_back(
        {0x16, DialogueKind::Narration, false, {0},
         PersonFieldEncoding::Per2Code, false});
    CCZ_REQUIRE(!validate_dialogue_profile(profile));
}

