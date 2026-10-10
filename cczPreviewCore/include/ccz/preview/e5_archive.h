#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ccz::preview {

struct E5ResourceView {
    const std::uint8_t* data{};
    std::size_t size{};

    bool valid() const noexcept { return data != nullptr && size != 0; }
};

// Resolve one indexed resource without copying its bytes. The returned view
// remains valid while the source archive vector is unchanged.
bool find_e5_resource(const std::vector<std::uint8_t>& archive,
                      std::size_t resource_index,
                      std::size_t maximum_entry_count,
                      E5ResourceView& result) noexcept;

}  // namespace ccz::preview
