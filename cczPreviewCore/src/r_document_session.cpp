#include <ccz/preview/r_document_session.h>

#include <cwctype>

namespace ccz::editor_preview {

std::wstring NormalizeRPreviewDocumentPath(const std::wstring& path)
{
    std::wstring normalized;
    normalized.reserve(path.size());
    for (wchar_t character : path) {
        if (character == L'\\') {
            character = L'/';
        }
        normalized.push_back(static_cast<wchar_t>(std::towupper(character)));
    }
    return normalized;
}

RDocumentActivationDecision DecideRDocumentActivation(
    const RDocumentActivationSnapshot* cached,
    const RDocumentActivationSnapshot& current) noexcept
{
    if (cached == nullptr || current.document_id == 0 ||
        current.normalized_path_key.empty() || current.tree_generation == 0 ||
        current.tree_window == 0) {
        return RDocumentActivationDecision::Refresh;
    }
    if (cached->document_id != current.document_id ||
        cached->normalized_path_key != current.normalized_path_key) {
        return RDocumentActivationDecision::Refresh;
    }
    if (cached->content_epoch != current.content_epoch ||
        cached->tree_generation != current.tree_generation) {
        return RDocumentActivationDecision::Refresh;
    }
    if (cached->tree_window != current.tree_window ||
        cached->selected_target != current.selected_target) {
        return RDocumentActivationDecision::RejectStaleHandles;
    }
    return RDocumentActivationDecision::Reuse;
}

} // namespace ccz::editor_preview
