#include <ccz/preview/portrait_resolver.h>

#include <cstddef>
#include <cstdint>

namespace ccz::preview {
namespace {

constexpr std::int32_t kFirstDataPerson = 0;
constexpr std::int32_t kLastDataPerson = 1023;

bool can_read(const std::vector<std::uint8_t>& bytes, std::uint64_t offset,
              std::uint64_t count) {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t read_le16(const std::vector<std::uint8_t>& bytes,
                        std::uint64_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(offset)]) |
        (static_cast<std::uint16_t>(
             bytes[static_cast<std::size_t>(offset + 1)])
         << 8));
}

void fail(PortraitResult& result, const wchar_t* message) {
    result.resolved = false;
    result.encoded_image.clear();
    result.diagnostic = message;
}

}  // namespace

PortraitResult resolve_portrait(
    std::int32_t person_id,
    std::uint16_t face_state,
    const std::vector<std::uint8_t>& data_e5,
    const DialogueProfile& profile,
    const FaceArchive& face_archive,
    const std::vector<std::uint8_t>& face_archive_bytes) {
    PortraitResult result;
    result.person_id = person_id;
    result.face_state = face_state;

    if (person_id < kFirstDataPerson || person_id > kLastDataPerson) {
        fail(result, L"头像人物号不在 Data.e5 人物范围内");
        return result;
    }
    if (profile.data_record_stride == 0 || profile.avatar_width != 2) {
        fail(result, L"Data.e5 头像字段规格无效");
        return result;
    }

    const std::uint64_t record_offset =
        static_cast<std::uint64_t>(profile.data_record_base) +
        static_cast<std::uint64_t>(person_id) * profile.data_record_stride;
    const std::uint64_t avatar_offset =
        record_offset + profile.avatar_offset;
    if (!can_read(data_e5, avatar_offset, 2)) {
        fail(result, L"Data.e5 头像记录越界");
        return result;
    }
    const std::uint16_t avatar_code = read_le16(data_e5, avatar_offset);

    bool has_special_mapping = false;
    std::uint32_t candidate_index = 0;
    for (const SpecialFaceMapping& mapping : profile.special_state_map) {
        if (mapping.person_id == person_id && mapping.face_state == face_state) {
            candidate_index = mapping.zero_based_face_index;
            has_special_mapping = true;
            break;
        }
    }

    if (!has_special_mapping) {
        if (person_id == 0) {
            if (face_state > 3) {
                fail(result, L"0号人物头像状态不在普通、惊讶、愤怒、欣喜范围内");
                return result;
            }
            const std::uint32_t early_index =
                profile.face_index_base + face_state;
            const std::uint32_t late_index = early_index + 4;
            const FaceArchiveEntry* early_entry =
                find_face_entry(face_archive, early_index);
            const FaceArchiveEntry* late_entry =
                find_face_entry(face_archive, late_index);
            if (early_entry == nullptr || late_entry == nullptr) {
                fail(result, L"Face.e5 缺少0号人物的前八张章节头像");
                return result;
            }
            const std::vector<std::uint8_t> early_payload =
                copy_face_payload(face_archive_bytes, *early_entry);
            const std::vector<std::uint8_t> late_payload =
                copy_face_payload(face_archive_bytes, *late_entry);
            if (early_payload.empty() || early_payload != late_payload) {
                fail(result, L"0号人物前后章节头像不同，预览尚无章节状态可供选择");
                return result;
            }
            candidate_index = early_index;
        } else {
            if (face_state != 0 && face_state != 255) {
                fail(result, L"头像状态尚未登记 Face.e5 映射");
                return result;
            }
            if (!profile.face_index_bias.has_value()) {
                fail(result, L"普通头像缺少 Face.e5 索引偏移");
                return result;
            }

            const std::int64_t candidate =
                static_cast<std::int64_t>(avatar_code) +
                static_cast<std::int64_t>(*profile.face_index_bias);
            if (candidate < 0 || candidate > 0xffffffffLL) {
                fail(result, L"普通头像转换后的 Face.e5 索引越界");
                return result;
            }
            candidate_index = static_cast<std::uint32_t>(candidate);
        }
    }

    result.face_image_index = candidate_index;
    const FaceArchiveEntry* entry =
        find_face_entry(face_archive, candidate_index);
    if (entry == nullptr) {
        fail(result, L"Face.e5 中不存在目标头像索引");
        return result;
    }

    result.encoded_image = copy_face_payload(face_archive_bytes, *entry);
    if (result.encoded_image.empty()) {
        fail(result, L"Face.e5 头像载荷读取失败");
        return result;
    }

    result.resolved = true;
    return result;
}

}  // namespace ccz::preview


