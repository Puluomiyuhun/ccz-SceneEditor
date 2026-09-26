#include <ccz/preview/variable_control_state.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace ccz::preview {
namespace {

std::wstring lower_copy(const std::wstring& value) {
    std::wstring result = value;
    for (wchar_t& character : result) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    return result;
}

bool matches_search(const VariableControlRow& row,
                   const std::wstring& search) {
    if (search.empty()) {
        return true;
    }
    const std::wstring needle = lower_copy(search);
    return lower_copy(format_variable_control_row(row)).find(needle) !=
           std::wstring::npos;
}

bool reference_in_section(const std::vector<LogicalNodeKey>& references,
                          const LogicalNodeKey& section_key) {
    if (section_key.empty()) {
        return false;
    }
    return std::any_of(references.begin(), references.end(),
                       [&](const LogicalNodeKey& reference) {
                           return reference.rfind(section_key, 0) == 0;
                       });
}

std::wstring boolean_summary(const BooleanVariableUsage& usage) {
    return L"使用 " + std::to_wstring(usage.occurrence_count) + L" 次";
}

std::wstring integer_summary(const IntegerVariableUsage& usage) {
    return L"使用 " + std::to_wstring(usage.occurrence_count) + L" 次";
}

}  // namespace

std::wstring format_variable_control_row(const VariableControlRow& row) {
    std::wstring result = row.kind == VariableControlKind::Boolean
                              ? L"[布尔] b"
                              : L"[整型] n";
    result += std::to_wstring(row.id);
    result += L"  ";
    if (row.kind == VariableControlKind::Boolean) {
        result += row.boolean_value ? L"True" : L"False";
    } else {
        result += std::to_wstring(row.integer_value);
    }
    result += L"  " + row.usage_summary;
    return result;
}

std::vector<VariableControlRow> build_variable_control_rows(
    const ScriptVariableIndex& index, const SimulationEnvironment& environment,
    const std::wstring& search, bool show_current_section_only,
    const LogicalNodeKey& current_section_key) {
    std::vector<VariableControlRow> rows;
    for (const auto& entry : index.boolean_variables) {
        const BooleanVariableUsage& usage = entry.second;
        if (show_current_section_only &&
            !reference_in_section(usage.references, current_section_key)) {
            continue;
        }
        VariableControlRow row;
        row.kind = VariableControlKind::Boolean;
        row.id = usage.id;
        const auto value = environment.boolean_variables.find(usage.id);
        row.boolean_value = value != environment.boolean_variables.end() &&
                            value->second;
        row.is_default = !row.boolean_value;
        row.occurrence_count = usage.occurrence_count;
        row.usage_summary = boolean_summary(usage);
        if (matches_search(row, search)) {
            rows.push_back(std::move(row));
        }
    }
    for (const auto& entry : index.integer_variables) {
        const IntegerVariableUsage& usage = entry.second;
        if (show_current_section_only &&
            !reference_in_section(usage.references, current_section_key)) {
            continue;
        }
        VariableControlRow row;
        row.kind = VariableControlKind::Integer;
        row.id = usage.id;
        const auto value = environment.integer_variables.find(usage.id);
        row.integer_value = value == environment.integer_variables.end()
                                ? 0
                                : value->second;
        row.is_default = row.integer_value == 0;
        row.occurrence_count = usage.occurrence_count;
        row.usage_summary = integer_summary(usage);
        if (matches_search(row, search)) {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool apply_variable_control_action(
    SimulationEnvironment& environment, VariableControlKind kind,
    std::int32_t id, VariableControlAction action,
    std::int32_t integer_value) {
    if (kind == VariableControlKind::Boolean) {
        if (action == VariableControlAction::SetBooleanTrue) {
            const auto found = environment.boolean_variables.find(id);
            if (found != environment.boolean_variables.end() && found->second) {
                return false;
            }
            environment.boolean_variables[id] = true;
            return true;
        }
        if (action == VariableControlAction::SetBooleanFalse ||
            action == VariableControlAction::RestoreBooleanDefault) {
            const auto found = environment.boolean_variables.find(id);
            if (found == environment.boolean_variables.end() || !found->second) {
                return false;
            }
            environment.boolean_variables[id] = false;
            return true;
        }
        return false;
    }

    if (action == VariableControlAction::SetIntegerValue) {
        const auto found = environment.integer_variables.find(id);
        if (found != environment.integer_variables.end() &&
            found->second == integer_value) {
            return false;
        }
        if (integer_value == 0) {
            if (found == environment.integer_variables.end()) {
                return false;
            }
            environment.integer_variables.erase(found);
            return true;
        }
        environment.integer_variables[id] = integer_value;
        return true;
    }
    if (action == VariableControlAction::RestoreIntegerDefault) {
        return environment.integer_variables.erase(id) != 0;
    }
    return false;
}

}  // namespace ccz::preview


