#pragma once

#include <ccz/preview/execution_model.h>

#include <cstddef>
#include <map>
#include <vector>

namespace ccz::preview {

struct ConditionTreeNode {
    LogicalNodeKey logical_key;
    std::uint16_t instruction_id{};
    PredicateKind kind{PredicateKind::None};
    PredicateState predicate_state{PredicateState::DefaultTrue};
    std::wstring label;
    bool has_true_branch{};
    bool has_false_branch{};
    std::vector<ConditionTreeNode> true_children;
    std::vector<ConditionTreeNode> false_children;
};

struct ConditionTreeModel {
    std::vector<ConditionTreeNode> roots;
};

bool condition_tree_model_equal(const ConditionTreeModel& left,
                                const ConditionTreeModel& right);

std::size_t condition_tree_node_count(const ConditionTreeModel& model);

ConditionTreeModel build_condition_tree_from_execution_model(
    const SectionExecutionModel& model);

}  // namespace ccz::preview

