#include "PreviewAssetCache.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <ccz/preview/text_color.h>

namespace ccz::editor_preview {
namespace {

constexpr std::uint64_t kMaximumAssetBytes = 64ull * 1024ull * 1024ull;

std::wstring source_root_from_document_path(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }
    // Keep this rule byte-for-byte equivalent to CScenePreviewWnd::GetRootPath:
    // the last non-RS separator is the Mod root, and an RS segment is skipped.
    for (std::size_t index = path.size(); index-- > 0;) {
        if (path[index] != L'\\' && path[index] != L'/') {
            continue;
        }
        if (index >= 2 &&
            (path[index - 1] == L'S' || path[index - 1] == L's') &&
            (path[index - 2] == L'R' || path[index - 2] == L'r')) {
            continue;
        }
        return path.substr(0, index + 1);
    }
    return {};
}

std::wstring module_root_from_process_path() {
    // The native editor resolves CczString.ini relative to the running
    // editor module, not relative to the active Mod/document.  Use a growing
    // buffer so a long installation path cannot silently produce a truncated
    // sibling path.
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length + 1 < buffer.size()) {
            const std::wstring module_path(buffer.data(), length);
            const std::size_t separator = module_path.find_last_of(L"\\/");
            return separator == std::wstring::npos
                ? std::wstring{}
                : module_path.substr(0, separator + 1);
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring normalize_root_key(std::wstring root) {
    std::replace(root.begin(), root.end(), L'/', L'\\');
    while (root.size() > 3 && !root.empty() && root.back() == L'\\') {
        root.pop_back();
    }
    for (wchar_t& character : root) {
        character = std::towlower(character);
    }
    return root;
}

std::size_t minimum_data_bytes(const ccz::preview::DialogueProfile& profile) {
    const std::uint64_t record_end =
        static_cast<std::uint64_t>(profile.data_record_base) +
        static_cast<std::uint64_t>(profile.data_record_stride);
    const std::uint64_t avatar_end =
        static_cast<std::uint64_t>(profile.data_record_base) +
        static_cast<std::uint64_t>(profile.avatar_offset) +
        static_cast<std::uint64_t>(profile.avatar_width);
    const std::uint64_t r_image_end =
        static_cast<std::uint64_t>(profile.data_record_base) +
        static_cast<std::uint64_t>(profile.r_image_offset) +
        static_cast<std::uint64_t>(profile.r_image_width);
    const std::uint64_t required =
        (std::max)({record_end, avatar_end, r_image_end, 1ull});
    return required > static_cast<std::uint64_t>(SIZE_MAX)
        ? SIZE_MAX : static_cast<std::size_t>(required);
}

bool read_file_bytes(const std::wstring& path,
                     std::vector<std::uint8_t>& bytes,
                     std::wstring& diagnostic) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        diagnostic = L"文件不存在或无法打开：" + path;
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaximumAssetBytes) {
        CloseHandle(file);
        diagnostic = L"资源文件大小超出安全上限：" + path;
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>((std::min)(
            bytes.size() - offset, static_cast<std::size_t>(1u << 20)));
        DWORD received = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &received, nullptr) ||
            received == 0) {
            bytes.clear();
            CloseHandle(file);
            diagnostic = L"读取资源文件失败：" + path;
            return false;
        }
        offset += received;
    }
    CloseHandle(file);
    return true;
}

void append_diagnostic(std::wstring& target, const std::wstring& value) {
    if (value.empty()) {
        return;
    }
    if (!target.empty()) {
        target += L"；";
    }
    target += value;
}

std::uint64_t catalog_file_revision(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard,
                              &attributes)) {
        return 0;
    }
    const std::uint64_t size =
        (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
        attributes.nFileSizeLow;
    const std::uint64_t write_time =
        (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime)
         << 32) |
        attributes.ftLastWriteTime.dwLowDateTime;
    // Size and last-write time are the same lightweight revision probe used by
    // the old DLL.  Mixing both halves avoids stale catalog reuse when a file
    // is edited in place without changing its length.
    return size ^ (write_time + 0x9E3779B97F4A7C15ull + (size << 6) +
                   (size >> 2));
}

}  // namespace

bool PreviewAssetCache::LoadForDocument(
    const std::wstring& document_path,
    const ccz::preview::DialogueProfile& profile,
    std::wstring* diagnostic) {
    const std::wstring root = source_root_from_document_path(document_path);
    if (root.empty()) {
        Clear();
        diagnostic_ = L"无法从当前剧本路径确定 Mod 根目录";
        if (diagnostic != nullptr) {
            *diagnostic = diagnostic_;
        }
        return false;
    }

    const std::wstring root_key = normalize_root_key(root);
    const std::wstring module_root = module_root_from_process_path();
    const std::wstring catalog_path = module_root.empty()
        ? std::wstring{}
        : module_root + L"CczString.ini";
    const std::uint64_t current_catalog_revision =
        catalog_path.empty() ? 0 : catalog_file_revision(catalog_path);

    // Assets are immutable for one document root during a refresh session.
    // This also guarantees Face.e5 is parsed at most once per root.
    if (root_key == root_key_ && catalog_revision_valid_ &&
        current_catalog_revision == catalog_revision_) {
        if (diagnostic != nullptr) {
            *diagnostic = diagnostic_;
        }
    return !data_e5_.empty() || face_archive_.valid;
    }

    Clear();
    root_key_ = root_key;
    catalog_revision_ = current_catalog_revision;
    catalog_revision_valid_ = true;
    const std::wstring data_path = root + L"Data.e5";
    const std::wstring face_path = root + L"E5\\Face.e5";

    std::wstring read_diagnostic;
    std::vector<std::uint8_t> catalog_bytes;
    read_diagnostic.clear();
    if (!catalog_path.empty() &&
        read_file_bytes(catalog_path, catalog_bytes, read_diagnostic)) {
        instruction_catalog_ = ccz::preview::parse_instruction_catalog(
            catalog_bytes);
        if (!instruction_catalog_.valid) {
            append_diagnostic(diagnostic_, instruction_catalog_.diagnostic);
        }
    } else {
        append_diagnostic(diagnostic_, L"CczString.ini 未加载");
    }

    read_diagnostic.clear();
    if (!read_file_bytes(data_path, data_e5_, read_diagnostic) ||
        data_e5_.size() < minimum_data_bytes(profile)) {
        data_e5_.clear();
        append_diagnostic(diagnostic_, L"Data.e5 未加载");
    }

    read_diagnostic.clear();
    if (!read_file_bytes(face_path, face_e5_, read_diagnostic)) {
        append_diagnostic(diagnostic_, L"Face.e5 未加载");
    } else {
        face_archive_ = ccz::preview::parse_face_archive(face_e5_, profile);
        if (!face_archive_.valid) {
            append_diagnostic(diagnostic_, face_archive_.diagnostic);
            face_e5_.clear();
            face_archive_ = {};
        }
    }

    if (diagnostic != nullptr) {
        *diagnostic = diagnostic_;
    }
    std::vector<std::uint8_t> engineBytes;std::wstring paletteDiagnostic;
    if(read_file_bytes(root+L"Ekd5.exe",engineBytes,paletteDiagnostic)) text_palette_=ccz::preview::extract_text_palette(engineBytes);
    std::wstring fbDiagnostic;
    if(read_file_bytes(root + L"E5\\fb.e5",fb_e5_,fbDiagnostic)) fb_archive_=ccz::preview::parse_face_archive(fb_e5_,profile);
    return !data_e5_.empty() || face_archive_.valid;
}

PreviewAssetsView PreviewAssetCache::view() const noexcept {
    PreviewAssetsView result;
    result.data_e5 = data_e5_.empty() ? nullptr : &data_e5_;
    result.face_e5 = face_e5_.empty() ? nullptr : &face_e5_;
    result.face_archive = face_archive_.valid ? &face_archive_ : nullptr;
    result.fb_e5=fb_e5_.empty()?nullptr:&fb_e5_;
    result.fb_archive=fb_archive_.valid?&fb_archive_:nullptr;
    result.text_palette=text_palette_.empty()?nullptr:&text_palette_;
    result.diagnostic = diagnostic_.empty() ? nullptr : &diagnostic_;
    return result;
}

const ccz::preview::InstructionCatalog&
PreviewAssetCache::instruction_catalog() const noexcept {
    return instruction_catalog_;
}

void PreviewAssetCache::Clear() {
    root_key_.clear();
    catalog_revision_ = 0;
    catalog_revision_valid_ = false;
    data_e5_.clear();
    face_e5_.clear();
    face_archive_ = {};
    fb_e5_.clear();fb_archive_={};text_palette_.clear();
    instruction_catalog_ = {};
    diagnostic_.clear();
}

}  // namespace ccz::editor_preview
