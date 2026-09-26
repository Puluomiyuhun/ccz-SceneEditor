#include <ccz/preview/instruction_catalog.h>

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <exception>
#include <locale>
#include <stdexcept>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

namespace ccz::preview {
namespace {

bool is_ascii_space(std::uint8_t value) {
    return value == 0x09 || value == 0x0a || value == 0x0c ||
           value == 0x0d || value == 0x20;
}

void trim_ascii_space(const std::vector<std::uint8_t>& bytes,
                      std::size_t& begin, std::size_t& end) {
    while (begin < end && is_ascii_space(bytes[begin])) {
        ++begin;
    }
    while (end > begin && is_ascii_space(bytes[end - 1])) {
        --end;
    }
}

void append_diagnostic(InstructionCatalog& catalog,
                       const std::wstring& message) {
    if (!catalog.diagnostic.empty()) {
        catalog.diagnostic += L"；";
    }
    catalog.diagnostic += message;
}

int hex_digit(std::uint8_t value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool parse_id(const std::vector<std::uint8_t>& bytes, std::size_t begin,
              std::size_t end, std::uint16_t& result) {
    if (begin == end) {
        return false;
    }

    // CczString.ini stores instruction IDs as hexadecimal even when the
    // optional 0x prefix is omitted (for example, 36, 37 and 79).  A
    // digits-only token therefore must not be interpreted as decimal.
    int base = 16;
    if (end - begin >= 2 && bytes[begin] == '0' &&
        (bytes[begin + 1] == 'x' || bytes[begin + 1] == 'X')) {
        begin += 2;
    }
    if (begin == end) {
        return false;
    }

    std::uint32_t value = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const int digit = hex_digit(bytes[index]);
        if (digit < 0 || digit >= base) {
            return false;
        }
        value = value * static_cast<std::uint32_t>(base) +
                static_cast<std::uint32_t>(digit);
        if (value > (std::numeric_limits<std::uint16_t>::max)()) {
            return false;
        }
    }

    result = static_cast<std::uint16_t>(value);
    return true;
}

bool decode_cp936(const std::vector<std::uint8_t>& bytes, std::size_t begin,
                  std::size_t end, std::wstring& result) {
    if (begin == end || end - begin > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }

    const char* source = reinterpret_cast<const char*>(bytes.data() + begin);
    const std::string input(source, end - begin);
    try {
        std::wstring_convert<
            std::codecvt_byname<wchar_t, char, std::mbstate_t>>
            converter(new std::codecvt_byname<wchar_t, char, std::mbstate_t>(
                ".936"));
        result = converter.from_bytes(input);
        return !result.empty();
    } catch (const std::exception&) {
        result.clear();
        return false;
    }
}

}  // namespace

InstructionCatalog parse_instruction_catalog(
    const std::vector<std::uint8_t>& cp936_bytes) {
    InstructionCatalog catalog;

    std::size_t line_end = 0;
    while (line_end < cp936_bytes.size() && cp936_bytes[line_end] != '\r' &&
           cp936_bytes[line_end] != '\n') {
        ++line_end;
    }

    std::size_t item_begin = 0;
    std::size_t item_index = 0;
    while (item_begin <= line_end) {
        std::size_t item_end = item_begin;
        while (item_end < line_end && cp936_bytes[item_end] != ',') {
            ++item_end;
        }

        std::size_t trimmed_begin = item_begin;
        std::size_t trimmed_end = item_end;
        trim_ascii_space(cp936_bytes, trimmed_begin, trimmed_end);

        // The editor's shipped CczString.ini terminates its first-line
        // instruction list with a comma.  Treat only that final empty field
        // as a delimiter, while retaining diagnostics for empty fields in
        // the middle of the catalog.
        if (trimmed_begin == trimmed_end && item_begin == line_end &&
            line_end != 0 && cp936_bytes[line_end - 1] == ',') {
            break;
        }

        std::size_t colon = trimmed_begin;
        while (colon < trimmed_end && cp936_bytes[colon] != ':') {
            ++colon;
        }

        bool item_valid = true;
        std::uint16_t instruction_id = 0;
        if (trimmed_begin == trimmed_end || colon == trimmed_end) {
            item_valid = false;
            append_diagnostic(catalog, L"第" + std::to_wstring(item_index) +
                                           L"项缺少 ASCII 冒号");
        } else {
            std::size_t id_begin = trimmed_begin;
            std::size_t id_end = colon;
            trim_ascii_space(cp936_bytes, id_begin, id_end);
            if (!parse_id(cp936_bytes, id_begin, id_end, instruction_id)) {
                item_valid = false;
                append_diagnostic(
                    catalog, L"第" + std::to_wstring(item_index) +
                                 L"项指令 ID 无效");
            }
        }

        std::size_t name_begin = colon == trimmed_end ? trimmed_end : colon + 1;
        std::size_t name_end = trimmed_end;
        trim_ascii_space(cp936_bytes, name_begin, name_end);
        std::wstring name;
        if (item_valid && !decode_cp936(cp936_bytes, name_begin, name_end, name)) {
            item_valid = false;
            append_diagnostic(
                catalog, L"第" + std::to_wstring(item_index) +
                             L"项名称不是有效的 CP936 字节序列");
        }

        if (item_valid) {
            const auto inserted = catalog.names.emplace(instruction_id, name);
            if (!inserted.second) {
                append_diagnostic(catalog, L"第" + std::to_wstring(item_index) +
                                               L"项重复的指令 ID");
            }
        }

        ++item_index;
        if (item_end == line_end) {
            break;
        }
        item_begin = item_end + 1;
    }

    if (catalog.names.empty()) {
        if (catalog.diagnostic.empty()) {
            append_diagnostic(catalog, L"第一行没有有效的指令目录项");
        }
        catalog.valid = false;
    } else {
        catalog.valid = true;
    }
    return catalog;
}

std::wstring instruction_name(const InstructionCatalog& catalog,
                              std::uint16_t instruction_id) {
    const auto found = catalog.names.find(instruction_id);
    if (found != catalog.names.end()) {
        return found->second;
    }

    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex << std::setw(2)
           << std::setfill(L'0') << instruction_id << L":未知指令";
    return stream.str();
}

}  // namespace ccz::preview
