#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattlePreviewSession.h"

#include <memory>

using namespace ccz::editor_preview;

CCZ_TEST(battle_preview_document_session_distinguishes_same_path_documents)
{
	BattlePreviewSessionRegistry registry;
	auto& first = registry.GetOrCreate(11);
	auto& second = registry.GetOrCreate(12);
	first.document_path = L"C:\\qa\\S_10.eex";
	second.document_path = first.document_path;
	first.editor_state.placement.instruction_id = 0x46;
	second.editor_state.placement.instruction_id = 0x47;
	first.editor_state.mouse_direction = 1;
	second.editor_state.mouse_direction = 3;
	first.ui.quick_mode_enabled = true;
	second.ui.quick_mode_enabled = false;
	first.ui.parameter_preview_mode = BattleParameterPreviewMode::AiPolicy;
	second.ui.parameter_preview_mode = BattleParameterPreviewMode::None;
	first.ui.copy_person_mode = true;
	first.ui.unrestricted_terrain_mode = true;
	first.editor_state.copy_person_mode = true;
	first.editor_state.unrestricted_terrain_mode = true;
	first.ui.zoom = 1.5f;
	second.ui.zoom = 0.75f;
	first.ui.deployment_instruction = 0x46;
	second.ui.deployment_instruction = 0x47;
	first.operations = std::make_unique<BattleEditorOps>(first.editor_state);
	second.operations = std::make_unique<BattleEditorOps>(second.editor_state);
	first.operations->SetCharacterSearch(L"曹");
	second.operations->SetCharacterSearch(L"孙");

	CCZ_REQUIRE_EQ(registry.Size(), std::size_t{2});
	CCZ_REQUIRE_EQ(first.editor_state.characters.query, std::wstring(L"曹"));
	CCZ_REQUIRE_EQ(second.editor_state.characters.query, std::wstring(L"孙"));
	CCZ_REQUIRE_EQ(first.editor_state.placement.instruction_id, 0x46);
	CCZ_REQUIRE_EQ(second.editor_state.placement.instruction_id, 0x47);
	CCZ_REQUIRE_EQ(first.editor_state.mouse_direction, 1);
	CCZ_REQUIRE_EQ(second.editor_state.mouse_direction, 3);
	CCZ_REQUIRE(first.ui.quick_mode_enabled);
	CCZ_REQUIRE(!second.ui.quick_mode_enabled);
	CCZ_REQUIRE_EQ(first.ui.parameter_preview_mode,
		BattleParameterPreviewMode::AiPolicy);
	CCZ_REQUIRE_EQ(second.ui.parameter_preview_mode,
		BattleParameterPreviewMode::None);
	CCZ_REQUIRE(first.ui.copy_person_mode);
	CCZ_REQUIRE(first.ui.unrestricted_terrain_mode);
	CCZ_REQUIRE(!second.ui.copy_person_mode);
	CCZ_REQUIRE(!second.ui.unrestricted_terrain_mode);
	CCZ_REQUIRE(first.editor_state.copy_person_mode);
	CCZ_REQUIRE(first.editor_state.unrestricted_terrain_mode);
	CCZ_REQUIRE(!second.editor_state.copy_person_mode);
	CCZ_REQUIRE(!second.editor_state.unrestricted_terrain_mode);
	CCZ_REQUIRE_EQ(first.ui.zoom, 1.5f);
	CCZ_REQUIRE_EQ(second.ui.zoom, 0.75f);
	CCZ_REQUIRE_EQ(first.ui.deployment_instruction, 0x46);
	CCZ_REQUIRE_EQ(second.ui.deployment_instruction, 0x47);
}

CCZ_TEST(battle_preview_document_session_registry_keeps_stable_instances)
{
	BattlePreviewSessionRegistry registry;
	auto& first = registry.GetOrCreate(7);
	first.editor_state.characters.selected_data_id = 77;
	for (SPreviewDocumentId id = 8; id < 80; ++id)
		registry.GetOrCreate(id).editor_state.characters.selected_data_id =
			static_cast<int>(id);

	CCZ_REQUIRE(&registry.GetOrCreate(7) == &first);
	CCZ_REQUIRE_EQ(first.editor_state.characters.selected_data_id, 77);
	CCZ_REQUIRE_EQ(registry.Size(), std::size_t{73});
}

CCZ_TEST(battle_preview_document_session_close_erases_only_target_id)
{
	BattlePreviewSessionRegistry registry;
	auto& first = registry.GetOrCreate(1);
	auto& second = registry.GetOrCreate(2);
	first.editor_state.characters.selected_data_id = 10;
	second.editor_state.characters.selected_data_id = 20;

	CCZ_REQUIRE(registry.Erase(2));
	CCZ_REQUIRE(registry.Find(2) == nullptr);
	CCZ_REQUIRE(registry.Find(1) == &first);
	CCZ_REQUIRE_EQ(first.editor_state.characters.selected_data_id, 10);
	CCZ_REQUIRE_EQ(registry.Size(), std::size_t{1});
}

CCZ_TEST(battle_preview_document_session_activation_checks_versions)
{
	const BattlePreviewDocumentVersion same{3, 5};
	const BattlePreviewDocumentVersion content_changed{4, 5};
	const BattlePreviewDocumentVersion tree_changed{4, 6};

	CCZ_REQUIRE_EQ(CheckBattlePreviewActivation(false, same, same),
		BattlePreviewActivationCheck::InitializeSession);
	CCZ_REQUIRE_EQ(CheckBattlePreviewActivation(true, same, same),
		BattlePreviewActivationCheck::ReuseSession);
	CCZ_REQUIRE_EQ(CheckBattlePreviewActivation(true, same, content_changed),
		BattlePreviewActivationCheck::RevalidatePinnedSection);
	CCZ_REQUIRE_EQ(CheckBattlePreviewActivation(true, content_changed, tree_changed),
		BattlePreviewActivationCheck::RebindTree);
}
