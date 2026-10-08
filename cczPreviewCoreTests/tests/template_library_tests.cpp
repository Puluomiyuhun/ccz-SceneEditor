#include "../../cczEditor2/IndexedComboSearch.h"
#include "../../cczEditor2/TemplateLibrary/TemplateStore.h"
#include "test_framework.h"
#include <fstream>

namespace {
ccz::templates::Entry Sample() {
  ccz::templates::Entry e;
  e.uid = L"test";
  e.name = L"战前模板";
  e.notes = L"备注";
  ccz::templates::Node n;
  n.id = 0x14;
  n.key = 0;
  n.params.resize(20);
  n.params[0] = 1023;
  n.has_text = true;
  n.text = L"文本\n完整保留";
  n.label = L"14:对话";
  e.roots.push_back(n);
  return e;
}
bool Rejects(const ccz::templates::Library &l) {
  try {
    ccz::templates::Encode(l);
    return false;
  } catch (...) {
    return true;
  }
}
struct TempFolder {
  std::wstring path;
  TempFolder() {
    wchar_t base[MAX_PATH]{};
    GetTempPathW(MAX_PATH, base);
    path = std::wstring(base) + ccz::templates::NewIdentity();
    CreateDirectoryW(path.c_str(), nullptr);
  }
  ~TempFolder() {
    for (auto &f : {L"library.czt", L"library.bak", L"library.lock"})
      DeleteFileW((path + L"\\" + f).c_str());
    RemoveDirectoryW(path.c_str());
  }
};
} // namespace
CCZ_TEST(template_roundtrip_preserves_text_params_and_tree) {
  auto e = Sample();
  e.roots[0].text = std::wstring(15000, L'中') + L"\n结尾";
  ccz::templates::Node end;
  end.id = 0;
  end.key = 1;
  end.params.resize(20);
  e.roots[0].children.push_back(end);
  ccz::templates::Library lib;
  lib.entries.push_back(e);
  auto copy = ccz::templates::Decode(ccz::templates::Encode(lib));
  CCZ_REQUIRE_EQ(copy.entries[0].roots[0].text, e.roots[0].text);
  CCZ_REQUIRE_EQ(copy.entries[0].roots[0].params, e.roots[0].params);
  CCZ_REQUIRE_EQ(copy.entries[0].roots[0].children.size(), 1u);
}
CCZ_TEST(template_internal_jump_requires_existing_local_target) {
  auto e = Sample();
  ccz::templates::Node jump = e.roots[0];
  jump.id = 0x76;
  jump.key = 1;
  jump.jump_target = 0;
  e.roots.push_back(jump);
  ccz::templates::Library lib;
  lib.entries.push_back(e);
  CCZ_REQUIRE(!Rejects(lib));
  lib.entries[0].roots[1].jump_target = 999;
  CCZ_REQUIRE(Rejects(lib));
  lib.entries[0].roots[1].jump_target = -1;
  CCZ_REQUIRE(Rejects(lib));
}
CCZ_TEST(template_damaged_or_future_file_is_rejected) {
  ccz::templates::Library lib;
  lib.entries.push_back(Sample());
  auto bytes = ccz::templates::Encode(lib);
  bytes[30] ^= 1;
  bool rejected = false;
  try {
    ccz::templates::Decode(bytes);
  } catch (...) {
    rejected = true;
  }
  CCZ_REQUIRE(rejected);
  bytes = ccz::templates::Encode(lib);
  bytes[4] = 2;
  rejected = false;
  try {
    ccz::templates::Decode(bytes);
  } catch (...) {
    rejected = true;
  }
  CCZ_REQUIRE(rejected);
  bytes.resize(20);
  rejected = false;
  try {
    ccz::templates::Decode(bytes);
  } catch (...) {
    rejected = true;
  }
  CCZ_REQUIRE(rejected);
}
CCZ_TEST(template_rejects_mixed_levels_and_orphan_marker) {
  ccz::templates::Library lib;
  lib.entries.push_back(Sample());
  lib.entries[0].roots[0].id = -2;
  CCZ_REQUIRE(Rejects(lib));
  lib.entries[0].level = ccz::templates::Level::Section;
  CCZ_REQUIRE(!Rejects(lib));
  lib.entries[0] = Sample();
  lib.entries[0].roots[0].id = 1;
  CCZ_REQUIRE(Rejects(lib));
}
CCZ_TEST(template_stores_extended_integer_arrays) {
  auto e = Sample();
  e.roots[0].params.resize(1000);
  for (int i = 0; i < 1000; ++i)
    e.roots[0].params[i] = i - 500;
  ccz::templates::Library lib;
  lib.entries.push_back(e);
  auto decoded = ccz::templates::Decode(ccz::templates::Encode(lib));
  CCZ_REQUIRE_EQ(decoded.entries[0].roots[0].params, e.roots[0].params);
}
CCZ_TEST(template_rejects_short_extended_instruction_parameters) {
  auto e = Sample();
  e.roots[0].id = 70;
  ccz::templates::Library lib;
  lib.entries.push_back(e);
  CCZ_REQUIRE(Rejects(lib));
  lib.entries[0].roots[0].params.resize(1000);
  CCZ_REQUIRE(!Rejects(lib));
}
CCZ_TEST(template_missing_main_does_not_erase_existing_backup) {
  TempFolder dir;
  ccz::templates::Store store(dir.path);
  auto e = Sample();
  ccz::templates::Library lib;
  std::wstring diagnostic;
  CCZ_REQUIRE(store.Save(e, false, lib, diagnostic));
  e.name = L"新版";
  CCZ_REQUIRE(store.Save(e, true, lib, diagnostic));
  DeleteFileW((dir.path + L"\\library.czt").c_str());
  CCZ_REQUIRE(store.Load(lib, diagnostic));
  CCZ_REQUIRE(!diagnostic.empty());
  CCZ_REQUIRE_EQ(lib.entries[0].name, L"战前模板");
  CCZ_REQUIRE(!store.Save(Sample(), false, lib, diagnostic));
}
CCZ_TEST(template_store_reloads_each_write_and_detects_stale_edit) {
  TempFolder dir;
  ccz::templates::Store first(dir.path), second(dir.path);
  ccz::templates::Library lib;
  std::wstring diagnostic;
  auto one = Sample();
  CCZ_REQUIRE(first.Save(one, false, lib, diagnostic));
  auto two = one;
  two.uid = L"second";
  two.name = L"第二份";
  CCZ_REQUIRE(second.Save(two, false, lib, diagnostic));
  CCZ_REQUIRE_EQ(lib.entries.size(), 2u);
  one.name = L"修改";
  CCZ_REQUIRE(first.Save(one, true, lib, diagnostic));
  CCZ_REQUIRE(!second.Save(one, true, lib, diagnostic));
  CCZ_REQUIRE(!diagnostic.empty());
  CCZ_REQUIRE(second.Load(lib, diagnostic));
  CCZ_REQUIRE_EQ(lib.entries[0].name, L"修改");
}
CCZ_TEST(template_store_corruption_retains_good_backup_and_refuses_overwrite) {
  TempFolder dir;
  ccz::templates::Store store(dir.path);
  auto e = Sample();
  ccz::templates::Library lib;
  std::wstring diagnostic;
  CCZ_REQUIRE(store.Save(e, false, lib, diagnostic));
  e.name = L"新版";
  CCZ_REQUIRE(store.Save(e, true, lib, diagnostic));
  {
    std::ofstream file(dir.path + L"\\library.czt",
                       std::ios::binary | std::ios::trunc);
    file << "bad";
  }
  CCZ_REQUIRE(store.Load(lib, diagnostic));
  CCZ_REQUIRE(!diagnostic.empty());
  CCZ_REQUIRE_EQ(lib.entries[0].name, L"战前模板");
  auto next = Sample();
  next.uid = L"third";
  CCZ_REQUIRE(!store.Save(next, false, lib, diagnostic));
  std::ifstream file(dir.path + L"\\library.czt", std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(file)), {});
  CCZ_REQUIRE_EQ(bytes, "bad");
}
CCZ_TEST(template_store_honors_cross_instance_file_lock) {
  TempFolder dir;
  ccz::templates::Store store(dir.path);
  HANDLE file = CreateFileW(
      (dir.path + L"\\library.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
  OVERLAPPED over{};
  CCZ_REQUIRE(LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &over));
  ccz::templates::Library lib;
  std::wstring diagnostic;
  bool loaded = store.Load(lib, diagnostic);
  UnlockFileEx(file, 0, 1, 0, &over);
  CloseHandle(file);
  CCZ_REQUIRE(!loaded);
  CCZ_REQUIRE(store.Save(Sample(), false, lib, diagnostic));
}
CCZ_TEST(instruction_combo_hex_input_does_not_change_person_decimal) {
  std::vector<ccz::editor::IndexedOption> options;
  for (int i = 0; i < 124; ++i) {
    ccz::editor::IndexedOption o;
    o.label = ccz::editor::HexCode(i) + L":名称";
    o.selection_value = i;
    o.numeric_code = i;
    o.hexadecimal = true;
    options.push_back(o);
  }
  options[0x55].label = L"55:战场复活";
  int value = -1;
  CCZ_REQUIRE(ccz::editor::ResolveIndexedCode(options, L"14", value));
  CCZ_REQUIRE_EQ(value, 0x14);
  CCZ_REQUIRE(ccz::editor::ResolveIndexedCode(options, L"4D", value));
  CCZ_REQUIRE_EQ(value, 0x4d);
  CCZ_REQUIRE(ccz::editor::ResolveIndexedCode(options, L"0x58", value));
  CCZ_REQUIRE_EQ(value, 0x58);
  CCZ_REQUIRE(!ccz::editor::ResolveIndexedCode(options, L"0xGG", value));
  auto filtered = ccz::editor::FilterIndexedOptions(options, L"复活");
  CCZ_REQUIRE_EQ(filtered.size(), 1u);
  CCZ_REQUIRE_EQ(options[filtered[0]].selection_value, 0x55);
  for (auto &o : options)
    o.hexadecimal = false;
  CCZ_REQUIRE(ccz::editor::ResolveIndexedCode(options, L"14", value));
  CCZ_REQUIRE_EQ(value, 14);
}
