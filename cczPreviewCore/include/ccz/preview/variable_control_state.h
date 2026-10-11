#pragma once

#include <ccz/preview/script_variable_index.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ccz::preview {

enum class VariableControlAction : std::uint8_t {
    SetBooleanTrue,
    SetBooleanFalse,
    RestoreBooleanDefault,
    SetIntegerValue,
    RestoreIntegerDefault,
};

enum class VariableControlKind : std::uint8_t {
    Boolean,
    Integer,
    Pointer,
    Memory,
};

struct VariableControlRow {
    VariableControlKind kind{};
    std::int32_t id{};
    std::int32_t integer_value{};
    bool boolean_value{};
    bool is_default{};
    std::size_t occurrence_count{};
    std::wstring usage_summary;
    bool value_known{true};
};

std::wstring format_variable_control_value(const VariableControlRow& row);
bool parse_simulated_control_value(const std::wstring& text, bool address_value,
                                  std::int32_t& value);

std::wstring format_variable_control_row(const VariableControlRow& row);

std::vector<VariableControlRow> build_variable_control_rows(
    const ScriptVariableIndex& index, const SimulationEnvironment& environment,
    const std::wstring& search, bool show_current_section_only,
    const LogicalNodeKey& current_section_key);

bool apply_variable_control_action(
    SimulationEnvironment& environment, VariableControlKind kind,
    std::int32_t id, VariableControlAction action,
    std::int32_t integer_value);

}  // namespace ccz::preview

