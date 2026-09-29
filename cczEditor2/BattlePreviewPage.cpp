#include "pch.h"

#include "cczEditor2.h"
#include "BattlePreviewPage.h"
#include "cczEditor2Doc.h"
#include "cczEditor2View.h"
#include "LongCharData.h"
#include "MainFrm.h"
#include "ScenePreviewHost.h"
#include "PreviewIntegration/BattlePreviewAdapter.h"
#include "PreviewIntegration/BattlePreviewPredicates.h"
#include "PreviewIntegration/EditorPreviewCatalog.h"
#include "PreviewIntegration/BattleQuickModeResolver.h"
#include "PreviewIntegration/E5ImageLoader.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include "PreviewIntegration/BattleWritebackQaGate.h"
#endif

#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/battle_instruction_parser.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include <filesystem>
#include <fstream>
#endif
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#define CCZ_BATTLE_PREVIEW_PERF(action) action
#else
#define CCZ_BATTLE_PREVIEW_PERF(action) ((void)0)
#endif

namespace {
constexpr int kCellPixels = 48;
constexpr std::size_t kBattleBitmapCacheBudgetBytes = 128u * 1024u * 1024u;
constexpr float kDefaultZoom = 1.0f;
constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 3.0f;
constexpr float kZoomStep = 0.25f;
constexpr int kSidebarWidth = 246;
constexpr UINT kCharacterTabId = 0xA100;
constexpr UINT kBrushTabId = 0xA101;
constexpr UINT kHideSidebarId = 0xA102;
constexpr UINT kShowSidebarId = 0xA103;
constexpr UINT kPreviousPageId = 0xA104;
constexpr UINT kNextPageId = 0xA105;
constexpr UINT kUndoId = 0xA106;
constexpr UINT kQuickModeId = 0xA10C;
constexpr UINT kConditionTreeId = 0xA10D;
constexpr UINT kConditionActionMessage = WM_APP + 0x38A;
constexpr UINT kSearchId = 0xA107;
constexpr UINT kDeploymentComboId = 0xA108;
constexpr UINT kCharacterListId = 0xA109;
constexpr UINT kPageLabelId = 0xA10A;
constexpr UINT kWritebackId = 0xA10B;
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)

std::wstring battle_writeback_qa_copy_root()
{
	std::vector<wchar_t> executablePath(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
		static_cast<DWORD>(executablePath.size()));
	if (length == 0 || length >= executablePath.size()) return {};
	return ccz::editor_preview::BattleWritebackQaCopyRootForExecutable(
		std::wstring_view(executablePath.data(), length));
}
#endif

bool contains_ignore_case(const std::wstring& value,
	const std::wstring& query)
{
	if (query.empty()) return true;
	if (value.size() < query.size()) return false;
	for (std::size_t start = 0; start + query.size() <= value.size(); ++start) {
		bool matches = true;
		for (std::size_t offset = 0; offset < query.size(); ++offset) {
			if (std::towlower(value[start + offset]) !=
				std::towlower(query[offset])) {
				matches = false;
				break;
			}
		}
		if (matches) return true;
	}
	return false;
}

void draw_deployment_slot_marker(Gdiplus::Graphics& graphics,
	float centerX, float baseY, float cell, int deploymentIndex,
	float opacity, bool hidden)
{
	const float visibility = opacity * (hidden ? 0.62f : 1.0f);
	const auto alpha = [visibility](int value) {
		return static_cast<BYTE>((std::min)(255,
			(std::max)(0, static_cast<int>(value * visibility))));
	};
	const float markerWidth = (std::max)(24.0f, cell * 0.80f);
	const float markerHeight = (std::max)(16.0f, cell * 0.42f);
	const Gdiplus::RectF markerRect(centerX - markerWidth * 0.5f,
		baseY - markerHeight - cell * 0.12f, markerWidth, markerHeight);
	Gdiplus::SolidBrush markerFill(Gdiplus::Color(
		alpha(232), 54, 61, 68));
	Gdiplus::Pen markerBorder(Gdiplus::Color(
		alpha(245), 210, 216, 220), (std::max)(1.0f, cell * 0.025f));
	graphics.FillEllipse(&markerFill, markerRect);
	graphics.DrawEllipse(&markerBorder, markerRect);

	// Keep the native zero-based slot visible; it is not a Data ID.
	const std::wstring markerText = L"#" + std::to_wstring(deploymentIndex);
	Gdiplus::Font markerFont(L"Microsoft YaHei",
		(std::max)(8.0f, cell * 0.21f),
		Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::StringFormat markerFormat;
	markerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
	markerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	Gdiplus::SolidBrush markerTextBrush(Gdiplus::Color(
		alpha(255), 245, 245, 235));
	graphics.DrawString(markerText.c_str(), -1, &markerFont,
		markerRect, &markerFormat, &markerTextBrush);
}

bool in_scope(const ccz::preview::LogicalNodeKey& key,
	const ccz::preview::LogicalNodeKey& scope)
{
	if (key.empty() || scope.empty()) return false;
	if (key == scope) return true;
	if (key.size() <= scope.size() || key.compare(0, scope.size(), scope) != 0)
		return false;
	const wchar_t separator = key[scope.size()];
	return separator == L'|' || separator == L'/';
}

const wchar_t* battle_field_name(ccz::preview::BattleFieldRole role)
{
	switch (role) {
	case ccz::preview::BattleFieldRole::DataId: return L"Data号";
	case ccz::preview::BattleFieldRole::DeploymentIndex: return L"槽位";
	case ccz::preview::BattleFieldRole::Reinforcement: return L"增援";
	case ccz::preview::BattleFieldRole::Hidden: return L"隐藏";
	case ccz::preview::BattleFieldRole::X: return L"X";
	case ccz::preview::BattleFieldRole::Y: return L"Y";
	case ccz::preview::BattleFieldRole::Direction: return L"朝向";
	case ccz::preview::BattleFieldRole::Level: return L"等级";
	case ccz::preview::BattleFieldRole::UnitType: return L"兵种";
	case ccz::preview::BattleFieldRole::AiPolicy: return L"AI";
	case ccz::preview::BattleFieldRole::TargetDataId: return L"目标Data号";
	case ccz::preview::BattleFieldRole::TargetX: return L"目标X";
	case ccz::preview::BattleFieldRole::TargetY: return L"目标Y";
	}
	return L"字段";
}

std::wstring describe_native_writeback(
	const ccz::editor_preview::BattleWritebackNativeBatch& batch)
{
	std::wostringstream summary;
	summary << L"原生树回读确认 " << batch.changes.size() << L" 条指令变更";
	std::size_t emitted_fields = 0;
	constexpr std::size_t kMaximumDisplayedFields = 16;
	for (const auto& change : batch.changes) {
		summary << L"；";
		if (change.kind ==
			ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction) {
			summary << L"新增";
		} else if (change.kind ==
			ccz::editor_preview::BattleWritebackChangeKind::DeleteInstruction) {
			summary << L"删除";
		} else {
			summary << L"修改";
		}
		summary << L"0x" << std::hex << std::uppercase
			<< change.instruction_id << std::dec;
		if (change.source_ordinal >= 0)
			summary << L"#" << change.source_ordinal;

		const auto* spec = ccz::preview::get_battle_field_spec(
			change.instruction_id);
		if (spec == nullptr || change.kind ==
			ccz::editor_preview::BattleWritebackChangeKind::DeleteInstruction)
			continue;
		const std::size_t row_count = change.instruction_id == 0x4b
			? 1U : spec->record_capacity;
		for (std::size_t row = 0; row < row_count; ++row) {
			const std::size_t offset = row * spec->record_stride;
			if (change.kind ==
				ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction) {
				bool occupied = change.instruction_id == 0x4b;
				for (std::size_t field = 0; field < spec->record_stride; ++field) {
					if (spec->field_roles[field] ==
						ccz::preview::BattleFieldRole::DataId &&
						change.int_data[offset + field] >= 0) occupied = true;
				}
				if (!occupied) continue;
			}
			bool row_has_delta = false;
			for (std::size_t field = 0; field < spec->record_stride; ++field) {
				const std::size_t index = offset + field;
				if (index >= change.int_data.size()) break;
				const auto role = spec->field_roles[field];
				const bool insertion = change.kind ==
					ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction;
				if (!insertion && (index >= change.original_int_data.size() ||
					change.original_int_data[index] == change.int_data[index]))
					continue;
				if (!row_has_delta) {
					summary << L"[" << row << L"]";
					row_has_delta = true;
				}
				if (emitted_fields >= kMaximumDisplayedFields) continue;
				summary << L" " << battle_field_name(role) << L"=";
				if (!insertion) {
					summary << change.original_int_data[index] << L"→";
				}
				summary << change.int_data[index];
				++emitted_fields;
			}
		}
	}
	if (emitted_fields >= kMaximumDisplayedFields)
		summary << L"；其余字段变更已省略";
	return summary.str();
}

bool RevealNativeWritebackInstruction(
	CcczEditor2View& view,
	const ccz::editor_preview::BattleWritebackNativeBatch& batch,
	std::uintptr_t inserted_handle)
{
	CTreeCtrl& tree = view.GetTreeCtrl();
	const auto reveal = [&tree, &view](std::uintptr_t handle) {
		if (handle == 0) return false;
		const HTREEITEM item = reinterpret_cast<HTREEITEM>(handle);
		if (tree.GetItemData(item) == 0 || !tree.EnsureVisible(item) ||
			!tree.SelectItem(item)) return false;
		view.cur_item = item;
		return true;
	};
	for (const auto& change : batch.changes) {
		if (change.kind !=
			ccz::editor_preview::BattleWritebackChangeKind::UpdateInstruction ||
			change.item_handle == 0) continue;
		if (reveal(change.item_handle)) return true;
	}
	return reveal(inserted_handle);
}

class BattlePreviewRefreshSuppression {
public:
	explicit BattlePreviewRefreshSuppression(CcczEditor2View& view)
		: view_(view), previous_(view.m_suppressBattleWritebackPreview)
	{
		view_.m_suppressBattleWritebackPreview = true;
		view_.m_suppressPreviewRefreshOnce = false;
	}
	~BattlePreviewRefreshSuppression()
	{
		view_.m_suppressPreviewRefreshOnce = false;
		view_.m_suppressBattleWritebackPreview = previous_;
	}
	BattlePreviewRefreshSuppression(const BattlePreviewRefreshSuppression&) = delete;
	BattlePreviewRefreshSuppression& operator=(
		const BattlePreviewRefreshSuppression&) = delete;

private:
	CcczEditor2View& view_;
	bool previous_{};
};

#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
class BattleWritebackPerformanceScope {
public:
	BattleWritebackPerformanceScope(const wchar_t* document_path, bool enabled)
		: started_(Clock::now()), phase_started_(started_)
	{
		if (!enabled || document_path == nullptr || *document_path == L'\0')
			return;
		try {
			log_path_ = std::filesystem::path(document_path).parent_path() /
				L"SWritebackPerf.log";
			enabled_ = !log_path_.empty();
		} catch (...) {
			enabled_ = false;
		}
	}

	~BattleWritebackPerformanceScope()
	{
		if (!enabled_) return;
		try {
			std::ofstream log(log_path_, std::ios::out | std::ios::app |
				std::ios::binary);
			if (!log) return;
			log << "SWritebackPerf;capture_us=" << capture_us_
				<< ";plan_us=" << plan_us_
				<< ";native_transaction_us=" << transaction_us_
				<< ";rebase_us=" << rebase_us_
				<< ";total_us=" << Elapsed(started_)
				<< ";changes=" << changes_
				<< ";insert=" << inserts_
				<< ";update=" << updates_
				<< ";delete=" << deletes_
				<< ";applied=" << (applied_ ? 1 : 0)
				<< ";rebased=" << (rebased_ ? 1 : 0)
				<< ";stage=" << stage_ << "\r\n";
		} catch (...) {
			// QA timing output must never affect native writeback behavior.
		}
	}

	void RecordCaptured()
	{
		capture_us_ = Elapsed(phase_started_);
		phase_started_ = Clock::now();
		stage_ = "plan";
	}

	void RecordPlan(const ccz::editor_preview::BattleWritebackNativeBatch& batch)
	{
		plan_us_ = Elapsed(phase_started_);
		phase_started_ = Clock::now();
		changes_ = batch.changes.size();
		for (const auto& change : batch.changes) {
			if (change.kind == ccz::editor_preview::BattleWritebackChangeKind::InsertInstruction)
				++inserts_;
			else if (change.kind == ccz::editor_preview::BattleWritebackChangeKind::UpdateInstruction)
				++updates_;
			else if (change.kind == ccz::editor_preview::BattleWritebackChangeKind::DeleteInstruction)
				++deletes_;
		}
		stage_ = "transaction";
	}

	void RecordTransaction(bool applied)
	{
		transaction_us_ = Elapsed(phase_started_);
		phase_started_ = Clock::now();
		applied_ = applied;
		stage_ = applied ? "rebase" : "transaction-failed";
	}

	void RecordRebase(bool rebased)
	{
		rebase_us_ = Elapsed(phase_started_);
		rebased_ = rebased;
		stage_ = "complete";
	}

private:
	using Clock = std::chrono::steady_clock;
	static std::uint64_t Elapsed(Clock::time_point start)
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - start).count());
	}

	std::filesystem::path log_path_;
	Clock::time_point started_;
	Clock::time_point phase_started_;
	std::uint64_t capture_us_{};
	std::uint64_t plan_us_{};
	std::uint64_t transaction_us_{};
	std::uint64_t rebase_us_{};
	std::size_t changes_{};
	std::size_t inserts_{};
	std::size_t updates_{};
	std::size_t deletes_{};
	const char* stage_{ "capture" };
	bool enabled_{};
	bool applied_{};
	bool rebased_{};
};
#endif

class MfcBattleWritebackNativeHost final
	: public ccz::editor_preview::BattleWritebackNativeHost {
public:
	MfcBattleWritebackNativeHost(CcczEditor2View& view,
		HTREEITEM revision_section)
		: view_(view), document_(view.GetDocument()), tree_(view.GetTreeCtrl()),
		  revision_section_(revision_section), initial_state_pending_(false)
	{
	}

	MfcBattleWritebackNativeHost(CcczEditor2View& view,
		HTREEITEM revision_section,
		const ccz::editor_preview::BattleWritebackNativeDocumentState& initial_state,
		std::uint64_t document_id,
		ccz::editor_preview::BattlePreviewDocumentVersion expected_version)
		: view_(view), document_(view.GetDocument()), tree_(view.GetTreeCtrl()),
		  revision_section_(revision_section), initial_state_(initial_state),
		  expected_document_id_(document_id),
		  expected_version_(expected_version)
	{
	}

	bool CaptureDocumentState(
		ccz::editor_preview::BattleWritebackNativeDocumentState& state) const override
	{
		state = {};
		if (document_ == nullptr || document_->pathName == nullptr ||
			!NodeExists(reinterpret_cast<std::uintptr_t>(revision_section_)))
			return false;
		if (initial_state_pending_) {
			const auto selected = reinterpret_cast<std::uintptr_t>(
				tree_.GetSelectedItem());
			if (initial_state_.document_revision == 0 ||
				document_->GetSPreviewDocumentId() != expected_document_id_ ||
				document_->GetPreviewTreeGeneration() !=
					expected_version_.tree_generation ||
				document_->GetPreviewContentEpoch() !=
					expected_version_.content_epoch ||
				selected != initial_state_.selected_handle ||
				view_.cur_code_ord != initial_state_.next_ordinal ||
				document_->modified != initial_state_.custom_modified ||
				(document_->IsModified() != FALSE) !=
					initial_state_.framework_modified ||
				(selected != 0 && !TreeItemExists(selected)))
				return false;
			state = initial_state_;
			initial_state_pending_ = false;
			return true;
		}
		try {
			last_captured_battle_ = {};
			ccz::editor_preview::CapturedEditorTree snapshot;
			std::wstring diagnostic;
			const std::uint64_t document_id_before =
				document_->GetSPreviewDocumentId();
			const ccz::editor_preview::BattlePreviewDocumentVersion
				version_before{
					document_->GetPreviewContentEpoch(),
					document_->GetPreviewTreeGeneration()};
			if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree_,
				revision_section_, std::wstring(document_->pathName), snapshot,
				diagnostic)) return false;
			auto capture =
				ccz::editor_preview::BuildBattlePreviewInput(std::move(snapshot));
			if (!capture.ok) return false;
			// cur_code_ord is global to the native document.  A stale counter may
			// otherwise assign a duplicate ordinal to a newly inserted instruction,
			// corrupting later save/index operations.  Gaps are safe; collisions are
			// not, so only accept a counter strictly beyond every existing node.
			if (!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
				capture.snapshot, view_.cur_code_ord, diagnostic)) return false;
			const std::uint64_t document_id_after =
				document_->GetSPreviewDocumentId();
			const ccz::editor_preview::BattlePreviewDocumentVersion
				version_after{
					document_->GetPreviewContentEpoch(),
					document_->GetPreviewTreeGeneration()};
			if (document_id_after != document_id_before ||
				version_after.content_epoch != version_before.content_epoch ||
				version_after.tree_generation != version_before.tree_generation)
				return false;

			state.selected_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetSelectedItem());
			state.next_ordinal = view_.cur_code_ord;
			state.custom_modified = document_->modified;
			state.framework_modified = document_->IsModified() != FALSE;
			state.document_revision = capture.input.document_revision;
			last_captured_battle_ = std::move(capture);
			last_capture_document_id_ = document_id_after;
			last_capture_content_epoch_ = version_after.content_epoch;
			last_capture_tree_generation_ = version_after.tree_generation;
			return state.next_ordinal >= 0 && state.document_revision != 0;
		} catch (...) {
			state = {};
			return false;
		}
	}

	bool TakeCommittedBattleCapture(
		ccz::editor_preview::BattlePreviewCapture& capture)
	{
		if (!last_captured_battle_.ok || document_ == nullptr ||
			document_->pathName == nullptr ||
			document_->GetSPreviewDocumentId() != last_capture_document_id_ ||
			document_->GetPreviewContentEpoch() != last_capture_content_epoch_ ||
			document_->GetPreviewTreeGeneration() != last_capture_tree_generation_ ||
			CString(document_->pathName).CompareNoCase(
				last_captured_battle_.input.document_path.c_str()) != 0)
			return false;
		capture = std::move(last_captured_battle_);
		last_capture_document_id_ = 0;
		last_capture_content_epoch_ = 0;
		last_capture_tree_generation_ = 0;
		return capture.ok;
	}

	bool CaptureNode(std::uintptr_t handle,
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot& node) const override
	{
		node = {};
		if (!NodeExists(handle)) return false;
		const HTREEITEM item = reinterpret_cast<HTREEITEM>(handle);
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(item));
		if (data == nullptr) return false;
		try {
			node.handle = handle;
			node.parent_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetParentItem(item));
			node.previous_sibling_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetPrevSiblingItem(item));
			node.next_sibling_handle = reinterpret_cast<std::uintptr_t>(
				tree_.GetNextSiblingItem(item));
			node.instruction_id = static_cast<std::uint16_t>(data->id);
			node.ordinal = data->ord;
			node.int_data.assign(data->int_data.begin(), data->int_data.end());
			node.has_children = tree_.GetChildItem(item) != nullptr;
			node.has_long_text = data->long_char_data != nullptr ||
				data->long_char_data_capacity != 0;
			node.has_structural_links = data->child != nullptr || data->bro != nullptr;
			return true;
		} catch (...) {
			node = {};
			return false;
		}
	}

	bool NodeExists(std::uintptr_t handle) const override
	{
		if (handle == 0 || tree_.GetSafeHwnd() == nullptr) return false;
		TVITEM item{};
		item.mask = TVIF_HANDLE | TVIF_PARAM;
		item.hItem = reinterpret_cast<HTREEITEM>(handle);
		return TreeView_GetItem(tree_.GetSafeHwnd(), &item) != FALSE &&
			item.lParam != 0;
	}

	bool TreeItemExists(std::uintptr_t handle) const
	{
		if (handle == 0 || tree_.GetSafeHwnd() == nullptr) return false;
		TVITEM item{};
		item.mask = TVIF_HANDLE | TVIF_PARAM;
		item.hItem = reinterpret_cast<HTREEITEM>(handle);
		// The document filename root is a valid selectable TreeView item with
		// lParam == 0; selection restoration must not treat it as a script node.
		return TreeView_GetItem(tree_.GetSafeHwnd(), &item) != FALSE;
	}

	bool SwapNodeIntDataAndRefresh(std::uintptr_t handle,
		std::vector<std::int32_t>& int_data) override
	{
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot node;
		if (!CaptureNode(handle, node) || node.has_children ||
			node.has_long_text || node.has_structural_links ||
			node.int_data.size() != int_data.size()) return false;
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(
			reinterpret_cast<HTREEITEM>(handle)));
		if (data == nullptr) return false;
		data->int_data.swap(int_data);
		try {
			view_.UpdateShow(reinterpret_cast<HTREEITEM>(handle));
		} catch (...) {
			data->int_data.swap(int_data);
			try {
				view_.UpdateShow(reinterpret_cast<HTREEITEM>(handle));
			} catch (...) {
				// Keep the original native fields even if display regeneration also
				// fails; a later normal tree refresh can reconstruct the label.
			}
			return false;
		}
		return true;
	}

	bool InsertBattleNode(std::uintptr_t parent_handle,
		std::uintptr_t after_handle, std::uint16_t instruction_id,
		const std::vector<std::int32_t>& int_data, int ordinal,
		std::uintptr_t& inserted_handle) override
	{
		inserted_handle = 0;
		if (parent_handle == 0 || !NodeExists(parent_handle) ||
			ordinal != view_.cur_code_ord) return false;
		HTREEITEM after = after_handle == 0 ? TVI_FIRST
			: reinterpret_cast<HTREEITEM>(after_handle);
		if (after_handle != 0 && (!NodeExists(after_handle) ||
			tree_.GetParentItem(after) != reinterpret_cast<HTREEITEM>(parent_handle)))
			return false;
		const HTREEITEM inserted = view_.InsertBattleInstructionAt(
			instruction_id, reinterpret_cast<HTREEITEM>(parent_handle), after,
			int_data);
		if (inserted == nullptr) return false;
		inserted_handle = reinterpret_cast<std::uintptr_t>(inserted);
		if (first_inserted_handle_ == 0)
			first_inserted_handle_ = inserted_handle;
		return true;
	}

	bool DeleteBattleNode(std::uintptr_t handle) override
	{
		ccz::editor_preview::BattleWritebackNativeNodeSnapshot node;
		if (!CaptureNode(handle, node) ||
			!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node))
			return false;
		const HTREEITEM item = reinterpret_cast<HTREEITEM>(handle);
		auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(item));
		if (data == nullptr || !tree_.DeleteItem(item)) return false;
		ccz::editor::ReleaseLongCharData(
			data->long_char_data, data->long_char_data_capacity);
		delete data;
		return true;
	}

	bool RestoreBattleNodeBefore(
		const ccz::editor_preview::BattleWritebackNativeNodeSnapshot& node,
		std::uintptr_t before_handle,
		std::uintptr_t& restored_handle) override
	{
		restored_handle = 0;
		// Undo rollback may need to restore an inserted 0x46/0x47 page after a
		// later inverse operation fails. Keep restoration aligned with the same
		// exact deployment-node policy used by deletion.
		if (!ccz::editor_preview::CanDeleteBattleWritebackNativeNode(node) ||
			node.parent_handle == 0 ||
		!NodeExists(node.parent_handle)) return false;
		const HTREEITEM parent = reinterpret_cast<HTREEITEM>(node.parent_handle);
		HTREEITEM after = TVI_LAST;
		if (before_handle != 0) {
			if (!NodeExists(before_handle)) return false;
			const HTREEITEM before = reinterpret_cast<HTREEITEM>(before_handle);
			if (tree_.GetParentItem(before) != parent) return false;
			const HTREEITEM previous = tree_.GetPrevSiblingItem(before);
			after = previous != nullptr ? previous : TVI_FIRST;
		}

		std::unique_ptr<ItemData> data;
		try {
			data = std::make_unique<ItemData>(
				static_cast<int>(node.int_data.size()));
			data->id = node.instruction_id;
			data->ord = node.ordinal;
			data->int_data.assign(node.int_data.begin(), node.int_data.end());
		} catch (...) {
			return false;
		}
		TVINSERTSTRUCT insert{};
		insert.hParent = parent;
		insert.hInsertAfter = after;
		insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE |
			TVIF_SELECTEDIMAGE;
		insert.item.pszText = view_.code[node.instruction_id];
		insert.item.lParam = 0;
		insert.item.iImage = 0;
		insert.item.iSelectedImage = 1;
		const HTREEITEM restored = tree_.InsertItem(&insert);
		if (restored == nullptr) return false;
		if (!tree_.SetItemData(restored, reinterpret_cast<DWORD_PTR>(data.get()))) {
			tree_.DeleteItem(restored);
			return false;
		}
		try {
			view_.UpdateShow(restored);
		} catch (...) {
			tree_.DeleteItem(restored);
			return false;
		}
		data.release();
		restored_handle = reinterpret_cast<std::uintptr_t>(restored);
		return true;
	}

	bool MarkDocumentModified() override
	{
		if (document_ == nullptr) return false;
		document_->OnModified(TRUE);
		document_->SetModifiedFlag(TRUE);
		return document_->modified && document_->IsModified();
	}

	bool RestoreDocumentState(
		const ccz::editor_preview::BattleWritebackNativeDocumentState& state,
		const std::vector<ccz::editor_preview::BattleWritebackNativeHandleMapping>&
			remapped_handles) override
	{
		if (document_ == nullptr || state.next_ordinal < 0) return false;
		std::uintptr_t selected = state.selected_handle;
		for (const auto& mapping : remapped_handles) {
			if (mapping.old_handle == selected) {
				selected = mapping.new_handle;
				break;
			}
		}
		if (selected != 0 && !TreeItemExists(selected)) return false;

		const HTREEITEM previous_selection = tree_.GetSelectedItem();
		const HTREEITEM target = reinterpret_cast<HTREEITEM>(selected);
		{
			BattlePreviewRefreshSuppression suppress(view_);
			if (target != nullptr && !tree_.SelectItem(target)) {
				if (previous_selection != nullptr)
					tree_.SelectItem(previous_selection);
				return false;
			}
			if (target == nullptr) tree_.SelectItem(nullptr);
			view_.cur_item = target;
		}
		view_.cur_code_ord = state.next_ordinal;
		document_->OnModified(state.custom_modified ? TRUE : FALSE);
		document_->SetModifiedFlag(state.framework_modified ? TRUE : FALSE);
		// OnModified(TRUE) advances the content epoch even though this restore
		// only reinstates dirty flags and selection. Keep the cached full-tree
		// capture current across that known non-content version bump.
		if (last_captured_battle_.ok) {
			last_capture_document_id_ = document_->GetSPreviewDocumentId();
			last_capture_content_epoch_ = document_->GetPreviewContentEpoch();
			last_capture_tree_generation_ =
				document_->GetPreviewTreeGeneration();
		}
		return true;
	}

	std::uintptr_t first_inserted_handle() const noexcept
	{
		return first_inserted_handle_;
	}

private:
	CcczEditor2View& view_;
	CcczEditor2Doc* document_{};
	CTreeCtrl& tree_;
	HTREEITEM revision_section_{};
	ccz::editor_preview::BattleWritebackNativeDocumentState initial_state_;
	std::uint64_t expected_document_id_{};
	ccz::editor_preview::BattlePreviewDocumentVersion expected_version_{};
	mutable ccz::editor_preview::BattlePreviewCapture last_captured_battle_;
	mutable std::uint64_t last_capture_document_id_{};
	mutable std::uint64_t last_capture_content_epoch_{};
	mutable std::uint64_t last_capture_tree_generation_{};
	mutable bool initial_state_pending_{ true };
	std::uintptr_t first_inserted_handle_{};
};

const ccz::preview::RawReplayNode* find_logical_node(
	const std::vector<ccz::preview::RawReplayNode>& nodes,
	const ccz::preview::LogicalNodeKey& key)
{
	for (const auto& node : nodes) {
		if (node.logical_key == key) return &node;
		if (const auto* found = find_logical_node(node.children, key))
			return found;
	}
	return nullptr;
}

bool contains_logical_node(
	const std::vector<ccz::preview::RawReplayNode>& nodes,
	const ccz::preview::LogicalNodeKey& key)
{
	return find_logical_node(nodes, key) != nullptr;
}

bool is_current_terminal_anchor(
	const std::vector<ccz::preview::RawReplayNode>& nodes,
	const ccz::preview::LogicalNodeKey& parent_key,
	const ccz::preview::LogicalNodeKey& before_key)
{
	const auto* parent = find_logical_node(nodes, parent_key);
	return parent != nullptr && !parent->children.empty() &&
		parent->children.back().instruction_id == 0x00 &&
		parent->children.back().logical_key == before_key;
}

bool build_fresh_editor_state_for_scope(
	const ccz::editor_preview::BattlePreviewCapture& capture,
	const ccz::editor_preview::BattleConditionScope& scope,
	const ccz::editor_preview::BattleEditorState& old_state,
	ccz::editor_preview::BattleEditorState& fresh_state,
	std::wstring& diagnostic)
{
	diagnostic.clear();
	for (const auto& key : scope.reachable_deployment_keys) {
		if (!in_scope(key, capture.input.scope_key) ||
			!contains_logical_node(capture.input.nodes, key)) {
			diagnostic = L"来源 " + key + L" 已从 Scene1 条件分支中消失";
			return false;
		}
	}
	for (const auto& anchor : scope.insertion_anchors) {
		if (!anchor.resolved) continue;
		if (!in_scope(anchor.parent_key, capture.input.scope_key) ||
			!in_scope(anchor.before_key, capture.input.scope_key) ||
			!is_current_terminal_anchor(capture.input.nodes,
				anchor.parent_key, anchor.before_key)) {
			diagnostic = L"Scene1 条件分支的新增指令锚点已变化";
			return false;
		}
	}
	if (scope.insertion_anchor_resolved &&
		(!in_scope(scope.insert_parent_key, capture.input.scope_key) ||
		 !in_scope(scope.insert_before_key, capture.input.scope_key) ||
		 !is_current_terminal_anchor(capture.input.nodes,
			scope.insert_parent_key, scope.insert_before_key))) {
		diagnostic = L"Scene1 条件分支的新增指令位置已变化";
		return false;
	}

	ccz::preview::BattleParseOptions options;
	options.scope_root_key = capture.input.scope_key;
	options.filter_deployment_nodes = true;
	options.reachable_deployment_keys = scope.reachable_deployment_keys;
	const auto parsed = ccz::preview::parse_battle_instructions(
		capture.input.document_path, capture.input.nodes, options);
	if (!parsed.ok) {
		diagnostic = parsed.state.diagnostic.empty()
			? L"新 Scene1 来源无法按原条件分支解析" : parsed.state.diagnostic;
		return false;
	}
	const auto& parsedMap = parsed.state.map;
	const auto& oldMap = old_state.battle.map;
	if (parsedMap.map_id != oldMap.map_id ||
		(parsedMap.width > 0 && oldMap.width > 0 &&
			parsedMap.width != oldMap.width) ||
		(parsedMap.height > 0 && oldMap.height > 0 &&
			parsedMap.height != oldMap.height)) {
		diagnostic = L"地图或网格尺寸已变化，无法在保留当前临时布置的同时安全合并";
		return false;
	}

	fresh_state = old_state;
	fresh_state.battle = parsed.state;
	// The instruction parser knows the map ID, but map-cell dimensions are
	// resolved from the JPG. Reuse that proven geometry for the same map so a
	// draft rebase does not replace it with the parser's default 0x0 projection.
	if (oldMap.width > 0 && oldMap.height > 0) {
		fresh_state.battle.map.width = oldMap.width;
		fresh_state.battle.map.height = oldMap.height;
		fresh_state.battle.map.offset_x = oldMap.offset_x;
		fresh_state.battle.map.offset_y = oldMap.offset_y;
		fresh_state.battle.map.projection_valid = oldMap.projection_valid;
	}
	fresh_state.battle.terrain = old_state.battle.terrain;
	fresh_state.scope_root = capture.input.scope_key;
	fresh_state.next_cache_id = 1;
	fresh_state.selection.reset();
	fresh_state.adjusting_cache_id = 0;
	for (auto& unit : fresh_state.battle.units) {
		if (!unit.fields_decoded ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
		unit.editor_deployment_instruction = unit.source_instruction;
		unit.editor_origin_fields = unit.raw_fields;
	}
	ccz::editor_preview::BattleEditorOps assignIdentities(fresh_state);
	return true;
}

}

BEGIN_MESSAGE_MAP(CSBattlePreviewPage, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_CAPTURECHANGED()
	ON_WM_MOUSEMOVE()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_CONTROL(EN_CHANGE, kSearchId, &CSBattlePreviewPage::OnSearchChanged)
	ON_CONTROL(LBN_SELCHANGE, kCharacterListId,
		&CSBattlePreviewPage::OnCharacterSelectionChanged)
	ON_CONTROL(CBN_SELCHANGE, kDeploymentComboId,
		&CSBattlePreviewPage::OnDeploymentSelectionChanged)
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(kCharacterTabId, &CSBattlePreviewPage::OnCharacterTab)
	ON_BN_CLICKED(kBrushTabId, &CSBattlePreviewPage::OnBrushTab)
	ON_BN_CLICKED(kHideSidebarId, &CSBattlePreviewPage::OnHideSidebar)
	ON_BN_CLICKED(kShowSidebarId, &CSBattlePreviewPage::OnShowSidebar)
	ON_BN_CLICKED(kPreviousPageId,
		&CSBattlePreviewPage::OnPreviousCharacterPage)
	ON_BN_CLICKED(kNextPageId, &CSBattlePreviewPage::OnNextCharacterPage)
	ON_BN_CLICKED(kUndoId, &CSBattlePreviewPage::OnUndo)
	ON_BN_CLICKED(kQuickModeId, &CSBattlePreviewPage::OnQuickModeClicked)
	ON_BN_CLICKED(kConditionTreeId, &CSBattlePreviewPage::OnConditionTreeClicked)
	ON_MESSAGE(kConditionActionMessage,
		&CSBattlePreviewPage::OnConditionActionMessage)
	ON_BN_CLICKED(kWritebackId, &CSBattlePreviewPage::OnWriteback)
END_MESSAGE_MAP()

CSBattlePreviewPage::~CSBattlePreviewPage()
{
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	FinishPerformanceActivation();
	#endif
	UnbindSession();
	ReleaseActiveBitmapPins();
	ClearUnitSprites();
	m_decodedBitmaps.clear();
	m_bitmapCachePolicy.Clear();
	m_assets.Clear();
}

#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
CSBattlePreviewPage::PerformanceActivationScope::PerformanceActivationScope(
	CSBattlePreviewPage& page, const wchar_t* document_path)
	: page_(page)
{
	page_.BeginPerformanceActivation(document_path);
}

CSBattlePreviewPage::PerformanceActivationScope::~PerformanceActivationScope()
{
	page_.FinishPerformanceActivationOnRequestReturn();
}

void CSBattlePreviewPage::BeginPerformanceActivation(
	const wchar_t* document_path)
{
	FinishPerformanceActivation();
	m_perfCounters = {};
	m_perfAwaitingFirstPaint = false;
	m_perfDocumentPath = document_path != nullptr ? document_path : L"";
	const std::wstring qa_root = battle_writeback_qa_copy_root();
	if (qa_root.empty() ||
		!ccz::editor_preview::IsBattleWritebackQaCopy(
			m_perfDocumentPath, qa_root)) {
		m_perfDocumentPath.clear();
		return;
	}
	m_perfStarted = std::chrono::steady_clock::now();
	m_perfActive = true;
}

void CSBattlePreviewPage::FinishPerformanceActivationOnRequestReturn()
{
	if (m_perfActive && !m_perfAwaitingFirstPaint)
		FinishPerformanceActivation();
}

void CSBattlePreviewPage::FinishPerformanceActivation()
{
	if (!m_perfActive) return;
	m_perfCounters.elapsed_ms = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - m_perfStarted).count());
	try {
		const std::filesystem::path document(m_perfDocumentPath);
		const auto log_path = document.parent_path() / L"SPreviewPerf.log";
		std::ofstream log(log_path, std::ios::out | std::ios::app | std::ios::binary);
		if (log) {
			const int bytes = WideCharToMultiByte(CP_UTF8, 0,
				m_perfDocumentPath.data(),
				static_cast<int>(m_perfDocumentPath.size()), nullptr, 0,
				nullptr, nullptr);
			std::string path_utf8(static_cast<std::size_t>((std::max)(0, bytes)), '\0');
			if (bytes > 0) {
				WideCharToMultiByte(CP_UTF8, 0, m_perfDocumentPath.data(),
					static_cast<int>(m_perfDocumentPath.size()), path_utf8.data(),
					bytes, nullptr, nullptr);
			}
			log << "SPreviewPerf;doc=" << path_utf8
				<< ";full_document_captures="
				<< m_perfCounters.full_document_captures
				<< ";pinned_section_captures="
				<< m_perfCounters.pinned_section_captures
				<< ";battle_input_builds="
				<< m_perfCounters.battle_input_builds
				<< ";battle_loads=" << m_perfCounters.battle_loads
				<< ";map_decodes=" << m_perfCounters.map_decodes
				<< ";sprite_decodes=" << m_perfCounters.sprite_decodes
				<< ";elapsed_ms=" << m_perfCounters.elapsed_ms << "\r\n";
		}
	} catch (...) {
		// QA telemetry must never affect preview state or writeback decisions.
	}
	m_perfActive = false;
	m_perfAwaitingFirstPaint = false;
	m_perfDocumentPath.clear();
}

void CSBattlePreviewPage::RecordFullDocumentCapture()
{
	if (m_perfActive) ++m_perfCounters.full_document_captures;
}

void CSBattlePreviewPage::RecordPinnedSectionCapture()
{
	if (m_perfActive) ++m_perfCounters.pinned_section_captures;
}

void CSBattlePreviewPage::RecordBattleInputBuild()
{
	if (m_perfActive) ++m_perfCounters.battle_input_builds;
}

void CSBattlePreviewPage::RecordBattleLoad()
{
	if (m_perfActive) ++m_perfCounters.battle_loads;
}

void CSBattlePreviewPage::RecordMapDecode()
{
	if (m_perfActive) ++m_perfCounters.map_decodes;
}

void CSBattlePreviewPage::RecordSpriteDecode()
{
	if (m_perfActive) ++m_perfCounters.sprite_decodes;
}
#endif

BOOL CSBattlePreviewPage::CreatePage(CWnd* parent, UINT control_id)
{
	const CString windowClass = AfxRegisterWndClass(
		CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
	return CreateEx(0, windowClass, _T("S 战场预览"),
		WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
			WS_HSCROLL | WS_VSCROLL,
		CRect(0, 0, 1, 1), parent, control_id);
}

ccz::editor_preview::BattlePreviewDocumentSession&
CSBattlePreviewPage::ActiveSession() noexcept
{
	return m_activeSession != nullptr ? *m_activeSession : m_unboundSession;
}

const ccz::editor_preview::BattlePreviewDocumentSession&
CSBattlePreviewPage::ActiveSession() const noexcept
{
	return m_activeSession != nullptr ? *m_activeSession : m_unboundSession;
}

void CSBattlePreviewPage::BindSession(
	ccz::editor_preview::BattlePreviewDocumentSession& session,
	CcczEditor2Doc& document, CTreeCtrl& tree)
{
	if (m_activeSession != &session) {
		UnbindSession();
		m_activeSession = &session;
		m_editorOps = nullptr;
		if (!session.active_workspace_key.empty()) {
			const auto workspace = session.condition_workspaces.find(
				session.active_workspace_key);
			if (workspace != session.condition_workspaces.end())
				m_editorOps = workspace->second->operations.get();
		}
		RestoreActiveSessionUiState();
	}
	m_activeDocument = &document;
	session.document_id = document.GetSPreviewDocumentId();
	if (session.document_path.empty())
		session.document_path = document.pathName;
	m_documentPath = session.document_path.c_str();
	const bool sessionTreeStillCurrent = !session.has_baseline ||
		session.observed_version.tree_generation ==
			document.GetPreviewTreeGeneration();
	m_sourceTreeWindow = sessionTreeStillCurrent
		? tree.GetSafeHwnd() : nullptr;
	m_panActive = false;
	m_leftStampActive = false;
	m_panStartPoint = CPoint(-1, -1);
	m_panStartScrollX = m_panStartScrollY = 0;
	m_wheelRemainder = 0;
	m_lastStampCell.reset();
	m_hoverCell.reset();
	if (session.baseline_capture.ok) {
		std::wstring assetDiagnostic;
		m_assets.LoadForDocument(session.baseline_capture.input.document_path,
			session.editor_state.battle.map.map_id, &assetDiagnostic);
		UpdateSessionAssetKeys();
		if (!m_unitSpritePath.empty() &&
			(m_unitSpritePath != m_assets.unit_mov_path() ||
			 m_unitSpriteRevision != m_assets.unit_mov_revision())) {
			ClearUnitSprites();
		}
		if (!assetDiagnostic.empty()) AppendDiagnostic(assetDiagnostic);
		LoadMapImage();
	}
	if (m_controlsCreated) RefreshEditorControls();
}

void CSBattlePreviewPage::SaveActiveSessionUiState()
{
	if (m_activeSession == nullptr) return;
	auto& session = *m_activeSession;
	auto& ui = session.ui;
	ui.zoom = m_zoom;
	ui.scroll_x = m_scrollX;
	ui.scroll_y = m_scrollY;
	ui.sidebar_visible = m_sidebarVisible;
	ui.brush_tab_active = m_brushTabActive;
	ui.quick_mode_enabled = ActiveSession().ui.quick_mode_enabled;
	ui.deployment_instruction = session.editor_state.placement.instruction_id;
	ui.selected_character_data_id =
		session.editor_state.characters.selected_data_id;
	ui.selected_brush_data_id = session.editor_state.selected_minion_brush
		? session.editor_state.selected_minion_brush->first_data_id : -1;
	ui.selected_unit_cache_id = 0;
	if (m_selectedUnit && *m_selectedUnit < session.editor_state.battle.units.size())
		ui.selected_unit_cache_id =
			session.editor_state.battle.units[*m_selectedUnit].editor_cache_id;
	if (m_searchEdit.GetSafeHwnd() != nullptr) {
		CString search;
		m_searchEdit.GetWindowText(search);
		ui.search_text = search.GetString();
	}
	session.document_path = m_documentPath.GetString();
	session.diagnostic = m_diagnostic.GetString();
	session.quick_mode_diagnostic = m_quickModeDiagnostic.GetString();
	if (!session.active_workspace_key.empty()) {
		const auto workspace = session.condition_workspaces.find(
			session.active_workspace_key);
		if (workspace != session.condition_workspaces.end())
			workspace->second->state = session.editor_state;
	}
}

void CSBattlePreviewPage::RestoreActiveSessionUiState()
{
	if (m_activeSession == nullptr) return;
	auto& session = *m_activeSession;
	auto& ui = session.ui;
	if (!ui.initialized) {
		ui.quick_mode_enabled = AfxGetApp()->GetProfileInt(
			_T("S Battle Preview"), _T("QuickMode"), 1) != 0;
		ui.initialized = true;
	}
	m_zoom = ui.zoom > 0 ? ui.zoom : kDefaultZoom;
	m_scrollX = ui.scroll_x;
	m_scrollY = ui.scroll_y;
	m_sidebarVisible = ui.sidebar_visible;
	m_brushTabActive = ui.brush_tab_active;
	m_diagnostic = session.diagnostic.c_str();
	m_quickModeDiagnostic = session.quick_mode_diagnostic.c_str();
	if (ui.initialized && session.has_baseline) {
		session.editor_state.characters.query = ui.search_text;
		session.editor_state.characters.selected_data_id =
			ui.selected_character_data_id;
		session.editor_state.placement.instruction_id =
			ui.deployment_instruction;
		if (ui.selected_brush_data_id >= 0) {
			const auto brush = std::find_if(
				session.editor_state.minion_brushes.begin(),
				session.editor_state.minion_brushes.end(),
				[&ui](const auto& item) {
					return item.first_data_id == ui.selected_brush_data_id;
				});
			if (brush != session.editor_state.minion_brushes.end())
				session.editor_state.selected_minion_brush = *brush;
		} else {
			session.editor_state.selected_minion_brush.reset();
		}
	}
	m_restoringSessionUi = true;
	if (m_searchEdit.GetSafeHwnd() != nullptr)
		m_searchEdit.SetWindowText(ui.search_text.c_str());
	if (m_quickModeButton.GetSafeHwnd() != nullptr)
		m_quickModeButton.SetCheck(ui.quick_mode_enabled
			? BST_CHECKED : BST_UNCHECKED);
	m_restoringSessionUi = false;
	RefreshSelectedUnitIndex();
	if (ui.selected_unit_cache_id != 0) {
		for (std::size_t index = 0;
			index < session.editor_state.battle.units.size(); ++index) {
			if (session.editor_state.battle.units[index].editor_cache_id ==
				ui.selected_unit_cache_id) {
				m_selectedUnit = index;
				break;
			}
		}
	}
	if (m_controlsCreated) {
		LayoutEditorControls();
		RefreshEditorControls();
		RefreshConditionTree();
	}
	UpdateViewport();
}

void CSBattlePreviewPage::UnbindSession()
{
	if (m_activeSession == nullptr) return;
	if ((m_panActive || m_leftStampActive) && GetSafeHwnd() != nullptr)
		ReleaseCapture();
	SaveActiveSessionUiState();
	ReleaseActiveBitmapPins();
	m_panActive = false;
	m_leftStampActive = false;
	m_panStartPoint = CPoint(-1, -1);
	m_panStartScrollX = m_panStartScrollY = 0;
	m_wheelRemainder = 0;
	m_lastStampCell.reset();
	m_hoverCell.reset();
	m_activeSession = nullptr;
	m_activeDocument = nullptr;
	m_editorOps = nullptr;
	m_sourceTreeWindow = nullptr;
	m_documentPath.Empty();
	m_selectedUnit.reset();
	if (m_conditionTreePane.GetSafeHwnd() != nullptr) {
		m_conditionTreePane.UpdateModel(nullptr, {}, {});
		m_conditionTreePane.ShowPane(false);
	}
	if (m_controlsCreated) RefreshEditorControls();
	if (GetSafeHwnd() != nullptr) Invalidate(FALSE);
}

int CSBattlePreviewPage::OnCreate(LPCREATESTRUCT create)
{
	if (CWnd::OnCreate(create) == -1) return -1;
	ShowScrollBar(SB_HORZ, FALSE);
	ShowScrollBar(SB_VERT, FALSE);
	CreateEditorControls();
	LayoutEditorControls();
	return 0;
}

void CSBattlePreviewPage::CreateEditorControls()
{
	const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
	const DWORD controlStyle = WS_CHILD | WS_VISIBLE;
	if (!m_characterTabButton.Create(_T("角色列表"), buttonStyle,
		CRect(0, 0, 1, 1), this, kCharacterTabId) ||
		!m_brushTabButton.Create(_T("小兵笔刷"), buttonStyle,
			CRect(0, 0, 1, 1), this, kBrushTabId) ||
		!m_hideSidebarButton.Create(_T("隐藏"), buttonStyle,
			CRect(0, 0, 1, 1), this, kHideSidebarId) ||
		!m_showSidebarButton.Create(_T("角色/笔刷"), buttonStyle,
			CRect(0, 0, 1, 1), this, kShowSidebarId) ||
		!m_previousPageButton.Create(_T("上一页"), buttonStyle,
			CRect(0, 0, 1, 1), this, kPreviousPageId) ||
		!m_nextPageButton.Create(_T("下一页"), buttonStyle,
			CRect(0, 0, 1, 1), this, kNextPageId) ||
		!m_undoButton.Create(_T("撤回"), buttonStyle,
			CRect(0, 0, 1, 1), this, kUndoId) ||
		!m_quickModeButton.Create(_T("快速模式"),
			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
			CRect(0, 0, 1, 1), this, kQuickModeId) ||
		!m_conditionTreeButton.Create(_T("条件树"), buttonStyle,
			CRect(0, 0, 1, 1), this, kConditionTreeId) ||
		!m_writebackButton.Create(_T("写回"), buttonStyle,
			CRect(0, 0, 1, 1), this, kWritebackId) ||
		!m_searchEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
			CRect(0, 0, 1, 1), this, kSearchId) ||
		!m_deploymentCombo.Create(controlStyle | WS_VSCROLL | CBS_DROPDOWNLIST,
			CRect(0, 0, 1, 1), this, kDeploymentComboId) ||
		!m_characterList.Create(controlStyle | WS_BORDER | WS_VSCROLL |
			LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
			CRect(0, 0, 1, 1), this, kCharacterListId) ||
		!m_pageLabel.Create(_T("0 / 0"), controlStyle | SS_CENTER,
			CRect(0, 0, 1, 1), this, kPageLabelId)) {
		AppendDiagnostic(L"S 战场角色侧栏控件创建失败");
		return;
	}
	m_showSidebarButton.ShowWindow(SW_HIDE);
	if (CFont* font = GetFont()) {
		m_characterTabButton.SetFont(font);
		m_brushTabButton.SetFont(font);
		m_hideSidebarButton.SetFont(font);
		m_showSidebarButton.SetFont(font);
		m_previousPageButton.SetFont(font);
		m_nextPageButton.SetFont(font);
		m_undoButton.SetFont(font);
		m_quickModeButton.SetFont(font);
		m_conditionTreeButton.SetFont(font);
		m_writebackButton.SetFont(font);
		m_searchEdit.SetFont(font);
		m_deploymentCombo.SetFont(font);
		m_characterList.SetFont(font);
		m_pageLabel.SetFont(font);
	}
	m_quickModeButton.SetCheck(ActiveSession().ui.quick_mode_enabled ? BST_CHECKED : BST_UNCHECKED);
	if (!m_conditionTreePane.CreatePane(this,
		[this](const ccz::preview::LogicalNodeKey& nodeKey,
			ccz::preview::ConditionTreeAction action,
			std::int32_t integerValue) {
			ApplyConditionActionAndRefresh(nodeKey, action, integerValue);
		})) {
		AppendDiagnostic(L"S 战场条件树控件创建失败");
	}
	m_controlsCreated = true;
	RefreshEditorControls();
}

void CSBattlePreviewPage::LayoutEditorControls()
{
	if (GetSafeHwnd() == nullptr) return;
	CRect client;
	GetClientRect(&client);
	const int panelWidth = m_sidebarVisible
		? (std::min)(kSidebarWidth, (std::max)(0, client.Width())) : 0;
	m_canvasRect.SetRect(0, 0,
		(std::max)(0, client.Width() - panelWidth), client.Height());
	if (!m_controlsCreated) {
		UpdateViewport();
		Invalidate(FALSE);
		return;
	}
	if (m_sidebarVisible) {
		const int left = m_canvasRect.right + 4;
		const int right = (std::max)(left,
			static_cast<int>(client.right) - 4);
		const int width = right - left;
		const int tabGap = 3;
		const int roleWidth = (std::max)(48, (width - 2 * tabGap) * 36 / 100);
		const int brushWidth = (std::max)(48, (width - 2 * tabGap) * 36 / 100);
		const int hideWidth = (std::max)(32, width - roleWidth - brushWidth - 2 * tabGap);
		int y = 6;
		m_characterTabButton.MoveWindow(left, y, roleWidth, 24);
		m_brushTabButton.MoveWindow(left + roleWidth + tabGap, y,
			brushWidth, 24);
		m_hideSidebarButton.MoveWindow(left + roleWidth + tabGap + brushWidth + tabGap,
			y, hideWidth, 24);
		y += 30;
		m_quickModeButton.MoveWindow(left, y, width, 22);
		y += 26;
		if (ActiveSession().condition_button_visible) {
			m_conditionTreeButton.MoveWindow(left, y, width, 22);
			m_conditionTreeButton.ShowWindow(SW_SHOW);
			y += 26;
		} else {
			m_conditionTreeButton.ShowWindow(SW_HIDE);
		}
		m_searchEdit.MoveWindow(left, y, width, 22);
		y += 26;
		m_deploymentCombo.MoveWindow(left, y, width, 160);
		y += 28;
		const int bottom = client.bottom - 8;
		const int undoY = (std::max)(y, bottom - 54);
		m_characterList.MoveWindow(left, y, width, (std::max)(0, undoY - y - 3));
		const int writebackWidth = (std::min)(96, width);
		const int undoWidth = (std::max)(0, width - writebackWidth - tabGap);
		m_undoButton.MoveWindow(left, undoY, undoWidth, 24);
		m_writebackButton.MoveWindow(left + undoWidth + tabGap, undoY,
			writebackWidth, 24);
		const int pageY = bottom - 26;
		const int pageGap = 3;
		const int pageButtonWidth = (std::max)(38, (width - 2 * pageGap - 70) / 2);
		m_previousPageButton.MoveWindow(left, pageY, pageButtonWidth, 22);
		m_pageLabel.MoveWindow(left + pageButtonWidth + pageGap, pageY,
			70, 22);
		m_nextPageButton.MoveWindow(left + pageButtonWidth + pageGap + 70 + pageGap,
			pageY, (std::max)(0, width - pageButtonWidth - 70 - 2 * pageGap), 22);
		m_showSidebarButton.ShowWindow(SW_HIDE);
		for (CWnd* control : {static_cast<CWnd*>(&m_characterTabButton),
			static_cast<CWnd*>(&m_brushTabButton),
			static_cast<CWnd*>(&m_hideSidebarButton),
			static_cast<CWnd*>(&m_previousPageButton),
			static_cast<CWnd*>(&m_nextPageButton),
			static_cast<CWnd*>(&m_undoButton),
			static_cast<CWnd*>(&m_writebackButton),
			static_cast<CWnd*>(&m_quickModeButton),
			static_cast<CWnd*>(&m_searchEdit), static_cast<CWnd*>(&m_deploymentCombo),
			static_cast<CWnd*>(&m_characterList), static_cast<CWnd*>(&m_pageLabel)}) {
			if (control->GetSafeHwnd() != nullptr) control->ShowWindow(SW_SHOW);
		}
	} else {
		for (CWnd* control : {static_cast<CWnd*>(&m_characterTabButton),
			static_cast<CWnd*>(&m_brushTabButton),
			static_cast<CWnd*>(&m_hideSidebarButton),
			static_cast<CWnd*>(&m_previousPageButton),
			static_cast<CWnd*>(&m_nextPageButton),
			static_cast<CWnd*>(&m_undoButton),
			static_cast<CWnd*>(&m_writebackButton),
			static_cast<CWnd*>(&m_quickModeButton),
			static_cast<CWnd*>(&m_searchEdit), static_cast<CWnd*>(&m_deploymentCombo),
			static_cast<CWnd*>(&m_characterList), static_cast<CWnd*>(&m_pageLabel)}) {
			if (control->GetSafeHwnd() != nullptr) control->ShowWindow(SW_HIDE);
		}
		const int buttonWidth = 96;
		m_showSidebarButton.MoveWindow(
			(std::max)(0, static_cast<int>(client.right) - buttonWidth - 8), 6,
			buttonWidth, 24);
		m_showSidebarButton.ShowWindow(SW_SHOW);
		m_quickModeButton.MoveWindow(8, 34,
			(std::min)(118, (std::max)(0, m_canvasRect.Width() - 16)), 24);
		m_quickModeButton.ShowWindow(SW_SHOW);
		if (ActiveSession().condition_button_visible) {
			m_conditionTreeButton.MoveWindow(132, 34,
				(std::min)(96, (std::max)(0, m_canvasRect.Width() - 140)), 24);
			m_conditionTreeButton.ShowWindow(SW_SHOW);
		} else {
			m_conditionTreeButton.ShowWindow(SW_HIDE);
		}
	}
	UpdateViewport();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::RefreshEditorControls(bool refreshPlacementChoices)
{
	if (!m_controlsCreated) return;
	m_characterTabButton.SetWindowText(m_brushTabActive
		? _T("角色列表") : _T("角色列表 •"));
	m_brushTabButton.SetWindowText(m_brushTabActive
		? _T("小兵笔刷 •") : _T("小兵笔刷"));
	if (refreshPlacementChoices) {
		RefreshCharacterList();
		RefreshDeploymentList();
	}
	RefreshSelectedUnitIndex();
	const std::size_t pages = m_editorOps
		? m_editorOps->CharacterPageCount() : 0;
	CString pageText;
	if (m_brushTabActive) {
		pageText.Format(_T("%u 组"), static_cast<unsigned>(
			ActiveSession().editor_state.minion_brushes.size()));
	} else {
		const std::size_t current = pages == 0 ? 0
			: ActiveSession().editor_state.characters.current_page + 1;
		pageText.Format(_T("%u / %u"), static_cast<unsigned>(current),
			static_cast<unsigned>(pages));
	}
	m_pageLabel.SetWindowText(pageText);
	m_previousPageButton.EnableWindow(!m_brushTabActive &&
		ActiveSession().editor_state.characters.current_page > 0);
	m_nextPageButton.EnableWindow(!m_brushTabActive && pages > 0 &&
		ActiveSession().editor_state.characters.current_page + 1 < pages);
	m_undoButton.EnableWindow(!ActiveSession().stale &&
		(ActiveSession().native_writeback_transaction.undo_available() ||
			(m_editorOps && m_editorOps->CanUndo())));
	m_writebackButton.EnableWindow(m_editorOps != nullptr &&
		ActiveSession().baseline_capture.ok &&
		ActiveSession().active_condition_scope.writeback_scope_resolved &&
		!ActiveSession().stale && !ActiveSession().native_writeback_rebase_required);
	m_deploymentCombo.EnableWindow(m_editorOps != nullptr);
	LayoutEditorControls();
}

void CSBattlePreviewPage::RefreshCharacterList()
{
	if (!m_controlsCreated) return;
	m_characterList.SetRedraw(FALSE);
	m_characterList.ResetContent();
	const std::wstring query = ActiveSession().editor_state.characters.query;
	if (m_brushTabActive) {
		for (const auto& brush : ActiveSession().editor_state.minion_brushes) {
			std::wostringstream label;
			label << brush.name << L"  (" << brush.first_data_id << L"-"
				<< brush.last_data_id << L")";
			if (!contains_ignore_case(label.str(), query)) continue;
			const int index = m_characterList.AddString(label.str().c_str());
			if (index != LB_ERR && index != LB_ERRSPACE) {
				m_characterList.SetItemData(index,
					static_cast<DWORD_PTR>(brush.first_data_id));
				if (ActiveSession().editor_state.selected_minion_brush &&
					ActiveSession().editor_state.selected_minion_brush->first_data_id ==
					brush.first_data_id) m_characterList.SetCurSel(index);
			}
		}
	} else if (m_editorOps) {
		for (const auto& character : m_editorOps->VisibleCharacters()) {
			std::wostringstream label;
			label << character.name << L"  (Data " << character.data_id << L")";
			const int index = m_characterList.AddString(label.str().c_str());
			if (index != LB_ERR && index != LB_ERRSPACE) {
				m_characterList.SetItemData(index,
					static_cast<DWORD_PTR>(character.data_id));
				if (ActiveSession().editor_state.characters.selected_data_id == character.data_id)
					m_characterList.SetCurSel(index);
			}
		}
	}
	m_characterList.SetRedraw(TRUE);
	m_characterList.Invalidate(FALSE);
}

void CSBattlePreviewPage::RefreshBrushList()
{
	RefreshCharacterList();
}

void CSBattlePreviewPage::RefreshDeploymentList()
{
	if (!m_controlsCreated) return;
	const std::uint16_t active = ActiveSession().editor_state.placement.instruction_id;
	m_deploymentCombo.ResetContent();
	const struct DeploymentChoice { LPCTSTR label; std::uint16_t instruction; } choices[] = {
		{ _T("我军出场 (0x4B)"), 0x4b },
		{ _T("友军出场 (0x46)"), 0x46 },
		{ _T("敌军出场 (0x47)"), 0x47 },
	};
	int selected = CB_ERR;
	for (const auto& choice : choices) {
		const int item = m_deploymentCombo.AddString(choice.label);
		if (item == CB_ERR || item == CB_ERRSPACE) continue;
		m_deploymentCombo.SetItemData(item, choice.instruction);
		if (choice.instruction == active) selected = item;
	}
	if (selected != CB_ERR) m_deploymentCombo.SetCurSel(selected);
}

void CSBattlePreviewPage::RefreshSelectedUnitIndex()
{
	m_selectedUnit.reset();
	if (!ActiveSession().editor_state.selection) return;
	for (std::size_t index = 0; index < ActiveSession().editor_state.battle.units.size(); ++index) {
		const auto& unit = ActiveSession().editor_state.battle.units[index];
		if (unit.editor_pending_delete ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
		const bool byCache = ActiveSession().editor_state.selection->cache_id != 0 &&
			unit.editor_cache_id == ActiveSession().editor_state.selection->cache_id;
		const bool bySource = ActiveSession().editor_state.selection->cache_id == 0 &&
			unit.source_key == ActiveSession().editor_state.selection->source_key &&
			unit.record_index == ActiveSession().editor_state.selection->record_index &&
			static_cast<std::size_t>(unit.page) == ActiveSession().editor_state.selection->page;
		if (byCache || bySource) {
			m_selectedUnit = index;
			return;
		}
	}
}

void CSBattlePreviewPage::BindSelectedDeployment()
{
	if (!m_editorOps) return;
	const int selected = m_deploymentCombo.GetCurSel();
	if (selected == CB_ERR) return;
	const auto result = m_editorOps->SetDeploymentInstruction(
		static_cast<std::uint16_t>(m_deploymentCombo.GetItemData(selected)));
	ShowOperationResult(result);
}

void CSBattlePreviewPage::ShowOperationResult(
	const ccz::editor_preview::BattleEditorResult& result,
	bool refreshPlacementChoices)
{
	if (!result.diagnostic.empty()) m_diagnostic = result.diagnostic.c_str();
	RefreshSelectedUnitIndex();
	if (m_controlsCreated) RefreshEditorControls(refreshPlacementChoices);
	Invalidate(FALSE);
}

void CSBattlePreviewPage::StampAt(
	const ccz::preview::BattleCoord& coordinate)
{
	if (!m_editorOps || !m_leftStampActive ||
		ActiveSession().editor_state.mode != ccz::editor_preview::BattleEditorMode::Place ||
		m_lastStampCell == coordinate)
		return;
	m_lastStampCell = coordinate;
	const auto preview = m_editorOps->PreviewPlacement(coordinate);
	if (!preview.valid) {
		m_diagnostic = preview.diagnostic.c_str();
		Invalidate(FALSE);
		return;
	}
	const auto result = m_editorOps->Place(coordinate);
	const bool refreshPlacementChoices =
		!ActiveSession().editor_state.selected_minion_brush.has_value();
	ShowOperationResult(result, refreshPlacementChoices);
}

void CSBattlePreviewPage::SetSidebarVisible(bool visible)
{
	if (m_sidebarVisible == visible) return;
	m_sidebarVisible = visible;
	LayoutEditorControls();
}

void CSBattlePreviewPage::OnDestroy()
{
	if (m_panActive) ReleaseCapture();
	if (m_leftStampActive) ReleaseCapture();
	m_panActive = false;
	m_leftStampActive = false;
	UnbindSession();
	CWnd::OnDestroy();
}

bool CSBattlePreviewPage::RequestPreviewRefresh(
	CTreeCtrl& tree, HTREEITEM selectedItem, const wchar_t* documentPath)
{
	if (m_activeSession == nullptr || m_activeDocument == nullptr)
		return false;
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	PerformanceActivationScope perfActivation(*this, documentPath);
	#endif
	if (m_activeDocument->GetSPreviewDocumentId() !=
		ActiveSession().document_id) return false;
	if (ActiveSession().stale) return true;
	const ccz::editor_preview::BattlePreviewDocumentVersion currentVersion{
		m_activeDocument->GetPreviewContentEpoch(),
		m_activeDocument->GetPreviewTreeGeneration()};
	const auto activationCheck =
		ccz::editor_preview::CheckBattlePreviewActivation(
			ActiveSession().has_baseline,
			ActiveSession().observed_version, currentVersion);
	if (activationCheck ==
		ccz::editor_preview::BattlePreviewActivationCheck::RebindTree) {
		if (HasPendingPreviewChanges()) {
			ActiveSession().native_writeback_transaction.Clear();
			m_sourceTreeWindow = nullptr;
			MarkSessionStale(
				L"S 剧本树已重建，旧节点句柄和原生撤回记录已失效。当前临时布置已保留，写回与撤回已暂停；切换模式并确认丢弃后可重新读取。");
			return true;
		}
		ClearDocument();
		m_documentPath = documentPath ? documentPath : L"";
		m_sourceTreeWindow = tree.GetSafeHwnd();
	}
	if (ActiveSession().ui.quick_mode_enabled)
		return RequestQuickModeRefresh(tree, documentPath);

	ccz::editor_preview::CapturedEditorTree snapshot;
	std::wstring diagnostic;
	if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree, selectedItem,
			documentPath ? std::wstring(documentPath) : std::wstring(),
			snapshot, diagnostic)) {
		if (!ConfirmDiscardTemporaryChanges()) return false;
		ClearDocument();
		m_documentPath = documentPath ? documentPath : L"";
		AppendDiagnostic(diagnostic.empty()
			? L"无法读取 S 剧本结构" : diagnostic);
		Invalidate(FALSE);
		return true;
	}
	CCZ_BATTLE_PREVIEW_PERF(RecordBattleInputBuild());
	auto capture = ccz::editor_preview::BuildBattlePreviewInput(
		std::move(snapshot));
	if (capture.ok && ActiveSession().baseline_capture.ok &&
		!m_documentPath.IsEmpty() &&
		m_documentPath.CompareNoCase(capture.input.document_path.c_str()) == 0 &&
		capture.input.document_revision ==
			ActiveSession().baseline_capture.input.document_revision &&
		capture.input.scope_key ==
			ActiveSession().baseline_capture.input.scope_key) {
		// S preview state belongs to the whole Section; selecting another
		// instruction inside it must not discard its placement cache or undo.
		return true;
	}
	if (!capture.ok) {
		if (!ConfirmDiscardTemporaryChanges()) return false;
		ClearDocument();
		m_documentPath = documentPath ? documentPath : L"";
		AppendDiagnostic(capture.diagnostic.empty()
			? L"无法读取 S 战场预览结构" : capture.diagnostic);
		Invalidate(FALSE);
		return true;
	}
	if (!ConfirmDiscardTemporaryChanges()) return false;
	ClearDocument();
	m_documentPath = documentPath ? documentPath : L"";
	LoadCapturedBattle(std::move(capture), tree.GetSafeHwnd());
	Invalidate(FALSE);
	return true;
}

bool CSBattlePreviewPage::RequestQuickModeRefresh(
	CTreeCtrl& tree, const wchar_t* documentPath)
{
	if (ActiveSession().stale) return true;
	const CString requestedPath = documentPath ? documentPath : L"";
	const HWND requestedTree = tree.GetSafeHwnd();
	const bool sameSource = ActiveSession().baseline_capture.ok &&
		m_sourceTreeWindow == requestedTree &&
		!m_documentPath.IsEmpty() &&
		m_documentPath.CompareNoCase(requestedPath) == 0;
	if (sameSource) {
		ccz::editor_preview::CapturedEditorTree freshPinnedSection;
		std::wstring diagnostic;
		CCZ_BATTLE_PREVIEW_PERF(RecordPinnedSectionCapture());
		if (!ccz::editor_preview::CaptureEditorPinnedSectionSnapshot(
			tree,
			reinterpret_cast<HTREEITEM>(ActiveSession().baseline_capture.snapshot.section_root.runtime_handle),
			ActiveSession().baseline_capture.snapshot.section_path,
			requestedPath.GetString(), freshPinnedSection, diagnostic)) {
			ActiveSession().native_writeback_transaction.Clear();
			if (HasPendingPreviewDrafts()) {
				m_sourceTreeWindow = nullptr;
				MarkSessionStale(
					L"快速模式无法再定位原 Scene1 节点。当前布置已保留，但旧来源的写回与撤回已暂停；切换模式并确认丢弃后可重新读取。");
				return true;
			}
			m_sourceTreeWindow = nullptr;
		} else {
			const std::uint64_t freshSectionRevision =
				ccz::editor_preview::ComputeBattleSectionRevision(
					requestedPath.GetString(), freshPinnedSection.section_root);
			if (freshSectionRevision == ActiveSession().quick_mode_section_revision) {
				const bool documentContentChanged =
					m_activeDocument->GetPreviewContentEpoch() !=
					ActiveSession().observed_version.content_epoch;
				if (documentContentChanged)
					ActiveSession().native_writeback_transaction.Clear();
				ActiveSession().observed_version.content_epoch =
					m_activeDocument->GetPreviewContentEpoch();
				ActiveSession().observed_version.tree_generation =
					m_activeDocument->GetPreviewTreeGeneration();
				m_quickModeDiagnostic.Empty();
				if (documentContentChanged && m_controlsCreated)
					RefreshEditorControls();
				Invalidate(FALSE);
				return true;
			}
			// A changed Scene1 content revision invalidates the old QA native
			// undo preimage. It is not an uncommitted preview draft and must not
			// block refreshing the source baseline.
			ActiveSession().native_writeback_transaction.Clear();
			m_sourceTreeWindow = nullptr;
		}
	}

	ccz::editor_preview::CapturedEditorTree documentSnapshot;
	std::wstring diagnostic;
	CCZ_BATTLE_PREVIEW_PERF(RecordFullDocumentCapture());
	if (!ccz::editor_preview::CaptureEditorDocumentSnapshot(
		tree, requestedPath.GetString(), documentSnapshot, diagnostic)) {
		if (HasPendingPreviewChanges()) {
			MarkSessionStale(L"快速模式无法完整校验剧本树。当前布置已保留，写回与撤回已暂停；请切换模式并确认丢弃后重试。");
			return true;
		}
		ClearDocument();
		m_documentPath = requestedPath;
		m_sourceTreeWindow = requestedTree;
		m_quickModeDiagnostic = (
			L"快速模式无法读取完整剧本：" + diagnostic).c_str();
		Invalidate(FALSE);
		return true;
	}

	auto resolution = ccz::editor_preview::ResolveScene1BattleSection(
		std::move(documentSnapshot));
	if (!resolution.resolved && HasPendingPreviewDrafts()) {
		MarkSessionStale(L"Scene1 来源无法重新解析。当前临时布置已保留，写入已暂停：" +
			resolution.diagnostic);
		return true;
	}
	m_documentPath = requestedPath;
	m_sourceTreeWindow = requestedTree;
	if (!resolution.resolved) {
		ActiveSession().native_writeback_transaction.Clear();
		ClearDocument();
		m_documentPath = requestedPath;
		m_sourceTreeWindow = requestedTree;
		m_quickModeDiagnostic = resolution.diagnostic.c_str();
		Invalidate(FALSE);
		return true;
	}

	std::wstring rebaseDiagnostic;
	if (HasPendingPreviewDrafts()) {
		if (!LoadQuickModeBaselinePreservingDraft(
			std::move(resolution.capture), requestedTree,
			rebaseDiagnostic)) {
			MarkSessionStale(rebaseDiagnostic.empty()
				? L"Scene1 来源已变化，临时布置无法安全映射；原预览和布置已保留，写入已暂停"
				: rebaseDiagnostic);
			return true;
		}
	} else {
		ActiveSession().native_writeback_transaction.Clear();
		m_diagnostic.Empty();
		m_quickModeDiagnostic.Empty();
		m_selectedUnit.reset();
		LoadCapturedBattle(std::move(resolution.capture), requestedTree);
	}
	m_quickModeDiagnostic.Empty();
	Invalidate(FALSE);
	return true;
}

bool CSBattlePreviewPage::SetQuickModeEnabled(
	bool enabled, CTreeCtrl& tree, HTREEITEM selectedItem,
	const wchar_t* documentPath)
{
	if (m_activeSession == nullptr || m_activeDocument == nullptr)
		return false;
	if (enabled == ActiveSession().ui.quick_mode_enabled) return true;
	if (!ConfirmDiscardTemporaryChanges()) {
		if (m_quickModeButton.GetSafeHwnd() != nullptr)
			m_quickModeButton.SetCheck(ActiveSession().ui.quick_mode_enabled
				? BST_CHECKED : BST_UNCHECKED);
		return false;
	}

	ClearDocument();
	ActiveSession().ui.quick_mode_enabled = enabled;
	const bool accepted = enabled
		? RequestQuickModeRefresh(tree, documentPath)
		: RequestPreviewRefresh(tree, selectedItem, documentPath);
	if (!accepted) {
		ActiveSession().ui.quick_mode_enabled = !enabled;
		if (m_quickModeButton.GetSafeHwnd() != nullptr)
			m_quickModeButton.SetCheck(ActiveSession().ui.quick_mode_enabled
				? BST_CHECKED : BST_UNCHECKED);
		return false;
	}
	AfxGetApp()->WriteProfileInt(_T("S Battle Preview"), _T("QuickMode"),
		enabled ? 1 : 0);
	if (m_quickModeButton.GetSafeHwnd() != nullptr)
		m_quickModeButton.SetCheck(enabled ? BST_CHECKED : BST_UNCHECKED);
	return true;
}

bool CSBattlePreviewPage::IsBoundToTree(HWND treeWindow) const
{
	return treeWindow != nullptr && m_sourceTreeWindow == treeWindow &&
		ActiveSession().baseline_capture.ok;
}

void CSBattlePreviewPage::EditorTreeDestroyed(HWND treeWindow)
{
	if (IsBoundToTree(treeWindow)) UnbindSession();
}

bool CSBattlePreviewPage::ConfirmDiscardTemporaryChanges() const
{
	return ConfirmDiscardSessionChanges(ActiveSession());
}

bool CSBattlePreviewPage::ConfirmDiscardSessionChanges(
	const ccz::editor_preview::BattlePreviewDocumentSession& session) const
{
	const bool hasTemporaryUndo = std::any_of(
		session.condition_workspaces.begin(), session.condition_workspaces.end(),
		[](const auto& workspace) {
			return workspace.second != nullptr &&
				workspace.second->operations != nullptr &&
				workspace.second->operations->CanUndo();
		});
	if (!hasTemporaryUndo) return true;
	const wchar_t* message =
		L"当前 S 战场有尚未写入剧本的临时布置或修改。重载预览、切换剧本或模式、关闭来源窗口会丢弃这些编辑状态。是否继续？";
	return AfxMessageBox(message,
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

bool CSBattlePreviewPage::HasPendingPreviewDrafts() const
{
	return std::any_of(
		ActiveSession().condition_workspaces.begin(), ActiveSession().condition_workspaces.end(),
		[](const auto& workspace) {
			return workspace.second != nullptr &&
				workspace.second->operations != nullptr &&
				workspace.second->operations->CanUndo();
		});
}

bool CSBattlePreviewPage::HasPendingPreviewChanges() const
{
	return HasPendingPreviewDrafts() ||
		ActiveSession().native_writeback_transaction.undo_available();
}

void CSBattlePreviewPage::MarkSessionStale(
	const std::wstring& diagnostic)
{
	ActiveSession().stale = true;
	m_quickModeDiagnostic = diagnostic.c_str();
	if (m_controlsCreated) RefreshEditorControls();
	Invalidate(FALSE);
}

bool CSBattlePreviewPage::BuildPendingWritebackPlan(
	const ccz::editor_preview::BattlePreviewCapture& fresh_capture,
	ccz::editor_preview::BattleWritebackPlanBuild& plan,
	std::wstring& diagnostic,
	ccz::editor_preview::BattleWritebackNativeBatch* native_batch) const
{
	plan = {};
	diagnostic.clear();
	if (native_batch != nullptr) *native_batch = {};
	if (ActiveSession().stale) {
		diagnostic = L"战场写回预检失败：当前 S 预览来源已过期；临时布置已保留，请切换模式并确认丢弃后重新读取";
		return false;
	}
	if (!ActiveSession().baseline_capture.ok) {
		diagnostic = L"战场写回预检失败：当前 S 布置缓存没有有效加载基线";
		return false;
	}

	if (!fresh_capture.ok) {
		diagnostic = fresh_capture.diagnostic.empty()
			? L"战场写回预检失败：无法重新捕获当前剧本树"
			: fresh_capture.diagnostic;
		return false;
	}
	if (!ActiveSession().active_condition_scope.writeback_scope_resolved) {
		diagnostic = ActiveSession().active_condition_scope.diagnostic.empty()
			? L"战场写回预检失败：当前条件分支的出场来源无法安全确定"
			: ActiveSession().active_condition_scope.diagnostic;
		return false;
	}
	ccz::editor_preview::BattleWritebackReachabilityScope reachabilityScope;
	reachabilityScope.filter_deployments = true;
	reachabilityScope.reachable_deployment_keys =
		ActiveSession().active_condition_scope.reachable_deployment_keys;
	reachabilityScope.insertion_anchors =
		ActiveSession().active_condition_scope.insertion_anchors;
	if (reachabilityScope.insertion_anchors.empty() &&
		ActiveSession().active_condition_scope.insertion_anchor_resolved) {
		reachabilityScope.insert_parent_key =
			ActiveSession().active_condition_scope.insert_parent_key;
		reachabilityScope.insert_before_key =
			ActiveSession().active_condition_scope.insert_before_key;
	}

	ccz::editor_preview::BattleWritebackSectionSnapshot section_snapshot;
	if (!ccz::editor_preview::BuildBattleWritebackSectionSnapshot(
		ActiveSession().baseline_capture, fresh_capture, section_snapshot,
		diagnostic, ActiveSession().ui.quick_mode_enabled
			? ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection
			: ccz::editor_preview::BattleWritebackRevisionScope::WholeDocument,
		&reachabilityScope)) {
		return false;
	}

	plan = ccz::editor_preview::BuildBattleWritebackPlan(
		ActiveSession().editor_state.battle, ActiveSession().baseline_capture.input.scope_key,
		section_snapshot);
	diagnostic = plan.diagnostic;
	if (!plan.ok) return false;
	if (native_batch != nullptr) {
		*native_batch = ccz::editor_preview::BuildBattleWritebackNativeBatch(
			plan, fresh_capture, ActiveSession().baseline_capture.input.scope_key);
		if (!native_batch->ok) {
			diagnostic = native_batch->diagnostic;
			return false;
		}
	}
	return true;
}

bool CSBattlePreviewPage::ApplyPendingNativeWritebackForCurrentSection(
	CcczEditor2View& view, std::wstring& diagnostic)
{
	diagnostic.clear();
	if (!ActiveSession().baseline_capture.ok ||
		ActiveSession().baseline_capture.snapshot.section_root.runtime_handle == 0) {
		diagnostic = L"S 战场写回拒绝：当前没有有效的 Section 布置基线";
		return false;
	}
	const HTREEITEM section = reinterpret_cast<HTREEITEM>(
		ActiveSession().baseline_capture.snapshot.section_root.runtime_handle);
	return ApplyPendingNativeWritebackTransaction(view, section, diagnostic, true);
}

bool CSBattlePreviewPage::ApplyPendingNativeWritebackTransaction(
	CcczEditor2View& view, HTREEITEM selectedItem,
	std::wstring& diagnostic, bool requireNonemptyBatch)
{
	diagnostic.clear();
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	BattleWritebackPerformanceScope writebackPerf(
		m_documentPath.GetString(), !m_documentPath.IsEmpty());
	#endif
	CcczEditor2Doc* document = view.GetDocument();
	if (ActiveSession().stale) {
		diagnostic = L"S 战场原生写回拒绝：当前预览来源已过期，临时布置已保留；请切换模式并确认丢弃后重新读取";
		return false;
	}
	if (document == nullptr || document->pathName == nullptr ||
		m_documentPath.IsEmpty() ||
		document->GetSPreviewDocumentId() != ActiveSession().document_id ||
		CString(document->pathName).CompareNoCase(m_documentPath) != 0) {
		diagnostic = L"S 战场原生写回拒绝：预览与活动剧本文档不一致";
		return false;
	}
	if (ActiveSession().has_baseline &&
		document->GetPreviewTreeGeneration() !=
			ActiveSession().observed_version.tree_generation) {
		ActiveSession().native_writeback_transaction.Clear();
		m_sourceTreeWindow = nullptr;
		MarkSessionStale(L"S 剧本树已重建。旧节点句柄和原生撤回记录已失效；当前临时布置已保留，写回与撤回已暂停。切换模式并确认丢弃后可重新读取。");
		diagnostic = m_quickModeDiagnostic.GetString();
		return false;
	}
	CTreeCtrl& tree = view.GetTreeCtrl();
	if (tree.GetSafeHwnd() == nullptr) {
		diagnostic = L"S 战场原生写回拒绝：当前剧本树窗口已关闭";
		return false;
	}
	if (ActiveSession().ui.quick_mode_enabled && tree.GetSafeHwnd() != m_sourceTreeWindow) {
		diagnostic = L"S 战场原生写回拒绝：活动窗口不是当前快速模式绑定的剧本树";
		return false;
	}
	const ccz::editor_preview::BattlePreviewDocumentVersion captureVersionBefore{
		document->GetPreviewContentEpoch(),
		document->GetPreviewTreeGeneration()};
	ccz::editor_preview::CapturedEditorTree snapshot;
	if (ActiveSession().ui.quick_mode_enabled) {
		ccz::editor_preview::CapturedEditorTree freshDocument;
		if (!ccz::editor_preview::CaptureEditorDocumentSnapshot(tree,
			std::wstring(document->pathName), freshDocument, diagnostic) ||
			!ccz::editor_preview::SelectCapturedBattleSection(
				std::move(freshDocument),
				ActiveSession().baseline_capture.snapshot.section_root.runtime_handle,
				ActiveSession().baseline_capture.snapshot.section_path,
				snapshot, diagnostic)) {
			if (diagnostic.empty())
				diagnostic = L"S 战场原生写回拒绝：无法捕获固定的 Scene1 Section";
			return false;
		}
	} else if (!ccz::editor_preview::CaptureEditorTreeSnapshot(tree, selectedItem,
		std::wstring(document->pathName), snapshot, diagnostic)) {
		if (diagnostic.empty()) diagnostic = L"S 战场原生写回拒绝：无法捕获活动剧本";
		return false;
	}
	const ccz::editor_preview::BattlePreviewDocumentVersion captureVersion{
		document->GetPreviewContentEpoch(),
		document->GetPreviewTreeGeneration()};
	if (captureVersion.tree_generation != captureVersionBefore.tree_generation ||
		captureVersion.tree_generation !=
			ActiveSession().observed_version.tree_generation) {
		ActiveSession().native_writeback_transaction.Clear();
		m_sourceTreeWindow = nullptr;
		MarkSessionStale(L"S 剧本树在写回预检期间已重建。旧节点句柄和原生撤回记录已失效；当前临时布置已保留，写回与撤回已暂停。");
		diagnostic = m_quickModeDiagnostic.GetString();
		return false;
	}
	if (captureVersion.content_epoch != captureVersionBefore.content_epoch) {
		diagnostic = L"S 战场原生写回拒绝：预检期间剧本内容发生变化，请重新执行写回";
		return false;
	}
	CCZ_BATTLE_PREVIEW_PERF(RecordBattleInputBuild());
	auto capture = ccz::editor_preview::BuildBattlePreviewInput(
		std::move(snapshot));
	if (!capture.ok ||
		capture.input.document_key != ActiveSession().baseline_capture.input.document_key ||
		capture.input.scope_key != ActiveSession().baseline_capture.input.scope_key) {
		diagnostic = capture.diagnostic.empty()
			? L"S 战场原生写回拒绝：活动文档或 Section 已偏离布置缓存基线"
			: capture.diagnostic;
		return false;
	}
	if (!ccz::editor_preview::ValidateBattleWritebackNextOrdinal(
		capture.snapshot, view.cur_code_ord, diagnostic)) {
		if (diagnostic.empty())
			diagnostic = L"S 战场原生写回拒绝：当前指令序号计数与剧本节点不一致";
		return false;
	}
	CCZ_BATTLE_PREVIEW_PERF(writebackPerf.RecordCaptured());

	ccz::editor_preview::BattleWritebackPlanBuild plan;
	ccz::editor_preview::BattleWritebackNativeBatch batch;
	if (!BuildPendingWritebackPlan(capture, plan, diagnostic, &batch))
		return false;
	CCZ_BATTLE_PREVIEW_PERF(writebackPerf.RecordPlan(batch));
	if (requireNonemptyBatch && batch.changes.empty()) {
		diagnostic = L"S 战场写回预检通过，但当前没有需要提交的布阵改动";
		return false;
	}
	if (document->GetSPreviewDocumentId() != ActiveSession().document_id ||
		document->GetPreviewTreeGeneration() != captureVersion.tree_generation) {
		ActiveSession().native_writeback_transaction.Clear();
		m_sourceTreeWindow = nullptr;
		MarkSessionStale(L"S 剧本树在写回前发生重建。旧节点句柄和原生撤回记录已失效；当前临时布置已保留，写回与撤回已暂停。");
		diagnostic = m_quickModeDiagnostic.GetString();
		return false;
	}
	if (document->GetPreviewContentEpoch() != captureVersion.content_epoch) {
		diagnostic = L"S 战场原生写回拒绝：预检完成后剧本内容发生变化，未执行任何写入，请重新执行写回";
		return false;
	}
	const HTREEITEM revisionSection = reinterpret_cast<HTREEITEM>(
		ActiveSession().baseline_capture.snapshot.section_root.runtime_handle);
	ccz::editor_preview::BattleWritebackNativeDocumentState preflight_state;
	preflight_state.selected_handle = reinterpret_cast<std::uintptr_t>(
		tree.GetSelectedItem());
	preflight_state.next_ordinal = view.cur_code_ord;
	preflight_state.custom_modified = document->modified;
	preflight_state.framework_modified = document->IsModified() != FALSE;
	preflight_state.document_revision = capture.input.document_revision;
	MfcBattleWritebackNativeHost host(view, revisionSection, preflight_state,
		ActiveSession().document_id, captureVersion);
	BattlePreviewRefreshSuppression suppress(view);
	const bool applied = ActiveSession().native_writeback_transaction.Apply(
		batch, host, diagnostic);
	CCZ_BATTLE_PREVIEW_PERF(writebackPerf.RecordTransaction(applied));
	if (applied) {
		ActiveSession().native_writeback_rebase_required = true;
		ccz::editor_preview::BattlePreviewCapture committed_capture;
		const bool has_committed_capture =
			host.TakeCommittedBattleCapture(committed_capture);
		std::wstring rebaseDiagnostic;
		const bool rebased = RebaseBattlePreviewAfterNativeWriteback(
			view, revisionSection, rebaseDiagnostic,
			has_committed_capture ? &committed_capture : nullptr);
		CCZ_BATTLE_PREVIEW_PERF(writebackPerf.RecordRebase(rebased));
		diagnostic = L"S 战场字段已写入原生剧本树并逐条读回确认；" +
			describe_native_writeback(batch);
		if (rebased) {
			diagnostic += L"；当前预览已按写入后的剧本树重建，可继续写入";
		} else {
			diagnostic += L"；预览基线重建失败，已暂停后续写入：" +
				rebaseDiagnostic;
		}
		if (requireNonemptyBatch && RevealNativeWritebackInstruction(
			view, batch, host.first_inserted_handle())) {
			diagnostic += L"；编辑树已展开并选中对应的写入指令";
		}
	}
	RefreshEditorControls();
	Invalidate(FALSE);
	return applied;
}

bool CSBattlePreviewPage::RebaseBattlePreviewAfterNativeWriteback(
	CcczEditor2View& view, HTREEITEM selectedItem,
	std::wstring& diagnostic,
	ccz::editor_preview::BattlePreviewCapture* committed_capture_pointer)
{
	diagnostic.clear();
	CcczEditor2Doc* document = view.GetDocument();
	if (document == nullptr || document->pathName == nullptr ||
		!ActiveSession().baseline_capture.ok || m_documentPath.IsEmpty() ||
		CString(document->pathName).CompareNoCase(m_documentPath) != 0) {
		diagnostic = L"写入后无法重建预览基线：活动剧本文档已变化";
		ActiveSession().native_writeback_rebase_required = true;
		return false;
	}
	CTreeCtrl& tree = view.GetTreeCtrl();
	if (ActiveSession().ui.quick_mode_enabled && tree.GetSafeHwnd() != m_sourceTreeWindow) {
		diagnostic = L"写入后无法重建预览基线：快速模式来源树已变化";
		ActiveSession().native_writeback_rebase_required = true;
		return false;
	}

	const auto expectedDocumentKey =
		ActiveSession().baseline_capture.input.document_key;
	const auto expectedScopeKey = ActiveSession().baseline_capture.input.scope_key;
	const auto sectionHandle =
		ActiveSession().baseline_capture.snapshot.section_root.runtime_handle;
	const auto sectionPath = ActiveSession().baseline_capture.snapshot.section_path;
	ccz::editor_preview::BattlePreviewCapture freshCapture;
	if (committed_capture_pointer != nullptr &&
		committed_capture_pointer->ok &&
		committed_capture_pointer->snapshot.section_root.runtime_handle ==
			sectionHandle &&
		committed_capture_pointer->snapshot.section_path == sectionPath) {
		// The native transaction already captured and hashed the committed full
		// document for its undo record. RestoreDocumentState changes selection
		// and dirty flags only, so reuse that exact capture for the new baseline.
		freshCapture = std::move(*committed_capture_pointer);
	} else {
		ccz::editor_preview::CapturedEditorTree snapshot;
		if (ActiveSession().ui.quick_mode_enabled) {
			ccz::editor_preview::CapturedEditorTree freshDocument;
			if (!ccz::editor_preview::CaptureEditorDocumentSnapshot(
				tree, std::wstring(document->pathName), freshDocument, diagnostic) ||
				!ccz::editor_preview::SelectCapturedBattleSection(
					std::move(freshDocument), sectionHandle, sectionPath,
					snapshot, diagnostic)) {
				if (diagnostic.empty())
					diagnostic = L"快速模式固定的 Scene1 Section 无法重新捕获";
				ActiveSession().native_writeback_rebase_required = true;
				return false;
			}
		} else if (!ccz::editor_preview::CaptureEditorTreeSnapshot(
			tree, selectedItem, std::wstring(document->pathName), snapshot,
			diagnostic)) {
			if (diagnostic.empty())
				diagnostic = L"当前战场 Section 无法重新捕获";
			ActiveSession().native_writeback_rebase_required = true;
			return false;
		}
		freshCapture = ccz::editor_preview::BuildBattlePreviewInput(
			std::move(snapshot));
	}
	if (!freshCapture.ok ||
		freshCapture.input.document_key != expectedDocumentKey ||
		freshCapture.input.scope_key != expectedScopeKey) {
		diagnostic = freshCapture.diagnostic.empty()
			? L"写入后文档或 Section 标识发生变化，拒绝重建预览基线"
			: freshCapture.diagnostic;
		ActiveSession().native_writeback_rebase_required = true;
		return false;
	}

	m_diagnostic.Empty();
	LoadCapturedBattle(std::move(freshCapture), tree.GetSafeHwnd());
	if (!ActiveSession().baseline_capture.ok || m_editorOps == nullptr ||
		ActiveSession().baseline_capture.input.document_key != expectedDocumentKey ||
		ActiveSession().baseline_capture.input.scope_key != expectedScopeKey) {
		diagnostic = L"写入已完成，但新的原生剧本树未能建立可编辑的预览基线";
		ActiveSession().native_writeback_rebase_required = true;
		return false;
	}
	ActiveSession().native_writeback_rebase_required = false;
	diagnostic = L"预览基线已从写入后的原生剧本树重建";
	return true;
}

bool CSBattlePreviewPage::UndoNativeWriteback(
	CcczEditor2View& view, std::wstring& diagnostic)
{
	diagnostic.clear();
	if (ActiveSession().stale) {
		diagnostic = L"S 战场撤回拒绝：当前预览来源已过期，临时布置已保留；旧来源的撤回已暂停";
		return false;
	}
	if (!ActiveSession().native_writeback_transaction.undo_available()) {
		diagnostic = L"当前没有可撤回的 S 战场原生写回事务";
		return false;
	}
	CcczEditor2Doc* document = view.GetDocument();
	if ((ActiveSession().ui.quick_mode_enabled &&
		 view.GetTreeCtrl().GetSafeHwnd() != m_sourceTreeWindow) ||
		document == nullptr || document->pathName == nullptr ||
		document->GetSPreviewDocumentId() != ActiveSession().document_id ||
		CString(document->pathName).CompareNoCase(m_documentPath) != 0) {
		diagnostic = L"S 战场撤回拒绝：活动剧本文档与写回事务不一致";
		return false;
	}
	if (document->GetPreviewTreeGeneration() !=
		ActiveSession().observed_version.tree_generation) {
		ActiveSession().native_writeback_transaction.Clear();
		m_sourceTreeWindow = nullptr;
		MarkSessionStale(L"S 剧本树已重建。旧节点句柄和原生撤回记录已失效；临时布置已保留，撤回已暂停。");
		diagnostic = m_quickModeDiagnostic.GetString();
		return false;
	}
	const HTREEITEM revisionSection = reinterpret_cast<HTREEITEM>(
		ActiveSession().baseline_capture.snapshot.section_root.runtime_handle);
	MfcBattleWritebackNativeHost host(view, revisionSection);
	BattlePreviewRefreshSuppression suppress(view);
	const std::uint64_t previousContentEpoch =
		document->GetPreviewContentEpoch();
	const bool undone = ActiveSession().native_writeback_transaction.Undo(host, diagnostic);
	if (undone) {
		if (document->GetPreviewContentEpoch() == previousContentEpoch)
			document->MarkEditorContentChanged();
		ActiveSession().native_writeback_rebase_required = true;
		std::wstring rebaseDiagnostic;
		if (!RebaseBattlePreviewAfterNativeWriteback(
				view, revisionSection, rebaseDiagnostic)) {
			diagnostic += L"；撤回后预览基线重建失败，已暂停后续写入：" +
				rebaseDiagnostic;
		} else {
			diagnostic += L"；预览已同步到撤回后的剧本树";
		}
	}
	RefreshEditorControls();
	Invalidate(FALSE);
	return undone;
}

void CSBattlePreviewPage::ClearDocument()
{
	if (m_panActive && GetSafeHwnd() != nullptr) ReleaseCapture();
	ReleaseActiveBitmapPins();
	m_panActive = false;
	m_panStartPoint = CPoint(-1, -1);
	m_zoom = kDefaultZoom;
	m_wheelRemainder = 0;
	m_scrollX = m_scrollY = 0;
	m_selectedUnit.reset();
	m_hoverCell.reset();
	m_lastStampCell.reset();
	ActiveSession().baseline_capture = {};
	ActiveSession().map_asset_key = {};
	ActiveSession().unit_mov_asset_key = {};
	ActiveSession().native_writeback_transaction.Clear();
	ActiveSession().native_writeback_rebase_required = false;
	ActiveSession().has_baseline = false;
	ActiveSession().stale = false;
	ActiveSession().editor_state = {};
	m_editorOps = nullptr;
	ActiveSession().condition_workspaces.clear();
	ActiveSession().active_workspace_key.clear();
	ActiveSession().active_condition_scope = {};
	ActiveSession().condition_options = {};
	ActiveSession().condition_catalog = {};
	ActiveSession().condition_model.reset();
	ActiveSession().condition_button_visible = false;
	ActiveSession().ui.search_text.clear();
	ActiveSession().ui.selected_character_data_id = -1;
	ActiveSession().ui.selected_brush_data_id = -1;
	ActiveSession().ui.selected_unit_cache_id = 0;
	ActiveSession().ui.deployment_instruction = 0x4b;
	m_restoringSessionUi = true;
	if (m_searchEdit.GetSafeHwnd() != nullptr)
		m_searchEdit.SetWindowText(_T(""));
	m_restoringSessionUi = false;
	m_conditionTreePane.UpdateModel(nullptr, {}, {});
	m_conditionTreePane.ShowPane(false);
	m_documentPath.Empty();
	m_diagnostic.Empty();
	m_quickModeDiagnostic.Empty();
	m_sourceTreeWindow = nullptr;
	ActiveSession().quick_mode_section_revision = 0;
	if (m_controlsCreated) RefreshEditorControls();
	UpdateViewport();
	if (GetSafeHwnd() != nullptr) Invalidate(FALSE);
}

bool CSBattlePreviewPage::LoadQuickModeBaselinePreservingDraft(
	ccz::editor_preview::BattlePreviewCapture capture,
	HWND sourceTreeWindow, std::wstring& diagnostic)
{
	diagnostic.clear();
	if (!capture.ok || !HasPendingPreviewDrafts()) {
		diagnostic = L"快速模式草稿重建没有有效的来源或临时布置";
		return false;
	}
	SaveActiveSessionUiState();
	auto& session = ActiveSession();

	auto newConditionOptions = session.condition_options;
	const auto newCondition = ccz::editor_preview::BuildBattleConditionTree(
		capture, session.condition_catalog, newConditionOptions);
	if (!newCondition.ok) {
		diagnostic = newCondition.diagnostic.empty()
			? L"新 Scene1 来源无法重建条件树，原临时布置已保留"
			: newCondition.diagnostic;
		return false;
	}
	const auto activeDraft = session.condition_workspaces.find(
		session.active_workspace_key);
	if (activeDraft != session.condition_workspaces.end() &&
		activeDraft->second != nullptr &&
		activeDraft->second->operations != nullptr &&
		activeDraft->second->operations->CanUndo() &&
		activeDraft->second->scope.workspace_key !=
			newCondition.scope.workspace_key) {
		diagnostic = L"当前条件分支已变化，无法确认原临时布置仍属于同一分支；原布置已保留，写入已暂停";
		return false;
	}

	std::map<ccz::preview::LogicalNodeKey,
		std::unique_ptr<ccz::editor_preview::BattlePreviewConditionWorkspace>>
		rebasedWorkspaces;
	for (const auto& entry : session.condition_workspaces) {
		const auto& oldWorkspace = entry.second;
		if (oldWorkspace == nullptr || oldWorkspace->operations == nullptr ||
			!oldWorkspace->operations->CanUndo()) continue;

		ccz::editor_preview::BattleEditorState freshState;
		std::wstring scopeDiagnostic;
		if (!build_fresh_editor_state_for_scope(capture, oldWorkspace->scope,
				oldWorkspace->state, freshState, scopeDiagnostic)) {
			diagnostic = L"临时布置映射冲突：" + scopeDiagnostic;
			return false;
		}
		const auto rebased = ccz::editor_preview::RebaseBattleEditorDraft(
			oldWorkspace->state, freshState);
		if (!rebased.ok) {
			std::wostringstream message;
			message << L"临时布置映射冲突：";
			for (std::size_t i = 0; i < rebased.conflicts.size(); ++i) {
				if (i != 0) message << L"；";
				message << rebased.conflicts[i];
			}
			diagnostic = message.str();
			return false;
		}

		auto workspace = std::make_unique<
			ccz::editor_preview::BattlePreviewConditionWorkspace>();
		workspace->state = rebased.state;
		workspace->scope = oldWorkspace->scope;
		if (entry.first == newCondition.scope.workspace_key)
			workspace->scope = newCondition.scope;
		rebasedWorkspaces.emplace(entry.first, std::move(workspace));
	}

	// Preflight every cached draft before replacing the session baseline. A
	// conflict leaves the old preview and its undo history untouched.
	auto oldBaseline = std::move(session.baseline_capture);
	auto oldEditorState = std::move(session.editor_state);
	auto oldWorkspaces = std::move(session.condition_workspaces);
	const auto oldActiveWorkspaceKey = session.active_workspace_key;
	auto oldActiveScope = session.active_condition_scope;
	auto oldConditionModel = session.condition_model;
	auto oldConditionOptions = session.condition_options;
	auto oldConditionCatalog = session.condition_catalog;
	const auto oldObservedVersion = session.observed_version;
	const auto oldHasBaseline = session.has_baseline;
	const auto oldStale = session.stale;
	const auto oldQuickModeRevision = session.quick_mode_section_revision;
	const auto oldMapAssetKey = session.map_asset_key;
	const auto oldUnitMovAssetKey = session.unit_mov_asset_key;
	const auto oldDocumentPath = session.document_path;
	const auto oldConditionButtonVisible = session.condition_button_visible;

	session.native_writeback_transaction.Clear();
	session.condition_options = std::move(newConditionOptions);
	m_documentPath = capture.input.document_path.c_str();
	m_sourceTreeWindow = sourceTreeWindow;
	m_diagnostic.Empty();
	m_quickModeDiagnostic.Empty();
	m_selectedUnit.reset();
	m_hoverCell.reset();
	LoadCapturedBattle(std::move(capture), sourceTreeWindow);
	if (!session.baseline_capture.ok || session.active_workspace_key.empty() ||
		session.condition_workspaces.find(session.active_workspace_key) ==
			session.condition_workspaces.end()) {
		session.baseline_capture = std::move(oldBaseline);
		session.editor_state = std::move(oldEditorState);
		session.condition_workspaces = std::move(oldWorkspaces);
		session.active_workspace_key = oldActiveWorkspaceKey;
		session.active_condition_scope = std::move(oldActiveScope);
		session.condition_model = std::move(oldConditionModel);
		session.condition_options = std::move(oldConditionOptions);
		session.condition_catalog = std::move(oldConditionCatalog);
		session.observed_version = oldObservedVersion;
		session.has_baseline = oldHasBaseline;
		session.stale = oldStale;
		session.quick_mode_section_revision = oldQuickModeRevision;
		session.map_asset_key = oldMapAssetKey;
		session.unit_mov_asset_key = oldUnitMovAssetKey;
		session.document_path = oldDocumentPath;
		session.condition_button_visible = oldConditionButtonVisible;
		m_editorOps = nullptr;
		const auto restored = session.condition_workspaces.find(
			session.active_workspace_key);
		if (restored != session.condition_workspaces.end())
			m_editorOps = restored->second->operations.get();
		m_documentPath = oldDocumentPath.c_str();
		RefreshConditionTree();
		UpdateViewport();
		diagnostic = L"新 Scene1 预览基线未能加载，原临时布置已恢复";
		return false;
	}

	auto activeRebased = rebasedWorkspaces.find(
		session.active_workspace_key);
	if (activeRebased != rebasedWorkspaces.end()) {
		session.editor_state = activeRebased->second->state;
		session.active_condition_scope = activeRebased->second->scope;
	}
	for (auto& entry : rebasedWorkspaces) {
		// Every condition workspace's operations object must point at the
		// session's stable active-state address. ActivateConditionScope swaps
		// values at that address when the user changes branches.
		entry.second->operations = std::make_unique<
			ccz::editor_preview::BattleEditorOps>(session.editor_state);
		const auto found = session.condition_workspaces.find(entry.first);
		if (found != session.condition_workspaces.end())
			found->second = std::move(entry.second);
		else
			session.condition_workspaces.emplace(entry.first,
				std::move(entry.second));
	}
	auto current = session.condition_workspaces.find(
		session.active_workspace_key);
	if (current != session.condition_workspaces.end()) {
		session.editor_state = current->second->state;
		session.active_condition_scope = current->second->scope;
		m_editorOps = current->second->operations.get();
		session.ui.selected_unit_cache_id = session.editor_state.selection
			? session.editor_state.selection->cache_id : 0;
		session.ui.selected_character_data_id =
			session.editor_state.characters.selected_data_id;
		session.ui.selected_brush_data_id =
			session.editor_state.selected_minion_brush
				? session.editor_state.selected_minion_brush->first_data_id : -1;
		session.ui.deployment_instruction =
			session.editor_state.placement.instruction_id;
	}
	RefreshConditionTree();
	RefreshCharacterList();
	RefreshDeploymentList();
	RefreshSelectedUnitIndex();
	UpdateViewport();
	RefreshEditorControls();
	Invalidate(FALSE);
	diagnostic = L"Scene1 指令已重新读取；临时布置已合并，旧的临时撤回记录已清除";
	AppendDiagnostic(diagnostic);
	return true;
}

void CSBattlePreviewPage::LoadCapturedBattle(
	ccz::editor_preview::BattlePreviewCapture capture,
	HWND sourceTreeWindow)
{
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	RecordBattleLoad();
	if (m_perfActive) m_perfAwaitingFirstPaint = true;
	#endif
	if (!capture.ok) {
		AppendDiagnostic(capture.diagnostic);
		return;
	}
	// Keep the exact S-section/document snapshot used to build the placement
	// cache. A future native writeback must compare against a fresh capture;
	// it must never infer source nodes from the mutable placement cache.
	ActiveSession().baseline_capture = std::move(capture);
	ActiveSession().document_path =
		ActiveSession().baseline_capture.input.document_path;
	ActiveSession().has_baseline = true;
	ActiveSession().stale = false;
	if (m_activeDocument != nullptr) {
		ActiveSession().observed_version.content_epoch =
			m_activeDocument->GetPreviewContentEpoch();
		ActiveSession().observed_version.tree_generation =
			m_activeDocument->GetPreviewTreeGeneration();
	}
	m_sourceTreeWindow = sourceTreeWindow;
	ActiveSession().quick_mode_section_revision = capture.input.section_revision;
	ActiveSession().condition_workspaces.clear();
	ActiveSession().active_workspace_key.clear();
	m_editorOps = nullptr;
	ActiveSession().condition_catalog = ccz::editor_preview::MakeEditorPreviewCatalog();
	ccz::preview::InstructionCatalog moduleCatalog;
	std::wstring catalogDiagnostic;
	if (ccz::editor_preview::LoadEditorInstructionCatalogForModule(
		moduleCatalog, &catalogDiagnostic)) {
		for (const auto& entry : moduleCatalog.names)
			ActiveSession().condition_catalog.names[entry.first] = entry.second;
		ActiveSession().condition_catalog.valid = true;
	} else if (!catalogDiagnostic.empty()) {
		AppendDiagnostic(catalogDiagnostic);
	}
	const auto condition = ccz::editor_preview::BuildBattleConditionTree(
		ActiveSession().baseline_capture, ActiveSession().condition_catalog, ActiveSession().condition_options);
	if (!condition.ok) {
		AppendDiagnostic(condition.diagnostic.empty()
			? L"无法建立 S 战场条件树模型" : condition.diagnostic);
		RefreshConditionTree();
		return;
	}
	ActiveSession().condition_model = condition.model;
	if (!ActivateConditionScope(condition.scope)) {
		AppendDiagnostic(L"S 战场条件分支无法加载；原有布置缓存已保留");
	}
	RefreshConditionTree();
}

bool CSBattlePreviewPage::ActivateConditionScope(
	const ccz::editor_preview::BattleConditionScope& scope)
{
	if (scope.workspace_key.empty() || !ActiveSession().baseline_capture.ok)
		return false;
	if (ActiveSession().active_workspace_key == scope.workspace_key && m_editorOps != nullptr) {
		ActiveSession().active_condition_scope = scope;
		const auto active = ActiveSession().condition_workspaces.find(ActiveSession().active_workspace_key);
		if (active != ActiveSession().condition_workspaces.end()) active->second->scope = scope;
		return true;
	}
	if (!ActiveSession().active_workspace_key.empty()) {
		const auto previous = ActiveSession().condition_workspaces.find(ActiveSession().active_workspace_key);
		if (previous != ActiveSession().condition_workspaces.end())
			previous->second->state = ActiveSession().editor_state;
	}

	const auto existing = ActiveSession().condition_workspaces.find(scope.workspace_key);
	if (existing != ActiveSession().condition_workspaces.end()) {
		ActiveSession().editor_state = existing->second->state;
		existing->second->scope = scope;
		m_editorOps = existing->second->operations.get();
		ActiveSession().active_workspace_key = scope.workspace_key;
		ActiveSession().active_condition_scope = scope;
		m_diagnostic.Empty();
		RefreshSelectedUnitIndex();
		UpdateViewport();
		RefreshEditorControls();
		return true;
	}

	ActiveSession().editor_state = {};
	ActiveSession().editor_state.scope_root = ActiveSession().baseline_capture.input.scope_key;
	ActiveSession().editor_state.characters.page_size = 20;
	ccz::preview::BattleParseOptions options;
	options.scope_root_key = ActiveSession().baseline_capture.input.scope_key;
	options.filter_deployment_nodes = true;
	options.reachable_deployment_keys = scope.reachable_deployment_keys;
	const auto parsed = ccz::preview::parse_battle_instructions(
		ActiveSession().baseline_capture.input.document_path,
		ActiveSession().baseline_capture.input.nodes, options);
	ActiveSession().editor_state.battle = parsed.state;
	for (auto& unit : ActiveSession().editor_state.battle.units) {
		if (!unit.fields_decoded ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
		unit.editor_deployment_instruction = unit.source_instruction;
		unit.editor_origin_fields = unit.raw_fields;
	}
	if (!parsed.field_table_complete)
		AppendDiagnostic(L"部分战场单位字段尚未解码");
	for (const auto& item : parsed.diagnostics) AppendDiagnostic(item);

	std::wstring assetsDiagnostic;
	m_assets.LoadForDocument(ActiveSession().baseline_capture.input.document_path,
		ActiveSession().editor_state.battle.map.map_id, &assetsDiagnostic);
	UpdateSessionAssetKeys();
	if (!m_unitSpritePath.empty() &&
		(m_unitSpritePath != m_assets.unit_mov_path() ||
		 m_unitSpriteRevision != m_assets.unit_mov_revision())) {
		ClearUnitSprites();
	}
	AppendDiagnostic(assetsDiagnostic);
	const auto& characterCatalog = m_assets.character_catalog();
	if (characterCatalog.ok) {
		ActiveSession().editor_state.characters.entries = characterCatalog.characters;
		ActiveSession().editor_state.minion_brushes = characterCatalog.brushes;
	} else {
		AppendDiagnostic(characterCatalog.diagnostic);
	}
	const bool mapLoaded = LoadMapImage();
	std::vector<std::uint8_t> decodedTerrain;
	if (!mapLoaded || !ActiveSession().editor_state.battle.map.projection_valid ||
		!ccz::preview::decode_battle_hexzmap(
			m_assets.hexzmap_e5(), ActiveSession().editor_state.battle.map.map_id,
			ActiveSession().editor_state.battle.map.width, ActiveSession().editor_state.battle.map.height,
			decodedTerrain)) {
		ActiveSession().editor_state.battle.terrain.clear();
		AppendDiagnostic(L"Hexzmap 地形未加载或与战场 JPG 网格不匹配");
	} else {
		ActiveSession().editor_state.battle.terrain.swap(decodedTerrain);
	}

	auto workspace = std::make_unique<ccz::editor_preview::BattlePreviewConditionWorkspace>();
	workspace->state = ActiveSession().editor_state;
	workspace->scope = scope;
	workspace->operations =
		std::make_unique<ccz::editor_preview::BattleEditorOps>(ActiveSession().editor_state);
	m_editorOps = workspace->operations.get();
	ActiveSession().condition_workspaces.emplace(scope.workspace_key, std::move(workspace));
	ActiveSession().active_workspace_key = scope.workspace_key;
	ActiveSession().active_condition_scope = scope;
	RefreshSelectedUnitIndex();
	UpdateViewport();
	RefreshEditorControls();
	return true;
}

void CSBattlePreviewPage::RefreshConditionTree()
{
	m_conditionTreePane.UpdateModel(ActiveSession().condition_model, {}, ActiveSession().condition_options);
	ActiveSession().condition_button_visible = ActiveSession().condition_model != nullptr &&
		m_conditionTreePane.HasControllableNodes();
	if (m_conditionTreeButton.GetSafeHwnd() != nullptr)
		m_conditionTreeButton.ShowWindow(ActiveSession().condition_button_visible
			? SW_SHOW : SW_HIDE);
	if (!ActiveSession().condition_button_visible) m_conditionTreePane.ShowPane(false);
	if (m_controlsCreated) LayoutEditorControls();
}

void CSBattlePreviewPage::OnConditionTreeClicked()
{
	if (!ActiveSession().condition_button_visible ||
		m_conditionTreePane.GetSafeHwnd() == nullptr ||
		!m_conditionTreePane.HasControllableNodes()) return;
	m_conditionTreePane.ShowPane(true);
	Invalidate(FALSE);
}

LRESULT CSBattlePreviewPage::ApplyConditionActionAndRefresh(
	const ccz::preview::LogicalNodeKey& nodeKey,
	ccz::preview::ConditionTreeAction action,
	std::int32_t integerValue)
{
	if (ActiveSession().condition_model == nullptr) return 0;
	auto candidate = ActiveSession().condition_options;
	std::wstring diagnostic;
	if (!ccz::editor_preview::ApplyConditionTreeActionToOptions(
		candidate, *ActiveSession().condition_model, nodeKey, action, integerValue,
		&diagnostic)) {
		if (!diagnostic.empty()) AppendDiagnostic(diagnostic);
		return 0;
	}
	ActiveSession().condition_options = std::move(candidate);
	if (PostMessage(kConditionActionMessage, 0, 0) == FALSE)
		return OnConditionActionMessage(0, 0);
	return 0;
}

LRESULT CSBattlePreviewPage::OnConditionActionMessage(WPARAM, LPARAM)
{
	if (!ActiveSession().baseline_capture.ok) return 0;
	const auto condition = ccz::editor_preview::BuildBattleConditionTree(
		ActiveSession().baseline_capture, ActiveSession().condition_catalog, ActiveSession().condition_options);
	if (!condition.ok) {
		AppendDiagnostic(condition.diagnostic.empty()
			? L"S 战场条件分支无法重建；当前布置缓存已保留"
			: condition.diagnostic);
		RefreshConditionTree();
		return 0;
	}
	if (!ActivateConditionScope(condition.scope)) {
		AppendDiagnostic(L"S 战场条件分支无法加载；当前布置缓存已保留");
		RefreshConditionTree();
		return 0;
	}
	ActiveSession().condition_model = condition.model;
	RefreshConditionTree();
	Invalidate(FALSE);
	return 0;
}

bool CSBattlePreviewPage::LoadMapImage()
{
	if (!ActiveSession().editor_state.battle.map.valid || ActiveSession().editor_state.battle.map.map_id < 0 ||
		m_assets.map_jpg_path().empty() ||
		ActiveSession().map_asset_key.path.empty()) {
		if (!m_activeMapKey.path.empty()) UnpinActiveBitmap(m_activeMapKey);
		m_activeMapKey = {};
		m_mapBitmap = nullptr;
		AppendDiagnostic(L"S 战场 JPG 不可用");
		return false;
	}
	const auto key = ActiveSession().map_asset_key;
	if (!(m_activeMapKey == key)) {
		if (!m_activeMapKey.path.empty()) UnpinActiveBitmap(m_activeMapKey);
		m_activeMapKey = key;
		m_mapBitmap = nullptr;
	}
	Gdiplus::Bitmap* loaded = FindCachedBitmap(key);
	if (loaded == nullptr) {
		#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
		RecordMapDecode();
		#endif
		Gdiplus::Bitmap* source = Gdiplus::Bitmap::FromFile(
			m_assets.map_jpg_path().c_str(), FALSE);
		if (source == nullptr || source->GetLastStatus() != Gdiplus::Ok) {
			delete source;
			AppendDiagnostic(L"S 战场 JPG 解码失败");
			return false;
		}
		const Gdiplus::Rect bounds(0, 0,
			static_cast<INT>(source->GetWidth()),
			static_cast<INT>(source->GetHeight()));
		Gdiplus::Bitmap* converted = source->Clone(bounds, PixelFormat32bppARGB);
		delete source;
		if (converted == nullptr || converted->GetLastStatus() != Gdiplus::Ok) {
			delete converted;
			AppendDiagnostic(L"S 战场 JPG 像素格式转换失败");
			return false;
		}
		loaded = StoreCachedBitmap(key, converted);
	}
	if (loaded == nullptr) {
		AppendDiagnostic(L"S 战场 JPG 未能加入共享图像缓存");
		return false;
	}
	m_mapBitmap = loaded;
	const int width = static_cast<int>(loaded->GetWidth());
	const int height = static_cast<int>(loaded->GetHeight());
	if (width < kCellPixels || height < kCellPixels ||
		width % kCellPixels != 0 || height % kCellPixels != 0) {
		ActiveSession().editor_state.battle.map.projection_valid = false;
		AppendDiagnostic(L"JPG 尺寸无法证明 48 像素逻辑格投影");
		return true;
	}
	ActiveSession().editor_state.battle.map.width = width / kCellPixels;
	ActiveSession().editor_state.battle.map.height = height / kCellPixels;
	ActiveSession().editor_state.battle.map.offset_x = ActiveSession().editor_state.battle.map.offset_y = 0;
	ActiveSession().editor_state.battle.map.projection_valid = true;
	return true;
}

void CSBattlePreviewPage::AppendDiagnostic(const std::wstring& value)
{
	if (value.empty()) return;
	if (!m_diagnostic.IsEmpty()) m_diagnostic += _T(" | ");
	m_diagnostic += value.c_str();
}

void CSBattlePreviewPage::ClearUnitSprites()
{
	const auto pinned = m_activeSpritePins;
	for (const auto& key : pinned) UnpinActiveBitmap(key);
	m_activeSpritePins.clear();
	m_unitSprites.clear();
	m_unitSpritePath.clear();
	m_unitSpriteRevision = 0;
}

void CSBattlePreviewPage::ReleaseActiveBitmapPins()
{
	ClearUnitSprites();
	const auto pinned = m_activeBitmapPins;
	for (const auto& key : pinned) {
		m_bitmapCachePolicy.Unpin(key);
		m_activeBitmapPins.erase(key);
	}
	m_activeMapKey = {};
	m_mapBitmap = nullptr;
	EvictBitmapsToBudget();
}

void CSBattlePreviewPage::PinActiveBitmap(
	const ccz::editor_preview::BattleBitmapCacheKey& key)
{
	if (key.path.empty() || m_activeBitmapPins.find(key) !=
		m_activeBitmapPins.end()) return;
	if (m_bitmapCachePolicy.Pin(key)) m_activeBitmapPins.insert(key);
}

void CSBattlePreviewPage::UnpinActiveBitmap(
	const ccz::editor_preview::BattleBitmapCacheKey& key) noexcept
{
	const auto found = m_activeBitmapPins.find(key);
	if (found == m_activeBitmapPins.end()) return;
	m_bitmapCachePolicy.Unpin(key);
	m_activeBitmapPins.erase(found);
	EvictBitmapsToBudget();
}

void CSBattlePreviewPage::EvictBitmapsToBudget()
{
	const auto evicted = m_bitmapCachePolicy.EvictToBudget(
		kBattleBitmapCacheBudgetBytes);
	for (const auto& key : evicted) m_decodedBitmaps.erase(key);
}

Gdiplus::Bitmap* CSBattlePreviewPage::FindCachedBitmap(
	const ccz::editor_preview::BattleBitmapCacheKey& key)
{
	const auto found = m_decodedBitmaps.find(key);
	if (found == m_decodedBitmaps.end() ||
		!m_bitmapCachePolicy.Contains(key)) return nullptr;
	PinActiveBitmap(key);
	return found->second.get();
}

Gdiplus::Bitmap* CSBattlePreviewPage::StoreCachedBitmap(
	const ccz::editor_preview::BattleBitmapCacheKey& key,
	Gdiplus::Bitmap* bitmap)
{
	if (bitmap == nullptr || bitmap->GetLastStatus() != Gdiplus::Ok) {
		delete bitmap;
		return nullptr;
	}
	const auto existing = m_decodedBitmaps.find(key);
	if (existing != m_decodedBitmaps.end()) {
		delete bitmap;
		return FindCachedBitmap(key);
	}
	const std::uint64_t decodedBytes =
		static_cast<std::uint64_t>(bitmap->GetWidth()) *
		static_cast<std::uint64_t>(bitmap->GetHeight()) * 4u;
	if (decodedBytes == 0 || decodedBytes >
		(std::numeric_limits<std::size_t>::max)()) {
		delete bitmap;
		return nullptr;
	}
	auto inserted = m_decodedBitmaps.emplace(key,
		std::unique_ptr<Gdiplus::Bitmap>(bitmap));
	if (!inserted.second) return nullptr;
	if (!m_bitmapCachePolicy.RecordDecode(key,
		static_cast<std::size_t>(decodedBytes))) {
		m_decodedBitmaps.erase(inserted.first);
		return nullptr;
	}
	PinActiveBitmap(key);
	Gdiplus::Bitmap* cached = inserted.first->second.get();
	EvictBitmapsToBudget();
	return cached;
}

void CSBattlePreviewPage::UpdateSessionAssetKeys()
{
	if (m_activeSession == nullptr) return;
	ActiveSession().map_asset_key =
		ccz::editor_preview::MakeBattleBitmapCacheKey(
			m_assets.map_jpg_path(), m_assets.map_jpg_revision());
	ActiveSession().unit_mov_asset_key =
		ccz::editor_preview::MakeBattleBitmapCacheKey(
			m_assets.unit_mov_path(), m_assets.unit_mov_revision());
}

Gdiplus::Bitmap* CSBattlePreviewPage::GetUnitSprite(
	const ccz::preview::BattleUnit& unit, int rawDirection,
	ccz::editor_preview::BattleAssetCache::UnitSpriteRef* spriteRef)
{
	if (spriteRef == nullptr) return nullptr;
	*spriteRef = {};
	const auto armKind = ccz::preview::battle_visual_arm_kind(unit);
	if (!armKind || !m_assets.ResolveUnitSprite(unit.data_id, *armKind,
			rawDirection, spriteRef)) return nullptr;
	if (m_unitSpritePath != m_assets.unit_mov_path() ||
		m_unitSpriteRevision != m_assets.unit_mov_revision()) {
		ClearUnitSprites();
		m_unitSpritePath = m_assets.unit_mov_path();
		m_unitSpriteRevision = m_assets.unit_mov_revision();
	}
	const int key = spriteRef->resource_index * 16 + spriteRef->frame;
	const auto found = m_unitSprites.find(key);
	if (found != m_unitSprites.end()) return found->second;
	if (m_unitSpritePath.empty()) {
		m_unitSprites[key] = nullptr;
		return nullptr;
	}
	std::wostringstream subresource;
	subresource << L"resource-" << spriteRef->resource_index
		<< L"-frame-" << spriteRef->frame;
	const auto cacheKey = ccz::editor_preview::MakeBattleBitmapCacheKey(
		m_assets.unit_mov_path(), m_assets.unit_mov_revision(),
		subresource.str());
	if (Gdiplus::Bitmap* cached = FindCachedBitmap(cacheKey)) {
		m_unitSprites[key] = cached;
		m_activeSpritePins.insert(cacheKey);
		return cached;
	}
	CString path(m_unitSpritePath.c_str());
	CString diagnostic;
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	RecordSpriteDecode();
	#endif
	Gdiplus::Bitmap* sheet = LoadBitmapFromE5(
		path, spriteRef->resource_index, diagnostic, false);
	if (sheet == nullptr || sheet->GetLastStatus() != Gdiplus::Ok) {
		delete sheet;
		m_unitSprites[key] = nullptr;
		if (!diagnostic.IsEmpty())
			AppendDiagnostic(std::wstring(L"Unit_mov 读取失败：") +
				diagnostic.GetString());
		return nullptr;
	}
	const int frameCount = static_cast<int>(sheet->GetHeight()) / kCellPixels;
	if (sheet->GetWidth() < kCellPixels || spriteRef->frame < 0 ||
		spriteRef->frame >= frameCount) {
		delete sheet;
		m_unitSprites[key] = nullptr;
		AppendDiagnostic(L"Unit_mov 帧尺寸无效");
		return nullptr;
	}
	auto* frame = new Gdiplus::Bitmap(
		kCellPixels, kCellPixels, PixelFormat32bppARGB);
	if (frame->GetLastStatus() != Gdiplus::Ok) {
		delete frame;
		delete sheet;
		m_unitSprites[key] = nullptr;
		return nullptr;
	}
	Gdiplus::Graphics frameGraphics(frame);
	frameGraphics.Clear(Gdiplus::Color(0, 0, 0, 0));
	frameGraphics.DrawImage(sheet,
		Gdiplus::Rect(0, 0, kCellPixels, kCellPixels), 0,
		spriteRef->frame * kCellPixels, kCellPixels, kCellPixels,
		Gdiplus::UnitPixel);
	delete sheet;
	ApplyMagentaTransparency(frame);
	Gdiplus::Bitmap* cached = StoreCachedBitmap(cacheKey, frame);
	if (cached == nullptr) {
		m_unitSprites[key] = nullptr;
		return nullptr;
	}
	m_unitSprites[key] = cached;
	m_activeSpritePins.insert(cacheKey);
	return cached;
}

void CSBattlePreviewPage::ApplyMagentaTransparency(Gdiplus::Bitmap* bitmap)
{
	if (bitmap == nullptr || bitmap->GetWidth() == 0 ||
		bitmap->GetHeight() == 0) return;
	Gdiplus::BitmapData data{};
	const Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap->GetWidth()),
		static_cast<INT>(bitmap->GetHeight()));
	if (bitmap->LockBits(&rect,
			Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
			PixelFormat32bppARGB, &data) != Gdiplus::Ok) return;
	for (int y = 0; y < rect.Height; ++y) {
		auto* row = reinterpret_cast<DWORD*>(
			reinterpret_cast<BYTE*>(data.Scan0) + y * data.Stride);
		for (int x = 0; x < rect.Width; ++x) {
			const DWORD pixel = row[x];
			const BYTE alpha = static_cast<BYTE>((pixel >> 24) & 0xff);
			const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xff);
			const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xff);
			const BYTE blue = static_cast<BYTE>(pixel & 0xff);
			if (alpha && red >= 242 && green <= 16 && blue >= 230)
				row[x] = 0;
		}
	}
	bitmap->UnlockBits(&data);
}

void CSBattlePreviewPage::UpdateViewport()
{
	if (GetSafeHwnd() == nullptr || m_viewportUpdating) return;
	m_viewportUpdating = true;
	const int imageWidth = m_mapBitmap
		? static_cast<int>(m_mapBitmap->GetWidth())
		: ActiveSession().editor_state.battle.map.width * kCellPixels;
	const int imageHeight = m_mapBitmap
		? static_cast<int>(m_mapBitmap->GetHeight())
		: ActiveSession().editor_state.battle.map.height * kCellPixels;
	const int contentWidth = static_cast<int>(std::lround(imageWidth * m_zoom));
	const int contentHeight = static_cast<int>(std::lround(imageHeight * m_zoom));
	if (imageWidth <= 0 || imageHeight <= 0) {
		m_scrollX = m_scrollY = 0;
		ShowScrollBar(SB_HORZ, FALSE);
		ShowScrollBar(SB_VERT, FALSE);
		m_viewportUpdating = false;
		return;
	}
	for (int pass = 0; pass < 3; ++pass) {
		CRect client;
		GetClientRect(&client);
		const int availableWidth = (std::max)(0, client.Width() -
			(m_sidebarVisible ? kSidebarWidth : 0));
		ShowScrollBar(SB_HORZ, contentWidth > availableWidth);
		ShowScrollBar(SB_VERT, contentHeight > client.Height());
	}
	// Showing a native scrollbar changes the non-client size. Reposition the
	// child sidebar once more so it remains outside the map viewport.
	LayoutEditorControls();
	ClampViewport();
	CRect client;
	GetClientRect(&client);
	SCROLLINFO horizontal{};
	horizontal.cbSize = sizeof(horizontal);
	horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	horizontal.nMin = 0;
	horizontal.nMax = (std::max)(0, contentWidth - 1);
	horizontal.nPage = static_cast<UINT>((std::max)(1, m_canvasRect.Width()));
	horizontal.nPos = m_scrollX;
	SetScrollInfo(SB_HORZ, &horizontal, TRUE);
	SCROLLINFO vertical{};
	vertical.cbSize = sizeof(vertical);
	vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	vertical.nMin = 0;
	vertical.nMax = (std::max)(0, contentHeight - 1);
	vertical.nPage = static_cast<UINT>((std::max)(1, m_canvasRect.Height()));
	vertical.nPos = m_scrollY;
	SetScrollInfo(SB_VERT, &vertical, TRUE);
	m_viewportUpdating = false;
}

void CSBattlePreviewPage::ClampViewport()
{
	const int width = m_mapBitmap ? static_cast<int>(m_mapBitmap->GetWidth())
		: ActiveSession().editor_state.battle.map.width * kCellPixels;
	const int height = m_mapBitmap ? static_cast<int>(m_mapBitmap->GetHeight())
		: ActiveSession().editor_state.battle.map.height * kCellPixels;
	m_scrollX = (std::max)(0, (std::min)(m_scrollX,
		(std::max)(0, static_cast<int>(width * m_zoom) - m_canvasRect.Width())));
	m_scrollY = (std::max)(0, (std::min)(m_scrollY,
		(std::max)(0, static_cast<int>(height * m_zoom) - m_canvasRect.Height())));
}

void CSBattlePreviewPage::SetViewportScroll(int x, int y, bool invalidate)
{
	m_scrollX = x;
	m_scrollY = y;
	UpdateViewport();
	if (invalidate) Invalidate(FALSE);
}

void CSBattlePreviewPage::ZoomAt(CPoint point, float zoom)
{
	const float next = (std::max)(kMinZoom, (std::min)(kMaxZoom, zoom));
	if (std::abs(next - m_zoom) < 0.001f) return;
	const int localX = point.x - m_canvasRect.left;
	const int localY = point.y - m_canvasRect.top;
	const float logicalX = (localX + m_scrollX) / m_zoom;
	const float logicalY = (localY + m_scrollY) / m_zoom;
	m_zoom = next;
	m_scrollX = static_cast<int>(std::lround(logicalX * next - localX));
	m_scrollY = static_cast<int>(std::lround(logicalY * next - localY));
	UpdateViewport();
	Invalidate(FALSE);
}

std::optional<ccz::preview::BattleCoord>
CSBattlePreviewPage::BattleCoordFromScreen(CPoint point) const
{
	if (m_mapBitmap == nullptr || !ActiveSession().editor_state.battle.map.projection_valid ||
		m_zoom <= 0 || !m_canvasRect.PtInRect(point))
		return std::nullopt;
	const float x = static_cast<float>(point.x - m_canvasRect.left + m_scrollX);
	const float y = static_cast<float>(point.y - m_canvasRect.top + m_scrollY);
	if (x < 0 || y < 0 || x >= m_mapBitmap->GetWidth() * m_zoom ||
		y >= m_mapBitmap->GetHeight() * m_zoom) return std::nullopt;
	const ccz::preview::BattleCoord coordinate{
		static_cast<int>(std::floor(x / (kCellPixels * m_zoom))),
		static_cast<int>(std::floor(y / (kCellPixels * m_zoom)))};
	return ccz::preview::is_battle_coord_in_bounds(coordinate,
		ActiveSession().editor_state.battle.map.width, ActiveSession().editor_state.battle.map.height)
		? std::optional<ccz::preview::BattleCoord>(coordinate)
		: std::nullopt;
}

std::optional<std::size_t> CSBattlePreviewPage::HitTestUnit(
	const ccz::preview::BattleCoord& coordinate) const
{
	std::optional<std::size_t> hit;
	for (std::size_t index = 0;
		index < ActiveSession().editor_state.battle.units.size(); ++index) {
		const auto& unit = ActiveSession().editor_state.battle.units[index];
		if (!unit.fields_decoded || unit.editor_pending_delete ||
			(!unit.editor_staged_new &&
			 !in_scope(unit.source_key, ActiveSession().editor_state.scope_root)) ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit) ||
			unit.position != coordinate) continue;
		if (hit) return std::nullopt;
		hit = index;
	}
	return hit;
}

void CSBattlePreviewPage::DrawCanvas(
	Gdiplus::Graphics& graphics, int width, int height)
{
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	graphics.SetClip(Gdiplus::Rect(0, 0, width, height),
		Gdiplus::CombineModeReplace);
	Gdiplus::SolidBrush background(Gdiplus::Color(255, 32, 32, 32));
	graphics.FillRectangle(&background, 0, 0, width, height);
	const int ox = m_canvasRect.left - m_scrollX;
	const int oy = m_canvasRect.top - m_scrollY;
	const float cell = kCellPixels * m_zoom;
	const bool currentMapImage = ActiveSession().baseline_capture.ok &&
		m_mapBitmap != nullptr &&
		m_activeMapKey == ActiveSession().map_asset_key;
	const bool projection = currentMapImage &&
		ActiveSession().editor_state.battle.map.projection_valid &&
		ActiveSession().editor_state.battle.map.width > 0 &&
		ActiveSession().editor_state.battle.map.height > 0;
	graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
		m_canvasRect.Width(), m_canvasRect.Height()), Gdiplus::CombineModeReplace);
	if (currentMapImage) {
		graphics.DrawImage(m_mapBitmap,
			Gdiplus::Rect(ox, oy,
				static_cast<int>(std::lround(m_mapBitmap->GetWidth() * m_zoom)),
				static_cast<int>(std::lround(m_mapBitmap->GetHeight() * m_zoom))),
			0, 0, static_cast<INT>(m_mapBitmap->GetWidth()),
			static_cast<INT>(m_mapBitmap->GetHeight()), Gdiplus::UnitPixel);
	}
	if (projection) {
		Gdiplus::Pen grid(Gdiplus::Color(105, 255, 255, 255), 1.0f);
		for (int x = 0; x <= ActiveSession().editor_state.battle.map.width; ++x) {
			const float px = ox + x * cell;
			graphics.DrawLine(&grid, px, static_cast<float>(oy), px,
				oy + ActiveSession().editor_state.battle.map.height * cell);
		}
		for (int y = 0; y <= ActiveSession().editor_state.battle.map.height; ++y) {
			const float py = oy + y * cell;
			graphics.DrawLine(&grid, static_cast<float>(ox), py,
				ox + ActiveSession().editor_state.battle.map.width * cell, py);
		}
		if (m_hoverCell) {
			Gdiplus::SolidBrush hover(Gdiplus::Color(60, 255, 235, 70));
			graphics.FillRectangle(&hover, ox + m_hoverCell->x * cell,
				oy + m_hoverCell->y * cell, cell, cell);
		}
	}
	int decoded = 0;
	if (projection) for (std::size_t i = 0;
		i < ActiveSession().editor_state.battle.units.size(); ++i) {
		const auto& unit = ActiveSession().editor_state.battle.units[i];
		if (!unit.fields_decoded || unit.editor_pending_delete ||
			(!unit.editor_staged_new &&
			 !in_scope(unit.source_key, ActiveSession().editor_state.scope_root)) ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit) ||
			(ActiveSession().editor_state.mode ==
				ccz::editor_preview::BattleEditorMode::AdjustPosition &&
			 unit.editor_cache_id == ActiveSession().editor_state.adjusting_cache_id) ||
			!ccz::preview::is_battle_coord_in_bounds(
				unit.position, ActiveSession().editor_state.battle.map.width,
				ActiveSession().editor_state.battle.map.height)) continue;
		++decoded;
		const float cx = ox + (unit.position.x + 0.5f) * cell;
		const float by = oy + (unit.position.y + 1.0f) * cell;
		const int size = (std::max)(8,
			static_cast<int>(std::lround(kCellPixels * m_zoom)));
		const int dx = static_cast<int>(std::lround(cx)) - size / 2;
		const int dy = static_cast<int>(std::lround(by)) - size;
		ccz::editor_preview::BattleAssetCache::UnitSpriteRef spriteRef;
		Gdiplus::Bitmap* sprite = GetUnitSprite(
			unit, unit.direction, &spriteRef);
		if (sprite && sprite->GetLastStatus() == Gdiplus::Ok) {
			Gdiplus::ColorMatrix matrix = {
				1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
				0,0,0,unit.hidden ? 0.42f : 1.0f,0, 0,0,0,0,1};
			Gdiplus::ImageAttributes attributes;
			attributes.SetColorMatrix(&matrix);
			if (spriteRef.flip_x) {
				Gdiplus::Matrix oldTransform;
				graphics.GetTransform(&oldTransform);
				Gdiplus::Matrix flip;
				flip.Translate(static_cast<float>(dx + size),
					static_cast<float>(dy));
				flip.Scale(-1.0f, 1.0f);
				graphics.SetTransform(&flip);
				graphics.DrawImage(sprite, Gdiplus::Rect(0, 0, size, size),
					0, 0, 48, 48, Gdiplus::UnitPixel, &attributes);
				graphics.SetTransform(&oldTransform);
			} else {
				graphics.DrawImage(sprite, Gdiplus::Rect(dx, dy, size, size),
					0, 0, 48, 48, Gdiplus::UnitPixel, &attributes);
			}
			} else {
				if (ccz::preview::is_battle_deployment_slot_marker(unit)) {
					draw_deployment_slot_marker(graphics, cx, by, cell,
						unit.deployment_index, 1.0f, unit.hidden);
				} else {
					const int radius = (std::max)(5,
						static_cast<int>(cell * 0.28f));
					Gdiplus::SolidBrush unitBrush(Gdiplus::Color(
						unit.hidden ? 105 : 235, 35, 110, 225));
					graphics.FillEllipse(&unitBrush,
						static_cast<int>(cx) - radius,
						static_cast<int>(by) - radius * 2,
						radius * 2, radius * 2);
				}
			}
		if (m_selectedUnit == i) {
			Gdiplus::Pen selected(Gdiplus::Color(255,255,220,40), 2.0f);
			graphics.DrawRectangle(&selected,
				static_cast<int>(cx-cell*0.46f),
				static_cast<int>(by-cell*0.96f),
				(std::max)(1, static_cast<int>(cell*0.92f)),
				(std::max)(1, static_cast<int>(cell*0.92f)));
		}
	}
	if (m_editorOps && m_hoverCell &&
		(ActiveSession().editor_state.mode == ccz::editor_preview::BattleEditorMode::Place ||
		 ActiveSession().editor_state.mode ==
			ccz::editor_preview::BattleEditorMode::AdjustPosition)) {
		const auto placement = ActiveSession().editor_state.mode ==
			ccz::editor_preview::BattleEditorMode::Place
			? m_editorOps->PreviewPlacement(*m_hoverCell)
			: m_editorOps->PreviewPositionAdjustment(*m_hoverCell);
		const bool adjusting = ActiveSession().editor_state.mode ==
			ccz::editor_preview::BattleEditorMode::AdjustPosition;
		if (placement.valid || (adjusting && placement.unit.fields_decoded)) {
			const float centerX = ox + (m_hoverCell->x + 0.5f) * cell;
			const float baseY = oy + (m_hoverCell->y + 1.0f) * cell;
			const int size = (std::max)(8, static_cast<int>(
				std::lround(kCellPixels * m_zoom)));
			ccz::editor_preview::BattleAssetCache::UnitSpriteRef spriteRef;
			Gdiplus::Bitmap* sprite = GetUnitSprite(
				placement.unit, placement.unit.direction, &spriteRef);
			if (!placement.valid && adjusting) {
				Gdiplus::Pen invalidTarget(Gdiplus::Color(230, 235, 45, 45), 2.0f);
				graphics.DrawRectangle(&invalidTarget,
					static_cast<int>(ox + m_hoverCell->x * cell),
					static_cast<int>(oy + m_hoverCell->y * cell),
					(std::max)(1, static_cast<int>(cell)),
					(std::max)(1, static_cast<int>(cell)));
			}
			if (sprite && sprite->GetLastStatus() == Gdiplus::Ok) {
				Gdiplus::ColorMatrix ghostMatrix = {
					1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
					0,0,0,placement.valid ? 0.55f : 0.35f,0, 0,0,0,0,1};
				Gdiplus::ImageAttributes ghostAttributes;
				ghostAttributes.SetColorMatrix(&ghostMatrix);
				const int dx = static_cast<int>(std::lround(centerX)) - size / 2;
				const int dy = static_cast<int>(std::lround(baseY)) - size;
				if (spriteRef.flip_x) {
					Gdiplus::Matrix oldTransform;
					graphics.GetTransform(&oldTransform);
					Gdiplus::Matrix flip;
					flip.Translate(static_cast<float>(dx + size),
						static_cast<float>(dy));
					flip.Scale(-1.0f, 1.0f);
					graphics.SetTransform(&flip);
					graphics.DrawImage(sprite, Gdiplus::Rect(0, 0, size, size),
						0, 0, 48, 48, Gdiplus::UnitPixel, &ghostAttributes);
					graphics.SetTransform(&oldTransform);
				} else {
					graphics.DrawImage(sprite, Gdiplus::Rect(dx, dy, size, size),
						0, 0, 48, 48, Gdiplus::UnitPixel, &ghostAttributes);
				}
			} else if (ccz::preview::is_battle_deployment_slot_marker(
				placement.unit)) {
				draw_deployment_slot_marker(graphics, centerX, baseY, cell,
					placement.unit.deployment_index,
					placement.valid ? 0.55f : 0.35f,
					placement.unit.hidden);
			}
		}
	}
	Gdiplus::SolidBrush banner(Gdiplus::Color(220, 20, 20, 20));
	Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
	Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 225, 170));
	CString title;
	if (projection) title.Format(
		_T("S 战场预览 地图:%d 网格:%dx%d 全单位 缩放:%.2fx 已解析:%d"),
		ActiveSession().editor_state.battle.map.map_id,
		ActiveSession().editor_state.battle.map.width, ActiveSession().editor_state.battle.map.height,
		m_zoom, decoded);
	else title.Format(_T("S 战场预览 地图:%d 网格投影未闭合 缩放:%.2fx"),
		ActiveSession().editor_state.battle.map.map_id, m_zoom);
	graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
		m_canvasRect.Width(), m_canvasRect.Height()), Gdiplus::CombineModeReplace);
	graphics.FillRectangle(&banner, m_canvasRect.left, 0,
		m_canvasRect.Width(), 54);
	graphics.DrawString(title, -1, &font,
		Gdiplus::PointF(static_cast<float>(m_canvasRect.left + 8), 6.0f), &text);
	if (m_sidebarVisible) {
		graphics.SetClip(Gdiplus::Rect(0, 0, width, height),
			Gdiplus::CombineModeReplace);
		Gdiplus::SolidBrush sidebar(Gdiplus::Color(255, 40, 40, 40));
		graphics.FillRectangle(&sidebar, m_canvasRect.right, 0,
			width - m_canvasRect.right, height);
	}
	CString visibleDiagnostic = m_quickModeDiagnostic.IsEmpty()
		? m_diagnostic : m_quickModeDiagnostic;
	if (!m_quickModeDiagnostic.IsEmpty() && !m_diagnostic.IsEmpty()) {
		visibleDiagnostic += _T(" | ");
		visibleDiagnostic += m_diagnostic;
	}
	if (!visibleDiagnostic.IsEmpty()) {
		Gdiplus::SolidBrush panel(Gdiplus::Color(210, 20, 20, 20));
		graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
			m_canvasRect.Width(), m_canvasRect.Height()),
			Gdiplus::CombineModeReplace);
		graphics.FillRectangle(&panel, m_canvasRect.left,
			(std::max)(0, height-26), m_canvasRect.Width(), 26);
		graphics.DrawString(visibleDiagnostic, -1, &font,
			Gdiplus::PointF(8.0f, static_cast<float>(
				(std::max)(0, height-22))), &text);
	}
}

void CSBattlePreviewPage::OnSize(UINT type, int cx, int cy)
{
	CWnd::OnSize(type, cx, cy);
	LayoutEditorControls();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnPaint()
{
	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	if (client.IsRectEmpty()) {
		#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
		FinishPerformanceActivation();
		#endif
		return;
	}
	CDC memory;
	memory.CreateCompatibleDC(&dc);
	CBitmap buffer;
	buffer.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
	CBitmap* old = memory.SelectObject(&buffer);
	{
		Gdiplus::Graphics graphics(memory.m_hDC);
		DrawCanvas(graphics, client.Width(), client.Height());
	}
	dc.BitBlt(0, 0, client.Width(), client.Height(),
		&memory, 0, 0, SRCCOPY);
	memory.SelectObject(old);
	#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
	if (m_perfAwaitingFirstPaint) FinishPerformanceActivation();
	#endif
}

BOOL CSBattlePreviewPage::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void CSBattlePreviewPage::OnLButtonDown(UINT, CPoint point)
{
	SetFocus();
	const auto coordinate = BattleCoordFromScreen(point);
	if (!coordinate) return;
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->ConfirmPositionAdjustment(*coordinate));
		return;
	}
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::Place) {
		m_leftStampActive = true;
		m_lastStampCell.reset();
		SetCapture();
		StampAt(*coordinate);
		return;
	}
	m_selectedUnit = HitTestUnit(*coordinate);
	if (m_selectedUnit && m_editorOps) {
		ShowOperationResult(m_editorOps->SelectAt(*coordinate));
	} else {
		ActiveSession().editor_state.selection.reset();
		Invalidate(FALSE);
	}
}

void CSBattlePreviewPage::OnLButtonUp(UINT, CPoint)
{
	if (!m_leftStampActive) return;
	m_leftStampActive = false;
	m_lastStampCell.reset();
	if (GetCapture() == this) ReleaseCapture();
}

void CSBattlePreviewPage::OnRButtonDown(UINT, CPoint point)
{
	SetFocus();
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::Place) {
		m_leftStampActive = false;
		m_lastStampCell.reset();
		if (GetCapture() == this) ReleaseCapture();
		ShowOperationResult(m_editorOps->CancelPlacementBrush());
		return;
	}
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->CancelPositionAdjustment());
		return;
	}
	const auto coordinate = BattleCoordFromScreen(point);
	if (!coordinate || !m_editorOps) return;
	m_hoverCell = coordinate;
	const auto hit = HitTestUnit(*coordinate);
	if (!hit) {
		m_diagnostic = _T("目标格没有唯一的可操作单位");
		Invalidate(FALSE);
		return;
	}
	const std::uint64_t cacheId =
		ActiveSession().editor_state.battle.units[*hit].editor_cache_id;
	ShowOperationResult(m_editorOps->SelectAt(*coordinate));
	CMenu menu;
	if (!menu.CreatePopupMenu()) return;
	constexpr UINT kAdjustPositionCommand = 1;
	constexpr UINT kDeleteUnitCommand = 2;
	menu.AppendMenu(MF_STRING, kAdjustPositionCommand, _T("调整位置"));
	menu.AppendMenu(MF_STRING, kDeleteUnitCommand, _T("删除"));
	CPoint screenPoint = point;
	ClientToScreen(&screenPoint);
	const UINT command = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		screenPoint.x, screenPoint.y, this);
	if (command == kAdjustPositionCommand) {
		ShowOperationResult(m_editorOps->BeginPositionAdjustment(cacheId));
	} else if (command == kDeleteUnitCommand) {
		ShowOperationResult(m_editorOps->DeleteAt(*coordinate));
	}
}

void CSBattlePreviewPage::OnContextMenu(CWnd*, CPoint)
{
	// Suppress MFC's dock-pane menu on the S canvas. Right-click is reserved
	// for placement cancellation and the explicit unit actions above.
}

void CSBattlePreviewPage::OnKeyDown(UINT character, UINT repeat_count,
	UINT flags)
{
	if (character == VK_ESCAPE && m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition) {
		ShowOperationResult(m_editorOps->CancelPositionAdjustment());
		return;
	}
	CWnd::OnKeyDown(character, repeat_count, flags);
}

void CSBattlePreviewPage::OnCaptureChanged(CWnd* window)
{
	if (m_leftStampActive) {
		m_leftStampActive = false;
		m_lastStampCell.reset();
	}
	if (m_panActive) {
		m_panActive = false;
		m_panStartPoint = CPoint(-1, -1);
	}
	CWnd::OnCaptureChanged(window);
}

void CSBattlePreviewPage::OnSearchChanged()
{
	if (m_restoringSessionUi || !m_editorOps) return;
	CString query;
	m_searchEdit.GetWindowText(query);
	const auto result = m_editorOps->SetCharacterSearch(
		std::wstring(query.GetString()));
	ShowOperationResult(result);
}

void CSBattlePreviewPage::OnCharacterSelectionChanged()
{
	if (!m_editorOps) return;
	const int selected = m_characterList.GetCurSel();
	if (selected == LB_ERR) return;
	const int dataId = static_cast<int>(m_characterList.GetItemData(selected));
	const auto result = m_brushTabActive
		? m_editorOps->SelectMinionBrush(dataId)
		: m_editorOps->SelectCharacter(dataId);
	ShowOperationResult(result);
}

void CSBattlePreviewPage::OnDeploymentSelectionChanged()
{
	BindSelectedDeployment();
}

void CSBattlePreviewPage::OnCharacterTab()
{
	m_brushTabActive = false;
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnBrushTab()
{
	m_brushTabActive = true;
	RefreshBrushList();
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnPreviousCharacterPage()
{
	if (!m_editorOps || m_brushTabActive ||
		ActiveSession().editor_state.characters.current_page == 0) return;
	ShowOperationResult(m_editorOps->SetCharacterPage(
		ActiveSession().editor_state.characters.current_page - 1));
}

void CSBattlePreviewPage::OnNextCharacterPage()
{
	if (!m_editorOps || m_brushTabActive) return;
	ShowOperationResult(m_editorOps->SetCharacterPage(
		ActiveSession().editor_state.characters.current_page + 1));
}

void CSBattlePreviewPage::OnUndo()
{
	if (ActiveSession().native_writeback_transaction.undo_available()) {
		CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
		CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
		CcczEditor2View* view = child != nullptr
			? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
		std::wstring diagnostic;
		if (view == nullptr) {
			diagnostic = L"S 战场撤回拒绝：没有活动的原生剧本文档";
		} else {
			UndoNativeWriteback(*view, diagnostic);
		}
		AppendDiagnostic(diagnostic);
		RefreshEditorControls();
		Invalidate(FALSE);
		return;
	}
	if (m_editorOps) ShowOperationResult(m_editorOps->Undo());
}

void CSBattlePreviewPage::OnQuickModeClicked()
{
	auto* host = static_cast<CScenePreviewWnd*>(GetParent());
	const bool requested = m_quickModeButton.GetCheck() == BST_CHECKED;
	if (host == nullptr || !host->SetBattleQuickModeEnabled(requested)) {
		m_quickModeButton.SetCheck(ActiveSession().ui.quick_mode_enabled
			? BST_CHECKED : BST_UNCHECKED);
	}
}

void CSBattlePreviewPage::OnWriteback()
{
	std::wstring diagnostic;
	CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	CMDIChildWnd* child = frame != nullptr ? frame->MDIGetActive() : nullptr;
	CcczEditor2View* view = child != nullptr
		? DYNAMIC_DOWNCAST(CcczEditor2View, child->GetActiveView()) : nullptr;
	if (view == nullptr) {
		diagnostic = L"S 战场写回拒绝：没有活动的原生剧本文档";
	} else if (ApplyPendingNativeWritebackForCurrentSection(*view, diagnostic)) {
		CString status = L"布阵改动已写入编辑器内存中的剧本树；请使用编辑器正常保存以写入磁盘。\n";
		status += diagnostic.c_str();
		m_diagnostic = status;
		diagnostic.clear();
	}
	if (!diagnostic.empty()) AppendDiagnostic(diagnostic);
	RefreshEditorControls();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnHideSidebar()
{
	SetSidebarVisible(false);
}

void CSBattlePreviewPage::OnShowSidebar()
{
	SetSidebarVisible(true);
}

void CSBattlePreviewPage::OnMButtonDown(UINT, CPoint point)
{
	SetFocus();
	m_panActive = true;
	m_panStartPoint = point;
	m_panStartScrollX = m_scrollX;
	m_panStartScrollY = m_scrollY;
	SetCapture();
	SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
}

void CSBattlePreviewPage::OnMButtonUp(UINT, CPoint)
{
	if (!m_panActive) return;
	m_panActive = false;
	m_panStartPoint = CPoint(-1, -1);
	ReleaseCapture();
	SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void CSBattlePreviewPage::OnMouseMove(UINT, CPoint point)
{
	if (m_panActive) {
		SetViewportScroll(m_panStartScrollX -
			(point.x - m_panStartPoint.x), m_panStartScrollY -
			(point.y - m_panStartPoint.y));
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
		return;
	}
	const auto cell = BattleCoordFromScreen(point);
	if (m_leftStampActive && cell) StampAt(*cell);
	if (cell != m_hoverCell) {
		m_hoverCell = cell;
		Invalidate(FALSE);
	}
	SetCursor(cell ? LoadCursor(nullptr, IDC_HAND)
		: LoadCursor(nullptr, IDC_ARROW));
}

BOOL CSBattlePreviewPage::OnMouseWheel(
	UINT flags, short delta, CPoint screenPoint)
{
	CPoint point = screenPoint;
	ScreenToClient(&point);
	if (flags & MK_CONTROL) {
		if (m_canvasRect.PtInRect(point))
			ZoomAt(point, m_zoom + (delta > 0 ? kZoomStep : -kZoomStep));
		m_wheelRemainder = 0;
		return TRUE;
	}
	const bool placing = ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::Place;
	const bool adjustingPosition = ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::AdjustPosition;
	if (!m_canvasRect.PtInRect(point) || !m_editorOps ||
		(!placing && !adjustingPosition)) {
		m_wheelRemainder = 0;
		return TRUE;
	}
	// Accumulate partial wheel deltas so precision touchpads still produce one
	// direction step per standard wheel notch.
	m_wheelRemainder += delta;
	const int notches = m_wheelRemainder / WHEEL_DELTA;
	if (notches != 0) {
		m_wheelRemainder -= notches * WHEEL_DELTA;
		ShowOperationResult(m_editorOps->CycleDirection(notches));
	}
	return TRUE;
}

void CSBattlePreviewPage::OnHScroll(UINT code, UINT position, CScrollBar*)
{
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &info);
	int next = info.nPos;
	const int line = (std::max)(1, static_cast<int>(
		std::lround(kCellPixels * m_zoom)));
	const int page = (std::max)(1, static_cast<int>(info.nPage));
	switch (code) {
	case SB_LINELEFT: next -= line; break;
	case SB_LINERIGHT: next += line; break;
	case SB_PAGELEFT: next -= page; break;
	case SB_PAGERIGHT: next += page; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: next = static_cast<int>(position); break;
	case SB_TOP: next = info.nMin; break;
	case SB_BOTTOM: next = info.nMax; break;
	default: return;
	}
	SetViewportScroll(next, m_scrollY);
}

void CSBattlePreviewPage::OnVScroll(UINT code, UINT position, CScrollBar*)
{
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	GetScrollInfo(SB_VERT, &info);
	int next = info.nPos;
	const int line = (std::max)(1, static_cast<int>(
		std::lround(kCellPixels * m_zoom)));
	const int page = (std::max)(1, static_cast<int>(info.nPage));
	switch (code) {
	case SB_LINEUP: next -= line; break;
	case SB_LINEDOWN: next += line; break;
	case SB_PAGEUP: next -= page; break;
	case SB_PAGEDOWN: next += page; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: next = static_cast<int>(position); break;
	case SB_TOP: next = info.nMin; break;
	case SB_BOTTOM: next = info.nMax; break;
	default: return;
	}
	SetViewportScroll(m_scrollX, next);
}
