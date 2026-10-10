#include <ccz/preview/script_variable_index.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ccz::preview {
namespace {

class IndexHasher {
public:
    void add_byte(std::uint8_t value) {
        hash_ ^= static_cast<std::uint64_t>(value);
        hash_ *= 1099511628211ull;
    }

    void add_u64(std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            add_byte(static_cast<std::uint8_t>(value & 0xffu));
            value >>= 8;
        }
    }

    void add_string(const std::wstring& value) {
        add_u64(static_cast<std::uint64_t>(value.size()));
        for (const wchar_t character : value) {
            add_u64(static_cast<std::uint64_t>(character));
        }
    }

    std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_{1469598103934665603ull};
};

void hash_nodes(IndexHasher& hasher,
                const std::vector<RawReplayNode>& nodes) {
    hasher.add_u64(static_cast<std::uint64_t>(nodes.size()));
    for (const RawReplayNode& node : nodes) {
        hasher.add_string(node.logical_key);
        hasher.add_u64(node.instruction_id);
        hasher.add_u64(node.command.instruction_id);
        hasher.add_u64(static_cast<std::uint64_t>(node.command.int_data.size()));
        for (const std::int32_t value : node.command.int_data) {
            hasher.add_u64(static_cast<std::uint64_t>(
                static_cast<std::int64_t>(value)));
        }
        hasher.add_string(node.command.text);
        hasher.add_string(node.source_text);
        hash_nodes(hasher, node.children);
    }
}

void append_diagnostic(std::vector<std::wstring>& diagnostics,
                       const std::wstring& message) {
    if (message.empty()) {
        return;
    }
    if (std::find(diagnostics.begin(), diagnostics.end(), message) ==
        diagnostics.end()) {
        diagnostics.push_back(message);
    }
}

void append_reference(std::vector<LogicalNodeKey>& references,
                      const LogicalNodeKey& key) {
    if (std::find(references.begin(), references.end(), key) ==
        references.end()) {
        references.push_back(key);
    }
}

void record_boolean_usage(ScriptVariableIndex& index,
                          const BooleanVariableExpression& expression,
                          const LogicalNodeKey& reference) {
    for (const std::int32_t id : expression.true_variables) {
        BooleanVariableUsage& usage = index.boolean_variables[id];
        usage.id = id;
        ++usage.occurrence_count;
        ++usage.true_list_occurrences;
        append_reference(usage.references, reference);
    }
    for (const std::int32_t id : expression.false_variables) {
        BooleanVariableUsage& usage = index.boolean_variables[id];
        usage.id = id;
        ++usage.occurrence_count;
        ++usage.false_list_occurrences;
        append_reference(usage.references, reference);
    }

    for (const std::int32_t id : expression.true_variables) {
        if (std::find(expression.false_variables.begin(),
                      expression.false_variables.end(), id) ==
            expression.false_variables.end()) {
            continue;
        }
        index.boolean_variables[id].conflicts = true;
        append_diagnostic(index.diagnostics,
                          L"0x05 变量同时位于真/假列表：Var" +
                              std::to_wstring(id) + L"（" + reference + L"）");
    }
}

void record_integer_operand(ScriptVariableIndex& index,
                            const IntegerExpressionOperand& operand,
                            const LogicalNodeKey& reference) {
    if (operand.raw_value<0 || operand.raw_value>=4096 || operand.kind == IntegerOperandKind::Constant || static_cast<int>(operand.kind)>=6) {
        return;
    }
    const bool pointer = operand.kind == IntegerOperandKind::PointerVariable || operand.kind == IntegerOperandKind::PointerValue || operand.kind == IntegerOperandKind::PointerAddress;
    IntegerVariableUsage& usage = (pointer ? index.pointer_variables : index.integer_variables)[operand.raw_value];
    usage.id = operand.raw_value;
    ++usage.occurrence_count;
    append_reference(usage.references, reference);
}

void scan_nodes(ScriptVariableIndex& index,
                const std::vector<RawReplayNode>& nodes) {
    for (const RawReplayNode& node : nodes) {
        if (node.instruction_id == 0x05) {
            const BooleanVariableExpression expression =
                decode_boolean_variable_expression(node.command);
            if (expression.decoded) {
                record_boolean_usage(index, expression, node.logical_key);
            } else if (!node.command.int_data.empty()) {
                append_diagnostic(
                    index.diagnostics,
                    L"无法索引 0x05：" +
                        (expression.diagnostic.empty()
                             ? L"结构化数据无效"
                             : expression.diagnostic));
            }
        } else if (node.instruction_id == 0x77 && node.command.int_data.size()>=5) {
            const auto& f=node.command.int_data;
            IntegerExpressionOperand dst;dst.raw_value=f[1];
            dst.kind=f[0]==2 ? IntegerOperandKind::IntegerVariable : IntegerOperandKind::PointerVariable;
            record_integer_operand(index,dst,node.logical_key);
            IntegerExpressionOperand src;src.raw_value=f[4];src.kind=static_cast<IntegerOperandKind>(f[3]);
            record_integer_operand(index,src,node.logical_key);
        } else if(node.instruction_id==0x78 && !node.command.int_data.empty()) {
            IntegerExpressionOperand dst;dst.raw_value=node.command.int_data[0];dst.kind=IntegerOperandKind::IntegerVariable;
            record_integer_operand(index,dst,node.logical_key);
        } else if (node.instruction_id == 0x79) {
            const IntegerComparisonExpression expression =
                decode_integer_expression(node.command);
            if (expression.decoded) {
                record_integer_operand(index, expression.left,
                                       node.logical_key);
                record_integer_operand(index, expression.right,
                                       node.logical_key);
            } else if (!node.command.int_data.empty()) {
                append_diagnostic(
                    index.diagnostics,
                    L"无法索引 0x79：" +
                        (expression.diagnostic.empty()
                             ? L"结构化数据无效"
                             : expression.diagnostic));
            }
        }
        scan_nodes(index, node.children);
    }
}

}  // namespace

ScriptVariableIndex scan_script_variables(
    const std::vector<RawReplayNode>& document_nodes,
    const LogicalNodeKey& document_key) {
    ScriptVariableIndex index;
    index.document_key = document_key;
    IndexHasher hasher;
    hash_nodes(hasher, document_nodes);
    index.source_fingerprint = hasher.value();
    scan_nodes(index, document_nodes);
    index.valid = true;
    return index;
}

void synchronize_boolean_defaults(SimulationEnvironment& environment,
                                  const ScriptVariableIndex& index) {
    for (auto iterator = environment.boolean_variables.begin();
         iterator != environment.boolean_variables.end();) {
        if (index.boolean_variables.find(iterator->first) ==
            index.boolean_variables.end()) {
            iterator = environment.boolean_variables.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (const auto& entry : index.boolean_variables) {
        environment.boolean_variables.emplace(entry.first, false);
    }
}

void synchronize_integer_scope(SimulationEnvironment& environment,
                               const ScriptVariableIndex& index) {
    for (auto iterator = environment.integer_variables.begin();
         iterator != environment.integer_variables.end();) {
        if (index.integer_variables.find(iterator->first) ==
            index.integer_variables.end()) {
            iterator = environment.integer_variables.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (const auto& entry : index.integer_variables) {
        environment.integer_variables.emplace(entry.first, 0);
    }
}

}  // namespace ccz::preview


