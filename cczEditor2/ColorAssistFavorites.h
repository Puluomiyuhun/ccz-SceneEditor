#pragma once

#include <array>
#include <string>
#include <windows.h>
#include <shlobj.h>

namespace ccz::editor_preview {

inline constexpr std::array<int, 12> kDefaultColorFavorites = {
    0x28, 0x2A, 0x26, 0x46, 0x54, 0x5A,
    0x64, 0x3A, 0x3C, 0x77, 0x84, 0x8A};

inline std::wstring ColorFavoritesIniPath() {
    PWSTR appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                    &appdata)))
        return {};
    std::wstring folder(appdata);
    CoTaskMemFree(appdata);
    folder += L"\\cczSceneEditor2";
    if (!CreateDirectoryW(folder.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    return folder + L"\\ColorAssist.ini";
}

inline std::array<int, 12> LoadColorFavorites(const std::wstring& path) {
    auto values = kDefaultColorFavorites;
    if (path.empty())
        return values;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::wstring key = L"Color" + std::to_wstring(i + 1);
        const int value = GetPrivateProfileIntW(L"Favorites", key.c_str(),
                                                -1, path.c_str());
        if (value >= 0 && value <= 255)
            values[i] = value;
    }
    return values;
}

inline bool SaveColorFavorites(const std::wstring& path,
                               const std::array<int, 12>& values) {
    if (path.empty())
        return false;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] < 0 || values[i] > 255)
            return false;
        const std::wstring key = L"Color" + std::to_wstring(i + 1);
        const std::wstring value = std::to_wstring(values[i]);
        if (!WritePrivateProfileStringW(L"Favorites", key.c_str(),
                                        value.c_str(), path.c_str()))
            return false;
    }
    return true;
}

} // namespace ccz::editor_preview
