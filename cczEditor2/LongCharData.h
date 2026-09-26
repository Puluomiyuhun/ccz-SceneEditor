#pragma once

#include <cstddef>
#include <cwchar>
#include <new>

namespace ccz {
namespace editor {

// The editor's normal text capacity includes the terminating NUL.  The
// pointer stored in ItemData is accompanied by its actual capacity; callers
// must never substitute this constant for that runtime value.
constexpr std::size_t kLongCharDataCapacity = 3000;
constexpr std::size_t kLongCharDataMaxLength = kLongCharDataCapacity - 1;

inline bool StoreLongCharData(
    wchar_t* destination,
    std::size_t capacity,
    const wchar_t* text) noexcept
{
    if (!destination || capacity == 0 || !text) return false;

    const std::size_t length = std::wcslen(text);
    if (length >= capacity) return false;

    for (std::size_t i = 0; i <= length; ++i) {
        destination[i] = text[i];
    }
    return true;
}

inline bool ReplaceLongCharData(
    wchar_t*& destination,
    std::size_t& capacity,
    const wchar_t* text,
    std::size_t replacement_capacity) 
{
    if (!text || replacement_capacity == 0) return false;

    const std::size_t length = std::wcslen(text);
    if (length >= replacement_capacity) return false;

    wchar_t* replacement = new (std::nothrow) wchar_t[replacement_capacity];
    if (!replacement) return false;
    for (std::size_t i = 0; i <= length; ++i) {
        replacement[i] = text[i];
    }

    delete[] destination;
    destination = replacement;
    capacity = replacement_capacity;
    return true;
}

inline void ReleaseLongCharData(
    wchar_t*& destination,
    std::size_t& capacity) noexcept
{
    delete[] destination;
    destination = nullptr;
    capacity = 0;
}

} // namespace editor
} // namespace ccz
