#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ccz::editor_preview {

struct BattleBitmapCacheKey {
	std::wstring path;
	std::uint64_t revision{};

	bool operator<(const BattleBitmapCacheKey& other) const noexcept;
	bool operator==(const BattleBitmapCacheKey& other) const noexcept;
};

BattleBitmapCacheKey MakeBattleBitmapCacheKey(
	const std::wstring& path, std::uint64_t revision,
	const std::wstring& subresource = {});

// Platform-neutral decoded-image residency and LRU metadata. The MFC page
// owns the actual immutable GDI+ bitmaps and applies returned evictions.
class BattleBitmapCachePolicy {
public:
	bool RecordDecode(const BattleBitmapCacheKey& key, std::size_t bytes);
	bool Pin(const BattleBitmapCacheKey& key) noexcept;
	void Unpin(const BattleBitmapCacheKey& key) noexcept;
	bool Contains(const BattleBitmapCacheKey& key) noexcept;
	std::vector<BattleBitmapCacheKey> EvictToBudget(std::size_t max_bytes);
	std::size_t ResidentBytes() const noexcept { return resident_bytes_; }
	std::size_t Size() const noexcept { return entries_.size(); }
	void Clear() noexcept;

private:
	struct Entry {
		std::size_t bytes{};
		std::size_t pin_count{};
		std::uint64_t last_use{};
	};

	void Touch(Entry& entry) noexcept;

	std::map<BattleBitmapCacheKey, Entry> entries_;
	std::size_t resident_bytes_{};
	std::uint64_t use_clock_{};
};

} // namespace ccz::editor_preview
