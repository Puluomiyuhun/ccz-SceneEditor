#pragma once
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccz::templates {
enum class Level : int { Instructions, Section, Scene };
inline const wchar_t *LevelName(Level level) {
  return level == Level::Scene     ? L"Scene"
         : level == Level::Section ? L"Section"
                                   : L"指令组";
}
struct Node {
  int id = 0;
  int key = -1; // Template-local identity; independent of native ordinals.
  int jump_target = -1;
  std::vector<int> params;
  std::wstring text, label;
  bool has_text = false;
  std::vector<Node> children;
};
struct Entry {
  std::wstring uid, name, notes;
  std::uint64_t revision = 1;
  Level level = Level::Instructions;
  std::vector<Node> roots;
};
struct Library {
  std::uint64_t revision = 0;
  std::vector<Entry> entries;
};
constexpr std::size_t MaxBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxNodes = 100000;

inline void ValidateForest(const std::vector<Node> &forest, std::size_t depth,
                           std::size_t &count, std::set<int> &keys,
                           std::vector<int> &jumps) {
  if (depth > 64)
    throw std::runtime_error("template nesting exceeds limit");
  for (const auto &node : forest) {
    if (++count > MaxNodes || node.id < -2 || node.id > 123 || node.key < 0 ||
        !keys.insert(node.key).second ||
        node.params.size() < (node.id == 70 || node.id == 71 ? 1000u
                              : node.id == 5                 ? 60u
                                                             : 20u) ||
        node.params.size() > 1000 || node.text.size() > 1000000 ||
        node.label.size() > 20000)
      throw std::runtime_error("invalid template node");
    if (node.id == 0x76) {
      if (node.jump_target < -1)
        throw std::runtime_error("external template jump");
      if (node.jump_target >= 0) jumps.push_back(node.jump_target);
    } else if (node.jump_target != -1)
      throw std::runtime_error("unexpected jump target");
    if (!node.children.empty()) {
      if (node.id == -1) {
        for (auto &child : node.children)
          if (child.id != -2)
            throw std::runtime_error("Scene requires Sections");
      } else if (node.id == -2) {
        for (auto &child : node.children)
          if (child.id < 0)
            throw std::runtime_error("Section requires instructions");
      } else {
        for (auto &child : node.children)
          if (child.id < 0)
            throw std::runtime_error("event requires instructions");
        if (node.children.back().id != 0 ||
            !node.children.back().children.empty())
          throw std::runtime_error("event is missing terminator");
      }
    }
    // 01 and its following event root are a structural pair.
    for (std::size_t i = 0; i < node.children.size(); ++i) {
      const auto &child = node.children[i];
      if (child.id == 1 && (i + 1 == node.children.size() ||
                            node.children[i + 1].children.empty() ||
                            node.children[i + 1].id <= 0))
        throw std::runtime_error("orphan subevent marker");
    }
    ValidateForest(node.children, depth + 1, count, keys, jumps);
  }
}
inline void Validate(const Entry &entry) {
  if (entry.uid.empty() || entry.name.empty() || entry.uid.size() > 128 ||
      entry.name.size() > 256 || entry.notes.size() > 4096 ||
      entry.roots.empty() || entry.level < Level::Instructions ||
      entry.level > Level::Scene)
    throw std::runtime_error("invalid template metadata");
  const int rootId = entry.level == Level::Scene     ? -1
                     : entry.level == Level::Section ? -2
                                                     : 2;
  for (auto &node : entry.roots)
    if ((rootId < 0 && node.id != rootId) || (rootId >= 0 && node.id < 0))
      throw std::runtime_error("mixed template levels");
  std::set<int> keys;
  std::vector<int> jumps;
  std::size_t count = 0;
  ValidateForest(entry.roots, 0, count, keys, jumps);
  for (int target : jumps)
    if (!keys.count(target))
      throw std::runtime_error("missing internal jump target");
  for (std::size_t i = 0; i < entry.roots.size(); ++i) {
    auto &node = entry.roots[i];
    if (node.id == 1 &&
        (i + 1 == entry.roots.size() || entry.roots[i + 1].id <= 0 ||
         entry.roots[i + 1].children.empty()))
      throw std::runtime_error("orphan root marker");
    if (entry.level == Level::Instructions && node.id == 0)
      throw std::runtime_error("standalone event boundary");
  }
}

// Explicit little-endian words and UTF-16 code units. Decode is bounded and
// commits only after the entire file, checksum and every entry are validated.
class Writer {
public:
  std::vector<unsigned char> bytes;
  void Word(std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
  }
  void Long(std::uint64_t value) {
    Word(static_cast<std::uint32_t>(value));
    Word(static_cast<std::uint32_t>(value >> 32));
  }
  void Text(const std::wstring &value) {
    Word(static_cast<std::uint32_t>(value.size()));
    for (auto ch : value)
      Word(static_cast<std::uint32_t>(ch));
  }
};
class Reader {
public:
  const std::vector<unsigned char> &bytes;
  std::size_t pos = 0;
  explicit Reader(const std::vector<unsigned char> &value) : bytes(value) {}
  std::uint32_t Word() {
    if (bytes.size() - pos < 4)
      throw std::runtime_error("truncated template library");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= std::uint32_t(bytes[pos++]) << (8 * i);
    return v;
  }
  std::uint64_t Long() {
    auto lo = Word();
    return lo | (std::uint64_t(Word()) << 32);
  }
  std::size_t Count(std::size_t limit) {
    auto n = Word();
    if (n > limit)
      throw std::runtime_error("template limit exceeded");
    return n;
  }
  std::wstring Text(std::size_t limit) {
    std::wstring v;
    auto n = Count(limit);
    if (n > (bytes.size() - pos) / 4)
      throw std::runtime_error("truncated text");
    v.reserve(n);
    while (n--) {
      auto c = Word();
      if (!c || c > 65535)
        throw std::runtime_error("invalid text unit");
      v.push_back(static_cast<wchar_t>(c));
    }
    return v;
  }
};
inline std::uint64_t Checksum(const std::vector<unsigned char> &data,
                              std::size_t n) {
  std::uint64_t h = 14695981039346656037ULL;
  for (std::size_t i = 0; i < n; ++i) {
    h ^= data[i];
    h *= 1099511628211ULL;
  }
  return h;
}
inline void WriteNodes(Writer &out, const std::vector<Node> &nodes) {
  out.Word(static_cast<std::uint32_t>(nodes.size()));
  for (auto &n : nodes) {
    out.Word(n.id);
    out.Word(n.key);
    out.Word(n.jump_target);
    out.Word(n.has_text ? 1 : 0);
    out.Word(static_cast<std::uint32_t>(n.params.size()));
    for (int p : n.params)
      out.Word(p);
    out.Text(n.text);
    out.Text(n.label);
    WriteNodes(out, n.children);
  }
}
inline std::vector<Node> ReadNodes(Reader &in, std::size_t depth,
                                   std::size_t &total) {
  if (depth > 64)
    throw std::runtime_error("excessive depth");
  std::vector<Node> nodes;
  auto count = in.Count(MaxNodes);
  if (count > MaxNodes - total)
    throw std::runtime_error("excessive nodes");
  total += count;
  nodes.reserve(count);
  while (count--) {
    Node n;
    n.id = static_cast<int>(in.Word());
    n.key = static_cast<int>(in.Word());
    n.jump_target = static_cast<int>(in.Word());
    n.has_text = in.Count(1) != 0;
    auto size = in.Count(1000);
    while (size--)
      n.params.push_back(static_cast<int>(in.Word()));
    n.text = in.Text(1000000);
    n.label = in.Text(20000);
    n.children = ReadNodes(in, depth + 1, total);
    nodes.push_back(std::move(n));
  }
  return nodes;
}
inline std::vector<unsigned char> Encode(const Library &library) {
  if (library.entries.size() > 10000)
    throw std::runtime_error("excessive entries");
  std::size_t total = 0;
  Writer out;
  out.Word(0x544C5A43);
  out.Word(1);
  out.Long(library.revision);
  out.Word(static_cast<std::uint32_t>(library.entries.size()));
  std::set<std::wstring> ids;
  for (auto &e : library.entries) {
    Validate(e);
    {
      std::set<int> keys;
      std::vector<int> jumps;
      ValidateForest(e.roots, 0, total, keys, jumps);
    }
    if (!ids.insert(e.uid).second)
      throw std::runtime_error("duplicate template identity");
    out.Text(e.uid);
    out.Long(e.revision);
    out.Text(e.name);
    out.Text(e.notes);
    out.Word(static_cast<int>(e.level));
    WriteNodes(out, e.roots);
    if (out.bytes.size() > MaxBytes - 8)
      throw std::runtime_error("library too large");
  }
  out.Long(Checksum(out.bytes, out.bytes.size()));
  return out.bytes;
}
inline Library Decode(const std::vector<unsigned char> &bytes) {
  if (bytes.size() < 28 || bytes.size() > MaxBytes)
    throw std::runtime_error("invalid library size");
  Reader in(bytes);
  if (in.Word() != 0x544C5A43 || in.Word() != 1)
    throw std::runtime_error("unsupported template library version");
  Library lib;
  lib.revision = in.Long();
  auto count = in.Count(10000);
  std::set<std::wstring> ids;
  std::size_t total = 0;
  while (count--) {
    Entry e;
    e.uid = in.Text(128);
    e.revision = in.Long();
    e.name = in.Text(256);
    e.notes = in.Text(4096);
    e.level = static_cast<Level>(in.Word());
    e.roots = ReadNodes(in, 0, total);
    Validate(e);
    if (!ids.insert(e.uid).second)
      throw std::runtime_error("duplicate identity");
    lib.entries.push_back(std::move(e));
  }
  auto sum = in.Long();
  if (in.pos != bytes.size() || sum != Checksum(bytes, bytes.size() - 8))
    throw std::runtime_error("template checksum mismatch");
  return lib;
}
} // namespace ccz::templates
