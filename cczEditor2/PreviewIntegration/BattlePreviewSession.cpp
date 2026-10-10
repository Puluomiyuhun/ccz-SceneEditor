#include "BattlePreviewSession.h"

#include <stdexcept>
#include <utility>

namespace ccz::editor_preview {

BattlePreviewActivationCheck CheckBattlePreviewActivation(
	bool has_baseline,
	const BattlePreviewDocumentVersion& saved,
	const BattlePreviewDocumentVersion& current) noexcept
{
	if (!has_baseline)
		return BattlePreviewActivationCheck::InitializeSession;
	if (saved.tree_generation != current.tree_generation)
		return BattlePreviewActivationCheck::RebindTree;
	if (saved.content_epoch != current.content_epoch)
		return BattlePreviewActivationCheck::RevalidatePinnedSection;
	return BattlePreviewActivationCheck::ReuseSession;
}

BattlePreviewDocumentSession& BattlePreviewSessionRegistry::GetOrCreate(
	SPreviewDocumentId document_id)
{
	if (document_id == 0)
		throw std::invalid_argument("S preview document ID must be nonzero");

	const auto found = sessions_.find(document_id);
	if (found != sessions_.end()) return *found->second;

	auto session = std::make_unique<BattlePreviewDocumentSession>();
	session->document_id = document_id;
	auto inserted = sessions_.emplace(document_id, std::move(session));
	return *inserted.first->second;
}

BattlePreviewDocumentSession* BattlePreviewSessionRegistry::Find(
	SPreviewDocumentId document_id) noexcept
{
	const auto found = sessions_.find(document_id);
	return found == sessions_.end() ? nullptr : found->second.get();
}

const BattlePreviewDocumentSession* BattlePreviewSessionRegistry::Find(
	SPreviewDocumentId document_id) const noexcept
{
	const auto found = sessions_.find(document_id);
	return found == sessions_.end() ? nullptr : found->second.get();
}

bool BattlePreviewSessionRegistry::Erase(
	SPreviewDocumentId document_id) noexcept
{
	return sessions_.erase(document_id) != 0;
}

} // namespace ccz::editor_preview
