#include "test_framework.h"

#include "../../cczEditor2/PreviewIntegration/BattleBitmapCachePolicy.h"

#include <algorithm>

using ccz::editor_preview::BattleBitmapCacheKey;
using ccz::editor_preview::BattleBitmapCachePolicy;
using ccz::editor_preview::MakeBattleBitmapCacheKey;

CCZ_TEST(battle_bitmap_cache_normalizes_path_case_and_slashes) {
	const auto left = MakeBattleBitmapCacheKey(L"D:/Mod/map/M010.jpg", 7);
	const auto right = MakeBattleBitmapCacheKey(L"d:\\mod\\map\\m010.jpg", 7);
	CCZ_REQUIRE(left == right);
}

CCZ_TEST(battle_bitmap_cache_reuses_same_map_key) {
	BattleBitmapCachePolicy policy;
	const auto key = MakeBattleBitmapCacheKey(L"D:\\Mod\\map\\m010.jpg", 7);
	CCZ_REQUIRE(policy.RecordDecode(key, 100));
	CCZ_REQUIRE(policy.Contains(key));
	CCZ_REQUIRE_EQ(policy.Size(), 1u);
	CCZ_REQUIRE_EQ(policy.ResidentBytes(), 100u);
}

CCZ_TEST(battle_bitmap_cache_keeps_distinct_maps_separate) {
	BattleBitmapCachePolicy policy;
	const auto first = MakeBattleBitmapCacheKey(L"D:\\Mod\\map\\m010.jpg", 7);
	const auto second = MakeBattleBitmapCacheKey(L"D:\\Mod\\map\\m011.jpg", 7);
	CCZ_REQUIRE(policy.RecordDecode(first, 100));
	CCZ_REQUIRE(policy.RecordDecode(second, 200));
	CCZ_REQUIRE_EQ(policy.Size(), 2u);
	CCZ_REQUIRE_EQ(policy.ResidentBytes(), 300u);
}

CCZ_TEST(battle_bitmap_cache_invalidates_a_changed_file_revision) {
	BattleBitmapCachePolicy policy;
	const auto oldRevision = MakeBattleBitmapCacheKey(
		L"D:\\Mod\\map\\m010.jpg", 7);
	const auto newRevision = MakeBattleBitmapCacheKey(
		L"D:\\Mod\\map\\m010.jpg", 8);
	CCZ_REQUIRE(policy.RecordDecode(oldRevision, 100));
	CCZ_REQUIRE(policy.RecordDecode(newRevision, 120));
	CCZ_REQUIRE_EQ(policy.Size(), 2u);
	CCZ_REQUIRE(policy.Contains(oldRevision));
	CCZ_REQUIRE(policy.Contains(newRevision));
}

CCZ_TEST(battle_bitmap_cache_evicts_least_recently_used_unpinned_entries) {
	BattleBitmapCachePolicy policy;
	const auto oldest = MakeBattleBitmapCacheKey(L"oldest", 1);
	const auto newest = MakeBattleBitmapCacheKey(L"newest", 1);
	CCZ_REQUIRE(policy.RecordDecode(oldest, 60));
	CCZ_REQUIRE(policy.RecordDecode(newest, 60));
	CCZ_REQUIRE(policy.Contains(oldest));
	const auto evicted = policy.EvictToBudget(60);
	CCZ_REQUIRE_EQ(evicted.size(), 1u);
	CCZ_REQUIRE(evicted.front() == newest);
	CCZ_REQUIRE(policy.Contains(oldest));
	CCZ_REQUIRE(!policy.Contains(newest));
	CCZ_REQUIRE_EQ(policy.ResidentBytes(), 60u);
}

CCZ_TEST(battle_bitmap_cache_pins_protect_active_resources_until_unpinned) {
	BattleBitmapCachePolicy policy;
	const auto map = MakeBattleBitmapCacheKey(L"map", 1);
	const auto sprite = MakeBattleBitmapCacheKey(L"sprite", 1);
	CCZ_REQUIRE(policy.RecordDecode(map, 80));
	CCZ_REQUIRE(policy.RecordDecode(sprite, 40));
	CCZ_REQUIRE(policy.Pin(map));
	const auto firstEvictions = policy.EvictToBudget(40);
	CCZ_REQUIRE_EQ(firstEvictions.size(), 1u);
	CCZ_REQUIRE(firstEvictions.front() == sprite);
	CCZ_REQUIRE(policy.Contains(map));
	CCZ_REQUIRE_EQ(policy.ResidentBytes(), 80u);
	policy.Unpin(map);
	const auto secondEvictions = policy.EvictToBudget(40);
	CCZ_REQUIRE_EQ(secondEvictions.size(), 1u);
	CCZ_REQUIRE(secondEvictions.front() == map);
	CCZ_REQUIRE_EQ(policy.ResidentBytes(), 0u);
}
