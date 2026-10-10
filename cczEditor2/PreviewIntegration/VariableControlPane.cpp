#include "VariableControlPane.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cwchar>
#include <cwctype>
#include <cwchar>
#include <string>

namespace {

constexpr int kMargin = 6;
constexpr int kGap = 4;
constexpr int kRowHeight = 28;
constexpr int kSearchHeight = 28;
constexpr int kLabelWidth = 72;
constexpr int kEditWidth = 104;
constexpr int kOperationWidth = 112;

std::wstring lower_copy(const std::wstring& value)
{
	std::wstring result = value;
	for (wchar_t& character : result) {
		character = static_cast<wchar_t>(std::towlower(character));
	}
	return result;
}

bool row_matches_search(const ccz::preview::VariableControlRow& row,
	const std::wstring& search)
{
	if (search.empty()) {
		return true;
	}
	return lower_copy(ccz::preview::format_variable_control_row(row)).find(
		lower_copy(search)) != std::wstring::npos;
}

}  // namespace

BEGIN_MESSAGE_MAP(CVariableControlPane, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_CONTROL(EN_CHANGE, CVariableControlPane::kSearchEditId,
		&CVariableControlPane::OnSearchChanged)
	ON_COMMAND_RANGE(CVariableControlPane::kIntegerAssignButtonId,
		CVariableControlPane::kBooleanFalseButtonId,
		&CVariableControlPane::OnActionCommand)
END_MESSAGE_MAP()

bool CVariableControlPane::CreatePane(CWnd* owner, ActionHandler handler)
{
	if (owner == nullptr || owner->GetSafeHwnd() == nullptr) {
		return false;
	}
	m_actionHandler = std::move(handler);
	m_owner = owner;
	if (GetSafeHwnd() != nullptr) {
		return GetParent() == owner || GetOwner() == owner;
	}
	LPCTSTR class_name = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1), nullptr);
	if (class_name == nullptr) {
		return false;
	}
	const BOOL created = CreateEx(
		WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, class_name, L"变量控制",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
			WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 620, 420), owner, 0);
	if (!created) {
		m_actionHandler = nullptr;
		return false;
	}
	ShowWindow(SW_HIDE);
	return true;
}

void CVariableControlPane::PlaceNearOwner(int width, int height)
{
	if (m_hasInitialPlacement) {
		return;
	}
	CRect work_area;
	if (m_owner != nullptr && m_owner->GetSafeHwnd() != nullptr) {
		m_owner->GetWindowRect(&work_area);
	}
	if (work_area.IsRectEmpty()) {
		SystemParametersInfo(SPI_GETWORKAREA, 0, &work_area, 0);
	}
	int x = work_area.left + (work_area.Width() - width) / 2;
	int y = work_area.top + (work_area.Height() - height) / 2;
	if (!work_area.IsRectEmpty()) {
		const int left = static_cast<int>(work_area.left);
		const int top = static_cast<int>(work_area.top);
		const int right = static_cast<int>(work_area.right);
		const int bottom = static_cast<int>(work_area.bottom);
		x = (std::max)(left, (std::min)(x, right - width));
		y = (std::max)(top, (std::min)(y, bottom - height));
	}
	MoveWindow(x, y, width, height, FALSE);
	m_hasInitialPlacement = true;
}

void CVariableControlPane::SetDiagnosticHandler(DiagnosticHandler handler)
{
	m_diagnosticHandler = std::move(handler);
}

void CVariableControlPane::UpdateRows(
	const ccz::preview::ScriptVariableIndex& index,
	const ccz::preview::SimulationEnvironment& environment,
	const ccz::preview::LogicalNodeKey& section_key)
{
	ccz::preview::VariableControlKind previous_kind = m_selectedKind;
	const std::int32_t previous_id = m_selectedId;
	const bool had_selection = m_hasSelection;
	m_index = index;
	m_sectionKey = section_key;
	// The pane receives a value snapshot for display, but does not retain the
	// simulation environment.  PreviewSession::options() is the sole owner of
	// mutable values and publishes the next snapshot after a refresh.
	m_rows = ccz::preview::build_variable_control_rows(
		m_index, environment, L"", false, m_sectionKey);
	m_hasSelection = had_selection;
	m_selectedKind = previous_kind;
	m_selectedId = previous_id;
	RebuildVisibleRows();
}

void CVariableControlPane::ShowPane(bool show)
{
	if (GetSafeHwnd() == nullptr) {
		return;
	}
	if (show) {
		PlaceNearOwner(620, 420);
	}
	ShowWindow(show ? SW_SHOWNORMAL : SW_HIDE);
	if (show && m_list.GetSafeHwnd() != nullptr) {
		SetForegroundWindow();
		m_list.SetFocus();
	}
}

void CVariableControlPane::OnClose()
{
	ShowPane(false);
}

bool CVariableControlPane::HasRows() const noexcept
{
	return !m_rows.empty();
}

int CVariableControlPane::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1) {
		return -1;
	}
	m_searchEdit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
		ES_AUTOHSCROLL, CRect(0, 0, 1, 1), this, kSearchEditId);
	m_list.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_VSCROLL,
		CRect(0, 0, 1, 1), this, kVariableListId);
	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT |
		LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	m_list.InsertColumn(0, L"类型", LVCFMT_LEFT, 90);
	m_list.InsertColumn(1, L"变量", LVCFMT_LEFT, 110);
	m_list.InsertColumn(2, L"值", LVCFMT_LEFT, 100);
	m_list.InsertColumn(3, L"使用次数", LVCFMT_LEFT, 100);

	m_integerLabel.Create(L"整型值", WS_CHILD | SS_CENTERIMAGE,
		CRect(0, 0, 1, 1), this, kIntegerLabelId);
	m_booleanLabel.Create(L"布尔变量", WS_CHILD | SS_CENTERIMAGE,
		CRect(0, 0, 1, 1), this, kBooleanLabelId);
	m_integerEdit.Create(WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
		CRect(0, 0, 1, 1), this, kIntegerValueEditId);
	m_integerAssignButton.Create(L"变量赋值",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kIntegerAssignButtonId);
	m_integerResetButton.Create(L"重置为0",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kIntegerResetButtonId);
	m_booleanTrueButton.Create(L"设为True",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kBooleanTrueButtonId);
	m_booleanFalseButton.Create(L"设为False",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 1, 1), this,
		kBooleanFalseButtonId);

	m_font.CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	for (CWnd* control : {static_cast<CWnd*>(&m_searchEdit),
		static_cast<CWnd*>(&m_list), static_cast<CWnd*>(&m_integerLabel),
		static_cast<CWnd*>(&m_booleanLabel), static_cast<CWnd*>(&m_integerEdit),
		static_cast<CWnd*>(&m_integerAssignButton),
		static_cast<CWnd*>(&m_integerResetButton),
		static_cast<CWnd*>(&m_booleanTrueButton),
		static_cast<CWnd*>(&m_booleanFalseButton)}) {
		control->SetFont(&m_font);
	}
	LayoutControls(1, 1);
	UpdateActionControls();
	return 0;
}

void CVariableControlPane::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	LayoutControls(cx, cy);
}

void CVariableControlPane::OnDestroy()
{
	m_list.DeleteAllItems();
	m_visibleRowIndices.clear();
	m_rows.clear();
	m_index = {};
	m_sectionKey.clear();
	m_hasSelection = false;
	m_actionHandler = nullptr;
	m_diagnosticHandler = nullptr;
	m_font.DeleteObject();
	CWnd::OnDestroy();
}

void CVariableControlPane::LayoutControls(int width, int height)
{
	if (GetSafeHwnd() == nullptr) {
		return;
	}
	width = (std::max)(1, width);
	height = (std::max)(1, height);
	const int content_width = (std::max)(1, width - 2 * kMargin);
	if (m_searchEdit.GetSafeHwnd() != nullptr) {
		m_searchEdit.MoveWindow(kMargin, kMargin, content_width, kSearchHeight);
	}
	const int boolean_y = (std::max)(kMargin,
		height - kMargin - kRowHeight);
	const int integer_y = (std::max)(kMargin,
		boolean_y - kGap - kRowHeight);
	const int list_top = kMargin + kSearchHeight + kGap;
	const int list_height = (std::max)(1,
		integer_y - kGap - list_top);
	if (m_list.GetSafeHwnd() != nullptr) {
		m_list.MoveWindow(kMargin, list_top, content_width, list_height);
	}
	const int label_x = kMargin;
	const int edit_x = label_x + kLabelWidth + kGap;
	const int operation_x = edit_x + kEditWidth + kGap;
	const int reset_x = operation_x + kOperationWidth + kGap;
	const int reset_width = (std::max)(1,
		width - kMargin - reset_x);
	if (m_integerLabel.GetSafeHwnd() != nullptr) {
		m_integerLabel.MoveWindow(label_x, integer_y, kLabelWidth,
			kRowHeight);
	}
	if (m_booleanLabel.GetSafeHwnd() != nullptr) {
		m_booleanLabel.MoveWindow(label_x, boolean_y, kLabelWidth,
			kRowHeight);
	}
	if (m_integerEdit.GetSafeHwnd() != nullptr) {
		m_integerEdit.MoveWindow(edit_x, integer_y, kEditWidth, kRowHeight);
	}
	if (m_integerAssignButton.GetSafeHwnd() != nullptr) {
		m_integerAssignButton.MoveWindow(operation_x, integer_y,
			kOperationWidth, kRowHeight);
	}
	if (m_integerResetButton.GetSafeHwnd() != nullptr) {
		m_integerResetButton.MoveWindow(reset_x, integer_y, reset_width,
			kRowHeight);
	}
	if (m_booleanTrueButton.GetSafeHwnd() != nullptr) {
		m_booleanTrueButton.MoveWindow(operation_x, boolean_y,
			kOperationWidth, kRowHeight);
	}
	if (m_booleanFalseButton.GetSafeHwnd() != nullptr) {
		m_booleanFalseButton.MoveWindow(reset_x, boolean_y, reset_width,
			kRowHeight);
	}
}

void CVariableControlPane::RebuildVisibleRows()
{
	if (m_list.GetSafeHwnd() == nullptr) {
		return;
	}
	wchar_t search_buffer[256]{};
	m_searchEdit.GetWindowTextW(search_buffer,
		static_cast<int>(_countof(search_buffer)));
	const std::wstring search(search_buffer);
	m_rebuilding = true;
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	m_visibleRowIndices.clear();
	for (std::size_t row_index = 0; row_index < m_rows.size(); ++row_index) {
		const auto& row = m_rows[row_index];
		if (!row_matches_search(row, search)) {
			continue;
		}
		m_visibleRowIndices.push_back(row_index);
		const std::wstring type = row.kind ==
			ccz::preview::VariableControlKind::Boolean
			? L"布尔变量" : row.kind == ccz::preview::VariableControlKind::Pointer ? L"指针" : row.kind == ccz::preview::VariableControlKind::Memory ? L"模拟地址" : L"整型值";
		std::wstring variable = (row.kind ==
			ccz::preview::VariableControlKind::Boolean ? L"b" : row.kind == ccz::preview::VariableControlKind::Pointer ? L"p" : row.kind == ccz::preview::VariableControlKind::Memory ? L"@" : L"n") +
			std::to_wstring(row.id);
		if(row.kind==ccz::preview::VariableControlKind::Memory) {wchar_t address[16]{};swprintf_s(address,L"@0x%08X",static_cast<std::uint32_t>(row.id));variable=address;}
		const std::wstring value = row.kind ==
			ccz::preview::VariableControlKind::Boolean
			? (row.boolean_value ? L"True" : L"False")
			: ccz::preview::format_variable_control_value(row);
		const std::wstring usage = row.usage_summary;
		LVITEMW item{};
		item.mask = LVIF_TEXT;
		item.iItem = static_cast<int>(m_visibleRowIndices.size() - 1);
		item.pszText = const_cast<wchar_t*>(type.c_str());
		m_list.InsertItem(&item);
		const std::wstring columns[] = {variable, value, usage};
		for (int column = 0; column < 3; ++column) {
			m_list.SetItemText(item.iItem, column + 1,
				columns[column].c_str());
		}
	}
	int selected = -1;
	if (m_hasSelection) {
		for (std::size_t index = 0; index < m_visibleRowIndices.size();
			++index) {
			const auto& row = m_rows[m_visibleRowIndices[index]];
			if (row.kind == m_selectedKind && row.id == m_selectedId) {
				selected = static_cast<int>(index);
				break;
			}
		}
	}
	if (selected < 0 && !m_visibleRowIndices.empty()) {
		selected = 0;
	}
	if (selected >= 0) {
		m_list.SetItemState(selected, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
		const auto& row = m_rows[m_visibleRowIndices[
			static_cast<std::size_t>(selected)]];
		m_selectedKind = row.kind;
		m_selectedId = row.id;
		m_hasSelection = true;
	} else {
		m_hasSelection = false;
	}
	m_list.SetRedraw(TRUE);
	m_rebuilding = false;
	UpdateActionControls();
}

const ccz::preview::VariableControlRow* CVariableControlPane::SelectedRow() const
{
	if (m_list.GetSafeHwnd() == nullptr || m_visibleRowIndices.empty()) {
		return nullptr;
	}
	const int selected = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || static_cast<std::size_t>(selected) >=
		m_visibleRowIndices.size()) {
		return nullptr;
	}
	const std::size_t row_index = m_visibleRowIndices[
		static_cast<std::size_t>(selected)];
	return row_index < m_rows.size() ? &m_rows[row_index] : nullptr;
}

void CVariableControlPane::UpdateActionControls()
{
	const auto* row = SelectedRow();
	const bool integer = row != nullptr && row->kind !=
		ccz::preview::VariableControlKind::Boolean;
	const bool boolean = row != nullptr && row->kind ==
		ccz::preview::VariableControlKind::Boolean;
	for (CWnd* control : {static_cast<CWnd*>(&m_integerLabel),
		static_cast<CWnd*>(&m_integerEdit),
		static_cast<CWnd*>(&m_integerAssignButton),
		static_cast<CWnd*>(&m_integerResetButton)}) {
		if (control->GetSafeHwnd() != nullptr) {
			control->ShowWindow(integer ? SW_SHOW : SW_HIDE);
		}
	}
	for (CWnd* control : {static_cast<CWnd*>(&m_booleanLabel),
		static_cast<CWnd*>(&m_booleanTrueButton),
		static_cast<CWnd*>(&m_booleanFalseButton)}) {
		if (control->GetSafeHwnd() != nullptr) {
			control->ShowWindow(boolean ? SW_SHOW : SW_HIDE);
		}
	}
	if (m_integerEdit.GetSafeHwnd() != nullptr) {
		m_integerEdit.EnableWindow(integer);
		m_integerEdit.SetWindowTextW(
			integer ? (row->value_known ? ccz::preview::format_variable_control_value(*row).c_str() : L"0") : L"");
	}
	if (m_integerAssignButton.GetSafeHwnd() != nullptr) {
		m_integerAssignButton.EnableWindow(integer);
		m_integerResetButton.EnableWindow(integer);
	}
	if (m_booleanTrueButton.GetSafeHwnd() != nullptr) {
		m_booleanTrueButton.EnableWindow(boolean && !row->boolean_value);
		m_booleanFalseButton.EnableWindow(boolean && row->boolean_value);
	}
}

bool CVariableControlPane::ReadIntegerValue(std::int32_t& value) const
{
	if (m_integerEdit.GetSafeHwnd() == nullptr) {
		return false;
	}
	wchar_t buffer[64]{};
	const int length = m_integerEdit.GetWindowTextW(buffer,
		static_cast<int>(_countof(buffer)));
	if (length <= 0) {
		return false;
	}
    const auto* row = SelectedRow();
    const bool address = row && (row->kind == ccz::preview::VariableControlKind::Pointer || row->kind == ccz::preview::VariableControlKind::Memory);
    return ccz::preview::parse_simulated_control_value(buffer, address, value);
}

void CVariableControlPane::NotifyDiagnostic(const std::wstring& diagnostic)
{
	if (!diagnostic.empty() && m_diagnosticHandler) {
		m_diagnosticHandler(diagnostic);
	}
}

void CVariableControlPane::NotifyAction(
	ccz::preview::VariableControlAction action,
	std::int32_t integer_value)
{
	const auto* row = SelectedRow();
	if (!m_actionHandler || row == nullptr) {
		return;
	}
	if (row->kind == ccz::preview::VariableControlKind::Boolean &&
		(action != ccz::preview::VariableControlAction::SetBooleanTrue &&
		 action != ccz::preview::VariableControlAction::SetBooleanFalse)) {
		return;
	}
	if (row->kind != ccz::preview::VariableControlKind::Boolean &&
		(action != ccz::preview::VariableControlAction::SetIntegerValue &&
		 action != ccz::preview::VariableControlAction::RestoreIntegerDefault)) {
		return;
	}
	m_actionHandler(row->kind, row->id, action, integer_value);
}

void CVariableControlPane::OnSearchChanged()
{
	if (!m_rebuilding) {
		RebuildVisibleRows();
	}
}

void CVariableControlPane::OnActionCommand(UINT command_id)
{
	switch (command_id) {
		case kBooleanTrueButtonId:
			NotifyAction(ccz::preview::VariableControlAction::SetBooleanTrue);
			break;
		case kBooleanFalseButtonId:
			NotifyAction(ccz::preview::VariableControlAction::SetBooleanFalse);
			break;
		case kIntegerAssignButtonId: {
			std::int32_t value = 0;
			if (!ReadIntegerValue(value)) {
				NotifyDiagnostic(L"请输入有效整数；指针和地址值也支持0x十六进制、&a编号、&p编号");
				return;
			}
			NotifyAction(ccz::preview::VariableControlAction::SetIntegerValue,
				value);
			break;
		}
		case kIntegerResetButtonId:
			NotifyAction(ccz::preview::VariableControlAction::RestoreIntegerDefault);
			break;
		default:
			break;
	}
}

BOOL CVariableControlPane::OnNotify(WPARAM wParam, LPARAM lParam,
	LRESULT* pResult)
{
	const auto* header = reinterpret_cast<const NMHDR*>(lParam);
	if (header != nullptr && header->hwndFrom == m_list.GetSafeHwnd() &&
		header->code == LVN_ITEMCHANGED) {
		if (!m_rebuilding) {
			const auto* row = SelectedRow();
			if (row != nullptr) {
				m_selectedKind = row->kind;
				m_selectedId = row->id;
				m_hasSelection = true;
			} else {
				m_hasSelection = false;
			}
			UpdateActionControls();
		}
		*pResult = 0;
		return TRUE;
	}
	return CWnd::OnNotify(wParam, lParam, pResult);
}
