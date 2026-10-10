#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ccz::preview {

struct InstructionCatalog {
    std::map<std::uint16_t, std::wstring> names;
    bool valid{};
    std::wstring diagnostic;
};

InstructionCatalog parse_instruction_catalog(
    const std::vector<std::uint8_t>& cp936_bytes);

std::wstring instruction_name(const InstructionCatalog& catalog,
                              std::uint16_t instruction_id);

}  // namespace ccz::preview


