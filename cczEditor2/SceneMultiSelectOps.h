#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace ccz {
namespace scene_preview {

struct LayoutActor {
	int id = -1;
	int x = 0;
	int y = 0;
};

struct LayoutResult {
	bool changed = false;
	bool eligible = false;
};

// Apply a sequence of insertions as one logical operation.  If an insertion
// fails, already-created handles are rolled back in reverse order.  The
// handle validity predicate is supplied by the caller because tree handles
// and standalone test handles use different sentinel values.
template <typename Item, typename Handle, typename InsertFn,
	          typename RollbackFn, typename IsValidFn>
inline bool ApplyBatchTransaction(
	const std::vector<Item>& items,
	InsertFn insert,
	RollbackFn rollback,
	IsValidFn isValid)
{
	std::vector<Handle> inserted;
	inserted.reserve(items.size());
	for (const Item& item : items) {
		const Handle handle = insert(item);
		if (!isValid(handle)) {
			for (auto it = inserted.rbegin(); it != inserted.rend(); ++it) {
				rollback(*it);
			}
			return false;
		}
		inserted.push_back(handle);
	}
	return true;
}

// Native wheel direction order: 0..3 with signed forward/backward steps.
inline int CycleDirection(int direction, int delta)
{
	const int normalized = ((direction % 4) + 4) % 4;
	return ((normalized + delta) % 4 + 4) % 4;
}

namespace detail {

inline std::vector<std::size_t> NormalizeSelection(
	const std::vector<LayoutActor>& actors,
	const std::vector<std::size_t>& selected)
{
	std::vector<std::size_t> normalized;
	for (std::size_t index : selected) {
		if (index >= actors.size()) continue;
		if (std::find(normalized.begin(), normalized.end(), index) == normalized.end())
			normalized.push_back(index);
	}
	return normalized;
}

inline int RoundDivideUnity(int numerator, int denominator)
{
	if (denominator <= 0) return 0;
	const int quotient = numerator / denominator;
	const int remainder = numerator % denominator;
	if (remainder == 0) return quotient;

	const int sign = (numerator < 0) ? -1 : 1;
	const int absoluteRemainder = std::abs(remainder);
	const int doubledRemainder = absoluteRemainder * 2;
	if (doubledRemainder < denominator) return quotient;
	if (doubledRemainder > denominator) return quotient + sign;

	// Mathf.RoundToInt uses the even integer for an exact half-way value.
	return (quotient % 2 == 0) ? quotient : quotient + sign;
}

} // namespace detail

inline LayoutResult AlignNearLines(
	std::vector<LayoutActor>& actors,
	const std::vector<std::size_t>& selected)
{
	const std::vector<std::size_t> indices = detail::NormalizeSelection(actors, selected);
	LayoutResult result;
	result.eligible = indices.size() >= 2;
	if (!result.eligible) return result;

	for (std::size_t left = 0; left < indices.size(); ++left) {
		for (std::size_t right = left + 1; right < indices.size(); ++right) {
			LayoutActor& a = actors[indices[left]];
			LayoutActor& b = actors[indices[right]];

			if (std::abs(a.x - b.x) < 2) {
				const int lineA = a.x;
				const int lineB = b.x;
				int countA = 0;
				int countB = 0;
				for (std::size_t index : indices) {
					countA += (actors[index].x == lineA) ? 1 : 0;
					countB += (actors[index].x == lineB) ? 1 : 0;
				}
				if (countA > countB && b.x != lineA) {
					b.x = lineA;
					result.changed = true;
				} else if (countB > countA && a.x != lineB) {
					a.x = lineB;
					result.changed = true;
				}
			}

			if (std::abs(a.y - b.y) < 2) {
				const int lineA = a.y;
				const int lineB = b.y;
				int countA = 0;
				int countB = 0;
				for (std::size_t index : indices) {
					countA += (actors[index].y == lineA) ? 1 : 0;
					countB += (actors[index].y == lineB) ? 1 : 0;
				}
				if (countA > countB && b.y != lineA) {
					b.y = lineA;
					result.changed = true;
				} else if (countB > countA && a.y != lineB) {
					a.y = lineB;
					result.changed = true;
				}
			}
		}
	}

	return result;
}

inline LayoutResult AlignToPrimaryAxis(
	std::vector<LayoutActor>& actors,
	std::size_t primaryIndex,
	const std::vector<std::size_t>& selected,
	bool alignX)
{
	const std::vector<std::size_t> indices = detail::NormalizeSelection(actors, selected);
	LayoutResult result;
	result.eligible = indices.size() >= 2 &&
		std::find(indices.begin(), indices.end(), primaryIndex) != indices.end();
	if (!result.eligible) return result;

	const int target = alignX ? actors[primaryIndex].x : actors[primaryIndex].y;
	for (std::size_t index : indices) {
		if (index == primaryIndex) continue;
		int& coordinate = alignX ? actors[index].x : actors[index].y;
		if (coordinate != target) {
			coordinate = target;
			result.changed = true;
		}
	}
	return result;
}

inline LayoutResult DistributeAxes(
	std::vector<LayoutActor>& actors,
	const std::vector<std::size_t>& selected)
{
	const std::vector<std::size_t> indices = detail::NormalizeSelection(actors, selected);
	LayoutResult result;
	result.eligible = indices.size() >= 3;
	if (!result.eligible) return result;

	std::vector<std::size_t> byX = indices;
	std::stable_sort(byX.begin(), byX.end(), [&](std::size_t lhs, std::size_t rhs) {
		return actors[lhs].x < actors[rhs].x;
	});
	const int firstX = actors[byX.front()].x;
	const int lastX = actors[byX.back()].x;
	const int stepX = detail::RoundDivideUnity(lastX - firstX,
		static_cast<int>(byX.size() - 1));
	for (std::size_t i = 1; i < byX.size(); ++i) {
		const int target = firstX + static_cast<int>(i) * stepX;
		if (actors[byX[i]].x != target) {
			actors[byX[i]].x = target;
			result.changed = true;
		}
	}

	std::vector<std::size_t> byY = indices;
	std::stable_sort(byY.begin(), byY.end(), [&](std::size_t lhs, std::size_t rhs) {
		return actors[lhs].y < actors[rhs].y;
	});
	const int firstY = actors[byY.front()].y;
	const int lastY = actors[byY.back()].y;
	const int stepY = detail::RoundDivideUnity(lastY - firstY,
		static_cast<int>(byY.size() - 1));
	for (std::size_t i = 1; i < byY.size(); ++i) {
		const int target = firstY + static_cast<int>(i) * stepY;
		if (actors[byY[i]].y != target) {
			actors[byY[i]].y = target;
			result.changed = true;
		}
	}

	return result;
}

inline LayoutResult PreciseDistributeAxis(
	std::vector<LayoutActor>& actors,
	const std::vector<std::size_t>& selected,
	bool distributeX)
{
	const std::vector<std::size_t> indices = detail::NormalizeSelection(actors, selected);
	LayoutResult result;
	result.eligible = indices.size() >= 3;
	if (!result.eligible) return result;

	std::vector<std::size_t> ordered = indices;
	std::stable_sort(ordered.begin(), ordered.end(), [&](std::size_t lhs, std::size_t rhs) {
		const int lhsCoordinate = distributeX ? actors[lhs].x : actors[lhs].y;
		const int rhsCoordinate = distributeX ? actors[rhs].x : actors[rhs].y;
		return lhsCoordinate < rhsCoordinate;
	});

	const int first = distributeX ? actors[ordered.front()].x : actors[ordered.front()].y;
	const int last = distributeX ? actors[ordered.back()].x : actors[ordered.back()].y;
	const int denominator = static_cast<int>(ordered.size() - 1);
	const int span = last - first;
	for (std::size_t i = 1; i + 1 < ordered.size(); ++i) {
		const int target = first + detail::RoundDivideUnity(
			span * static_cast<int>(i), denominator);
		int& coordinate = distributeX ? actors[ordered[i]].x : actors[ordered[i]].y;
		if (coordinate != target) {
			coordinate = target;
			result.changed = true;
		}
	}
	return result;
}

} // namespace scene_preview
} // namespace ccz
