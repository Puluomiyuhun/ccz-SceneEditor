#pragma once

#include <ccz/preview/execution_model.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ccz::preview {

enum class BooleanVariableRole : std::uint8_t {
    TrueList,
    FalseList,
};

struct BooleanVariableUsage {
    std::int32_t id{};
    std::size_t occurrence_count{};
    std::size_t true_list_occurrences{};
    std::size_t false_list_occurrences{};
    std::vector<LogicalNodeKey> references;
    bool conflicts{};
};

struct IntegerVariableUsage {
    std::int32_t id{};
    std::size_t occurrence_count{};
    std::vector<LogicalNodeKey> references;
};

struct ScriptVariableIndex {
    LogicalNodeKey document_key;
    std::uint64_t source_fingerprint{};
    bool valid{};
    std::map<std::int32_t, BooleanVariableUsage> boolean_variables;
    std::map<std::int32_t, IntegerVariableUsage> integer_variables;
    std::map<std::int32_t, IntegerVariableUsage> pointer_variables;
    std::vector<std::wstring> diagnostics;
};

ScriptVariableIndex scan_script_variables(
    const std::vector<RawReplayNode>& document_nodes,
    const LogicalNodeKey& document_key);

void synchronize_boolean_defaults(SimulationEnvironment& environment,
                                  const ScriptVariableIndex& index);

void synchronize_integer_scope(SimulationEnvironment& environment,
                               const ScriptVariableIndex& index);

}  // namespace ccz::preview


