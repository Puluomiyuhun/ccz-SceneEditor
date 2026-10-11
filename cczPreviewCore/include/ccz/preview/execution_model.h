#pragma once

#include <ccz/preview/dialogue_model.h>
#include <ccz/preview/instruction_catalog.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
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
    std::map<std::pair<std::int32_t,std::int32_t>,std::int32_t> data_attributes;
    std::map<std::int32_t, std::int32_t> pointer_variables;
    std::map<std::uint32_t, std::int32_t> simulated_memory;
    std::set<std::int32_t> unknown_pointer_variables;
    std::set<std::uint32_t> unknown_memory;
    bool boolean_values_invalidated{};
    std::set<std::int32_t> known_boolean_assignments;
    std::set<std::int32_t> unknown_integer_variables;
    // S-only preview identity context; R replay does not use this state.
    bool battle_unit_identity_available{};
    std::set<std::int32_t> known_battle_unit_data_ids;
    std::set<std::int32_t> unresolved_battle_status_data_ids;
};

// Offline address spaces: no process memory is read. Aliases address the live maps.
inline std::uint32_t simulated_variable_address(bool pointer, std::int32_t id) {
    return (pointer ? 0xE0000000u : 0xF0000000u) + static_cast<std::uint32_t>(id) * 4;
}
inline bool read_simulated_address(const SimulationEnvironment& e, std::uint32_t address, std::int32_t& value) {
    for (bool pointer : {false,true}) {
        const auto base = pointer ? 0xE0000000u : 0xF0000000u;
        if (address >= base && address < base + 4096u*4 && (address-base)%4==0) {
            int id = (address-base)/4;
            const auto& unknown = pointer ? e.unknown_pointer_variables : e.unknown_integer_variables;
            const auto& values = pointer ? e.pointer_variables : e.integer_variables;
            if (unknown.count(id)) return false;
            auto found = values.find(id);
            if (pointer && found == values.end()) return false;
            value = found == values.end() ? 0 : found->second; return true;
        }
    }
    if(e.unknown_memory.count(address)) return false;
    auto found=e.simulated_memory.find(address);
    value=found==e.simulated_memory.end() ? 0 : found->second;return true;
}
inline void write_simulated_address(SimulationEnvironment& e, std::uint32_t address, std::int32_t value) {
    for(bool pointer : {false,true}) {
        auto base=pointer ? 0xE0000000u : 0xF0000000u;
        if(address>=base && address<base+4096u*4 && (address-base)%4==0) {
            int id=(address-base)/4;
            (pointer ? e.pointer_variables : e.integer_variables)[id]=value;
            (pointer ? e.unknown_pointer_variables : e.unknown_integer_variables).erase(id);return;
        }
    }
    e.simulated_memory[address]=value;e.unknown_memory.erase(address);
}
inline bool read_simulated_operand(const SimulationEnvironment& e, int kind, int id, std::int32_t& value) {
    if(kind==0) {value=id;return true;}
    if((kind==3 || kind==5) && id>=0 && id<4096) {
        value=static_cast<std::int32_t>(simulated_variable_address(kind==3,id));return true;
    }
    if(kind!=0 && (id<0 || id>=4096)) return false;
    if(kind==4) return read_simulated_address(e,simulated_variable_address(false,id),value);
    if(kind==2) return read_simulated_address(e,simulated_variable_address(true,id),value);
    if(kind==1) {
        std::int32_t address;
        return read_simulated_operand(e,2,id,address) && read_simulated_address(e,static_cast<std::uint32_t>(address),value);
    }
    return false;
}

inline bool resolve_simulated_assignment_address(const SimulationEnvironment& environment,
    const IntegerExpressionOperand& operand, std::uint32_t& address) {
    if (operand.raw_value < 0 || operand.raw_value >= 4096) return false;
    if (operand.kind == IntegerOperandKind::IntegerVariable ||
        operand.kind == IntegerOperandKind::PointerVariable) {
        address = simulated_variable_address(operand.kind == IntegerOperandKind::PointerVariable, operand.raw_value);
        return true;
    }
    if (operand.kind == IntegerOperandKind::PointerValue) {
        std::int32_t pointer;
        if (!read_simulated_operand(environment, 2, operand.raw_value, pointer)) return false;
        address = static_cast<std::uint32_t>(pointer);
        return true;
    }
    return false;
}

// Persist only cells changed by a UI action, never replay-derived assignments.
inline void merge_simulation_changes(SimulationEnvironment& destination,
    const SimulationEnvironment& before, const SimulationEnvironment& after) {
    const auto merge_map = [](auto& out, const auto& prior, const auto& next) {
        for (const auto& item : prior) if (!next.count(item.first)) out.erase(item.first);
        for (const auto& item : next) {
            auto found = prior.find(item.first);
            if (found == prior.end() || found->second != item.second) out[item.first] = item.second;
        }
    };
    const auto merge_set = [](auto& out, const auto& prior, const auto& next) {
        for (const auto& item : prior) if (!next.count(item)) out.erase(item);
        for (const auto& item : next) if (!prior.count(item)) out.insert(item);
    };
    merge_map(destination.boolean_variables, before.boolean_variables, after.boolean_variables);
    merge_map(destination.integer_variables, before.integer_variables, after.integer_variables);
    merge_map(destination.pointer_variables, before.pointer_variables, after.pointer_variables);
    merge_map(destination.simulated_memory, before.simulated_memory, after.simulated_memory);
    merge_set(destination.unknown_integer_variables, before.unknown_integer_variables, after.unknown_integer_variables);
    merge_set(destination.unknown_pointer_variables, before.unknown_pointer_variables, after.unknown_pointer_variables);
    merge_set(destination.unknown_memory, before.unknown_memory, after.unknown_memory);
    merge_set(destination.known_boolean_assignments, before.known_boolean_assignments, after.known_boolean_assignments);
    if (before.boolean_values_invalidated != after.boolean_values_invalidated)
        destination.boolean_values_invalidated = after.boolean_values_invalidated;
}

inline bool simulation_environments_equal(const SimulationEnvironment& a, const SimulationEnvironment& b) {
    return a.boolean_variables == b.boolean_variables && a.integer_variables == b.integer_variables &&
        a.pointer_variables == b.pointer_variables && a.simulated_memory == b.simulated_memory &&
        a.data_attributes == b.data_attributes && a.unknown_pointer_variables == b.unknown_pointer_variables &&
        a.unknown_memory == b.unknown_memory && a.unknown_integer_variables == b.unknown_integer_variables &&
        a.boolean_values_invalidated == b.boolean_values_invalidated &&
        a.known_boolean_assignments == b.known_boolean_assignments &&
        a.battle_unit_identity_available == b.battle_unit_identity_available &&
        a.known_battle_unit_data_ids == b.known_battle_unit_data_ids &&
        a.unresolved_battle_status_data_ids == b.unresolved_battle_status_data_ids;
}

inline bool apply_simulated_arithmetic(SimulationEnvironment& e,const std::vector<std::int32_t>& f) {
 if(f.size()<5 || f[0]<0 || f[0]>2 || f[1]<0 || f[1]>=4096) return false;
 std::uint32_t address;
 if(f[0]==0) {std::int32_t pointer;if(!read_simulated_operand(e,2,f[1],pointer)) return false;address=static_cast<std::uint32_t>(pointer);}
 else address=simulated_variable_address(f[0]==1,f[1]);
 std::int32_t operand,current=0;
 if(!read_simulated_operand(e,f[3],f[4],operand) || (f[2]!=2 && !read_simulated_address(e,address,current))) return false;
 std::int64_t value=current;
 switch(f[2]) {case 0:value+=operand;break;case 1:value-=operand;break;case 2:value=operand;break;case 3:value*=operand;break;case 4:if(!operand)return false;value/=operand;break;case 5:if(!operand)return false;value%=operand;break;default:return false;}
 if(value<INT32_MIN || value>INT32_MAX) return false;
 write_simulated_address(e,address,static_cast<std::int32_t>(value));return true;
}
inline void invalidate_simulated_destination(SimulationEnvironment& e,const std::vector<std::int32_t>& fields) {
 if(fields.size()<2 || fields[1]<0 || fields[1]>=4096) return;
 if(fields[0]==2) e.unknown_integer_variables.insert(fields[1]);
 if(fields[0]==1) e.unknown_pointer_variables.insert(fields[1]);
 if(fields[0]==0) {
  std::int32_t pointer;if(!read_simulated_operand(e,2,fields[1],pointer)) return;
  auto address=static_cast<std::uint32_t>(pointer);e.unknown_memory.insert(address);
  for(bool p:{false,true}) {
   auto base=p?0xE0000000u:0xF0000000u;
   if(address>=base && address<base+4096u*4 && (address-base)%4==0)
    (p?e.unknown_pointer_variables:e.unknown_integer_variables).insert((address-base)/4);
  }
 }
}
inline void apply_simulated_avatar(SimulationEnvironment& e,const std::vector<std::int32_t>& fields) {
 if(fields.size()<4 || fields[0]<0 || fields[0]>1023 || fields[1]!=15) return;
 auto key=std::make_pair(fields[0],1);auto found=e.data_attributes.find(key);
 std::int64_t value=fields[3];
 if(fields[2]!=0) {
  if(found==e.data_attributes.end() || fields[2]<1 || fields[2]>2) {e.data_attributes.erase(key);return;}
  value=found->second+(fields[2]==1?(std::int64_t)fields[3]:-(std::int64_t)fields[3]);
 }
 if(value>=0 && value<=65535) e.data_attributes[key]=(int)value;else e.data_attributes.erase(key);
}
inline bool apply_simulated_property(SimulationEnvironment& e,const std::vector<std::int32_t>& f) {
 if(f.size()<4 || f[0]<0 || f[0]>=4096 || f[2]<0 || f[2]>1023) return false;
 auto key=std::make_pair(f[2],f[3]);
 if(f[1]==0) {
  auto found=e.data_attributes.find(key);
  if(found==e.data_attributes.end()) {e.unknown_integer_variables.insert(f[0]);return false;}
  write_simulated_address(e,simulated_variable_address(false,f[0]),found->second);return true;
 }
 if(f[1]==1) {std::int32_t value;if(!read_simulated_operand(e,4,f[0],value)) {e.data_attributes.erase(key);return false;}e.data_attributes[key]=value;return true;}
 return false;
}

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
    // Derived S preview display state; does not replace explicit user choices.
    std::set<LogicalNodeKey> unresolved_status_conditions;
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
