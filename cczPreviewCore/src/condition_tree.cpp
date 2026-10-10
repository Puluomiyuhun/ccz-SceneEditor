#include <ccz/preview/condition_tree.h>

#include <cstddef>
#include <utility>

namespace ccz::preview {
namespace {

void collect_execution_conditions(const std::vector<ExecutionNode>& nodes,
                                  std::vector<ConditionTreeNode>& destination);

void collect_execution_node(const ExecutionNode& node,
                            std::vector<ConditionTreeNode>& destination) {
    if (node.category == ExecutionCategory::Predicate) {
        ConditionTreeNode condition;
        condition.logical_key = node.logical_key;
        condition.instruction_id = node.instruction_id;
        condition.kind = node.predicate_kind;
        condition.predicate_state = node.predicate_state;
        if (node.instruction_id == 0x79) {
            condition.label = node.integer_expression.decoded
                                  ? format_integer_expression(
                                        node.integer_expression)
                                  : L"0x79 参数不可解码";
        } else {
            condition.label = node.decoded_parameters.empty()
                                  ? node.instruction_title
                                  : node.decoded_parameters;
        }
        condition.has_true_branch = !node.true_children.empty();
        condition.has_false_branch = !node.false_children.empty();
        collect_execution_conditions(node.true_children,
                                     condition.true_children);
        collect_execution_conditions(node.false_children,
                                     condition.false_children);
        collect_execution_conditions(node.ordinary_children,
                                     condition.true_children);
        destination.push_back(std::move(condition));
        return;
    }

    collect_execution_conditions(node.ordinary_children, destination);
    collect_execution_conditions(node.true_children, destination);
    collect_execution_conditions(node.false_children, destination);
    collect_execution_conditions(node.cases, destination);
}

void collect_execution_conditions(const std::vector<ExecutionNode>& nodes,
                                  std::vector<ConditionTreeNode>& destination) {
    for (const ExecutionNode& node : nodes) {
        collect_execution_node(node, destination);
    }
}

bool condition_node_equal(const ConditionTreeNode& left,
                          const ConditionTreeNode& right) {
    if (left.logical_key != right.logical_key ||
        left.instruction_id != right.instruction_id ||
        left.kind != right.kind ||
        left.predicate_state != right.predicate_state ||
        left.label != right.label ||
        left.has_true_branch != right.has_true_branch ||
        left.has_false_branch != right.has_false_branch ||
        left.true_children.size() != right.true_children.size() ||
        left.false_children.size() != right.false_children.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.true_children.size(); ++index) {
        if (!condition_node_equal(left.true_children[index],
                                  right.true_children[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.false_children.size(); ++index) {
        if (!condition_node_equal(left.false_children[index],
                                  right.false_children[index])) {
            return false;
        }
    }
    return true;
}

std::size_t count_nodes(const std::vector<ConditionTreeNode>& nodes) {
    std::size_t count = 0;
    for (const ConditionTreeNode& node : nodes) {
        ++count;
        count += count_nodes(node.true_children);
        count += count_nodes(node.false_children);
    }
    return count;
}

}  // namespace

bool condition_tree_model_equal(const ConditionTreeModel& left,
                                const ConditionTreeModel& right) {
    if (left.roots.size() != right.roots.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.roots.size(); ++index) {
        if (!condition_node_equal(left.roots[index], right.roots[index])) {
            return false;
        }
    }
    return true;
}

std::size_t condition_tree_node_count(const ConditionTreeModel& model) {
    return count_nodes(model.roots);
}

ConditionTreeModel build_condition_tree_from_execution_model(
    const SectionExecutionModel& model) {
    ConditionTreeModel result;
    collect_execution_conditions(model.entry_gates, result.roots);
    collect_execution_conditions(model.body, result.roots);
    return result;
}

}  // namespace ccz::preview
