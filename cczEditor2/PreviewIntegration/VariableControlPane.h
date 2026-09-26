#pragma once

#include <afxcmn.h>

#include <ccz/preview/variable_control_state.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class CVariableControlPane : public CWnd
{
public:
	using ActionHandler = std::function<void(
		ccz::preview::VariableControlKind,
		std::int32_t,
		ccz::preview::VariableControlAction,
		std::int32_t)>;
	using DiagnosticHandler = std::function<void(const std::wstring&)>;

	CVariableControlPane() noexcept = default;
	~CVariableControlPane() override = default;

	bool CreatePane(CWnd* owner, ActionHandler handler);
	void SetDiagnosticHandler(DiagnosticHandler handler);
	void UpdateRows(
		const ccz::preview::ScriptVariableIndex& index,
		const ccz::preview::SimulationEnvironment& environment,
		const ccz::preview::LogicalNodeKey& section_key);
	void ShowPane(bool show);
	bool HasRows() const noexcept;

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSearchChanged();
	afx_msg void OnActionCommand(UINT command_id);
	DECLARE_MESSAGE_MAP()

	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) override;

private:
	enum : UINT {
		kSearchEditId = 0x7F30,
		kVariableListId,
		kIntegerAssignButtonId,
		kIntegerResetButtonId,
		kBooleanTrueButtonId,
		kBooleanFalseButtonId,
		kIntegerLabelId,
		kBooleanLabelId,
		kIntegerValueEditId,
	};

	CEdit m_searchEdit;
	CListCtrl m_list;
	CStatic m_integerLabel;
	CStatic m_booleanLabel;
	CEdit m_integerEdit;
	CButton m_integerAssignButton;
	CButton m_integerResetButton;
	CButton m_booleanTrueButton;
	CButton m_booleanFalseButton;
	CFont m_font;

	ActionHandler m_actionHandler;
	DiagnosticHandler m_diagnosticHandler;
	CWnd* m_owner = nullptr;
	bool m_hasInitialPlacement{};
	ccz::preview::ScriptVariableIndex m_index;
	ccz::preview::LogicalNodeKey m_sectionKey;
	std::vector<ccz::preview::VariableControlRow> m_rows;
	std::vector<std::size_t> m_visibleRowIndices;
	ccz::preview::VariableControlKind m_selectedKind{
		ccz::preview::VariableControlKind::Boolean};
	std::int32_t m_selectedId{};
	bool m_hasSelection{};
	bool m_rebuilding{};

	void PlaceNearOwner(int width, int height);

	void LayoutControls(int width, int height);
	void RebuildVisibleRows();
	void UpdateActionControls();
	void NotifyAction(ccz::preview::VariableControlAction action,
		std::int32_t integer_value = 0);
	void NotifyDiagnostic(const std::wstring& diagnostic);
	bool ReadIntegerValue(std::int32_t& value) const;
	const ccz::preview::VariableControlRow* SelectedRow() const;
	};
