#pragma once

#include <algorithm>
#include <vector>

namespace ccz {
namespace scene_preview {

// 地图头像指令只接受直接的 Data.e5 人物号。
// 变量、特殊 Per2 选择器和“全部角色”均不进入静态预览状态。
inline bool is_direct_data_portrait_target(int personID) noexcept
{
	return personID >= 0 && personID < 1024;
}

// 与台词预览共用的普通头像映射：0号人物使用 Face.e5 第0张，
// 其他人物使用 Data.e5 头像码 + 7 的零基 Face.e5 索引。
inline int face_image_index_for_data_person(int personID, unsigned short avatarCode) noexcept
{
	if (!is_direct_data_portrait_target(personID)) return -1;
	if (personID == 0) return 0;
	return static_cast<int>(avatarCode) + 7;
}

struct MapPortraitPlacement {
	int personID = -1;
	int x = 0;
	int y = 0;
	int faceImageIndex = -1;
};

class MapPortraitState
{
public:
	bool show(int personID, int x, int y)
	{
		if (!is_direct_data_portrait_target(personID)) return false;
		MapPortraitPlacement* existing = find(personID);
		if (existing) {
			existing->x = x;
			existing->y = y;
			return true;
		}
		m_items.push_back({ personID, x, y, -1 });
		return true;
	}

	bool move(int personID, int x, int y)
	{
		if (!is_direct_data_portrait_target(personID)) return false;
		MapPortraitPlacement* existing = find(personID);
		if (!existing) return false;
		existing->x = x;
		existing->y = y;
		return true;
	}

	bool hide(int personID)
	{
		if (!is_direct_data_portrait_target(personID)) return false;
		const std::size_t before = m_items.size();
		m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
			[personID](const MapPortraitPlacement& item) {
				return item.personID == personID;
			}), m_items.end());
		return m_items.size() != before;
	}

	void clear() noexcept
	{
		m_items.clear();
	}

	MapPortraitPlacement* find(int personID) noexcept
	{
		for (auto& item : m_items) {
			if (item.personID == personID) return &item;
		}
		return nullptr;
	}

	const MapPortraitPlacement* find(int personID) const noexcept
	{
		for (const auto& item : m_items) {
			if (item.personID == personID) return &item;
		}
		return nullptr;
	}

	const std::vector<MapPortraitPlacement>& items() const noexcept
	{
		return m_items;
	}

	std::vector<MapPortraitPlacement>& items() noexcept
	{
		return m_items;
	}

private:
	std::vector<MapPortraitPlacement> m_items;
};

}  // namespace scene_preview
}  // namespace ccz
