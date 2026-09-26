#pragma once

#include <ccz/preview/dialogue_model.h>
#include <ccz/preview/instruction_catalog.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ccz::preview {

using LogicalNodeKey = std::wstring;
using RuntimeNodeHandle = std::uintptr_t;

struct RawReplayNode {
    LogicalNodeKey logical_key;
    RuntimeNodeHandle runtime_handle{};
    std::uint16_t instruction_id{};
    DialogueCommandView command;
    std::wstring source_text;
    std::vector<RawReplayNode> children;
};

enum class ExecutionCategory : std::uint8_t {
    Ordinary,
    Marker,
    UnconditionalContainer,
    Predicate,
    ElseBranch,
    Choice,
    Case,
    IntegerDispatchGroup,
    SectionGate,
    Trigger,
    Diagnostic,
};

enum class PredicateKind : std::uint8_t {
    None,
    Boolean,
    Integer,
    Unsupported,
};

enum class PredicateState : std::uint8_t {
    DefaultTrue,
    ExplicitTrue,
    ExplicitFalse,
    Unsupported,
};

enum class IntegerOperandKind : std::uint8_t {
    Constant,
    PointerValue,
    PointerVariable,
    PointerAddress,
    IntegerVariable,
    IntegerVariableAddress,
    Unsupported,
};

struct IntegerExpressionOperand {
    IntegerOperandKind kind{IntegerOperandKind::Unsupported};
    std::int32_t raw_value{};
};

enum class IntegerComparisonOperator : std::uint8_t {
    Equal,
    GreaterEqual,
    LessEqual,
    NotEqual,
    Unsupported,
};

struct IntegerComparisonExpression {
    bool decoded{};
    IntegerExpressionOperand left;
    IntegerComparisonOperator comparator{
        IntegerComparisonOperator::Unsupported};
    IntegerExpressionOperand right;
    std::wstring diagnostic;
};

enum class IntegerEvaluationState : std::uint8_t {
    NotEvaluated,
    True,
    False,
    Unsupported,
};

// The two maps are deliberately separate.  The editor's "Var" operands are
// boolean variables; 0x79's kind-4/kind-5 operands are integer variables.
// They must never share a numeric namespace or inherit each other's values.
struct SimulationEnvironment {
    std::map<std::int32_t, bool> boolean_variables;
    std::map<std::int32_t, std::int32_t> integer_variables;
};

// 0x05 stores two fixed-width variable lists in int_data rather than a
// display string: slots [0, 25) are required-true variables and slots
// [25, 50) are required-false variables.  A missing variable in the
// simulation environment has the documented default value false.
struct BooleanVariableExpression {
    bool decoded{};
    std::vector<std::int32_t> true_variables;
    std::vector<std::int32_t> false_variables;
    std::wstring diagnostic;
};

enum class BooleanEvaluationState : std::uint8_t {
    NotEvaluated,
    True,
    False,
    Unsupported,
};

enum class ChoiceState : std::uint8_t {
    AutoTarget,
    SelectedCase,
    Unselected,
};

struct ExecutionNode {
    LogicalNodeKey logical_key;
    RuntimeNodeHandle runtime_handle{};
    std::uint16_t instruction_id{};
    std::wstring instruction_title;
    std::wstring decoded_parameters;
    DialogueCommandView command;
    ExecutionCategory category{ExecutionCategory::Ordinary};
    PredicateKind predicate_kind{PredicateKind::None};
    PredicateState predicate_state{PredicateState::DefaultTrue};
    BooleanVariableExpression boolean_expression;
    BooleanEvaluationState boolean_evaluation{
        BooleanEvaluationState::NotEvaluated};
    IntegerComparisonExpression integer_expression;
    IntegerEvaluationState integer_evaluation{
        IntegerEvaluationState::NotEvaluated};
    // Captured from the typed simulation environment at model-build time so
    // the condition tree can show/edit the value without parsing display text.
    std::int32_t simulated_integer_value{};
    bool integer_value_available{};
    ChoiceState choice_state{ChoiceState::Unselected};
    bool synthetic{};
    std::wstring diagnostic;
    std::vector<ExecutionNode> ordinary_children;
    std::vector<ExecutionNode> true_children;
    std::vector<ExecutionNode> false_children;
    std::vector<ExecutionNode> cases;
};

struct SectionInput {
    LogicalNodeKey section_key;
    std::vector<RawReplayNode> nodes;
    LogicalNodeKey selected_target_key;
};

// Canonical replay state is keyed by logical node identity.
struct ReplayOptions {
    std::map<LogicalNodeKey, PredicateState> predicate_states;
    std::map<LogicalNodeKey, LogicalNodeKey> selected_case_by_choice;
    SimulationEnvironment simulated_variables;
};

struct SectionExecutionModel {
    LogicalNodeKey section_key;
    std::vector<ExecutionNode> entry_gates;
    std::vector<ExecutionNode> body;
    // A Section without its first top-level 0x00 is still useful for the
    // condition-tree projection, but it is not a safe replay boundary.
    bool boundary_complete{};
    LogicalNodeKey selected_target_key;
    std::uint64_t model_revision{};
    std::vector<std::wstring> diagnostics;
};

bool initialize_default_choice_selections(
    const SectionExecutionModel& model, ReplayOptions& options);

BooleanVariableExpression decode_boolean_variable_expression(
    const DialogueCommandView& command);

bool evaluate_boolean_variable_expression(
    const BooleanVariableExpression& expression,
    const SimulationEnvironment& environment, bool& result,
    std::wstring* diagnostic = nullptr);

bool apply_boolean_variable_assignment(
    SimulationEnvironment& environment,
    const BooleanVariableExpression& expression, bool desired_result,
    std::wstring* diagnostic = nullptr);

bool apply_integer_variable_assignment(
    SimulationEnvironment& environment,
    const IntegerComparisonExpression& expression, bool desired_result,
    std::wstring* diagnostic = nullptr);

IntegerComparisonExpression decode_integer_expression(
    const DialogueCommandView& command);

std::wstring format_integer_expression(
    const IntegerComparisonExpression& expression);

bool evaluate_integer_expression(const IntegerComparisonExpression& expression,
                                 const SimulationEnvironment& environment,
                                 bool& result,
                                 std::wstring* diagnostic = nullptr);

bool execution_node_reaches_target(const ExecutionNode& node,
                                   const LogicalNodeKey& target_node,
                                   const ReplayOptions& options);
bool execution_case_reaches_target(const ExecutionNode& case_node,
                                   const LogicalNodeKey& target_node,
                                   const ReplayOptions& options);
bool execution_nodes_reach_target(
    const std::vector<ExecutionNode>& nodes,
    const LogicalNodeKey& target_node, const ReplayOptions& options);

SectionExecutionModel build_section_execution_model(
    const SectionInput& input, const InstructionCatalog& catalog,
    const ReplayOptions& options);

bool execution_model_equal(const SectionExecutionModel& left,
                           const SectionExecutionModel& right);
std::size_t execution_node_count(const SectionExecutionModel& model);

LogicalNodeKey make_section_key(
    const std::wstring& document_path,
    const std::vector<std::uint32_t>& section_path);

LogicalNodeKey make_logical_node_key(
    const LogicalNodeKey& section_key,
    const std::vector<std::uint32_t>& child_path,
    std::uint16_t instruction_id);

}  // namespace ccz::preview
