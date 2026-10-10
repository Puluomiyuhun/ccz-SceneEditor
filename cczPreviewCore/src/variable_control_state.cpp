#include <ccz/preview/variable_control_state.h>

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <string>
#include <cerrno>
#include <climits>

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

std::wstring format_variable_control_value(const VariableControlRow& row) {
 if(!row.value_known) return L"未确定";
 if(row.kind==VariableControlKind::Boolean) return row.boolean_value?L"True":L"False";
 if(row.kind==VariableControlKind::Pointer) {
  auto address=static_cast<std::uint32_t>(row.integer_value);
  for(bool p:{false,true}) {
   auto base=p?0xe0000000u:0xf0000000u;
   if(address>=base && address<base+4096u*4 && (address-base)%4==0)
    return (p?L"&p":L"&a")+std::to_wstring((address-base)/4);
  }
  wchar_t hex[16]{};std::swprintf(hex,16,L"0x%08X",address);return hex;
 }
 return std::to_wstring(row.integer_value);
}
bool parse_simulated_control_value(const std::wstring& text, bool address_value, std::int32_t& value) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    const auto trimmed = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    wchar_t* end = nullptr;
    errno = 0;
    if (address_value && trimmed.size() > 2 && trimmed[0] == L'&' &&
        (trimmed[1] == L'a' || trimmed[1] == L'p')) {
        const auto id = std::wcstoll(trimmed.c_str()+2, &end, 10);
        if(errno || end == trimmed.c_str()+2 || *end || id < 0 || id >= 4096) return false;
        value = static_cast<std::int32_t>(simulated_variable_address(trimmed[1] == L'p', static_cast<int>(id)));
        return true;
    }
    const int base = address_value && trimmed.size() > 2 && trimmed[0] == L'0' &&
        (trimmed[1] == L'x' || trimmed[1] == L'X') ? 16 : 10;
    const auto parsed = std::wcstoll(trimmed.c_str(), &end, base);
    if(errno || end == trimmed.c_str() || *end || parsed < INT32_MIN ||
       parsed > (address_value ? 4294967295LL : INT32_MAX)) return false;
    value = static_cast<std::int32_t>(static_cast<std::uint32_t>(parsed));
    return true;
}
std::wstring format_variable_control_row(const VariableControlRow& row) {
    std::wstring result = row.kind == VariableControlKind::Boolean
                              ? L"[布尔] b"
                              : row.kind == VariableControlKind::Pointer ? L"[指针] p" : row.kind == VariableControlKind::Memory ? L"[地址] " : L"[整型] n";
    result += std::to_wstring(row.id);
    result += L"  ";
    if (row.kind == VariableControlKind::Boolean) {
        result += row.boolean_value ? L"True" : L"False";
    } else {
        result += format_variable_control_value(row);
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
        row.value_known = !environment.unknown_integer_variables.count(row.id);
        row.occurrence_count = usage.occurrence_count;
        row.usage_summary = integer_summary(usage);
        if (matches_search(row, search)) {
            rows.push_back(std::move(row));
        }
    }
    for(const auto& entry:index.pointer_variables) {
        const auto& usage=entry.second;
        if(show_current_section_only && !reference_in_section(usage.references,current_section_key)) continue;
        VariableControlRow row;row.kind=VariableControlKind::Pointer;row.id=usage.id;
        auto found=environment.pointer_variables.find(row.id);
        row.is_default=found==environment.pointer_variables.end();row.integer_value=row.is_default?0:found->second;
        row.value_known = !row.is_default && !environment.unknown_pointer_variables.count(row.id);
        row.occurrence_count=usage.occurrence_count;row.usage_summary=integer_summary(usage);
        if(matches_search(row,search)) rows.push_back(row);
        if(row.value_known) {
            VariableControlRow memory;memory.kind=VariableControlKind::Memory;memory.id=row.integer_value;
            memory.is_default=!read_simulated_address(environment,static_cast<std::uint32_t>(memory.id),memory.integer_value);
            memory.value_known = !memory.is_default;
            memory.usage_summary=L"指针 p"+std::to_wstring(row.id)+L" 的目标";
            if(matches_search(memory,search)) rows.push_back(memory);
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

    if(kind==VariableControlKind::Pointer || kind==VariableControlKind::Memory) {
        auto address=kind==VariableControlKind::Pointer ? simulated_variable_address(true,id) : static_cast<std::uint32_t>(id);
        if(action==VariableControlAction::SetIntegerValue) {
            std::int32_t old;
            if(read_simulated_address(environment,address,old) && old==integer_value) return false;
            write_simulated_address(environment,address,integer_value);return true;
        }
        if(action==VariableControlAction::RestoreIntegerDefault) {
            for(bool pointer:{false,true}) {
                auto base=pointer?0xE0000000u:0xF0000000u;
                if(address>=base && address<base+4096u*4 && (address-base)%4==0) {
                    auto target=static_cast<int>((address-base)/4);
                    bool changed=(pointer?environment.unknown_pointer_variables:environment.unknown_integer_variables).erase(target)!=0;
                    changed=((pointer?environment.pointer_variables:environment.integer_variables).erase(target)!=0)||changed;
                    return changed;
                }
            }
            bool changed=environment.unknown_memory.erase(address)!=0;
            return environment.simulated_memory.erase(address)!=0 || changed;
        }
        return false;
    }
    if (action == VariableControlAction::SetIntegerValue) {
        const bool wasUnknown=environment.unknown_integer_variables.erase(id)!=0;
        const auto found = environment.integer_variables.find(id);
        if (found != environment.integer_variables.end() &&
            found->second == integer_value) {
            return wasUnknown;
        }
        if (integer_value == 0) {
            if (found == environment.integer_variables.end()) {
                return wasUnknown;
            }
            environment.integer_variables.erase(found);
            return true;
        }
        environment.integer_variables[id] = integer_value;
        return true;
    }
    if (action == VariableControlAction::RestoreIntegerDefault) {
        bool changed=environment.unknown_integer_variables.erase(id)!=0;
        return environment.integer_variables.erase(id) != 0 || changed;
    }
    return false;
}

}  // namespace ccz::preview

