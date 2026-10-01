#include "test_framework.h"

#include "../../cczEditor2/TextWrapEditSession.h"

#include <string>
#include <string_view>

namespace {

int fake_cp936_byte_count(std::wstring_view text) {
    int result = 0;
    for (wchar_t ch : text) {
        result += ch <= 0x7f ? 1 : 2;
    }
    return result;
}

std::wstring repeated(wchar_t ch, std::size_t count) {
    return std::wstring(count, ch);
}

}  // namespace

CCZ_TEST(text_wrap_edit_session_reflows_its_generated_break_after_midline_insert) {
    using namespace ccz::text_wrap;
    const std::wstring source = repeated(L'甲', 20) + L"乙";
    EditTextState initial = CreateEditTextState(
        source, 38, fake_cp936_byte_count);
    CCZ_REQUIRE_EQ(initial.storage_text,
                  repeated(L'甲', 19) + L"\n甲乙");

    std::wstring edited = initial.edit_text;
    edited.insert(5, L"丙");
    const EditTextUpdate update = ApplyEditTextChange(
        initial, edited, 6, 6, 38, fake_cp936_byte_count);

    CCZ_REQUIRE_EQ(update.state.source_text,
                  repeated(L'甲', 5) + L"丙" + repeated(L'甲', 15) + L"乙");
    CCZ_REQUIRE_EQ(update.state.storage_text,
                  repeated(L'甲', 5) + L"丙" + repeated(L'甲', 13) +
                      L"\n甲甲乙");
    CCZ_REQUIRE_EQ(update.selection_start, std::size_t{6});
    CCZ_REQUIRE_EQ(update.selection_end, std::size_t{6});
}

CCZ_TEST(text_wrap_edit_session_preserves_manual_newlines) {
    using namespace ccz::text_wrap;
    const EditTextState state = CreateEditTextState(
        L"甲\r\n乙", 38, fake_cp936_byte_count);

    CCZ_REQUIRE_EQ(state.source_text, L"甲\n乙");
    CCZ_REQUIRE_EQ(state.storage_text, L"甲\n乙");
    CCZ_REQUIRE_EQ(state.edit_text, L"甲\r\n乙");
}

CCZ_TEST(text_wrap_edit_session_repeated_midline_edits_do_not_split_into_single_character_lines) {
    using namespace ccz::text_wrap;
    EditTextState state = CreateEditTextState(
        repeated(L'甲', 20), 38, fake_cp936_byte_count);

    std::wstring edited = state.edit_text;
    edited.insert(4, L"乙");
    auto update = ApplyEditTextChange(
        state, edited, 5, 5, 38, fake_cp936_byte_count);
    state = update.state;
    CCZ_REQUIRE_EQ(state.source_text,
                  repeated(L'甲', 4) + L"乙" + repeated(L'甲', 16));

    edited = state.edit_text;
    edited.insert(9, L"丙");
    update = ApplyEditTextChange(
        state, edited, 10, 10, 38, fake_cp936_byte_count);
    state = update.state;
    CCZ_REQUIRE_EQ(state.source_text,
                  repeated(L'甲', 4) + L"乙" + repeated(L'甲', 4) +
                      L"丙" + repeated(L'甲', 12));
    CCZ_REQUIRE_EQ(state.storage_text,
                  repeated(L'甲', 4) + L"乙" + repeated(L'甲', 4) +
                      L"丙" + repeated(L'甲', 9) + L"\n" +
                      repeated(L'甲', 3));
}

CCZ_TEST(text_wrap_edit_session_recreates_deleted_generated_break) {
    using namespace ccz::text_wrap;
    EditTextState state = CreateEditTextState(
        repeated(L'甲', 20), 38, fake_cp936_byte_count);
    const std::size_t break_position = state.edit_text.find(L"\r\n");
    CCZ_REQUIRE(break_position != std::wstring::npos);

    std::wstring edited = state.edit_text;
    edited.erase(break_position, 2);
    const auto update = ApplyEditTextChange(
        state, edited, break_position, break_position,
        38, fake_cp936_byte_count);

    CCZ_REQUIRE_EQ(update.state.source_text, repeated(L'甲', 20));
    CCZ_REQUIRE_EQ(update.state.storage_text,
                  repeated(L'甲', 19) + L"\n甲");
}

CCZ_TEST(text_wrap_edit_session_keeps_a_manual_break_distinct_from_generated_breaks) {
    using namespace ccz::text_wrap;
    EditTextState state = CreateEditTextState(
        repeated(L'甲', 19) + L"\n" + repeated(L'乙', 20),
        38, fake_cp936_byte_count);
    std::wstring edited = state.edit_text;
    edited.insert(3, L"丙");
    const auto update = ApplyEditTextChange(
        state, edited, 4, 4, 38, fake_cp936_byte_count);

    CCZ_REQUIRE_EQ(update.state.source_text,
                  repeated(L'甲', 3) + L"丙" + repeated(L'甲', 16) +
                      L"\n" + repeated(L'乙', 20));
    CCZ_REQUIRE(update.state.storage_text.find(L"\n\n") ==
                std::wstring::npos);
    CCZ_REQUIRE(update.state.storage_text.find(L"\n乙") !=
                std::wstring::npos);
}

CCZ_TEST(text_wrap_edit_session_treats_a_user_inserted_newline_as_manual) {
    using namespace ccz::text_wrap;
    EditTextState state = CreateEditTextState(
        repeated(L'甲', 30), 38, fake_cp936_byte_count);
    std::wstring edited = state.edit_text;
    edited.insert(5, L"\r\n");
    const auto update = ApplyEditTextChange(
        state, edited, 7, 7, 38, fake_cp936_byte_count);

    CCZ_REQUIRE_EQ(update.state.source_text,
                  repeated(L'甲', 5) + L"\n" + repeated(L'甲', 25));
    const std::size_t manual_break = update.state.storage_text.find(L'\n');
    const std::size_t generated_break = update.state.storage_text.find(
        L'\n', manual_break + 1);
    CCZ_REQUIRE(manual_break != std::wstring::npos);
    CCZ_REQUIRE(generated_break != std::wstring::npos);
    CCZ_REQUIRE_EQ(manual_break, std::size_t{5});
}
