#pragma once

#include <afxwin.h>

#include "BattleEditorOps.h"
#include "../CMyComboBox.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "BattleBatchParameterSelection.h"

class CBattleUnitParametersPane : public CWnd
{
public:
	using DefaultsHandler = std::function<ccz::editor_preview::BattleEditorResult(
		std::uint16_t, const ccz::editor_preview::BattlePlacementProfile&)>;
	using ResetHandler = std::function<ccz::editor_preview::BattleEditorResult(
		std::uint16_t)>;
	using UnitHandler = std::function<ccz::editor_preview::BattleEditorResult(
		const ccz::preview::BattleCoord&,
		const ccz::editor_preview::BattleUnitPatch&)>;
	using BatchUnitHandler = std::function<ccz::editor_preview::BattleEditorResult(
		const std::vector<std::uint64_t>&,
		const ccz::editor_preview::BattleUnitPatch&)>;
	using DiagnosticHandler = std::function<void(const std::wstring&)>;

	bool CreatePane(CWnd* owner, DefaultsHandler defaults_handler,
		ResetHandler reset_handler, UnitHandler unit_handler,
		BatchUnitHandler batch_unit_handler);
	void UpdateState(const ccz::editor_preview::BattleEditorState& state);
	void ShowDefaultsState(
		const ccz::editor_preview::BattleEditorState& state);
	void ShowSingleUnitState(
		const ccz::editor_preview::BattleEditorState& state,
		std::uint64_t cache_id);
	void ShowBatchState(
		const ccz::editor_preview::BattleEditorState& state,
		std::vector<std::uint64_t> cache_ids,
		ccz::editor_preview::BattleBatchParameterMask checked_parameters);
	void SetDiagnosticHandler(DiagnosticHandler handler);
	void ShowPane(bool show);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT create);
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnApplyDefaults();
	afx_msg void OnResetDefaults();
	afx_msg void OnApplyUnit();
	afx_msg void OnDefaultPolicyChanged();
	afx_msg void OnUnitPolicyChanged();
	afx_msg void OnBatchPolicyApplyChanged();
	DECLARE_MESSAGE_MAP()

private:
	enum class FormMode { Defaults, SingleUnit, Batch };
	enum : UINT {
		kDefaultsGroupId = 0x7F80,
		kCurrentGroupId,
		kDefaultsDirectionId,
		kDefaultsHiddenId,
		kDefaultsLevelId,
		kDefaultsTypeId,
		kDefaultsReinforcementId,
		kDefaultsPolicyId,
		kDefaultsTargetId,
		kDefaultsTargetXId,
		kDefaultsTargetYId,
		kApplyDefaultsId,
		kResetDefaultsId,
		kDefaultsStatusId,
		kUnitDataId,
		kUnitXId,
		kUnitYId,
		kUnitDirectionId,
		kUnitHiddenId,
		kUnitLevelId,
		kUnitTypeId,
		kUnitReinforcementId,
		kUnitPolicyId,
		kUnitTargetId,
		kUnitTargetXId,
		kUnitTargetYId,
		kApplyUnitId,
		kUnitStatusId,
		kUnitHiddenBatchValueId = 0x7FC8,
		kUnitReinforcementBatchValueId,
		kBatchApplyDirectionId = 0x7FD0,
		kBatchApplyHiddenId,
		kBatchApplyLevelId,
		kBatchApplyUnitTypeId,
		kBatchApplyReinforcementId,
		kBatchApplyPolicyId,
		kBatchApplyTargetId,
	};

	CWnd* m_owner{};
	CStatic m_defaultsGroup;
	CStatic m_currentGroup;
	CComboBox m_defaultsDirection;
	CButton m_defaultsHidden;
	CEdit m_defaultsLevel;
	CComboBox m_defaultsType;
	CButton m_defaultsReinforcement;
	CComboBox m_defaultsPolicy;
	CMyComboBox m_defaultsTarget;
	std::vector<int> m_defaultsTargetDataIds;
	CEdit m_defaultsTargetX;
	CEdit m_defaultsTargetY;
	CButton m_applyDefaults;
	CButton m_resetDefaults;
	CStatic m_defaultsStatus;
	CStatic m_modeInfo;
	CComboBox m_unitData;
	CEdit m_unitX;
	CEdit m_unitY;
	CComboBox m_unitDirection;
	CButton m_unitHidden;
	CComboBox m_unitHiddenBatchValue;
	CEdit m_unitLevel;
	CComboBox m_unitType;
	CButton m_unitReinforcement;
	CComboBox m_unitReinforcementBatchValue;
	CComboBox m_unitPolicy;
	CMyComboBox m_unitTarget;
	std::vector<int> m_unitTargetDataIds;
	CEdit m_unitTargetX;
	CEdit m_unitTargetY;
	CButton m_applyUnit;
	CStatic m_unitStatus;
	CButton m_batchApplyDirection;
	CButton m_batchApplyHidden;
	CButton m_batchApplyLevel;
	CButton m_batchApplyUnitType;
	CButton m_batchApplyReinforcement;
	CButton m_batchApplyPolicy;
	CButton m_batchApplyTarget;
	CStatic m_parameterPlaceholders[7];
	CFont m_font;
	std::vector<std::unique_ptr<CStatic>> m_labels;

	DefaultsHandler m_defaultsHandler;
	ResetHandler m_resetHandler;
	UnitHandler m_unitHandler;
	BatchUnitHandler m_batchUnitHandler;
	DiagnosticHandler m_diagnosticHandler;
	ccz::editor_preview::BattlePlacementDefaults m_defaultsSnapshot;
	ccz::editor_preview::BattlePlacementProfile m_profileSnapshot;
	ccz::preview::BattleUnit m_unitSnapshot;
	std::vector<ccz::preview::BattleUnit> m_unitSnapshots;
	std::vector<std::uint64_t> m_batchCacheIds;
	ccz::editor_preview::BattleBatchParameterMask m_batchSupportedParameters{};
	ccz::editor_preview::BattleBatchParameterMask m_initialBatchCheckedParameters{};
	std::uint16_t m_instructionId{0x4b};
	FormMode m_mode{FormMode::Defaults};
	bool m_hasCurrentUnit{};
	bool m_rebuilding{};
	bool m_batchSelectionPending{};

	void LayoutControls(int cx, int cy);
	void UpdateDefaultTargetControls();
	void UpdateUnitTargetControls();
	CButton* BatchParameterControl(
		ccz::editor_preview::BattleBatchParameter parameter) noexcept;
	bool BatchParameterSupported(
		ccz::editor_preview::BattleBatchParameter parameter) const noexcept;
	bool BatchParameterChecked(
		ccz::editor_preview::BattleBatchParameter parameter) const noexcept;
	void SetDiagnostic(const std::wstring& diagnostic);
	void UpdateIntegerEdit(CEdit& edit, int value);
	bool ReadIntegerEdit(const CEdit& edit, int& value) const;
	void SelectComboValue(CComboBox& combo, int value);
	int SelectedComboValue(const CComboBox& combo) const;
	int SelectedTargetDataId(const CMyComboBox& combo,
		const std::vector<int>& data_ids) const;
	void SetComboValue(CComboBox& combo, int value, const wchar_t* fallback_prefix);
	void AddCommonChoices();
	void UpdateControlRoles();
};
