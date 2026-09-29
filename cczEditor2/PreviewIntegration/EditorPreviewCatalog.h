#pragma once

#include <ccz/preview/instruction_catalog.h>

namespace ccz::editor_preview {

// Built-in fallback shared by the R and S condition-tree projections.
ccz::preview::InstructionCatalog MakeEditorPreviewCatalog();

// Loads the editor-package CczString.ini. This catalog utility is shared by
// the pages without routing S through the R dialogue asset cache.
bool LoadEditorInstructionCatalogForModule(
    ccz::preview::InstructionCatalog& catalog,
    std::wstring* diagnostic);

}  // namespace ccz::editor_preview
