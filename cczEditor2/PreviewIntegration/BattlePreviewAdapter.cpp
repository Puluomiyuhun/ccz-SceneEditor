#include "BattlePreviewAdapter.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ccz::editor_preview {
namespace {

constexpr std::size_t kMaximumTreeDepth = 128;
constexpr std::size_t kMaximumCapturedNodes = 100000;
constexpr std::uint16_t kInvalidInstructionId = 0xffff;

std::wstring NormalizeDocumentPath(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 1 && path.back() == L'\\') path.pop_back();
    return path;
}

class RevisionHasher {
public:
    void AddByte(std::uint8_t value) {
        hash_ ^= value;
        hash_ *= 1099511628211ull;
    }

    void AddU64(std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            AddByte(static_cast<std::uint8_t>(value & 0xffu));
            value >>= 8;
        }
    }

    void AddString(const std::wstring& value) {
        AddU64(static_cast<std::uint64_t>(value.size()));
        for (wchar_t character : value)
            AddU64(static_cast<std::uint64_t>(character));
    }

    std::uint64_t Value() const { return hash_; }

private:
    std::uint64_t hash_ = 1469598103934665603ull;
};

void HashCapturedNode(RevisionHasher& hasher,
                     const CapturedEditorNode& node) {
    hasher.AddU64(static_cast<std::uint64_t>(node.instruction_id));
    hasher.AddU64(static_cast<std::uint64_t>(node.ordinal));
    hasher.AddU64(static_cast<std::uint64_t>(node.int_data.size()));
    for (int value : node.int_data) {
        hasher.AddU64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
    }
    hasher.AddString(node.long_text);
    hasher.AddString(node.display_text);
    hasher.AddU64(static_cast<std::uint64_t>(node.children.size()));
    for (const CapturedEditorNode& child : node.children)
        HashCapturedNode(hasher, child);
}

void HashCapturedRoots(RevisionHasher& hasher,
                       const std::vector<CapturedEditorNode>& roots) {
    hasher.AddU64(static_cast<std::uint64_t>(roots.size()));
    for (const CapturedEditorNode& root : roots)
        HashCapturedNode(hasher, root);
}

std::uint16_t EncodedInstructionId(int instruction_id) {
    if (instruction_id < 0 || instruction_id > 0xffff) {
        return kInvalidInstructionId;
    }
    return static_cast<std::uint16_t>(instruction_id);
}

struct ConversionBudget {
    std::size_t node_count{};
};

bool ConvertNode(const CapturedEditorNode& source,
                 const ccz::preview::LogicalNodeKey& key_base,
                 const std::vector<std::uint32_t>& path,
                 std::size_t depth, ConversionBudget& budget,
                 ccz::preview::RawReplayNode& destination,
                 std::wstring& diagnostic) {
    if (depth > kMaximumTreeDepth) {
        diagnostic = L"战场剧本树深度超出安全上限";
        return false;
    }
    if (budget.node_count >= kMaximumCapturedNodes) {
        diagnostic = L"战场剧本节点数量超出安全上限";
        return false;
    }
    ++budget.node_count;

    destination = {};
    destination.runtime_handle = source.runtime_handle;
    destination.instruction_id = EncodedInstructionId(source.instruction_id);
    destination.logical_key = ccz::preview::make_logical_node_key(
        key_base, path, destination.instruction_id);
    destination.command.instruction_id = destination.instruction_id;
    destination.command.int_data.assign(source.int_data.begin(),
                                        source.int_data.end());
    destination.command.text = source.long_text;
    destination.source_text = source.display_text;
    destination.children.reserve(source.children.size());

    for (std::size_t index = 0; index < source.children.size(); ++index) {
        std::vector<std::uint32_t> child_path = path;
        child_path.push_back(static_cast<std::uint32_t>(index));
        ccz::preview::RawReplayNode child;
        if (!ConvertNode(source.children[index], key_base, child_path,
                         depth + 1, budget, child, diagnostic)) {
            destination = {};
            return false;
        }
        destination.children.push_back(std::move(child));
    }
    return true;
}

bool ConvertRoots(const std::vector<CapturedEditorNode>& source,
                  const ccz::preview::LogicalNodeKey& key_base,
                  std::vector<ccz::preview::RawReplayNode>& destination,
                  std::wstring& diagnostic) {
    destination.clear();
    destination.reserve(source.size());
    ConversionBudget budget;
    for (std::size_t index = 0; index < source.size(); ++index) {
        ccz::preview::RawReplayNode node;
        if (!ConvertNode(source[index], key_base,
                         {static_cast<std::uint32_t>(index)}, 0, budget,
                         node, diagnostic)) {
            destination.clear();
            return false;
        }
        destination.push_back(std::move(node));
    }
    return true;
}

const ccz::preview::RawReplayNode* FindNodeByHandle(
    const std::vector<ccz::preview::RawReplayNode>& nodes,
    std::uintptr_t runtime_handle) {
    for (const auto& node : nodes) {
        if (node.runtime_handle == runtime_handle) return &node;
        if (const auto* found = FindNodeByHandle(node.children, runtime_handle))
            return found;
    }
    return nullptr;
}

ccz::preview::LogicalNodeKey ScopePrefix(
    const ccz::preview::RawReplayNode& node) {
    constexpr wchar_t marker[] = L"|instruction:";
    const std::size_t position = node.logical_key.rfind(marker);
    return position == std::wstring::npos
        ? ccz::preview::LogicalNodeKey{}
        : node.logical_key.substr(0, position);
}

}  // namespace

BattlePreviewCapture BuildBattlePreviewInput(
    const CapturedEditorTree& document_snapshot) {
    BattlePreviewCapture result;
    const std::wstring document_path =
        NormalizeDocumentPath(document_snapshot.document_path);
    if (document_path.empty()) {
        result.diagnostic = L"战场剧本文档路径为空";
        return result;
    }
    if (document_snapshot.section_path.empty()) {
        result.diagnostic = L"战场预览需要选中一个 Section 以建立作用域";
        return result;
    }
    result.snapshot = document_snapshot;
    if (document_snapshot.document_roots.empty()) {
        result.diagnostic = L"战场预览快照没有文档节点";
        return result;
    }

    result.input.document_key = document_path + L"|document";
    result.input.document_path = document_path;
    if (!ConvertRoots(document_snapshot.document_roots,
                      result.input.document_key, result.input.nodes,
                      result.diagnostic)) {
        return result;
    }

    const auto* section_node = FindNodeByHandle(
        result.input.nodes, document_snapshot.section_root.runtime_handle);
    if (section_node == nullptr) {
        result.diagnostic = L"战场预览快照找不到当前 Section 作用域";
        return result;
    }
    result.input.scope_key = ScopePrefix(*section_node);
    if (result.input.scope_key.empty()) {
        result.diagnostic = L"战场预览快照无法建立 Section 作用域";
        return result;
    }

    result.mode = ccz::preview::classify_preview_mode(
        result.input.document_path, result.input.nodes);
    if (result.mode.mode != ccz::preview::PreviewMode::Battle) {
        result.diagnostic = result.mode.diagnostic.empty()
            ? L"S 剧本缺少可验证的战场数据"
            : result.mode.diagnostic;
        return result;
    }

    RevisionHasher revision;
    revision.AddString(document_path);
    // The revision protects document-content undo, not the view's current
    // selection.  section_path and selected_path are navigation state; moving
    // to another Section must not stale a native writeback undo record.
    HashCapturedRoots(revision, document_snapshot.document_roots);
    result.input.document_revision = revision.Value();

    result.ok = true;
    return result;
}

}  // namespace ccz::editor_preview
