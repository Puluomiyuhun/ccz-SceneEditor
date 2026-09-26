#include "test_framework.h"

#include <PreviewIntegration/DialogueOverlayRenderer.h>
#include <PreviewIntegration/PreviewAssetCache.h>
#include <PreviewIntegration/PreviewRefreshDecision.h>
#include <PreviewIntegration/PreviewSession.h>

#include <ccz/preview/dialogue_overlay_model.h>
#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/instruction_catalog.h>

#include <windows.h>
#include <gdiplus.h>

#include <cstdio>
#include <algorithm>
#include <cwctype>
#include <utility>
#include <string>
#include <vector>

namespace {

using ccz::editor_preview::PreviewAssetCache;
using ccz::editor_preview::PreviewAssetsView;
using ccz::editor_preview::PreviewSession;
using ccz::preview::DialogueOverlayModel;
using ccz::preview::DialogueProfile;
using ccz::editor_preview::EditorPreviewInput;
using ccz::preview::InstructionCatalog;
using ccz::preview::RawReplayNode;

std::vector<unsigned char> make_data_archive(unsigned char first_byte) {
    std::vector<unsigned char> bytes(0x18c + 0x20, 0);
    bytes[0] = first_byte;
    return bytes;
}

std::wstring make_temp_root(const wchar_t* suffix) {
    wchar_t temp_path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring root = temp_path;
    root += L"ccz-task8-";
    root += suffix;
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + L"\\E5").c_str(), nullptr);
    return root + L"\\";
}

void write_bytes(const std::wstring& path,
                 const std::vector<unsigned char>& bytes) {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"wb");
    CCZ_REQUIRE(file != nullptr);
    if (file != nullptr) {
        fwrite(bytes.data(), 1, bytes.size(), file);
        fclose(file);
    }
}

std::vector<unsigned char> make_face_archive() {
    std::vector<unsigned char> bytes(0x124, 0);
    bytes[0x110 + 0] = 0;
    bytes[0x110 + 3] = 8;
    bytes[0x110 + 7] = 8;
    bytes[0x110 + 11] = 0x1c;
    bytes[0x110 + 10] = 1;
    bytes[0x110 + 9] = 0;
    bytes[0x110 + 8] = 0;
    bytes[0x11c] = 0x89;
    bytes[0x11d] = 0x50;
    bytes[0x11e] = 0x4e;
    bytes[0x11f] = 0x47;
    return bytes;
}

std::wstring module_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        CCZ_REQUIRE(length != 0);
        if (length + 1 < buffer.size()) {
            const std::wstring module_path(buffer.data(), length);
            const std::size_t separator = module_path.find_last_of(L"\\/");
            CCZ_REQUIRE(separator != std::wstring::npos);
            return module_path.substr(0, separator + 1);
        }
        CCZ_REQUIRE(buffer.size() < 32768);
        buffer.resize(buffer.size() * 2);
    }
}

bool read_optional_bytes(const std::wstring& path,
                         std::vector<unsigned char>& bytes) {
    bytes.clear();
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
    if (file == nullptr) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        fread(bytes.data(), 1, bytes.size(), file);
    }
    fclose(file);
    return true;
}

DialogueProfile fixture_profile() {
    DialogueProfile profile;
    profile.data_record_base = 0x18c;
    profile.data_record_stride = 0x20;
    profile.avatar_offset = 0x0d;
    profile.avatar_width = 2;
    profile.r_image_offset = 0x0f;
    profile.r_image_width = 2;
    profile.face_index_table_offset = 0x110;
    profile.face_entry_size = 12;
    profile.dialogue_ids = {
        {0x14, ccz::preview::DialogueKind::Standard, true, {},
         ccz::preview::PersonFieldEncoding::None, false},
    };
    return profile;
}

RawReplayNode raw_node(const wchar_t* key, std::uint16_t instruction_id,
                       const wchar_t* text) {
    RawReplayNode node;
    node.logical_key = key;
    node.instruction_id = instruction_id;
    node.command.instruction_id = instruction_id;
    node.command.text = text;
    node.source_text = text;
    return node;
}

InstructionCatalog fixture_catalog() {
    InstructionCatalog catalog;
    catalog.valid = true;
    catalog.names.emplace(0x00, L"事件结束");
    catalog.names.emplace(0x14, L"对话");
    return catalog;
}

EditorPreviewInput fixture_dialogue_input() {
    EditorPreviewInput input;
    input.document_key = L"task8-diagnostic|document";
    input.document_revision = 1;
    input.section.section_key = L"task8-diagnostic|section";
    input.section.selected_target_key = L"dialogue";
    input.section.nodes.push_back(raw_node(L"boundary", 0x00, L"0:边界"));
    input.section.nodes.push_back(raw_node(L"dialogue", 0x14,
                                           L"14:对白正文"));
    input.document_nodes = input.section.nodes;
    return input;
}

std::wstring alternate_windows_path(std::wstring path) {
    std::replace(path.begin(), path.end(), L'\\', L'/');
    for (wchar_t& character : path) {
        if (std::iswalpha(character)) {
            character = std::iswlower(character)
                ? std::towupper(character) : std::towlower(character);
        }
    }
    return path;
}

bool rect_inside(const CRect& outer, const CRect& inner) {
    return inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}

void remove_tree(const std::wstring& root) {
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = root + L"*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0) continue;
        DeleteFileW((root + data.cFileName).c_str());
    } while (FindNextFileW(find, &data));
    FindClose(find);
    RemoveDirectoryW(root.c_str());
}

}  // namespace

CCZ_TEST(asset_cache_switching_root_drops_old_owned_assets) {
    const std::wstring first_root = make_temp_root(L"a");
    const std::wstring second_root = make_temp_root(L"b");
    write_bytes(first_root + L"Data.e5", make_data_archive(1));
    write_bytes(first_root + L"E5\\Face.e5", make_face_archive());
    write_bytes(second_root + L"Data.e5", make_data_archive(4));

    PreviewAssetCache cache;
    std::wstring diagnostic;
    CCZ_REQUIRE(cache.LoadForDocument(first_root + L"RS\\R_17.eex",
                                      fixture_profile(), &diagnostic));
    const PreviewAssetsView first = cache.view();
    CCZ_REQUIRE(first.data_e5 != nullptr);
    CCZ_REQUIRE(first.face_archive != nullptr);
    const unsigned char first_byte = first.data_e5->at(0);

    CCZ_REQUIRE(cache.LoadForDocument(second_root + L"R_17.eex",
                                      fixture_profile(), &diagnostic));
    const PreviewAssetsView second = cache.view();
    CCZ_REQUIRE(second.data_e5 != nullptr);
    CCZ_REQUIRE_EQ(first_byte, static_cast<unsigned char>(1));
    CCZ_REQUIRE_EQ(second.data_e5->at(0), static_cast<unsigned char>(4));
    CCZ_REQUIRE(second.face_e5 == nullptr);
    CCZ_REQUIRE(second.face_archive == nullptr);

    remove_tree(first_root + L"E5\\");
    RemoveDirectoryW(first_root.c_str());
    remove_tree(second_root);
}

CCZ_TEST(asset_cache_reads_catalog_from_editor_module_directory) {
    const std::wstring root = make_temp_root(L"module-catalog");
    write_bytes(root + L"Data.e5", make_data_archive(9));

    const std::wstring catalog_path = module_directory() + L"CczString.ini";
    std::vector<unsigned char> previous_catalog;
    const bool had_previous_catalog =
        read_optional_bytes(catalog_path, previous_catalog);
    write_bytes(catalog_path, std::vector<unsigned char>{
        '2', '7', ':', 'm', 'o', 'd', 'u', 'l', 'e', '-', 'n', 'a', 't', 'i',
        'v', 'e', ',', '2', '9', ':', 'p', 'o', 'r', 't', 'r', 'a', 'i', 't',
        ','});

    PreviewAssetCache cache;
    std::wstring diagnostic;
    CCZ_REQUIRE(cache.LoadForDocument(root + L"R_17.eex",
                                      fixture_profile(), &diagnostic));
    const InstructionCatalog& catalog = cache.instruction_catalog();
    CCZ_REQUIRE(catalog.valid);
    const auto found = catalog.names.find(0x27);
    CCZ_REQUIRE(found != catalog.names.end());
    CCZ_REQUIRE_EQ(found->second, std::wstring(L"module-native"));

    if (had_previous_catalog) {
        write_bytes(catalog_path, previous_catalog);
    } else {
        DeleteFileW(catalog_path.c_str());
    }
    remove_tree(root);
}

CCZ_TEST(asset_cache_root_key_is_case_and_separator_insensitive) {
    const std::wstring root = make_temp_root(L"root-variant");
    write_bytes(root + L"Data.e5", make_data_archive(7));
    write_bytes(root + L"E5\\Face.e5", make_face_archive());

    PreviewAssetCache cache;
    std::wstring diagnostic;
    CCZ_REQUIRE(cache.LoadForDocument(root + L"RS\\R_17.eex",
                                      fixture_profile(), &diagnostic));
    DeleteFileW((root + L"Data.e5").c_str());
    DeleteFileW((root + L"E5\\Face.e5").c_str());

    const std::wstring variant = alternate_windows_path(root) +
                                 L"rs/r_17.eex";
    CCZ_REQUIRE(cache.LoadForDocument(variant, fixture_profile(),
                                      &diagnostic));
    const PreviewAssetsView assets = cache.view();
    CCZ_REQUIRE(assets.data_e5 != nullptr);
    CCZ_REQUIRE_EQ(assets.data_e5->at(0), static_cast<unsigned char>(7));
    CCZ_REQUIRE(assets.face_archive != nullptr);
    remove_tree(root + L"E5\\");
    RemoveDirectoryW(root.c_str());
}

CCZ_TEST(asset_cache_missing_data_keeps_face_cache_and_reports_diagnostic) {
    const std::wstring root = make_temp_root(L"missing-data");
    write_bytes(root + L"E5\\Face.e5", make_face_archive());
    PreviewAssetCache cache;
    std::wstring diagnostic;
    CCZ_REQUIRE(cache.LoadForDocument(root + L"R_17.eex",
                                      fixture_profile(), &diagnostic));
    const PreviewAssetsView assets = cache.view();
    CCZ_REQUIRE(assets.data_e5 == nullptr);
    CCZ_REQUIRE(assets.face_e5 != nullptr);
    CCZ_REQUIRE(assets.face_archive != nullptr);
    CCZ_REQUIRE(!diagnostic.empty());
    CCZ_REQUIRE(assets.diagnostic != nullptr);
    CCZ_REQUIRE(!assets.diagnostic->empty());
    remove_tree(root + L"E5\\");
    RemoveDirectoryW(root.c_str());
}

CCZ_TEST(asset_cache_missing_face_removes_only_portrait_source) {
    const std::wstring root = make_temp_root(L"missing-face");
    write_bytes(root + L"Data.e5", make_data_archive(1));
    PreviewAssetCache cache;
    std::wstring diagnostic;
    CCZ_REQUIRE(cache.LoadForDocument(root + L"R_17.eex",
                                      fixture_profile(), &diagnostic));
    const PreviewAssetsView assets = cache.view();
    CCZ_REQUIRE(assets.data_e5 != nullptr);
    CCZ_REQUIRE(assets.face_e5 == nullptr);
    CCZ_REQUIRE(assets.face_archive == nullptr);
    CCZ_REQUIRE(!diagnostic.empty());
    remove_tree(root);
}

CCZ_TEST(asset_cache_rejects_empty_and_truncated_data_with_diagnostic) {
    const std::wstring empty_root = make_temp_root(L"empty-data");
    write_bytes(empty_root + L"Data.e5", {});
    PreviewAssetCache empty_cache;
    std::wstring diagnostic;
    empty_cache.LoadForDocument(empty_root + L"R_17.eex",
                                fixture_profile(), &diagnostic);
    CCZ_REQUIRE(empty_cache.view().data_e5 == nullptr);
    CCZ_REQUIRE(diagnostic.find(L"Data.e5") != std::wstring::npos);

    const std::wstring truncated_root = make_temp_root(L"truncated-data");
    write_bytes(truncated_root + L"Data.e5", {1, 2, 3});
    PreviewAssetCache truncated_cache;
    diagnostic.clear();
    truncated_cache.LoadForDocument(truncated_root + L"R_17.eex",
                                    fixture_profile(), &diagnostic);
    CCZ_REQUIRE(truncated_cache.view().data_e5 == nullptr);
    CCZ_REQUIRE(diagnostic.find(L"Data.e5") != std::wstring::npos);
    remove_tree(empty_root);
    remove_tree(truncated_root);
}

CCZ_TEST(session_publishes_asset_diagnostic_and_unchanged_skips_repaint) {
    PreviewSession session;
    const EditorPreviewInput input = fixture_dialogue_input();
    const InstructionCatalog catalog = fixture_catalog();
    const DialogueProfile profile = fixture_profile();
    const std::wstring asset_diagnostic = L"Data.e5 未加载";
    PreviewAssetsView assets;
    assets.diagnostic = &asset_diagnostic;

    const auto first = session.Refresh(input, catalog, profile, assets);
    CCZ_REQUIRE_EQ(first.status, ccz::editor_preview::RefreshStatus::Updated);
    CCZ_REQUIRE(session.frame().dialogue.diagnostic.find(asset_diagnostic) !=
                std::wstring::npos);
    CCZ_REQUIRE(std::find(session.frame().diagnostics.begin(),
                          session.frame().diagnostics.end(),
                          asset_diagnostic) != session.frame().diagnostics.end());

    int repaint_count = 0;
    if (ccz::editor_preview::DecidePreviewRefreshAction(
            first.status, first.frame_changed, true) ==
        ccz::editor_preview::PreviewRefreshAction::Repaint) {
        ++repaint_count;
    }
    const auto second = session.Refresh(input, catalog, profile, assets);
    if (ccz::editor_preview::DecidePreviewRefreshAction(
            second.status, second.frame_changed, true) ==
        ccz::editor_preview::PreviewRefreshAction::Repaint) {
        ++repaint_count;
    }
    CCZ_REQUIRE_EQ(second.status, ccz::editor_preview::RefreshStatus::Unchanged);
    CCZ_REQUIRE_EQ(
        ccz::editor_preview::DecidePreviewRefreshAction(
            second.status, second.frame_changed, true),
        ccz::editor_preview::PreviewRefreshAction::SkipRepaint);
    CCZ_REQUIRE_EQ(repaint_count, 1);
}

CCZ_TEST(invalid_preview_request_requires_native_scene_clear) {
    using ccz::editor_preview::DecidePreviewRefreshAction;
    using ccz::editor_preview::PreviewRefreshAction;
    using ccz::editor_preview::RefreshStatus;
    CCZ_REQUIRE_EQ(DecidePreviewRefreshAction(
                       RefreshStatus::InvalidInput, false, true),
                   PreviewRefreshAction::ClearAndRepaint);
    CCZ_REQUIRE_EQ(DecidePreviewRefreshAction(
                       RefreshStatus::ReentrantRejected, false, true),
                   PreviewRefreshAction::ClearAndRepaint);
    CCZ_REQUIRE_EQ(DecidePreviewRefreshAction(
                       RefreshStatus::Updated, false, false),
                   PreviewRefreshAction::ClearAndRepaint);
}

CCZ_TEST(identical_overlay_models_do_not_request_repaint) {
    DialogueOverlayModel first;
    first.visible = true;
    first.speaker_label = L"甲";
    first.body = L"正文";
    DialogueOverlayModel second = first;
    CCZ_REQUIRE(ccz::preview::dialogue_overlay_model_equal(first, second));
}

CCZ_TEST(hidden_overlay_paints_opaque_empty_panel) {
    const CRect client(0, 0, 640, 400);
    ccz::editor_preview::OverlayLayout layout;
    CCZ_REQUIRE(ccz::editor_preview::CalculateDialogueOverlayLayout(client,
                                                                      layout));
    CCZ_REQUIRE(layout.panel.left >= client.left);
    CCZ_REQUIRE(layout.panel.top >= client.top);
    CCZ_REQUIRE(layout.panel.right <= client.right);
    CCZ_REQUIRE(layout.panel.bottom <= client.bottom);

    CDC dc;
    CCZ_REQUIRE(dc.CreateCompatibleDC(nullptr));
    CBitmap bitmap;
    CCZ_REQUIRE(bitmap.CreateBitmap(640, 400, 1, 32, nullptr));
    CBitmap* previous = dc.SelectObject(&bitmap);
    dc.FillSolidRect(client, RGB(200, 0, 0));
    Gdiplus::GdiplusStartupInput startup_input;
    ULONG_PTR startup_token = 0;
    CCZ_REQUIRE(Gdiplus::GdiplusStartup(&startup_token, &startup_input,
                                        nullptr) == Gdiplus::Ok);
    DialogueOverlayModel hidden;
    ccz::editor_preview::PaintDialogueOverlay(dc, layout, hidden);
    const COLORREF pixel = dc.GetPixel(layout.panel.left + 2,
                                       layout.panel.top + 2);
    CCZ_REQUIRE_EQ(GetRValue(pixel), static_cast<BYTE>(20));
    CCZ_REQUIRE_EQ(GetGValue(pixel), static_cast<BYTE>(20));
    CCZ_REQUIRE_EQ(GetBValue(pixel), static_cast<BYTE>(24));
    Gdiplus::GdiplusShutdown(startup_token);
    dc.SelectObject(previous);
}

CCZ_TEST(overlay_layout_keeps_all_subrectangles_inside_narrow_canvases) {
    const std::vector<CRect> clients = {
        CRect(0, 0, 24, 137), CRect(0, 0, 160, 152),
        CRect(0, 0, 161, 152), CRect(0, 0, 640, 152),
        CRect(5, 7, 166, 207),
    };
    for (const CRect& client : clients) {
        ccz::editor_preview::OverlayLayout layout;
        if (!ccz::editor_preview::CalculateDialogueOverlayLayout(client,
                                                                  layout)) {
            continue;
        }
        CCZ_REQUIRE(rect_inside(client, layout.panel));
        CCZ_REQUIRE(rect_inside(client, layout.portrait));
        CCZ_REQUIRE(rect_inside(client, layout.speaker));
        CCZ_REQUIRE(rect_inside(client, layout.body));
        CCZ_REQUIRE(rect_inside(client, layout.diagnostic));
    }
}
