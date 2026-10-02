#include "BattleWritebackAdapter.h"
#include "BattlePreviewPredicates.h"
#include "EditorTreeSnapshot.h"
#include "PreviewWritebackGate.h"

#include <ccz/preview/battle_field_spec.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ccz::editor_preview {
namespace {

using ccz::preview::BattleFieldRole;
using ccz::preview::BattleFieldSpec;
using ccz::preview::BattleUnit;

struct BattleInsertAnchorKey {
    std::uintptr_t parent_handle{};
    std::uintptr_t before_handle{};

    bool operator==(const BattleInsertAnchorKey& other) const noexcept {
        return parent_handle == other.parent_handle &&
            before_handle == other.before_handle;
    }
};

struct BattleInsertAnchorKeyHash {
    std::size_t operator()(const BattleInsertAnchorKey& key) const noexcept {
        const std::size_t parent_hash =
            std::hash<std::uintptr_t>{}(key.parent_handle);
        const std::size_t before_hash =
            std::hash<std::uintptr_t>{}(key.before_handle);
        return parent_hash ^ (before_hash + static_cast<std::size_t>(0x9e3779b9) +
            (parent_hash << 6) + (parent_hash >> 2));
    }
};

std::int32_t empty_sentinel(BattleFieldRole role) {
    switch (role) {
    case BattleFieldRole::DataId:
    case BattleFieldRole::DeploymentIndex:
    case BattleFieldRole::TargetDataId:
        return -1;
    case BattleFieldRole::Direction:
        return -1;
    case BattleFieldRole::UnitType:
        return 2;
    case BattleFieldRole::AiPolicy:
        return 1;
    case BattleFieldRole::Reinforcement:
    case BattleFieldRole::Hidden:
    case BattleFieldRole::Level:
    case BattleFieldRole::X:
    case BattleFieldRole::Y:
    case BattleFieldRole::TargetX:
    case BattleFieldRole::TargetY:
        return 0;
    }
    return 0;
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_value(std::vector<std::uint8_t>& bytes, std::int32_t value,
                  std::uint16_t type) {
    if (ccz::preview::battle_field_type_byte_width(type) == 4U) {
        const auto raw = static_cast<std::uint32_t>(value);
        bytes.push_back(static_cast<std::uint8_t>(raw & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((raw >> 16) & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((raw >> 24) & 0xff));
        return;
    }
    append_u16(bytes, static_cast<std::uint16_t>(value));
}

bool writeback_evidence_complete(const BattleWritebackEvidence& evidence,
                                 std::wstring& diagnostic) {
    if (!evidence.field_table_verified) {
        diagnostic = L"战场写回被拒绝：字段表验证未闭合";
        return false;
    }
    if (!evidence.sample_manifest_verified || !evidence.group_order_verified) {
        diagnostic = L"战场写回被拒绝：样本或组顺序 manifest 未闭合";
        return false;
    }
    if (!evidence.native_reload_verified) {
        diagnostic = L"战场写回被拒绝：尚未通过主编辑器/原生重载验证";
        return false;
    }
    if (!evidence.kind300_external_acceptance_verified) {
        diagnostic = L"战场写回被拒绝：kind 300 外部接受性尚未验证";
        return false;
    }
    return true;
}

std::int32_t value_for_role(const BattleUnit& unit, BattleFieldRole role,
                            std::int32_t preserved) {
    if (!ccz::preview::is_battle_field_dirty(unit, role)) {
        return preserved;
    }
    switch (role) {
    case BattleFieldRole::DataId: return unit.data_id;
    case BattleFieldRole::DeploymentIndex: return unit.deployment_index;
    case BattleFieldRole::Reinforcement: return unit.reinforcement ? 1 : 0;
    case BattleFieldRole::Hidden: return unit.hidden ? 1 : 0;
    case BattleFieldRole::X: return unit.position.x;
    case BattleFieldRole::Y: return unit.position.y;
    // Direction stays in editor/raw field semantics.  The Pro UI's visual
    // 3-dir conversion belongs to its painter and must not leak into EEX.
    case BattleFieldRole::Direction: return unit.direction;
    case BattleFieldRole::Level: return unit.level;
    case BattleFieldRole::UnitType: return unit.unit_type;
    case BattleFieldRole::AiPolicy: return unit.ai_policy;
    case BattleFieldRole::TargetDataId: return unit.target;
    case BattleFieldRole::TargetX: return unit.target_x;
    case BattleFieldRole::TargetY: return unit.target_y;
    }
    return preserved;
}

struct GroupKey {
    ccz::preview::LogicalNodeKey source_key;
    std::uint16_t instruction{};
    std::size_t page{};

    bool operator==(const GroupKey& other) const {
        return source_key == other.source_key &&
               instruction == other.instruction && page == other.page;
    }
};

struct Group {
    GroupKey key;
    std::vector<const BattleUnit*> units;
};

std::wstring hex_instruction(std::uint16_t instruction) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << instruction;
    return stream.str();
}

bool build_group(const GroupKey& key, const std::vector<const BattleUnit*>& units,
                 BattleWritebackRecord& record, std::wstring& diagnostic) {
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(key.instruction);
    if (spec == nullptr || !spec->semantic_complete ||
        spec->record_stride == 0 ||
        spec->field_types.size() != spec->record_stride ||
        spec->field_roles.size() != spec->record_stride) {
        diagnostic = L"战场写回被拒绝：" + hex_instruction(key.instruction) +
                     L" 字段表不完整";
        return false;
    }

    std::vector<const BattleUnit*> ordered = units;
    std::sort(ordered.begin(), ordered.end(), [](const BattleUnit* left,
                                                  const BattleUnit* right) {
        return left->record_index < right->record_index;
    });
    if (key.instruction == 0x4b && ordered.size() != 1) {
        diagnostic = L"战场写回被拒绝：0x4B 单条来源记录不得合并多个单位";
        return false;
    }
    const std::size_t required = spec->record_capacity == 0
                                     ? ordered.size()
                                     : spec->record_capacity;
    if (ordered.size() != required) {
        diagnostic = L"战场写回被拒绝：" + hex_instruction(key.instruction) +
                     L" 固定槽记录数不完整";
        return false;
    }
    for (std::size_t row = 0; row < ordered.size(); ++row) {
        const BattleUnit& unit = *ordered[row];
        if (!unit.fields_decoded || unit.raw_fields.size() != spec->record_stride ||
            unit.record_index != row) {
            diagnostic = L"战场写回被拒绝：" + hex_instruction(key.instruction) +
                         L" 记录索引或原始字段不完整";
            return false;
        }
    }

    record.instruction_id = key.instruction;
    record.source_key = key.source_key;
    record.page = key.page;
    append_u16(record.bytes, record.instruction_id);
    for (const BattleUnit* unit : ordered) {
        for (std::size_t field = 0; field < spec->record_stride; ++field) {
            const std::int32_t value = value_for_role(
                *unit, spec->field_roles[field], unit->raw_fields[field]);
            record.fields.push_back(value);
            append_u16(record.bytes, spec->field_types[field]);
            append_value(record.bytes, value, spec->field_types[field]);
        }
    }
    return true;
}

bool key_in_scope(const ccz::preview::LogicalNodeKey& key,
                  const ccz::preview::LogicalNodeKey& scope) {
    if (key.empty() || scope.empty()) return false;
    if (key == scope) return true;
    if (key.size() <= scope.size() || key.compare(0, scope.size(), scope) != 0)
        return false;
    return key[scope.size()] == L'|' || key[scope.size()] == L'/';
}

std::size_t native_vector_size(std::uint16_t instruction) {
    switch (instruction) {
    case 0x4b: return 20U;
    case 0x46:
    case 0x47: return 1000U;
    case 0x32:
    case 0x55: return 6U;
    case 0x58: return 7U;
    default: return 0U;
    }
}

std::size_t record_count(const BattleFieldSpec& spec) {
    return spec.record_capacity == 0 ? 1U : spec.record_capacity;
}

bool valid_native_source(const BattleWritebackSourceInstruction& source,
                         std::wstring& diagnostic) {
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(source.instruction_id);
    if (spec == nullptr || !spec->semantic_complete ||
        spec->record_stride == 0 ||
        spec->field_roles.size() != spec->record_stride) {
        diagnostic = L"战场写回计划失败：出场指令字段表不完整";
        return false;
    }
    const std::size_t expected = native_vector_size(source.instruction_id);
    if (source.origin_int_data.size() != expected ||
        source.current_int_data.size() != expected) {
        diagnostic = L"战场写回计划失败：来源指令的原生 int_data 长度不符";
        return false;
    }
    if (source.instruction_id == 0x4b) return true;
    const auto data_id_field = std::find(
        spec->field_roles.begin(), spec->field_roles.end(),
        BattleFieldRole::DataId);
    if (data_id_field == spec->field_roles.end()) {
        diagnostic = L"战场写回计划失败：固定指令缺少 Data 号字段";
        return false;
    }
    const std::size_t data_id_offset = static_cast<std::size_t>(
        data_id_field - spec->field_roles.begin());
    for (std::size_t row = 0; row < record_count(*spec); ++row) {
        const std::size_t offset = row * spec->record_stride;
        if (source.current_int_data[offset + data_id_offset] < -1) {
            diagnostic = L"战场写回计划失败：固定指令第 " +
                std::to_wstring(row + 1) +
                L" 槽 Data 号小于空槽标记 -1，拒绝覆盖";
            return false;
        }
    }
    return true;
}

struct PlannedNode {
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey source_key;
    ccz::preview::LogicalNodeKey native_source_key;
    std::uintptr_t runtime_handle{};
    ccz::preview::LogicalNodeKey insert_parent_key;
    ccz::preview::LogicalNodeKey insert_before_key;
    int ordinal{-1};
    std::vector<std::int32_t> origin_int_data;
    std::vector<std::int32_t> int_data;
    std::vector<std::vector<std::int32_t>> group_records;
    std::wstring group_label;
    bool source_backed{};
    bool four_b_group{};
    bool changed{};
    bool deleted{};
};

struct PendingBinding {
    std::uint64_t cache_id{};
    std::size_t planned_node{};
    std::size_t record_index{};
};

struct MinionBrushBatch {
    std::uint64_t batch_id{};
    int first_data_id{-1};
    std::vector<int> data_ids;
};

bool fixed_row_is_empty(const PlannedNode& node, const BattleFieldSpec& spec,
                        std::size_t row) {
    const std::size_t offset = row * spec.record_stride;
    for (std::size_t field = 0; field < spec.record_stride; ++field) {
        if (spec.field_roles[field] == BattleFieldRole::DataId) {
            return node.int_data[offset + field] == -1;
        }
    }
    return false;
}

bool fixed_row_is_empty(const std::vector<std::int32_t>& int_data,
                        const BattleFieldSpec& spec, std::size_t row) {
    const std::size_t offset = row * spec.record_stride;
    for (std::size_t field = 0; field < spec.record_stride; ++field) {
        if (spec.field_roles[field] == BattleFieldRole::DataId) {
            return int_data[offset + field] == -1;
        }
    }
    return false;
}

void clear_fixed_row(PlannedNode& node, const BattleFieldSpec& spec,
                     std::size_t row) {
    const std::size_t offset = row * spec.record_stride;
    for (std::size_t field = 0; field < spec.record_stride; ++field)
        node.int_data[offset + field] = empty_sentinel(spec.field_roles[field]);
    node.changed = true;
}

void write_unit_row(PlannedNode& node, const BattleFieldSpec& spec,
                    std::size_t row, const BattleUnit& unit,
                    bool staged_new) {
    const std::size_t offset = row * spec.record_stride;
    for (std::size_t field = 0; field < spec.record_stride; ++field) {
        const BattleFieldRole role = spec.field_roles[field];
        const std::int32_t preserved = staged_new
            ? (field < unit.raw_fields.size() ? unit.raw_fields[field] : 0)
            : node.int_data[offset + field];
        if (staged_new || ccz::preview::is_battle_field_dirty(unit, role)) {
            node.int_data[offset + field] =
                value_for_role(unit, role, preserved);
        }
    }
    node.changed = true;
}

PlannedNode make_new_node(std::uint16_t instruction_id) {
    PlannedNode node;
    node.instruction_id = instruction_id;
    node.int_data.assign(native_vector_size(instruction_id), 0);
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(instruction_id);
    if (spec != nullptr && spec->record_capacity != 0) {
        for (std::size_t row = 0; row < spec->record_capacity; ++row)
            clear_fixed_row(node, *spec, row);
    } else if (instruction_id == 0x4b) {
        node.int_data[3] = -1;
    }
    return node;
}

void append_unique_cache_id(std::vector<std::uint64_t>& ids,
                            std::uint64_t cache_id) {
    if (cache_id != 0 &&
        std::find(ids.begin(), ids.end(), cache_id) == ids.end())
        ids.push_back(cache_id);
}

bool is_battle_deployment(std::uint16_t instruction_id) {
    return instruction_id == 0x4b || instruction_id == 0x46 ||
           instruction_id == 0x47;
}

bool is_battle_map_operation(std::uint16_t instruction_id) {
    return instruction_id == 0x32 || instruction_id == 0x55 ||
           instruction_id == 0x58;
}

bool is_map_operation_insert_valid(std::uint16_t instruction_id,
    const std::vector<std::int32_t>& int_data, int map_width,
    int map_height, std::wstring& diagnostic) {
    BattleMapOperationInstruction instruction{
        instruction_id, int_data, map_width, map_height};
    return ValidateBattleMapOperationInstruction(instruction, diagnostic);
}

std::wstring normalized_path(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 1 && path.back() == L'\\') path.pop_back();
    return path;
}

const ccz::preview::RawReplayNode* find_raw_node(
    const std::vector<ccz::preview::RawReplayNode>& nodes,
    std::uintptr_t runtime_handle, std::size_t& matches) {
    const ccz::preview::RawReplayNode* found = nullptr;
    for (const auto& node : nodes) {
        if (node.runtime_handle == runtime_handle) {
            ++matches;
            found = &node;
        }
        if (const auto* child = find_raw_node(node.children, runtime_handle,
                                               matches)) {
            found = child;
        }
    }
    return found;
}

const ccz::preview::RawReplayNode* find_raw_node_by_key(
    const std::vector<ccz::preview::RawReplayNode>& nodes,
    const ccz::preview::LogicalNodeKey& logical_key,
    std::size_t& matches) {
    const ccz::preview::RawReplayNode* found = nullptr;
    for (const auto& node : nodes) {
        if (node.logical_key == logical_key) {
            ++matches;
            found = &node;
        }
        if (const auto* child = find_raw_node_by_key(
                node.children, logical_key, matches)) {
            found = child;
        }
    }
    return found;
}

bool index_captured_nodes(
    const ccz::editor_preview::CapturedEditorNode& node,
    std::unordered_map<std::uintptr_t,
                       const ccz::editor_preview::CapturedEditorNode*>& index,
    std::wstring& diagnostic) {
    if (node.runtime_handle == 0 ||
        !index.emplace(node.runtime_handle, &node).second) {
        diagnostic = L"战场写回快照失败：TreeView 运行时句柄为空或重复";
        return false;
    }
    for (const auto& child : node.children) {
        if (!index_captured_nodes(child, index, diagnostic)) return false;
    }
    return true;
}

struct SelectedCapturedInstruction {
    const ccz::editor_preview::CapturedEditorNode* parent{};
    const ccz::editor_preview::CapturedEditorNode* selected{};
    std::size_t child_index{};
    std::size_t matches{};
};

void find_selected_captured_instruction(
    const ccz::editor_preview::CapturedEditorNode& node,
    const ccz::editor_preview::CapturedEditorNode* parent,
    std::size_t child_index, std::uintptr_t selected_handle,
    SelectedCapturedInstruction& result) {
    if (node.runtime_handle == selected_handle) {
        ++result.matches;
        result.parent = parent;
        result.selected = &node;
        result.child_index = child_index;
    }
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        find_selected_captured_instruction(node.children[index], &node,
            index, selected_handle, result);
    }
}

struct CapturedDeployment {
    std::uint16_t instruction_id{};
    ccz::preview::LogicalNodeKey key;
    const ccz::preview::RawReplayNode* raw{};
    const ccz::editor_preview::CapturedEditorNode* captured{};
};

bool collect_deployments(
    const ccz::preview::RawReplayNode& node,
    const ccz::preview::LogicalNodeKey& scope_key,
    const std::unordered_map<
        std::uintptr_t,
        const ccz::editor_preview::CapturedEditorNode*>& captured_index,
    std::vector<CapturedDeployment>& result,
    std::unordered_set<ccz::preview::LogicalNodeKey>& seen_keys,
    std::wstring& diagnostic) {
    if (is_battle_deployment(node.instruction_id)) {
        if (!key_in_scope(node.logical_key, scope_key) ||
            node.runtime_handle == 0 ||
            !seen_keys.emplace(node.logical_key).second) {
            diagnostic = L"战场写回快照失败：出场指令作用域、句柄或逻辑键无效";
            return false;
        }
        const auto captured = captured_index.find(node.runtime_handle);
        if (captured == captured_index.end() ||
            captured->second->instruction_id != node.instruction_id ||
            node.command.instruction_id != node.instruction_id ||
            captured->second->int_data.size() != node.command.int_data.size()) {
            diagnostic = L"战场写回快照失败：出场指令无法映射回原生 TreeView 节点";
            return false;
        }
        for (std::size_t field = 0;
             field < node.command.int_data.size(); ++field) {
            if (captured->second->int_data[field] !=
                node.command.int_data[field]) {
                diagnostic = L"战场写回快照失败：预览树与原生字段快照不一致";
                return false;
            }
        }
        if (captured->second->ordinal < 0) {
            diagnostic = L"战场写回快照失败：出场指令缺少原生树序号";
            return false;
        }
        result.push_back({node.instruction_id, node.logical_key, &node,
                          captured->second});
    }
    for (const auto& child : node.children) {
        if (!collect_deployments(child, scope_key, captured_index, result,
                                 seen_keys, diagnostic)) {
            return false;
        }
    }
    return true;
}

bool collect_section_deployments(
    const BattlePreviewCapture& capture,
    std::vector<CapturedDeployment>& result,
    std::wstring& diagnostic) {
    std::unordered_map<
        std::uintptr_t,
        const ccz::editor_preview::CapturedEditorNode*> captured_index;
    if (!index_captured_nodes(capture.snapshot.section_root, captured_index,
                              diagnostic)) {
        return false;
    }
    std::size_t section_matches = 0;
    const auto* raw_section = find_raw_node(
        capture.input.nodes, capture.snapshot.section_root.runtime_handle,
        section_matches);
    if (section_matches != 1 || raw_section == nullptr ||
        raw_section->instruction_id != 0xffff ||
        !key_in_scope(raw_section->logical_key, capture.input.scope_key)) {
        diagnostic = L"战场写回快照失败：无法唯一定位当前 Section 的完整树";
        return false;
    }

    std::unordered_set<ccz::preview::LogicalNodeKey> seen_keys;
    for (const auto& child : raw_section->children) {
        if (!collect_deployments(child, capture.input.scope_key,
                                 captured_index, result, seen_keys,
                                 diagnostic)) {
            return false;
        }
    }
    return true;
}

bool collect_max_native_ordinal(
    const ccz::editor_preview::CapturedEditorNode& node,
    int& maximum_ordinal) {
    if (node.instruction_id >= 0) {
        if (node.ordinal < 0) return false;
        if (node.ordinal > maximum_ordinal) maximum_ordinal = node.ordinal;
    }
    for (const auto& child : node.children) {
        if (!collect_max_native_ordinal(child, maximum_ordinal)) return false;
    }
    return true;
}

}  // namespace

bool CanDeleteBattleWritebackNativeNode(
    const BattleWritebackNativeNodeSnapshot& node) noexcept {
    const bool is_single_unit = node.instruction_id == 0x4b;
    const bool is_multi_unit_page = node.instruction_id == 0x46 ||
        node.instruction_id == 0x47;
    const bool is_map_operation =
        node.instruction_id == 0x32 || node.instruction_id == 0x55 ||
        node.instruction_id == 0x58;
    const std::size_t expected_size = is_single_unit ? 20U
        : is_multi_unit_page ? 1000U
        : node.instruction_id == 0x58 ? 7U : 6U;
    return node.handle != 0 &&
        (is_single_unit || is_multi_unit_page || is_map_operation) &&
        node.int_data.size() == expected_size && !node.has_children &&
        !node.has_long_text && !node.has_structural_links;
}

bool ValidateBattleWritebackNextOrdinal(
    const CapturedEditorTree& document_snapshot, int next_ordinal,
    std::wstring& diagnostic) {
    diagnostic.clear();
    if (document_snapshot.document_roots.empty()) {
        diagnostic = L"战场写回拒绝：完整剧本文档树为空，无法校验插入序号";
        return false;
    }

    int maximum_ordinal = -1;
    for (const auto& root : document_snapshot.document_roots) {
        if (!collect_max_native_ordinal(root, maximum_ordinal)) {
            diagnostic = L"战场写回拒绝：现存指令缺少有效的全局序号";
            return false;
        }
    }
    if (next_ordinal < 0 || next_ordinal <= maximum_ordinal) {
        diagnostic = L"战场写回拒绝：编辑器下一个指令序号不高于剧本现存最大序号";
        return false;
    }
    return true;
}

bool resolve_selected_instruction_anchor(
    const BattlePreviewCapture& capture,
    ccz::preview::LogicalNodeKey& parent_key,
    ccz::preview::LogicalNodeKey& before_key,
    std::uintptr_t& parent_handle,
    std::uintptr_t& before_handle,
    const ccz::preview::RawReplayNode*& parent_raw,
    std::wstring& diagnostic) {
    parent_key.clear();
    before_key.clear();
    parent_handle = 0;
    before_handle = 0;
    parent_raw = nullptr;
    const auto& path = capture.snapshot.selected_path;
    if (path.empty()) {
        diagnostic = L"当前没有选中可作为新增锚点的普通指令";
        return false;
    }
    const ccz::editor_preview::CapturedEditorNode* parent =
        &capture.snapshot.section_root;
    for (std::size_t depth = 0; depth + 1 < path.size(); ++depth) {
        if (path[depth] >= parent->children.size()) {
            diagnostic = L"当前选中指令路径已不在固定 Section 中";
            return false;
        }
        parent = &parent->children[path[depth]];
    }
    const std::size_t selected_index = path.back();
    if (selected_index >= parent->children.size() ||
        parent->children.empty() || selected_index + 1 >= parent->children.size()) {
        diagnostic = L"选中节点不是可定位的 Section 内普通指令";
        return false;
    }
    const auto& selected = parent->children[selected_index];
    const auto& terminal = parent->children.back();
    if (parent->instruction_id < 0 || parent->instruction_id == 1 ||
        parent->instruction_id == 3 || terminal.instruction_id != 0 ||
        !terminal.children.empty() || selected.instruction_id < 0 ||
        selected.instruction_id == 0 || selected.instruction_id == 1 ||
        selected.instruction_id == 3 || selected.runtime_handle == 0) {
        diagnostic = L"不能选择 Scene/Section、结构标记或事件结束节点作为插入锚点";
        return false;
    }
    const auto& before = parent->children[selected_index + 1];
    std::size_t parent_matches = 0;
    parent_raw = find_raw_node(capture.input.nodes, parent->runtime_handle,
                               parent_matches);
    if (parent_matches != 1 || parent_raw == nullptr ||
        parent_raw->children.size() != parent->children.size() ||
        selected_index + 1 >= parent_raw->children.size() ||
        parent_raw->children[selected_index].runtime_handle !=
            selected.runtime_handle ||
        parent_raw->children[selected_index + 1].runtime_handle !=
            before.runtime_handle ||
        parent_raw->children.back().instruction_id != 0 ||
        !parent_raw->children.back().children.empty() ||
        !key_in_scope(parent_raw->logical_key, capture.input.scope_key) ||
        !key_in_scope(parent_raw->children[selected_index + 1].logical_key,
                      capture.input.scope_key)) {
        diagnostic = L"选中指令的原生同级节点或末尾 0x00 事件终止符无法验证";
        parent_raw = nullptr;
        return false;
    }
    parent_key = parent_raw->logical_key;
    before_key = parent_raw->children[selected_index + 1].logical_key;
    parent_handle = parent_raw->runtime_handle;
    before_handle = parent_raw->children[selected_index + 1].runtime_handle;
    return true;
}

bool BuildBattleWritebackSectionSnapshot(
    const BattlePreviewCapture& baseline,
    const BattlePreviewCapture& current,
    BattleWritebackSectionSnapshot& output,
    std::wstring& diagnostic,
    BattleWritebackRevisionScope revision_scope,
    const BattleWritebackReachabilityScope* reachability_scope) {
    output = {};
    diagnostic.clear();
    const auto fail = [&output, &diagnostic](const wchar_t* reason) {
        output = {};
        diagnostic = reason;
        return false;
    };
    if (!baseline.ok || !current.ok ||
        baseline.mode.mode != ccz::preview::PreviewMode::Battle ||
        current.mode.mode != ccz::preview::PreviewMode::Battle) {
        return fail(L"战场写回快照失败：基线或当前捕获不是有效的 S 战场预览");
    }
    const bool revision_mismatch =
        revision_scope == BattleWritebackRevisionScope::WholeDocument &&
        (baseline.input.document_revision != current.input.document_revision ||
         baseline.input.section_revision != current.input.section_revision);
    if (normalized_path(baseline.input.document_path).empty() ||
        normalized_path(baseline.input.document_path) !=
            normalized_path(current.input.document_path) ||
        normalized_path(baseline.input.document_path) !=
            normalized_path(baseline.snapshot.document_path) ||
        normalized_path(current.input.document_path) !=
            normalized_path(current.snapshot.document_path) ||
        normalized_path(baseline.snapshot.document_path) !=
            normalized_path(current.snapshot.document_path) ||
        revision_mismatch ||
        baseline.input.scope_key.empty() ||
        baseline.input.scope_key != current.input.scope_key ||
        baseline.snapshot.section_path != current.snapshot.section_path ||
        baseline.snapshot.section_root.runtime_handle == 0 ||
        baseline.snapshot.section_root.runtime_handle !=
            current.snapshot.section_root.runtime_handle) {
        return fail(revision_scope == BattleWritebackRevisionScope::PinnedSection
            ? L"战场写回快照失败：文档、Scene1 Section 或来源版本已变化，请刷新预览"
            : L"战场写回快照失败：文档、Section 或加载后版本已变化，请刷新预览");
    }

    std::vector<CapturedDeployment> original;
    std::vector<CapturedDeployment> live;
    if (!collect_section_deployments(baseline, original, diagnostic) ||
        !collect_section_deployments(current, live, diagnostic)) {
        output = {};
        return false;
    }
    if (reachability_scope != nullptr &&
        reachability_scope->filter_deployments) {
        std::unordered_set<ccz::preview::LogicalNodeKey> reachable_keys;
        std::unordered_set<std::uintptr_t> reachable_handles;
        for (const auto& key : reachability_scope->reachable_deployment_keys) {
            if (!key_in_scope(key, current.input.scope_key) ||
                !reachable_keys.emplace(key).second) {
                return fail(L"战场写回快照失败：条件树出场来源键越界或重复");
            }
        }
        for (const std::uintptr_t handle :
             reachability_scope->reachable_deployment_handles) {
            if (handle == 0 || !reachable_handles.emplace(handle).second) {
                return fail(L"战场写回快照失败：条件树出场来源句柄为空或重复");
            }
        }
        const auto retain_baseline_reachable = [&reachable_keys](
            const CapturedDeployment& deployment) {
            return reachable_keys.find(deployment.key) != reachable_keys.end();
        };
        const auto retain_current_reachable = [&reachable_keys,
                &reachable_handles](const CapturedDeployment& deployment) {
            if (!reachable_handles.empty()) {
                return reachable_handles.find(
                    deployment.captured->runtime_handle) !=
                    reachable_handles.end();
            }
            return reachable_keys.find(deployment.key) != reachable_keys.end();
        };
        original.erase(std::remove_if(original.begin(), original.end(),
            [&retain_baseline_reachable](const CapturedDeployment& deployment) {
                return !retain_baseline_reachable(deployment);
            }), original.end());
        live.erase(std::remove_if(live.begin(), live.end(),
            [&retain_current_reachable](const CapturedDeployment& deployment) {
                return !retain_current_reachable(deployment);
            }), live.end());
        const std::size_t expected_live = reachable_handles.empty()
            ? reachable_keys.size() : reachable_handles.size();
        if (original.size() != reachable_keys.size() ||
            live.size() != expected_live) {
            return fail(L"战场写回快照失败：活动条件分支的出场来源无法在基线和当前树中一一映射");
        }
    }
    if (original.size() != live.size()) {
        return fail(L"战场写回快照失败：当前 Section 的出场指令集合与缓存基线不同");
    }

    BattleWritebackSectionSnapshot candidate;
    candidate.scope_key = current.input.scope_key;
    if (reachability_scope != nullptr) {
        std::unordered_map<std::uintptr_t, const CapturedEditorNode*>
            captured_by_handle;
        bool captured_nodes_indexed = false;
        const auto validate_anchor = [&](
            const ccz::preview::LogicalNodeKey& parent_key,
            const ccz::preview::LogicalNodeKey& before_key,
            std::uintptr_t parent_handle,
            std::uintptr_t before_handle,
            ccz::preview::LogicalNodeKey& resolved_parent_key,
            ccz::preview::LogicalNodeKey& resolved_before_key,
            std::wstring& anchor_diagnostic) {
            if (parent_key.empty() || before_key.empty()) {
                if (parent_handle == 0 || before_handle == 0) {
                    anchor_diagnostic = L"条件树插入父节点与后继锚点不完整";
                    return false;
                }
            }
            if ((parent_handle == 0 &&
                 !key_in_scope(parent_key, candidate.scope_key)) ||
                (before_handle == 0 &&
                 !key_in_scope(before_key, candidate.scope_key))) {
                anchor_diagnostic = L"条件树插入锚点越过当前 Section";
                return false;
            }
            std::size_t parent_matches = 0;
            std::size_t before_matches = 0;
            const auto* parent = parent_handle != 0
                ? find_raw_node(current.input.nodes, parent_handle,
                                parent_matches)
                : find_raw_node_by_key(current.input.nodes, parent_key,
                                       parent_matches);
            const auto* before = before_handle != 0
                ? find_raw_node(current.input.nodes, before_handle,
                                before_matches)
                : find_raw_node_by_key(current.input.nodes, before_key,
                                       before_matches);
            if (!captured_nodes_indexed) {
                if (!index_captured_nodes(current.snapshot.section_root,
                        captured_by_handle, anchor_diagnostic)) return false;
                captured_nodes_indexed = true;
            }
            if (parent_matches != 1 || before_matches != 1 ||
                parent == nullptr || before == nullptr ||
                !key_in_scope(parent->logical_key, candidate.scope_key) ||
                !key_in_scope(before->logical_key, candidate.scope_key) ||
                parent->children.empty() ||
                parent->runtime_handle == 0 || before->runtime_handle == 0) {
                anchor_diagnostic = L"条件树插入父节点或后继锚点已失效";
                return false;
            }
            const auto before_position = std::find_if(
                parent->children.begin(), parent->children.end(),
                [before](const ccz::preview::RawReplayNode& child) {
                    return child.runtime_handle == before->runtime_handle;
                });
            const auto& terminal = parent->children.back();
            if (before_position == parent->children.end() ||
                terminal.instruction_id != 0 || !terminal.children.empty()) {
                anchor_diagnostic =
                    L"条件树插入位置必须是事件终止符之前的同级节点";
                return false;
            }
            const auto captured_parent =
                captured_by_handle.find(parent->runtime_handle);
            const auto captured_before =
                captured_by_handle.find(before->runtime_handle);
            const auto captured_terminal =
                captured_by_handle.find(terminal.runtime_handle);
            if (captured_parent == captured_by_handle.end() ||
                captured_before == captured_by_handle.end() ||
                captured_terminal == captured_by_handle.end() ||
                captured_parent->second->instruction_id !=
                    static_cast<int>(parent->instruction_id) ||
                captured_parent->second->children.size() !=
                    parent->children.size() ||
                captured_parent->second->children.empty() ||
                captured_parent->second->children.back().runtime_handle !=
                    terminal.runtime_handle ||
                captured_parent->second->children[
                    static_cast<std::size_t>(before_position -
                        parent->children.begin())].runtime_handle !=
                    before->runtime_handle ||
                captured_terminal->second->instruction_id != 0 ||
                captured_terminal->second->runtime_handle == 0) {
                anchor_diagnostic =
                    L"条件树插入锚点无法映射回原生子事件";
                return false;
            }
            resolved_parent_key = parent->logical_key;
            resolved_before_key = before->logical_key;
            return true;
        };

        const auto& parent_key = reachability_scope->insert_parent_key;
        const auto& before_key = reachability_scope->insert_before_key;
        if (parent_key.empty() != before_key.empty()) {
            return fail(L"战场写回快照失败：条件树插入父节点与终止锚点不完整");
        }
        if (!parent_key.empty()) {
            std::wstring anchor_diagnostic;
            ccz::preview::LogicalNodeKey resolved_parent;
            ccz::preview::LogicalNodeKey resolved_before;
            if (!validate_anchor(parent_key, before_key, 0, 0,
                    resolved_parent, resolved_before, anchor_diagnostic)) {
                output = {};
                diagnostic = L"战场写回快照失败：" + anchor_diagnostic;
                return false;
            }
            candidate.insert_parent_key = std::move(resolved_parent);
            candidate.insert_before_key = std::move(resolved_before);
        }

        std::unordered_set<std::uint16_t> seen_anchor_types;
        for (const BattleDeploymentInsertionAnchor& anchor :
             reachability_scope->insertion_anchors) {
            if (!is_battle_deployment(anchor.instruction_id) ||
                !seen_anchor_types.insert(anchor.instruction_id).second) {
                return fail(L"战场写回快照失败：条件树插入锚点类别无效或重复");
            }
            if (!anchor.resolved) {
                if (!anchor.parent_key.empty() || !anchor.before_key.empty()) {
                    return fail(L"战场写回快照失败：未决锚点携带了可写入节点");
                }
                candidate.insertion_anchors.push_back(anchor);
                continue;
            }
            std::wstring anchor_diagnostic;
            ccz::preview::LogicalNodeKey resolved_parent;
            ccz::preview::LogicalNodeKey resolved_before;
            if (!validate_anchor(anchor.parent_key, anchor.before_key,
                    anchor.parent_handle, anchor.before_handle,
                    resolved_parent, resolved_before, anchor_diagnostic)) {
                output = {};
                diagnostic = L"战场写回快照失败：" + anchor_diagnostic;
                return false;
            }
            BattleDeploymentInsertionAnchor resolved = anchor;
            resolved.parent_key = std::move(resolved_parent);
            resolved.before_key = std::move(resolved_before);
            candidate.insertion_anchors.push_back(std::move(resolved));
        }

        if (!original.empty() || !live.empty()) {
            const auto own_anchor = std::find_if(
                candidate.insertion_anchors.begin(),
                candidate.insertion_anchors.end(),
                [](const BattleDeploymentInsertionAnchor& anchor) {
                    return anchor.instruction_id == 0x4b && anchor.resolved;
                });
            if (own_anchor != candidate.insertion_anchors.end()) {
                std::size_t matches = 0;
                const auto* parent = find_raw_node_by_key(current.input.nodes,
                    own_anchor->parent_key, matches);
                candidate.has_current_own_deployment_group = matches == 1 &&
                    parent != nullptr && parent->instruction_id == 2 &&
                    parent->command.text == L"我军出场";
            }
        }
    }
    if (original.empty() && live.empty()) {
        candidate.insert_parent_key.clear();
        candidate.insert_before_key.clear();
        candidate.insertion_anchors.clear();
        ccz::preview::LogicalNodeKey selected_parent_key;
        ccz::preview::LogicalNodeKey selected_before_key;
        std::uintptr_t selected_parent_handle = 0;
        std::uintptr_t selected_before_handle = 0;
        const ccz::preview::RawReplayNode* selected_parent_raw = nullptr;
        std::wstring selected_diagnostic;
        if (resolve_selected_instruction_anchor(current,
                selected_parent_key, selected_before_key,
                selected_parent_handle, selected_before_handle,
                selected_parent_raw, selected_diagnostic)) {
            candidate.insert_parent_key = selected_parent_key;
            candidate.insert_before_key = selected_before_key;
            candidate.has_current_own_deployment_group =
                selected_parent_raw->instruction_id == 2 &&
                selected_parent_raw->command.text == L"我军出场";
            for (const std::uint16_t instruction_id : {
                     std::uint16_t(0x4b), std::uint16_t(0x46),
                     std::uint16_t(0x47)}) {
                BattleDeploymentInsertionAnchor anchor;
                anchor.instruction_id = instruction_id;
                anchor.parent_key = selected_parent_key;
                anchor.before_key = selected_before_key;
                anchor.resolved = true;
                anchor.parent_handle = selected_parent_handle;
                anchor.before_handle = selected_before_handle;
                candidate.insertion_anchors.push_back(std::move(anchor));
            }
        } else {
            for (const std::uint16_t instruction_id : {
                     std::uint16_t(0x4b), std::uint16_t(0x46),
                     std::uint16_t(0x47)}) {
                BattleDeploymentInsertionAnchor anchor;
                anchor.instruction_id = instruction_id;
                anchor.diagnostic = selected_diagnostic;
                candidate.insertion_anchors.push_back(std::move(anchor));
            }
        }
    }
    for (const auto& baseline_node : original) {
        const auto found = std::find_if(
            live.begin(), live.end(), [&baseline_node](const auto& node) {
                return node.captured->runtime_handle ==
                           baseline_node.captured->runtime_handle &&
                       node.instruction_id == baseline_node.instruction_id;
            });
        if (found == live.end()) {
            return fail(L"战场写回快照失败：当前 TreeView 无法映射到缓存基线节点");
        }
        BattleWritebackSourceInstruction instruction;
        instruction.instruction_id = found->instruction_id;
        instruction.logical_key = baseline_node.key;
        instruction.current_logical_key = found->key;
        instruction.runtime_handle = found->captured->runtime_handle;
        instruction.ordinal = found->captured->ordinal;
        instruction.origin_int_data.assign(
            baseline_node.captured->int_data.begin(),
            baseline_node.captured->int_data.end());
        instruction.current_int_data.assign(
            found->captured->int_data.begin(),
            found->captured->int_data.end());
        candidate.instructions.push_back(std::move(instruction));
    }

    // New records in legacy callers belong to the first explicit Section event. Only create
    // anchors when that event has a recognizable final 0x00 terminator; any
    // unfamiliar layout stays non-insertable and the planner fails closed.
    std::size_t raw_section_matches = 0;
    const auto* raw_section = find_raw_node(
        current.input.nodes, current.snapshot.section_root.runtime_handle,
        raw_section_matches);
    if (reachability_scope == nullptr && original.empty() && live.empty()) {
        // Keep the user-selected normal instruction as the first-insertion
        // anchor; the generic first-event fallback below is not appropriate
        // when this Section has no deployment command at all.
    } else if (reachability_scope == nullptr &&
        raw_section_matches == 1 && raw_section != nullptr) {
        const auto event = std::find_if(
            raw_section->children.begin(), raw_section->children.end(),
            [](const auto& node) { return node.instruction_id == 0; });
        if (event != raw_section->children.end() &&
            !event->children.empty()) {
            const auto& terminal = event->children.back();
            if (terminal.instruction_id == 0 && terminal.children.empty() &&
                key_in_scope(event->logical_key, candidate.scope_key) &&
                key_in_scope(terminal.logical_key, candidate.scope_key)) {
                candidate.insert_parent_key = event->logical_key;
                candidate.insert_before_key = terminal.logical_key;
            }
        }
    }
    output = std::move(candidate);
    return true;
}

BattleWritebackPlanBuild BuildBattleWritebackPlan(
    const ccz::preview::BattlePreviewState& state,
    const ccz::preview::LogicalNodeKey& scope_key,
    const BattleWritebackSectionSnapshot& snapshot) {
    BattleWritebackPlanBuild result;
    const auto fail_plan = [&result](const std::wstring& diagnostic) {
        result = {};
        result.diagnostic = diagnostic;
        return result;
    };
    if (scope_key.empty() || snapshot.scope_key != scope_key) {
        return fail_plan(L"战场写回计划失败：Section 作用域不一致");
    }
    const auto resolve_insert_anchor = [&snapshot, &scope_key](
        std::uint16_t instruction_id,
        ccz::preview::LogicalNodeKey& parent_key,
        ccz::preview::LogicalNodeKey& before_key,
        std::wstring& diagnostic) {
        std::size_t matches = 0;
        const BattleDeploymentInsertionAnchor* typed_anchor = nullptr;
        for (const auto& anchor : snapshot.insertion_anchors) {
            if (anchor.instruction_id != instruction_id) continue;
            typed_anchor = &anchor;
            ++matches;
        }
        if (matches > 1) {
            diagnostic = L"战场写回计划失败：" +
                hex_instruction(instruction_id) +
                L" 存在重复的子事件插入锚点";
            return false;
        }
        if (typed_anchor != nullptr) {
            if (!typed_anchor->resolved) {
                diagnostic = typed_anchor->diagnostic.empty()
                    ? L"战场写回计划失败：" + hex_instruction(instruction_id) +
                        L" 子事件插入锚点不唯一"
                    : L"战场写回计划失败：" + typed_anchor->diagnostic;
                return false;
            }
            parent_key = typed_anchor->parent_key;
            before_key = typed_anchor->before_key;
        } else if (snapshot.insertion_anchors.empty()) {
            parent_key = snapshot.insert_parent_key;
            before_key = snapshot.insert_before_key;
        } else {
            diagnostic = L"战场写回计划失败：当前可达分支没有 " +
                hex_instruction(instruction_id) +
                L" 类型指令的唯一子事件插入锚点";
            return false;
        }
        if (!key_in_scope(parent_key, scope_key) ||
            !key_in_scope(before_key, scope_key) ||
            parent_key == before_key) {
            diagnostic = L"战场写回计划失败：" +
                hex_instruction(instruction_id) +
                L" 指令插入锚点未限定在当前 Section";
            return false;
        }
        return true;
    };

    std::vector<std::uint64_t> cache_ids;
    std::vector<MinionBrushBatch> minion_batches;
    for (const BattleUnit& unit : state.units) {
        const bool deployment_unit = unit.editor_staged_new ||
            is_battle_deployment(unit.source_instruction);
        if (!deployment_unit) continue;
        if (unit.editor_staged_new && unit.editor_pending_delete) continue;
        if (!unit.fields_decoded) {
            return fail_plan(L"战场写回计划失败：布阵缓存含未解析字段的单位");
        }
        if (IsBattleFixedEmptySlot(unit)) continue;
        if (unit.editor_cache_id == 0) {
            return fail_plan(L"战场写回计划失败：单位缺少稳定缓存 ID");
        }
        if (std::find(cache_ids.begin(), cache_ids.end(), unit.editor_cache_id) !=
            cache_ids.end()) {
            return fail_plan(L"战场写回计划失败：缓存 ID 重复");
        }
        cache_ids.push_back(unit.editor_cache_id);

        if (unit.editor_staged_new &&
            ((unit.editor_minion_brush_batch_id == 0 &&
              unit.editor_minion_brush_first_id >= 0) ||
             (unit.editor_minion_brush_batch_id != 0 &&
              unit.editor_minion_brush_first_id < 0))) {
            return fail_plan(L"战场写回计划失败：小兵笔刷批次身份或起始号不完整");
        }
        if (unit.editor_staged_new &&
            unit.editor_minion_brush_batch_id != 0) {
            const std::uint64_t batch_id =
                unit.editor_minion_brush_batch_id;
            const int first_id = unit.editor_minion_brush_first_id;
            if (unit.data_id < first_id) {
                return fail_plan(L"战场写回计划失败：小兵笔刷单位编号早于批次起始号");
            }
            auto batch = std::find_if(minion_batches.begin(), minion_batches.end(),
                [batch_id](const MinionBrushBatch& candidate) {
                    return candidate.batch_id == batch_id;
                });
            if (batch == minion_batches.end()) {
                minion_batches.push_back({batch_id, first_id, {unit.data_id}});
            } else {
                if (batch->first_data_id != first_id) {
                    return fail_plan(L"战场写回计划失败：同一小兵笔刷批次的稳定起始号不一致");
                }
                batch->data_ids.push_back(unit.data_id);
            }
        }
    }
    for (MinionBrushBatch& batch : minion_batches) {
        std::sort(batch.data_ids.begin(), batch.data_ids.end());
        for (std::size_t index = 0; index < batch.data_ids.size(); ++index) {
            const std::int64_t expected =
                static_cast<std::int64_t>(batch.first_data_id) +
                static_cast<std::int64_t>(index);
            if (static_cast<std::int64_t>(batch.data_ids[index]) != expected) {
                return fail_plan(L"战场写回计划失败：小兵笔刷批次 Data 号不连续");
            }
        }
    }

    std::vector<const BattleWritebackSourceInstruction*> ordered_sources;
    std::vector<int> source_ordinals;
    ordered_sources.reserve(snapshot.instructions.size());
    for (const BattleWritebackSourceInstruction& source : snapshot.instructions) {
        if (!is_battle_deployment(source.instruction_id) ||
            !key_in_scope(source.logical_key, scope_key) || source.ordinal < 0) {
            return fail_plan(L"战场写回计划失败：来源节点类别或 Section 锚点无效");
        }
        if (std::find(source_ordinals.begin(), source_ordinals.end(),
                      source.ordinal) != source_ordinals.end()) {
            return fail_plan(L"战场写回计划失败：来源节点顺序重复，无法稳定排序");
        }
        source_ordinals.push_back(source.ordinal);
        if (!valid_native_source(source, result.diagnostic)) {
            const std::wstring diagnostic = result.diagnostic;
            return fail_plan(diagnostic);
        }
        if (std::any_of(ordered_sources.begin(), ordered_sources.end(),
                [&source](const BattleWritebackSourceInstruction* existing) {
                    return existing->logical_key == source.logical_key;
                })) {
            return fail_plan(L"战场写回计划失败：来源逻辑键重复");
        }
        ordered_sources.push_back(&source);
    }
    std::stable_sort(ordered_sources.begin(), ordered_sources.end(),
        [](const BattleWritebackSourceInstruction* left,
           const BattleWritebackSourceInstruction* right) {
            return left->ordinal < right->ordinal;
        });

    std::vector<PlannedNode> nodes;
    nodes.reserve(ordered_sources.size() + state.units.size());
    for (const auto* source : ordered_sources) {
        PlannedNode node;
        node.instruction_id = source->instruction_id;
        node.source_key = source->logical_key;
        node.native_source_key = source->current_logical_key.empty()
            ? source->logical_key : source->current_logical_key;
        node.runtime_handle = source->runtime_handle;
        node.ordinal = source->ordinal;
        node.origin_int_data = source->origin_int_data;
        node.int_data = source->current_int_data;
        node.source_backed = true;
        nodes.push_back(std::move(node));
    }

    const auto node_index_for = [&nodes](
        const ccz::preview::LogicalNodeKey& key, std::uint16_t instruction) {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            if (nodes[index].source_backed && nodes[index].source_key == key &&
                nodes[index].instruction_id == instruction) return index;
        }
        return nodes.size();
    };

    std::vector<std::vector<bool>> source_rows;
    source_rows.reserve(nodes.size());
    for (const PlannedNode& node : nodes) {
        const BattleFieldSpec* spec =
            ccz::preview::get_battle_field_spec(node.instruction_id);
        source_rows.emplace_back(record_count(*spec), false);
    }
    std::vector<PendingBinding> pending_bindings;
    std::vector<std::vector<std::uint64_t>> changed_cache_ids(nodes.size());
    std::vector<const BattleUnit*> staged_units;
    struct FourBSourceAnchor {
        const BattleUnit* unit{};
        std::size_t node_index{};
    };
    std::vector<FourBSourceAnchor> four_b_sources;

    for (const BattleUnit& unit : state.units) {
        if (!unit.fields_decoded ||
            (unit.editor_pending_delete && unit.editor_staged_new)) continue;
        if (unit.editor_staged_new) {
            if (!unit.source_key.empty() || unit.editor_cache_id == 0 ||
                !is_battle_deployment(unit.editor_deployment_instruction) ||
                unit.data_id < 0) {
                return fail_plan(L"战场写回计划失败：新单位缺少稳定缓存身份或部署类别");
            }
            staged_units.push_back(&unit);
            continue;
        }
        const BattleFieldSpec* spec =
            ccz::preview::get_battle_field_spec(unit.source_instruction);
        if (spec == nullptr) continue;
        if (IsBattleFixedEmptySlot(unit)) continue;
        if (!key_in_scope(unit.source_key, scope_key) ||
            !is_battle_deployment(unit.source_instruction)) {
            return fail_plan(L"战场写回计划失败：既有单位来源越过当前 Section");
        }
        if (unit.editor_deployment_instruction != unit.source_instruction) {
            return fail_plan(L"战场写回计划失败：类别迁移尚无已证明的无损字段映射");
        }
        if (unit.editor_cache_id == 0) {
            return fail_plan(L"战场写回计划失败：既有单位缺少稳定缓存 ID");
        }
        const std::size_t node_index =
            node_index_for(unit.source_key, unit.source_instruction);
        if (node_index == nodes.size()) {
            return fail_plan(L"战场写回计划失败：缓存来源指令已不存在或类别已变化");
        }
        if (unit.record_index >= source_rows[node_index].size() ||
            unit.raw_fields.size() != spec->record_stride ||
            unit.editor_origin_fields.size() != spec->record_stride ||
            nodes[node_index].origin_int_data.size() !=
                nodes[node_index].int_data.size()) {
            return fail_plan(L"战场写回计划失败：来源记录行或原始字段基线不完整");
        }
        const std::size_t offset = unit.record_index * spec->record_stride;
        const auto begin = nodes[node_index].origin_int_data.begin() +
                           static_cast<std::ptrdiff_t>(offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(spec->record_stride);
        if (!std::equal(unit.editor_origin_fields.begin(),
                        unit.editor_origin_fields.end(), begin, end)) {
            return fail_plan(L"战场写回计划失败：单位来源记录与缓存基线不一致");
        }
        const auto data_id_role = std::find(spec->field_roles.begin(),
            spec->field_roles.end(), BattleFieldRole::DataId);
        if (data_id_role != spec->field_roles.end()) {
            const std::size_t data_id_index = static_cast<std::size_t>(
                data_id_role - spec->field_roles.begin());
            if (nodes[node_index].int_data[offset + data_id_index] !=
                nodes[node_index].origin_int_data[offset + data_id_index]) {
                return fail_plan(L"战场写回冲突：剧本中该固定槽的 Data 号已变化");
            }
        }
        if (source_rows[node_index][unit.record_index]) {
            return fail_plan(L"战场写回计划失败：多个缓存单位指向同一来源记录行");
        }
        source_rows[node_index][unit.record_index] = true;
        if (unit.source_instruction == 0x4b) {
            four_b_sources.push_back({&unit, node_index});
        }

        if (unit.editor_pending_delete) {
            if (unit.source_instruction == 0x4b) {
                if (nodes[node_index].int_data !=
                    nodes[node_index].origin_int_data) {
                    return fail_plan(L"战场写回冲突：待删除的 0x4B 来源指令已被剧本修改");
                }
                nodes[node_index].deleted = true;
                nodes[node_index].changed = true;
            } else {
                for (std::size_t field = 0; field < spec->record_stride;
                     ++field) {
                    const std::size_t field_offset = offset + field;
                    if (nodes[node_index].int_data[field_offset] !=
                        nodes[node_index].origin_int_data[field_offset]) {
                        return fail_plan(L"战场写回冲突：待删除单位的剧本字段已变化");
                    }
                }
                clear_fixed_row(nodes[node_index], *spec, unit.record_index);
            }
            append_unique_cache_id(changed_cache_ids[node_index],
                                   unit.editor_cache_id);
        } else if (!unit.dirty_fields.empty()) {
            for (std::size_t field = 0; field < spec->record_stride; ++field) {
                const BattleFieldRole role = spec->field_roles[field];
                if (!ccz::preview::is_battle_field_dirty(unit, role)) continue;
                const std::size_t field_offset = offset + field;
                const std::int32_t baseline_value =
                    nodes[node_index].origin_int_data[field_offset];
                const std::int32_t live_value =
                    nodes[node_index].int_data[field_offset];
                const std::int32_t preview_value =
                    value_for_role(unit, role, baseline_value);
                if (live_value != baseline_value &&
                    live_value != preview_value) {
                    return fail_plan(L"战场写回冲突：剧本与预览修改了同一单位字段");
                }
            }
            write_unit_row(nodes[node_index], *spec, unit.record_index, unit,
                           false);
            append_unique_cache_id(changed_cache_ids[node_index],
                                   unit.editor_cache_id);
        }
        if (!unit.editor_pending_delete && !unit.dirty_fields.empty()) {
            pending_bindings.push_back(
                {unit.editor_cache_id, node_index, unit.record_index});
        }
    }

    std::sort(four_b_sources.begin(), four_b_sources.end(),
        [](const FourBSourceAnchor& left, const FourBSourceAnchor& right) {
            return left.node_index < right.node_index;
        });

    // Every occupied source row must have exactly one cache anchor.  Otherwise
    // a partial parser result could cause a writer to silently drop a unit.
    for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
        const BattleFieldSpec* spec = ccz::preview::get_battle_field_spec(
            nodes[node_index].instruction_id);
        for (std::size_t row = 0; row < record_count(*spec); ++row) {
            if (nodes[node_index].instruction_id != 0x4b &&
                fixed_row_is_empty(nodes[node_index].origin_int_data,
                                   *spec, row)) continue;
            if (!source_rows[node_index][row]) {
                return fail_plan(L"战场写回计划失败：存在未纳入缓存的原生出场记录");
            }
        }
    }

    const bool has_staged_four_b = std::any_of(
        staged_units.begin(), staged_units.end(), [](const BattleUnit* unit) {
            return unit->editor_deployment_instruction == 0x4b;
        });
    const bool create_first_four_b_group = has_staged_four_b &&
        four_b_sources.empty() && !snapshot.has_current_own_deployment_group;
    std::size_t four_b_group_node_index = nodes.size();
    const bool four_b_layout_changes = has_staged_four_b || std::any_of(
        four_b_sources.begin(), four_b_sources.end(),
        [](const FourBSourceAnchor& anchor) {
            return anchor.unit->editor_pending_delete;
        });
    if (four_b_layout_changes) {
        for (const FourBSourceAnchor& anchor : four_b_sources) {
            const PlannedNode& node = nodes[anchor.node_index];
            if (node.int_data.empty() || node.origin_int_data.empty() ||
                node.int_data[0] != node.origin_int_data[0]) {
                return fail_plan(L"战场写回冲突：0x4B 部署槽序号已变化，无法安全重排");
            }
        }
    }
    int next_four_b_slot = 0;
    if (four_b_layout_changes) {
        for (const FourBSourceAnchor& anchor : four_b_sources) {
            const BattleUnit& unit = *anchor.unit;
            if (unit.editor_pending_delete) continue;
            const int compacted_slot = next_four_b_slot++;
            PlannedNode& node = nodes[anchor.node_index];
            const BattleFieldSpec* spec =
                ccz::preview::get_battle_field_spec(0x4b);
            if (spec == nullptr || spec->field_roles.empty() ||
                spec->field_roles[0] != BattleFieldRole::DeploymentIndex) {
                return fail_plan(L"战场写回计划失败：0x4B 部署槽字段定义无效");
            }
            if (node.int_data[0] == compacted_slot) continue;
            node.int_data[0] = compacted_slot;
            node.changed = true;
            append_unique_cache_id(changed_cache_ids[anchor.node_index],
                                   unit.editor_cache_id);
            const bool binding_exists = std::any_of(
                pending_bindings.begin(), pending_bindings.end(),
                [&unit, &anchor](const PendingBinding& binding) {
                    return binding.cache_id == unit.editor_cache_id &&
                           binding.planned_node == anchor.node_index &&
                           binding.record_index == unit.record_index;
                });
            if (!binding_exists) {
                pending_bindings.push_back(
                    {unit.editor_cache_id, anchor.node_index,
                     unit.record_index});
            }
        }
    }

    for (const BattleUnit* unit : staged_units) {
        const std::uint16_t instruction = unit->editor_deployment_instruction;
        const BattleFieldSpec* spec =
            ccz::preview::get_battle_field_spec(instruction);
        std::size_t node_index = nodes.size();
        std::size_t row_index = 0;
        if (instruction == 0x4b) {
            if (create_first_four_b_group) {
                if (four_b_group_node_index == nodes.size()) {
                    PlannedNode group;
                    group.instruction_id = 0x4b;
                    group.four_b_group = true;
                    group.group_label = L"我军出场";
                    group.changed = true;
                    std::wstring anchor_diagnostic;
                    if (!resolve_insert_anchor(instruction,
                            group.insert_parent_key,
                            group.insert_before_key, anchor_diagnostic)) {
                        return fail_plan(anchor_diagnostic);
                    }
                    nodes.push_back(std::move(group));
                    changed_cache_ids.resize(nodes.size());
                    four_b_group_node_index = nodes.size() - 1;
                }
                node_index = four_b_group_node_index;
            } else {
            PlannedNode node = make_new_node(instruction);
            std::wstring anchor_diagnostic;
            if (!resolve_insert_anchor(instruction, node.insert_parent_key,
                    node.insert_before_key, anchor_diagnostic)) {
                return fail_plan(anchor_diagnostic);
            }
            node.changed = true;
            nodes.push_back(std::move(node));
            changed_cache_ids.resize(nodes.size());
            node_index = nodes.size() - 1;
            }
        } else {
            for (std::size_t candidate = 0; candidate < nodes.size(); ++candidate) {
                if (nodes[candidate].instruction_id != instruction ||
                    nodes[candidate].deleted) continue;
                for (std::size_t row = 0; row < spec->record_capacity; ++row) {
                    if (fixed_row_is_empty(nodes[candidate], *spec, row)) {
                        node_index = candidate;
                        row_index = row;
                        break;
                    }
                }
                if (node_index != nodes.size()) break;
            }
            if (node_index == nodes.size()) {
                PlannedNode node = make_new_node(instruction);
                std::wstring anchor_diagnostic;
                if (!resolve_insert_anchor(instruction, node.insert_parent_key,
                        node.insert_before_key, anchor_diagnostic)) {
                    return fail_plan(anchor_diagnostic);
                }
                node.changed = true;
                nodes.push_back(std::move(node));
                changed_cache_ids.resize(nodes.size());
                node_index = nodes.size() - 1;
            }
        }
        BattleUnit staged_row = *unit;
        if (instruction == 0x4b) {
            staged_row.deployment_index = next_four_b_slot++;
            ccz::preview::mark_battle_field_dirty(
                staged_row, BattleFieldRole::DeploymentIndex);
        }
        std::size_t binding_record_index = row_index;
        if (nodes[node_index].four_b_group) {
            PlannedNode record = make_new_node(0x4b);
            write_unit_row(record, *spec, 0, staged_row, true);
            binding_record_index = nodes[node_index].group_records.size();
            nodes[node_index].group_records.push_back(std::move(record.int_data));
            nodes[node_index].changed = true;
        } else {
            write_unit_row(nodes[node_index], *spec, row_index, staged_row,
                           true);
        }
        append_unique_cache_id(changed_cache_ids[node_index],
                               unit->editor_cache_id);
        pending_bindings.push_back({unit->editor_cache_id, node_index,
                                    binding_record_index});
    }

    std::vector<std::size_t> node_to_change(nodes.size(),
                                            (std::numeric_limits<std::size_t>::max)());
    for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
        const PlannedNode& node = nodes[node_index];
        if (!node.changed || node.four_b_group != (pass == 0)) continue;
        BattleWritebackChange change;
        change.instruction_id = node.instruction_id;
        change.source_key = node.source_backed
            ? node.native_source_key : node.source_key;
        change.source_ordinal = node.ordinal;
        if (!node.four_b_group) change.int_data = node.int_data;
        change.group_label = node.group_label;
        change.group_records = node.group_records;
        change.affected_cache_ids = changed_cache_ids[node_index];
        if (node.source_backed) {
            change.kind = node.deleted
                ? BattleWritebackChangeKind::DeleteInstruction
                : BattleWritebackChangeKind::UpdateInstruction;
        } else {
            if (!key_in_scope(node.insert_parent_key, scope_key) ||
                !key_in_scope(node.insert_before_key, scope_key) ||
                node.insert_parent_key == node.insert_before_key) {
                return fail_plan(L"战场写回计划失败：新指令的类型专属锚点无效");
            }
            change.kind = node.four_b_group
                ? BattleWritebackChangeKind::InsertFourBGroup
                : BattleWritebackChangeKind::InsertInstruction;
            change.insert_parent_key = node.insert_parent_key;
            change.insert_before_key = node.insert_before_key;
        }
        node_to_change[node_index] = result.changes.size();
        result.changes.push_back(std::move(change));
    }
    }
    for (const PendingBinding& binding : pending_bindings) {
        if (binding.planned_node >= node_to_change.size() ||
            node_to_change[binding.planned_node] ==
                (std::numeric_limits<std::size_t>::max)()) {
            return fail_plan(L"战场写回计划失败：缓存单位无法绑定输出变更");
        }
        result.bindings.push_back({binding.cache_id,
                                   node_to_change[binding.planned_node],
                                   binding.record_index});
    }
    result.ok = true;
    result.product_writeback_allowed = false;
    result.diagnostic = L"已生成 S 战场 Section 变更计划；原生提交门仍关闭";
    return result;
}

BattleWritebackNativeBatch BuildBattleWritebackNativeBatch(
    const BattleWritebackPlanBuild& plan,
    const BattlePreviewCapture& current,
    const ccz::preview::LogicalNodeKey& scope_key) {
    BattleWritebackNativeBatch result;
    const auto fail_batch = [&result](const wchar_t* diagnostic) {
        result = {};
        result.diagnostic = diagnostic;
        return result;
    };
    if (!plan.ok || scope_key.empty() || !current.ok ||
        current.mode.mode != ccz::preview::PreviewMode::Battle ||
        current.input.scope_key != scope_key) {
        return fail_batch(L"战场原生预检失败：计划、当前 S Section 或预览捕获无效");
    }
    if (normalized_path(current.input.document_path).empty() ||
        normalized_path(current.input.document_path) !=
            normalized_path(current.snapshot.document_path)) {
        return fail_batch(L"战场原生预检失败：当前文档路径与 TreeView 快照不一致");
    }
    std::wstring file_name = normalized_path(current.input.document_path);
    const std::size_t separator = file_name.find_last_of(L'\\');
    if (separator != std::wstring::npos) file_name.erase(0, separator + 1);
    for (wchar_t& character : file_name)
        character = static_cast<wchar_t>(std::towupper(character));
    if (file_name.rfind(L"S_", 0) != 0 && file_name.rfind(L"S-", 0) != 0) {
        return fail_batch(L"战场原生预检失败：当前文档不是 S_ 或 S- 剧本");
    }

    std::unordered_map<ccz::preview::LogicalNodeKey,
                       const ccz::preview::RawReplayNode*> raw_by_key;
    std::unordered_map<std::uintptr_t,
                       const ccz::preview::RawReplayNode*> raw_by_handle;
    const auto index_raw = [&](const auto& self,
                               const ccz::preview::RawReplayNode& node) -> bool {
        if (node.runtime_handle == 0 || node.logical_key.empty() ||
            !raw_by_key.emplace(node.logical_key, &node).second ||
            !raw_by_handle.emplace(node.runtime_handle, &node).second) {
            return false;
        }
        for (const auto& child : node.children) {
            if (!self(self, child)) return false;
        }
        return true;
    };
    for (const auto& root : current.input.nodes) {
        if (!index_raw(index_raw, root)) {
            return fail_batch(L"战场原生预检失败：逻辑键或 TreeView 句柄重复/为空");
        }
    }

    std::unordered_map<std::uintptr_t,
                       const CapturedEditorNode*> captured_by_handle;
    std::wstring capture_diagnostic;
    if (current.snapshot.document_roots.empty()) {
        if (!index_captured_nodes(current.snapshot.section_root,
                                  captured_by_handle, capture_diagnostic)) {
            return fail_batch(L"战场原生预检失败：TreeView 快照句柄重复或无效");
        }
    } else {
        for (const auto& root : current.snapshot.document_roots) {
            if (!index_captured_nodes(root, captured_by_handle,
                                      capture_diagnostic)) {
                return fail_batch(L"战场原生预检失败：TreeView 快照句柄重复或无效");
            }
        }
    }

    result.changes.reserve(plan.changes.size());
    std::unordered_set<std::uintptr_t> mutated_handles;
    for (const BattleWritebackChange& change : plan.changes) {
        if (!is_battle_deployment(change.instruction_id)) {
            return fail_batch(L"战场原生预检失败：计划包含非出场指令");
        }
        const std::size_t expected_size =
            native_vector_size(change.instruction_id);
        if ((change.kind == BattleWritebackChangeKind::InsertFourBGroup &&
             (change.instruction_id != 0x4b || !change.int_data.empty() ||
              change.group_label != L"我军出场" ||
              change.group_records.empty() ||
              std::any_of(change.group_records.begin(),
                  change.group_records.end(), [](const auto& record) {
                      return record.size() != 20U;
                  }))) ||
            (change.kind != BattleWritebackChangeKind::InsertFourBGroup &&
             change.int_data.size() != expected_size)) {
            return fail_batch(L"战场原生预检失败：出场指令 int_data 长度不符");
        }

        BattleWritebackNativeChange native_change;
        native_change.kind = change.kind;
        native_change.instruction_id = change.instruction_id;
        native_change.source_ordinal = change.source_ordinal;
        native_change.int_data = change.int_data;

        if (change.kind == BattleWritebackChangeKind::InsertInstruction ||
            change.kind == BattleWritebackChangeKind::InsertFourBGroup) {
            if (!change.source_key.empty() || change.source_ordinal != -1 ||
                !key_in_scope(change.insert_parent_key, scope_key) ||
                !key_in_scope(change.insert_before_key, scope_key)) {
                return fail_batch(L"战场原生预检失败：新增指令缺少当前 Section 的插入锚点");
            }
            const auto parent_found = raw_by_key.find(change.insert_parent_key);
            const auto before_found = raw_by_key.find(change.insert_before_key);
            if (parent_found == raw_by_key.end() ||
                before_found == raw_by_key.end()) {
                return fail_batch(L"战场原生预检失败：新增指令的父节点或终止锚点已失效");
            }
            const auto* parent = parent_found->second;
            const auto* before = before_found->second;
            if (parent->children.empty() || before->runtime_handle == 0 ||
                parent->instruction_id < 0 || parent->instruction_id == 1 ||
                parent->instruction_id == 3) {
                return fail_batch(L"战场原生预检失败：插入锚点所属子事件无效");
            }
            const auto before_position = std::find_if(
                parent->children.begin(), parent->children.end(),
                [before](const auto& child) {
                    return child.runtime_handle == before->runtime_handle;
                });
            const auto& terminal = parent->children.back();
            if (before_position == parent->children.end() ||
                terminal.instruction_id != 0 ||
                !terminal.children.empty() ||
                before_position > parent->children.end() - 1) {
                return fail_batch(L"战场原生预检失败：插入位置不是末尾 0x00 事件终止符之前的同级节点");
            }
            const auto captured_parent = captured_by_handle.find(
                parent->runtime_handle);
            const auto captured_before = captured_by_handle.find(
                before->runtime_handle);
            const auto captured_terminal = captured_by_handle.find(
                terminal.runtime_handle);
            if (captured_parent == captured_by_handle.end() ||
                captured_before == captured_by_handle.end() ||
                captured_terminal == captured_by_handle.end() ||
                captured_parent->second->instruction_id !=
                    static_cast<int>(parent->instruction_id) ||
                captured_before->second->instruction_id !=
                    static_cast<int>(before->instruction_id) ||
                captured_terminal->second->instruction_id != 0 ||
                captured_parent->second->children.size() !=
                    parent->children.size() ||
                captured_parent->second->children.back().runtime_handle !=
                    terminal.runtime_handle) {
                return fail_batch(L"战场原生预检失败：插入锚点无法映射回原生树");
            }
            for (std::size_t index = 0; index < parent->children.size(); ++index) {
                const auto captured_child = captured_by_handle.find(
                    parent->children[index].runtime_handle);
                if (captured_child == captured_by_handle.end() ||
                    captured_parent->second->children[index].runtime_handle !=
                        parent->children[index].runtime_handle ||
                    captured_parent->second->children[index].instruction_id !=
                        static_cast<int>(parent->children[index].instruction_id)) {
                    return fail_batch(L"战场原生预检失败：事件子节点顺序与句柄快照不一致");
                }
            }
            native_change.parent_handle = parent->runtime_handle;
            native_change.insert_before_handle = before->runtime_handle;
            native_change.insert_terminal_handle = terminal.runtime_handle;
            if (before_position != parent->children.begin()) {
                const auto& previous = *(before_position - 1);
                if (IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(
                        previous.instruction_id)) {
                    return fail_batch(L"战场原生预检失败：不允许在事件结束或子事件设定后面添加指令");
                }
                const auto captured_previous = captured_by_handle.find(
                    previous.runtime_handle);
                if (captured_previous == captured_by_handle.end() ||
                    captured_previous->second->instruction_id !=
                        static_cast<int>(previous.instruction_id)) {
                    return fail_batch(L"战场原生预检失败：插入前置兄弟无法映射回原生树");
                }
                native_change.insert_after_handle = previous.runtime_handle;
            }
            if (change.kind == BattleWritebackChangeKind::InsertFourBGroup) {
                native_change.group_label = change.group_label;
                native_change.group_records = change.group_records;
            }
        } else {
            if (change.source_key.empty() ||
                !key_in_scope(change.source_key, scope_key)) {
                return fail_batch(L"战场原生预检失败：既有指令来源越过当前 Section");
            }
            const auto raw_found = raw_by_key.find(change.source_key);
            if (raw_found == raw_by_key.end()) {
                return fail_batch(L"战场原生预检失败：既有指令来源逻辑键已失效");
            }
            const auto* raw = raw_found->second;
            const auto captured_found = captured_by_handle.find(
                raw->runtime_handle);
            if (captured_found == captured_by_handle.end() ||
                raw->instruction_id != change.instruction_id ||
                captured_found->second->instruction_id !=
                    static_cast<int>(change.instruction_id) ||
                captured_found->second->ordinal != change.source_ordinal ||
                captured_found->second->int_data.size() != expected_size ||
                !captured_found->second->children.empty() ||
                !captured_found->second->long_text.empty() ||
                raw->command.int_data.size() != expected_size ||
                !std::equal(captured_found->second->int_data.begin(),
                            captured_found->second->int_data.end(),
                            raw->command.int_data.begin())) {
                return fail_batch(L"战场原生预检失败：既有指令类型、序号或字段与捕获不一致");
            }
            if (!mutated_handles.insert(raw->runtime_handle).second) {
                return fail_batch(L"战场原生预检失败：同一原生节点在变更集中重复出现");
            }
            native_change.original_int_data.assign(
                captured_found->second->int_data.begin(),
                captured_found->second->int_data.end());
            if (change.kind == BattleWritebackChangeKind::DeleteInstruction &&
                (change.instruction_id != 0x4b ||
                 !captured_found->second->children.empty() ||
                 !captured_found->second->long_text.empty())) {
                return fail_batch(L"战场原生预检失败：仅允许删除无子节点的 0x4B 单位指令");
            }
            native_change.item_handle = raw->runtime_handle;
        }
        result.changes.push_back(std::move(native_change));
    }

    result.ok = true;
    result.diagnostic = L"全部 S 战场节点和插入锚点已解析；尚未执行原生写入";
    return result;
}

BattleWritebackNativeBatch BuildBattleWritebackInsertBatchAfterSelection(
    const BattlePreviewCapture& current,
    const std::vector<BattleMapOperationInstruction>& instructions) {
    BattleWritebackNativeBatch result;
    const auto fail_batch = [&result](const wchar_t* message) {
        result = {};
        result.diagnostic = message;
        return result;
    };
    if (!current.ok || current.mode.mode != ccz::preview::PreviewMode::Battle ||
        current.input.document_path.empty() ||
        current.snapshot.document_path.empty() ||
        normalized_path(current.input.document_path) !=
            normalized_path(current.snapshot.document_path) ||
        current.snapshot.document_roots.empty() ||
        current.snapshot.selected_handle == 0 || instructions.empty()) {
        return fail_batch(L"地图操作预检失败：S 预览、完整文档树或选中指令无效");
    }
    std::wstring file_name = normalized_path(current.input.document_path);
    const std::size_t separator = file_name.find_last_of(L'\\');
    if (separator != std::wstring::npos) file_name.erase(0, separator + 1);
    for (wchar_t& character : file_name)
        character = static_cast<wchar_t>(std::towupper(character));
    if (file_name.rfind(L"S_", 0) != 0 && file_name.rfind(L"S-", 0) != 0) {
        return fail_batch(L"地图操作预检失败：当前文档不是 S 剧本");
    }

    std::unordered_map<std::uintptr_t,
        const ccz::editor_preview::CapturedEditorNode*> captured_by_handle;
    std::wstring capture_diagnostic;
    for (const auto& root : current.snapshot.document_roots) {
        if (!index_captured_nodes(root, captured_by_handle,
                capture_diagnostic)) {
            return fail_batch(L"地图操作预检失败：完整剧本树句柄为空或重复");
        }
    }
    SelectedCapturedInstruction anchor;
    for (const auto& root : current.snapshot.document_roots) {
        find_selected_captured_instruction(root, nullptr, 0,
            current.snapshot.selected_handle, anchor);
    }
    if (anchor.matches != 1 || anchor.parent == nullptr ||
        anchor.selected == nullptr || anchor.selected->ordinal < 0 ||
        anchor.selected->instruction_id <= 0 ||
        anchor.selected->instruction_id == 1 ||
        anchor.selected->instruction_id == 3 ||
        !anchor.selected->children.empty() ||
        anchor.parent->runtime_handle == 0 ||
        anchor.child_index + 1 >= anchor.parent->children.size()) {
        return fail_batch(L"地图操作预检失败：请在完整剧本树中选中一条普通叶子指令作为写入锚点");
    }
    if (anchor.parent->instruction_id == 2 &&
        anchor.parent->long_text == L"我军出场") {
        return fail_batch(L"地图操作预检失败：选中指令位于“我军出场”专用分组；请改选其他分组中的指令");
    }
    const auto& siblings = anchor.parent->children;
    const auto& terminal = siblings.back();
    const bool section_entry = anchor.parent->instruction_id == -2;
    if (terminal.instruction_id != 0 || terminal.runtime_handle == 0 ||
        (!section_entry && !terminal.children.empty()) ||
        (section_entry && !terminal.children.empty() &&
            terminal.instruction_id != 0)) {
        return fail_batch(L"地图操作预检失败：锚点所属节点缺少合法的末尾事件边界");
    }

    try {
        result.changes.reserve(instructions.size());
        for (const auto& instruction : instructions) {
            std::wstring instruction_diagnostic;
            if (!ValidateBattleMapOperationInstruction(instruction,
                    instruction_diagnostic)) {
                result.changes.clear();
                result.diagnostic = instruction_diagnostic;
                return result;
            }
            BattleWritebackNativeChange change;
            change.kind = BattleWritebackChangeKind::InsertInstruction;
            change.instruction_id = instruction.instruction_id;
            change.parent_handle = anchor.parent->runtime_handle;
            change.insert_after_handle = anchor.selected->runtime_handle;
            change.insert_before_handle =
                siblings[anchor.child_index + 1].runtime_handle;
            change.insert_terminal_handle = terminal.runtime_handle;
            change.source_ordinal = -1;
            change.int_data = instruction.int_data;
            change.map_width = instruction.map_width;
            change.map_height = instruction.map_height;
            result.changes.push_back(std::move(change));
        }
    } catch (...) {
        return fail_batch(L"地图操作预检失败：无法分配有序原生插入批次");
    }
    result.ok = true;
    result.diagnostic = L"地图操作已绑定到当前选中指令之后；原生事务尚未执行";
    return result;
}

bool BattleWritebackNativeTransaction::Apply(
    const BattleWritebackNativeBatch& batch,
    BattleWritebackNativeHost& host, std::wstring& diagnostic) {
    diagnostic.clear();
    if (!batch.ok || batch.changes.empty()) {
        diagnostic = L"S 战场事务拒绝：变更批次无效或为空";
        return false;
    }

    BattleWritebackNativeDocumentState before_state;
    if (!host.CaptureDocumentState(before_state) ||
        before_state.next_ordinal < 0) {
        diagnostic = L"S 战场事务拒绝：无法捕获有效的文档状态";
        return false;
    }
    if (undo_stack_.size() == undo_stack_.max_size()) {
        diagnostic = L"S 战场事务拒绝：撤回记录数量已达上限";
        return false;
    }
    try {
        // Reserve the undo slot before mutating the native tree.  Committing
        // the prepared record at the end then cannot allocate after writes.
        undo_stack_.reserve(undo_stack_.size() + 1);
    } catch (...) {
        diagnostic = L"S 战场事务拒绝：无法预留撤回记录空间";
        return false;
    }

    struct PreparedChange {
        AppliedOperation operation;
        std::uintptr_t base_insert_after{};
        std::vector<std::int32_t> swap_input;
    };
    std::vector<PreparedChange> prepared;
    std::vector<BattleWritebackNativeNodeSnapshot> source_snapshots(
        batch.changes.size());
    std::unordered_set<std::uintptr_t> source_handles;
    std::unordered_set<std::uintptr_t> deleted_handles;
    std::unordered_map<BattleInsertAnchorKey, std::uintptr_t,
        BattleInsertAnchorKeyHash> last_inserted_by_anchor;
    std::size_t insert_count = 0;
    try {
        prepared.reserve(batch.changes.size());
        source_handles.reserve(batch.changes.size());
        deleted_handles.reserve(batch.changes.size());
        last_inserted_by_anchor.reserve(batch.changes.size());
        for (std::size_t index = 0; index < batch.changes.size(); ++index) {
            const BattleWritebackNativeChange& change = batch.changes[index];
            const bool four_b_group =
                change.kind == BattleWritebackChangeKind::InsertFourBGroup;
            const bool map_operation =
                is_battle_map_operation(change.instruction_id);
            const bool valid_four_b_group = four_b_group &&
                change.instruction_id == 0x4b && change.int_data.empty() &&
                change.group_label == L"我军出场" &&
                !change.group_records.empty() &&
                std::none_of(change.group_records.begin(),
                    change.group_records.end(), [](const auto& record) {
                        return record.size() != 20U;
                    });
            const bool valid_map_insert = map_operation &&
                change.kind == BattleWritebackChangeKind::InsertInstruction &&
                is_map_operation_insert_valid(change.instruction_id,
                    change.int_data, change.map_width, change.map_height,
                    diagnostic);
            const bool valid_deployment_change =
                is_battle_deployment(change.instruction_id) &&
                (four_b_group ? valid_four_b_group
                    : change.int_data.size() ==
                        native_vector_size(change.instruction_id));
            if ((!valid_deployment_change && !valid_map_insert) ||
                (map_operation && !valid_map_insert)) {
                if (diagnostic.empty())
                    diagnostic = L"S 战场事务拒绝：指令类型或完整字段长度无效";
                return false;
            }
            if (four_b_group && !valid_four_b_group) {
                diagnostic = L"S 战场事务拒绝：指令类型或完整字段长度无效";
                return false;
            }
            PreparedChange entry;
            entry.operation.kind = change.kind;
            entry.operation.after_int_data = change.int_data;

            if (change.kind == BattleWritebackChangeKind::InsertInstruction ||
                four_b_group) {
                if (change.item_handle != 0 || change.parent_handle == 0 ||
                    change.insert_before_handle == 0 ||
                    change.source_ordinal != -1 ||
                    !change.original_int_data.empty()) {
                    diagnostic = L"S 战场事务拒绝：新增指令缺少有效的父节点或终止锚点";
                    return false;
                }
                const std::size_t ordinals = four_b_group
                    ? change.group_records.size() + 3U : 1U;
                if (ordinals > static_cast<std::size_t>(
                        (std::numeric_limits<int>::max)()) - insert_count) {
                    diagnostic = L"S 战场事务拒绝：新增节点数量将溢出";
                    return false;
                }
                insert_count += ordinals;
                entry.operation.node.handle = 0;
                entry.operation.node.parent_handle = change.parent_handle;
                entry.operation.node.instruction_id = four_b_group ? 2
                    : change.instruction_id;
                entry.operation.node.int_data = change.int_data;
                entry.operation.group_label = change.group_label;
                entry.operation.group_records = change.group_records;
                entry.operation.group_parent_handle = change.parent_handle;
                entry.operation.group_after_handle = change.insert_after_handle;
                entry.operation.group_before_handle = change.insert_before_handle;
            } else if (change.kind ==
                       BattleWritebackChangeKind::UpdateInstruction ||
                       change.kind ==
                       BattleWritebackChangeKind::DeleteInstruction) {
                if (change.item_handle == 0 || change.source_ordinal < 0 ||
                    !source_handles.insert(change.item_handle).second ||
                    !host.NodeExists(change.item_handle) ||
                    !host.CaptureNode(change.item_handle,
                                      source_snapshots[index])) {
                    diagnostic = L"S 战场事务拒绝：来源节点已失效或重复";
                    return false;
                }
                const auto& source = source_snapshots[index];
                if (source.handle != change.item_handle ||
                    source.instruction_id != change.instruction_id ||
                    source.ordinal != change.source_ordinal ||
                    source.int_data != change.original_int_data ||
                    source.int_data.size() != native_vector_size(change.instruction_id) ||
                    source.has_children || source.has_long_text ||
                    source.has_structural_links) {
                    diagnostic = L"S 战场事务拒绝：来源节点与预检时的完整原值不一致";
                    return false;
                }
                if (change.kind == BattleWritebackChangeKind::DeleteInstruction &&
                    change.instruction_id != 0x4b) {
                    diagnostic = L"S 战场事务拒绝：当前只允许删除 0x4B 单位指令";
                    return false;
                }
                if (change.kind == BattleWritebackChangeKind::DeleteInstruction)
                    deleted_handles.insert(change.item_handle);
                entry.operation.node = source;
                entry.operation.current_handle = change.item_handle;
                if (change.kind == BattleWritebackChangeKind::UpdateInstruction)
                    entry.swap_input = change.int_data;
            } else {
                diagnostic = L"S 战场事务拒绝：包含未知变更类型";
                return false;
            }
            prepared.push_back(std::move(entry));
        }
    } catch (...) {
        diagnostic = L"S 战场事务拒绝：预分配完整变更快照失败";
        return false;
    }

    if (insert_count > static_cast<std::size_t>(
            (std::numeric_limits<int>::max)() - before_state.next_ordinal)) {
        diagnostic = L"S 战场事务拒绝：新指令序号将溢出";
        return false;
    }

    // Resolve every event and terminal anchor, including the predecessor
    // chain that will remain after this batch's deletions, before any writes.
    int next_insert_ordinal = before_state.next_ordinal;
    for (std::size_t index = 0; index < batch.changes.size(); ++index) {
        const auto& change = batch.changes[index];
        if (change.kind != BattleWritebackChangeKind::InsertInstruction &&
            change.kind != BattleWritebackChangeKind::InsertFourBGroup) continue;

        BattleWritebackNativeNodeSnapshot parent;
        BattleWritebackNativeNodeSnapshot before;
        const std::uintptr_t terminal_handle =
            change.insert_terminal_handle != 0
                ? change.insert_terminal_handle : change.insert_before_handle;
        BattleWritebackNativeNodeSnapshot terminal;
        if (!host.NodeExists(change.parent_handle) ||
            !host.NodeExists(change.insert_before_handle) ||
            !host.NodeExists(terminal_handle) ||
            !host.CaptureNode(change.parent_handle, parent) ||
            !host.CaptureNode(change.insert_before_handle, before) ||
            !host.CaptureNode(terminal_handle, terminal) ||
            !parent.has_children ||
            before.parent_handle != change.parent_handle ||
            terminal.instruction_id != 0 ||
            terminal.parent_handle != change.parent_handle ||
            terminal.has_long_text ||
            terminal.next_sibling_handle != 0 ||
            before.previous_sibling_handle != change.insert_after_handle ||
            (change.insert_before_handle == terminal_handle &&
             before.instruction_id != 0)) {
            diagnostic = L"S 战场事务拒绝：插入父节点、同级位置或末尾终止锚点已变化";
            return false;
        }
        if (is_battle_map_operation(change.instruction_id) &&
            parent.instruction_id == 2 &&
            parent.long_text == L"我军出场") {
            diagnostic = L"S 战场事务拒绝：地图操作不能插入“我军出场”专用分组";
            return false;
        }
        const bool section_entry_event_boundary =
            is_battle_map_operation(change.instruction_id) &&
            parent.instruction_id == static_cast<std::uint16_t>(-2) &&
            terminal.has_children;
        if ((!section_entry_event_boundary &&
                (terminal.has_children || terminal.has_structural_links)) ||
            (section_entry_event_boundary &&
                (!terminal.has_children || terminal.instruction_id != 0))) {
            diagnostic = L"S 战场事务拒绝：Section 入口事件边界不符合地图操作插入条件";
            return false;
        }
        std::uintptr_t following = change.insert_before_handle;
        std::unordered_set<std::uintptr_t> sibling_chain;
        while (following != terminal_handle) {
            if (following == 0 || !sibling_chain.insert(following).second) {
                diagnostic = L"S 战场事务拒绝：插入位置之后无法抵达事件终止符";
                return false;
            }
            BattleWritebackNativeNodeSnapshot sibling;
            if (!host.CaptureNode(following, sibling) ||
                sibling.parent_handle != change.parent_handle ||
                sibling.next_sibling_handle == 0) {
                diagnostic = L"S 战场事务拒绝：插入位置与末尾终止符不在同一子事件";
                return false;
            }
            following = sibling.next_sibling_handle;
        }
        if (change.insert_after_handle != 0) {
            BattleWritebackNativeNodeSnapshot after;
            if (!host.NodeExists(change.insert_after_handle) ||
                !host.CaptureNode(change.insert_after_handle, after) ||
                after.parent_handle != change.parent_handle ||
                after.next_sibling_handle != change.insert_before_handle) {
                diagnostic = L"S 战场事务拒绝：插入前置兄弟与终止锚点不相邻";
                return false;
            }
            if (IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(
                    after.instruction_id)) {
                diagnostic = L"S 战场事务拒绝：不允许在事件结束或子事件设定后面添加指令";
                return false;
            }
        }

        std::uintptr_t surviving_after = change.insert_after_handle;
        std::unordered_set<std::uintptr_t> visited;
        while (surviving_after != 0 &&
               deleted_handles.find(surviving_after) != deleted_handles.end()) {
            if (!visited.insert(surviving_after).second) {
                diagnostic = L"S 战场事务拒绝：插入前置链存在循环";
                return false;
            }
            BattleWritebackNativeNodeSnapshot deleted_predecessor;
            if (!host.CaptureNode(surviving_after, deleted_predecessor) ||
                deleted_predecessor.parent_handle != change.parent_handle) {
                diagnostic = L"S 战场事务拒绝：待删除前置兄弟无法解析";
                return false;
            }
            surviving_after = deleted_predecessor.previous_sibling_handle;
        }
        if (surviving_after != 0) {
            BattleWritebackNativeNodeSnapshot surviving_predecessor;
            if (!host.NodeExists(surviving_after) ||
                !host.CaptureNode(surviving_after, surviving_predecessor) ||
                surviving_predecessor.parent_handle != change.parent_handle) {
                diagnostic = L"S 战场事务拒绝：删除前置指令后无法解析实际插入位置";
                return false;
            }
            if (IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(
                    surviving_predecessor.instruction_id)) {
                diagnostic = L"S 战场事务拒绝：删除前置指令后会紧跟事件结束或子事件设定";
                return false;
            }
        }

        auto& entry = prepared[index];
        entry.base_insert_after = surviving_after;
        entry.operation.node.previous_sibling_handle = surviving_after;
        entry.operation.node.next_sibling_handle = change.insert_before_handle;
        entry.operation.node.ordinal = next_insert_ordinal;
        if (change.kind == BattleWritebackChangeKind::InsertFourBGroup) {
            entry.operation.group_parent_handle = change.parent_handle;
            entry.operation.group_after_handle = surviving_after;
            entry.operation.group_before_handle = change.insert_before_handle;
            entry.operation.node.handle = 0;
            entry.operation.node.ordinal = next_insert_ordinal;
            next_insert_ordinal += static_cast<int>(
                change.group_records.size() + 3U);
        } else {
            ++next_insert_ordinal;
        }
        const BattleInsertAnchorKey anchor_key{
            change.parent_handle, change.insert_before_handle};
        try {
            const auto [anchor, inserted] = last_inserted_by_anchor.try_emplace(
                anchor_key, surviving_after);
            if (!inserted && anchor->second != surviving_after) {
                diagnostic = L"S 战场事务拒绝：同一终止锚点对应多个前置节点";
                return false;
            }
        } catch (...) {
            diagnostic = L"S 战场事务拒绝：无法预分配插入前置索引";
            return false;
        }
    }

    UndoRecord candidate;
    candidate.before_state = before_state;
    std::vector<BattleWritebackNativeHandleMapping> rollback_mappings;
    try {
        candidate.operations.reserve(prepared.size());
        rollback_mappings.reserve(prepared.size());
    } catch (...) {
        diagnostic = L"S 战场事务拒绝：撤回记录预分配失败";
        return false;
    }

    const auto remap_handle = [](std::uintptr_t handle,
        const std::vector<BattleWritebackNativeHandleMapping>& mappings) {
        for (const auto& mapping : mappings)
            if (mapping.old_handle == handle) return mapping.new_handle;
        return handle;
    };
    const auto rollback_applied = [&]() {
        rollback_mappings.clear();
        bool restored = true;
        for (auto operation = candidate.operations.rbegin();
             operation != candidate.operations.rend(); ++operation) {
            switch (operation->kind) {
            case BattleWritebackChangeKind::UpdateInstruction: {
                auto& value = operation->rollback_values;
                if (!host.SwapNodeIntDataAndRefresh(
                        operation->current_handle, value)) {
                    restored = false;
                    break;
                }
                BattleWritebackNativeNodeSnapshot restored_node;
                if (!host.CaptureNode(operation->current_handle,
                                      restored_node) ||
                    restored_node.handle != operation->current_handle ||
                    restored_node.parent_handle !=
                        operation->node.parent_handle ||
                    restored_node.instruction_id !=
                        operation->node.instruction_id ||
                    restored_node.ordinal != operation->node.ordinal ||
                    restored_node.int_data != operation->node.int_data) {
                    restored = false;
                }
                break;
            }
            case BattleWritebackChangeKind::DeleteInstruction: {
                const auto before = remap_handle(
                    operation->node.next_sibling_handle, rollback_mappings);
                std::uintptr_t restored_handle = 0;
                if (!host.RestoreBattleNodeBefore(operation->node, before,
                                                  restored_handle) ||
                    restored_handle == 0) {
                    restored = false;
                    break;
                }
                rollback_mappings.push_back(
                    {operation->node.handle, restored_handle});
                break;
            }
            case BattleWritebackChangeKind::InsertInstruction:
                if (operation->current_handle == 0 ||
                    !host.DeleteBattleNode(operation->current_handle))
                    restored = false;
                break;
            case BattleWritebackChangeKind::InsertFourBGroup:
                if (operation->group_marker_handle == 0 ||
                    operation->group_handle == 0 ||
                    !host.DeleteBattleFourBGroup(
                        operation->group_marker_handle,
                        operation->group_handle))
                    restored = false;
                break;
            }
        }
        if (!host.RestoreDocumentState(before_state, rollback_mappings))
            restored = false;
        BattleWritebackNativeDocumentState after_rollback;
        if (!host.CaptureDocumentState(after_rollback) ||
            after_rollback.document_revision != before_state.document_revision ||
            after_rollback.next_ordinal != before_state.next_ordinal ||
            after_rollback.custom_modified != before_state.custom_modified ||
            after_rollback.framework_modified != before_state.framework_modified) {
            restored = false;
        }
        return restored;
    };
    const auto fail_and_rollback = [&](const wchar_t* reason) {
        const bool restored = rollback_applied();
        diagnostic = reason;
        if (restored) {
            diagnostic += L"；本次原生变更已完整回滚";
        } else {
            diagnostic += L"；原生回滚未能完整确认，请保留当前剧本文档并检查状态";
        }
        return false;
    };

    int next_ordinal = before_state.next_ordinal;
    for (std::size_t index = 0; index < batch.changes.size(); ++index) {
        const auto& change = batch.changes[index];
        PreparedChange& entry = prepared[index];
        AppliedOperation& operation = entry.operation;
        if (change.kind == BattleWritebackChangeKind::UpdateInstruction) {
            if (!host.SwapNodeIntDataAndRefresh(operation.current_handle,
                                                 entry.swap_input)) {
                return fail_and_rollback(L"S 战场事务失败：更新原生指令未完成");
            }
            const bool source_matches =
                entry.swap_input == source_snapshots[index].int_data;
            operation.rollback_values = std::move(entry.swap_input);
            candidate.operations.push_back(std::move(operation));
            if (!source_matches) {
                return fail_and_rollback(L"S 战场事务失败：更新节点在预检后发生变化");
            }
            BattleWritebackNativeNodeSnapshot updated_snapshot;
            const AppliedOperation& applied_operation =
                candidate.operations.back();
            if (!host.CaptureNode(applied_operation.current_handle,
                                  updated_snapshot) ||
                updated_snapshot.handle != applied_operation.current_handle ||
                updated_snapshot.parent_handle !=
                    applied_operation.node.parent_handle ||
                updated_snapshot.instruction_id !=
                    applied_operation.node.instruction_id ||
                updated_snapshot.ordinal != applied_operation.node.ordinal ||
                updated_snapshot.int_data != applied_operation.after_int_data ||
                updated_snapshot.has_children ||
                updated_snapshot.has_long_text ||
                updated_snapshot.has_structural_links) {
                return fail_and_rollback(
                    L"S 战场事务失败：更新节点字段未能完整回读验证");
            }
        } else if (change.kind == BattleWritebackChangeKind::DeleteInstruction) {
            if (!host.DeleteBattleNode(operation.current_handle))
                return fail_and_rollback(L"S 战场事务失败：删除原生 0x4B 指令未完成");
            candidate.operations.push_back(std::move(operation));
        } else if (change.kind == BattleWritebackChangeKind::InsertFourBGroup) {
            const BattleInsertAnchorKey anchor_key{
                change.parent_handle, change.insert_before_handle};
            const auto anchor = last_inserted_by_anchor.find(anchor_key);
            if (anchor == last_inserted_by_anchor.end())
                return fail_and_rollback(
                    L"S 战场事务失败：新增我军分组的插入前置索引丢失");
            std::vector<std::uintptr_t> record_handles;
            std::uintptr_t marker_handle = 0;
            std::uintptr_t group_handle = 0;
            std::uintptr_t terminal_handle = 0;
            const int first_ordinal = next_ordinal;
            if (!host.InsertBattleFourBGroup(change.parent_handle,
                    anchor->second, change.group_label, change.group_records,
                    first_ordinal, marker_handle, group_handle,
                    record_handles, terminal_handle) ||
                marker_handle == 0 || group_handle == 0 ||
                terminal_handle == 0 ||
                record_handles.size() != change.group_records.size()) {
                return fail_and_rollback(
                    L"S 战场事务失败：新增原生我军出场分组未完成");
            }
            operation.group_marker_handle = marker_handle;
            operation.group_handle = group_handle;
            operation.group_terminal_handle = terminal_handle;
            operation.group_parent_handle = change.parent_handle;
            operation.group_after_handle = anchor->second;
            operation.group_before_handle = change.insert_before_handle;
            operation.group_record_handles = std::move(record_handles);
            operation.group_records = change.group_records;
            operation.group_label = change.group_label;
            operation.node.handle = marker_handle;
            operation.node.parent_handle = change.parent_handle;
            operation.node.instruction_id = 1;
            operation.node.ordinal = first_ordinal;
            operation.node.previous_sibling_handle = anchor->second;
            operation.node.next_sibling_handle = group_handle;
            anchor->second = group_handle;
            next_ordinal += static_cast<int>(change.group_records.size() + 3U);

            BattleWritebackNativeNodeSnapshot marker;
            BattleWritebackNativeNodeSnapshot group;
            BattleWritebackNativeNodeSnapshot terminal;
            bool captured = host.CaptureNode(marker_handle, marker) &&
                host.CaptureNode(group_handle, group) &&
                host.CaptureNode(terminal_handle, terminal) &&
                marker.parent_handle == change.parent_handle &&
                marker.instruction_id == 1 &&
                marker.ordinal == first_ordinal &&
                marker.previous_sibling_handle == operation.group_after_handle &&
                marker.next_sibling_handle == group_handle &&
                group.parent_handle == change.parent_handle &&
                group.instruction_id == 2 &&
                group.ordinal == first_ordinal + 1 &&
                group.long_text == change.group_label &&
                group.previous_sibling_handle == marker_handle &&
                group.next_sibling_handle == change.insert_before_handle &&
                terminal.parent_handle == group_handle &&
                terminal.instruction_id == 0 &&
                terminal.ordinal == first_ordinal + 2 &&
                !terminal.has_children && !terminal.has_long_text &&
                !terminal.has_structural_links &&
                terminal.next_sibling_handle == 0;
            for (std::size_t record_index = 0;
                 captured && record_index < record_handles.size();
                 ++record_index) {
                BattleWritebackNativeNodeSnapshot record;
                captured = host.CaptureNode(record_handles[record_index], record) &&
                    record.parent_handle == group_handle &&
                    record.instruction_id == 0x4b &&
                    record.ordinal == first_ordinal + 3 +
                        static_cast<int>(record_index) &&
                    record.int_data == change.group_records[record_index] &&
                    !record.has_children && !record.has_long_text &&
                    !record.has_structural_links &&
                    record.next_sibling_handle ==
                        (record_index + 1 < record_handles.size()
                            ? record_handles[record_index + 1]
                            : terminal_handle);
            }
            candidate.operations.push_back(std::move(operation));
            if (!captured) {
                return fail_and_rollback(
                    L"S 战场事务失败：我军分组及 0x4B 记录未能完整回读验证");
            }
            candidate.operations.back().node = std::move(marker);
        } else {
            const BattleInsertAnchorKey anchor_key{
                change.parent_handle, change.insert_before_handle};
            const auto anchor = last_inserted_by_anchor.find(anchor_key);
            if (anchor == last_inserted_by_anchor.end())
                return fail_and_rollback(
                    L"S 战场事务失败：新增指令的插入前置索引丢失");
            const std::uintptr_t after_handle = anchor->second;
            std::uintptr_t inserted_handle = 0;
            if (!host.InsertBattleNode(change.parent_handle, after_handle,
                    change.instruction_id, operation.node.int_data,
                    next_ordinal, inserted_handle) || inserted_handle == 0) {
                return fail_and_rollback(L"S 战场事务失败：新增原生出场指令未完成");
            }
            anchor->second = inserted_handle;
            operation.current_handle = inserted_handle;
            operation.node.handle = inserted_handle;
            operation.node.ordinal = next_ordinal++;
            operation.node.previous_sibling_handle = after_handle;
            operation.node.next_sibling_handle = change.insert_before_handle;
            BattleWritebackNativeNodeSnapshot inserted_snapshot;
            const bool captured = host.CaptureNode(inserted_handle,
                                                   inserted_snapshot);
            candidate.operations.push_back(std::move(operation));
            if (!captured || inserted_snapshot.handle != inserted_handle ||
                inserted_snapshot.parent_handle != change.parent_handle ||
                inserted_snapshot.instruction_id != change.instruction_id ||
                inserted_snapshot.ordinal != candidate.operations.back().node.ordinal ||
                inserted_snapshot.int_data != change.int_data ||
                inserted_snapshot.has_children || inserted_snapshot.has_long_text ||
                inserted_snapshot.has_structural_links) {
                return fail_and_rollback(L"S 战场事务失败：新增节点未能完整回读验证");
            }
            candidate.operations.back().node = std::move(inserted_snapshot);
        }
    }

    if (!host.MarkDocumentModified())
        return fail_and_rollback(L"S 战场事务失败：无法设置文档修改状态");

    BattleWritebackNativeDocumentState committed_state;
    if (!host.CaptureDocumentState(committed_state) ||
        committed_state.next_ordinal != next_ordinal ||
        !committed_state.custom_modified || !committed_state.framework_modified) {
        return fail_and_rollback(L"S 战场事务失败：提交后的文档状态与计划不一致");
    }
    for (const auto& operation : candidate.operations) {
        if (operation.kind == BattleWritebackChangeKind::DeleteInstruction &&
            committed_state.selected_handle == operation.node.handle) {
            committed_state.selected_handle = operation.node.parent_handle;
        }
    }
    if (!host.RestoreDocumentState(committed_state, {}))
        return fail_and_rollback(L"S 战场事务失败：无法恢复提交后的选择/dirty 状态");
    // RestoreDocumentState only restores selection, the global ordinal, and
    // dirty flags. It does not touch script nodes, so the full-tree revision
    // captured immediately above remains the committed content revision.
    candidate.committed_revision = committed_state.document_revision;
    undo_stack_.push_back(std::move(candidate));
    diagnostic = L"S 战场原生变更已作为独立事务提交；可按提交顺序撤回";
    return true;
}

bool BattleWritebackNativeTransaction::Undo(
    BattleWritebackNativeHost& host, std::wstring& diagnostic) {
    diagnostic.clear();
    if (undo_stack_.empty()) {
        diagnostic = L"S 战场撤回不可用：当前没有已提交的原生事务";
        return false;
    }
    UndoRecord& undo_ = undo_stack_.back();

    BattleWritebackNativeDocumentState committed_state;
    if (!host.CaptureDocumentState(committed_state) ||
        committed_state.document_revision != undo_.committed_revision) {
        diagnostic = L"S 战场撤回拒绝：提交后剧本文档内容已变化，撤回记录已陈旧";
        return false;
    }
    std::unordered_set<std::uintptr_t> checked_handles;
    for (const AppliedOperation& operation : undo_.operations) {
        if (operation.kind == BattleWritebackChangeKind::UpdateInstruction ||
            operation.kind == BattleWritebackChangeKind::InsertInstruction) {
            BattleWritebackNativeNodeSnapshot current;
            if (operation.current_handle == 0 ||
                !checked_handles.insert(operation.current_handle).second ||
                !host.NodeExists(operation.current_handle) ||
                !host.CaptureNode(operation.current_handle, current)) {
                diagnostic = L"S 战场撤回拒绝：已提交节点失效或重复";
                return false;
            }
            const auto& expected = operation.kind ==
                BattleWritebackChangeKind::UpdateInstruction
                    ? operation.after_int_data : operation.node.int_data;
            const auto expected_id = operation.node.instruction_id;
            if (current.instruction_id != expected_id ||
                current.int_data != expected || current.has_children ||
                current.has_long_text || current.has_structural_links ||
                (operation.kind == BattleWritebackChangeKind::UpdateInstruction &&
                 (current.parent_handle != operation.node.parent_handle ||
                  current.ordinal != operation.node.ordinal))) {
                diagnostic = L"S 战场撤回拒绝：已提交节点内容与撤回记录不一致";
                return false;
            }
        } else if (operation.kind ==
                   BattleWritebackChangeKind::InsertFourBGroup) {
            BattleWritebackNativeNodeSnapshot marker;
            BattleWritebackNativeNodeSnapshot group;
            BattleWritebackNativeNodeSnapshot terminal;
            const int first_ordinal = operation.node.ordinal;
            bool valid = operation.group_marker_handle != 0 &&
                operation.group_handle != 0 &&
                operation.group_terminal_handle != 0 &&
                operation.group_record_handles.size() ==
                    operation.group_records.size() &&
                checked_handles.insert(operation.group_marker_handle).second &&
                checked_handles.insert(operation.group_handle).second &&
                checked_handles.insert(operation.group_terminal_handle).second &&
                host.NodeExists(operation.group_marker_handle) &&
                host.NodeExists(operation.group_handle) &&
                host.NodeExists(operation.group_terminal_handle) &&
                host.CaptureNode(operation.group_marker_handle, marker) &&
                host.CaptureNode(operation.group_handle, group) &&
                host.CaptureNode(operation.group_terminal_handle, terminal);
            valid = valid && marker.handle == operation.group_marker_handle &&
                marker.parent_handle == operation.group_parent_handle &&
                marker.instruction_id == 1 && marker.ordinal == first_ordinal &&
                marker.previous_sibling_handle == operation.group_after_handle &&
                marker.next_sibling_handle == operation.group_handle &&
                group.handle == operation.group_handle &&
                group.parent_handle == operation.group_parent_handle &&
                group.instruction_id == 2 && group.ordinal == first_ordinal + 1 &&
                group.long_text == operation.group_label &&
                group.previous_sibling_handle == operation.group_marker_handle &&
                terminal.handle == operation.group_terminal_handle &&
                terminal.parent_handle == operation.group_handle &&
                terminal.instruction_id == 0 && terminal.ordinal == first_ordinal + 2 &&
                !terminal.has_children && !terminal.has_long_text &&
                !terminal.has_structural_links &&
                terminal.next_sibling_handle == 0 &&
                terminal.previous_sibling_handle ==
                    (operation.group_record_handles.empty()
                        ? 0 : operation.group_record_handles.back());
            for (std::size_t record_index = 0;
                 valid && record_index < operation.group_record_handles.size();
                 ++record_index) {
                const auto handle = operation.group_record_handles[record_index];
                BattleWritebackNativeNodeSnapshot record;
                valid = handle != 0 && checked_handles.insert(handle).second &&
                    host.NodeExists(handle) && host.CaptureNode(handle, record) &&
                    record.parent_handle == operation.group_handle &&
                    record.instruction_id == 0x4b &&
                    record.ordinal == first_ordinal + 3 +
                        static_cast<int>(record_index) &&
                    record.int_data == operation.group_records[record_index] &&
                    !record.has_children && !record.has_long_text &&
                    !record.has_structural_links &&
                    record.previous_sibling_handle ==
                        (record_index == 0 ? 0 :
                            operation.group_record_handles[record_index - 1]) &&
                    record.next_sibling_handle ==
                        (record_index + 1 < operation.group_record_handles.size()
                            ? operation.group_record_handles[record_index + 1]
                            : operation.group_terminal_handle);
            }
            std::uintptr_t next_sibling = group.next_sibling_handle;
            std::unordered_set<std::uintptr_t> sibling_chain;
            while (valid && next_sibling != operation.group_before_handle) {
                if (next_sibling == 0 ||
                    !sibling_chain.insert(next_sibling).second) {
                    valid = false;
                    break;
                }
                BattleWritebackNativeNodeSnapshot sibling;
                valid = host.CaptureNode(next_sibling, sibling) &&
                    sibling.parent_handle == operation.group_parent_handle;
                if (valid) next_sibling = sibling.next_sibling_handle;
            }
            if (!valid) {
                diagnostic = L"S 战场撤回拒绝：我军出场分组、标签或记录与撤回快照不一致";
                return false;
            }
        } else if (operation.kind == BattleWritebackChangeKind::DeleteInstruction) {
            if (host.NodeExists(operation.node.handle)) {
                diagnostic = L"S 战场撤回拒绝：已删除来源句柄意外重新出现";
                return false;
            }
        }
    }

    std::vector<std::size_t> undone_operations;
    std::vector<BattleWritebackNativeHandleMapping> mappings;
    std::vector<BattleWritebackNativeHandleMapping> redo_mappings;
    std::vector<std::vector<std::int32_t>> undo_swap_values;
    try {
        undone_operations.reserve(undo_.operations.size());
        mappings.reserve(undo_.operations.size());
        redo_mappings.reserve(undo_.operations.size());
        undo_swap_values.resize(undo_.operations.size());
        for (std::size_t index = 0; index < undo_.operations.size(); ++index) {
            if (undo_.operations[index].kind ==
                BattleWritebackChangeKind::UpdateInstruction) {
                undo_swap_values[index] = undo_.operations[index].node.int_data;
            }
        }
    } catch (...) {
        diagnostic = L"S 战场撤回拒绝：撤回工作区分配失败";
        return false;
    }

    const auto remap_handle = [](std::uintptr_t handle,
        const std::vector<BattleWritebackNativeHandleMapping>& values) {
        for (const auto& mapping : values)
            if (mapping.old_handle == handle) return mapping.new_handle;
        return handle;
    };
    const auto restore_undo_to_committed = [&]() {
        redo_mappings.clear();
        bool restored = true;
        for (auto undone = undone_operations.rbegin();
             undone != undone_operations.rend(); ++undone) {
            AppliedOperation& operation = undo_.operations[*undone];
            if (operation.kind == BattleWritebackChangeKind::UpdateInstruction) {
                auto& value = undo_swap_values[*undone];
                if (!host.SwapNodeIntDataAndRefresh(operation.current_handle, value) ||
                    value != operation.node.int_data) {
                    restored = false;
                }
            } else if (operation.kind == BattleWritebackChangeKind::DeleteInstruction) {
                if (operation.current_handle == 0 ||
                    !host.DeleteBattleNode(operation.current_handle)) {
                    restored = false;
                }
                operation.current_handle = 0;
            } else if (operation.kind == BattleWritebackChangeKind::InsertInstruction) {
                const auto before = remap_handle(
                    operation.node.next_sibling_handle, redo_mappings);
                std::uintptr_t new_handle = 0;
                if (!host.RestoreBattleNodeBefore(operation.node, before,
                                                   new_handle) || new_handle == 0) {
                    restored = false;
                } else {
                    redo_mappings.push_back({operation.node.handle, new_handle});
                    operation.current_handle = new_handle;
                }
            } else if (operation.kind ==
                       BattleWritebackChangeKind::InsertFourBGroup) {
                const auto parent = remap_handle(
                    operation.group_parent_handle, redo_mappings);
                const auto after = remap_handle(
                    operation.group_after_handle, redo_mappings);
                const auto before = remap_handle(
                    operation.group_before_handle, redo_mappings);
                std::uintptr_t marker = 0;
                std::uintptr_t group = 0;
                std::uintptr_t terminal = 0;
                std::vector<std::uintptr_t> records;
                if (!host.RestoreBattleFourBGroupBefore(parent, after, before,
                        operation.group_label, operation.group_records,
                        operation.node.ordinal, marker, group, records,
                        terminal) || marker == 0 || group == 0 ||
                    terminal == 0 || records.size() !=
                        operation.group_record_handles.size()) {
                    restored = false;
                    continue;
                }
                redo_mappings.push_back(
                    {operation.group_marker_handle, marker});
                redo_mappings.push_back({operation.group_handle, group});
                redo_mappings.push_back(
                    {operation.group_terminal_handle, terminal});
                for (std::size_t index = 0; index < records.size(); ++index) {
                    redo_mappings.push_back(
                        {operation.group_record_handles[index], records[index]});
                }
                operation.group_marker_handle = marker;
                operation.group_handle = group;
                operation.group_terminal_handle = terminal;
                operation.group_parent_handle = parent;
                operation.group_after_handle = after;
                operation.group_before_handle = before;
                operation.group_record_handles = std::move(records);
                operation.node.handle = marker;
                operation.node.parent_handle = parent;
                operation.node.previous_sibling_handle = after;
                operation.node.next_sibling_handle = group;
                BattleWritebackNativeNodeSnapshot marker_snapshot;
                if (!host.CaptureNode(marker, marker_snapshot)) {
                    restored = false;
                } else {
                    operation.node = std::move(marker_snapshot);
                }
            }
        }
        if (!host.RestoreDocumentState(committed_state, redo_mappings))
            restored = false;
        BattleWritebackNativeDocumentState current;
        if (!host.CaptureDocumentState(current) ||
            current.document_revision != undo_.committed_revision ||
            current.next_ordinal != committed_state.next_ordinal ||
            current.custom_modified != committed_state.custom_modified ||
            current.framework_modified != committed_state.framework_modified) {
            restored = false;
        }
        return restored;
    };
    const auto fail_undo = [&](const wchar_t* reason) {
        const bool restored = restore_undo_to_committed();
        diagnostic = reason;
        if (restored)
            diagnostic += L"；撤回操作已整体回滚，原撤回记录仍保留";
        else
            diagnostic += L"；撤回回滚未能完整确认，请检查当前剧本文档状态";
        return false;
    };

    for (std::size_t reverse_index = undo_.operations.size();
         reverse_index > 0; --reverse_index) {
        const std::size_t index = reverse_index - 1;
        AppliedOperation& operation = undo_.operations[index];
        if (operation.kind == BattleWritebackChangeKind::UpdateInstruction) {
            auto& value = undo_swap_values[index];
            if (!host.SwapNodeIntDataAndRefresh(operation.current_handle, value))
                return fail_undo(L"S 战场撤回失败：恢复既有指令字段失败");
            undone_operations.push_back(index);
            if (value != operation.after_int_data)
                return fail_undo(L"S 战场撤回失败：既有指令在撤回期间发生变化");
        } else if (operation.kind == BattleWritebackChangeKind::DeleteInstruction) {
            const auto before = remap_handle(
                operation.node.next_sibling_handle, mappings);
            std::uintptr_t restored_handle = 0;
            if (!host.RestoreBattleNodeBefore(operation.node, before,
                                               restored_handle) || restored_handle == 0)
                return fail_undo(L"S 战场撤回失败：恢复已删除 0x4B 指令失败");
            operation.current_handle = restored_handle;
            mappings.push_back({operation.node.handle, restored_handle});
            undone_operations.push_back(index);
        } else if (operation.kind == BattleWritebackChangeKind::InsertInstruction) {
            if (!host.DeleteBattleNode(operation.current_handle))
                return fail_undo(L"S 战场撤回失败：删除事务新增指令失败");
            undone_operations.push_back(index);
        } else if (operation.kind ==
                   BattleWritebackChangeKind::InsertFourBGroup) {
            if (!host.DeleteBattleFourBGroup(
                    operation.group_marker_handle,
                    operation.group_handle))
                return fail_undo(L"S 战场撤回失败：删除事务新增我军出场分组失败");
            undone_operations.push_back(index);
        }
    }

    if (!host.RestoreDocumentState(undo_.before_state, mappings))
        return fail_undo(L"S 战场撤回失败：文档选择或 dirty 状态恢复失败");
    BattleWritebackNativeDocumentState restored_state;
    if (!host.CaptureDocumentState(restored_state) ||
        restored_state.document_revision != undo_.before_state.document_revision ||
        restored_state.next_ordinal != undo_.before_state.next_ordinal ||
        restored_state.custom_modified != undo_.before_state.custom_modified ||
        restored_state.framework_modified != undo_.before_state.framework_modified) {
        return fail_undo(L"S 战场撤回失败：撤回后的原始文档状态不匹配");
    }

    undo_stack_.pop_back();
    diagnostic = undo_stack_.empty()
        ? L"S 战场原生事务已撤回"
        : L"最近一次 S 战场原生事务已撤回；更早提交仍可继续撤回";
    return true;
}

void BattleWritebackNativeTransaction::Clear() noexcept {
    undo_stack_.clear();
}

void ClearBattleFixedEmptySlot(BattleUnit& unit) {
    const BattleFieldSpec* spec =
        ccz::preview::get_battle_field_spec(unit.source_instruction);
    if (spec == nullptr || spec->record_capacity == 0 ||
        spec->field_roles.size() != spec->record_stride) {
        return;
    }
    unit.fields_decoded = true;
    unit.raw_fields.resize(spec->record_stride);
    unit.dirty_fields.clear();
    unit.data_id = -1;
    unit.deployment_index = -1;
    unit.position = {0, 0};
    unit.direction = -1;
    unit.hidden = false;
    unit.level = 0;
    unit.unit_type = 2;
    unit.reinforcement = false;
    unit.ai_policy = 1;
    unit.target = -1;
    unit.target_x = 0;
    unit.target_y = 0;
    for (std::size_t index = 0; index < spec->field_roles.size(); ++index) {
        unit.raw_fields[index] = empty_sentinel(spec->field_roles[index]);
    }
    unit.diagnostic = L"固定战场槽已清为空槽 sentinel；记录 framing 保留";
}

BattleWritebackBuild BuildBattleWriteback(
    const ccz::preview::BattlePreviewState& state,
    const BattleWritebackEvidence& evidence) {
    BattleWritebackBuild result;
    if (!writeback_evidence_complete(evidence, result.diagnostic)) {
        return result;
    }

    // First-seen state order is the only safe emission order.  Logical paths
    // often contain numeric fragments (for example /2 and /10), so map text
    // ordering would silently change the script order.
    std::vector<Group> groups;
    std::vector<ccz::preview::LogicalNodeKey> four_b_sources;
    for (const BattleUnit& unit : state.units) {
        const BattleFieldSpec* spec =
            ccz::preview::get_battle_field_spec(unit.source_instruction);
        if (spec == nullptr) continue;
        if (unit.source_key.empty() || unit.page < 0) {
            result.diagnostic = L"战场写回被拒绝：记录缺少稳定来源或页码";
            return result;
        }
        const GroupKey key{unit.source_key, unit.source_instruction,
                           static_cast<std::size_t>(unit.page)};
        if (key.instruction == 0x4b) {
            if (key.page != 0) {
                result.diagnostic =
                    L"战场写回被拒绝：0x4B 来源记录不支持页码分割";
                return result;
            }
            if (std::find(four_b_sources.begin(), four_b_sources.end(),
                          key.source_key) != four_b_sources.end()) {
                result.diagnostic =
                    L"战场写回被拒绝：同一来源的 0x4B 记录不得重复出现";
                return result;
            }
            four_b_sources.push_back(key.source_key);
        }
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&key](const Group& candidate) {
                                      return candidate.key == key;
                                  });
        if (group == groups.end()) {
            groups.push_back({key, {&unit}});
        } else {
            group->units.push_back(&unit);
        }
    }
    if (groups.empty()) {
        result.diagnostic = L"战场写回被拒绝：没有可解释的战场记录";
        return result;
    }
    for (const Group& group : groups) {
        BattleWritebackRecord record;
        if (!build_group(group.key, group.units, record, result.diagnostic)) {
            return result;
        }
        result.records.push_back(std::move(record));
    }
    result.ok = true;
    // The evidence gate passed for a caller-supplied proof set, but this is
    // still a value-only adapter.  Product command routing remains disabled
    // until a later integration task performs native reload verification.
    result.product_writeback_allowed = false;
    result.diagnostic = L"已构建仅供审计的战场临时字节；产品写回仍禁用";
    return result;
}

bool BattleWritebackTransaction::Begin(
    const BattleWritebackMemoryState& current) {
    if (active_) return false;
    original_ = current;
    proposed_ = current;
    staged_ = false;
    active_ = true;
    return true;
}

bool BattleWritebackTransaction::Stage(
    const BattleWritebackMemoryState& proposed) {
    if (!active_) return false;
    proposed_ = proposed;
    staged_ = true;
    return true;
}

bool BattleWritebackTransaction::CommitToMemory(
    BattleWritebackMemoryState& current) {
    if (!active_ || !staged_) return false;
    current = proposed_;
    committed_undo_ = original_;
    undo_available_ = true;
    active_ = false;
    staged_ = false;
    return true;
}

bool BattleWritebackTransaction::Rollback(BattleWritebackMemoryState& current) {
    if (!active_) return false;
    current = original_;
    active_ = false;
    staged_ = false;
    return true;
}

bool BattleWritebackTransaction::UndoToMemory(
    BattleWritebackMemoryState& current) {
    if (active_ || !undo_available_) return false;
    current = committed_undo_;
    undo_available_ = false;
    committed_undo_ = {};
    return true;
}

void BattleWritebackTransaction::Clear() noexcept {
    active_ = false;
    staged_ = false;
    undo_available_ = false;
    original_ = {};
    proposed_ = {};
    committed_undo_ = {};
}

}  // namespace ccz::editor_preview
