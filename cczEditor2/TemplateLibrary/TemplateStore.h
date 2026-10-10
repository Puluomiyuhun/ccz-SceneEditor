#pragma once
#include "TemplateModel.h"
#include <functional>
#include <shlobj.h>
#include <windows.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace ccz::templates {
inline std::wstring NewIdentity() {
  GUID guid{};
  if (FAILED(CoCreateGuid(&guid)))
    throw std::runtime_error("GUID creation failed");
  wchar_t text[40]{};
  StringFromGUID2(guid, text, 40);
  return text;
}
class FileHandle {
public:
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit FileHandle(HANDLE h) : value(h) {}
  ~FileHandle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
  FileHandle(const FileHandle &) = delete;
};
class Store {
public:
  explicit Store(std::wstring folder = L"") : folder_(std::move(folder)) {}
  std::wstring Folder() {
    EnsureFolder();
    return folder_;
  }
  std::pair<std::uint64_t, std::uint64_t> Stamp() {
    EnsureFolder();
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW((folder_ + L"\\library.czt").c_str(),
                              GetFileExInfoStandard, &info))
      return {};
    return {(std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) |
                info.ftLastWriteTime.dwLowDateTime,
            (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow};
  }
  bool Load(Library &library, std::wstring &diagnostic) {
    return Locked([&] { return ReadCurrent(library, diagnostic); }, diagnostic);
  }
  // Mutations always reload while holding the same cross-process lock.
  bool Save(Entry entry, bool replacing, Library &result,
            std::wstring &diagnostic) {
    return Locked(
        [&] {
          Library current;
          if (!ReadCurrent(current, diagnostic) || !diagnostic.empty())
            return false;
          auto found =
              std::find_if(current.entries.begin(), current.entries.end(),
                           [&](auto &e) { return e.uid == entry.uid; });
          if (replacing) {
            if (found == current.entries.end() ||
                found->revision != entry.revision) {
              diagnostic = L"模板已被另一个编辑器修改，请刷新后重试";
              return false;
            }
            ++entry.revision;
            *found = std::move(entry);
          } else {
            if (found != current.entries.end()) {
              diagnostic = L"模板编号冲突，请重试";
              return false;
            }
            current.entries.push_back(std::move(entry));
          }
          ++current.revision;
          if (!Write(current, diagnostic))
            return false;
          result = std::move(current);
          return true;
        },
        diagnostic);
  }
  bool Remove(const Entry &entry, Library &result, std::wstring &diagnostic) {
    return Locked(
        [&] {
          Library current;
          if (!ReadCurrent(current, diagnostic) || !diagnostic.empty())
            return false;
          auto found =
              std::find_if(current.entries.begin(), current.entries.end(),
                           [&](auto &e) { return e.uid == entry.uid; });
          if (found == current.entries.end() ||
              found->revision != entry.revision) {
            diagnostic = L"模板已发生变化，请刷新后重试";
            return false;
          }
          current.entries.erase(found);
          ++current.revision;
          if (!Write(current, diagnostic))
            return false;
          result = std::move(current);
          return true;
        },
        diagnostic);
  }

private:
  std::wstring folder_;
  void EnsureFolder() {
    if (folder_.empty()) {
      PWSTR appdata = nullptr;
      if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                      &appdata)))
        throw std::runtime_error("AppData unavailable");
      folder_ = appdata;
      CoTaskMemFree(appdata);
      folder_ += L"\\cczSceneEditor2";
      MakeDirectory(folder_);
      folder_ += L"\\TemplateLibrary";
    }
    MakeDirectory(folder_);
  }
  static void MakeDirectory(const std::wstring &path) {
    if (!CreateDirectoryW(path.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
      throw std::runtime_error("cannot create template directory");
  }
  template <class F> bool Locked(F action, std::wstring &diagnostic) {
    diagnostic.clear();
    try {
      EnsureFolder();
      FileHandle lock(CreateFileW((folder_ + L"\\library.lock").c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
      OVERLAPPED over{};
      if (lock.value == INVALID_HANDLE_VALUE ||
          !LockFileEx(lock.value,
                      LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1,
                      0, &over)) {
        diagnostic = L"模板库正在被其他编辑器使用，稍后重试";
        return false;
      }
      struct Unlock {
        HANDLE h;
        OVERLAPPED *o;
        ~Unlock() { UnlockFileEx(h, 0, 1, 0, o); }
      } unlock{lock.value, &over};
      return action();
    } catch (...) {
      diagnostic = L"模板库读取或保存失败，请检查文件权限、格式及磁盘空间";
      return false;
    }
  }
  static bool Read(const std::wstring &path, Library &result, bool &missing) {
    missing = false;
    FileHandle f(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr));
    if (f.value == INVALID_HANDLE_VALUE) {
      missing = GetLastError() == ERROR_FILE_NOT_FOUND;
      if (missing)
        return false;
      throw std::runtime_error("cannot read library");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.value, &size) || size.QuadPart < 0 ||
        size.QuadPart > MaxBytes)
      throw std::runtime_error("invalid file size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(f.value, bytes.data(), static_cast<DWORD>(bytes.size()),
                  &read, nullptr) ||
        read != bytes.size())
      throw std::runtime_error("cannot read file");
    result = Decode(bytes);
    return true;
  }
  bool ReadCurrent(Library &result, std::wstring &diagnostic) {
    bool missing = false;
    try {
      if (Read(folder_ + L"\\library.czt", result, missing))
        return true;
      if (missing &&
          GetFileAttributesW((folder_ + L"\\library.bak").c_str()) ==
              INVALID_FILE_ATTRIBUTES &&
          GetLastError() == ERROR_FILE_NOT_FOUND) {
        result = {};
        return true;
      }
    } catch (...) {
    }
    try {
      bool backupMissing = false;
      if (Read(folder_ + L"\\library.bak", result, backupMissing)) {
        diagnostic = L"模板库主文件损坏，已展示备份；为保护数据暂不允许修改。请"
                     L"备份并修复 library.czt";
        return true;
      }
    } catch (...) {
    }
    diagnostic = L"模板库文件损坏、版本不兼容或无法读取，原文件已保留";
    return false;
  }
  bool Write(const Library &library, std::wstring &diagnostic) {
    auto bytes = Encode(library);
    auto temp = folder_ + L"\\" + NewIdentity() + L".tmp";
    bool ok = false;
    {
      FileHandle f(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
      DWORD written = 0;
      ok = f.value != INVALID_HANDLE_VALUE &&
           WriteFile(f.value, bytes.data(), static_cast<DWORD>(bytes.size()),
                     &written, nullptr) &&
           written == bytes.size() && FlushFileBuffers(f.value);
    }
    if (ok) {
      auto main = folder_ + L"\\library.czt";
      auto backup = folder_ + L"\\library.bak";
      if (GetFileAttributesW(main.c_str()) == INVALID_FILE_ATTRIBUTES)
        ok = MoveFileExW(temp.c_str(), main.c_str(), MOVEFILE_WRITE_THROUGH) !=
             FALSE;
      else
        ok = ReplaceFileW(main.c_str(), temp.c_str(), backup.c_str(), 0,
                          nullptr, nullptr) != FALSE;
    }
    if (!ok) {
      DeleteFileW(temp.c_str());
      diagnostic = L"模板库保存失败，原有模板未更改";
    }
    return ok;
  }
};
} // namespace ccz::templates
