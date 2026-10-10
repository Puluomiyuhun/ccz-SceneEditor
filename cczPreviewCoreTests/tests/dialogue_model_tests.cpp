#include "test_framework.h"

#include <ccz/preview/dialogue_model.h>
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

CCZ_TEST(dialogue_model_extracts_speaker_prefix_and_newlines) {
    const DialogueCommandView command{20, {}, L"&曹操\\n你先留下。\\n第二句"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE(state.visible);
    CCZ_REQUIRE_EQ(state.kind, DialogueKind::Standard);
    CCZ_REQUIRE_EQ(state.speaker_label, std::wstring(L"曹操"));
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"你先留下。\n第二句"));
}

CCZ_TEST(dialogue_model_preserves_actual_line_breaks) {
    const DialogueCommandView command{20, {}, L"&曹操\n实际换行"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE_EQ(state.speaker_label, std::wstring(L"曹操"));
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"实际换行"));
}

CCZ_TEST(dialogue_model_decodes_data_number_speaker_prefix) {
    const DialogueCommandView command{20, {}, L"&*.398\\n指定人物台词"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE_EQ(state.speaker_label, std::wstring(L"*.398"));
    CCZ_REQUIRE_EQ(state.person_references.size(), std::size_t(1));
    CCZ_REQUIRE_EQ(state.person_references.front().kind,
                   PersonReferenceKind::DataPerson);
    CCZ_REQUIRE_EQ(state.person_references.front().value, 398);
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"指定人物台词"));
}

CCZ_TEST(dialogue_model_splits_multiple_0x14_speakers) {
    const DialogueCommandView command{
        20, {}, L"&陆坤\n哎。\n&*.174\n云筱，久闻大名。"};
    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, make_profile());

    CCZ_REQUIRE_EQ(segments.size(), std::size_t{2});
    CCZ_REQUIRE_EQ(segments[0].speaker_label, std::wstring(L"陆坤"));
    CCZ_REQUIRE_EQ(segments[0].body, std::wstring(L"哎。"));
    CCZ_REQUIRE_EQ(segments[1].speaker_label, std::wstring(L"*.174"));
    CCZ_REQUIRE_EQ(segments[1].body, std::wstring(L"云筱，久闻大名。"));
    CCZ_REQUIRE_EQ(segments[1].person_references.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(segments[1].person_references.front().value, 174);
}

CCZ_TEST(dialogue_model_does_not_split_plain_body_newlines) {
    const DialogueCommandView command{
        20, {}, L"&陆坤\n第一行\n第二行"};
    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, make_profile());

    CCZ_REQUIRE_EQ(segments.size(), std::size_t{1});
    CCZ_REQUIRE_EQ(segments.front().speaker_label, std::wstring(L"陆坤"));
    CCZ_REQUIRE_EQ(segments.front().body,
                   std::wstring(L"第一行\n第二行"));
}

CCZ_TEST(dialogue_model_falls_back_for_malformed_multi_dialogue_marker) {
    const DialogueCommandView command{20, {}, L"&陆坤\n第一句\n&云筱"};
    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, make_profile());

    CCZ_REQUIRE_EQ(segments.size(), std::size_t{1});
    CCZ_REQUIRE(!segments.front().diagnostic.empty());
    CCZ_REQUIRE_EQ(segments.front().body,
                   std::wstring(L"第一句\n&云筱"));
}

CCZ_TEST(dialogue3_rejects_legacy_single_speaker_prefix) {
    DialogueProfile profile = make_profile();
    profile.dialogue_ids.push_back(
        {122, DialogueKind::Extended, true, {},
         PersonFieldEncoding::SpeakerPrefixLabel, true});
    const DialogueCommandView command{
        122, {}, L"&云夜\n第一段\n&第二段标记"};

    const std::vector<DialogueState> segments =
        decode_dialogue_segments(command, profile);

    CCZ_REQUIRE_EQ(segments.size(), std::size_t{1});
    CCZ_REQUIRE(!segments.front().visible);
    CCZ_REQUIRE(!segments.front().diagnostic.empty());
}

CCZ_TEST(dialogue_model_decodes_explicit_per2_people) {
    const DialogueCommandView command{21, {12, 1028}, L"正文"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE(state.visible);
    CCZ_REQUIRE_EQ(state.person_references.size(), std::size_t(2));
    CCZ_REQUIRE_EQ(state.person_references[0].kind, PersonReferenceKind::DataPerson);
    CCZ_REQUIRE_EQ(state.person_references[0].value, 12);
    CCZ_REQUIRE_EQ(state.person_references[1].kind, PersonReferenceKind::BattleUnit);
    CCZ_REQUIRE_EQ(state.person_references[1].value, 0);
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"正文"));
}

CCZ_TEST(dialogue_model_reports_short_explicit_person_fields) {
    const DialogueCommandView command{21, {12}, L"正文"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE(state.visible);
    CCZ_REQUIRE_EQ(state.person_references.size(), std::size_t(1));
    CCZ_REQUIRE(!state.diagnostic.empty());
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"正文"));
}

CCZ_TEST(dialogue_model_handles_solo_flags_without_fake_person) {
    const DialogueCommandView command{99, {1, 0}, L"单挑台词"};
    const DialogueState state = decode_dialogue_command(command, make_profile());

    CCZ_REQUIRE(state.visible);
    CCZ_REQUIRE_EQ(state.kind, DialogueKind::Solo);
    CCZ_REQUIRE(state.person_references.empty());
    CCZ_REQUIRE_EQ(state.body, std::wstring(L"单挑台词"));
}

CCZ_TEST(dialogue_model_maps_per2_boundaries) {
    const PersonReference data_zero = decode_per2_code(0);
    const PersonReference data_last = decode_per2_code(1023);
    const PersonReference special = decode_per2_code(1024);
    const PersonReference battle_first = decode_per2_code(1028);
    const PersonReference battle_last = decode_per2_code(1277);
    const PersonReference variable_first = decode_per2_code(-2);
    const PersonReference variable_last = decode_per2_code(-4097);
    const PersonReference none = decode_per2_code(-1);

    CCZ_REQUIRE_EQ(data_zero.kind, PersonReferenceKind::DataPerson);
    CCZ_REQUIRE_EQ(data_zero.value, 0);
    CCZ_REQUIRE_EQ(data_last.value, 1023);
    CCZ_REQUIRE_EQ(special.kind, PersonReferenceKind::SpecialSelector);
    CCZ_REQUIRE_EQ(special.value, 0);
    CCZ_REQUIRE_EQ(battle_first.value, 0);
    CCZ_REQUIRE_EQ(battle_last.value, 249);
    CCZ_REQUIRE_EQ(variable_first.kind, PersonReferenceKind::Variable);
    CCZ_REQUIRE_EQ(variable_first.value, 0);
    CCZ_REQUIRE_EQ(variable_last.value, 4095);
    CCZ_REQUIRE_EQ(none.kind, PersonReferenceKind::None);
}

CCZ_TEST(dialogue_model_rejects_invalid_per2_code) {
    const PersonReference invalid_high = decode_per2_code(1278);
    const PersonReference invalid_low = decode_per2_code(-4098);
    CCZ_REQUIRE_EQ(invalid_high.kind, PersonReferenceKind::Invalid);
    CCZ_REQUIRE_EQ(invalid_low.kind, PersonReferenceKind::Invalid);
}

CCZ_TEST(dialogue3_three_slots_abbreviated_masks_and_terminator) {
    DialogueCommandView command;command.instruction_id=0x7a;
    command.text=L"&4,0,1,2,3\n左\n&2&&\n中\n&1&&\n右\n&7&&\n全部\n&5,0,4,-1,5\n两人\n&&";
    DialogueProfile profile;
    auto states=decode_dialogue_segments(command,profile);
    CCZ_REQUIRE_EQ(states.size(),static_cast<std::size_t>(5));
    CCZ_REQUIRE(states[0].half_body && states[0].half_body_mask==4);
    CCZ_REQUIRE(states[1].half_body_people==states[0].half_body_people);
    CCZ_REQUIRE(states[1].speaker_label==L"2");
    CCZ_REQUIRE(states[2].half_body_mask==1 && states[3].half_body_mask==7);
    CCZ_REQUIRE(states[4].half_body_people[1]==L"-1");
    CCZ_REQUIRE(states[4].body.find(L"&&")==std::wstring::npos);
    for(int mask=0;mask<8;++mask) {command.text=L"&"+std::to_wstring(mask)+L",0,1,2,3\n正文";auto decoded=decode_dialogue_segments(command,profile);CCZ_REQUIRE(decoded[0].half_body_mask==mask);}
}
