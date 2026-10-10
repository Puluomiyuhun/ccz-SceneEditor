#pragma once

#include <cstdint>
#include <string>

namespace ccz::editor_preview {

using PreviewDocumentId = std::uint64_t;

struct RDocumentActivationSnapshot {
    PreviewDocumentId document_id{};
    std::wstring normalized_path_key;
    std::uint64_t content_epoch{};
    std::uint64_t tree_generation{};
    std::uintptr_t tree_window{};
    std::uintptr_t selected_target{};
};

enum class RDocumentActivationDecision {
    Refresh,
    Reuse,
    RejectStaleHandles,
};

std::wstring NormalizeRPreviewDocumentPath(const std::wstring& path);

RDocumentActivationDecision DecideRDocumentActivation(
    const RDocumentActivationSnapshot* cached,
    const RDocumentActivationSnapshot& current) noexcept;

} // namespace ccz::editor_preview
