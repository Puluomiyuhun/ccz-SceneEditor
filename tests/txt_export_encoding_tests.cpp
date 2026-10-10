#include <windows.h>

#include <cassert>
#include <string>
#include <vector>

static std::vector<char> EncodeForTxt(const std::wstring& text)
{
    const int required = WideCharToMultiByte(
        936, 0, text.c_str(), -1, NULL, 0, NULL, FALSE);
    assert(required > 0);

    std::vector<char> encoded(static_cast<std::size_t>(required));
    const int written = WideCharToMultiByte(
        936, 0, text.c_str(), -1, encoded.data(), required, NULL, FALSE);
    assert(written > 0);
    if (written <= 1) return {};
    return std::vector<char>(encoded.begin(), encoded.begin() + (written - 1));
}

static int EncodedLength(const std::wstring& text)
{
    return WideCharToMultiByte(936, 0, text.c_str(), -1, NULL, 0, NULL, FALSE) - 1;
}

int main()
{
    // T1: this is deliberately above the historical 3000-byte stack limit.
    const std::wstring long_text(3500, L'汉');
    const std::vector<char> long_encoded = EncodeForTxt(long_text);
    assert(long_encoded.size() == static_cast<std::size_t>(EncodedLength(long_text)));
    assert(long_encoded.size() > 3000);

    // T2: a node with no text contributes no content byte and never exposes NUL.
    const std::vector<char> empty_encoded = EncodeForTxt(L"");
    assert(empty_encoded.empty());

    // T3: mixed ASCII/Chinese text preserves the API's exact CP936 length.
    const std::wstring mixed = L"A汉B字C";
    const std::vector<char> mixed_encoded = EncodeForTxt(mixed);
    assert(mixed_encoded.size() == static_cast<std::size_t>(EncodedLength(mixed)));
    for (char byte : mixed_encoded) assert(byte != '\0');

    // T4: an unrepresentable character follows Windows' existing default-char
    // conversion policy, while the output remains bounded and NUL-free.
    const std::vector<char> fallback_encoded = EncodeForTxt(L"😀");
    assert(fallback_encoded.size() == static_cast<std::size_t>(EncodedLength(L"😀")));
    for (char byte : fallback_encoded) assert(byte != '\0');

    return 0;
}
