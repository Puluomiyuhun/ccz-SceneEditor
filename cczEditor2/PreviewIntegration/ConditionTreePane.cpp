#include "ConditionTreePane.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cwchar>
#include <functional>
#include <map>
#include <string>

namespace {

constexpr int kMargin = 6;
constexpr int kButtonHeight = 24;
constexpr int kButtonGap = 4;
constexpr int kTreeItemHeight = 24;

bool is_displayed_category(ccz::preview::ExecutionCategory category) {
	using ccz::preview::ExecutionCategory;
	switch (category) {
		case ExecutionCategory::Predicate:
		case ExecutionCategory::Choice:
		case ExecutionCategory::Case:
		case ExecutionCategory::IntegerDispatchGroup:
		case ExecutionCategory::Diagnostic:
			return true;
		default:
			return false;
	}
}

bool node_blocks_subtree(const ccz::preview::ExecutionNode& node) {
	return node.category == ccz::preview::ExecutionCategory::Diagnostic ||
		(node.category == ccz::preview::ExecutionCategory::Case &&
			!node.diagnostic.empty());
}

bool action_enabled(const ccz::preview::ConditionTreeActionState& state,
	ccz::preview::ConditionTreeAction action) {
	using ccz::preview::ConditionTreeAction;
	switch (action) {
		case ConditionTreeAction::SetPredicateTrue:
			return state.can_set_true;
		case ConditionTreeAction::SetPredicateFalse:
			return state.can_set_false;
		case ConditionTreeAction::MakePredicateReachable:
			return state.can_make_reachable;
		case ConditionTreeAction::RestorePredicateDefault:
			return state.can_restore_predicate;
		case ConditionTreeAction::SetIntegerValue:
			return state.can_set_integer;
		case ConditionTreeAction::RestoreIntegerDefault:
			return state.can_restore_integer;
		case ConditionTreeAction::SelectCase:
			return state.can_select_case;
		case ConditionTreeAction::RestoreChoiceAuto:
			return state.can_restore_choice_auto;
	}
	return false;
}

bool replay_options_equal(const ccz::preview::ReplayOptions& left,
	const ccz::preview::ReplayOptions& right) {
	return left.unresolved_status_conditions == right.unresolved_status_conditions &&
		left.predicate_states == right.predicate_states &&
		left.selected_case_by_choice == right.selected_case_by_choice &&
		left.simulated_variables.boolean_variables ==
			right.simulated_variables.boolean_variables &&
		left.simulated_variables.integer_variables ==
			right.simulated_variables.integer_variables;
}

void create_condition_icons(CImageList& image_list) {
	if (!image_list.Create(16, 16, ILC_COLOR32 | ILC_MASK, 7, 0)) {
		return;
	}
	const std::array<COLORREF, 7> colors = {
		RGB(120, 120, 120), RGB(150, 150, 150), RGB(45, 105, 190),
		RGB(50, 165, 80), RGB(195, 65, 65), RGB(205, 150, 35),
		RGB(220, 165, 30)};
	for (const COLORREF color : colors) {
		CDC memory;
		CBitmap bitmap;
		if (!memory.CreateCompatibleDC(nullptr) ||
			!bitmap.CreateBitmap(16, 16, 1, 32, nullptr)) {
			return;
		}
		CBitmap* previous = memory.SelectObject(&bitmap);
		memory.FillSolidRect(0, 0, 16, 16, RGB(255, 0, 255));
		memory.FillSolidRect(2, 2, 12, 12, color);
		memory.SelectObject(previous);
		image_list.Add(&bitmap, RGB(255, 0, 255));
	}
}

}  // namespace

BEGIN_MESSAGE_MAP(CConditionTreePane, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_COMMAND_RANGE(CConditionTreePane::kTrueButtonId,
		CConditionTreePane::kRestoreChoiceButtonId, &CConditionTreePane::OnActionCommand)
END_MESSAGE_MAP()

bool CConditionTreePane::CreatePane(CWnd* owner, ActionHandler handler)
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
		WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, class_name, L"条件树",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
			WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 640, 560), owner, 0);
	if (!created) {
		m_actionHandler = nullptr;
		return false;
	}
	ShowWindow(SW_HIDE);
	return true;
}

void CConditionTreePane::PlaceNearOwner(int width, int height)
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

void CConditionTreePane::ShowPane(bool show)
{
	if (GetSafeHwnd() == nullptr) {
		return;
	}
	if (show) {
		PlaceNearOwner(640, 560);
	}
	ShowWindow(show ? SW_SHOWNORMAL : SW_HIDE);
	if (show) {
		SetForegroundWindow();
		m_tree.SetFocus();
	}
}

void CConditionTreePane::OnClose()
{
	// Closing the floating page hides it; the model and expansion state remain
	// available for the next click on the preview button.
	ShowPane(false);
}

void CConditionTreePane::UpdateModel(
	std::shared_ptr<const ccz::preview::SectionExecutionModel> model,
	const ccz::preview::LogicalNodeKey& active_node,
	const ccz::preview::ReplayOptions& options)
{
	const bool changed = m_model == nullptr || model == nullptr ||
		m_activeNode != active_node ||
		!replay_options_equal(m_options, options) ||
		(model != nullptr &&
			!ccz::preview::execution_model_equal(*m_model, *model));
	m_model = std::move(model);
	m_options = options;
	m_activeNode = active_node;
	if (GetSafeHwnd() == nullptr) {
		return;
	}
	if (changed) {
		if (m_model == nullptr) {
			m_activeNode.clear();
			m_selectedKey.clear();
			m_expandedState.clear();
			RebuildTree(true);
		} else {
			RebuildTree();
		}
	} else {
		UpdateActionControls();
	}
}

bool CConditionTreePane::HasControllableNodes() const noexcept
{
	if (m_model == nullptr) {
		return false;
	}
	const auto has_action = [this](const ccz::preview::ExecutionNode& node) {
		const auto state = ccz::preview::condition_tree_action_state(&node,
			m_options);
		return state.can_set_true || state.can_set_false ||
			state.can_restore_predicate ||
			state.can_set_integer || state.can_restore_integer ||
			state.can_select_case || state.can_restore_choice_auto;
	};
	std::function<bool(const std::vector<ccz::preview::ExecutionNode>&)> scan;
	scan = [&](const std::vector<ccz::preview::ExecutionNode>& nodes) {
		for (const auto& node : nodes) {
			if (has_action(node)) {
				return true;
			}
			if (scan(node.ordinary_children) || scan(node.true_children) ||
				scan(node.false_children) || scan(node.cases)) {
				return true;
			}
		}
		return false;
	};
	return scan(m_model->entry_gates) || scan(m_model->body);
}

int CConditionTreePane::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1) {
		return -1;
	}
	m_tree.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
		TVS_LINESATROOT | TVS_SHOWSELALWAYS,
		CRect(0, 0, 1, 1), this, kConditionTreeId);
	m_trueButton.Create(L"判定为真", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kTrueButtonId);
	m_falseButton.Create(L"判定为假", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kFalseButtonId);
	m_restoreButton.Create(L"恢复默认", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kRestoreButtonId);
	m_integerLabel.Create(L"整型值:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
		CRect(0, 0, 1, 1), this, kIntegerLabelId);
	m_integerEdit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
		ES_AUTOHSCROLL, CRect(0, 0, 1, 1), this, kIntegerValueEditId);
	m_integerButton.Create(L"设置整型值", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kIntegerValueButtonId);
	m_selectCaseButton.Create(L"选中此分支", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kSelectCaseButtonId);
	m_restoreChoiceButton.Create(L"恢复自动", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
		BS_PUSHBUTTON, CRect(0, 0, 1, 1), this, kRestoreChoiceButtonId);

	m_font.CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	for (CWnd* control : {static_cast<CWnd*>(&m_tree),
		static_cast<CWnd*>(&m_trueButton), static_cast<CWnd*>(&m_falseButton),
		static_cast<CWnd*>(&m_restoreButton), static_cast<CWnd*>(&m_integerLabel),
		static_cast<CWnd*>(&m_integerEdit), static_cast<CWnd*>(&m_integerButton),
		static_cast<CWnd*>(&m_selectCaseButton),
		static_cast<CWnd*>(&m_restoreChoiceButton)}) {
		control->SetFont(&m_font);
	}
	create_condition_icons(m_imageList);
	if (m_tree.GetSafeHwnd() != nullptr && m_imageList.GetSafeHandle() != nullptr) {
		m_tree.SetImageList(&m_imageList, TVSIL_NORMAL);
		m_tree.SetItemHeight(kTreeItemHeight);
	}
	LayoutControls(1, 1);
	UpdateActionControls();
	return 0;
}

void CConditionTreePane::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	LayoutControls(cx, cy);
}

void CConditionTreePane::OnDestroy()
{
	m_tree.DeleteAllItems();
	m_bindings.clear();
	m_model.reset();
	m_options = {};
	m_activeNode.clear();
	m_selectedKey.clear();
	m_expandedState.clear();
	m_imageList.DeleteImageList();
	m_font.DeleteObject();
	CWnd::OnDestroy();
}

void CConditionTreePane::LayoutControls(int width, int height)
{
	if (GetSafeHwnd() == nullptr) {
		return;
	}
	const int row_two_y = (std::max)(kMargin,
		height - kMargin - kButtonHeight);
	const int row_one_y = (std::max)(kMargin,
		row_two_y - kButtonGap - kButtonHeight);
	const int tree_height = (std::max)(1,
		row_one_y - kMargin - kButtonGap);
	const int tree_width = (std::max)(1, width - 2 * kMargin);
	if (m_tree.GetSafeHwnd() != nullptr) {
		m_tree.MoveWindow(kMargin, kMargin, tree_width, tree_height);
	}
	const int available_width = (std::max)(1, width - 2 * kMargin);
	const int button_width = (std::max)(1,
		(available_width - 4 * kButtonGap) / 5);
	int x = kMargin;
	for (CButton* button : {&m_trueButton, &m_falseButton, &m_restoreButton,
		&m_selectCaseButton, &m_restoreChoiceButton}) {
		if (button->GetSafeHwnd() != nullptr) {
			button->MoveWindow(x, row_one_y, button_width, kButtonHeight);
		}
		x += button_width + kButtonGap;
	}
	if (m_integerLabel.GetSafeHwnd() != nullptr) {
		m_integerLabel.MoveWindow(kMargin, row_two_y, 58, kButtonHeight);
	}
	if (m_integerEdit.GetSafeHwnd() != nullptr) {
		m_integerEdit.MoveWindow(kMargin + 62, row_two_y, 100, kButtonHeight);
	}
	if (m_integerButton.GetSafeHwnd() != nullptr) {
		m_integerButton.MoveWindow(kMargin + 166, row_two_y, 120,
			kButtonHeight);
	}
}

CConditionTreePane::TreeItemBinding*
CConditionTreePane::BindingForItem(HTREEITEM item)
{
	if (item == nullptr || m_tree.GetSafeHwnd() == nullptr) {
		return nullptr;
	}
	return reinterpret_cast<TreeItemBinding*>(m_tree.GetItemData(item));
}

const CConditionTreePane::TreeItemBinding*
CConditionTreePane::BindingForItem(HTREEITEM item) const
{
	if (item == nullptr || m_tree.GetSafeHwnd() == nullptr) {
		return nullptr;
	}
	return reinterpret_cast<const TreeItemBinding*>(m_tree.GetItemData(item));
}

const ccz::preview::ExecutionNode* CConditionTreePane::FindNode(
	const std::vector<ccz::preview::ExecutionNode>& nodes,
	const ccz::preview::LogicalNodeKey& key) const
{
	for (const auto& node : nodes) {
		if (node.logical_key == key) {
			return &node;
		}
		if (const auto* result = FindNode(node.ordinary_children, key)) {
			return result;
		}
		if (const auto* result = FindNode(node.true_children, key)) {
			return result;
		}
		if (const auto* result = FindNode(node.false_children, key)) {
			return result;
		}
		if (const auto* result = FindNode(node.cases, key)) {
			return result;
		}
	}
	return nullptr;
}

const ccz::preview::ExecutionNode* CConditionTreePane::FindNode(
	const ccz::preview::LogicalNodeKey& key) const
{
	if (m_model == nullptr) {
		return nullptr;
	}
	if (const auto* result = FindNode(m_model->entry_gates, key)) {
		return result;
	}
	return FindNode(m_model->body, key);
}

bool CConditionTreePane::ContainsDisplayedNode(
	const std::vector<ccz::preview::ExecutionNode>& nodes) const
{
	for (const auto& node : nodes) {
		if (is_displayed_category(node.category)) {
			return true;
		}
		if (ContainsDisplayedNode(node.ordinary_children) ||
			ContainsDisplayedNode(node.true_children) ||
			ContainsDisplayedNode(node.false_children) ||
			ContainsDisplayedNode(node.cases)) {
			return true;
		}
	}
	return false;
}

bool CConditionTreePane::NodeContainsTarget(
	const ccz::preview::ExecutionNode& node,
	const ccz::preview::LogicalNodeKey& target) const
{
	if (target.empty() || node_blocks_subtree(node)) {
		return false;
	}
	if (node.logical_key == target) {
		return true;
	}
	using ccz::preview::ChoiceState;
	using ccz::preview::ExecutionCategory;
	using ccz::preview::IntegerEvaluationState;
	using ccz::preview::PredicateKind;
	using ccz::preview::PredicateState;
	switch (node.category) {
		case ExecutionCategory::Predicate:
			if (node.predicate_kind == PredicateKind::Integer) {
				if (node.integer_evaluation == IntegerEvaluationState::True) {
					return SequenceContainsTarget(node.true_children, target);
				}
				if (node.integer_evaluation == IntegerEvaluationState::False) {
					return SequenceContainsTarget(node.false_children, target);
				}
				return false;
			}
			if (node.predicate_state == PredicateState::Unsupported ||
				node.predicate_kind == PredicateKind::Unsupported) {
				return false;
			}
			return node.predicate_state == PredicateState::ExplicitFalse
				? SequenceContainsTarget(node.false_children, target)
				: SequenceContainsTarget(node.true_children, target);
		case ExecutionCategory::Choice:
			if (SequenceContainsTarget(node.ordinary_children, target)) {
				return true;
			}
			if (node.choice_state == ChoiceState::Unselected) {
				return false;
			}
			for (const auto& case_node : node.cases) {
				if (case_node.choice_state == node.choice_state &&
					NodeContainsTarget(case_node, target)) {
					return true;
				}
			}
			return false;
		case ExecutionCategory::Case:
			return SequenceContainsTarget(node.ordinary_children, target);
		case ExecutionCategory::IntegerDispatchGroup:
			for (const auto& case_node : node.cases) {
				if (case_node.logical_key == target) {
					return true;
				}
				if (case_node.integer_evaluation == IntegerEvaluationState::True &&
					NodeContainsTarget(case_node, target)) {
					return true;
				}
			}
			return false;
		case ExecutionCategory::Diagnostic:
			return false;
		default:
			return SequenceContainsTarget(node.ordinary_children, target) ||
				SequenceContainsTarget(node.true_children, target) ||
				SequenceContainsTarget(node.false_children, target) ||
				SequenceContainsTarget(node.cases, target);
	}
}

bool CConditionTreePane::SequenceContainsTarget(
	const std::vector<ccz::preview::ExecutionNode>& nodes,
	const ccz::preview::LogicalNodeKey& target) const
{
	return std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) {
		return NodeContainsTarget(node, target);
	});
}

HTREEITEM CConditionTreePane::InsertBoundItem(
	HTREEITEM parent, const std::wstring& text,
	const ccz::preview::LogicalNodeKey& node_key,
	ccz::preview::ExecutionCategory category, VisualRole role,
	bool reachable, bool active_path, bool synthetic,
	ccz::preview::ConditionTreeVisualState visual_state,
	std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map)
{
	m_bindings.push_back({});
	TreeItemBinding& binding = m_bindings.back();
	binding.node_key = node_key;
	binding.category = category;
	binding.role = role;
	binding.reachable = reachable;
	binding.active_path = active_path;
	binding.synthetic = synthetic;
	binding.visual_state = visual_state;
	TVINSERTSTRUCTW insert{};
	insert.hParent = parent;
	insert.hInsertAfter = TVI_LAST;
	insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	insert.item.pszText = const_cast<wchar_t*>(text.c_str());
	insert.item.lParam = reinterpret_cast<LPARAM>(&binding);
	insert.item.iImage = (role == VisualRole::TrueBranch) ? 3 :
		(role == VisualRole::FalseBranch ? 4 :
			(category == ccz::preview::ExecutionCategory::Predicate ? 2 :
			(category == ccz::preview::ExecutionCategory::Choice ||
			 category == ccz::preview::ExecutionCategory::Case ? 5 :
			 category == ccz::preview::ExecutionCategory::Diagnostic ? 6 : 0)));
	insert.item.iSelectedImage = insert.item.iImage;
	const HTREEITEM item = m_tree.InsertItem(&insert);
	if (item == nullptr) {
		m_bindings.pop_back();
		return nullptr;
	}
	if (!node_key.empty()) {
		item_map[node_key] = item;
	}
	return item;
}

void CConditionTreePane::ExpandIfNeeded(HTREEITEM item,
	const ccz::preview::LogicalNodeKey& key, bool section_root,
	bool entry_root, bool active_path)
{
	if (item == nullptr) {
		return;
	}
	const bool expand = ccz::preview::condition_tree_should_expand(
		m_expandedState, key, section_root, entry_root, active_path);
	m_tree.Expand(item, expand ? TVE_EXPAND : TVE_COLLAPSE);
}

void CConditionTreePane::InsertBranch(
	HTREEITEM parent, const wchar_t* label, VisualRole role,
	const std::vector<ccz::preview::ExecutionNode>& nodes, bool reachable,
	const ccz::preview::LogicalNodeKey& parent_key,
	std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map)
{
	if (!ContainsDisplayedNode(nodes)) {
		return;
	}
	const ccz::preview::LogicalNodeKey key = parent_key +
		(role == VisualRole::TrueBranch ? L"|ui:true" : L"|ui:false");
	const bool active_path = reachable && SequenceContainsTarget(nodes, m_activeNode);
	const auto visual_state = reachable
		? (active_path ? ccz::preview::ConditionTreeVisualState::ActivePath
			: ccz::preview::ConditionTreeVisualState::Normal)
		: ccz::preview::ConditionTreeVisualState::Unreachable;
	const HTREEITEM item = InsertBoundItem(
		parent, label, key, ccz::preview::ExecutionCategory::ElseBranch, role,
		reachable, active_path, true, visual_state, item_map);
	if (item == nullptr) {
		return;
	}
	InsertSequence(item, nodes, reachable, false, item_map);
	ExpandIfNeeded(item, key, false, false, active_path);
}

void CConditionTreePane::InsertNode(
	HTREEITEM parent, const ccz::preview::ExecutionNode& node,
	bool reachable, bool root_item,
	std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map)
{
	const bool node_reachable = reachable && !node_blocks_subtree(node);
	const bool active_path = node_reachable && NodeContainsTarget(node, m_activeNode);
	const auto visual_state = ccz::preview::condition_tree_visual_state(
		node, node_reachable, active_path, m_options);
	const HTREEITEM item = InsertBoundItem(
		parent, ccz::preview::condition_tree_node_title(node, m_options),
		node.logical_key,
		node.category, VisualRole::Node, node_reachable, active_path,
		node.synthetic, visual_state, item_map);
	if (item == nullptr) {
		return;
	}
	InsertSequence(item, node.ordinary_children, node_reachable, false, item_map);

	if (!node.true_children.empty()) {
		bool branch_reachable = node_reachable;
		if (node.category == ccz::preview::ExecutionCategory::Predicate) {
			branch_reachable = ccz::preview::condition_tree_predicate_branch_reachable(
				node, node_reachable, true, m_options);
		}
		InsertBranch(item, L"真分支", VisualRole::TrueBranch,
			node.true_children, branch_reachable, node.logical_key, item_map);
	}
	if (!node.false_children.empty()) {
		bool branch_reachable = node_reachable;
		if (node.category == ccz::preview::ExecutionCategory::Predicate) {
			branch_reachable = ccz::preview::condition_tree_predicate_branch_reachable(
				node, node_reachable, false, m_options);
		}
		InsertBranch(item, L"Else分支", VisualRole::FalseBranch,
			node.false_children, branch_reachable, node.logical_key, item_map);
	}
	if (!node.cases.empty()) {
		for (const auto& case_node : node.cases) {
			bool case_reachable = node_reachable;
			if (node.category == ccz::preview::ExecutionCategory::Choice) {
				case_reachable = node_reachable &&
					node.choice_state != ccz::preview::ChoiceState::Unselected &&
					case_node.choice_state == node.choice_state;
			} else if (node.category ==
				ccz::preview::ExecutionCategory::IntegerDispatchGroup) {
				case_reachable = node_reachable &&
					(case_node.integer_evaluation ==
						ccz::preview::IntegerEvaluationState::True ||
					 case_node.logical_key == m_activeNode);
			} else if (node.category == ccz::preview::ExecutionCategory::Diagnostic) {
				case_reachable = false;
			}
			if (node_blocks_subtree(case_node)) {
				case_reachable = false;
			}
			InsertNode(item, case_node, case_reachable, false, item_map);
		}
	}
	if (ContainsDisplayedNode(node.ordinary_children) ||
		ContainsDisplayedNode(node.true_children) ||
		ContainsDisplayedNode(node.false_children) ||
		!node.cases.empty()) {
		ExpandIfNeeded(item, node.logical_key, false, root_item, active_path);
	}
}

void CConditionTreePane::InsertSequence(
	HTREEITEM parent, const std::vector<ccz::preview::ExecutionNode>& nodes,
	bool parent_reachable, bool root_items,
	std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map)
{
	for (const auto& node : nodes) {
		if (is_displayed_category(node.category)) {
			InsertNode(parent, node, parent_reachable, root_items, item_map);
			continue;
		}
		// Ordinary commands and structural containers are intentionally omitted,
		// but their nested predicates and choices remain visible.
		InsertSequence(parent, node.ordinary_children, parent_reachable, root_items,
			item_map);
		InsertSequence(parent, node.true_children, parent_reachable, root_items,
			item_map);
		InsertSequence(parent, node.false_children, parent_reachable, root_items,
			item_map);
		InsertSequence(parent, node.cases, parent_reachable, root_items, item_map);
	}
}

void CConditionTreePane::CaptureExpandedItems(HTREEITEM item)
{
	for (HTREEITEM current = item; current != nullptr;
		current = m_tree.GetNextSiblingItem(current)) {
		if (const auto* binding = BindingForItem(current);
			binding != nullptr && !binding->node_key.empty()) {
			m_expandedState[binding->node_key] =
				(m_tree.GetItemState(current, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
		}
		CaptureExpandedItems(m_tree.GetChildItem(current));
	}
}

void CConditionTreePane::CaptureExpandedState()
{
	if (m_tree.GetSafeHwnd() != nullptr) {
		CaptureExpandedItems(m_tree.GetRootItem());
	}
}

void CConditionTreePane::RebuildTree(bool reset_state)
{
	if (m_tree.GetSafeHwnd() == nullptr) {
		return;
	}
	if (reset_state) {
		m_activeNode.clear();
		m_selectedKey.clear();
		m_expandedState.clear();
	} else {
		CaptureExpandedState();
	}
	m_rebuilding = true;
	m_tree.DeleteAllItems();
	// TreeView item lParams point into this deque; delete items first.
	m_bindings.clear();
	std::map<ccz::preview::LogicalNodeKey, HTREEITEM> item_map;
	if (m_model == nullptr) {
		m_rebuilding = false;
		UpdateActionControls();
		return;
	}

	const auto section_key = m_model->section_key.empty()
		? ccz::preview::LogicalNodeKey(L"section") : m_model->section_key;
	const auto section_ui_key = section_key + L"|ui:section";
	const bool section_active =
		SequenceContainsTarget(m_model->entry_gates, m_activeNode) ||
		SequenceContainsTarget(m_model->body, m_activeNode);
	const HTREEITEM section = InsertBoundItem(
		TVI_ROOT, L"[Section] " + section_key, section_ui_key,
		ccz::preview::ExecutionCategory::SectionGate, VisualRole::SectionRoot,
		true, section_active, true,
		ccz::preview::ConditionTreeVisualState::Normal, item_map);
	if (section != nullptr) {
		const auto entry_key = section_key + L"|ui:entry";
		const bool entry_active =
			SequenceContainsTarget(m_model->entry_gates, m_activeNode);
		const HTREEITEM entry = InsertBoundItem(
			section, L"入口区域", entry_key,
			ccz::preview::ExecutionCategory::SectionGate, VisualRole::EntryRoot,
			true, entry_active, true,
			ccz::preview::ConditionTreeVisualState::Normal, item_map);
		if (entry != nullptr) {
			InsertSequence(entry, m_model->entry_gates, true, false, item_map);
			ExpandIfNeeded(entry, entry_key, false, true, entry_active);
		}

		const auto body_key = section_key + L"|ui:body";
		const bool body_active = SequenceContainsTarget(m_model->body, m_activeNode);
		const HTREEITEM body = InsertBoundItem(
			section, L"正文区域", body_key,
			ccz::preview::ExecutionCategory::SectionGate, VisualRole::BodyRoot,
			true, body_active, true,
			ccz::preview::ConditionTreeVisualState::Normal, item_map);
		if (body != nullptr) {
			InsertSequence(body, m_model->body, true, false, item_map);
			ExpandIfNeeded(body, body_key, false, true, body_active);
		}
		ExpandIfNeeded(section, section_ui_key, true, false, section_active);
	}

	HTREEITEM selected_item = nullptr;
	if (!m_activeNode.empty()) {
		const auto found = item_map.find(m_activeNode);
		if (found != item_map.end()) {
			selected_item = found->second;
			m_selectedKey = m_activeNode;
		}
	}
	if (selected_item == nullptr && !m_selectedKey.empty()) {
		const auto found = item_map.find(m_selectedKey);
		if (found != item_map.end()) {
			selected_item = found->second;
		}
	}
	if (selected_item != nullptr) {
		m_tree.SelectItem(selected_item);
	}
	m_rebuilding = false;
	UpdateActionControls();
}

void CConditionTreePane::UpdateActionControls()
{
	const auto* selected = FindNode(m_selectedKey);
	const auto state = ccz::preview::condition_tree_action_state(selected,
		m_options);
	if (m_trueButton.GetSafeHwnd() != nullptr) {
		m_trueButton.EnableWindow(state.can_set_true);
		m_falseButton.EnableWindow(state.can_set_false);
		m_restoreButton.EnableWindow(state.can_restore_predicate ||
			state.can_restore_integer);
		m_restoreButton.SetWindowTextW(state.can_restore_integer
			? L"恢复整型默认" : L"恢复默认");
		m_selectCaseButton.EnableWindow(state.can_select_case);
		m_restoreChoiceButton.EnableWindow(state.can_restore_choice_auto);
	}
	const BOOL show_integer = state.can_set_integer ? SW_SHOW : SW_HIDE;
	for (CWnd* control : {static_cast<CWnd*>(&m_integerLabel),
		static_cast<CWnd*>(&m_integerEdit), static_cast<CWnd*>(&m_integerButton)}) {
		if (control->GetSafeHwnd() != nullptr) {
			control->ShowWindow(show_integer);
		}
	}
	if (m_integerEdit.GetSafeHwnd() != nullptr) {
		m_integerEdit.EnableWindow(state.can_set_integer);
		if (selected != nullptr && selected->integer_value_available) {
			m_integerEdit.SetWindowTextW(
				std::to_wstring(selected->simulated_integer_value).c_str());
		} else if (!state.can_set_integer) {
			m_integerEdit.SetWindowTextW(L"");
		}
	}
}

bool CConditionTreePane::ReadIntegerValue(std::int32_t& value) const
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
	wchar_t* end = nullptr;
	errno = 0;
	const long long parsed = std::wcstoll(buffer, &end, 10);
	if (errno == ERANGE || end == buffer || *end != L'\0' ||
		parsed < INT_MIN || parsed > INT_MAX) {
		return false;
	}
	value = static_cast<std::int32_t>(parsed);
	return true;
}

void CConditionTreePane::NotifyAction(
	ccz::preview::ConditionTreeAction action, std::int32_t integer_value)
{
	if (!m_actionHandler || m_model == nullptr || m_selectedKey.empty()) {
		return;
	}
	const auto* selected = FindNode(m_selectedKey);
	if (selected == nullptr || !action_enabled(
		ccz::preview::condition_tree_action_state(selected, m_options), action)) {
		return;
	}
	// The pane emits a typed logical action.  Session mutation and refresh
	// scheduling stay in the CScenePreviewWnd owner.
	m_actionHandler(m_selectedKey, action, integer_value);
}

void CConditionTreePane::NotifyRestoreAction()
{
	const auto* selected = FindNode(m_selectedKey);
	if (selected == nullptr) {
		return;
	}
	const auto state = ccz::preview::condition_tree_action_state(selected,
		m_options);
	NotifyAction(state.can_restore_integer
		? ccz::preview::ConditionTreeAction::RestoreIntegerDefault
		: ccz::preview::ConditionTreeAction::RestorePredicateDefault);
}

void CConditionTreePane::OnActionCommand(UINT command_id)
{
	switch (command_id) {
		case kTrueButtonId:
			NotifyAction(ccz::preview::ConditionTreeAction::SetPredicateTrue);
			break;
		case kFalseButtonId:
			NotifyAction(ccz::preview::ConditionTreeAction::SetPredicateFalse);
			break;
		case kRestoreButtonId:
			NotifyRestoreAction();
			break;
		case kIntegerValueButtonId: {
			std::int32_t value = 0;
			if (ReadIntegerValue(value)) {
				NotifyAction(ccz::preview::ConditionTreeAction::SetIntegerValue,
					value);
			}
			break;
		}
		case kSelectCaseButtonId:
			NotifyAction(ccz::preview::ConditionTreeAction::SelectCase);
			break;
		case kRestoreChoiceButtonId:
			NotifyAction(ccz::preview::ConditionTreeAction::RestoreChoiceAuto);
			break;
		default:
			break;
	}
}

BOOL CConditionTreePane::OnNotify(WPARAM wParam, LPARAM lParam,
	LRESULT* pResult)
{
	const auto* header = reinterpret_cast<const NMHDR*>(lParam);
	if (header != nullptr && header->hwndFrom == m_tree.GetSafeHwnd()) {
		if (header->code == NM_CUSTOMDRAW) {
			auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
			if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
				*pResult = CDRF_NOTIFYITEMDRAW;
				return TRUE;
			}
			if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
				const auto* binding = BindingForItem(
					reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec));
				if (binding != nullptr) {
					COLORREF text = RGB(75, 75, 75);
					if (binding->role == VisualRole::TrueBranch) {
						text = RGB(0, 128, 0);
					} else if (binding->role == VisualRole::FalseBranch) {
						text = RGB(180, 40, 40);
					} else if (binding->category ==
						ccz::preview::ExecutionCategory::Predicate) {
						text = RGB(0, 78, 150);
					} else if (binding->category ==
						ccz::preview::ExecutionCategory::Choice ||
						binding->category == ccz::preview::ExecutionCategory::Case) {
						text = RGB(150, 95, 0);
					}
					if (!binding->reachable) {
						text = RGB(160, 160, 160);
					}
					if ((draw->nmcd.uItemState & CDIS_SELECTED) != 0) {
						draw->clrText = RGB(255, 255, 255);
						draw->clrTextBk = RGB(45, 105, 190);
					} else {
						draw->clrText = text;
						if (!binding->reachable) {
							draw->clrTextBk = RGB(245, 245, 245);
						} else if (binding->visual_state ==
							ccz::preview::ConditionTreeVisualState::Diagnostic ||
							binding->visual_state ==
							ccz::preview::ConditionTreeVisualState::Unselected) {
							draw->clrTextBk = RGB(255, 247, 214);
						} else if (binding->active_path) {
							draw->clrTextBk = RGB(224, 245, 228);
						}
					}
				}
				*pResult = CDRF_DODEFAULT;
				return TRUE;
			}
		}
		if (header->code == TVN_SELCHANGEDW) {
			const auto* notification =
				reinterpret_cast<const NMTREEVIEWW*>(lParam);
			if (!m_rebuilding && notification != nullptr) {
				const auto* binding = BindingForItem(notification->itemNew.hItem);
				m_selectedKey = binding == nullptr ? ccz::preview::LogicalNodeKey()
					: binding->node_key;
				UpdateActionControls();
			}
			*pResult = 0;
			return TRUE;
		}
		if (header->code == TVN_ITEMEXPANDEDW) {
			const auto* notification =
				reinterpret_cast<const NMTREEVIEWW*>(lParam);
			if (!m_rebuilding && notification != nullptr) {
				const auto* binding = BindingForItem(notification->itemNew.hItem);
				if (binding != nullptr && !binding->node_key.empty()) {
					m_expandedState[binding->node_key] =
						(notification->itemNew.state & TVIS_EXPANDED) != 0;
				}
			}
			*pResult = 0;
			return TRUE;
		}
	}
	return CWnd::OnNotify(wParam, lParam, pResult);
}
