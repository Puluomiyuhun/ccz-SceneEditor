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
#include "PreviewIntegration/BattleBatchParameterSelection.h"
#include "PreviewIntegration/E5ImageLoader.h"
#include "PreviewIntegration/EditorTreeSnapshot.h"
#include "resource.h"
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include "PreviewIntegration/BattleWritebackQaGate.h"
#endif

#include <ccz/preview/battle_hexzmap.h>
#include <ccz/preview/battle_instruction_parser.h>
#include <ccz/preview/portrait_resolver.h>
#include <ccz/preview/script_variable_index.h>
#include <ccz/preview/variable_control_state.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstring>
#include <cwctype>
#include <limits>
#include <objidl.h>
#if defined(_DEBUG) && defined(CCZ_S_BATTLE_PREVIEW_PERF)
#include <filesystem>
#include <fstream>
#endif
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
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
constexpr int kStatusHeaderHeight = 60;
constexpr int kCollapsedControlToolbarHeight = 28;
constexpr int kStatusFooterHeight = 26;
constexpr UINT kCharacterTabId = 0xA100;
constexpr UINT kBrushTabId = 0xA101;
constexpr UINT kHideSidebarId = 0xA102;
constexpr UINT kShowSidebarId = 0xA103;
constexpr UINT kPreviousPageId = 0xA104;
constexpr UINT kNextPageId = 0xA105;
constexpr UINT kUndoId = 0xA106;
constexpr UINT kQuickModeId = 0xA10C;
constexpr UINT kConditionTreeId = 0xA10D;
constexpr UINT kCopyPersonModeId = 0xA10E;
constexpr UINT kUnrestrictedTerrainModeId = 0xA10F;
constexpr UINT kActivationButtonId = 0xA110;
constexpr UINT kVariableControlButtonId = 0xA111;
constexpr UINT kPreviewModeButtonId = 0xA112;
constexpr UINT kUnitParametersButtonId = 0xA113;
constexpr UINT kPreviewModeMenuFirstId = 0xA120;
constexpr UINT kConditionActionMessage = WM_APP + 0x38A;
constexpr UINT kSearchId = 0xA107;
constexpr UINT kDeploymentComboId = 0xA108;
constexpr UINT kCharacterListId = 0xA109;
constexpr UINT kPageLabelId = 0xA10A;
constexpr UINT kWritebackId = 0xA10B;

#pragma comment(lib, "ole32.lib")

const wchar_t* preview_mode_label(
	ccz::editor_preview::BattleParameterPreviewMode mode)
{
	using Mode = ccz::editor_preview::BattleParameterPreviewMode;
	switch (mode) {
	case Mode::BattlefieldIndex: return L"战场编号";
	case Mode::Level: return L"等级";
	case Mode::UnitType: return L"兵种级别";
	case Mode::AiPolicy: return L"行动方针";
	case Mode::Reinforcement: return L"援军";
	case Mode::None: default: return L"无";
	}
}

std::wstring battle_mod_root(const std::wstring& path)
{
	for (std::size_t index = path.size(); index-- > 0;) {
		if (path[index] != L'\\' && path[index] != L'/') continue;
		if (index >= 2 &&
			(path[index - 1] == L'S' || path[index - 1] == L's') &&
			(path[index - 2] == L'R' || path[index - 2] == L'r')) continue;
		return path.substr(0, index + 1);
	}
	return {};
}

std::uint64_t preview_asset_revision(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
		return 0;
	const std::uint64_t size =
		(static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
		attributes.nFileSizeLow;
	const std::uint64_t time =
		(static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
		attributes.ftLastWriteTime.dwLowDateTime;
	return size ^ (time + 0x9E3779B97F4A7C15ull + (size << 6) + (size >> 2));
}

Gdiplus::Bitmap* decode_portrait_bitmap(
	const std::vector<std::uint8_t>& encoded)
{
	if (encoded.empty()) return nullptr;
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, encoded.size());
	if (memory == nullptr) return nullptr;
	void* destination = GlobalLock(memory);
	if (destination == nullptr) {
		GlobalFree(memory);
		return nullptr;
	}
	std::memcpy(destination, encoded.data(), encoded.size());
	GlobalUnlock(memory);
	IStream* stream = nullptr;
	if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
		GlobalFree(memory);
		return nullptr;
	}
	Gdiplus::Bitmap source(stream);
	Gdiplus::Bitmap* decoded = nullptr;
	if (source.GetLastStatus() == Gdiplus::Ok && source.GetWidth() > 0 &&
		source.GetHeight() > 0) {
		decoded = source.Clone(Gdiplus::Rect(0, 0,
			static_cast<INT>(source.GetWidth()),
			static_cast<INT>(source.GetHeight())), PixelFormat32bppARGB);
		if (decoded != nullptr && decoded->GetLastStatus() != Gdiplus::Ok) {
			delete decoded;
			decoded = nullptr;
		}
	}
	stream->Release();
	return decoded;
}

const ccz::preview::DialogueProfile& battle_portrait_profile()
{
	static const ccz::preview::DialogueProfile profile = [] {
		ccz::preview::DialogueProfile value;
		value.architecture = "x86";
		value.data_record_base = 0x18c;
		value.data_record_stride = 0x20;
		value.avatar_offset = 0x0d;
		value.avatar_width = 2;
		value.r_image_offset = 0x0f;
		value.r_image_width = 2;
		value.face_index_table_offset = 0x110;
		value.face_entry_size = 12;
		value.face_index_base = 0;
		value.face_index_bias = 7;
		return value;
	}();
	return profile;
}

Gdiplus::Color battle_faction_color(std::uint16_t instruction,
	BYTE alpha = 255)
{
	if (instruction == 0x4b) return Gdiplus::Color(alpha, 255, 80, 80);
	if (instruction == 0x47) return Gdiplus::Color(alpha, 100, 165, 255);
	if (instruction == 0x46) return Gdiplus::Color(alpha, 255, 165, 55);
	return Gdiplus::Color(alpha, 210, 210, 210);
}

const wchar_t* battle_unit_type_name(int unit_type)
{
	static constexpr const wchar_t* names[] = {L"初级", L"中级", L"高级"};
	return unit_type >= 0 && unit_type < static_cast<int>(std::size(names))
		? names[unit_type] : L"未知";
}

const wchar_t* battle_ai_policy_name(int policy)
{
	static constexpr const wchar_t* names[] = {
		L"被动出击", L"主动出击", L"坚守原地", L"攻击武将",
		L"到指定点", L"跟随武将", L"逃到指定点"};
	return policy >= 0 && policy < static_cast<int>(std::size(names))
		? names[policy] : L"未指定";
}

bool unit_has_battle_role(const ccz::preview::BattleUnit& unit,
	ccz::preview::BattleFieldRole role)
{
	const std::uint16_t instruction = unit.editor_deployment_instruction != 0xffff
		? unit.editor_deployment_instruction : unit.source_instruction;
	const auto* spec = ccz::preview::get_battle_field_spec(
		instruction);
	return spec != nullptr && spec->semantic_complete &&
		std::find(spec->field_roles.begin(), spec->field_roles.end(), role) !=
			spec->field_roles.end();
}

std::wstring differing_batch_parameter_names(
	const ccz::editor_preview::BattleEditorState& state,
	const std::vector<std::uint64_t>& cache_ids,
	ccz::editor_preview::BattleBatchParameterMask& supported,
	ccz::editor_preview::BattleBatchParameterMask& differing_mask)
{
	using ccz::editor_preview::BattleBatchParameter;
	using ccz::editor_preview::batch_parameter_bit;
	using ccz::preview::BattleFieldRole;
	supported = 0;
	differing_mask = 0;
	std::vector<const ccz::preview::BattleUnit*> units;
	units.reserve(cache_ids.size());
	for (const std::uint64_t cache_id : cache_ids) {
		const auto found = std::find_if(state.battle.units.begin(),
			state.battle.units.end(), [cache_id](const auto& unit) {
				return unit.editor_cache_id == cache_id && unit.fields_decoded &&
					!unit.editor_pending_delete &&
					!ccz::editor_preview::IsBattleFixedEmptySlot(unit);
			});
		if (found == state.battle.units.end()) return L"目标单位已失效";
		units.push_back(&*found);
	}
	if (units.size() < 2) return {};
	const auto has_role = [&units](BattleFieldRole role) {
		return std::all_of(units.begin(), units.end(), [role](const auto* unit) {
			return unit_has_battle_role(*unit, role);
		});
	};
	std::vector<std::wstring> differing;
	const auto append_if_different = [&units, &differing, &supported,
		&differing_mask](BattleBatchParameter parameter, const wchar_t* name,
		auto get_value) {
		supported |= batch_parameter_bit(parameter);
		const auto first = get_value(*units.front());
		if (std::any_of(units.begin() + 1, units.end(),
			[&get_value, &first](const auto* unit) {
				return get_value(*unit) != first;
			})) {
			differing_mask |= batch_parameter_bit(parameter);
			differing.emplace_back(name);
		}
	};
	if (has_role(BattleFieldRole::Direction))
		append_if_different(BattleBatchParameter::Direction, L"朝向",
			[](const auto& unit) { return unit.direction; });
	if (has_role(BattleFieldRole::Hidden))
		append_if_different(BattleBatchParameter::Hidden, L"隐藏",
			[](const auto& unit) { return unit.hidden; });
	if (has_role(BattleFieldRole::Level))
		append_if_different(BattleBatchParameter::Level, L"等级偏移",
			[](const auto& unit) { return unit.level; });
	if (has_role(BattleFieldRole::UnitType))
		append_if_different(BattleBatchParameter::UnitType, L"兵种级别",
			[](const auto& unit) { return unit.unit_type; });
	if (has_role(BattleFieldRole::Reinforcement))
		append_if_different(BattleBatchParameter::Reinforcement, L"援军",
			[](const auto& unit) { return unit.reinforcement; });
	if (has_role(BattleFieldRole::AiPolicy))
		append_if_different(BattleBatchParameter::AiPolicy, L"行动方针",
			[](const auto& unit) { return unit.ai_policy; });
	const bool has_target_data = has_role(BattleFieldRole::TargetDataId);
	const bool has_target_coordinates = has_role(BattleFieldRole::TargetX) &&
		has_role(BattleFieldRole::TargetY);
	if (has_role(BattleFieldRole::AiPolicy) &&
		(has_target_data || has_target_coordinates)) {
		supported |= batch_parameter_bit(BattleBatchParameter::Target);
		const auto* first = units.front();
		const bool target_differs = std::any_of(units.begin() + 1, units.end(),
			[first, has_target_data, has_target_coordinates](const auto* unit) {
				return (has_target_data && unit->target != first->target) ||
					(has_target_coordinates &&
						(unit->target_x != first->target_x ||
						 unit->target_y != first->target_y));
			});
		if (target_differs) {
			differing_mask |= batch_parameter_bit(BattleBatchParameter::Target);
			differing.emplace_back(L"目标");
		}
	}
	std::wstring result;
	for (const auto& name : differing) {
		if (!result.empty()) result += L"、";
		result += name;
	}
	return result;
}

class CBatchParameterWarningDialog : public CDialog
{
public:
	explicit CBatchParameterWarningDialog(std::wstring differences)
		: CDialog(IDD_BATTLE_BATCH_WARNING),
		  m_differences(std::move(differences)) {}

protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(L"批量修改参数");
		CString message;
		message.Format(L"所选单位的以下参数原值不一致：%s。\n覆盖：所有可用参数保持勾选；仅修改统一项：取消勾选存在差异的参数；取消：不修改。",
			m_differences.c_str());
		if (CWnd* text = GetDlgItem(IDC_BATTLE_BATCH_WARNING_TEXT))
			text->SetWindowText(message);
		if (CWnd* safeDefault = GetDlgItem(IDC_BATTLE_BATCH_UNIFORM))
			safeDefault->SetFocus();
		return FALSE;
	}
	afx_msg void OnOverwrite();
	afx_msg void OnUniform();
	afx_msg void OnCancelClicked();
	DECLARE_MESSAGE_MAP()

private:
	std::wstring m_differences;
};

BEGIN_MESSAGE_MAP(CBatchParameterWarningDialog, CDialog)
	ON_BN_CLICKED(IDC_BATTLE_BATCH_OVERWRITE,
		&CBatchParameterWarningDialog::OnOverwrite)
	ON_BN_CLICKED(IDC_BATTLE_BATCH_UNIFORM,
		&CBatchParameterWarningDialog::OnUniform)
	ON_BN_CLICKED(IDCANCEL,
		&CBatchParameterWarningDialog::OnCancelClicked)
END_MESSAGE_MAP()

void CBatchParameterWarningDialog::OnOverwrite()
{
	EndDialog(IDC_BATTLE_BATCH_OVERWRITE);
}

void CBatchParameterWarningDialog::OnUniform()
{
	EndDialog(IDC_BATTLE_BATCH_UNIFORM);
}

void CBatchParameterWarningDialog::OnCancelClicked()
{
	EndDialog(IDCANCEL);
}
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
			if (data->long_char_data != nullptr)
				node.long_text.assign(data->long_char_data);
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

	bool InsertBattleFourBGroup(std::uintptr_t parent_handle,
		std::uintptr_t after_handle, const std::wstring& label,
		const std::vector<std::vector<std::int32_t>>& records,
		int first_ordinal, std::uintptr_t& marker_handle,
		std::uintptr_t& group_handle,
		std::vector<std::uintptr_t>& record_handles,
		std::uintptr_t& terminal_handle) override
	{
		return CreateBattleFourBGroup(parent_handle, after_handle, label,
			records, first_ordinal, true, marker_handle, group_handle,
			record_handles, terminal_handle);
	}

	bool RestoreBattleFourBGroupBefore(std::uintptr_t parent_handle,
		std::uintptr_t after_handle, std::uintptr_t before_handle,
		const std::wstring& label,
		const std::vector<std::vector<std::int32_t>>& records,
		int first_ordinal, std::uintptr_t& marker_handle,
		std::uintptr_t& group_handle,
		std::vector<std::uintptr_t>& record_handles,
		std::uintptr_t& terminal_handle) override
	{
		marker_handle = group_handle = terminal_handle = 0;
		record_handles.clear();
		if (before_handle == 0 || !NodeExists(before_handle) ||
			!NodeExists(parent_handle)) return false;
		const HTREEITEM parent = reinterpret_cast<HTREEITEM>(parent_handle);
		const HTREEITEM before = reinterpret_cast<HTREEITEM>(before_handle);
		if (tree_.GetParentItem(before) != parent) return false;
		HTREEITEM cursor = nullptr;
		if (after_handle == 0) {
			cursor = tree_.GetChildItem(parent);
		} else {
			if (!NodeExists(after_handle) ||
				tree_.GetParentItem(reinterpret_cast<HTREEITEM>(after_handle)) != parent)
				return false;
			cursor = tree_.GetNextSiblingItem(
				reinterpret_cast<HTREEITEM>(after_handle));
		}
		bool before_follows = false;
		for (; cursor != nullptr; cursor = tree_.GetNextSiblingItem(cursor)) {
			if (cursor == before) { before_follows = true; break; }
		}
		if (!before_follows) return false;
		return CreateBattleFourBGroup(parent_handle, after_handle, label,
			records, first_ordinal, false, marker_handle, group_handle,
			record_handles, terminal_handle);
	}

	bool DeleteBattleFourBGroup(std::uintptr_t marker_handle,
		std::uintptr_t group_handle) override
	{
		if (!NodeExists(marker_handle) || !NodeExists(group_handle)) return false;
		const HTREEITEM marker = reinterpret_cast<HTREEITEM>(marker_handle);
		const HTREEITEM group = reinterpret_cast<HTREEITEM>(group_handle);
		const HTREEITEM parent = tree_.GetParentItem(marker);
		auto* marker_data = reinterpret_cast<ItemData*>(tree_.GetItemData(marker));
		auto* group_data = reinterpret_cast<ItemData*>(tree_.GetItemData(group));
		if (parent == nullptr || tree_.GetParentItem(group) != parent ||
			marker_data == nullptr || group_data == nullptr ||
			marker_data->id != 1 || group_data->id != 2 ||
			group_data->long_char_data == nullptr ||
			std::wstring(group_data->long_char_data) != L"我军出场" ||
			tree_.GetNextSiblingItem(marker) != group) return false;

		std::vector<ItemData*> owned_data;
		try {
			const auto collect = [&](const auto& self, HTREEITEM item) -> bool {
				for (HTREEITEM child = tree_.GetChildItem(item); child != nullptr;
					child = tree_.GetNextSiblingItem(child)) {
					auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(child));
					if (data == nullptr) return false;
					owned_data.push_back(data);
					if (!self(self, child)) return false;
				}
				return true;
			};
			if (!collect(collect, group)) return false;
			HTREEITEM last_child = tree_.GetChildItem(group);
			if (last_child == nullptr) return false;
			for (HTREEITEM child = tree_.GetNextSiblingItem(last_child);
				child != nullptr; child = tree_.GetNextSiblingItem(child))
				last_child = child;
			auto* terminal_data = reinterpret_cast<ItemData*>(
				tree_.GetItemData(last_child));
			if (terminal_data == nullptr || terminal_data->id != 0 ||
				tree_.GetChildItem(last_child) != nullptr) return false;
			for (HTREEITEM child = tree_.GetChildItem(group);
				child != last_child; child = tree_.GetNextSiblingItem(child)) {
				auto* data = reinterpret_cast<ItemData*>(tree_.GetItemData(child));
				if (data == nullptr || data->id != 0x4b ||
					data->int_data.size() != 20 ||
					tree_.GetChildItem(child) != nullptr) return false;
			}
			owned_data.push_back(group_data);
			owned_data.push_back(marker_data);
		} catch (...) {
			return false;
		}

		if (!tree_.DeleteItem(group)) return false;
		if (!tree_.DeleteItem(marker)) return false;
		for (ItemData* data : owned_data) {
			ccz::editor::ReleaseLongCharData(
				data->long_char_data, data->long_char_data_capacity);
			delete data;
		}
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
	bool CreateBattleFourBGroup(std::uintptr_t parent_handle,
		std::uintptr_t after_handle, const std::wstring& label,
		const std::vector<std::vector<std::int32_t>>& records,
		int first_ordinal, bool advance_document_ordinal,
		std::uintptr_t& marker_handle, std::uintptr_t& group_handle,
		std::vector<std::uintptr_t>& record_handles,
		std::uintptr_t& terminal_handle)
	{
		marker_handle = group_handle = terminal_handle = 0;
		record_handles.clear();
		if (label != L"我军出场" || records.empty() ||
			first_ordinal < 0 ||
			first_ordinal > (std::numeric_limits<int>::max)() - 3 ||
			records.size() > static_cast<std::size_t>(
				(std::numeric_limits<int>::max)() - first_ordinal - 3) ||
			!NodeExists(parent_handle) ||
			(advance_document_ordinal && view_.cur_code_ord != first_ordinal))
			return false;
		for (const auto& record : records)
			if (record.size() != 20U) return false;

		const HTREEITEM parent = reinterpret_cast<HTREEITEM>(parent_handle);
		HTREEITEM after = TVI_FIRST;
		if (after_handle != 0) {
			if (!NodeExists(after_handle) ||
				tree_.GetParentItem(reinterpret_cast<HTREEITEM>(after_handle)) != parent)
				return false;
			after = reinterpret_cast<HTREEITEM>(after_handle);
		}
		const HTREEITEM original_selection = tree_.GetSelectedItem();
		const int original_next_ordinal = view_.cur_code_ord;
		std::vector<ItemData*> created_data;
		try {
			created_data.reserve(records.size() + 3U);
			record_handles.reserve(records.size());
		} catch (...) {
			return false;
		}

		const auto create_item = [&](int id, HTREEITEM item_parent,
			HTREEITEM insert_after, int ordinal,
			const std::vector<std::int32_t>* int_data,
			const std::wstring* long_text) -> HTREEITEM {
			const int saved_ordinal = view_.cur_code_ord;
			if (advance_document_ordinal) {
				if (saved_ordinal != ordinal) return nullptr;
			} else {
				view_.cur_code_ord = ordinal;
			}
			ItemData* raw_data = nullptr;
			try {
				raw_data = view_.InitData(id);
			} catch (...) {
				view_.cur_code_ord = saved_ordinal;
				return nullptr;
			}
			if (raw_data == nullptr) {
				view_.cur_code_ord = saved_ordinal;
				return nullptr;
			}
			if (!advance_document_ordinal) view_.cur_code_ord = saved_ordinal;
			raw_data->ord = ordinal;
			if (int_data != nullptr) {
				if (raw_data->int_data.size() != int_data->size()) {
					ccz::editor::ReleaseLongCharData(
						raw_data->long_char_data,
						raw_data->long_char_data_capacity);
					delete raw_data;
					if (advance_document_ordinal) view_.cur_code_ord = saved_ordinal;
					return nullptr;
				}
				std::copy(int_data->begin(), int_data->end(),
					raw_data->int_data.begin());
			}
			if (long_text != nullptr &&
				!ccz::editor::ReplaceLongCharData(
					raw_data->long_char_data,
					raw_data->long_char_data_capacity,
					long_text->c_str(),
					ccz::editor::kLongCharDataCapacity)) {
				ccz::editor::ReleaseLongCharData(
					raw_data->long_char_data,
					raw_data->long_char_data_capacity);
				delete raw_data;
				if (advance_document_ordinal) view_.cur_code_ord = saved_ordinal;
				return nullptr;
			}
			TVINSERTSTRUCT insert{};
			insert.hParent = item_parent;
			insert.hInsertAfter = insert_after;
			insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE |
				TVIF_SELECTEDIMAGE;
			insert.item.pszText = view_.code[id];
			insert.item.lParam = 0;
			insert.item.iImage = 0;
			insert.item.iSelectedImage = 1;
			const HTREEITEM item = tree_.InsertItem(&insert);
			if (item == nullptr || !tree_.SetItemData(item,
				reinterpret_cast<DWORD_PTR>(raw_data))) {
				if (item != nullptr) tree_.DeleteItem(item);
				ccz::editor::ReleaseLongCharData(
					raw_data->long_char_data,
					raw_data->long_char_data_capacity);
				delete raw_data;
				if (advance_document_ordinal) view_.cur_code_ord = saved_ordinal;
				return nullptr;
			}
			try {
				view_.UpdateShow(item);
			} catch (...) {
				tree_.DeleteItem(item);
				ccz::editor::ReleaseLongCharData(
					raw_data->long_char_data,
					raw_data->long_char_data_capacity);
				delete raw_data;
				if (advance_document_ordinal) view_.cur_code_ord = saved_ordinal;
				return nullptr;
			}
			created_data.push_back(raw_data);
			return item;
		};

		const auto rollback_created = [&]() {
			if (group_handle != 0 && NodeExists(group_handle))
				tree_.DeleteItem(reinterpret_cast<HTREEITEM>(group_handle));
			if (marker_handle != 0 && NodeExists(marker_handle))
				tree_.DeleteItem(reinterpret_cast<HTREEITEM>(marker_handle));
			for (ItemData* data : created_data) {
				ccz::editor::ReleaseLongCharData(
					data->long_char_data, data->long_char_data_capacity);
				delete data;
			}
			view_.cur_code_ord = original_next_ordinal;
			if (original_selection != nullptr &&
				TreeItemExists(reinterpret_cast<std::uintptr_t>(original_selection)))
				tree_.SelectItem(original_selection);
			marker_handle = group_handle = terminal_handle = 0;
			record_handles.clear();
		};

		const int first_child_ordinal = first_ordinal + 2;
		const std::wstring group_text = L"我军出场";
		HTREEITEM marker = create_item(1, parent, after,
			first_ordinal, nullptr, nullptr);
		if (marker == nullptr) { rollback_created(); return false; }
		marker_handle = reinterpret_cast<std::uintptr_t>(marker);
		HTREEITEM group = create_item(2, parent, marker,
			first_ordinal + 1, nullptr, &group_text);
		if (group == nullptr) { rollback_created(); return false; }
		group_handle = reinterpret_cast<std::uintptr_t>(group);
		HTREEITEM terminal = create_item(0, group, TVI_LAST,
			first_child_ordinal, nullptr, nullptr);
		if (terminal == nullptr) { rollback_created(); return false; }
		terminal_handle = reinterpret_cast<std::uintptr_t>(terminal);
		std::uintptr_t previous_record = 0;
		for (std::size_t index = 0; index < records.size(); ++index) {
			const int ordinal = first_ordinal + 3 + static_cast<int>(index);
			const HTREEITEM record = create_item(0x4b, group,
				previous_record == 0 ? TVI_FIRST
					: reinterpret_cast<HTREEITEM>(previous_record),
				ordinal, &records[index], nullptr);
			if (record == nullptr) { rollback_created(); return false; }
			previous_record = reinterpret_cast<std::uintptr_t>(record);
			record_handles.push_back(previous_record);
		}
		if (!advance_document_ordinal) view_.cur_code_ord = original_next_ordinal;
		if (!record_handles.empty() && first_inserted_handle_ == 0)
			first_inserted_handle_ = record_handles.front();
		return true;
	}

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

std::uint16_t battle_unit_deployment(const ccz::preview::BattleUnit& unit)
{
	return unit.editor_deployment_instruction != 0xffff
		? unit.editor_deployment_instruction : unit.source_instruction;
}

bool battle_unit_in_active_scope(
	const ccz::editor_preview::BattleEditorState& state,
	const ccz::preview::BattleUnit& unit)
{
	return unit.fields_decoded &&
		(unit.editor_staged_new || in_scope(unit.source_key, state.scope_root));
}

int predicted_battlefield_index(
	const ccz::editor_preview::BattleEditorState& state,
	const ccz::preview::BattleUnit& target)
{
	const std::uint16_t instruction = battle_unit_deployment(target);
	if (instruction != 0x4b && instruction != 0x46 && instruction != 0x47)
		return -1;

	if (instruction == 0x4b) {
		const bool layout_changes = std::any_of(
			state.battle.units.begin(), state.battle.units.end(),
			[&state](const ccz::preview::BattleUnit& unit) {
				return battle_unit_in_active_scope(state, unit) &&
					((unit.source_instruction == 0x4b &&
						unit.editor_pending_delete) ||
						(unit.editor_staged_new &&
						battle_unit_deployment(unit) == 0x4b));
			});
		int next = 0;
		for (const auto& unit : state.battle.units) {
			if (!battle_unit_in_active_scope(state, unit) ||
				unit.editor_staged_new || unit.editor_pending_delete ||
				unit.source_instruction != 0x4b ||
				ccz::editor_preview::IsBattleFixedEmptySlot(unit)) continue;
			if (unit.editor_cache_id == target.editor_cache_id)
				return layout_changes ? next : unit.battlefield_index;
			++next;
		}
		for (const auto& unit : state.battle.units) {
			if (!battle_unit_in_active_scope(state, unit) ||
				!unit.editor_staged_new || unit.editor_pending_delete ||
				battle_unit_deployment(unit) != 0x4b) continue;
			if (unit.editor_cache_id == target.editor_cache_id) return next;
			++next;
		}
		return target.battlefield_index;
	}

	if (!target.editor_staged_new) return target.battlefield_index;
	std::vector<int> available_slots;
	for (const auto& unit : state.battle.units) {
		if (!battle_unit_in_active_scope(state, unit) || unit.editor_staged_new ||
			unit.source_instruction != instruction ||
			(!unit.editor_pending_delete &&
				!ccz::editor_preview::IsBattleFixedEmptySlot(unit))) continue;
		if (unit.battlefield_index >= 0)
			available_slots.push_back(unit.battlefield_index);
	}
	std::size_t next_slot = 0;
	for (const auto& unit : state.battle.units) {
		if (!battle_unit_in_active_scope(state, unit) || !unit.editor_staged_new ||
			unit.editor_pending_delete || battle_unit_deployment(unit) != instruction)
			continue;
		if (unit.editor_cache_id == target.editor_cache_id) {
			return next_slot < available_slots.size()
				? available_slots[next_slot] : -1;
		}
		++next_slot;
	}
	return -1;
}

struct BattleUnitStatusLabelParts {
	std::wstring identity;
	std::wstring body;
};

BattleUnitStatusLabelParts own_battle_unit_status_label(int battlefield_index)
{
	return { L"【我军】",
		(battlefield_index >= 0
			? std::to_wstring(battlefield_index) : std::wstring(L"待分配")) +
			L".将领" };
}

const ccz::preview::ExecutionNode* find_execution_node_by_runtime_handle(
	const std::vector<ccz::preview::ExecutionNode>& nodes,
	std::uintptr_t runtime_handle)
{
	if (runtime_handle == 0) return nullptr;
	for (const auto& node : nodes) {
		if (node.runtime_handle == runtime_handle) return &node;
		for (const auto* children : {&node.ordinary_children,
			&node.true_children, &node.false_children, &node.cases}) {
			if (const auto* found = find_execution_node_by_runtime_handle(
					*children, runtime_handle)) return found;
		}
	}
	return nullptr;
}

const ccz::preview::ExecutionNode* find_execution_node_by_runtime_handle(
	const ccz::preview::SectionExecutionModel& model,
	std::uintptr_t runtime_handle)
{
	if (const auto* found = find_execution_node_by_runtime_handle(
			model.entry_gates, runtime_handle)) return found;
	return find_execution_node_by_runtime_handle(model.body, runtime_handle);
}

}

void CBattlePrimaryActionButton::DrawItem(LPDRAWITEMSTRUCT draw_item)
{
	if (draw_item == nullptr || draw_item->hDC == nullptr) return;
	CDC dc;
	dc.Attach(draw_item->hDC);
	CRect bounds(draw_item->rcItem);
	const bool disabled = (draw_item->itemState & ODS_DISABLED) != 0;
	const bool pressed = (draw_item->itemState & ODS_SELECTED) != 0;
	const COLORREF face = disabled ? RGB(92, 82, 75)
		: pressed ? RGB(151, 61, 20) : RGB(194, 87, 33);
	dc.FillSolidRect(&bounds, face);
	dc.Draw3dRect(&bounds, RGB(255, 207, 142), RGB(91, 39, 17));
	CRect accent(bounds.left + 2, bounds.top + 2,
		bounds.left + 5, bounds.bottom - 2);
	dc.FillSolidRect(&accent, disabled ? RGB(150, 136, 126)
		: RGB(255, 220, 154));
	CRect text_bounds(bounds);
	text_bounds.DeflateRect(7, 1, 3, 1);
	if (pressed) text_bounds.OffsetRect(1, 1);
	CString label;
	GetWindowText(label);
	const int old_background = dc.SetBkMode(TRANSPARENT);
	const COLORREF old_text = dc.SetTextColor(
		disabled ? RGB(215, 205, 198) : RGB(255, 255, 255));
	CFont* font = GetFont();
	CFont* old_font = font != nullptr ? dc.SelectObject(font) : nullptr;
	dc.DrawText(label, &text_bounds,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if ((draw_item->itemState & ODS_FOCUS) != 0) {
		CRect focus(bounds);
		focus.DeflateRect(7, 4);
		dc.DrawFocusRect(&focus);
	}
	if (old_font != nullptr) dc.SelectObject(old_font);
	dc.SetTextColor(old_text);
	dc.SetBkMode(old_background);
	dc.Detach();
}

BEGIN_MESSAGE_MAP(CBattlePreviewModeButton, CButton)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEHOVER()
	ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

void CBattlePreviewModeButton::OnMouseMove(UINT flags, CPoint point)
{
	if (!m_tracking) {
		TRACKMOUSEEVENT tracking{};
		tracking.cbSize = sizeof(tracking);
		tracking.dwFlags = TME_HOVER | TME_LEAVE;
		tracking.hwndTrack = GetSafeHwnd();
		tracking.dwHoverTime = HOVER_DEFAULT;
		m_tracking = TrackMouseEvent(&tracking) != FALSE;
	}
	CButton::OnMouseMove(flags, point);
}

void CBattlePreviewModeButton::OnMouseHover(UINT flags, CPoint point)
{
	m_tracking = false;
	if (m_hoverHandler) m_hoverHandler();
	CButton::OnMouseHover(flags, point);
}

void CBattlePreviewModeButton::OnMouseLeave()
{
	m_tracking = false;
	CButton::OnMouseLeave();
}

BEGIN_MESSAGE_MAP(CSBattlePreviewPage, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_CAPTURECHANGED()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
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
	ON_BN_CLICKED(kCopyPersonModeId,
		&CSBattlePreviewPage::OnCopyPersonModeClicked)
	ON_BN_CLICKED(kUnrestrictedTerrainModeId,
		&CSBattlePreviewPage::OnUnrestrictedTerrainModeClicked)
	ON_BN_CLICKED(kConditionTreeId, &CSBattlePreviewPage::OnConditionTreeClicked)
	ON_BN_CLICKED(kActivationButtonId,
		&CSBattlePreviewPage::OnActivationButtonClicked)
	ON_BN_CLICKED(kVariableControlButtonId,
		&CSBattlePreviewPage::OnVariableControlClicked)
	ON_BN_CLICKED(kUnitParametersButtonId,
		&CSBattlePreviewPage::OnUnitParametersClicked)
	ON_BN_CLICKED(kPreviewModeButtonId,
		&CSBattlePreviewPage::OnPreviewModeClicked)
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
		session.editor_state.movement_terrain = m_assets.movement_terrain();
		for (auto& workspace : session.condition_workspaces) {
			if (workspace.second != nullptr)
				workspace.second->state.movement_terrain =
					m_assets.movement_terrain();
		}
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
	ui.copy_person_mode = session.editor_state.copy_person_mode;
	ui.unrestricted_terrain_mode =
		session.editor_state.unrestricted_terrain_mode;
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
	session.editor_state.copy_person_mode = ui.copy_person_mode;
	session.editor_state.unrestricted_terrain_mode =
		ui.unrestricted_terrain_mode;
	m_restoringSessionUi = true;
	if (m_searchEdit.GetSafeHwnd() != nullptr)
		m_searchEdit.SetWindowText(ui.search_text.c_str());
	if (m_quickModeButton.GetSafeHwnd() != nullptr)
		m_quickModeButton.SetCheck(ui.quick_mode_enabled
			? BST_CHECKED : BST_UNCHECKED);
	if (m_copyPersonModeButton.GetSafeHwnd() != nullptr)
		m_copyPersonModeButton.SetCheck(ui.copy_person_mode
			? BST_CHECKED : BST_UNCHECKED);
	if (m_unrestrictedTerrainModeButton.GetSafeHwnd() != nullptr)
		m_unrestrictedTerrainModeButton.SetCheck(ui.unrestricted_terrain_mode
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
		RefreshCharacterList();
		RefreshDeploymentSelection();
		RefreshEditorControls();
		RefreshConditionTree();
	}
	UpdateViewport();
}

void CSBattlePreviewPage::UnbindSession()
{
	if (m_activeSession == nullptr) return;
	if ((m_panActive || m_leftStampActive || m_selectionGestureActive) &&
		GetSafeHwnd() != nullptr)
		ReleaseCapture();
	SaveActiveSessionUiState();
	ReleaseActiveBitmapPins();
	m_panActive = false;
	m_leftStampActive = false;
	m_selectionGestureActive = false;
	m_selectionDragStarted = false;
	m_selectionPressCell.reset();
	m_selectionLastCell.reset();
	m_selectionPressPoint = CPoint(-1, -1);
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
	if (m_variableControlPane.GetSafeHwnd() != nullptr) {
		m_variableControlPane.UpdateRows({}, {}, {});
		m_variableControlPane.ShowPane(false);
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
			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_VCENTER,
			CRect(0, 0, 1, 1), this, kQuickModeId) ||
		!m_copyPersonModeButton.Create(_T("复制人"),
			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_VCENTER,
			CRect(0, 0, 1, 1), this, kCopyPersonModeId) ||
		!m_unrestrictedTerrainModeButton.Create(_T("无地形限制"),
			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_VCENTER,
			CRect(0, 0, 1, 1), this, kUnrestrictedTerrainModeId) ||
		!m_conditionTreeButton.Create(_T("条件树"), buttonStyle,
			CRect(0, 0, 1, 1), this, kConditionTreeId) ||
		!m_activationButton.Create(_T("激活子事件"), buttonStyle,
			CRect(0, 0, 1, 1), this, kActivationButtonId) ||
		!m_variableControlButton.Create(_T("变量控制"), buttonStyle,
			CRect(0, 0, 1, 1), this, kVariableControlButtonId) ||
		!m_previewModeButton.Create(_T("预览切换 ▾"), buttonStyle,
			CRect(0, 0, 1, 1), this, kPreviewModeButtonId) ||
		!m_unitParametersButton.Create(_T("默认参数"), buttonStyle,
			CRect(0, 0, 1, 1), this, kUnitParametersButtonId) ||
		!m_writebackButton.Create(_T("写回"),
			WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
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
	const bool uiFontCreated = m_uiFont.CreateFont(20, 0, 0, 0,
		FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, _T("Microsoft YaHei")) != FALSE;
	const bool toolbarFontCreated = m_toolbarFont.CreateFont(16, 0, 0, 0,
		FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, _T("Microsoft YaHei")) != FALSE;
	const bool writebackFontCreated = m_writebackFont.CreateFont(20, 0, 0, 0,
		FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, _T("Microsoft YaHei")) != FALSE;
	if (uiFontCreated) {
		m_characterTabButton.SetFont(&m_uiFont);
		m_brushTabButton.SetFont(&m_uiFont);
		m_hideSidebarButton.SetFont(&m_uiFont);
		m_showSidebarButton.SetFont(&m_uiFont);
		m_previousPageButton.SetFont(&m_uiFont);
		m_nextPageButton.SetFont(&m_uiFont);
		m_undoButton.SetFont(&m_uiFont);
		m_quickModeButton.SetFont(&m_uiFont);
		m_copyPersonModeButton.SetFont(&m_uiFont);
		m_unrestrictedTerrainModeButton.SetFont(&m_uiFont);
		m_conditionTreeButton.SetFont(&m_uiFont);
		m_activationButton.SetFont(&m_uiFont);
		m_variableControlButton.SetFont(&m_uiFont);
		m_previewModeButton.SetFont(&m_uiFont);
		m_unitParametersButton.SetFont(&m_uiFont);
		m_writebackButton.SetFont(writebackFontCreated
			? &m_writebackFont : &m_uiFont);
		m_searchEdit.SetFont(&m_uiFont);
		m_deploymentCombo.SetFont(&m_uiFont);
		m_characterList.SetFont(&m_uiFont);
		m_pageLabel.SetFont(&m_uiFont);
	} else {
		AppendDiagnostic(L"S 战场控件微软雅黑字体创建失败");
	}
	if (!toolbarFontCreated)
		AppendDiagnostic(L"S 战场紧凑工具栏字体创建失败");
	if (!writebackFontCreated)
		AppendDiagnostic(L"S 战场写回按钮字体创建失败");
	m_quickModeButton.SetCheck(ActiveSession().ui.quick_mode_enabled ? BST_CHECKED : BST_UNCHECKED);
	m_copyPersonModeButton.SetCheck(ActiveSession().ui.copy_person_mode
		? BST_CHECKED : BST_UNCHECKED);
	m_unrestrictedTerrainModeButton.SetCheck(
		ActiveSession().ui.unrestricted_terrain_mode
			? BST_CHECKED : BST_UNCHECKED);
	if (!m_conditionTreePane.CreatePane(this,
		[this](const ccz::preview::LogicalNodeKey& nodeKey,
			ccz::preview::ConditionTreeAction action,
			std::int32_t integerValue) {
			ApplyConditionActionAndRefresh(nodeKey, action, integerValue);
	})) {
		AppendDiagnostic(L"S 战场条件树控件创建失败");
	}
	if (!m_variableControlPane.CreatePane(this,
		[this](ccz::preview::VariableControlKind kind, std::int32_t id,
			ccz::preview::VariableControlAction action,
			std::int32_t integerValue) {
			ApplyVariableActionAndRefresh(kind, id, action, integerValue);
		})) {
		AppendDiagnostic(L"S 战场变量控制控件创建失败");
	}
	m_previewModeButton.SetHoverHandler([this]() {
		ShowPreviewModeMenu();
	});
	if (!m_unitParametersPane.CreatePane(this,
		[this](std::uint16_t instruction,
			const ccz::editor_preview::BattlePlacementProfile& profile) {
			if (m_editorOps == nullptr) {
				ccz::editor_preview::BattleEditorResult result;
				result.diagnostic = L"当前没有可编辑的 S 战场会话";
				return result;
			}
			const auto result = m_editorOps->SetPlacementProfile(instruction, profile);
			if (result.ok) {
				RefreshEditorControls();
				Invalidate(FALSE);
			}
			return result;
		},
		[this](std::uint16_t instruction) {
			if (m_editorOps == nullptr) {
				ccz::editor_preview::BattleEditorResult result;
				result.diagnostic = L"当前没有可编辑的 S 战场会话";
				return result;
			}
			const auto result = m_editorOps->ResetPlacementProfile(instruction);
			if (result.ok) {
				RefreshEditorControls();
				Invalidate(FALSE);
			}
			return result;
		},
		[this](const ccz::preview::BattleCoord& coordinate,
			const ccz::editor_preview::BattleUnitPatch& patch) {
			if (m_editorOps == nullptr) {
				ccz::editor_preview::BattleEditorResult result;
				result.diagnostic = L"当前没有可编辑的 S 战场会话";
				return result;
			}
			const auto result = m_editorOps->ModifyAt(coordinate, patch);
			if (result.ok) {
				RefreshEditorControls();
				RefreshCharacterListIfDeploymentChanged();
				Invalidate(FALSE);
			}
			return result;
		},
		[this](const std::vector<std::uint64_t>& cacheIds,
			const ccz::editor_preview::BattleUnitPatch& patch) {
			if (m_editorOps == nullptr) {
				ccz::editor_preview::BattleEditorResult result;
				result.diagnostic = L"当前没有可编辑的 S 战场会话";
				return result;
			}
			const auto result = m_editorOps->ModifySelectionGroup(cacheIds, patch);
			if (result.ok) {
				RefreshEditorControls();
				RefreshCharacterListIfDeploymentChanged();
				Invalidate(FALSE);
			}
			return result;
		})) {
		AppendDiagnostic(L"S 战场出场参数窗口创建失败");
	}
	m_variableControlPane.SetDiagnosticHandler(
		[this](const std::wstring& diagnostic) {
			AppendDiagnostic(diagnostic);
		});
	m_controlsCreated = true;
	RefreshCharacterList();
	RefreshDeploymentList();
	RefreshEditorControls();
}

void CSBattlePreviewPage::LayoutEditorControls()
{
	if (GetSafeHwnd() == nullptr) return;
	CRect client;
	GetClientRect(&client);
	const int panelWidth = m_sidebarVisible
		? (std::min)(kSidebarWidth, (std::max)(0, client.Width())) : 0;
	const int reservedHeaderHeight = m_sidebarVisible
		? kStatusHeaderHeight
		: kStatusHeaderHeight + kCollapsedControlToolbarHeight;
	const int headerHeight = (std::min)(reservedHeaderHeight,
		(std::max)(0, client.Height()));
	const int footerTop = (std::max)(headerHeight,
		client.Height() - kStatusFooterHeight);
	m_canvasRect.SetRect(0, headerHeight,
		(std::max)(0, client.Width() - panelWidth), footerTop);
	if (!m_controlsCreated) {
		UpdateViewport();
		Invalidate(FALSE);
		return;
	}
	if (m_sidebarVisible) {
		m_quickModeButton.SetFont(&m_uiFont);
		m_copyPersonModeButton.SetFont(&m_uiFont);
		m_unrestrictedTerrainModeButton.SetFont(&m_uiFont);
		m_conditionTreeButton.SetFont(&m_uiFont);
		m_activationButton.SetFont(&m_uiFont);
		m_variableControlButton.SetFont(&m_uiFont);
		m_previewModeButton.SetFont(&m_uiFont);
		m_unitParametersButton.SetFont(&m_uiFont);
		m_writebackButton.SetFont(m_writebackFont.GetSafeHandle() != nullptr
			? &m_writebackFont : &m_uiFont);
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
		const int modeGap = 3;
		const int halfModeWidth = (width - modeGap) / 2;
		m_copyPersonModeButton.MoveWindow(left, y, halfModeWidth, 22);
		m_unrestrictedTerrainModeButton.MoveWindow(
			left + halfModeWidth + modeGap, y,
			width - halfModeWidth - modeGap, 22);
		y += 26;
		if (ActiveSession().condition_button_visible) {
			m_conditionTreeButton.MoveWindow(left, y, width, 22);
			m_conditionTreeButton.ShowWindow(SW_SHOW);
			y += 26;
		} else {
			m_conditionTreeButton.ShowWindow(SW_HIDE);
		}
		const int previewGap = 3;
		const int previewModeWidth = (width - previewGap) * 62 / 100;
		m_previewModeButton.MoveWindow(left, y, previewModeWidth, 22);
		m_unitParametersButton.MoveWindow(left + previewModeWidth + previewGap,
			y, width - previewModeWidth - previewGap, 22);
		m_previewModeButton.ShowWindow(SW_SHOW);
		m_unitParametersButton.ShowWindow(SW_SHOW);
		y += 26;
		const bool showActivation = ActiveSession().activation_button_visible;
		const bool showVariables = ActiveSession().variable_button_visible;
		if (showActivation || showVariables) {
			const int actionGap = 3;
			const int actionHalfWidth = (width - actionGap) / 2;
			if (showActivation) {
				m_activationButton.MoveWindow(left, y,
					showVariables ? actionHalfWidth : width, 22);
				m_activationButton.ShowWindow(SW_SHOW);
			} else {
				m_activationButton.ShowWindow(SW_HIDE);
			}
			if (showVariables) {
				m_variableControlButton.MoveWindow(
					showActivation ? left + actionHalfWidth + actionGap : left,
					y, showActivation ? width - actionHalfWidth - actionGap : width,
					22);
				m_variableControlButton.ShowWindow(SW_SHOW);
			} else {
				m_variableControlButton.ShowWindow(SW_HIDE);
			}
			y += 26;
		} else {
			m_activationButton.ShowWindow(SW_HIDE);
			m_variableControlButton.ShowWindow(SW_HIDE);
		}
		m_writebackButton.MoveWindow(left, y, width, 22);
		y += 26;
		m_searchEdit.MoveWindow(left, y, width, 22);
		y += 26;
		m_deploymentCombo.MoveWindow(left, y, width, 160);
		y += 28;
		const int bottom = client.bottom - 8;
		const int undoY = (std::max)(y, bottom - 54);
		m_characterList.MoveWindow(left, y, width, (std::max)(0, undoY - y - 3));
		m_undoButton.MoveWindow(left, undoY, width, 24);
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
			static_cast<CWnd*>(&m_copyPersonModeButton),
			static_cast<CWnd*>(&m_unrestrictedTerrainModeButton),
			static_cast<CWnd*>(&m_previewModeButton),
			static_cast<CWnd*>(&m_unitParametersButton),
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
			static_cast<CWnd*>(&m_copyPersonModeButton),
			static_cast<CWnd*>(&m_unrestrictedTerrainModeButton),
			static_cast<CWnd*>(&m_previewModeButton),
			static_cast<CWnd*>(&m_unitParametersButton),
			static_cast<CWnd*>(&m_searchEdit), static_cast<CWnd*>(&m_deploymentCombo),
			static_cast<CWnd*>(&m_characterList), static_cast<CWnd*>(&m_pageLabel)}) {
			if (control->GetSafeHwnd() != nullptr) control->ShowWindow(SW_HIDE);
		}
		m_activationButton.ShowWindow(SW_HIDE);
		m_variableControlButton.ShowWindow(SW_HIDE);
		const int buttonWidth = 96;
		m_showSidebarButton.MoveWindow(
			(std::max)(0, static_cast<int>(client.right) - buttonWidth - 8), 6,
			buttonWidth, 24);
		m_showSidebarButton.ShowWindow(SW_SHOW);
		LayoutCollapsedControlToolbar();
	}
	UpdateViewport();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::LayoutCollapsedControlToolbar()
{
	const int margin = 8;
	const int controlGap = 4;
	const int writebackGap = 12;
	const int toolbarY = kStatusHeaderHeight + 3;
	const int toolbarHeight = 22;
	const int availableWidth = (std::max)(0, m_canvasRect.Width() - 2 * margin);
	const int writebackWidth = (std::min)(92, availableWidth);
	const int writebackX = m_canvasRect.right - margin - writebackWidth;
	m_writebackButton.MoveWindow(writebackX, toolbarY,
		writebackWidth, toolbarHeight);
	m_writebackButton.SetFont(m_writebackFont.GetSafeHandle() != nullptr
		? &m_writebackFont : &m_uiFont);
	m_writebackButton.ShowWindow(SW_SHOW);

	struct ToolbarControl {
		CButton* button;
		int preferred_width;
	};
	std::vector<ToolbarControl> controls;
	controls.push_back({ &m_quickModeButton, 100 });
	controls.push_back({ &m_copyPersonModeButton, 70 });
	controls.push_back({ &m_unrestrictedTerrainModeButton, 100 });
	controls.push_back({ &m_previewModeButton, 100 });
	controls.push_back({ &m_unitParametersButton, 78 });
	if (ActiveSession().condition_button_visible)
		controls.push_back({ &m_conditionTreeButton, 76 });
	else
		m_conditionTreeButton.ShowWindow(SW_HIDE);
	if (ActiveSession().activation_button_visible)
		controls.push_back({ &m_activationButton, 108 });
	else
		m_activationButton.ShowWindow(SW_HIDE);
	if (ActiveSession().variable_button_visible)
		controls.push_back({ &m_variableControlButton, 82 });
	else
		m_variableControlButton.ShowWindow(SW_HIDE);
	const int gaps = controls.empty() ? 0
		: static_cast<int>(controls.size() - 1) * controlGap;
	const int controlsBudget = (std::max)(0,
		writebackX - writebackGap - margin - gaps);
	int preferredTotal = 0;
	for (const auto& control : controls)
		preferredTotal += control.preferred_width;
	const double scale = preferredTotal > 0
		? (std::min)(1.0, static_cast<double>(controlsBudget) / preferredTotal)
		: 1.0;
	int x = margin;
	for (const auto& control : controls) {
		const int width = static_cast<int>(std::lround(
			control.preferred_width * scale));
		control.button->MoveWindow(x, toolbarY, width, toolbarHeight);
		control.button->SetFont(m_toolbarFont.GetSafeHandle() != nullptr
			? &m_toolbarFont : &m_uiFont);
		control.button->ShowWindow(SW_SHOW);
		x += width + controlGap;
	}
}

void CSBattlePreviewPage::RefreshEditorControls()
{
	if (!m_controlsCreated) return;
	const auto setTextIfChanged = [](CWnd& control, LPCTSTR text) {
		CString current;
		control.GetWindowText(current);
		if (current.Compare(text) != 0) control.SetWindowText(text);
	};
	setTextIfChanged(m_characterTabButton, m_brushTabActive
		? _T("角色列表") : _T("角色列表 •"));
	setTextIfChanged(m_brushTabButton, m_brushTabActive
		? _T("小兵笔刷 •") : _T("小兵笔刷"));
	RefreshSelectedUnitIndex();
	RefreshCharacterListSelection();
	RefreshDeploymentSelection();
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
	setTextIfChanged(m_pageLabel, pageText);
	const bool previousEnabled = !m_brushTabActive &&
		ActiveSession().editor_state.characters.current_page > 0;
	if ((m_previousPageButton.IsWindowEnabled() != FALSE) != previousEnabled)
		m_previousPageButton.EnableWindow(previousEnabled);
	const bool nextEnabled = !m_brushTabActive && pages > 0 &&
		ActiveSession().editor_state.characters.current_page + 1 < pages;
	if ((m_nextPageButton.IsWindowEnabled() != FALSE) != nextEnabled)
		m_nextPageButton.EnableWindow(nextEnabled);
	const bool undoEnabled = !ActiveSession().stale &&
		(ActiveSession().native_writeback_transaction.undo_available() ||
			(m_editorOps && m_editorOps->CanUndo()));
	if ((m_undoButton.IsWindowEnabled() != FALSE) != undoEnabled)
		m_undoButton.EnableWindow(undoEnabled);
	const bool writebackEnabled = m_editorOps != nullptr &&
		ActiveSession().baseline_capture.ok &&
		ActiveSession().active_condition_scope.writeback_scope_resolved &&
		!ActiveSession().stale && !ActiveSession().native_writeback_rebase_required;
	if ((m_writebackButton.IsWindowEnabled() != FALSE) != writebackEnabled)
		m_writebackButton.EnableWindow(writebackEnabled);
	const bool previewModeEnabled =
		ActiveSession().editor_state.battle.map.projection_valid;
	if ((m_previewModeButton.IsWindowEnabled() != FALSE) != previewModeEnabled)
		m_previewModeButton.EnableWindow(previewModeEnabled);
	if (m_copyPersonModeButton.GetSafeHwnd() != nullptr &&
		(m_copyPersonModeButton.GetCheck() == BST_CHECKED) !=
			ActiveSession().ui.copy_person_mode) {
		m_copyPersonModeButton.SetCheck(ActiveSession().ui.copy_person_mode
			? BST_CHECKED : BST_UNCHECKED);
	}
	if (m_unrestrictedTerrainModeButton.GetSafeHwnd() != nullptr &&
		(m_unrestrictedTerrainModeButton.GetCheck() == BST_CHECKED) !=
			ActiveSession().ui.unrestricted_terrain_mode) {
		m_unrestrictedTerrainModeButton.SetCheck(
			ActiveSession().ui.unrestricted_terrain_mode
				? BST_CHECKED : BST_UNCHECKED);
	}
	const bool deploymentEnabled = m_editorOps != nullptr;
	if ((m_deploymentCombo.IsWindowEnabled() != FALSE) != deploymentEnabled)
		m_deploymentCombo.EnableWindow(deploymentEnabled);
	const bool defaultsEnabled = deploymentEnabled;
	if ((m_unitParametersButton.IsWindowEnabled() != FALSE) != defaultsEnabled)
		m_unitParametersButton.EnableWindow(defaultsEnabled);
	if (m_unitParametersPane.GetSafeHwnd() != nullptr)
		m_unitParametersPane.UpdateState(ActiveSession().editor_state);
}

void CSBattlePreviewPage::RefreshCharacterList()
{
	if (!m_controlsCreated) return;
	m_characterList.SetRedraw(FALSE);
	m_characterList.ResetContent();
	const std::wstring query = ActiveSession().editor_state.characters.query;
	const std::vector<int> deployedDataIds = m_editorOps
		? m_editorOps->CurrentDeployedCharacterDataIds() : std::vector<int>{};
	if (m_brushTabActive) {
		for (const auto& brush : ActiveSession().editor_state.minion_brushes) {
			std::wostringstream searchable;
			searchable << brush.name << L" " << brush.first_data_id << L" "
				<< brush.last_data_id;
			if (!contains_ignore_case(searchable.str(), query)) continue;
			std::size_t remaining = 0;
			if (brush.first_data_id >= 0 &&
				brush.last_data_id >= brush.first_data_id) {
				const auto occupiedBegin = std::lower_bound(
					deployedDataIds.begin(), deployedDataIds.end(),
					brush.first_data_id);
				const auto occupiedEnd = std::upper_bound(
					occupiedBegin, deployedDataIds.end(), brush.last_data_id);
				const std::size_t occupied = static_cast<std::size_t>(
					std::distance(occupiedBegin, occupiedEnd));
				const std::size_t capacity = static_cast<std::size_t>(
					brush.last_data_id - brush.first_data_id + 1);
				remaining = occupied >= capacity ? 0 : capacity - occupied;
			}
			std::wostringstream label;
			label << brush.name << L"（剩余 " << remaining << L"）";
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
			label << character.data_id << L"." << character.name;
			if (std::binary_search(deployedDataIds.begin(), deployedDataIds.end(),
				character.data_id)) label << L" [已出场]";
			const int index = m_characterList.AddString(label.str().c_str());
			if (index != LB_ERR && index != LB_ERRSPACE) {
				m_characterList.SetItemData(index,
					static_cast<DWORD_PTR>(character.data_id));
				if (ActiveSession().editor_state.characters.selected_data_id == character.data_id)
					m_characterList.SetCurSel(index);
			}
		}
	}
	m_rosterDeployedDataIds = deployedDataIds;
	m_characterList.SetRedraw(TRUE);
	m_characterList.Invalidate(FALSE);
}

void CSBattlePreviewPage::RefreshCharacterListIfDeploymentChanged()
{
	if (!m_controlsCreated) return;
	const std::vector<int> deployedDataIds = m_editorOps
		? m_editorOps->CurrentDeployedCharacterDataIds() : std::vector<int>{};
	if (deployedDataIds != m_rosterDeployedDataIds)
		RefreshCharacterList();
}

void CSBattlePreviewPage::RefreshDeploymentList()
{
	if (!m_controlsCreated) return;
	m_deploymentCombo.ResetContent();
	const struct DeploymentChoice { LPCTSTR label; std::uint16_t instruction; } choices[] = {
		{ _T("我军出场 (0x4B)"), 0x4b },
		{ _T("友军出场 (0x46)"), 0x46 },
		{ _T("敌军出场 (0x47)"), 0x47 },
	};
	for (const auto& choice : choices) {
		const int item = m_deploymentCombo.AddString(choice.label);
		if (item == CB_ERR || item == CB_ERRSPACE) continue;
		m_deploymentCombo.SetItemData(item, choice.instruction);
	}
	RefreshDeploymentSelection();
}

void CSBattlePreviewPage::RefreshCharacterListSelection()
{
	if (!m_controlsCreated) return;
	const int dataId = m_brushTabActive
		? (ActiveSession().editor_state.selected_minion_brush
			? ActiveSession().editor_state.selected_minion_brush->first_data_id
			: -1)
		: ActiveSession().editor_state.characters.selected_data_id;
	int desired = LB_ERR;
	if (dataId >= 0) {
		const int count = m_characterList.GetCount();
		for (int index = 0; index < count; ++index) {
			if (static_cast<int>(m_characterList.GetItemData(index)) == dataId) {
				desired = index;
				break;
			}
		}
	}
	if (m_characterList.GetCurSel() != desired)
		m_characterList.SetCurSel(desired);
}

void CSBattlePreviewPage::RefreshDeploymentSelection()
{
	if (!m_controlsCreated) return;
	const auto instruction = ActiveSession().editor_state.placement.instruction_id;
	int desired = CB_ERR;
	const int count = m_deploymentCombo.GetCount();
	for (int index = 0; index < count; ++index) {
		if (m_deploymentCombo.GetItemData(index) == instruction) {
			desired = index;
			break;
		}
	}
	if (m_deploymentCombo.GetCurSel() != desired)
		m_deploymentCombo.SetCurSel(desired);
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
	const ccz::editor_preview::BattleEditorResult& result)
{
	if (!result.diagnostic.empty()) m_diagnostic = result.diagnostic.c_str();
	if (m_controlsCreated) RefreshEditorControls();
	else RefreshSelectedUnitIndex();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::ShowSelectionOperationResult(
	const ccz::editor_preview::BattleEditorResult& result)
{
	if (!result.diagnostic.empty()) m_diagnostic = result.diagnostic.c_str();
	// Selection membership does not alter brush, deployment, or parameter
	// controls. Avoid rebuilding the hidden parameter pane and roster controls.
	m_selectedUnit.reset();
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
	ShowOperationResult(result);
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
	if (m_selectionGestureActive) ReleaseCapture();
	m_panActive = false;
	m_leftStampActive = false;
	m_selectionGestureActive = false;
	m_selectionDragStarted = false;
	UnbindSession();
	if (m_conditionTreePane.GetSafeHwnd() != nullptr)
		m_conditionTreePane.DestroyWindow();
	if (m_variableControlPane.GetSafeHwnd() != nullptr)
		m_variableControlPane.DestroyWindow();
	if (m_unitParametersPane.GetSafeHwnd() != nullptr)
		m_unitParametersPane.DestroyWindow();
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
		UpdateConditionActivationTarget(
			capture.snapshot.selected_handle);
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
				UpdateConditionActivationTarget(
					reinterpret_cast<std::uintptr_t>(tree.GetSelectedItem()));
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
	reachabilityScope.reachable_deployment_handles =
		ActiveSession().active_condition_scope.reachable_deployment_handles;
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
	if ((m_panActive || m_leftStampActive || m_selectionGestureActive) &&
		GetSafeHwnd() != nullptr) ReleaseCapture();
	ReleaseActiveBitmapPins();
	m_panActive = false;
	m_selectionGestureActive = false;
	m_selectionDragStarted = false;
	m_selectionPressCell.reset();
	m_selectionLastCell.reset();
	m_selectionPressPoint = CPoint(-1, -1);
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
	ActiveSession().condition_variable_index = {};
	ActiveSession().condition_activation_target.clear();
	ActiveSession().condition_button_visible = false;
	ActiveSession().activation_button_visible = false;
	ActiveSession().variable_button_visible = false;
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
	m_variableControlPane.UpdateRows({}, {}, {});
	m_variableControlPane.ShowPane(false);
	m_documentPath.Empty();
	m_diagnostic.Empty();
	m_quickModeDiagnostic.Empty();
	m_sourceTreeWindow = nullptr;
	ActiveSession().quick_mode_section_revision = 0;
	if (m_controlsCreated) {
		RefreshCharacterList();
		RefreshDeploymentSelection();
		RefreshEditorControls();
	}
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
	auto oldConditionVariableIndex = session.condition_variable_index;
	auto oldConditionActivationTarget = session.condition_activation_target;
	const auto oldObservedVersion = session.observed_version;
	const auto oldHasBaseline = session.has_baseline;
	const auto oldStale = session.stale;
	const auto oldQuickModeRevision = session.quick_mode_section_revision;
	const auto oldMapAssetKey = session.map_asset_key;
	const auto oldUnitMovAssetKey = session.unit_mov_asset_key;
	const auto oldDocumentPath = session.document_path;
	const auto oldConditionButtonVisible = session.condition_button_visible;
	const auto oldActivationButtonVisible = session.activation_button_visible;
	const auto oldVariableButtonVisible = session.variable_button_visible;

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
		session.condition_variable_index = std::move(oldConditionVariableIndex);
		session.condition_activation_target =
			std::move(oldConditionActivationTarget);
		session.observed_version = oldObservedVersion;
		session.has_baseline = oldHasBaseline;
		session.stale = oldStale;
		session.quick_mode_section_revision = oldQuickModeRevision;
		session.map_asset_key = oldMapAssetKey;
		session.unit_mov_asset_key = oldUnitMovAssetKey;
		session.document_path = oldDocumentPath;
		session.condition_button_visible = oldConditionButtonVisible;
		session.activation_button_visible = oldActivationButtonVisible;
		session.variable_button_visible = oldVariableButtonVisible;
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
	RefreshDeploymentSelection();
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
	ActiveSession().condition_variable_index =
		ccz::preview::scan_script_variables(
			ActiveSession().baseline_capture.input.nodes,
			ActiveSession().baseline_capture.input.document_key);
	ccz::preview::synchronize_boolean_defaults(
		ActiveSession().condition_options.simulated_variables,
		ActiveSession().condition_variable_index);
	ccz::preview::synchronize_integer_scope(
		ActiveSession().condition_options.simulated_variables,
		ActiveSession().condition_variable_index);
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
	UpdateConditionActivationTarget(
		ActiveSession().baseline_capture.snapshot.selected_handle);
	RefreshConditionTree();
}

bool CSBattlePreviewPage::ActivateConditionScope(
	const ccz::editor_preview::BattleConditionScope& scope)
{
	if (scope.workspace_key.empty() || !ActiveSession().baseline_capture.ok)
		return false;
	if (ActiveSession().active_workspace_key == scope.workspace_key && m_editorOps != nullptr) {
		ActiveSession().editor_state.copy_person_mode =
			ActiveSession().ui.copy_person_mode;
		ActiveSession().editor_state.unrestricted_terrain_mode =
			ActiveSession().ui.unrestricted_terrain_mode;
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
		ActiveSession().editor_state.copy_person_mode =
			ActiveSession().ui.copy_person_mode;
		ActiveSession().editor_state.unrestricted_terrain_mode =
			ActiveSession().ui.unrestricted_terrain_mode;
		existing->second->scope = scope;
		existing->second->state.copy_person_mode =
			ActiveSession().ui.copy_person_mode;
		existing->second->state.unrestricted_terrain_mode =
			ActiveSession().ui.unrestricted_terrain_mode;
		m_editorOps = existing->second->operations.get();
		ActiveSession().active_workspace_key = scope.workspace_key;
		ActiveSession().active_condition_scope = scope;
		m_diagnostic.Empty();
		UpdateViewport();
		RefreshCharacterList();
		RefreshEditorControls();
		return true;
	}

	ActiveSession().editor_state = {};
	ActiveSession().editor_state.scope_root = ActiveSession().baseline_capture.input.scope_key;
	ActiveSession().editor_state.copy_person_mode =
		ActiveSession().ui.copy_person_mode;
	ActiveSession().editor_state.unrestricted_terrain_mode =
		ActiveSession().ui.unrestricted_terrain_mode;
	ActiveSession().editor_state.characters.page_size =
		ccz::editor_preview::kBattleCharacterPageSize;
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
	ActiveSession().editor_state.movement_terrain = m_assets.movement_terrain();
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
	UpdateViewport();
	RefreshCharacterList();
	RefreshEditorControls();
	return true;
}

void CSBattlePreviewPage::RefreshConditionTree()
{
	m_conditionTreePane.UpdateModel(ActiveSession().condition_model, {}, ActiveSession().condition_options);
	ActiveSession().condition_button_visible = ActiveSession().condition_model != nullptr &&
		m_conditionTreePane.HasControllableNodes();
	if (ActiveSession().baseline_capture.ok) {
		m_variableControlPane.UpdateRows(
			ActiveSession().condition_variable_index,
			ActiveSession().condition_options.simulated_variables,
			ActiveSession().baseline_capture.input.scope_key);
	} else {
		m_variableControlPane.UpdateRows({}, {}, {});
	}
	ActiveSession().variable_button_visible =
		ActiveSession().condition_model != nullptr &&
		ActiveSession().condition_variable_index.valid &&
		m_variableControlPane.HasRows();
	ActiveSession().activation_button_visible =
		ActiveSession().condition_model != nullptr &&
		!ActiveSession().condition_activation_target.empty() &&
		ccz::preview::condition_tree_model_can_activate_target(
			*ActiveSession().condition_model,
			ActiveSession().condition_activation_target,
			ActiveSession().condition_options);
	if (m_conditionTreeButton.GetSafeHwnd() != nullptr)
		m_conditionTreeButton.ShowWindow(ActiveSession().condition_button_visible
			? SW_SHOW : SW_HIDE);
	if (m_activationButton.GetSafeHwnd() != nullptr)
		m_activationButton.ShowWindow(ActiveSession().activation_button_visible
			? SW_SHOW : SW_HIDE);
	if (m_variableControlButton.GetSafeHwnd() != nullptr)
		m_variableControlButton.ShowWindow(ActiveSession().variable_button_visible
			? SW_SHOW : SW_HIDE);
	if (!ActiveSession().condition_button_visible) m_conditionTreePane.ShowPane(false);
	if (!ActiveSession().variable_button_visible)
		m_variableControlPane.ShowPane(false);
	if (m_controlsCreated) LayoutEditorControls();
}

void CSBattlePreviewPage::UpdateConditionActivationTarget(
	std::uintptr_t runtime_handle)
{
	auto& session = ActiveSession();
	const bool wasVisible = session.activation_button_visible;
	session.condition_activation_target.clear();
	if (session.condition_model != nullptr) {
		const auto* selected = find_execution_node_by_runtime_handle(
			*session.condition_model, runtime_handle);
		if (selected != nullptr)
			session.condition_activation_target = selected->logical_key;
	}
	session.activation_button_visible = session.condition_model != nullptr &&
		!session.condition_activation_target.empty() &&
		ccz::preview::condition_tree_model_can_activate_target(
			*session.condition_model, session.condition_activation_target,
			session.condition_options);
	if (m_activationButton.GetSafeHwnd() != nullptr)
		m_activationButton.ShowWindow(session.activation_button_visible
			? SW_SHOW : SW_HIDE);
	if (wasVisible != session.activation_button_visible && m_controlsCreated)
		LayoutEditorControls();
}

void CSBattlePreviewPage::OnConditionTreeClicked()
{
	if (!ActiveSession().condition_button_visible ||
		m_conditionTreePane.GetSafeHwnd() == nullptr ||
		!m_conditionTreePane.HasControllableNodes()) return;
	m_variableControlPane.ShowPane(false);
	m_unitParametersPane.ShowPane(false);
	m_conditionTreePane.ShowPane(true);
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnUnitParametersClicked()
{
	if (m_unitParametersPane.GetSafeHwnd() == nullptr) return;
	m_conditionTreePane.ShowPane(false);
	m_variableControlPane.ShowPane(false);
	m_unitParametersPane.ShowDefaultsState(ActiveSession().editor_state);
	m_unitParametersPane.ShowPane(true);
}

void CSBattlePreviewPage::OpenSingleUnitParameters(std::uint64_t cache_id)
{
	if (m_unitParametersPane.GetSafeHwnd() == nullptr) return;
	m_conditionTreePane.ShowPane(false);
	m_variableControlPane.ShowPane(false);
	m_unitParametersPane.ShowSingleUnitState(
		ActiveSession().editor_state, cache_id);
	m_unitParametersPane.ShowPane(true);
}

void CSBattlePreviewPage::OpenBatchUnitParameters()
{
	if (m_unitParametersPane.GetSafeHwnd() == nullptr) return;
	const auto& state = ActiveSession().editor_state;
	std::vector<std::uint64_t> cache_ids;
	cache_ids.reserve(state.selection_group.size());
	for (const auto& selected : state.selection_group)
		if (selected.cache_id != 0) cache_ids.push_back(selected.cache_id);
	if (cache_ids.size() < 2) {
		AppendDiagnostic(L"批量修改至少需要两个有效的多选单位");
		return;
	}
	ccz::editor_preview::BattleBatchParameterMask supported = 0;
	ccz::editor_preview::BattleBatchParameterMask differing = 0;
	const std::wstring differences =
		differing_batch_parameter_names(state, cache_ids, supported, differing);
	if (differences == L"目标单位已失效") {
		AppendDiagnostic(differences);
		return;
	}
	ccz::editor_preview::BattleBatchParameterSelection selection{
		true, supported};
	if (!differences.empty()) {
		CBatchParameterWarningDialog dialog(differences);
		const INT_PTR choice = dialog.DoModal();
		const auto decision = choice == IDC_BATTLE_BATCH_OVERWRITE
			? ccz::editor_preview::BattleBatchWarningChoice::OverwriteAll
			: (choice == IDC_BATTLE_BATCH_UNIFORM
				? ccz::editor_preview::BattleBatchWarningChoice::OnlyUniform
				: ccz::editor_preview::BattleBatchWarningChoice::Cancel);
		selection = ccz::editor_preview::choose_batch_parameters(
			decision, supported, differing);
	}
	if (!selection.proceed) return;
	m_conditionTreePane.ShowPane(false);
	m_variableControlPane.ShowPane(false);
	m_unitParametersPane.ShowBatchState(state, std::move(cache_ids),
		selection.checked);
	m_unitParametersPane.ShowPane(true);
}

void CSBattlePreviewPage::OnPreviewModeClicked()
{
	ShowPreviewModeMenu();
}

void CSBattlePreviewPage::ShowPreviewModeMenu()
{
	if (m_previewModeButton.GetSafeHwnd() == nullptr ||
		!m_previewModeButton.IsWindowVisible()) return;
	CMenu menu;
	if (!menu.CreatePopupMenu()) return;
	using Mode = ccz::editor_preview::BattleParameterPreviewMode;
	const struct Choice { Mode mode; const wchar_t* label; } choices[] = {
		{Mode::BattlefieldIndex, L"战场编号"},
		{Mode::Level, L"等级"},
		{Mode::UnitType, L"兵种级别"},
		{Mode::AiPolicy, L"行动方针"},
		{Mode::Reinforcement, L"援军"},
		{Mode::None, L"无"},
	};
	const Mode active = ActiveSession().ui.parameter_preview_mode;
	for (std::size_t index = 0; index < std::size(choices); ++index) {
		UINT flags = MF_STRING;
		if (choices[index].mode == active) flags |= MF_CHECKED;
		menu.AppendMenu(flags, kPreviewModeMenuFirstId +
			static_cast<UINT>(index), choices[index].label);
	}
	CRect button;
	m_previewModeButton.GetWindowRect(&button);
	const UINT selected = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
		button.left, button.bottom, this);
	if (selected < kPreviewModeMenuFirstId ||
		selected >= kPreviewModeMenuFirstId + std::size(choices)) return;
	SetParameterPreviewMode(choices[selected - kPreviewModeMenuFirstId].mode);
}

void CSBattlePreviewPage::SetParameterPreviewMode(
	ccz::editor_preview::BattleParameterPreviewMode mode)
{
	ActiveSession().ui.parameter_preview_mode = mode;
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnActivationButtonClicked()
{
	if (!ActiveSession().activation_button_visible ||
		ActiveSession().condition_model == nullptr ||
		ActiveSession().condition_activation_target.empty()) return;
	auto candidate = ActiveSession().condition_options;
	std::wstring diagnostic;
	if (!ccz::preview::activate_condition_tree_target(
		candidate, *ActiveSession().condition_model,
		ActiveSession().condition_activation_target, &diagnostic)) {
		if (!diagnostic.empty()) AppendDiagnostic(diagnostic);
		return;
	}
	ActiveSession().condition_options = std::move(candidate);
	if (PostMessage(kConditionActionMessage, 0, 0) == FALSE)
		OnConditionActionMessage(0, 0);
}

void CSBattlePreviewPage::OnVariableControlClicked()
{
	if (!ActiveSession().variable_button_visible ||
		m_variableControlPane.GetSafeHwnd() == nullptr ||
		!m_variableControlPane.HasRows()) return;
	m_conditionTreePane.ShowPane(false);
	m_unitParametersPane.ShowPane(false);
	m_variableControlPane.ShowPane(true);
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

void CSBattlePreviewPage::ApplyVariableActionAndRefresh(
	ccz::preview::VariableControlKind kind, std::int32_t id,
	ccz::preview::VariableControlAction action,
	std::int32_t integerValue)
{
	if (!ActiveSession().baseline_capture.ok ||
		!ActiveSession().condition_variable_index.valid) return;
	auto candidate = ActiveSession().condition_options;
	if (!ccz::preview::apply_variable_control_action(
		candidate.simulated_variables, kind, id, action, integerValue)) {
		AppendDiagnostic(L"变量操作未产生状态变化");
		return;
	}
	ccz::preview::synchronize_boolean_defaults(
		candidate.simulated_variables,
		ActiveSession().condition_variable_index);
	ccz::preview::synchronize_integer_scope(
		candidate.simulated_variables,
		ActiveSession().condition_variable_index);
	ActiveSession().condition_options = std::move(candidate);
	if (PostMessage(kConditionActionMessage, 0, 0) == FALSE)
		OnConditionActionMessage(0, 0);
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

void CSBattlePreviewPage::EnsurePortraitAssets()
{
	const std::wstring documentPath = m_documentPath.GetString();
	const std::wstring root = battle_mod_root(documentPath);
	if (root.empty()) {
		if (!m_portraitRootKey.empty()) {
			m_portraitAssets.Clear();
			m_unitPortraits.clear();
			m_failedPortraitDataIds.clear();
		}
		m_portraitRootKey.clear();
		m_portraitDataRevision = 0;
		m_portraitFaceRevision = 0;
		return;
	}
	const std::uint64_t dataRevision = preview_asset_revision(root + L"Data.e5");
	const std::uint64_t faceRevision = preview_asset_revision(
		root + L"E5\\Face.e5");
	if (root != m_portraitRootKey || dataRevision != m_portraitDataRevision ||
		faceRevision != m_portraitFaceRevision) {
		m_portraitAssets.Clear();
		m_unitPortraits.clear();
		m_failedPortraitDataIds.clear();
		m_portraitRootKey = root;
		m_portraitDataRevision = dataRevision;
		m_portraitFaceRevision = faceRevision;
		std::wstring diagnostic;
		m_portraitAssets.LoadForDocument(documentPath,
			battle_portrait_profile(), &diagnostic);
	}
}

Gdiplus::Bitmap* CSBattlePreviewPage::GetUnitPortrait(int data_id)
{
	if (data_id < 0 || data_id > 1023) return nullptr;
	EnsurePortraitAssets();
	const auto cached = m_unitPortraits.find(data_id);
	if (cached != m_unitPortraits.end()) return cached->second.get();
	if (m_failedPortraitDataIds.find(data_id) != m_failedPortraitDataIds.end())
		return nullptr;
	const auto assets = m_portraitAssets.view();
	if (assets.data_e5 == nullptr || assets.face_archive == nullptr ||
		assets.face_e5 == nullptr) {
		m_failedPortraitDataIds.insert(data_id);
		return nullptr;
	}
	const auto resolved = ccz::preview::resolve_portrait(data_id, 0,
		*assets.data_e5, battle_portrait_profile(), *assets.face_archive,
		*assets.face_e5);
	if (!resolved.resolved) {
		m_failedPortraitDataIds.insert(data_id);
		return nullptr;
	}
	std::unique_ptr<Gdiplus::Bitmap> bitmap(
		decode_portrait_bitmap(resolved.encoded_image));
	if (bitmap == nullptr) {
		m_failedPortraitDataIds.insert(data_id);
		return nullptr;
	}
	auto* result = bitmap.get();
	m_unitPortraits.emplace(data_id, std::move(bitmap));
	return result;
}

Gdiplus::Bitmap* CSBattlePreviewPage::GetOwnFactionPlaceholderPortrait()
{
	if (m_ownFactionPlaceholderPortraitLoaded)
		return m_ownFactionPlaceholderPortrait.get();
	m_ownFactionPlaceholderPortraitLoaded = true;

	HINSTANCE resource_module = AfxGetResourceHandle();
	HRSRC resource = ::FindResourceW(resource_module,
		MAKEINTRESOURCEW(IDR_BATTLE_OWN_PORTRAIT), RT_RCDATA);
	if (resource == nullptr) return nullptr;
	const DWORD resource_size = ::SizeofResource(resource_module, resource);
	if (resource_size == 0) return nullptr;
	HGLOBAL loaded_resource = ::LoadResource(resource_module, resource);
	if (loaded_resource == nullptr) return nullptr;
	const auto* resource_bytes = static_cast<const std::uint8_t*>(
		::LockResource(loaded_resource));
	if (resource_bytes == nullptr) return nullptr;

	const std::vector<std::uint8_t> encoded(resource_bytes,
		resource_bytes + resource_size);
	m_ownFactionPlaceholderPortrait.reset(decode_portrait_bitmap(encoded));
	return m_ownFactionPlaceholderPortrait.get();
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
		CRect clientBefore;
		GetClientRect(&clientBefore);
		ShowScrollBar(SB_HORZ, FALSE);
		ShowScrollBar(SB_VERT, FALSE);
		CRect clientAfter;
		GetClientRect(&clientAfter);
		if (clientBefore.Width() != clientAfter.Width() ||
			clientBefore.Height() != clientAfter.Height())
			LayoutEditorControls();
		m_viewportUpdating = false;
		return;
	}
	CRect clientBefore;
	GetClientRect(&clientBefore);
	for (int pass = 0; pass < 3; ++pass) {
		CRect client;
		GetClientRect(&client);
		const int reservedHeaderHeight = m_sidebarVisible
			? kStatusHeaderHeight
			: kStatusHeaderHeight + kCollapsedControlToolbarHeight;
		const int headerHeight = (std::min)(reservedHeaderHeight,
			(std::max)(0, client.Height()));
		const int footerTop = (std::max)(headerHeight,
			client.Height() - kStatusFooterHeight);
		const int availableHeight = footerTop - headerHeight;
		const int availableWidth = (std::max)(0, client.Width() -
			(m_sidebarVisible ? kSidebarWidth : 0));
		ShowScrollBar(SB_HORZ, contentWidth > availableWidth);
		ShowScrollBar(SB_VERT, contentHeight > availableHeight);
	}
	CRect clientAfter;
	GetClientRect(&clientAfter);
	if (clientBefore.Width() != clientAfter.Width() ||
		clientBefore.Height() != clientAfter.Height()) {
		// Only a scrollbar visibility change alters the client geometry. Panning
		// changes the map offset, so moving the sidebar controls on every mouse
		// move would force redundant child-window repaints and expose stale rows.
		LayoutEditorControls();
	}
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

void CSBattlePreviewPage::DrawHoveredUnitCard(
	Gdiplus::Graphics& graphics,
	const ccz::preview::BattleCoord& coordinate,
	const CPoint& pointer, float)
{
	if (m_panActive || !m_canvasRect.PtInRect(pointer)) return;
	const auto& state = ActiveSession().editor_state;
	std::vector<const ccz::preview::BattleUnit*> units;
	for (const auto& unit : state.battle.units) {
		if (!unit.fields_decoded || unit.editor_pending_delete ||
			(!unit.editor_staged_new && !in_scope(unit.source_key, state.scope_root)) ||
			ccz::editor_preview::IsBattleFixedEmptySlot(unit) ||
			unit.position != coordinate) continue;
		units.push_back(&unit);
	}
	if (units.empty()) return;

	const int canvasWidth = m_canvasRect.Width();
	const int canvasHeight = m_canvasRect.Height();
	if (canvasWidth < 210 || canvasHeight < 130) return;
	const int cardWidth = (std::min)(360, canvasWidth - 12);
	const int lineHeight = 18;
	const int portraitSize = (std::min)(88,
		(std::min)(cardWidth / 3, canvasHeight - 68));
	std::wstring title;
	std::vector<std::wstring> lines;
	if (units.size() == 1) {
		const auto& unit = *units.front();
		const std::uint16_t instruction = battle_unit_deployment(unit);
		const int battlefieldIndex = predicted_battlefield_index(state, unit);
		if (instruction == 0x4b) {
			const auto label = own_battle_unit_status_label(battlefieldIndex);
			title = label.identity + label.body;
		} else {
			std::wstring characterName = L"身份未解析";
			if (unit.data_id >= 0) {
				const auto found = std::find_if(state.characters.entries.begin(),
					state.characters.entries.end(), [&unit](const auto& character) {
						return character.data_id == unit.data_id;
					});
				if (found != state.characters.entries.end() && found->has_name)
					characterName = found->name;
				else
					characterName = L"Data " + std::to_wstring(unit.data_id);
			}
			title = unit.data_id >= 0
				? L"【" + std::to_wstring(unit.data_id) + L"】"
				: L"【未知】";
			title += battlefieldIndex >= 0
				? std::to_wstring(battlefieldIndex) : std::wstring(L"待分配");
			title += L"." + characterName;
		}
		lines.push_back(L"坐标：(" + std::to_wstring(unit.position.x) + L", " +
			std::to_wstring(unit.position.y) + L")");
		if (unit_has_battle_role(unit, ccz::preview::BattleFieldRole::Hidden))
			lines.push_back(std::wstring(L"隐藏：") + (unit.hidden ? L"是" : L"否"));
		if (unit_has_battle_role(unit, ccz::preview::BattleFieldRole::Level))
			lines.push_back(std::wstring(L"等级偏移：") +
				(unit.level >= 0 ? L"+" : L"") +
				std::to_wstring(unit.level));
		if (unit_has_battle_role(unit, ccz::preview::BattleFieldRole::UnitType))
			lines.push_back(L"兵种级别：" +
				std::wstring(battle_unit_type_name(unit.unit_type)));
		if (unit_has_battle_role(unit, ccz::preview::BattleFieldRole::Reinforcement))
			lines.push_back(std::wstring(L"援军：") +
				(unit.reinforcement ? L"是" : L"否"));
		if (unit_has_battle_role(unit, ccz::preview::BattleFieldRole::AiPolicy))
			lines.push_back(L"行动方针：" +
				std::wstring(battle_ai_policy_name(unit.ai_policy)));
		if ((unit.ai_policy == 3 || unit.ai_policy == 5) &&
			unit_has_battle_role(unit, ccz::preview::BattleFieldRole::TargetDataId))
			lines.push_back(L"目标 Data：" + std::to_wstring(unit.target));
		if ((unit.ai_policy == 4 || unit.ai_policy == 6) &&
			unit_has_battle_role(unit, ccz::preview::BattleFieldRole::TargetX) &&
			unit_has_battle_role(unit, ccz::preview::BattleFieldRole::TargetY))
			lines.push_back(L"目标坐标：(" + std::to_wstring(unit.target_x) + L", " +
				std::to_wstring(unit.target_y) + L")");
	}
	const int requiredHeight = units.size() > 1 ? 82 : (std::max)(
		56 + portraitSize,
		54 + static_cast<int>(lines.size()) * lineHeight);
	const int cardHeight = (std::min)(canvasHeight - 12, requiredHeight);
	const int canvasLeft = static_cast<int>(m_canvasRect.left);
	const int canvasTop = static_cast<int>(m_canvasRect.top);
	const int canvasRight = static_cast<int>(m_canvasRect.right);
	const int canvasBottom = static_cast<int>(m_canvasRect.bottom);
	int cardX = pointer.x + 18;
	int cardY = pointer.y + 18;
	if (cardX + cardWidth > canvasRight - 6)
		cardX = pointer.x - cardWidth - 18;
	if (cardY + cardHeight > canvasBottom - 6)
		cardY = pointer.y - cardHeight - 18;
	cardX = (std::max)(canvasLeft + 6,
		(std::min)(cardX, canvasRight - cardWidth - 6));
	cardY = (std::max)(canvasTop + 6,
		(std::min)(cardY, canvasBottom - cardHeight - 6));

	const Gdiplus::GraphicsState saved = graphics.Save();
	graphics.SetClip(Gdiplus::Rect(m_canvasRect.left, m_canvasRect.top,
		m_canvasRect.Width(), m_canvasRect.Height()),
		Gdiplus::CombineModeIntersect);
	const Gdiplus::Rect card(cardX, cardY, cardWidth, cardHeight);
	Gdiplus::SolidBrush panel(Gdiplus::Color(242, 26, 28, 32));
	Gdiplus::Pen border(Gdiplus::Color(230, 205, 210, 218), 1.2f);
	graphics.FillRectangle(&panel, card);
	graphics.DrawRectangle(&border, card);
	Gdiplus::Font titleFont(L"Microsoft YaHei", 12.0f,
		Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font bodyFont(L"Microsoft YaHei", 10.0f,
		Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 230, 182));
	Gdiplus::SolidBrush bodyBrush(Gdiplus::Color(255, 238, 240, 242));
	Gdiplus::StringFormat format;
	format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
	format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	const Gdiplus::RectF titleBounds(
		static_cast<float>(cardX + 10), static_cast<float>(cardY + 8),
		static_cast<float>(cardWidth - 20), 22.0f);
	if (units.size() > 1) {
		const std::wstring overlapTitle = L"此格有多个单位（" +
			std::to_wstring(units.size()) + L"）";
		graphics.DrawString(overlapTitle.c_str(), -1, &titleFont, titleBounds,
			&format, &titleBrush);
		Gdiplus::Font hintFont(L"Microsoft YaHei", 11.0f);
		const std::wstring hint = L"单位重叠，无法唯一对应头像和参数";
		graphics.DrawString(hint.c_str(), -1, &hintFont,
			Gdiplus::PointF(static_cast<float>(cardX + 12),
				static_cast<float>(cardY + 52)), &bodyBrush);
		graphics.Restore(saved);
		return;
	}

	const auto& unit = *units.front();
	graphics.DrawString(title.c_str(), -1, &titleFont, titleBounds,
		&format, &titleBrush);
	Gdiplus::Pen divider(Gdiplus::Color(130, 190, 195, 202), 1.0f);
	graphics.DrawLine(&divider, cardX + 10, cardY + 34,
		cardX + cardWidth - 10, cardY + 34);

	const int actualPortraitSize = (std::min)(portraitSize, cardHeight - 56);
	const Gdiplus::Rect portraitRect(cardX + 10, cardY + 46,
		actualPortraitSize, actualPortraitSize);
	Gdiplus::SolidBrush portraitBackground(Gdiplus::Color(255, 13, 14, 16));
	Gdiplus::Pen portraitBorder(Gdiplus::Color(210, 155, 160, 166), 1.0f);
	graphics.FillRectangle(&portraitBackground, portraitRect);
	Gdiplus::Bitmap* portrait = battle_unit_deployment(unit) == 0x4b
		? GetOwnFactionPlaceholderPortrait()
		: (unit.data_id >= 0 ? GetUnitPortrait(unit.data_id) : nullptr);
	if (portrait != nullptr && portrait->GetLastStatus() == Gdiplus::Ok) {
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		graphics.DrawImage(portrait, portraitRect);
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	} else {
		Gdiplus::Font emptyFont(L"Microsoft YaHei", 9.0f);
		Gdiplus::StringFormat centered;
		centered.SetAlignment(Gdiplus::StringAlignmentCenter);
		centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		graphics.DrawString(L"无头像", -1, &emptyFont,
			Gdiplus::RectF(static_cast<float>(portraitRect.X),
				static_cast<float>(portraitRect.Y),
				static_cast<float>(portraitRect.Width),
				static_cast<float>(portraitRect.Height)), &centered, &bodyBrush);
	}
	graphics.DrawRectangle(&portraitBorder, portraitRect);

	const int textLeft = portraitRect.X + portraitRect.Width + 12;
	const int maxLines = (cardY + cardHeight - 10 - (cardY + 44)) / lineHeight;
	for (std::size_t index = 0;
		index < lines.size() && index < static_cast<std::size_t>(maxLines); ++index) {
		const Gdiplus::RectF bounds(static_cast<float>(textLeft),
			static_cast<float>(cardY + 44 + static_cast<int>(index) * lineHeight),
			static_cast<float>(cardX + cardWidth - textLeft - 8),
			static_cast<float>(lineHeight));
		graphics.DrawString(lines[index].c_str(), -1, &bodyFont, bounds,
			&format, &bodyBrush);
	}
	graphics.Restore(saved);
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
	const auto& state = ActiveSession().editor_state;
	std::vector<std::uint64_t> selectedCacheIds;
	selectedCacheIds.reserve(state.selection_group.size());
	for (const auto& selected : state.selection_group)
		selectedCacheIds.push_back(selected.cache_id);
	std::sort(selectedCacheIds.begin(), selectedCacheIds.end());
	const auto is_group_selected = [&selectedCacheIds](std::uint64_t cache_id) {
		return std::binary_search(selectedCacheIds.begin(),
			selectedCacheIds.end(), cache_id);
	};
	const std::size_t mapCells = projection
		? static_cast<std::size_t>(state.battle.map.width) *
			static_cast<std::size_t>(state.battle.map.height)
		: 0;
	std::vector<std::uint8_t> factionMasks(mapCells, 0);
	std::vector<std::uint16_t> unitCounts(mapCells, 0);
	std::vector<const ccz::preview::BattleUnit*> visibleUnits;
	visibleUnits.reserve(state.battle.units.size());
	const auto visible_in_scope = [&state](
		const ccz::preview::BattleUnit& unit) {
		return unit.fields_decoded && !unit.editor_pending_delete &&
			(unit.editor_staged_new || in_scope(unit.source_key, state.scope_root)) &&
			!ccz::editor_preview::IsBattleFixedEmptySlot(unit) &&
			ccz::preview::is_battle_coord_in_bounds(unit.position,
				state.battle.map.width, state.battle.map.height);
	};
	int decoded = 0;
	int ownFactionCount = 0;
	int friendlyFactionCount = 0;
	int enemyFactionCount = 0;
	if (projection) for (std::size_t i = 0;
		i < ActiveSession().editor_state.battle.units.size(); ++i) {
		const auto& unit = ActiveSession().editor_state.battle.units[i];
		const bool moving_group_member =
			ActiveSession().editor_state.mode ==
				ccz::editor_preview::BattleEditorMode::AdjustPosition &&
				(ActiveSession().editor_state.selection_group.size() > 1
					? is_group_selected(unit.editor_cache_id)
					: unit.editor_cache_id ==
						ActiveSession().editor_state.adjusting_cache_id);
		if (!visible_in_scope(unit) || moving_group_member) continue;
		++decoded;
		const auto deployment = battle_unit_deployment(unit);
		if (deployment == 0x4b) ++ownFactionCount;
		else if (deployment == 0x46) ++friendlyFactionCount;
		else if (deployment == 0x47) ++enemyFactionCount;
		const std::size_t cellIndex = static_cast<std::size_t>(
			unit.position.y * state.battle.map.width + unit.position.x);
		if (cellIndex < mapCells) {
			++unitCounts[cellIndex];
			const std::uint8_t factionBit = deployment == 0x4b ? 0x1
				: deployment == 0x47 ? 0x2
				: deployment == 0x46 ? 0x4 : 0;
			factionMasks[cellIndex] |= factionBit;
		}
		visibleUnits.push_back(&unit);
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
	}
	if (projection) {
		const struct FactionOutline { std::uint8_t bit; std::uint16_t instruction; }
			outlines[] = {{0x1, 0x4b}, {0x2, 0x47}, {0x4, 0x46}};
		for (int y = 0; y < state.battle.map.height; ++y) {
			for (int x = 0; x < state.battle.map.width; ++x) {
				const std::size_t index = static_cast<std::size_t>(
					y * state.battle.map.width + x);
				if (index >= factionMasks.size() || factionMasks[index] == 0) continue;
				int layer = 0;
				for (const auto& outline : outlines) {
					if ((factionMasks[index] & outline.bit) == 0) continue;
					const float inset = 2.0f + layer * 2.5f;
					const float widthOnCell = (std::max)(2.0f, cell - inset * 2.0f);
					Gdiplus::Pen pen(battle_faction_color(outline.instruction),
						(std::max)(1.2f, m_zoom * 1.6f));
					graphics.DrawRectangle(&pen,
						Gdiplus::RectF(ox + x * cell + inset,
							oy + y * cell + inset, widthOnCell, widthOnCell));
					++layer;
				}
			}
		}
	}
	const auto previewMode = ActiveSession().ui.parameter_preview_mode;
	if (projection && previewMode ==
		ccz::editor_preview::BattleParameterPreviewMode::AiPolicy) {
		std::unordered_map<int, std::vector<const ccz::preview::BattleUnit*>> byDataId;
		byDataId.reserve(visibleUnits.size());
		for (const auto* unit : visibleUnits) {
			if (unit->data_id >= 0) byDataId[unit->data_id].push_back(unit);
		}
		for (const auto* unit : visibleUnits) {
			if (!unit_has_battle_role(*unit,
					ccz::preview::BattleFieldRole::AiPolicy)) continue;
			ccz::preview::BattleCoord target{};
			bool resolved = false;
			if (unit->ai_policy == 3 || unit->ai_policy == 5) {
				const auto found = byDataId.find(unit->target);
				if (found != byDataId.end() && found->second.size() == 1) {
					target = found->second.front()->position;
					resolved = true;
				}
			} else if ((unit->ai_policy == 4 || unit->ai_policy == 6) &&
				unit_has_battle_role(*unit,
					ccz::preview::BattleFieldRole::TargetX) &&
				unit_has_battle_role(*unit,
					ccz::preview::BattleFieldRole::TargetY)) {
				target = {unit->target_x, unit->target_y};
				resolved = ccz::preview::is_battle_coord_in_bounds(target,
					state.battle.map.width, state.battle.map.height);
			}
			if (!resolved) continue;
			Gdiplus::Pen targetLine(battle_faction_color(
				battle_unit_deployment(*unit), 185), 1.6f);
			targetLine.SetDashStyle(Gdiplus::DashStyleDash);
			const Gdiplus::PointF from(ox + (unit->position.x + 0.5f) * cell,
				oy + (unit->position.y + 0.5f) * cell);
			const Gdiplus::PointF to(ox + (target.x + 0.5f) * cell,
				oy + (target.y + 0.5f) * cell);
			graphics.DrawLine(&targetLine, from, to);
			const float markerSize = (std::max)(6.0f, cell * 0.34f);
			Gdiplus::Pen targetMarker(battle_faction_color(
				battle_unit_deployment(*unit)), (std::max)(1.2f, m_zoom * 1.8f));
			graphics.DrawEllipse(&targetMarker,
				to.X - markerSize * 0.5f, to.Y - markerSize * 0.5f,
				markerSize, markerSize);
		}
	}
	if (projection && previewMode !=
		ccz::editor_preview::BattleParameterPreviewMode::None) {
		Gdiplus::Font badgeFont(L"Microsoft YaHei",
			(std::max)(7.0f, (std::min)(10.0f, cell * 0.20f)),
			Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::SolidBrush badgeFill(Gdiplus::Color(220, 22, 24, 28));
		Gdiplus::SolidBrush badgeText(Gdiplus::Color(255, 255, 255, 245));
		Gdiplus::StringFormat centered;
		centered.SetAlignment(Gdiplus::StringAlignmentCenter);
		centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		centered.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		for (const auto* unit : visibleUnits) {
			const std::size_t cellIndex = static_cast<std::size_t>(
				unit->position.y * state.battle.map.width + unit->position.x);
			if (cellIndex >= unitCounts.size() || unitCounts[cellIndex] != 1) continue;
			std::wstring label;
			using Mode = ccz::editor_preview::BattleParameterPreviewMode;
			switch (previewMode) {
			case Mode::BattlefieldIndex: {
				const int index = predicted_battlefield_index(state, *unit);
				label = index >= 0 ? L"#" + std::to_wstring(index) : L"#?";
				break;
			}
			case Mode::Level:
				if (!unit_has_battle_role(*unit,
						ccz::preview::BattleFieldRole::Level)) continue;
				label = (unit->level >= 0 ? L"+" : L"") +
					std::to_wstring(unit->level) + L"级";
				break;
			case Mode::UnitType:
				if (!unit_has_battle_role(*unit,
						ccz::preview::BattleFieldRole::UnitType)) continue;
				label = battle_unit_type_name(unit->unit_type);
				break;
			case Mode::AiPolicy: {
				if (!unit_has_battle_role(*unit,
						ccz::preview::BattleFieldRole::AiPolicy)) continue;
				static constexpr const wchar_t* shortPolicies[] = {
					L"被", L"主", L"坚", L"攻", L"到", L"跟", L"逃"};
				label = unit->ai_policy >= 0 && unit->ai_policy < 7
					? shortPolicies[unit->ai_policy] : L"?";
				break;
			}
			case Mode::Reinforcement:
				if (!unit_has_battle_role(*unit,
						ccz::preview::BattleFieldRole::Reinforcement)) continue;
				label = unit->reinforcement ? L"援" : L"常";
				break;
			case Mode::None:
			default: continue;
			}
			const float badgeWidth = (std::max)(12.0f, cell - 6.0f);
			const float badgeHeight = (std::max)(12.0f,
				(std::min)(18.0f, cell * 0.34f));
			const Gdiplus::RectF badgeRect(
				ox + unit->position.x * cell + (cell - badgeWidth) * 0.5f,
				oy + unit->position.y * cell + 2.0f,
				badgeWidth, badgeHeight);
			Gdiplus::Pen badgeBorder(battle_faction_color(
				battle_unit_deployment(*unit)), 1.0f);
			graphics.FillRectangle(&badgeFill, badgeRect);
			graphics.DrawRectangle(&badgeBorder, badgeRect);
			graphics.DrawString(label.c_str(), -1, &badgeFont,
				badgeRect, &centered, &badgeText);
		}
	}
	for (const auto* unit : visibleUnits) {
		const bool selected = m_selectedUnit.has_value() &&
			*m_selectedUnit < state.battle.units.size() &&
			&state.battle.units[*m_selectedUnit] == unit;
		if (!selected && !is_group_selected(unit->editor_cache_id)) continue;
		const float cx = ox + (unit->position.x + 0.5f) * cell;
		const float by = oy + (unit->position.y + 1.0f) * cell;
		Gdiplus::Pen selection(selected
			? Gdiplus::Color(255, 255, 220, 40)
			: Gdiplus::Color(255, 195, 90, 255), 2.0f);
		graphics.DrawRectangle(&selection,
			static_cast<int>(cx - cell * 0.46f),
			static_cast<int>(by - cell * 0.96f),
			(std::max)(1, static_cast<int>(cell * 0.92f)),
			(std::max)(1, static_cast<int>(cell * 0.92f)));
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
		if (!placement.valid && adjusting) {
			Gdiplus::Pen invalidTarget(Gdiplus::Color(230, 235, 45, 45), 2.0f);
			graphics.DrawRectangle(&invalidTarget,
				static_cast<int>(ox + m_hoverCell->x * cell),
				static_cast<int>(oy + m_hoverCell->y * cell),
				(std::max)(1, static_cast<int>(cell)),
				(std::max)(1, static_cast<int>(cell)));
		} else if (placement.valid) {
			const auto draw_ghost = [&](const ccz::preview::BattleUnit& unit) {
				const float centerX = ox + (unit.position.x + 0.5f) * cell;
				const float baseY = oy + (unit.position.y + 1.0f) * cell;
				const int size = (std::max)(8, static_cast<int>(
					std::lround(kCellPixels * m_zoom)));
				ccz::editor_preview::BattleAssetCache::UnitSpriteRef spriteRef;
				Gdiplus::Bitmap* sprite = GetUnitSprite(
					unit, unit.direction, &spriteRef);
				if (sprite && sprite->GetLastStatus() == Gdiplus::Ok) {
					Gdiplus::ColorMatrix ghostMatrix = {
						1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
						0,0,0,0.55f,0, 0,0,0,0,1};
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
				} else if (ccz::preview::is_battle_deployment_slot_marker(unit)) {
					draw_deployment_slot_marker(graphics, centerX, baseY, cell,
						unit.deployment_index, 0.55f, unit.hidden);
				}
			};
			if (adjusting) {
				for (const auto& unit : placement.units) draw_ghost(unit);
			} else {
				draw_ghost(placement.unit);
			}
		}
	}
	if (projection && m_hoverCell && !m_panActive)
		DrawHoveredUnitCard(graphics, *m_hoverCell, m_hoverPoint, cell);
	Gdiplus::SolidBrush banner(Gdiplus::Color(220, 20, 20, 20));
	Gdiplus::Font font(L"Microsoft YaHei", 9.0f);
	Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 225, 170));
	CString title;
	if (projection) title.Format(
		_T("S 战场预览 地图:%d 网格:%dx%d 全单位 缩放:%.2fx 已解析：总%d"),
		ActiveSession().editor_state.battle.map.map_id,
		ActiveSession().editor_state.battle.map.width, ActiveSession().editor_state.battle.map.height,
		m_zoom, decoded);
	else title.Format(_T("S 战场预览 地图:%d 网格投影未闭合 缩放:%.2fx"),
		ActiveSession().editor_state.battle.map.map_id, m_zoom);
	graphics.SetClip(Gdiplus::Rect(0, 0, width, height),
		Gdiplus::CombineModeReplace);
	graphics.FillRectangle(&banner, m_canvasRect.left, 0,
		m_canvasRect.Width(), m_canvasRect.top);
	if (!m_sidebarVisible && m_canvasRect.top > kStatusHeaderHeight) {
		Gdiplus::SolidBrush toolbarBackground(
			Gdiplus::Color(255, 36, 36, 36));
		Gdiplus::Pen toolbarDivider(Gdiplus::Color(255, 70, 70, 70), 1.0f);
		graphics.FillRectangle(&toolbarBackground, m_canvasRect.left,
			kStatusHeaderHeight, m_canvasRect.Width(),
			kCollapsedControlToolbarHeight);
		graphics.DrawLine(&toolbarDivider, m_canvasRect.left,
			kStatusHeaderHeight, m_canvasRect.right,
			kStatusHeaderHeight);
	}
	const Gdiplus::PointF titleOrigin(
		static_cast<float>(m_canvasRect.left + 8), 6.0f);
	graphics.DrawString(title, -1, &font, titleOrigin, &text);
	if (projection) {
		Gdiplus::RectF titleBounds;
		graphics.MeasureString(title, -1, &font, titleOrigin, &titleBounds);
		float countX = titleOrigin.X + titleBounds.Width + 6.0f;
		const auto drawFactionCount = [&](const wchar_t* label, int count,
			std::uint16_t instruction) {
			const std::wstring segment = std::wstring(label) +
				std::to_wstring(count);
			Gdiplus::SolidBrush factionText(battle_faction_color(instruction));
			const Gdiplus::PointF origin(countX, titleOrigin.Y);
			graphics.DrawString(segment.c_str(), -1, &font, origin, &factionText);
			Gdiplus::RectF bounds;
			graphics.MeasureString(segment.c_str(), -1, &font, origin, &bounds);
			countX += bounds.Width + 6.0f;
		};
		drawFactionCount(L"我", ownFactionCount, 0x4b);
		drawFactionCount(L"友", friendlyFactionCount, 0x46);
		drawFactionCount(L"敌", enemyFactionCount, 0x47);
	}
	if (m_hoverCell) {
		static const wchar_t* const terrain_names[] = {
			L"平原", L"草原", L"树林", L"荒地", L"山地", L"岩山",
			L"山崖", L"雪原", L"桥梁", L"浅滩", L"沼泽", L"池塘",
			L"小河", L"大河", L"栅栏", L"城墙", L"城内", L"城门",
			L"城池", L"关隘", L"鹿柴", L"村落", L"兵营", L"居民",
			L"宝库", L"水池", L"火焰", L"船", L"祭坛", L"地下"};
		const auto& battle = ActiveSession().editor_state.battle;
		const std::size_t terrain_index = static_cast<std::size_t>(
			m_hoverCell->y * battle.map.width + m_hoverCell->x);
		std::wstring terrain_name = L"未知";
		if (terrain_index < battle.terrain.size() &&
			battle.terrain[terrain_index] < std::size(terrain_names)) {
			terrain_name = terrain_names[battle.terrain[terrain_index]];
		}
		const std::wstring coordinate_status =
			L"坐标（" + std::to_wstring(m_hoverCell->x) +
			L"，" + std::to_wstring(m_hoverCell->y) + L"），";
		const std::wstring terrain_status = L"地形：" + terrain_name + L"；";
		Gdiplus::RectF coordinateBounds;
		Gdiplus::RectF terrainBounds;
		const Gdiplus::PointF statusOrigin(
			static_cast<float>(m_canvasRect.left + 8), 30.0f);
		Gdiplus::SolidBrush coordinateText(Gdiplus::Color(255, 125, 211, 252));
		Gdiplus::SolidBrush terrainText(Gdiplus::Color(255, 134, 239, 172));
		graphics.DrawString(coordinate_status.c_str(), -1, &font,
			statusOrigin, &coordinateText);
		graphics.MeasureString(coordinate_status.c_str(), -1, &font,
			statusOrigin, &coordinateBounds);
		const Gdiplus::PointF terrainOrigin(
			statusOrigin.X + coordinateBounds.Width, statusOrigin.Y);
		graphics.DrawString(terrain_status.c_str(), -1, &font,
			terrainOrigin, &terrainText);
		graphics.MeasureString(terrain_status.c_str(), -1, &font,
			terrainOrigin, &terrainBounds);
		const auto hovered_index = HitTestUnit(*m_hoverCell);
		if (hovered_index && *hovered_index < battle.units.size()) {
			const auto& unit = battle.units[*hovered_index];
			const std::uint16_t instruction = battle_unit_deployment(unit);
			const int battlefield_index = predicted_battlefield_index(
				ActiveSession().editor_state, unit);
			std::wstring identity_label;
			std::wstring label_body;
			if (instruction == 0x4b) {
				const auto label = own_battle_unit_status_label(battlefield_index);
				identity_label = label.identity;
				label_body = label.body;
			} else {
				const auto& characters = ActiveSession().editor_state.characters.entries;
				const auto character = std::find_if(characters.begin(), characters.end(),
					[&unit](const auto& entry) {
						return entry.data_id == unit.data_id;
					});
				const std::wstring character_name = character != characters.end() && character->has_name
					? character->name : L"Data " + std::to_wstring(unit.data_id);
				identity_label = L"【" + std::to_wstring(unit.data_id) + L"】";
				label_body = (battlefield_index >= 0
					? std::to_wstring(battlefield_index) : std::wstring(L"待分配"))
					+ L"." + character_name;
			}
			Gdiplus::SolidBrush factionText(instruction == 0x4b
				? Gdiplus::Color(255, 255, 80, 80)
				: instruction == 0x47
					? Gdiplus::Color(255, 100, 165, 255)
					: Gdiplus::Color(255, 255, 165, 55));
			const Gdiplus::PointF hoverOrigin(
				statusOrigin.X + coordinateBounds.Width + terrainBounds.Width,
				statusOrigin.Y);
			Gdiplus::RectF dataIdentityBounds;
			Gdiplus::RectF ownIdentityBounds;
			graphics.MeasureString(L"【1023】", -1, &font,
				hoverOrigin, &dataIdentityBounds);
			graphics.MeasureString(L"【我军】", -1, &font,
				hoverOrigin, &ownIdentityBounds);
			const float identity_column_width = (std::max)(
				dataIdentityBounds.Width, ownIdentityBounds.Width);
			constexpr float identity_gap = 4.0f;
			graphics.DrawString(identity_label.c_str(), -1, &font,
				hoverOrigin, &factionText);
			graphics.DrawString(label_body.c_str(), -1, &font,
				Gdiplus::PointF(hoverOrigin.X + identity_column_width +
					identity_gap, hoverOrigin.Y), &factionText);
		}
	}
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
		graphics.SetClip(Gdiplus::Rect(m_canvasRect.left,
			m_canvasRect.bottom, m_canvasRect.Width(),
			(std::max)(0, height - static_cast<int>(m_canvasRect.bottom))),
			Gdiplus::CombineModeReplace);
		graphics.FillRectangle(&panel, m_canvasRect.left,
			m_canvasRect.bottom, m_canvasRect.Width(),
			(std::max)(0, height - static_cast<int>(m_canvasRect.bottom)));
		graphics.DrawString(visibleDiagnostic, -1, &font,
			Gdiplus::PointF(static_cast<float>(m_canvasRect.left + 8),
				static_cast<float>(m_canvasRect.bottom + 4)), &text);
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
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::Select) {
		m_selectionGestureActive = true;
		m_selectionDragStarted = false;
		m_selectionPressPoint = point;
		m_selectionPressCell = coordinate;
		m_selectionLastCell = coordinate;
		SetCapture();
		const auto result = m_editorOps->ToggleSelectionAt(*coordinate);
		if (result.ok && result.changed)
			ShowSelectionOperationResult(result);
		return;
	}
	m_selectedUnit = HitTestUnit(*coordinate);
	if (m_selectedUnit && m_editorOps) {
		ShowOperationResult(m_editorOps->SelectAt(*coordinate));
	} else {
		ActiveSession().editor_state.selection.reset();
		ActiveSession().editor_state.selection_group.clear();
		Invalidate(FALSE);
	}
}

void CSBattlePreviewPage::OnLButtonUp(UINT, CPoint point)
{
	if (m_selectionGestureActive) {
		if (!m_editorOps) {
			m_selectionGestureActive = false;
			m_selectionDragStarted = false;
			m_selectionPressCell.reset();
			m_selectionLastCell.reset();
			m_selectionPressPoint = CPoint(-1, -1);
			if (GetCapture() == this) ReleaseCapture();
			Invalidate(FALSE);
			return;
		}
		const auto coordinate = BattleCoordFromScreen(point);
		const int dx = std::abs(point.x - m_selectionPressPoint.x);
		const int dy = std::abs(point.y - m_selectionPressPoint.y);
		if (!m_selectionDragStarted && m_selectionPressCell &&
			(dx >= (std::max)(1, GetSystemMetrics(SM_CXDRAG)) ||
			dy >= (std::max)(1, GetSystemMetrics(SM_CYDRAG)))) {
			m_selectionDragStarted = true;
			m_editorOps->ClearSelectionGroup();
			m_selectedUnit.reset();
			m_selectionLastCell = m_selectionPressCell;
		}
		if (m_selectionDragStarted && coordinate && m_selectionLastCell) {
			const auto path = ccz::editor_preview::BattleSelectionPathCells(
				*m_selectionLastCell, *coordinate);
			for (const auto& crossed : path)
				m_editorOps->AddSelectionAt(crossed);
			m_selectionLastCell = coordinate;
		}
		const bool was_drag = m_selectionDragStarted;
		m_selectionGestureActive = false;
		m_selectionDragStarted = false;
		m_selectionPressCell.reset();
		m_selectionLastCell.reset();
		m_selectionPressPoint = CPoint(-1, -1);
		if (GetCapture() == this) ReleaseCapture();
		if (was_drag) Invalidate(FALSE);
		return;
	}
	if (!m_leftStampActive) return;
	m_leftStampActive = false;
	m_lastStampCell.reset();
	if (GetCapture() == this) ReleaseCapture();
	RefreshCharacterListIfDeploymentChanged();
}

void CSBattlePreviewPage::OnLButtonDblClk(UINT, CPoint point)
{
	if (!m_editorOps || ActiveSession().editor_state.mode !=
		ccz::editor_preview::BattleEditorMode::Select) return;
	const auto coordinate = BattleCoordFromScreen(point);
	const auto& units = ActiveSession().editor_state.battle.units;
	const bool occupied = coordinate && std::any_of(units.begin(), units.end(),
		[this, &coordinate](const auto& unit) {
			return unit.fields_decoded && !unit.editor_pending_delete &&
				(unit.editor_staged_new || in_scope(unit.source_key,
					ActiveSession().editor_state.scope_root)) &&
				!ccz::editor_preview::IsBattleFixedEmptySlot(unit) &&
				unit.position == *coordinate;
		});
	if (coordinate && !occupied) {
		ShowOperationResult(m_editorOps->ClearSelectionGroup());
		RefreshSelectedUnitIndex();
		Invalidate(FALSE);
	}
}

void CSBattlePreviewPage::OnRButtonDown(UINT, CPoint point)
{
	SetFocus();
	m_hoverPoint = point;
	if (m_editorOps && ActiveSession().editor_state.mode ==
		ccz::editor_preview::BattleEditorMode::Place) {
		m_leftStampActive = false;
		m_lastStampCell.reset();
		if (GetCapture() == this) ReleaseCapture();
		ShowOperationResult(m_editorOps->CancelPlacementBrush());
		RefreshCharacterListIfDeploymentChanged();
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
	CMenu menu;
	if (!menu.CreatePopupMenu()) return;
	constexpr UINT kAdjustPositionCommand = 1;
	constexpr UINT kDeleteUnitCommand = 2;
	constexpr UINT kModifyUnitParametersCommand = 3;
	constexpr UINT kFilterOwnCommand = 10;
	constexpr UINT kFilterAllyCommand = 11;
	constexpr UINT kFilterEnemyCommand = 12;
	constexpr UINT kRemoveSelectionCommand = 13;
	constexpr UINT kClearSelectionCommand = 14;
	constexpr UINT kAdjustGroupCommand = 15;
	constexpr UINT kDeleteGroupCommand = 16;
	constexpr UINT kModifyGroupParametersCommand = 17;
	const auto& state = ActiveSession().editor_state;
	const std::uint64_t cacheId = state.battle.units[*hit].editor_cache_id;
	const bool in_selection_group = std::any_of(
		state.selection_group.begin(), state.selection_group.end(),
		[cacheId](const auto& selected) {
			return selected.cache_id == cacheId;
		});
	const bool group_context = state.selection_group.size() > 1 &&
		in_selection_group;
	if (group_context) {
		CMenu filterMenu;
		if (!filterMenu.CreatePopupMenu()) return;
		filterMenu.AppendMenu(MF_STRING, kFilterOwnCommand, _T("仅选我军"));
		filterMenu.AppendMenu(MF_STRING, kFilterAllyCommand, _T("仅选友军"));
		filterMenu.AppendMenu(MF_STRING, kFilterEnemyCommand, _T("仅选敌军"));
		menu.AppendMenu(MF_POPUP,
			reinterpret_cast<UINT_PTR>(filterMenu.GetSafeHmenu()),
			_T("按阵营筛选"));
		filterMenu.Detach();
		menu.AppendMenu(MF_STRING, kRemoveSelectionCommand,
			_T("取消选择该角色"));
		menu.AppendMenu(MF_STRING, kClearSelectionCommand,
			_T("取消全部选择"));
		menu.AppendMenu(MF_SEPARATOR);
		CString batchEditLabel;
		batchEditLabel.Format(_T("批量修改参数（%u）"),
			static_cast<unsigned>(state.selection_group.size()));
		menu.AppendMenu(MF_STRING, kModifyGroupParametersCommand,
			batchEditLabel);
		menu.AppendMenu(MF_STRING, kAdjustGroupCommand, _T("批量调整位置"));
		menu.AppendMenu(MF_STRING, kDeleteGroupCommand, _T("批量删除"));
	} else {
		ShowOperationResult(m_editorOps->SelectAt(*coordinate));
		menu.AppendMenu(MF_STRING, kModifyUnitParametersCommand,
			_T("修改单位参数"));
		menu.AppendMenu(MF_STRING, kAdjustPositionCommand, _T("调整位置"));
		menu.AppendMenu(MF_STRING, kDeleteUnitCommand, _T("删除"));
	}
	CPoint screenPoint = point;
	ClientToScreen(&screenPoint);
	const UINT command = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		screenPoint.x, screenPoint.y, this);
	if (group_context && command == kFilterOwnCommand) {
		ShowOperationResult(m_editorOps->FilterSelectionByDeployment(0x4b));
	} else if (group_context && command == kFilterAllyCommand) {
		ShowOperationResult(m_editorOps->FilterSelectionByDeployment(0x46));
	} else if (group_context && command == kFilterEnemyCommand) {
		ShowOperationResult(m_editorOps->FilterSelectionByDeployment(0x47));
	} else if (group_context && command == kRemoveSelectionCommand) {
		ShowOperationResult(m_editorOps->RemoveSelectionAt(*coordinate));
	} else if (group_context && command == kClearSelectionCommand) {
		ShowOperationResult(m_editorOps->ClearSelectionGroup());
	} else if (group_context && command == kModifyGroupParametersCommand) {
		OpenBatchUnitParameters();
	} else if (group_context && command == kAdjustGroupCommand) {
		ShowOperationResult(m_editorOps->BeginPositionAdjustment(cacheId));
	} else if (group_context && command == kDeleteGroupCommand) {
		ShowOperationResult(m_editorOps->DeleteSelectionGroup());
		RefreshCharacterListIfDeploymentChanged();
	} else if (!group_context && command == kModifyUnitParametersCommand) {
		OpenSingleUnitParameters(cacheId);
	} else if (!group_context && command == kAdjustPositionCommand) {
		ShowOperationResult(m_editorOps->BeginPositionAdjustment(cacheId));
	} else if (!group_context && command == kDeleteUnitCommand) {
		ShowOperationResult(m_editorOps->DeleteAt(*coordinate));
		RefreshCharacterListIfDeploymentChanged();
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
	const bool interruptedStamp = m_leftStampActive;
	const bool interruptedSelection = m_selectionGestureActive;
	if (m_leftStampActive) {
		m_leftStampActive = false;
		m_lastStampCell.reset();
	}
	if (m_selectionGestureActive) {
		m_selectionGestureActive = false;
		m_selectionDragStarted = false;
		m_selectionPressCell.reset();
		m_selectionLastCell.reset();
		m_selectionPressPoint = CPoint(-1, -1);
	}
	if (m_panActive) {
		m_panActive = false;
		m_panStartPoint = CPoint(-1, -1);
	}
	if (interruptedStamp)
		RefreshCharacterListIfDeploymentChanged();
	if (interruptedSelection) Invalidate(FALSE);
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
	RefreshCharacterList();
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
	RefreshCharacterList();
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnBrushTab()
{
	m_brushTabActive = true;
	RefreshCharacterList();
	RefreshEditorControls();
}

void CSBattlePreviewPage::OnPreviousCharacterPage()
{
	if (!m_editorOps || m_brushTabActive ||
		ActiveSession().editor_state.characters.current_page == 0) return;
	const auto result = m_editorOps->SetCharacterPage(
		ActiveSession().editor_state.characters.current_page - 1);
	ShowOperationResult(result);
	RefreshCharacterList();
}

void CSBattlePreviewPage::OnNextCharacterPage()
{
	if (!m_editorOps || m_brushTabActive) return;
	const auto result = m_editorOps->SetCharacterPage(
		ActiveSession().editor_state.characters.current_page + 1);
	ShowOperationResult(result);
	RefreshCharacterList();
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
		RefreshCharacterListIfDeploymentChanged();
		RefreshEditorControls();
		Invalidate(FALSE);
		return;
	}
	if (m_editorOps) {
		ShowOperationResult(m_editorOps->Undo());
		RefreshCharacterListIfDeploymentChanged();
	}
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

void CSBattlePreviewPage::OnCopyPersonModeClicked()
{
	if (m_restoringSessionUi || m_activeSession == nullptr) return;
	const bool enabled = m_copyPersonModeButton.GetCheck() == BST_CHECKED;
	ActiveSession().ui.copy_person_mode = enabled;
	ActiveSession().editor_state.copy_person_mode = enabled;
	if (m_editorOps != nullptr) m_editorOps->SetCopyPersonMode(enabled);
	for (auto& workspace : ActiveSession().condition_workspaces)
		workspace.second->state.copy_person_mode = enabled;
	RefreshEditorControls();
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnUnrestrictedTerrainModeClicked()
{
	if (m_restoringSessionUi || m_activeSession == nullptr) return;
	const bool enabled =
		m_unrestrictedTerrainModeButton.GetCheck() == BST_CHECKED;
	ActiveSession().ui.unrestricted_terrain_mode = enabled;
	ActiveSession().editor_state.unrestricted_terrain_mode = enabled;
	if (m_editorOps != nullptr)
		m_editorOps->SetUnrestrictedTerrainMode(enabled);
	for (auto& workspace : ActiveSession().condition_workspaces)
		workspace.second->state.unrestricted_terrain_mode = enabled;
	RefreshEditorControls();
	Invalidate(FALSE);
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

void CSBattlePreviewPage::OnMButtonUp(UINT, CPoint point)
{
	if (!m_panActive) return;
	m_panActive = false;
	m_panStartPoint = CPoint(-1, -1);
	ReleaseCapture();
	SetCursor(LoadCursor(nullptr, IDC_ARROW));
	m_hoverPoint = point;
	const auto cell = BattleCoordFromScreen(point);
	if (cell != m_hoverCell) m_hoverCell = cell;
	Invalidate(FALSE);
}

void CSBattlePreviewPage::OnMouseMove(UINT flags, CPoint point)
{
	if (!m_trackingMouseLeave) {
		TRACKMOUSEEVENT tracking{};
		tracking.cbSize = sizeof(tracking);
		tracking.dwFlags = TME_LEAVE;
		tracking.hwndTrack = GetSafeHwnd();
		m_trackingMouseLeave = ::TrackMouseEvent(&tracking) != FALSE;
	}
	if (m_panActive) {
		SetViewportScroll(m_panStartScrollX -
			(point.x - m_panStartPoint.x), m_panStartScrollY -
			(point.y - m_panStartPoint.y));
		SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
		return;
	}
	m_hoverPoint = point;
	const auto cell = BattleCoordFromScreen(point);
	if (m_selectionGestureActive && (flags & MK_LBUTTON) && m_editorOps &&
		m_selectionPressCell) {
		const int dx = std::abs(point.x - m_selectionPressPoint.x);
		const int dy = std::abs(point.y - m_selectionPressPoint.y);
		if (!m_selectionDragStarted &&
			(dx >= (std::max)(1, GetSystemMetrics(SM_CXDRAG)) ||
			 dy >= (std::max)(1, GetSystemMetrics(SM_CYDRAG)))) {
			m_selectionDragStarted = true;
			m_editorOps->ClearSelectionGroup();
			m_selectedUnit.reset();
			m_selectionLastCell = m_selectionPressCell;
		}
		if (m_selectionDragStarted && cell && m_selectionLastCell) {
			const auto path = ccz::editor_preview::BattleSelectionPathCells(
				*m_selectionLastCell, *cell);
			for (const auto& crossed : path)
				m_editorOps->AddSelectionAt(crossed);
			m_selectionLastCell = cell;
			Invalidate(FALSE);
		}
	}
	if (m_leftStampActive && cell) StampAt(*cell);
	if (cell != m_hoverCell) {
		m_hoverCell = cell;
		Invalidate(FALSE);
	}
	SetCursor(cell ? LoadCursor(nullptr, IDC_HAND)
		: LoadCursor(nullptr, IDC_ARROW));
}

void CSBattlePreviewPage::OnMouseLeave()
{
	m_trackingMouseLeave = false;
	m_hoverPoint = CPoint(-1, -1);
	m_hoverCell.reset();
	Invalidate(FALSE);
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
