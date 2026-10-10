#include "test_framework.h"

#include <ccz/preview/r_document_session.h>

using namespace ccz::editor_preview;

namespace {
RDocumentActivationSnapshot Snapshot(
    PreviewDocumentId id = 7,
    std::wstring path = L"d:/mods/demo/R_01.eex",
    std::uint64_t content_epoch = 3,
    std::uint64_t tree_generation = 5,
    std::uintptr_t tree_window = 0x100,
    std::uintptr_t selected_target = 0x200) {
    RDocumentActivationSnapshot snapshot;
    snapshot.document_id = id;
    snapshot.normalized_path_key = NormalizeRPreviewDocumentPath(path);
    snapshot.content_epoch = content_epoch;
    snapshot.tree_generation = tree_generation;
    snapshot.tree_window = tree_window;
    snapshot.selected_target = selected_target;
    return snapshot;
}
}

CCZ_TEST(r_document_session_reuses_an_exact_activation_snapshot) {
    const auto cached = Snapshot();
    const auto current = Snapshot(7, L"D:\\MODS\\DEMO\\R_01.EEX");
    CCZ_REQUIRE_EQ(DecideRDocumentActivation(&cached, current),
                   RDocumentActivationDecision::Reuse);
}

CCZ_TEST(r_document_session_does_not_merge_documents_with_the_same_path) {
    const auto cached = Snapshot(7);
    const auto current = Snapshot(8);
    CCZ_REQUIRE_EQ(DecideRDocumentActivation(&cached, current),
                   RDocumentActivationDecision::Refresh);
}

CCZ_TEST(r_document_session_refreshes_when_content_or_tree_version_changes) {
    const auto cached = Snapshot();
    CCZ_REQUIRE_EQ(
        DecideRDocumentActivation(&cached, Snapshot(7, L"d:/mods/demo/R_01.eex", 4)),
        RDocumentActivationDecision::Refresh);
    CCZ_REQUIRE_EQ(
        DecideRDocumentActivation(&cached,
            Snapshot(7, L"d:/mods/demo/R_01.eex", 3, 6)),
        RDocumentActivationDecision::Refresh);
}

CCZ_TEST(r_document_session_rejects_cached_tree_handles_from_another_tree) {
    const auto cached = Snapshot();
    CCZ_REQUIRE_EQ(
        DecideRDocumentActivation(&cached,
            Snapshot(7, L"d:/mods/demo/R_01.eex", 3, 5, 0x101)),
        RDocumentActivationDecision::RejectStaleHandles);
    CCZ_REQUIRE_EQ(
        DecideRDocumentActivation(&cached,
            Snapshot(7, L"d:/mods/demo/R_01.eex", 3, 5, 0x100, 0x201)),
        RDocumentActivationDecision::RejectStaleHandles);
}

CCZ_TEST(r_document_session_refreshes_when_no_cached_baseline_exists) {
    const auto current = Snapshot();
    CCZ_REQUIRE_EQ(DecideRDocumentActivation(nullptr, current),
                   RDocumentActivationDecision::Refresh);
}
