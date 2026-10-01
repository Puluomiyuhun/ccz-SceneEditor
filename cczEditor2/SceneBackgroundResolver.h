#pragma once

#include <limits>
#include <vector>

namespace ccz {
namespace scene_preview {

// Mmap.e5 stores interior backgrounds after the first 40 entries.  The
// script-facing number is one-based for both branches, while the archive
// index is zero-based; this helper returns the actual archive index.
inline bool try_resolve_background_image_index(
    const std::vector<int>& int_data, int& image_index) noexcept {
    image_index = -1;
    if (int_data.empty()) {
        return false;
    }

    switch (int_data[0]) {
        case 0:  // 外场景: serialized value is int_data[1] + 1.
            if (int_data.size() < 2 || int_data[1] < 0) {
                return false;
            }
            image_index = int_data[1];
            return true;

        case 2:  // 内场景: serialized value is int_data[3] + 41.
            if (int_data.size() < 4 || int_data[3] < 0 ||
                int_data[3] > (std::numeric_limits<int>::max)() - 40) {
                return false;
            }
            image_index = int_data[3] + 40;
            return true;

        default:
            // 中国地图和战场地图 are outside the current R-script scope.
            return false;
    }
}

// The status bar shows the one-based number written in the script, while
// rendering continues to use the zero-based Mmap.e5 image index.
inline bool try_resolve_background_script_number(
    int image_index, int& script_number) noexcept {
    script_number = -1;
    if (image_index < 0 ||
        image_index == (std::numeric_limits<int>::max)()) {
        return false;
    }
    script_number = image_index + 1;
    return true;
}

}  // namespace scene_preview
}  // namespace ccz
