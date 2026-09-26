#include "EditorPreviewAdapter.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ccz::editor_preview {
namespace {

constexpr std::size_t kMaximumTreeDepth = 128;
constexpr std::size_t kMaximumCapturedNodes = 100000;
constexpr std::uint16_t kInvalidInstructionId = 0xffff;

std::wstring NormalizeDocumentPath(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 1 && path.back() == L'\\') {
        path.pop_back();
    }
    return path;
}

std::uint16_t EncodedInstructionId(int instruction_id) {
    if (instruction_id < 0 || instruction_id > 0xffff) {
        return kInvalidInstructionId;
    }
    return static_cast<std::uint16_t>(instruction_id);
}

void append_diagnostic(std::wstring& destination,
                       const std::wstring& message) {
    if (message.empty()) {
        return;
    }
    if (!destination.empty()) {
        destination += L"；";
    }
    destination += message;
}

class RevisionHasher {
public:
    void AddByte(std::uint8_t value) {
        hash_ ^= static_cast<std::uint64_t>(value);
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
        for (wchar_t character : value) {
            AddU64(static_cast<std::uint64_t>(character));
        }
    }

    std::uint64_t Value() const { return hash_; }

private:
    std::uint64_t hash_{1469598103934665603ull};
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
    for (const CapturedEditorNode& child : node.children) {
        HashCapturedNode(hasher, child);
    }
}

void HashCapturedRoots(RevisionHasher& hasher,
                       const std::vector<CapturedEditorNode>& roots) {
    hasher.AddU64(static_cast<std::uint64_t>(roots.size()));
    for (const CapturedEditorNode& root : roots) {
        HashCapturedNode(hasher, root);
    }
}

struct ConversionBudget {
    std::size_t node_count{};
};

struct ConversionContext {
    const ccz::preview::LogicalNodeKey& key_base;
    ConversionBudget& budget;
    std::wstring diagnostic;
};

bool ConvertNode(const CapturedEditorNode& source,
                 const ccz::preview::LogicalNodeKey& key_base,
                 const std::vector<std::uint32_t>& path,
                 std::size_t depth, ConversionContext& context,
                 ccz::preview::RawReplayNode& destination) {
    if (depth > kMaximumTreeDepth) {
        context.diagnostic = L"剧本树深度超出回放安全上限";
        return false;
    }
    if (context.budget.node_count >= kMaximumCapturedNodes) {
        context.diagnostic = L"剧本树节点数量超出回放安全上限";
        return false;
    }
    ++context.budget.node_count;

    const std::uint16_t instruction_id =
        EncodedInstructionId(source.instruction_id);
    destination = {};
    destination.runtime_handle = source.runtime_handle;
    destination.instruction_id = instruction_id;
    destination.logical_key = ccz::preview::make_logical_node_key(
        key_base, path, instruction_id);
    destination.command.instruction_id = instruction_id;
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
                         depth + 1, context, child)) {
            destination = {};
            return false;
        }
        destination.children.push_back(std::move(child));
    }
    return true;
}

const CapturedEditorNode* FindSelectedNode(
    const CapturedEditorNode& root,
    const std::vector<std::uint32_t>& selected_path) {
    const CapturedEditorNode* current = &root;
    for (std::uint32_t index : selected_path) {
        if (index >= current->children.size()) {
            return nullptr;
        }
        current = &current->children[index];
    }
    return current;
}

bool FindNodePathByHandle(
    const CapturedEditorNode& root, std::uintptr_t handle,
    std::vector<std::uint32_t>& path,
    const CapturedEditorNode*& found) {
    if (root.runtime_handle == handle) {
        found = &root;
        return true;
    }
    for (std::size_t index = 0; index < root.children.size(); ++index) {
        path.push_back(static_cast<std::uint32_t>(index));
        if (FindNodePathByHandle(root.children[index], handle, path, found)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

bool ConvertRoots(const std::vector<CapturedEditorNode>& roots,
                  const ccz::preview::LogicalNodeKey& key_base,
                  ConversionBudget& budget,
                  std::vector<ccz::preview::RawReplayNode>& destination,
                  std::wstring& diagnostic) {
    ConversionContext context{key_base, budget};
    std::vector<ccz::preview::RawReplayNode> converted;
    converted.reserve(roots.size());
    for (std::size_t index = 0; index < roots.size(); ++index) {
        ccz::preview::RawReplayNode node;
        if (!ConvertNode(roots[index], key_base, {static_cast<std::uint32_t>(index)},
                         0, context, node)) {
            diagnostic = context.diagnostic;
            return false;
        }
        converted.push_back(std::move(node));
    }
    destination = std::move(converted);
    return true;
}

}  // namespace

std::wstring CopyLongText(const wchar_t* data, std::size_t capacity) {
    if (data == nullptr || capacity == 0) {
        return {};
    }
    std::size_t length = 0;
    while (length < capacity && data[length] != L'\0') {
        ++length;
    }
    return std::wstring(data, length);
}

EditorPreviewCapture BuildEditorPreviewInput(
    const CapturedEditorTree& snapshot) {
    EditorPreviewCapture result;
    const std::wstring document_path =
        NormalizeDocumentPath(snapshot.document_path);
    if (document_path.empty()) {
        result.diagnostic = L"剧本文档路径为空";
        return result;
    }

    // A selection above the Section level is a valid idle state.  Do not
    // publish document or section data while the preview has no target.
    if (snapshot.section_path.empty()) {
        result.ok = true;
        result.idle = true;
        return result;
    }

    if (snapshot.section_root.instruction_id != -2) {
        result.diagnostic = L"Section 快照缺少 Section 容器";
        return result;
    }

    // The core model owns the Section phase split.  The adapter only verifies
    // that the native direct-child sequence contains its first top-level 0x00;
    // entry nodes before it must remain in the ordered input.
    const auto boundary = std::find_if(
        snapshot.section_root.children.begin(),
        snapshot.section_root.children.end(),
        [](const CapturedEditorNode& node) {
            return node.instruction_id == 0;
        });
    if (boundary == snapshot.section_root.children.end()) {
        result.diagnostic = L"Section 快照缺少顶层 0x00 事件边界";
        return result;
    }

    const ccz::preview::LogicalNodeKey document_key =
        document_path + L"|document";
    const ccz::preview::LogicalNodeKey section_key =
        ccz::preview::make_section_key(document_path, snapshot.section_path);

    EditorPreviewInput input;
    input.document_key = document_key;

    std::wstring conversion_diagnostic;
    ConversionBudget conversion_budget;
    if (!ConvertRoots(snapshot.document_roots, document_key,
                      conversion_budget, input.document_nodes,
                      conversion_diagnostic)) {
        result.diagnostic = conversion_diagnostic;
        return result;
    }

    std::vector<ccz::preview::RawReplayNode> section_nodes;
    if (!ConvertRoots(snapshot.section_root.children, section_key,
                      conversion_budget, section_nodes,
                      conversion_diagnostic)) {
        result.diagnostic = conversion_diagnostic;
        return result;
    }
    input.section.section_key = section_key;
    input.section.nodes = std::move(section_nodes);

    const CapturedEditorNode* selected =
        FindSelectedNode(snapshot.section_root, snapshot.selected_path);
    if (selected == nullptr) {
        result.diagnostic = L"选中节点路径不在 Section 快照中";
        return result;
    }
    input.section.selected_target_key = ccz::preview::make_logical_node_key(
        section_key, snapshot.selected_path,
        EncodedInstructionId(selected->instruction_id));

    RevisionHasher revision;
    revision.AddString(document_path);
    revision.AddU64(static_cast<std::uint64_t>(snapshot.section_path.size()));
    for (std::uint32_t value : snapshot.section_path) {
        revision.AddU64(value);
    }
    HashCapturedRoots(revision, snapshot.document_roots);
    input.document_revision = revision.Value();

    result.ok = true;
    result.input = std::move(input);
    return result;
}

EditorPreviewCapture BuildEditorPreviewInputForSection(
    const CapturedEditorTree& document_snapshot,
    std::uintptr_t section_handle,
    std::uintptr_t selected_handle) {
    EditorPreviewCapture result;
    if (document_snapshot.document_roots.empty()) {
        result.diagnostic = L"剧本文档快照为空";
        return result;
    }

    const CapturedEditorNode& document_root =
        document_snapshot.document_roots.front();
    std::vector<std::uint32_t> section_path;
    const CapturedEditorNode* section = nullptr;
    if (!FindNodePathByHandle(document_root, section_handle, section_path,
                              section) || section == nullptr ||
        section->instruction_id != -2 || section_path.empty()) {
        result.diagnostic = L"文档快照中未找到目标 Section";
        return result;
    }

    std::vector<std::uint32_t> selected_path;
    const CapturedEditorNode* selected = nullptr;
    if (!FindNodePathByHandle(*section, selected_handle, selected_path,
                              selected) ||
        selected == nullptr || selected_path.empty()) {
        result.diagnostic = L"目标节点不在目标 Section 内";
        return result;
    }

    CapturedEditorTree scoped = document_snapshot;
    scoped.section_path = std::move(section_path);
    scoped.section_root = *section;
    scoped.selected_path = std::move(selected_path);
    return BuildEditorPreviewInput(scoped);
}

EditorPreviewCapture BuildEditorPreviewInputForSectionToEnd(
    const CapturedEditorTree& document_snapshot,
    std::uintptr_t section_handle) {
    if (document_snapshot.document_roots.empty()) {
        EditorPreviewCapture result;
        result.diagnostic = L"剧本文档快照为空";
        return result;
    }

    const CapturedEditorNode& document_root =
        document_snapshot.document_roots.front();
    std::vector<std::uint32_t> section_path;
    const CapturedEditorNode* section = nullptr;
    if (!FindNodePathByHandle(document_root, section_handle, section_path,
                              section) || section == nullptr ||
        section->instruction_id != -2 || section_path.empty()) {
        EditorPreviewCapture result;
        result.diagnostic = L"文档快照中未找到目标 Section";
        return result;
    }

    for (const CapturedEditorNode& child : section->children) {
        if (child.instruction_id == 0) {
            return BuildEditorPreviewInputForSection(
                document_snapshot, section_handle, child.runtime_handle);
        }
    }

    EditorPreviewCapture result;
    result.diagnostic = L"目标 Section 缺少顶层 0x00 事件边界";
    return result;
}

}  // namespace ccz::editor_preview
