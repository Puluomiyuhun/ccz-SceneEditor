#include "TextWrapEditSession.h"

#include <algorithm>
#include <utility>

namespace ccz::text_wrap {
namespace {

bool is_hex_digit(wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') ||
           (ch >= L'a' && ch <= L'f') ||
           (ch >= L'A' && ch <= L'F');
}

std::wstring normalize_lf(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == L'\r') {
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            result.push_back(L'\n');
        } else {
            result.push_back(text[index]);
        }
    }
    return result;
}

std::wstring to_crlf(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size() + text.size() / 16);
    for (wchar_t ch : text) {
        if (ch == L'\n') result.push_back(L'\r');
        result.push_back(ch);
    }
    return result;
}

int count_bytes(ByteCountFunction byte_count, std::wstring_view text) {
    if (text.empty()) return 0;
    const int result = byte_count != nullptr ? byte_count(text) : 0;
    return result > 0 ? result : static_cast<int>(text.size());
}

bool read_color_tag(std::wstring_view text, std::size_t start,
                    std::size_t line_end, std::size_t& end,
                    int& payload_bytes, ByteCountFunction byte_count) {
    if (start + 4 > line_end || text[start] != L'[' ||
        text[start + 1] != L'C' || !is_hex_digit(text[start + 2]) ||
        !is_hex_digit(text[start + 3])) {
        return false;
    }

    const std::size_t close = text.find(L']', start + 4);
    if (close == std::wstring_view::npos || close >= line_end) {
        end = line_end;
        payload_bytes = count_bytes(
            byte_count, text.substr(start + 4, line_end - (start + 4)));
        return true;
    }

    end = close + 1;
    payload_bytes = count_bytes(
        byte_count, text.substr(start + 4, close - (start + 4)));
    return true;
}

struct FormattedText {
    std::wstring storage_text;
    std::vector<std::size_t> storage_to_source;
    std::vector<std::size_t> source_to_storage;
};

void append_wrapped_line(std::wstring_view source, std::size_t line_start,
                         std::size_t line_end, int byte_limit,
                         ByteCountFunction byte_count,
                         FormattedText& formatted) {
    int visible_bytes = 0;
    for (std::size_t index = line_start; index < line_end;) {
        formatted.source_to_storage[index] = formatted.storage_text.size();

        std::size_t unit_end = index + 1;
        int unit_bytes = 0;
        if (!read_color_tag(source, index, line_end, unit_end, unit_bytes,
                            byte_count)) {
            if (index + 1 < line_end && source[index] >= 0xd800 &&
                source[index] <= 0xdbff && source[index + 1] >= 0xdc00 &&
                source[index + 1] <= 0xdfff) {
                unit_end = index + 2;
            }
            unit_bytes = count_bytes(
                byte_count, source.substr(index, unit_end - index));
        }

        if (byte_limit > 0 && visible_bytes > 0 &&
            visible_bytes + unit_bytes > byte_limit) {
            formatted.storage_text.push_back(L'\n');
            formatted.storage_to_source.push_back(index);
            formatted.source_to_storage[index] =
                formatted.storage_text.size();
            visible_bytes = 0;
        }

        for (std::size_t source_index = index;
             source_index < unit_end; ++source_index) {
            formatted.storage_text.push_back(source[source_index]);
            formatted.storage_to_source.push_back(source_index + 1);
            formatted.source_to_storage[source_index + 1] =
                formatted.storage_text.size();
        }
        index = unit_end;
        visible_bytes += unit_bytes;
    }
    formatted.source_to_storage[line_end] = formatted.storage_text.size();
}

FormattedText format_source(std::wstring_view source, int byte_limit,
                            ByteCountFunction byte_count) {
    FormattedText result;
    result.storage_to_source.push_back(0);
    result.source_to_storage.assign(source.size() + 1, 0);

    std::size_t line_start = 0;
    while (line_start <= source.size()) {
        const std::size_t newline = source.find(L'\n', line_start);
        const std::size_t line_end = newline == std::wstring_view::npos
                                         ? source.size() : newline;
        append_wrapped_line(source, line_start, line_end, byte_limit,
                            byte_count, result);
        if (newline == std::wstring_view::npos) break;

        result.source_to_storage[newline] = result.storage_text.size();
        result.storage_text.push_back(L'\n');
        result.storage_to_source.push_back(newline + 1);
        result.source_to_storage[newline + 1] = result.storage_text.size();
        line_start = newline + 1;
    }
    return result;
}

EditTextState make_state(std::wstring source, int byte_limit,
                         ByteCountFunction byte_count,
                         std::size_t maximum_edit_length) {
    EditTextState result;
    result.source_text = normalize_lf(source);

    FormattedText formatted = format_source(
        result.source_text, byte_limit, byte_count);
    result.storage_text = std::move(formatted.storage_text);
    result.edit_text = to_crlf(result.storage_text);

    if (byte_limit > 0 && result.edit_text.size() > maximum_edit_length) {
        formatted = format_source(result.source_text, 0, byte_count);
        result.storage_text = std::move(formatted.storage_text);
        result.edit_text = to_crlf(result.storage_text);
    }

    result.edit_to_source.clear();
    result.edit_to_source.reserve(result.edit_text.size() + 1);
    result.edit_to_source.push_back(formatted.storage_to_source.front());
    for (std::size_t index = 0; index < result.storage_text.size(); ++index) {
        if (result.storage_text[index] == L'\n') {
            result.edit_to_source.push_back(
                formatted.storage_to_source[index]);
            result.edit_to_source.push_back(
                formatted.storage_to_source[index + 1]);
        } else {
            result.edit_to_source.push_back(
                formatted.storage_to_source[index + 1]);
        }
    }

    std::vector<std::size_t> storage_to_edit(result.storage_text.size() + 1);
    std::size_t edit_position = 0;
    for (std::size_t index = 0; index < result.storage_text.size(); ++index) {
        storage_to_edit[index] = edit_position;
        edit_position += result.storage_text[index] == L'\n' ? 2 : 1;
    }
    storage_to_edit[result.storage_text.size()] = edit_position;
    result.source_to_edit.resize(result.source_text.size() + 1);
    for (std::size_t source_position = 0;
         source_position <= result.source_text.size(); ++source_position) {
        result.source_to_edit[source_position] = storage_to_edit[
            formatted.source_to_storage[source_position]];
    }
    return result;
}

std::size_t mapped_source_position(const EditTextState& state,
                                  std::size_t edit_position) {
    if (state.edit_to_source.empty()) return 0;
    return state.edit_to_source[(std::min)(
        edit_position, state.edit_to_source.size() - 1)];
}

std::size_t mapped_edit_position(const EditTextState& state,
                                 std::size_t source_position) {
    if (state.source_to_edit.empty()) return 0;
    return state.source_to_edit[(std::min)(
        source_position, state.source_to_edit.size() - 1)];
}

struct TextDelta {
    std::size_t old_start{};
    std::size_t old_end{};
    std::size_t new_start{};
    std::size_t new_end{};
};

bool same_replacement(std::wstring_view old_text, std::wstring_view new_text,
                      const TextDelta& delta) {
    return delta.old_start <= old_text.size() &&
           delta.old_end <= old_text.size() &&
           delta.new_start <= new_text.size() &&
           delta.new_end <= new_text.size() &&
           delta.old_start == delta.new_start &&
           old_text.substr(0, delta.old_start) ==
               new_text.substr(0, delta.new_start) &&
           old_text.substr(delta.old_end) == new_text.substr(delta.new_end);
}

TextDelta find_delta(const EditTextState& previous,
                     std::wstring_view new_text,
                     std::size_t selection_start,
                     std::size_t selection_end) {
    const std::wstring_view old_text(previous.edit_text);
    const std::ptrdiff_t length_delta =
        static_cast<std::ptrdiff_t>(new_text.size()) -
        static_cast<std::ptrdiff_t>(old_text.size());

    // A collapsed selection plus the resulting caret identifies ordinary
    // insertions and deletions even when surrounding characters repeat.
    if (selection_start == selection_end && length_delta > 0 &&
        selection_start >= static_cast<std::size_t>(length_delta)) {
        const std::size_t new_start = selection_start -
            static_cast<std::size_t>(length_delta);
        const TextDelta candidate{new_start, new_start, new_start,
                                  selection_start};
        if (same_replacement(old_text, new_text, candidate)) return candidate;
    }
    if (selection_start == selection_end && length_delta < 0) {
        const std::size_t deleted = static_cast<std::size_t>(-length_delta);
        const TextDelta candidate{selection_start, selection_start + deleted,
                                  selection_start, selection_start};
        if (same_replacement(old_text, new_text, candidate)) return candidate;
    }

    std::size_t prefix = 0;
    while (prefix < old_text.size() && prefix < new_text.size() &&
           old_text[prefix] == new_text[prefix]) {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix < old_text.size() - prefix &&
           suffix < new_text.size() - prefix &&
           old_text[old_text.size() - suffix - 1] ==
               new_text[new_text.size() - suffix - 1]) {
        ++suffix;
    }
    return {prefix, old_text.size() - suffix,
            prefix, new_text.size() - suffix};
}

std::size_t source_position_after_inserted_prefix(
    std::wstring_view inserted, std::size_t prefix_length,
    std::size_t source_start) {
    const std::size_t safe_length = (std::min)(prefix_length, inserted.size());
    return source_start + normalize_lf(inserted.substr(0, safe_length)).size();
}

}  // namespace

EditTextState CreateEditTextState(
    std::wstring_view text, int byte_limit, ByteCountFunction byte_count,
    std::size_t maximum_edit_length) {
    return make_state(normalize_lf(text), byte_limit, byte_count,
                      maximum_edit_length);
}

EditTextUpdate ApplyEditTextChange(
    const EditTextState& previous, std::wstring_view edit_text,
    std::size_t selection_start, std::size_t selection_end,
    int byte_limit, ByteCountFunction byte_count,
    std::size_t maximum_edit_length) {
    const std::wstring normalized_edit = to_crlf(normalize_lf(edit_text));
    selection_start = (std::min)(selection_start, normalized_edit.size());
    selection_end = (std::min)(selection_end, normalized_edit.size());
    if (selection_start > selection_end) std::swap(selection_start, selection_end);

    const TextDelta delta = find_delta(
        previous, normalized_edit, selection_start, selection_end);
    const std::size_t source_start = mapped_source_position(
        previous, delta.old_start);
    const std::size_t source_end = mapped_source_position(
        previous, delta.old_end);
    std::wstring inserted = normalize_lf(std::wstring_view(normalized_edit).substr(
        delta.new_start, delta.new_end - delta.new_start));

    std::wstring source = previous.source_text;
    const std::size_t replace_start = (std::min)(source_start, source_end);
    const std::size_t replace_end = (std::max)(source_start, source_end);
    source.replace(replace_start, replace_end - replace_start, inserted);

    const auto map_new_selection = [&](std::size_t position) {
        if (position <= delta.new_start) {
            return mapped_source_position(previous, position);
        }
        if (position >= delta.new_end) {
            const std::size_t old_position = delta.old_end +
                (position - delta.new_end);
            const std::size_t old_source = mapped_source_position(
                previous, old_position);
            const std::ptrdiff_t adjustment =
                static_cast<std::ptrdiff_t>(inserted.size()) -
                static_cast<std::ptrdiff_t>(replace_end - replace_start);
            const std::ptrdiff_t mapped =
                static_cast<std::ptrdiff_t>(old_source) + adjustment;
            return static_cast<std::size_t>((std::max<std::ptrdiff_t>)(0, mapped));
        }
        return source_position_after_inserted_prefix(
            normalized_edit.substr(delta.new_start,
                                   delta.new_end - delta.new_start),
            position - delta.new_start, replace_start);
    };

    EditTextUpdate result;
    result.state = make_state(std::move(source), byte_limit, byte_count,
                              maximum_edit_length);
    result.selection_start = mapped_edit_position(
        result.state, (std::min)(map_new_selection(selection_start),
                                 result.state.source_text.size()));
    result.selection_end = mapped_edit_position(
        result.state, (std::min)(map_new_selection(selection_end),
                                 result.state.source_text.size()));
    return result;
}

EditTextUpdate RewrapEditText(
    const EditTextState& previous, std::size_t selection_start,
    std::size_t selection_end, int byte_limit,
    ByteCountFunction byte_count, std::size_t maximum_edit_length) {
    const std::size_t source_start = mapped_source_position(
        previous, selection_start);
    const std::size_t source_end = mapped_source_position(
        previous, selection_end);
    EditTextUpdate result;
    result.state = make_state(previous.source_text, byte_limit, byte_count,
                              maximum_edit_length);
    result.selection_start = mapped_edit_position(result.state, source_start);
    result.selection_end = mapped_edit_position(result.state, source_end);
    return result;
}

EditTextState AdoptEditTextAsSource(
    std::wstring_view edit_text, std::size_t maximum_edit_length) {
    return make_state(normalize_lf(edit_text), 0, nullptr,
                      maximum_edit_length);
}

}  // namespace ccz::text_wrap
