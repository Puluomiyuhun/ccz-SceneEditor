#pragma once
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <cerrno>
#include <cwchar>
namespace ccz::editor {
inline bool ParseTransferTarget(const std::wstring& text, const std::set<int>& ids, int& value) {
  if (text.empty()) return false;
  for (wchar_t c : text) if (c < L'0' || c > L'9') return false;
  wchar_t* end = nullptr; errno = 0;
  long number = wcstol(text.c_str(), &end, 10);
  if (!end || *end || errno == ERANGE || number < 0 || number >= 200000 || !ids.count((int)number)) return false;
  value = (int)number; return true;
}
inline int RemapTransferredJump(int target, const std::map<int,int>& internal,
                                const std::map<int,int>& external) {
  auto i = internal.find(target);
  if (i != internal.end()) return i->second;
  return external.at(target);
}
}
