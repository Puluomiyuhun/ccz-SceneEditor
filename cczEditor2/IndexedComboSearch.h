#pragma once

#include <cwctype>
#include <limits>
#include <string>
#include <vector>

namespace ccz {
namespace editor {

// The value written by a dialog is deliberately kept separate from the
// displayed numeric code.  This is required by segmented equipment lists:
// the label shows the global item number while the script stores a local
// segment index.
struct IndexedOption {
    std::wstring label;
    int selection_value = -1;
    int numeric_code = -1;
    // A numeric code can remain valid while being intentionally absent from
    // the text-search result set.  This is separate from whether a typed
    // decimal query may resolve the option.
    bool numeric_searchable = true;
    // Reserved item numbers such as 255/511 remain selectable from the
    // native list, but must not be accepted from a typed decimal query.
    bool numeric_resolvable = true;
    bool hexadecimal = false;
};

inline bool ParseHexCode(const std::wstring& text,int& value) {
    std::size_t start=text.size()>2&&text[0]==L'0'&&(text[1]==L'x'||text[1]==L'X')?2:0;
    if(start==text.size()||text.empty())return false;
    unsigned int parsed=0;
    for(std::size_t i=start;i<text.size();++i){auto c=std::towlower(text[i]);int d=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;
        if(d<0||parsed>(static_cast<unsigned int>((std::numeric_limits<int>::max)())-d)/16)return false;parsed=parsed*16+d;}
    value=static_cast<int>(parsed);return true;
}
inline std::wstring HexCode(int value){const wchar_t* digits=L"0123456789abcdef";std::wstring s;do{s.insert(s.begin(),digits[value%16]);value/=16;}while(value);return s;}

inline bool ParseDecimalCode(const std::wstring& text, int& value)
{
    if (text.empty()) return false;
    unsigned long long parsed = 0;
    const unsigned long long max_value =
        static_cast<unsigned long long>((std::numeric_limits<int>::max)());
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return false;
        const unsigned long long digit = static_cast<unsigned long long>(ch - L'0');
        // Check before multiplying so arbitrarily long input cannot wrap the
        // accumulator before it is compared with INT_MAX.
        if (parsed > max_value / 10ULL ||
            (parsed == max_value / 10ULL && digit > max_value % 10ULL)) {
            return false;
        }
        parsed = parsed * 10ULL + digit;
    }
    value = static_cast<int>(parsed);
    return true;
}

inline bool ExtractLeadingDecimalCode(const std::wstring& label, int& value)
{
	// Character pickers use both legacy `id:name` labels and the S-preview
	// roster's `id.name` format.  Accept both so numeric direct input keeps
	// resolving the same indexed character without changing its display text.
	const size_t separator = label.find_first_of(L":.");
	if (separator == std::wstring::npos || separator == 0) return false;
	return ParseDecimalCode(label.substr(0, separator), value);
}

inline wchar_t FoldCase(wchar_t ch)
{
    return static_cast<wchar_t>(std::towlower(ch));
}

inline bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool matches = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (FoldCase(haystack[start + i]) != FoldCase(needle[i])) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

inline bool MatchesIndexedOption(
    const IndexedOption& option,
    const std::wstring& query)
{
    if (ContainsInsensitive(option.label, query)) return true;
    if (option.numeric_code < 0 || !option.numeric_searchable) return false;

    // Some entries deliberately display a non-numeric label (for example
    // “无”) while still carrying a stable numeric code.  Keep that code in
    // the same live-search index as the visible label.
    if(option.hexadecimal){int code=-1;if(ParseHexCode(query,code)&&code==option.numeric_code)return true;return ContainsInsensitive(HexCode(option.numeric_code),query);}
    return ContainsInsensitive(std::to_wstring(option.numeric_code), query);
}

inline std::vector<size_t> FilterIndexedOptions(
    const std::vector<IndexedOption>& options,
    const std::wstring& query)
{
    std::vector<size_t> result;
    for (size_t i = 0; i < options.size(); ++i) {
        if (MatchesIndexedOption(options[i], query)) {
            result.push_back(i);
        }
    }
    return result;
}

// Resolve the first candidate in the order presented by the filtered list.
// The caller supplies the visible model indexes so that keyboard submission
// follows the same ordering the user sees, without changing the text while
// they are still typing.
inline bool ResolveFirstMatchingOption(
    const std::vector<IndexedOption>& options,
    const std::vector<size_t>& visible_indexes,
    const std::wstring& query,
    int& selection_value)
{
    for (size_t index : visible_indexes) {
        if (index < options.size() && MatchesIndexedOption(options[index], query)) {
            selection_value = options[index].selection_value;
            return true;
        }
    }
    return false;
}

// When the user is extending a query, the previous result set is a valid
// upper bound for the next result set.  Keeping this operation separate from
// the full scan lets the UI reuse that candidate set without changing the
// model indexes stored in IndexedOption.
inline std::vector<size_t> FilterIndexedOptionsFromCandidates(
    const std::vector<IndexedOption>& options,
    const std::wstring& query,
    const std::vector<size_t>& candidate_indexes)
{
    std::vector<size_t> result;
    result.reserve(candidate_indexes.size());
    for (size_t index : candidate_indexes) {
        if (index < options.size() && MatchesIndexedOption(options[index], query)) {
            result.push_back(index);
        }
    }
    return result;
}

inline bool IsStrictQueryExtension(
    const std::wstring& previous_query,
    const std::wstring& current_query)
{
    if (previous_query.empty() || current_query.size() <= previous_query.size()) {
        return false;
    }
    for (size_t i = 0; i < previous_query.size(); ++i) {
        if (FoldCase(previous_query[i]) != FoldCase(current_query[i])) {
            return false;
        }
    }
    return true;
}

inline bool ResolveDecimalCode(
    const std::vector<IndexedOption>& options,
    const std::wstring& text,
    int& selection_value)
{
    int numeric_code = -1;
    if (!ParseDecimalCode(text, numeric_code)) return false;
    for (const IndexedOption& option : options) {
        if (option.numeric_code == numeric_code && option.numeric_resolvable) {
            selection_value = option.selection_value;
            return true;
        }
    }
    return false;
}

inline bool ResolveIndexedCode(const std::vector<IndexedOption>& options,const std::wstring& text,int& value) {
    for(auto& option:options){int code=-1;if((option.hexadecimal?ParseHexCode(text,code):ParseDecimalCode(text,code))&&option.numeric_resolvable&&option.numeric_code==code){value=option.selection_value;return true;}}
    return false;
}

} // namespace editor
} // namespace ccz
