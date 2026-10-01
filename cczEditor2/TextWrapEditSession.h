#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace ccz::text_wrap {

using ByteCountFunction = int (*)(std::wstring_view text);

struct EditTextState {
    // Text authored by the user, with manual line breaks normalized to LF.
    std::wstring source_text;
    // Text stored in the script after automatic wrapping, using LF.
    std::wstring storage_text;
    // Text currently displayed by the Windows edit control, using CRLF.
    std::wstring edit_text;
    // Maps edit-control caret positions to source-text offsets.
    std::vector<std::size_t> edit_to_source;
    // Maps source-text offsets to edit-control caret positions.
    std::vector<std::size_t> source_to_edit;
};

struct EditTextUpdate {
    EditTextState state;
    std::size_t selection_start{};
    std::size_t selection_end{};
};

EditTextState CreateEditTextState(
    std::wstring_view text, int byte_limit, ByteCountFunction byte_count,
    std::size_t maximum_edit_length =
        (std::numeric_limits<std::size_t>::max)());

EditTextUpdate ApplyEditTextChange(
    const EditTextState& previous, std::wstring_view edit_text,
    std::size_t selection_start, std::size_t selection_end,
    int byte_limit, ByteCountFunction byte_count,
    std::size_t maximum_edit_length =
        (std::numeric_limits<std::size_t>::max)());

EditTextUpdate RewrapEditText(
    const EditTextState& previous, std::size_t selection_start,
    std::size_t selection_end, int byte_limit,
    ByteCountFunction byte_count,
    std::size_t maximum_edit_length =
        (std::numeric_limits<std::size_t>::max)());

EditTextState AdoptEditTextAsSource(
    std::wstring_view edit_text,
    std::size_t maximum_edit_length =
        (std::numeric_limits<std::size_t>::max)());

}  // namespace ccz::text_wrap
