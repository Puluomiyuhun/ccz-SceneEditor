#pragma once

#include <afxcmn.h>

#include <ccz/preview/condition_tree_state.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class CConditionTreePane : public CWnd
{
public:
	using ActionHandler = std::function<void(
		const ccz::preview::LogicalNodeKey&,
		ccz::preview::ConditionTreeAction,
		std::int32_t)>;

	CConditionTreePane() noexcept = default;
	~CConditionTreePane() override = default;

	bool CreatePane(CWnd* owner, ActionHandler handler);
	void UpdateModel(
		std::shared_ptr<const ccz::preview::SectionExecutionModel> model,
		const ccz::preview::LogicalNodeKey& active_node,
		const ccz::preview::ReplayOptions& options);
	void ShowPane(bool show);
	bool HasControllableNodes() const noexcept;

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnActionCommand(UINT command_id);
	DECLARE_MESSAGE_MAP()

	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) override;

private:
	enum : UINT {
		kConditionTreeId = 0x7F20,
		kTrueButtonId,
		kFalseButtonId,
		kRestoreButtonId,
		kIntegerLabelId,
		kIntegerValueEditId,
		kIntegerValueButtonId,
		kSelectCaseButtonId,
		kRestoreChoiceButtonId,
	};

	enum class VisualRole : std::uint8_t {
		Node,
		SectionRoot,
		EntryRoot,
		BodyRoot,
		TrueBranch,
		FalseBranch,
	};

	struct TreeItemBinding {
		ccz::preview::LogicalNodeKey node_key;
		ccz::preview::ExecutionCategory category{
			ccz::preview::ExecutionCategory::Ordinary};
		VisualRole role{VisualRole::Node};
		bool reachable{true};
		bool active_path{};
		bool synthetic{};
		ccz::preview::ConditionTreeVisualState visual_state{
			ccz::preview::ConditionTreeVisualState::Normal};
	};

	CButton m_trueButton;
	CButton m_falseButton;
	CButton m_restoreButton;
	CStatic m_integerLabel;
	CEdit m_integerEdit;
	CButton m_integerButton;
	CButton m_selectCaseButton;
	CButton m_restoreChoiceButton;
	CTreeCtrl m_tree;
	CFont m_font;
	CImageList m_imageList;

	ActionHandler m_actionHandler;
	CWnd* m_owner = nullptr;
	bool m_hasInitialPlacement{};
	std::shared_ptr<const ccz::preview::SectionExecutionModel> m_model;
	ccz::preview::ReplayOptions m_options;
	ccz::preview::LogicalNodeKey m_activeNode;
	ccz::preview::LogicalNodeKey m_selectedKey;
	ccz::preview::ConditionTreeExpansionMap m_expandedState;
	std::deque<TreeItemBinding> m_bindings;
	bool m_rebuilding{};

	void PlaceNearOwner(int width, int height);

	void LayoutControls(int width, int height);
	void RebuildTree(bool reset_state = false);
	void CaptureExpandedState();
	void CaptureExpandedItems(HTREEITEM item);
	void UpdateActionControls();
	void NotifyAction(ccz::preview::ConditionTreeAction action,
		std::int32_t integer_value = 0);
	void NotifyRestoreAction();
	bool ReadIntegerValue(std::int32_t& value) const;

	TreeItemBinding* BindingForItem(HTREEITEM item);
	const TreeItemBinding* BindingForItem(HTREEITEM item) const;
	const ccz::preview::ExecutionNode* FindNode(
		const std::vector<ccz::preview::ExecutionNode>& nodes,
		const ccz::preview::LogicalNodeKey& key) const;
	const ccz::preview::ExecutionNode* FindNode(
		const ccz::preview::LogicalNodeKey& key) const;
	bool ContainsDisplayedNode(
		const std::vector<ccz::preview::ExecutionNode>& nodes) const;
	bool NodeContainsTarget(const ccz::preview::ExecutionNode& node,
		const ccz::preview::LogicalNodeKey& target) const;
	bool SequenceContainsTarget(
		const std::vector<ccz::preview::ExecutionNode>& nodes,
		const ccz::preview::LogicalNodeKey& target) const;

	HTREEITEM InsertBoundItem(
		HTREEITEM parent, const std::wstring& text,
		const ccz::preview::LogicalNodeKey& node_key,
		ccz::preview::ExecutionCategory category, VisualRole role,
		bool reachable, bool active_path, bool synthetic,
		ccz::preview::ConditionTreeVisualState visual_state,
		std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map);
	void InsertSequence(
		HTREEITEM parent, const std::vector<ccz::preview::ExecutionNode>& nodes,
		bool parent_reachable, bool root_items,
		std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map);
	void InsertNode(
		HTREEITEM parent, const ccz::preview::ExecutionNode& node,
		bool reachable, bool root_item,
		std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map);
	void InsertBranch(
		HTREEITEM parent, const wchar_t* label, VisualRole role,
		const std::vector<ccz::preview::ExecutionNode>& nodes,
		bool reachable, const ccz::preview::LogicalNodeKey& parent_key,
		std::map<ccz::preview::LogicalNodeKey, HTREEITEM>& item_map);
	void ExpandIfNeeded(HTREEITEM item,
		const ccz::preview::LogicalNodeKey& key, bool section_root,
		bool entry_root, bool active_path);
};
