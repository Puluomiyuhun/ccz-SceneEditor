#include <ccz/preview/battle_instruction_parser.h>
#include <ccz/preview/battle_field_spec.h>

#include <algorithm>
#include <map>
#include <sstream>

namespace ccz::preview {
namespace {

constexpr std::uint16_t kContainerInstruction = 0xffff;
constexpr std::uint16_t kBattleMapInstruction = 0x27;
constexpr std::uint16_t kBattleInitInstruction = 0x44;

bool validate_spec(const BattleFieldSpec& spec) {
    if (spec.record_stride == 0 ||
        spec.field_types.size() != spec.record_stride ||
        spec.field_roles.size() != spec.record_stride) {
        return false;
    }
    std::size_t byte_width = 0;
    for (const std::uint16_t type : spec.field_types) {
        byte_width += battle_field_type_byte_width(type);
    }
    return byte_width != 0;
}

void decode_unit_fields(const BattleFieldSpec& spec, BattleUnit& unit) {
    for (std::size_t index = 0; index < spec.field_roles.size(); ++index) {
        const std::int32_t value = unit.raw_fields[index];
        switch (spec.field_roles[index]) {
        case BattleFieldRole::DataId:
            unit.data_id = value;
            break;
        case BattleFieldRole::DeploymentIndex:
            unit.deployment_index = value;
            unit.battlefield_index = value;
            break;
        case BattleFieldRole::Hidden:
            unit.hidden = value != 0;
            break;
        case BattleFieldRole::X:
            unit.position.x = value;
            break;
        case BattleFieldRole::Y:
            unit.position.y = value;
            break;
        case BattleFieldRole::Direction:
            unit.direction = value;
            break;
        case BattleFieldRole::Level:
            unit.level = value;
            break;
        case BattleFieldRole::UnitType:
            unit.unit_type = value;
            break;
        case BattleFieldRole::Reinforcement:
            unit.reinforcement = value != 0;
            break;
        case BattleFieldRole::AiPolicy:
            unit.ai_policy = value;
            break;
        case BattleFieldRole::TargetDataId:
            unit.target = value;
            break;
        case BattleFieldRole::TargetX:
            unit.target_x = value;
            break;
        case BattleFieldRole::TargetY:
            unit.target_y = value;
            break;
        }
    }
    unit.fields_decoded = spec.semantic_complete;
    unit.diagnostic = L"单位字段已按编辑器源码解码；分组、页码与记录顺序仍仅供诊断，禁止写回";
}

bool in_scope(const RawReplayNode& node, const LogicalNodeKey& scope) {
    if (scope.empty()) {
        return false;
    }
    if (node.logical_key == scope) {
        return true;
    }
    if (node.logical_key.size() <= scope.size() ||
        node.logical_key.compare(0, scope.size(), scope) != 0) {
        return false;
    }
    // Synthetic test trees use "scope|node:..."; converted editor trees use
    // "document|node:path/...|instruction:id".  Both are descendants only
    // when a structural separator follows the requested prefix.
    const wchar_t separator = node.logical_key[scope.size()];
    return separator == L'|' || separator == L'/';
}

void add_diagnostic(BattleParseResult& result, const std::wstring& text) {
    if (std::find(result.diagnostics.begin(), result.diagnostics.end(), text) ==
        result.diagnostics.end()) {
        result.diagnostics.push_back(text);
    }
}

std::wstring hex_instruction(std::uint16_t instruction_id) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << instruction_id;
    return stream.str();
}

std::size_t effective_field_count(const BattleFieldSpec& spec,
                                  std::size_t stored_field_count) {
    // cczEditor2::InitData keeps fixed battle pages in a reserved 1000-value
    // vector.  Only the declared page capacity is meaningful; the remaining
    // values are editor storage, not additional records.  Keep accepting the
    // compact exact-record form used by replay snapshots and unit tests.
    if ((spec.instruction_id == 0x46 || spec.instruction_id == 0x47) &&
        stored_field_count == 1000) {
        return spec.record_capacity * spec.record_stride;
    }
    // 0x4B uses the editor's default 20-value vector even though its source
    // record has only five meaningful fields.
    if (spec.instruction_id == 0x4b && stored_field_count == 20) {
        return spec.record_stride;
    }
    return stored_field_count;
}

void parse_siblings(
    const std::vector<RawReplayNode>& siblings,
    const BattleParseOptions& options, BattleParseResult& result,
    bool& scope_seen,
    std::map<std::uint16_t, std::size_t>& page_counters);

void parse_node(const RawReplayNode& node, const BattleParseOptions& options,
                BattleParseResult& result, bool& scope_seen,
                std::map<std::uint16_t, std::size_t>& page_counters,
                std::size_t adjacent_predecessors) {
    const bool selected = in_scope(node, options.scope_root_key);
    if (selected) {
        scope_seen = true;
        const bool is_deployment = node.instruction_id == 0x4b ||
            node.instruction_id == 0x46 || node.instruction_id == 0x47;
        const bool deployment_reachable = !is_deployment ||
            !options.filter_deployment_nodes ||
            std::find(options.reachable_deployment_keys.begin(),
                      options.reachable_deployment_keys.end(),
                      node.logical_key) !=
                options.reachable_deployment_keys.end();
        const bool is_map_record = node.instruction_id == kBattleMapInstruction;
        const bool map_reachable = !is_map_record ||
            !options.filter_map_nodes ||
            std::find(options.reachable_map_keys.begin(),
                      options.reachable_map_keys.end(), node.logical_key) !=
                options.reachable_map_keys.end();
        if (!deployment_reachable || !map_reachable) {
            parse_siblings(node.children, options, result, scope_seen,
                           page_counters);
            return;
        }
        switch (node.instruction_id) {
        case kContainerInstruction:
            break;
        case kBattleInitInstruction:
            // 0x44 is only an initialization/boundary marker.  It has no
            // proven map field, so no payload is interpreted here.
            result.state.initialized = true;
            break;
        case kBattleMapInstruction:
            // cczEditor2View::EditData selects the dynamic field at
            // int_data[mode + 1].  Mode 3 is the battle-map form, therefore
            // the map index is the fifth normalized field, not int_data[1].
            if (node.command.int_data.empty()) {
                add_diagnostic(result, L"0x27 战场地图记录长度不足");
                break;
            }
            if (node.command.int_data[0] == 3) {
                const std::size_t map_field_index =
                    static_cast<std::size_t>(node.command.int_data[0]) + 1;
                if (node.command.int_data.size() <= map_field_index) {
                    add_diagnostic(result, L"0x27 战场地图记录长度不足");
                    break;
                }
                result.state.map.valid = true;
                result.state.map.map_id =
                    node.command.int_data[map_field_index];
                result.state.map.image_index = -1;
            }
            break;
        default: {
            const BattleFieldSpec* spec =
                get_battle_field_spec(node.instruction_id);
            if (spec == nullptr) {
                // A battle section also contains normal dialogue/control
                // instructions.  They are intentionally ignored here; only
                // records with a proven battle layout enter this state.
                break;
            }
            if (!validate_spec(*spec)) {
                add_diagnostic(result, hex_instruction(node.instruction_id) +
                                           L" 字段规范与规范化记录边界不一致");
                break;
            }
            const std::size_t field_count = effective_field_count(
                *spec, node.command.int_data.size());
            if (field_count % spec->record_stride != 0) {
                add_diagnostic(result, hex_instruction(node.instruction_id) +
                                           L" 记录长度不是完整字段步长");
                break;
            }
            const std::size_t row_count =
                field_count / spec->record_stride;
            if (node.instruction_id == 0x4b && row_count != 1) {
                add_diagnostic(result,
                               L"0x4B 单条来源记录必须恰好包含一个单位");
                break;
            }
            if (spec->record_capacity != 0 &&
                row_count > spec->record_capacity) {
                add_diagnostic(result, hex_instruction(node.instruction_id) +
                                           L" 记录数量超过编辑器字段容量");
                break;
            }
            const std::size_t page = spec->record_capacity == 0
                                         ? 0
                                         : page_counters[node.instruction_id]++;
            for (std::size_t row = 0; row < row_count; ++row) {
                BattleUnit unit;
                unit.source_instruction = node.instruction_id;
                unit.source_key = node.logical_key;
                unit.record_index = row;
                unit.page = static_cast<int>(page);
                if (node.instruction_id == 0x46) {
                    unit.battlefield_index = static_cast<int>(
                        20 + adjacent_predecessors * 20 + row);
                } else if (node.instruction_id == 0x47) {
                    unit.battlefield_index = static_cast<int>(
                        60 + adjacent_predecessors * 80 + row);
                }
                const auto begin = node.command.int_data.begin() +
                                   static_cast<std::ptrdiff_t>(row * spec->record_stride);
                const auto end = begin +
                                 static_cast<std::ptrdiff_t>(spec->record_stride);
                unit.raw_fields.assign(begin, end);
                if (spec->semantic_complete) {
                    decode_unit_fields(*spec, unit);
                }
                result.state.units.push_back(std::move(unit));
            }
            break;
        }
        }
    }

    parse_siblings(node.children, options, result, scope_seen,
                   page_counters);
}

void parse_siblings(
    const std::vector<RawReplayNode>& siblings,
    const BattleParseOptions& options, BattleParseResult& result,
    bool& scope_seen,
    std::map<std::uint16_t, std::size_t>& page_counters) {
    std::uint16_t previous_instruction = kContainerInstruction;
    std::size_t adjacent_predecessors = 0;
    bool has_previous = false;
    for (const RawReplayNode& node : siblings) {
        if (has_previous && node.instruction_id == previous_instruction) {
            ++adjacent_predecessors;
        } else {
            adjacent_predecessors = 0;
        }
        parse_node(node, options, result, scope_seen, page_counters,
                   adjacent_predecessors);
        previous_instruction = node.instruction_id;
        has_previous = true;
    }
}

}  // namespace

BattleParseResult parse_battle_instructions(
    const std::wstring& /*document_path*/,
    const std::vector<RawReplayNode>& document_nodes,
    const BattleParseOptions& options) {
    BattleParseResult result;
    bool scope_seen = false;
    std::map<std::uint16_t, std::size_t> page_counters;
    if (options.scope_root_key.empty()) {
        add_diagnostic(result, L"战场解析必须提供当前战场作用域");
        return result;
    }

    parse_siblings(document_nodes, options, result, scope_seen,
                   page_counters);

    if (!scope_seen) {
        add_diagnostic(result, L"未找到指定的战场作用域");
    }

    if (!result.state.initialized && result.state.units.empty() &&
        !result.state.map.valid && result.diagnostics.empty()) {
        add_diagnostic(result, L"当前战场作用域没有可识别的战场记录");
    }

    // Structural failures are always fatal.  Unit grouping/page/order is
    // intentionally not inferred here; parser output remains read-only.
    bool structural_failure = false;
    for (const std::wstring& diagnostic : result.diagnostics) {
        if (diagnostic.find(L"长度") != std::wstring::npos ||
            diagnostic.find(L"容量") != std::wstring::npos ||
            diagnostic.find(L"作用域") != std::wstring::npos ||
            diagnostic.find(L"必须恰好包含一个单位") != std::wstring::npos ||
            diagnostic.find(L"没有可识别") != std::wstring::npos) {
            structural_failure = true;
        }
    }
    result.field_table_complete = std::all_of(
        result.state.units.begin(), result.state.units.end(),
        [](const BattleUnit& unit) { return unit.fields_decoded; });
    result.ok = !structural_failure;
    return result;
}

}  // namespace ccz::preview
