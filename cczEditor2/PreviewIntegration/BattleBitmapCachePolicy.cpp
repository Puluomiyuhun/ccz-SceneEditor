#include "BattleBitmapCachePolicy.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <utility>

namespace ccz::editor_preview {

bool BattleBitmapCacheKey::operator<(
	const BattleBitmapCacheKey& other) const noexcept
{
	if (path != other.path) return path < other.path;
	return revision < other.revision;
}

bool BattleBitmapCacheKey::operator==(
	const BattleBitmapCacheKey& other) const noexcept
{
	return path == other.path && revision == other.revision;
}

BattleBitmapCacheKey MakeBattleBitmapCacheKey(
	const std::wstring& path, std::uint64_t revision,
	const std::wstring& subresource)
{
	BattleBitmapCacheKey key;
	key.path = path;
	std::replace(key.path.begin(), key.path.end(), L'/', L'\\');
	while (key.path.size() > 3 && key.path.back() == L'\\')
		key.path.pop_back();
	std::transform(key.path.begin(), key.path.end(), key.path.begin(),
		[](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
	if (!subresource.empty()) {
		key.path += L"#";
		key.path += subresource;
	}
	key.revision = revision;
	return key;
}

void BattleBitmapCachePolicy::Touch(Entry& entry) noexcept
{
	if (use_clock_ != UINT64_MAX) ++use_clock_;
	else {
		// Rebase the small metadata table when the counter wraps instead of
		// allowing recently used resources to look older than every cache entry.
		std::uint64_t oldest = use_clock_;
		for (const auto& item : entries_)
			oldest = (std::min)(oldest, item.second.last_use);
		const std::uint64_t offset = oldest > 0 ? oldest - 1 : 0;
		for (auto& item : entries_) item.second.last_use -= offset;
		use_clock_ -= offset;
		if (use_clock_ != UINT64_MAX) ++use_clock_;
	}
	entry.last_use = use_clock_;
}

bool BattleBitmapCachePolicy::RecordDecode(
	const BattleBitmapCacheKey& key, std::size_t bytes)
{
	if (key.path.empty() || bytes == 0) return false;
	auto found = entries_.find(key);
	const std::size_t previousBytes = found == entries_.end()
		? 0 : found->second.bytes;
	if (bytes > (std::numeric_limits<std::size_t>::max)() -
		(resident_bytes_ - previousBytes)) return false;
	auto& entry = found == entries_.end()
		? entries_[key] : found->second;
	resident_bytes_ -= previousBytes;
	entry.bytes = bytes;
	resident_bytes_ += bytes;
	Touch(entry);
	return true;
}

bool BattleBitmapCachePolicy::Pin(
	const BattleBitmapCacheKey& key) noexcept
{
	auto found = entries_.find(key);
	if (found == entries_.end()) return false;
	if (found->second.pin_count != (std::numeric_limits<std::size_t>::max)())
		++found->second.pin_count;
	Touch(found->second);
	return true;
}

void BattleBitmapCachePolicy::Unpin(
	const BattleBitmapCacheKey& key) noexcept
{
	auto found = entries_.find(key);
	if (found != entries_.end() && found->second.pin_count > 0)
		--found->second.pin_count;
}

bool BattleBitmapCachePolicy::Contains(
	const BattleBitmapCacheKey& key) noexcept
{
	auto found = entries_.find(key);
	if (found == entries_.end()) return false;
	Touch(found->second);
	return true;
}

std::vector<BattleBitmapCacheKey> BattleBitmapCachePolicy::EvictToBudget(
	std::size_t maxBytes)
{
	std::vector<BattleBitmapCacheKey> evicted;
	while (resident_bytes_ > maxBytes) {
		auto victim = entries_.end();
		for (auto current = entries_.begin(); current != entries_.end(); ++current) {
			if (current->second.pin_count != 0) continue;
			if (victim == entries_.end() ||
				current->second.last_use < victim->second.last_use)
				victim = current;
		}
		if (victim == entries_.end()) break;
		resident_bytes_ -= victim->second.bytes;
		evicted.push_back(victim->first);
		entries_.erase(victim);
	}
	return evicted;
}

void BattleBitmapCachePolicy::Clear() noexcept
{
	entries_.clear();
	resident_bytes_ = 0;
	use_clock_ = 0;
}

} // namespace ccz::editor_preview
