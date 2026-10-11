#pragma once

#include "PreviewSession.h"

#include <ccz/preview/dialogue_profile.h>
#include <ccz/preview/instruction_catalog.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::editor_preview {

// Owns the document-root resources used by the dialogue and portrait models.
// The returned view is read-only and remains valid until the next load or
// Clear call on this cache.
class PreviewAssetCache {
public:
    bool LoadForDocument(const std::wstring& document_path,
                         const ccz::preview::DialogueProfile& profile,
                         std::wstring* diagnostic);
    PreviewAssetsView view() const noexcept;
    const ccz::preview::InstructionCatalog& instruction_catalog() const noexcept;
    void Clear();

private:
    std::wstring root_key_;
    std::uint64_t catalog_revision_{};
    bool catalog_revision_valid_{};
    std::vector<std::uint8_t> data_e5_;
    std::vector<std::uint8_t> face_e5_;
    ccz::preview::FaceArchive face_archive_;
    std::vector<std::uint32_t> text_palette_;
    std::vector<std::uint8_t> fb_e5_;
    ccz::preview::FaceArchive fb_archive_;
    ccz::preview::InstructionCatalog instruction_catalog_;
    std::wstring diagnostic_;
};

}  // namespace ccz::editor_preview
