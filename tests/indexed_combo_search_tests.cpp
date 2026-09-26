#include <cassert>
#include <string>
#include <vector>

#include "../cczEditor2/IndexedComboSearch.h"

using ccz::editor::IndexedOption;

int main()
{
    const std::vector<IndexedOption> people = {
        {L"0:曹操", 0, 0},
        {L"174:夏侯惇", 174, 174},
        {L"Var0", 1028, -1},
        {L"无", 5374, -1},
    };

    int selection = -1;
    assert(ccz::editor::ResolveDecimalCode(people, L"174", selection));
    assert(selection == 174);
    assert(!ccz::editor::ResolveDecimalCode(people, L"1740", selection));
    assert(!ccz::editor::ResolveDecimalCode(people, L"-1", selection));
    assert(!ccz::editor::ResolveDecimalCode(people, L"999999999999999999999999999999999999999999", selection));

    std::vector<size_t> matches = ccz::editor::FilterIndexedOptions(people, L"夏侯");
    assert(matches.size() == 1 && matches[0] == 1);
    matches = ccz::editor::FilterIndexedOptions(people, L"VAR");
    assert(matches.size() == 1 && matches[0] == 2);

    const std::vector<IndexedOption> duplicateNames = {
        {L"1:风间樱", 1, 1},
        {L"2:风间樱（备用）", 2, 2},
    };
    int first_match = -1;
    assert(ccz::editor::ResolveFirstMatchingOption(
        duplicateNames, {0, 1}, L"风间", first_match));
    assert(first_match == 1);
    assert(ccz::editor::ResolveFirstMatchingOption(
        duplicateNames, {1, 0}, L"风间", first_match));
    assert(first_match == 2);
    assert(!ccz::editor::ResolveFirstMatchingOption(
        duplicateNames, {0, 1}, L"不存在", first_match));

    const std::vector<IndexedOption> segmentedWeapons = {
        {L"默认装备", 0, -1},
        {L"卸去装备", 1, -1},
        {L"70:青釭剑", 2, 70},
        {L"71:倚天剑", 3, 71},
    };
    assert(ccz::editor::ResolveDecimalCode(segmentedWeapons, L"70", selection));
    assert(selection == 2); // global item number 70 -> local segment selection 2
    assert(!ccz::editor::ResolveDecimalCode(segmentedWeapons, L"0", selection));
    assert(!ccz::editor::ResolveDecimalCode(segmentedWeapons, L"默认", selection));

    const std::vector<IndexedOption> fullItems = {
        {L"254:铁枪", 254, 254},
        {L"无", 255, 255, false, false},
        {L"保留项", 511, 511, false, false},
    };
    matches = ccz::editor::FilterIndexedOptions(fullItems, L"255");
    // The special item-list row “无” is neither a live-search result nor a
    // directly resolvable numeric item.  It remains selectable from the list.
    assert(matches.empty());
    assert(!ccz::editor::ResolveDecimalCode(fullItems, L"255", selection));
    matches = ccz::editor::FilterIndexedOptions(fullItems, L"511");
    assert(matches.empty());
    assert(!ccz::editor::ResolveDecimalCode(fullItems, L"511", selection));

    const std::vector<IndexedOption> segmentedItems = {
        {L"默认装备", 0, -1},
        {L"卸去装备", 1, -1},
        {L"无", 2, 255, false, false},
        {L"分段保留项", 3, 511, false, false},
    };
    assert(!ccz::editor::ResolveDecimalCode(segmentedItems, L"255", selection));
    matches = ccz::editor::FilterIndexedOptions(segmentedItems, L"511");
    assert(matches.empty());
    assert(!ccz::editor::ResolveDecimalCode(segmentedItems, L"511", selection));

    // Person lists have no item-list 255 exception: an ordinary 255:... row
    // remains searchable by its decimal data number.
    const std::vector<IndexedOption> peopleWith255 = {
        {L"255:角色255", 255, 255},
    };
    matches = ccz::editor::FilterIndexedOptions(peopleWith255, L"255");
    assert(matches.size() == 1 && matches[0] == 0);
    assert(ccz::editor::ResolveDecimalCode(peopleWith255, L"255", selection));
    assert(selection == 255);

    return 0;
}
