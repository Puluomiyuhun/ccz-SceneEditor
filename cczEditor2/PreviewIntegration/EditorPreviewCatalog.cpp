#include "EditorPreviewCatalog.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ccz::editor_preview {
namespace {

constexpr std::uint64_t kMaximumCatalogBytes = 64ull * 1024ull * 1024ull;

std::wstring module_root_from_process_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length + 1 < buffer.size()) {
            const std::wstring module_path(buffer.data(), length);
            const std::size_t separator = module_path.find_last_of(L"\\/");
            return separator == std::wstring::npos
                ? std::wstring{} : module_path.substr(0, separator + 1);
        }
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

std::uint64_t file_revision(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard,
                              &attributes)) return 0;
    const std::uint64_t size =
        (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
        attributes.nFileSizeLow;
    const std::uint64_t write_time =
        (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime)
         << 32) |
        attributes.ftLastWriteTime.dwLowDateTime;
    return size ^ (write_time + 0x9E3779B97F4A7C15ull + (size << 6) +
                   (size >> 2));
}

bool read_catalog_file(const std::wstring& path,
                       std::vector<std::uint8_t>& bytes,
                       std::wstring& diagnostic) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        diagnostic = L"CczString.ini 不存在或无法打开：" + path;
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaximumCatalogBytes) {
        CloseHandle(file);
        diagnostic = L"CczString.ini 大小超出安全上限";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>((std::min)(
            bytes.size() - offset, static_cast<std::size_t>(1u << 20)));
        DWORD received = 0;
        if (!ReadFile(file, bytes.data() + offset, request, &received,
                      nullptr) || received == 0) {
            bytes.clear();
            CloseHandle(file);
            diagnostic = L"读取 CczString.ini 失败";
            return false;
        }
        offset += received;
    }
    CloseHandle(file);
    return true;
}

}  // namespace

ccz::preview::InstructionCatalog MakeEditorPreviewCatalog() {
    ccz::preview::InstructionCatalog catalog;
    catalog.valid = true;
    catalog.names = {
        {0x00, L"事件结束"}, {0x01, L"子事件设定"},
        {0x02, L"内部信息"},
        {0x03, L"else"}, {0x05, L"变量测试"},
        {0x0a, L"初始化局部变量"},
        {0x12, L"选择框"}, {0x13, L"case"},
        {0x14, L"对话"}, {0x15, L"对话2"}, {0x16, L"信息"},
        {0x17, L"场所名设定"}, {0x18, L"事件名称设定"},
        {0x1c, L"绘图"}, {0x20, L"头像状态"}, {0x24, L"CD音轨"},
        {0x27, L"背景显示"}, {0x29, L"地图头像显示"},
        {0x2a, L"地图头像移动"}, {0x2b, L"地图头像消失"},
        {0x2c, L"地图文字显示"}, {0x2f, L"清除人物"},
        {0x30, L"武将出现"}, {0x31, L"武将消失"},
        {0x32, L"武将移动"}, {0x33, L"武将转向"},
        {0x34, L"武将动作"}, {0x36, L"武将状态测试"},
        {0x37, L"钱、章节编号、忠奸测试"},
        {0x55, L"战场复活"}, {0x58, L"战场障碍"},
        {0x44, L"战斗初始化"}, {0x46, L"友军出场设定"},
        {0x47, L"敌军出场设定"}, {0x4b, L"我军出场设定"},
        {0x4c, L"隐藏武将出现"},
        {0x63, L"单挑对话"}, {0x69, L"旁白"},
        {0x79, L"变量测试"}, {0x7a, L"对话3"},
    };
    return catalog;
}

bool LoadEditorInstructionCatalogForModule(
    ccz::preview::InstructionCatalog& catalog,
    std::wstring* diagnostic) {
    catalog = {};
    if (diagnostic != nullptr) diagnostic->clear();
    const std::wstring module_root = module_root_from_process_path();
    if (module_root.empty()) {
        if (diagnostic != nullptr)
            *diagnostic = L"无法从编辑器程序路径定位 CczString.ini";
        return false;
    }
    const std::wstring catalog_path = module_root + L"CczString.ini";
    struct CachedModuleCatalog {
        std::wstring path;
        std::uint64_t revision{};
        bool initialized{};
        ccz::preview::InstructionCatalog value;
    };
    static std::mutex cache_mutex;
    static CachedModuleCatalog cached;
    const std::uint64_t revision = file_revision(catalog_path);
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (cached.initialized && cached.path == catalog_path &&
        cached.revision == revision) {
        catalog = cached.value;
        if (!catalog.valid && diagnostic != nullptr)
            *diagnostic = catalog.diagnostic;
        return catalog.valid;
    }

    std::vector<std::uint8_t> bytes;
    std::wstring read_diagnostic;
    if (!read_catalog_file(catalog_path, bytes, read_diagnostic)) {
        catalog.diagnostic = std::move(read_diagnostic);
    } else {
        catalog = ccz::preview::parse_instruction_catalog(bytes);
    }
    cached.path = catalog_path;
    cached.revision = revision;
    cached.initialized = true;
    cached.value = catalog;
    if (!catalog.valid && diagnostic != nullptr)
        *diagnostic = catalog.diagnostic;
    return catalog.valid;
}

}  // namespace ccz::editor_preview
