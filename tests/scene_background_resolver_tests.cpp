#include "../cczEditor2/SceneBackgroundResolver.h"

#include <cassert>
#include <vector>

int main() {
    int image_index = -1;

    // R-script outer scene: displayed number 4 is serialized as raw index 3.
    assert(ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{0, 3, 0, 0, 0}, image_index));
    assert(image_index == 3);
    int script_number = -1;
    assert(ccz::scene_preview::try_resolve_background_script_number(
        image_index, script_number));
    assert(script_number == 4);

    // The status display must report the instruction-facing serial, while the
    // renderer continues using the zero-based archive image index.
    assert(ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{0, 10, 0, 0, 0}, image_index));
    assert(image_index == 10);
    assert(ccz::scene_preview::try_resolve_background_script_number(
        image_index, script_number));
    assert(script_number == 11);

    // R-script interior scene: displayed number 238 is raw 197 + the 40-entry
    // archive prefix (the existing behavior must remain unchanged).
    assert(ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{2, 0, 0, 197, 0}, image_index));
    assert(image_index == 237);
    assert(ccz::scene_preview::try_resolve_background_script_number(
        image_index, script_number));
    assert(script_number == 238);

    assert(ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{2, 0, 0, 184, 0}, image_index));
    assert(image_index == 224);
    assert(ccz::scene_preview::try_resolve_background_script_number(
        image_index, script_number));
    assert(script_number == 225);

    // Other 0x27 branches and malformed payloads remain outside this scope.
    assert(!ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{1, 4, 0, 0, 0}, image_index));
    assert(!ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{0}, image_index));
    assert(!ccz::scene_preview::try_resolve_background_image_index(
        std::vector<int>{2, 0, 0}, image_index));
    return 0;
}
