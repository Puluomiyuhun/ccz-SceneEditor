#pragma once

#include <vector>

namespace ccz {
namespace scene_preview {

enum class PositionSourceKind {
    None,
    ActorAppear,
    ActorMove,
    PortraitShow,
    PortraitMove,
};

struct PositionSource {
    PositionSourceKind kind = PositionSourceKind::None;
    int instructionId = -1;
    int xIndex = -1;
    int yIndex = -1;
    int subjectId = -1;
    int token = -1;

    bool valid() const noexcept
    {
        return kind != PositionSourceKind::None && instructionId >= 0 &&
            xIndex >= 0 && yIndex >= 0 && subjectId >= 0;
    }
};

struct PositionSourceSpec {
    PositionSourceKind kind = PositionSourceKind::None;
    int instructionId = -1;
    int xIndex = -1;
    int yIndex = -1;

    bool valid() const noexcept
    {
        return kind != PositionSourceKind::None && instructionId >= 0 &&
            xIndex >= 0 && yIndex >= 0;
    }
};

inline int position_subject_index(PositionSourceKind kind) noexcept
{
	if (kind == PositionSourceKind::ActorAppear) return 0;
	if (kind == PositionSourceKind::ActorMove) return 1;
	if (kind == PositionSourceKind::PortraitShow ||
		kind == PositionSourceKind::PortraitMove) return 0;
	return -1;
}

inline int position_direction_index(PositionSourceKind kind) noexcept
{
	if (kind == PositionSourceKind::ActorAppear) return 3;
	if (kind == PositionSourceKind::ActorMove) return 5;
	return -1;
}

inline bool PositionSourceMatchesInstruction(
	const PositionSource& source,
	const std::vector<int>& int_data) noexcept
{
	if (!source.valid()) return false;
	const int subjectIndex = position_subject_index(source.kind);
	return subjectIndex >= 0 &&
		subjectIndex < static_cast<int>(int_data.size()) &&
		int_data[subjectIndex] == source.subjectId;
}

inline PositionSourceSpec actor_position_source_spec(int instructionId) noexcept
{
    if (instructionId == 48) return {
        PositionSourceKind::ActorAppear, 48, 1, 2
    };
    if (instructionId == 50) return {
        PositionSourceKind::ActorMove, 50, 3, 4
    };
    return {};
}

inline PositionSourceSpec portrait_position_source_spec(int instructionId) noexcept
{
    if (instructionId == 41) return {
        PositionSourceKind::PortraitShow, 41, 1, 2
    };
    if (instructionId == 42) return {
        PositionSourceKind::PortraitMove, 42, 1, 2
    };
    return {};
}

// A tiny model used by standalone tests and by callers that need to express
// the "latest effective position instruction" rule without MFC handles.
class PositionSourceTracker
{
public:
    PositionSource source;

    bool recordActor(int subjectId, int instructionId, int token = -1) noexcept
    {
        const PositionSourceSpec spec = actor_position_source_spec(instructionId);
        if (!spec.valid() || subjectId < 0) return false;
        source = { spec.kind, spec.instructionId, spec.xIndex, spec.yIndex,
            subjectId, token };
        return true;
    }

    bool recordPortrait(int subjectId, int instructionId, int token = -1) noexcept
    {
        const PositionSourceSpec spec = portrait_position_source_spec(instructionId);
        if (!spec.valid() || subjectId < 0) return false;
        source = { spec.kind, spec.instructionId, spec.xIndex, spec.yIndex,
            subjectId, token };
        return true;
    }

    bool hidePortrait(int subjectId) noexcept
    {
        if (!source.valid() || source.subjectId != subjectId) return false;
        source = {};
        return true;
    }
};

}  // namespace scene_preview
}  // namespace ccz
