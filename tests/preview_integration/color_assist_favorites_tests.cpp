#include "../../cczEditor2/ColorAssistFavorites.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    const std::wstring path = argv[1];
    DeleteFileW(path.c_str());
    auto colors = ccz::editor_preview::LoadColorFavorites(path);
    if (colors != ccz::editor_preview::kDefaultColorFavorites)
        return 3;
    colors[0] = 0xFF;
    colors[1] = 0xFF;  // Separate favorite slots may use the same color.
    if (!ccz::editor_preview::SaveColorFavorites(path, colors))
        return 4;
    if (ccz::editor_preview::LoadColorFavorites(path) != colors)
        return 5;
    WritePrivateProfileStringW(L"Favorites", L"Color3", L"999", path.c_str());
    auto reloaded = ccz::editor_preview::LoadColorFavorites(path);
    if (reloaded[0] != 0xFF || reloaded[1] != 0xFF ||
        reloaded[2] != ccz::editor_preview::kDefaultColorFavorites[2])
        return 6;
    if (!ccz::editor_preview::SaveColorFavorites(
            path, ccz::editor_preview::kDefaultColorFavorites))
        return 7;
    if (ccz::editor_preview::LoadColorFavorites(path) !=
        ccz::editor_preview::kDefaultColorFavorites)
        return 8;
    DeleteFileW(path.c_str());
    std::cout << "COLOR_FAVORITES_PASS\n";
    return 0;
}
