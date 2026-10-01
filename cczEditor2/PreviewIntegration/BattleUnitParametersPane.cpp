#include "BattleUnitParametersPane.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cwchar>
#include <iterator>
#include <limits>
#include <utility>
#include <string>

namespace {

constexpr wchar_t kDirectionNames[][16] = {
	L"默认方向", L"朝北", L"朝东", L"朝南", L"朝西"};
constexpr wchar_t kPolicyNames[][20] = {
	L"被动出击", L"主动出击", L"坚守原地", L"攻击武将",
	L"到指定点", L"跟随武将", L"逃到指定点"};
constexpr wchar_t kUnitTypeNames[][12] = {
	L"初级", L"中级", L"高级"};

void add_choice(CComboBox& combo, const wchar_t* label, int value)
{
	const int index = combo.AddString(label);
	if (index != CB_ERR && index != CB_ERRSPACE)
		combo.SetItemData(index, static_cast<DWORD_PTR>(value));
}

void populate_target_choices(CMyComboBox& combo,
	const std::vector<ccz::editor_preview::BattleCharacterEntry>& characters,
	int selected_value, std::vector<int>& data_ids)
{
	combo.ResetContent();
	data_ids.clear();
	const auto add_target = [&combo, &data_ids](const wchar_t* label, int value) {
		const int index = combo.AddString(label);
		if (index == CB_ERR || index == CB_ERRSPACE) return index;
		if (index >= static_cast<int>(data_ids.size()))
			data_ids.resize(static_cast<std::size_t>(index + 1), INT_MIN);
		data_ids[static_cast<std::size_t>(index)] = value;
		return index;
	};
	add_target(L"无目标", -1);
	int selected_index = selected_value == -1 ? 0 : CB_ERR;
	for (const auto& character : characters) {
		CString label;
		label.Format(L"%d.%s", character.data_id, character.name.c_str());
		const int index = add_target(label, character.data_id);
		if (index == CB_ERR || index == CB_ERRSPACE) continue;
		if (character.data_id == selected_value) selected_index = index;
	}
	if (selected_index == CB_ERR) {
		CString label;
		label.Format(L"%d.未载入", selected_value);
		selected_index = add_target(label, selected_value);
	}
	combo.EnablePersonSearch();
	if (selected_index != CB_ERR && selected_index != CB_ERRSPACE)
		combo.SetCurSel(selected_index);
}

bool unit_has_role(const ccz::preview::BattleUnit& unit,
	ccz::preview::BattleFieldRole role)
{
	const auto* spec = ccz::preview::get_battle_field_spec(
		unit.editor_deployment_instruction != 0xffff
			? unit.editor_deployment_instruction : unit.source_instruction);
	return spec != nullptr && spec->semantic_complete &&
		std::find(spec->field_roles.begin(), spec->field_roles.end(), role) !=
			spec->field_roles.end();
}

bool key_in_scope(const ccz::preview::LogicalNodeKey& key,
	const ccz::preview::LogicalNodeKey& scope)
{
	if (key.empty() || scope.empty()) return false;
	if (key == scope) return true;
	if (key.size() <= scope.size() || key.compare(0, scope.size(), scope) != 0)
		return false;
	const wchar_t separator = key[scope.size()];
	return separator == L'|' || separator == L'/';
}

}  // namespace

BEGIN_MESSAGE_MAP(CBattleUnitParametersPane, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_BN_CLICKED(kApplyDefaultsId, &CBattleUnitParametersPane::OnApplyDefaults)
	ON_BN_CLICKED(kResetDefaultsId, &CBattleUnitParametersPane::OnResetDefaults)
	ON_BN_CLICKED(kApplyUnitId, &CBattleUnitParametersPane::OnApplyUnit)
	ON_CONTROL(CBN_SELCHANGE, kDefaultsPolicyId,
		&CBattleUnitParametersPane::OnDefaultPolicyChanged)
	ON_CONTROL(CBN_SELCHANGE, kUnitPolicyId,
		&CBattleUnitParametersPane::OnUnitPolicyChanged)
	ON_BN_CLICKED(kBatchApplyPolicyId,
		&CBattleUnitParametersPane::OnBatchPolicyApplyChanged)
END_MESSAGE_MAP()

bool CBattleUnitParametersPane::CreatePane(
	CWnd* owner, DefaultsHandler defaults_handler,
	ResetHandler reset_handler, UnitHandler unit_handler,
	BatchUnitHandler batch_unit_handler)
{
	if (owner == nullptr || owner->GetSafeHwnd() == nullptr) return false;
	m_owner = owner;
	m_defaultsHandler = std::move(defaults_handler);
	m_resetHandler = std::move(reset_handler);
	m_unitHandler = std::move(unit_handler);
	m_batchUnitHandler = std::move(batch_unit_handler);
	if (GetSafeHwnd() != nullptr) return GetParent() == owner;
	LPCTSTR className = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1), nullptr);
	if (className == nullptr) return false;
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, className,
		L"S 战场默认参数", WS_POPUP | WS_CAPTION | WS_SYSMENU |
			WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 860, 240), owner, 0)) return false;
	ShowWindow(SW_HIDE);
	return true;
}

void CBattleUnitParametersPane::SetDiagnosticHandler(
	DiagnosticHandler handler)
{
	m_diagnosticHandler = std::move(handler);
}

void CBattleUnitParametersPane::ShowPane(bool show)
{
	if (GetSafeHwnd() == nullptr) return;
	if (show) {
		CRect work;
		if (m_owner != nullptr) m_owner->GetWindowRect(&work);
		if (work.IsRectEmpty()) SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
		const int width = 860;
		const int height = 240;
		int x = work.left + (work.Width() - width) / 2;
		int y = work.top + (work.Height() - height) / 2;
		x = (std::max)(static_cast<int>(work.left),
			(std::min)(x, static_cast<int>(work.right) - width));
		y = (std::max)(static_cast<int>(work.top),
			(std::min)(y, static_cast<int>(work.bottom) - height));
		MoveWindow(x, y, width, height, FALSE);
	}
	ShowWindow(show ? SW_SHOWNORMAL : SW_HIDE);
	if (show) SetForegroundWindow();
}

void CBattleUnitParametersPane::ShowDefaultsState(
	const ccz::editor_preview::BattleEditorState& state)
{
	m_mode = FormMode::Defaults;
	m_batchCacheIds.clear();
	m_batchSelectionPending = false;
	if (m_defaultsStatus.GetSafeHwnd() != nullptr)
		m_defaultsStatus.SetWindowText(L"");
	UpdateState(state);
}

void CBattleUnitParametersPane::ShowSingleUnitState(
	const ccz::editor_preview::BattleEditorState& state,
	std::uint64_t cache_id)
{
	m_mode = FormMode::SingleUnit;
	m_batchCacheIds.assign(1, cache_id);
	m_batchSelectionPending = false;
	UpdateState(state);
}

void CBattleUnitParametersPane::ShowBatchState(
	const ccz::editor_preview::BattleEditorState& state,
	std::vector<std::uint64_t> cache_ids,
	ccz::editor_preview::BattleBatchParameterMask checked_parameters)
{
	m_mode = FormMode::Batch;
	m_batchCacheIds = std::move(cache_ids);
	m_initialBatchCheckedParameters = checked_parameters;
	m_batchSelectionPending = true;
	if (m_unitStatus.GetSafeHwnd() != nullptr)
		m_unitStatus.SetWindowText(L"");
	UpdateState(state);
}

void CBattleUnitParametersPane::UpdateState(
	const ccz::editor_preview::BattleEditorState& state)
{
	if (GetSafeHwnd() == nullptr) return;
	m_rebuilding = true;
	m_instructionId = m_mode == FormMode::Defaults
		? 0x47 : state.placement.instruction_id;
	m_defaultsSnapshot = m_mode == FormMode::Defaults
		? ccz::editor_preview::BattlePlacementDefaults{}
		: state.placement_defaults;
	m_profileSnapshot = {};
	const auto profile = state.placement_profiles.find(0x47);
	if (profile != state.placement_profiles.end())
		m_profileSnapshot = profile->second;
	if (m_mode == FormMode::Defaults) {
		if (m_profileSnapshot.direction)
			m_defaultsSnapshot.direction = *m_profileSnapshot.direction;
		if (m_profileSnapshot.hidden)
			m_defaultsSnapshot.hidden = *m_profileSnapshot.hidden;
		if (m_profileSnapshot.level)
			m_defaultsSnapshot.level = *m_profileSnapshot.level;
		if (m_profileSnapshot.unit_type)
			m_defaultsSnapshot.unit_type = *m_profileSnapshot.unit_type;
		if (m_profileSnapshot.reinforcement)
			m_defaultsSnapshot.reinforcement = *m_profileSnapshot.reinforcement;
		if (m_profileSnapshot.ai_policy &&
			*m_profileSnapshot.ai_policy >= 0 &&
			*m_profileSnapshot.ai_policy <= 6)
			m_defaultsSnapshot.ai_policy = *m_profileSnapshot.ai_policy;
		if (m_profileSnapshot.target)
			m_defaultsSnapshot.target = *m_profileSnapshot.target;
		if (m_profileSnapshot.target_x)
			m_defaultsSnapshot.target_x = *m_profileSnapshot.target_x;
		if (m_profileSnapshot.target_y)
			m_defaultsSnapshot.target_y = *m_profileSnapshot.target_y;
	}

	SelectComboValue(m_defaultsDirection, m_defaultsSnapshot.direction);
	if (m_defaultsHidden.GetSafeHwnd() != nullptr)
		m_defaultsHidden.SetCheck(m_defaultsSnapshot.hidden ? BST_CHECKED : BST_UNCHECKED);
	UpdateIntegerEdit(m_defaultsLevel, m_defaultsSnapshot.level);
	SelectComboValue(m_defaultsType, m_defaultsSnapshot.unit_type);
	if (m_defaultsReinforcement.GetSafeHwnd() != nullptr)
		m_defaultsReinforcement.SetCheck(m_defaultsSnapshot.reinforcement
			? BST_CHECKED : BST_UNCHECKED);
	SelectComboValue(m_defaultsPolicy, m_defaultsSnapshot.ai_policy);
	populate_target_choices(m_defaultsTarget, state.characters.entries,
		m_defaultsSnapshot.target, m_defaultsTargetDataIds);
	UpdateIntegerEdit(m_defaultsTargetX, m_defaultsSnapshot.target_x);
	UpdateIntegerEdit(m_defaultsTargetY, m_defaultsSnapshot.target_y);

	m_unitSnapshots.clear();
	m_hasCurrentUnit = false;
	if (m_mode != FormMode::Defaults) {
		for (const std::uint64_t cache_id : m_batchCacheIds) {
			const auto found = std::find_if(state.battle.units.begin(),
				state.battle.units.end(), [&state, cache_id](const auto& unit) {
				return unit.editor_cache_id == cache_id && unit.fields_decoded &&
					!unit.editor_pending_delete &&
					!ccz::editor_preview::IsBattleFixedEmptySlot(unit) &&
					(unit.editor_staged_new || key_in_scope(
						unit.source_key, state.scope_root));
			});
			if (found != state.battle.units.end())
				m_unitSnapshots.push_back(*found);
		}
	}
	if (!m_unitSnapshots.empty()) {
		m_unitSnapshot = m_unitSnapshots.front();
		m_hasCurrentUnit = true;
	}

	if (m_hasCurrentUnit) {
		const auto& unit = m_unitSnapshot;
		m_unitData.ResetContent();
		const bool hasDataId = m_mode == FormMode::SingleUnit &&
			unit_has_role(unit, ccz::preview::BattleFieldRole::DataId);
		if (hasDataId) {
			for (const auto& character : state.characters.entries) {
				CString label;
				label.Format(L"%d.%s", character.data_id, character.name.c_str());
				const int index = m_unitData.AddString(label);
				if (index != CB_ERR && index != CB_ERRSPACE)
					m_unitData.SetItemData(index,
						static_cast<DWORD_PTR>(character.data_id));
			}
			int selection = CB_ERR;
			for (int i = 0; i < m_unitData.GetCount(); ++i) {
				if (static_cast<int>(m_unitData.GetItemData(i)) == unit.data_id) {
					selection = i;
					break;
				}
			}
			if (selection == CB_ERR) {
				CString label;
				label.Format(L"Data %d（未知）", unit.data_id);
				selection = m_unitData.AddString(label);
				if (selection != CB_ERR && selection != CB_ERRSPACE)
					m_unitData.SetItemData(selection,
						static_cast<DWORD_PTR>(unit.data_id));
			}
			if (selection != CB_ERR) m_unitData.SetCurSel(selection);
		} else if (m_mode == FormMode::SingleUnit) {
			m_unitData.AddString(L"我军槽位（无 Data 号）");
			m_unitData.SetCurSel(0);
		}
		UpdateIntegerEdit(m_unitX, unit.position.x);
		UpdateIntegerEdit(m_unitY, unit.position.y);
		SelectComboValue(m_unitDirection, unit.direction);
		m_unitHidden.SetCheck(unit.hidden ? BST_CHECKED : BST_UNCHECKED);
		SelectComboValue(m_unitHiddenBatchValue, unit.hidden ? 1 : 0);
		UpdateIntegerEdit(m_unitLevel, unit.level);
		SelectComboValue(m_unitType, unit.unit_type);
		m_unitReinforcement.SetCheck(unit.reinforcement ? BST_CHECKED : BST_UNCHECKED);
		SelectComboValue(m_unitReinforcementBatchValue,
			unit.reinforcement ? 1 : 0);
		SetComboValue(m_unitPolicy, unit.ai_policy, L"未知方针 ");
		populate_target_choices(m_unitTarget, state.characters.entries,
			unit.target, m_unitTargetDataIds);
		UpdateIntegerEdit(m_unitTargetX, unit.target_x);
		UpdateIntegerEdit(m_unitTargetY, unit.target_y);
		CString status;
		if (m_mode == FormMode::Batch) {
			status.Format(L"批量修改 %u 个单位；勾选要修改的参数，其他参数保留原值",
				static_cast<unsigned>(m_unitSnapshots.size()));
			m_modeInfo.SetWindowText(status);
		} else {
			status.Format(L"当前单位：指令 0x%02X，坐标（%d，%d）",
				unit.editor_deployment_instruction != 0xffff
					? unit.editor_deployment_instruction : unit.source_instruction,
				unit.position.x, unit.position.y);
			m_modeInfo.SetWindowText(L"角色身份与坐标独立显示；下方参数按统一布局调整。");
			m_unitStatus.SetWindowText(status);
		}
	} else {
		m_unitData.ResetContent();
		m_unitStatus.SetWindowText(m_mode == FormMode::Batch
			? L"多选组中的单位已失效，请关闭窗口后重新选择。"
			: L"目标单位已失效，请关闭窗口后重新选择。");
		m_modeInfo.SetWindowText(m_mode == FormMode::Batch
			? L"当前分组不可用。"
			: L"当前单位不可用。");
	}
	if (m_mode == FormMode::Defaults)
		m_modeInfo.SetWindowText(L"新增单位默认值；只影响之后新增的单位，写入字段由出场指令类型决定。");
	UpdateControlRoles();
	UpdateDefaultTargetControls();
	UpdateUnitTargetControls();
	if (m_mode != FormMode::Defaults) {
		m_applyUnit.SetWindowText(m_mode == FormMode::Batch
			? L"统一应用到分组" : L"应用到当前单位");
	}
	SetWindowText(m_mode == FormMode::Defaults ? L"S 战场默认参数"
		: (m_mode == FormMode::Batch ? L"S 战场批量修改参数"
			: L"S 战场单体修改参数"));
	CRect client;
	GetClientRect(&client);
	LayoutControls(client.Width(), client.Height());
	m_rebuilding = false;
}

int CBattleUnitParametersPane::OnCreate(LPCREATESTRUCT create)
{
	if (CWnd::OnCreate(create) == -1) return -1;
	const DWORD child = WS_CHILD | WS_VISIBLE;
	const DWORD editStyle = child | WS_BORDER | ES_AUTOHSCROLL;
	const DWORD comboStyle = child | WS_VSCROLL | CBS_DROPDOWNLIST;
	const DWORD buttonStyle = child | BS_PUSHBUTTON;
	const DWORD checkStyle = child | BS_AUTOCHECKBOX | BS_VCENTER;
	if (!m_defaultsGroup.Create(L"",
		child | BS_GROUPBOX, CRect(0,0,1,1), this, kDefaultsGroupId) ||
		!m_currentGroup.Create(L"",
		child | BS_GROUPBOX, CRect(0,0,1,1), this, kCurrentGroupId) ||
		!m_defaultsDirection.Create(comboStyle, CRect(0,0,1,1), this, kDefaultsDirectionId) ||
		!m_defaultsHidden.Create(L"隐藏", checkStyle, CRect(0,0,1,1), this, kDefaultsHiddenId) ||
		!m_defaultsLevel.Create(editStyle, CRect(0,0,1,1), this, kDefaultsLevelId) ||
		!m_defaultsType.Create(comboStyle, CRect(0,0,1,1), this, kDefaultsTypeId) ||
		!m_defaultsReinforcement.Create(L"援军", checkStyle, CRect(0,0,1,1), this, kDefaultsReinforcementId) ||
		!m_defaultsPolicy.Create(comboStyle, CRect(0,0,1,1), this, kDefaultsPolicyId) ||
		!m_defaultsTarget.Create(comboStyle, CRect(0,0,1,1), this, kDefaultsTargetId) ||
		!m_defaultsTargetX.Create(editStyle, CRect(0,0,1,1), this, kDefaultsTargetXId) ||
		!m_defaultsTargetY.Create(editStyle, CRect(0,0,1,1), this, kDefaultsTargetYId) ||
		!m_applyDefaults.Create(L"应用新增默认值", buttonStyle, CRect(0,0,1,1), this, kApplyDefaultsId) ||
		!m_resetDefaults.Create(L"重置模板", buttonStyle, CRect(0,0,1,1), this, kResetDefaultsId) ||
		!m_defaultsStatus.Create(L"", child | SS_LEFT, CRect(0,0,1,1), this, kDefaultsStatusId) ||
		!m_modeInfo.Create(L"", child | SS_LEFT | SS_CENTERIMAGE,
			CRect(0,0,1,1), this, 0x7FB1) ||
		!m_unitData.Create(comboStyle, CRect(0,0,1,1), this, kUnitDataId) ||
		!m_unitX.Create(editStyle, CRect(0,0,1,1), this, kUnitXId) ||
		!m_unitY.Create(editStyle, CRect(0,0,1,1), this, kUnitYId) ||
		!m_unitDirection.Create(comboStyle, CRect(0,0,1,1), this, kUnitDirectionId) ||
		!m_unitHidden.Create(L"隐藏", checkStyle, CRect(0,0,1,1), this, kUnitHiddenId) ||
		!m_unitHiddenBatchValue.Create(comboStyle, CRect(0,0,1,1), this, kUnitHiddenBatchValueId) ||
		!m_unitLevel.Create(editStyle, CRect(0,0,1,1), this, kUnitLevelId) ||
		!m_unitType.Create(comboStyle, CRect(0,0,1,1), this, kUnitTypeId) ||
		!m_unitReinforcement.Create(L"援军", checkStyle, CRect(0,0,1,1), this, kUnitReinforcementId) ||
		!m_unitReinforcementBatchValue.Create(comboStyle, CRect(0,0,1,1), this, kUnitReinforcementBatchValueId) ||
		!m_unitPolicy.Create(comboStyle, CRect(0,0,1,1), this, kUnitPolicyId) ||
		!m_unitTarget.Create(comboStyle, CRect(0,0,1,1), this, kUnitTargetId) ||
		!m_unitTargetX.Create(editStyle, CRect(0,0,1,1), this, kUnitTargetXId) ||
		!m_unitTargetY.Create(editStyle, CRect(0,0,1,1), this, kUnitTargetYId) ||
		!m_applyUnit.Create(L"应用到当前单位", buttonStyle, CRect(0,0,1,1), this, kApplyUnitId) ||
		!m_unitStatus.Create(L"", child | SS_LEFT, CRect(0,0,1,1), this, kUnitStatusId) ||
		!m_batchApplyDirection.Create(L"朝向", checkStyle, CRect(0,0,1,1), this, kBatchApplyDirectionId) ||
		!m_batchApplyHidden.Create(L"隐藏", checkStyle, CRect(0,0,1,1), this, kBatchApplyHiddenId) ||
		!m_batchApplyLevel.Create(L"等级偏移", checkStyle, CRect(0,0,1,1), this, kBatchApplyLevelId) ||
		!m_batchApplyUnitType.Create(L"兵种级别", checkStyle, CRect(0,0,1,1), this, kBatchApplyUnitTypeId) ||
		!m_batchApplyReinforcement.Create(L"援军", checkStyle, CRect(0,0,1,1), this, kBatchApplyReinforcementId) ||
		!m_batchApplyPolicy.Create(L"行动方针", checkStyle, CRect(0,0,1,1), this, kBatchApplyPolicyId) ||
		!m_batchApplyTarget.Create(L"目标", checkStyle, CRect(0,0,1,1), this, kBatchApplyTargetId))
		return -1;
	const wchar_t* placeholder_texts[] = {
		L"— 朝向", L"— 隐藏", L"— 等级偏移", L"— 兵种级别",
		L"— 援军", L"— 行动方针", L"— 目标"};
	for (std::size_t i = 0; i < std::size(placeholder_texts); ++i) {
		if (!m_parameterPlaceholders[i].Create(placeholder_texts[i],
			child | SS_CENTERIMAGE, CRect(0,0,1,1), this,
			static_cast<UINT>(0x7FC0 + i))) return -1;
	}
	AddCommonChoices();

	// Labels use stable IDs from this private range; these are all visual
	// controls and have no effect on the document TreeView.
	const struct { UINT id; const wchar_t* text; } labels[] = {
		{0x7FA0,L"朝向"},{0x7FA1,L"等级偏移"},{0x7FA2,L"兵种级别"},
		{0x7FA3,L"行动方针"},{0x7FA4,L"武将"},{0x7FA5,L"X"},
		{0x7FA6,L"Y"},{0x7FA7,L"角色 / 槽位"},{0x7FA8,L"坐标 X"},
		{0x7FA9,L"坐标 Y"},{0x7FAA,L"朝向"},{0x7FAB,L"等级偏移"},
		{0x7FAC,L"兵种级别"},{0x7FAD,L"行动方针"},{0x7FAE,L"武将"},
		{0x7FAF,L"X"},{0x7FB0,L"Y"},
	};
	m_labels.clear();
	m_labels.reserve(std::size(labels));
	for (const auto& item : labels) {
		auto control = std::make_unique<CStatic>();
		if (!control->Create(item.text, child | SS_CENTERIMAGE,
			CRect(0,0,1,1), this, item.id)) return -1;
		m_labels.push_back(std::move(control));
	}
	if (m_font.CreateFont(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei")) {
		for (CWnd* control : {static_cast<CWnd*>(&m_defaultsGroup),
			static_cast<CWnd*>(&m_currentGroup), static_cast<CWnd*>(&m_defaultsDirection),
			static_cast<CWnd*>(&m_defaultsHidden), static_cast<CWnd*>(&m_defaultsLevel),
			static_cast<CWnd*>(&m_defaultsType), static_cast<CWnd*>(&m_defaultsReinforcement),
			static_cast<CWnd*>(&m_defaultsPolicy), static_cast<CWnd*>(&m_defaultsTarget),
			static_cast<CWnd*>(&m_defaultsTargetX), static_cast<CWnd*>(&m_defaultsTargetY),
			static_cast<CWnd*>(&m_applyDefaults), static_cast<CWnd*>(&m_resetDefaults),
			static_cast<CWnd*>(&m_defaultsStatus), static_cast<CWnd*>(&m_modeInfo),
			static_cast<CWnd*>(&m_unitData),
			static_cast<CWnd*>(&m_unitX), static_cast<CWnd*>(&m_unitY),
			static_cast<CWnd*>(&m_unitDirection), static_cast<CWnd*>(&m_unitHidden),
			static_cast<CWnd*>(&m_unitHiddenBatchValue),
			static_cast<CWnd*>(&m_unitLevel), static_cast<CWnd*>(&m_unitType),
			static_cast<CWnd*>(&m_unitReinforcement), static_cast<CWnd*>(&m_unitPolicy),
			static_cast<CWnd*>(&m_unitReinforcementBatchValue),
			static_cast<CWnd*>(&m_unitTarget), static_cast<CWnd*>(&m_unitTargetX),
			static_cast<CWnd*>(&m_unitTargetY), static_cast<CWnd*>(&m_applyUnit),
			static_cast<CWnd*>(&m_unitStatus),
			static_cast<CWnd*>(&m_batchApplyDirection),
			static_cast<CWnd*>(&m_batchApplyHidden),
			static_cast<CWnd*>(&m_batchApplyLevel),
			static_cast<CWnd*>(&m_batchApplyUnitType),
			static_cast<CWnd*>(&m_batchApplyReinforcement),
			static_cast<CWnd*>(&m_batchApplyPolicy),
			static_cast<CWnd*>(&m_batchApplyTarget)}) control->SetFont(&m_font);
		for (CStatic& placeholder : m_parameterPlaceholders)
			placeholder.SetFont(&m_font);
		for (const auto& control : m_labels) control->SetFont(&m_font);
	}
	return 0;
}

void CBattleUnitParametersPane::AddCommonChoices()
{
	for (int value = -1; value < 4; ++value)
		add_choice(m_defaultsDirection, kDirectionNames[value + 1], value);
	for (int value = -1; value < 4; ++value)
		add_choice(m_unitDirection, kDirectionNames[value + 1], value);
	for (int value = 0; value < 3; ++value) {
		add_choice(m_defaultsType, kUnitTypeNames[value], value);
		add_choice(m_unitType, kUnitTypeNames[value], value);
	}
	for (int value = 0; value < 7; ++value) {
		add_choice(m_defaultsPolicy, kPolicyNames[value], value);
		add_choice(m_unitPolicy, kPolicyNames[value], value);
	}
	add_choice(m_unitHiddenBatchValue, L"显示", 0);
	add_choice(m_unitHiddenBatchValue, L"隐藏", 1);
	add_choice(m_unitReinforcementBatchValue, L"普通", 0);
	add_choice(m_unitReinforcementBatchValue, L"援军", 1);
}

void CBattleUnitParametersPane::OnSize(UINT type, int cx, int cy)
{
	CWnd::OnSize(type, cx, cy);
	LayoutControls(cx, cy);
}

void CBattleUnitParametersPane::LayoutControls(int cx, int cy)
{
	if (m_defaultsGroup.GetSafeHwnd() == nullptr) return;
	const int margin = 12;
	const int top = 8;
	const int right = (std::max)(margin, cx - margin);
	const bool defaults = m_mode == FormMode::Defaults;
	const bool batch = m_mode == FormMode::Batch;
	const auto label = [this](UINT id, int x, int y, int w) {
		if (CWnd* control = GetDlgItem(id)) control->MoveWindow(x,y,w,24);
	};
	const int rowContext = top + 8;
	const int row1 = rowContext + 34;
	const int row2 = row1 + 34;
	const int row3 = row2 + 34;
	const int footer = row3 + 36;
	const int groupHeight = (std::min)(cy - top - 4,
		footer + 28 + 12 - top);
	m_defaultsGroup.MoveWindow(margin, top, right - margin, groupHeight);
	m_currentGroup.MoveWindow(margin, top, right - margin, groupHeight);
	m_modeInfo.MoveWindow(24, rowContext, (std::max)(120, right - 36), 24);
	const auto place_placeholder = [this](std::size_t index,
		int x, int y, int width) {
		m_parameterPlaceholders[index].MoveWindow(x, y, width, 24);
	};
	if (defaults) {
		place_placeholder(0, 24, row1, 76);
		m_defaultsDirection.MoveWindow(104, row1, 132, 180);
		place_placeholder(2, 254, row1, 88);
		m_defaultsLevel.MoveWindow(346, row1, 72, 24);
		place_placeholder(3, 438, row1, 96);
		m_defaultsType.MoveWindow(538, row1, 150, 180);

		place_placeholder(1, 24, row2, 76);
		m_defaultsHidden.MoveWindow(104, row2, 132, 24);
		place_placeholder(4, 254, row2, 88);
		m_defaultsReinforcement.MoveWindow(346, row2, 132, 24);
		place_placeholder(5, 502, row2, 88);
		m_defaultsPolicy.MoveWindow(594, row2, 150, 180);

		place_placeholder(6, 24, row3, 76);
		label(0x7FAE, 108, row3, 76);
		m_defaultsTarget.MoveWindow(188, row3, 126, 180);
		label(0x7FAF, 318, row3, 20);
		m_defaultsTargetX.MoveWindow(342, row3, 72, 24);
		label(0x7FB0, 462, row3, 20);
		m_defaultsTargetY.MoveWindow(486, row3, 72, 24);

		m_applyDefaults.MoveWindow(24, footer, 172, 28);
		m_resetDefaults.MoveWindow(208, footer, 140, 28);
		m_defaultsStatus.MoveWindow(364, footer + 3,
			(std::max)(120, right - 372), 24);
		return;
	}
	if (batch) {
		m_batchApplyDirection.MoveWindow(24, row1, 76, 24);
		m_unitDirection.MoveWindow(104, row1, 132, 180);
		m_batchApplyLevel.MoveWindow(254, row1, 88, 24);
		m_unitLevel.MoveWindow(346, row1, 72, 24);
		m_batchApplyUnitType.MoveWindow(438, row1, 96, 24);
		m_unitType.MoveWindow(538, row1, 150, 180);

		m_batchApplyHidden.MoveWindow(24, row2, 76, 24);
		m_unitHiddenBatchValue.MoveWindow(104, row2, 132, 180);
		m_batchApplyReinforcement.MoveWindow(254, row2, 88, 24);
		m_unitReinforcementBatchValue.MoveWindow(346, row2, 132, 180);
		m_batchApplyPolicy.MoveWindow(502, row2, 88, 24);
		m_unitPolicy.MoveWindow(594, row2, 150, 180);

		m_batchApplyTarget.MoveWindow(24, row3, 76, 24);
		label(0x7FAE, 108, row3, 76);
		m_unitTarget.MoveWindow(188, row3, 126, 180);
		label(0x7FAF, 318, row3, 20);
		m_unitTargetX.MoveWindow(342, row3, 72, 24);
		label(0x7FB0, 462, row3, 20);
		m_unitTargetY.MoveWindow(486, row3, 72, 24);

		m_applyUnit.MoveWindow(24, footer, 172, 28);
		m_unitStatus.MoveWindow(210, footer + 3,
			(std::max)(120, right - 218), 24);
		return;
	}

	label(0x7FA7, 24, rowContext, 72);
	m_unitData.MoveWindow(100, rowContext, 214, 180);
	label(0x7FA8, 330, rowContext, 28);
	m_unitX.MoveWindow(362, rowContext, 70, 24);
	label(0x7FA9, 446, rowContext, 28);
	m_unitY.MoveWindow(478, rowContext, 70, 24);

	place_placeholder(0, 24, row1, 76);
	m_unitDirection.MoveWindow(104, row1, 132, 180);
	place_placeholder(2, 254, row1, 88);
	m_unitLevel.MoveWindow(346, row1, 72, 24);
	place_placeholder(3, 438, row1, 96);
	m_unitType.MoveWindow(538, row1, 150, 180);

	place_placeholder(1, 24, row2, 76);
	m_unitHidden.MoveWindow(104, row2, 132, 24);
	place_placeholder(4, 254, row2, 88);
	m_unitReinforcement.MoveWindow(346, row2, 132, 24);
	place_placeholder(5, 502, row2, 88);
	m_unitPolicy.MoveWindow(594, row2, 150, 180);

	place_placeholder(6, 24, row3, 76);
	label(0x7FAE, 108, row3, 76);
	m_unitTarget.MoveWindow(188, row3, 126, 180);
	label(0x7FAF, 318, row3, 20);
	m_unitTargetX.MoveWindow(342, row3, 72, 24);
	label(0x7FB0, 462, row3, 20);
	m_unitTargetY.MoveWindow(486, row3, 72, 24);

	m_applyUnit.MoveWindow(24, footer, 172, 28);
	m_unitStatus.MoveWindow(210, footer + 3,
		(std::max)(120, right - 218), 24);
}

void CBattleUnitParametersPane::OnClose()
{
	ShowPane(false);
}

void CBattleUnitParametersPane::UpdateIntegerEdit(CEdit& edit, int value)
{
	if (edit.GetSafeHwnd() == nullptr) return;
	const CString text(std::to_wstring(value).c_str());
	CString old;
	edit.GetWindowText(old);
	if (old != text) edit.SetWindowText(text);
}

bool CBattleUnitParametersPane::ReadIntegerEdit(
	const CEdit& edit, int& value) const
{
	CString text;
	edit.GetWindowText(text);
	const wchar_t* begin = text.GetString();
	while (*begin == L' ' || *begin == L'\t') ++begin;
	if (*begin == L'\0') return false;
	wchar_t* end = nullptr;
	errno = 0;
	const long parsed = std::wcstol(begin, &end, 10);
	while (end != nullptr && (*end == L' ' || *end == L'\t')) ++end;
	if (errno == ERANGE || end == begin || end == nullptr || *end != L'\0' ||
		parsed < (std::numeric_limits<int>::min)() ||
		parsed > (std::numeric_limits<int>::max)()) return false;
	value = static_cast<int>(parsed);
	return true;
}

void CBattleUnitParametersPane::SelectComboValue(CComboBox& combo, int value)
{
	for (int i = 0; i < combo.GetCount(); ++i) {
		if (static_cast<int>(combo.GetItemData(i)) == value) {
			combo.SetCurSel(i);
			return;
		}
	}
	combo.SetCurSel(CB_ERR);
}

int CBattleUnitParametersPane::SelectedComboValue(const CComboBox& combo) const
{
	const int selection = combo.GetCurSel();
	return selection == CB_ERR ? INT_MIN
		: static_cast<int>(combo.GetItemData(selection));
}

int CBattleUnitParametersPane::SelectedTargetDataId(
	const CMyComboBox& combo, const std::vector<int>& data_ids) const
{
	if (!combo.HasValidIndexedSelection()) return INT_MIN;
	const int selection = combo.GetCurSel();
	return selection < 0 || selection >= static_cast<int>(data_ids.size())
		? INT_MIN : data_ids[static_cast<std::size_t>(selection)];
}

void CBattleUnitParametersPane::SetComboValue(
	CComboBox& combo, int value, const wchar_t* fallback_prefix)
{
	SelectComboValue(combo, value);
	if (combo.GetCurSel() != CB_ERR) return;
	CString label;
	label.Format(L"%s%d", fallback_prefix, value);
	int index = CB_ERR;
	for (int i = 0; i < combo.GetCount(); ++i) {
		CString existing;
		combo.GetLBText(i, existing);
		if (existing == label) {
			index = i;
			break;
		}
	}
	if (index == CB_ERR) index = combo.AddString(label);
	if (index != CB_ERR && index != CB_ERRSPACE) {
		if (combo.GetItemData(index) == CB_ERR)
			combo.SetItemData(index, static_cast<DWORD_PTR>(value));
		combo.SetCurSel(index);
	}
}

void CBattleUnitParametersPane::UpdateDefaultTargetControls()
{
	const int policy = SelectedComboValue(m_defaultsPolicy);
	const bool active = m_mode == FormMode::Defaults;
	const bool showData = active && (policy == 3 || policy == 5);
	const bool showCoord = active && (policy == 4 || policy == 6);
	for (CWnd* control : {static_cast<CWnd*>(GetDlgItem(0x7FA4)),
		static_cast<CWnd*>(&m_defaultsTarget)})
		if (control != nullptr) control->EnableWindow(showData);
	for (CWnd* control : {static_cast<CWnd*>(GetDlgItem(0x7FA5)),
		static_cast<CWnd*>(GetDlgItem(0x7FA6)),
		static_cast<CWnd*>(&m_defaultsTargetX),
		static_cast<CWnd*>(&m_defaultsTargetY)})
		if (control != nullptr) control->EnableWindow(showCoord);
}

void CBattleUnitParametersPane::UpdateUnitTargetControls()
{
	const int policy = SelectedComboValue(m_unitPolicy);
	const auto hasRole = [this](ccz::preview::BattleFieldRole role) {
		return !m_unitSnapshots.empty() && std::all_of(
			m_unitSnapshots.begin(), m_unitSnapshots.end(),
			[role](const ccz::preview::BattleUnit& unit) {
				return unit_has_role(unit, role);
			});
	};
	const bool policiesAgree = m_mode != FormMode::Batch ||
		(!m_unitSnapshots.empty() && std::all_of(
			m_unitSnapshots.begin() + 1, m_unitSnapshots.end(),
			[this](const ccz::preview::BattleUnit& unit) {
				return unit.ai_policy == m_unitSnapshots.front().ai_policy;
			}));
	const bool active = m_mode != FormMode::Defaults && m_hasCurrentUnit &&
		(policiesAgree || (m_mode == FormMode::Batch &&
			BatchParameterChecked(
				ccz::editor_preview::BattleBatchParameter::AiPolicy))) &&
		hasRole(ccz::preview::BattleFieldRole::AiPolicy);
	const bool showData = active && hasRole(
		ccz::preview::BattleFieldRole::TargetDataId) &&
		(policy == 3 || policy == 5);
	const bool showCoord = active && hasRole(
		ccz::preview::BattleFieldRole::TargetX) &&
		hasRole(ccz::preview::BattleFieldRole::TargetY) &&
		(policy == 4 || policy == 6);
	for (CWnd* control : {static_cast<CWnd*>(GetDlgItem(0x7FAE)),
		static_cast<CWnd*>(&m_unitTarget)})
		if (control != nullptr) control->EnableWindow(showData);
	for (CWnd* control : {static_cast<CWnd*>(GetDlgItem(0x7FAF)),
		static_cast<CWnd*>(GetDlgItem(0x7FB0)),
		static_cast<CWnd*>(&m_unitTargetX),
		static_cast<CWnd*>(&m_unitTargetY)})
		if (control != nullptr) control->EnableWindow(showCoord);
}

void CBattleUnitParametersPane::OnDefaultPolicyChanged()
{
	if (!m_rebuilding) UpdateDefaultTargetControls();
}

void CBattleUnitParametersPane::OnUnitPolicyChanged()
{
	if (!m_rebuilding) UpdateUnitTargetControls();
}

void CBattleUnitParametersPane::OnBatchPolicyApplyChanged()
{
	if (m_rebuilding || m_mode != FormMode::Batch) return;
	UpdateControlRoles();
	UpdateUnitTargetControls();
}

CButton* CBattleUnitParametersPane::BatchParameterControl(
	ccz::editor_preview::BattleBatchParameter parameter) noexcept
{
	using Parameter = ccz::editor_preview::BattleBatchParameter;
	switch (parameter) {
	case Parameter::Direction: return &m_batchApplyDirection;
	case Parameter::Hidden: return &m_batchApplyHidden;
	case Parameter::Level: return &m_batchApplyLevel;
	case Parameter::UnitType: return &m_batchApplyUnitType;
	case Parameter::Reinforcement: return &m_batchApplyReinforcement;
	case Parameter::AiPolicy: return &m_batchApplyPolicy;
	case Parameter::Target: return &m_batchApplyTarget;
	}
	return nullptr;
}

bool CBattleUnitParametersPane::BatchParameterSupported(
	ccz::editor_preview::BattleBatchParameter parameter) const noexcept
{
	return (m_batchSupportedParameters &
		ccz::editor_preview::batch_parameter_bit(parameter)) != 0;
}

bool CBattleUnitParametersPane::BatchParameterChecked(
	ccz::editor_preview::BattleBatchParameter parameter) const noexcept
{
	const CButton* control = const_cast<CBattleUnitParametersPane*>(this)
		->BatchParameterControl(parameter);
	return control != nullptr && control->GetSafeHwnd() != nullptr &&
		control->GetCheck() == BST_CHECKED;
}

void CBattleUnitParametersPane::UpdateControlRoles()
{
	using ccz::editor_preview::BattleBatchParameter;
	using ccz::editor_preview::batch_parameter_bit;
	using ccz::preview::BattleFieldRole;
	const auto show = [](CWnd* control, bool visible) {
		if (control != nullptr)
			control->ShowWindow(visible ? SW_SHOW : SW_HIDE);
	};
	const auto label = [this, &show](UINT id, bool visible) {
		show(GetDlgItem(id), visible);
	};
	const bool defaults = m_mode == FormMode::Defaults;
	const bool batch = m_mode == FormMode::Batch;
	const bool single = m_mode == FormMode::SingleUnit;
	show(&m_defaultsGroup, defaults);
	show(&m_currentGroup, !defaults);
	show(&m_modeInfo, defaults || batch);
	for (CWnd* control : {static_cast<CWnd*>(&m_defaultsDirection),
		static_cast<CWnd*>(&m_defaultsHidden), static_cast<CWnd*>(&m_defaultsLevel),
		static_cast<CWnd*>(&m_defaultsType), static_cast<CWnd*>(&m_defaultsReinforcement),
		static_cast<CWnd*>(&m_defaultsPolicy), static_cast<CWnd*>(&m_defaultsTarget),
		static_cast<CWnd*>(&m_defaultsTargetX), static_cast<CWnd*>(&m_defaultsTargetY),
		static_cast<CWnd*>(&m_applyDefaults), static_cast<CWnd*>(&m_resetDefaults),
		static_cast<CWnd*>(&m_defaultsStatus)})
		show(control, defaults);
	for (UINT id = 0x7FA0; id <= 0x7FA6; ++id) label(id, FALSE);
	for (CWnd* control : {static_cast<CWnd*>(&m_unitData),
		static_cast<CWnd*>(&m_unitX), static_cast<CWnd*>(&m_unitY),
		static_cast<CWnd*>(&m_unitDirection), static_cast<CWnd*>(&m_unitHidden),
		static_cast<CWnd*>(&m_unitHiddenBatchValue),
		static_cast<CWnd*>(&m_unitLevel), static_cast<CWnd*>(&m_unitType),
		static_cast<CWnd*>(&m_unitReinforcement), static_cast<CWnd*>(&m_unitPolicy),
		static_cast<CWnd*>(&m_unitReinforcementBatchValue),
		static_cast<CWnd*>(&m_unitTarget), static_cast<CWnd*>(&m_unitTargetX),
		static_cast<CWnd*>(&m_unitTargetY), static_cast<CWnd*>(&m_applyUnit),
		static_cast<CWnd*>(&m_unitStatus)})
		show(control, !defaults);
	for (UINT id = 0x7FA7; id <= 0x7FA9; ++id) label(id, single);
	for (UINT id = 0x7FAA; id <= 0x7FAD; ++id) label(id, FALSE);
	for (UINT id = 0x7FAE; id <= 0x7FB0; ++id) label(id, TRUE);
	for (CStatic& placeholder : m_parameterPlaceholders)
		show(&placeholder, !batch);
	for (CButton* control : {&m_batchApplyDirection, &m_batchApplyHidden,
		&m_batchApplyLevel, &m_batchApplyUnitType, &m_batchApplyReinforcement,
		&m_batchApplyPolicy, &m_batchApplyTarget})
		show(control, batch);
	show(&m_unitHidden, !defaults && !batch);
	show(&m_unitHiddenBatchValue, batch);
	show(&m_unitReinforcement, !defaults && !batch);
	show(&m_unitReinforcementBatchValue, batch);

	if (defaults) {
		for (CWnd* control : {static_cast<CWnd*>(&m_defaultsDirection),
			static_cast<CWnd*>(&m_defaultsHidden),
			static_cast<CWnd*>(&m_defaultsLevel),
			static_cast<CWnd*>(&m_defaultsType),
			static_cast<CWnd*>(&m_defaultsReinforcement),
			static_cast<CWnd*>(&m_defaultsPolicy),
			static_cast<CWnd*>(&m_applyDefaults),
			static_cast<CWnd*>(&m_resetDefaults)})
			control->EnableWindow(TRUE);
		return;
	}

	const auto hasRole = [this](ccz::preview::BattleFieldRole role) {
		return !m_unitSnapshots.empty() && std::all_of(
			m_unitSnapshots.begin(), m_unitSnapshots.end(),
			[role](const ccz::preview::BattleUnit& unit) {
				return unit_has_role(unit, role);
			});
	};
	const bool hasData = hasRole(BattleFieldRole::DataId);
	const bool hasX = hasRole(BattleFieldRole::X);
	const bool hasY = hasRole(BattleFieldRole::Y);
	const bool hasDirection = hasRole(BattleFieldRole::Direction);
	const bool hasHidden = hasRole(BattleFieldRole::Hidden);
	const bool hasLevel = hasRole(BattleFieldRole::Level);
	const bool hasType = hasRole(BattleFieldRole::UnitType);
	const bool hasReinforcement = hasRole(BattleFieldRole::Reinforcement);
	const bool hasPolicy = hasRole(BattleFieldRole::AiPolicy);
	const auto allPoliciesAgree = [this]() {
		return !m_unitSnapshots.empty() && std::all_of(
			m_unitSnapshots.begin() + 1, m_unitSnapshots.end(),
			[this](const ccz::preview::BattleUnit& unit) {
				return unit.ai_policy == m_unitSnapshots.front().ai_policy;
			});
	};
	const bool hasTargetData = hasPolicy &&
		hasRole(BattleFieldRole::TargetDataId);
	const bool hasTargetCoord = hasPolicy && hasRole(BattleFieldRole::TargetX) &&
		hasRole(BattleFieldRole::TargetY);
	const bool hasTarget = hasTargetData || hasTargetCoord;
	m_batchSupportedParameters = 0;
	if (hasDirection) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::Direction);
	if (hasHidden) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::Hidden);
	if (hasLevel) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::Level);
	if (hasType) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::UnitType);
	if (hasReinforcement) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::Reinforcement);
	if (hasPolicy) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::AiPolicy);
	if (hasTarget) m_batchSupportedParameters |=
		batch_parameter_bit(BattleBatchParameter::Target);

	show(&m_unitData, m_hasCurrentUnit && !batch);
	show(&m_unitX, m_hasCurrentUnit && !batch);
	show(&m_unitY, m_hasCurrentUnit && !batch);
	show(&m_unitDirection, m_hasCurrentUnit);
	show(&m_unitLevel, m_hasCurrentUnit);
	show(&m_unitType, m_hasCurrentUnit);
	show(&m_unitPolicy, m_hasCurrentUnit);
	show(&m_applyUnit, m_hasCurrentUnit);
	show(&m_unitStatus, true);
	label(0x7FA7, m_hasCurrentUnit && !batch);
	label(0x7FA8, m_hasCurrentUnit && !batch);
	label(0x7FA9, m_hasCurrentUnit && !batch);
	label(0x7FAA, m_hasCurrentUnit && !batch);
	label(0x7FAB, m_hasCurrentUnit && !batch);
	label(0x7FAC, m_hasCurrentUnit && !batch);
	label(0x7FAD, m_hasCurrentUnit && !batch);
	label(0x7FAE, m_hasCurrentUnit);
	label(0x7FAF, m_hasCurrentUnit);
	label(0x7FB0, m_hasCurrentUnit);
	for (CButton* control : {&m_batchApplyDirection, &m_batchApplyHidden,
		&m_batchApplyLevel, &m_batchApplyUnitType, &m_batchApplyReinforcement,
		&m_batchApplyPolicy, &m_batchApplyTarget}) {
		const auto parameter = control == &m_batchApplyDirection
			? BattleBatchParameter::Direction
			: (control == &m_batchApplyHidden ? BattleBatchParameter::Hidden
			: (control == &m_batchApplyLevel ? BattleBatchParameter::Level
			: (control == &m_batchApplyUnitType ? BattleBatchParameter::UnitType
			: (control == &m_batchApplyReinforcement
				? BattleBatchParameter::Reinforcement
			: (control == &m_batchApplyPolicy ? BattleBatchParameter::AiPolicy
				: BattleBatchParameter::Target)))));
		if (m_batchSelectionPending) {
			control->SetCheck((m_initialBatchCheckedParameters &
				batch_parameter_bit(parameter)) != 0
				? BST_CHECKED : BST_UNCHECKED);
		}
		bool supported = BatchParameterSupported(parameter);
		if (parameter == BattleBatchParameter::Target && batch &&
			!allPoliciesAgree() &&
			!BatchParameterChecked(BattleBatchParameter::AiPolicy)) {
			supported = false;
		}
		control->EnableWindow(m_hasCurrentUnit && supported);
	}
	m_batchSelectionPending = false;
	m_applyUnit.EnableWindow(m_hasCurrentUnit &&
		(batch ? static_cast<bool>(m_batchUnitHandler)
			: static_cast<bool>(m_unitHandler)));
	m_unitData.EnableWindow(m_hasCurrentUnit && hasData);
	m_unitX.EnableWindow(m_hasCurrentUnit && hasX);
	m_unitY.EnableWindow(m_hasCurrentUnit && hasY);
	m_unitDirection.EnableWindow(m_hasCurrentUnit && hasDirection);
	m_unitHidden.EnableWindow(m_hasCurrentUnit && hasHidden);
	m_unitHiddenBatchValue.EnableWindow(m_hasCurrentUnit && hasHidden);
	m_unitLevel.EnableWindow(m_hasCurrentUnit && hasLevel);
	m_unitType.EnableWindow(m_hasCurrentUnit && hasType);
	m_unitReinforcement.EnableWindow(m_hasCurrentUnit && hasReinforcement);
	m_unitReinforcementBatchValue.EnableWindow(m_hasCurrentUnit && hasReinforcement);
	m_unitPolicy.EnableWindow(m_hasCurrentUnit && hasPolicy);
	m_unitTarget.EnableWindow(m_hasCurrentUnit && hasTargetData);
	m_unitTargetX.EnableWindow(m_hasCurrentUnit && hasTargetCoord);
	m_unitTargetY.EnableWindow(m_hasCurrentUnit && hasTargetCoord);
}

void CBattleUnitParametersPane::SetDiagnostic(
	const std::wstring& diagnostic)
{
	CStatic& target = m_mode == FormMode::Defaults
		? m_defaultsStatus : m_unitStatus;
	if (target.GetSafeHwnd() != nullptr)
		target.SetWindowText(diagnostic.c_str());
	if (m_diagnosticHandler) m_diagnosticHandler(diagnostic);
}

void CBattleUnitParametersPane::OnApplyDefaults()
{
	if (m_rebuilding || !m_defaultsHandler) return;
	auto profile = m_profileSnapshot;
	const int direction = SelectedComboValue(m_defaultsDirection);
	int level = 0, target = 0, targetX = 0, targetY = 0;
	if (direction == INT_MIN) {
		SetDiagnostic(L"新增默认值格式无效：请检查朝向");
		return;
	}
	if (direction != m_defaultsSnapshot.direction) profile.direction = direction;
	const bool hidden = m_defaultsHidden.GetCheck() == BST_CHECKED;
	if (hidden != m_defaultsSnapshot.hidden) profile.hidden = hidden;
	if (!ReadIntegerEdit(m_defaultsLevel, level)) {
		SetDiagnostic(L"等级偏移格式无效");
		return;
	}
	if (level != m_defaultsSnapshot.level) profile.level = level;
	const int type = SelectedComboValue(m_defaultsType);
	if (type == INT_MIN) {
		SetDiagnostic(L"兵种级别未选择");
		return;
	}
	if (type != m_defaultsSnapshot.unit_type) profile.unit_type = type;
	const bool reinforcement = m_defaultsReinforcement.GetCheck() == BST_CHECKED;
	if (reinforcement != m_defaultsSnapshot.reinforcement)
		profile.reinforcement = reinforcement;
	const int policy = SelectedComboValue(m_defaultsPolicy);
	if (policy == INT_MIN) {
		SetDiagnostic(L"行动方针未选择");
		return;
	}
	if (policy != m_defaultsSnapshot.ai_policy) profile.ai_policy = policy;
	if (policy == 3 || policy == 5 || policy == 4 || policy == 6) {
		target = SelectedTargetDataId(m_defaultsTarget,
			m_defaultsTargetDataIds);
		if (target == INT_MIN ||
			!ReadIntegerEdit(m_defaultsTargetX, targetX) ||
			!ReadIntegerEdit(m_defaultsTargetY, targetY)) {
			SetDiagnostic(L"默认目标字段无效；目标 Data 与坐标需要同时有效");
			return;
		}
		if (target != m_defaultsSnapshot.target ||
			targetX != m_defaultsSnapshot.target_x ||
			targetY != m_defaultsSnapshot.target_y) {
			profile.target = target;
			profile.target_x = targetX;
			profile.target_y = targetY;
		}
	}
	const auto result = m_defaultsHandler(0x47, profile);
	SetDiagnostic(result.diagnostic);
}

void CBattleUnitParametersPane::OnResetDefaults()
{
	if (m_rebuilding || !m_resetHandler) return;
	const auto result = m_resetHandler(0x47);
	SetDiagnostic(result.diagnostic);
}

void CBattleUnitParametersPane::OnApplyUnit()
{
	if (m_rebuilding || !m_hasCurrentUnit) return;
	const bool batch = m_mode == FormMode::Batch;
	if ((!batch && !m_unitHandler) || (batch && !m_batchUnitHandler)) return;
	using namespace ccz::preview;
	using ccz::editor_preview::BattleUnitPatch;
	BattleUnitPatch patch;
	const auto has = [this](BattleFieldRole role) {
		return !m_unitSnapshots.empty() && std::all_of(
			m_unitSnapshots.begin(), m_unitSnapshots.end(),
			[role](const BattleUnit& unit) {
				return unit_has_role(unit, role);
			});
	};
	const auto should_apply = [this, batch](
		ccz::editor_preview::BattleBatchParameter parameter) {
		if (!batch) return true;
		if (parameter ==
			ccz::editor_preview::BattleBatchParameter::Target) {
			const bool policiesAgree = !m_unitSnapshots.empty() &&
				std::all_of(m_unitSnapshots.begin() + 1,
					m_unitSnapshots.end(), [this](const BattleUnit& unit) {
						return unit.ai_policy == m_unitSnapshots.front().ai_policy;
					});
			return BatchParameterChecked(parameter) &&
				(policiesAgree || BatchParameterChecked(
					ccz::editor_preview::BattleBatchParameter::AiPolicy));
		}
		return BatchParameterChecked(parameter);
	};
	const auto needs_update = [this, batch](const auto& value,
		const auto& get_current) {
		if (!batch) return value != get_current(m_unitSnapshot);
		return std::any_of(m_unitSnapshots.begin(), m_unitSnapshots.end(),
			[&value, &get_current](const BattleUnit& unit) {
				return value != get_current(unit);
			});
	};
	if (!batch && has(BattleFieldRole::DataId)) {
		const int selected = m_unitData.GetCurSel();
		if (selected == CB_ERR) {
			SetDiagnostic(L"请先选择角色 Data 号");
			return;
		}
		const int dataId = static_cast<int>(m_unitData.GetItemData(selected));
		if (dataId != m_unitSnapshot.data_id) patch.data_id = dataId;
	}
	if (!batch && has(BattleFieldRole::X) && has(BattleFieldRole::Y)) {
		int x = 0, y = 0;
		if (!ReadIntegerEdit(m_unitX, x) || !ReadIntegerEdit(m_unitY, y)) {
			SetDiagnostic(L"坐标格式无效");
			return;
		}
		if (x != m_unitSnapshot.position.x || y != m_unitSnapshot.position.y)
			patch.position = BattleCoord{x, y};
	}
	if (has(BattleFieldRole::Direction) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::Direction)) {
		const int direction = SelectedComboValue(m_unitDirection);
		if (direction == INT_MIN) {
			SetDiagnostic(L"朝向未选择");
			return;
		}
		if (needs_update(direction,
			[](const BattleUnit& unit) { return unit.direction; }))
			patch.direction = direction;
	}
	if (has(BattleFieldRole::Hidden) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::Hidden)) {
		const int hiddenValue = batch
			? SelectedComboValue(m_unitHiddenBatchValue)
			: (m_unitHidden.GetCheck() == BST_CHECKED ? 1 : 0);
		if (hiddenValue == INT_MIN) {
			SetDiagnostic(L"隐藏状态未选择");
			return;
		}
		const bool hidden = hiddenValue == 1;
		if (needs_update(hidden,
			[](const BattleUnit& unit) { return unit.hidden; }))
			patch.hidden = hidden;
	}
	if (has(BattleFieldRole::Level) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::Level)) {
		int level = 0;
		if (!ReadIntegerEdit(m_unitLevel, level)) {
			SetDiagnostic(L"等级偏移格式无效");
			return;
		}
		if (needs_update(level,
			[](const BattleUnit& unit) { return unit.level; }))
			patch.level = level;
	}
	if (has(BattleFieldRole::UnitType) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::UnitType)) {
		const int type = SelectedComboValue(m_unitType);
		if (type == INT_MIN) {
			SetDiagnostic(L"兵种级别未选择");
			return;
		}
		if (needs_update(type,
			[](const BattleUnit& unit) { return unit.unit_type; }))
			patch.unit_type = type;
	}
	if (has(BattleFieldRole::Reinforcement) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::Reinforcement)) {
		const int reinforcementValue = batch
			? SelectedComboValue(m_unitReinforcementBatchValue)
			: (m_unitReinforcement.GetCheck() == BST_CHECKED ? 1 : 0);
		if (reinforcementValue == INT_MIN) {
			SetDiagnostic(L"援军状态未选择");
			return;
		}
		const bool reinforcement = reinforcementValue == 1;
		if (needs_update(reinforcement,
			[](const BattleUnit& unit) { return unit.reinforcement; }))
			patch.reinforcement = reinforcement;
	}
	const int policy = SelectedComboValue(m_unitPolicy);
	if (has(BattleFieldRole::AiPolicy) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::AiPolicy)) {
		if (policy == INT_MIN) {
			SetDiagnostic(L"行动方针未选择");
			return;
		}
		if (needs_update(policy,
			[](const BattleUnit& unit) { return unit.ai_policy; }))
			patch.ai_policy = policy;
	}
	if (policy == INT_MIN && has(BattleFieldRole::AiPolicy) &&
		should_apply(ccz::editor_preview::BattleBatchParameter::Target)) {
		SetDiagnostic(L"行动方针未选择");
		return;
	}
	if (should_apply(ccz::editor_preview::BattleBatchParameter::Target) &&
		(policy == 3 || policy == 5) &&
		has(BattleFieldRole::TargetDataId)) {
		const int target = SelectedTargetDataId(m_unitTarget,
			m_unitTargetDataIds);
		if (target == INT_MIN) {
			SetDiagnostic(L"请从目标角色列表中选择 Data 号");
			return;
		}
		if (needs_update(target,
			[](const BattleUnit& unit) { return unit.target; }))
			patch.target = target;
	}
	if (should_apply(ccz::editor_preview::BattleBatchParameter::Target) &&
		(policy == 4 || policy == 6) &&
		has(BattleFieldRole::TargetX) &&
		has(BattleFieldRole::TargetY)) {
		int targetX = 0, targetY = 0;
		if (!ReadIntegerEdit(m_unitTargetX, targetX) ||
			!ReadIntegerEdit(m_unitTargetY, targetY)) {
			SetDiagnostic(L"目标坐标格式无效");
			return;
		}
		if (needs_update(targetX,
			[](const BattleUnit& unit) { return unit.target_x; }))
			patch.target_x = targetX;
		if (needs_update(targetY,
			[](const BattleUnit& unit) { return unit.target_y; }))
			patch.target_y = targetY;
	}
	if (!patch.data_id && !patch.position && !patch.direction && !patch.hidden &&
		!patch.level && !patch.unit_type && !patch.reinforcement && !patch.ai_policy &&
		!patch.target && !patch.target_x && !patch.target_y) {
		SetDiagnostic(L"参数没有变化");
		return;
	}
	const auto result = batch
		? m_batchUnitHandler(m_batchCacheIds, patch)
		: m_unitHandler(m_unitSnapshot.position, patch);
	SetDiagnostic(result.diagnostic);
}
