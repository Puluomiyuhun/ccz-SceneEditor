#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace ccz::editor_preview {

inline constexpr wchar_t kBattleWritebackQaMarkerFile[] =
	L".ccz-s-battle-writeback-qa";
inline constexpr char kBattleWritebackQaMarkerContent[] =
	"CCZ_S_BATTLE_WRITEBACK_QA_V1";

inline std::wstring BattleWritebackQaCopyRootForExecutable(
	std::wstring_view executable_path) noexcept
{
	try {
		if (executable_path.empty()) return {};
		namespace fs = std::filesystem;
		const fs::path executable{ std::wstring(executable_path) };
		if (!executable.is_absolute() || executable.filename().empty()) return {};
		return (executable.parent_path() / L"writeback-qa-copies").wstring();
	} catch (...) {
		return {};
	}
}

// Accepts only a direct S_*.eex / S-*.eex child of a marked QA-copy folder.
// The function performs no writes and follows filesystem links before
// comparing the document with the designated root.
inline bool IsBattleWritebackQaCopy(
	std::wstring_view document_path,
	std::wstring_view qa_copy_root) noexcept
{
	try {
		if (document_path.empty() || qa_copy_root.empty()) return false;
		namespace fs = std::filesystem;
		std::error_code error;
		const fs::path requested_root{ std::wstring(qa_copy_root) };
		const fs::path requested_document{ std::wstring(document_path) };
		if (!requested_root.is_absolute() || !requested_document.is_absolute())
			return false;

		const fs::path root = fs::weakly_canonical(requested_root, error);
		if (error || !fs::is_directory(root, error) || error) return false;
		const fs::path marker = root / kBattleWritebackQaMarkerFile;
		if (!fs::is_regular_file(marker, error) || error) return false;
		std::ifstream marker_stream(marker, std::ios::binary);
		std::string marker_content;
		if (!marker_stream || !std::getline(marker_stream, marker_content) ||
			marker_content != kBattleWritebackQaMarkerContent) return false;

		const fs::path document =
			fs::weakly_canonical(requested_document, error);
		if (error || !fs::is_regular_file(document, error) || error) return false;
		const auto normalized = [](const fs::path& path) {
			std::wstring value = path.lexically_normal().wstring();
			std::replace(value.begin(), value.end(), L'/', L'\\');
			std::transform(value.begin(), value.end(), value.begin(),
				[](wchar_t character) { return std::towlower(character); });
			return value;
		};
		if (normalized(document.parent_path()) != normalized(root)) return false;

		std::wstring filename = document.filename().wstring();
		std::transform(filename.begin(), filename.end(), filename.begin(),
			[](wchar_t character) { return std::towlower(character); });
		std::wstring extension = document.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](wchar_t character) { return std::towlower(character); });
		const bool s_prefix = filename.rfind(L"s_", 0) == 0 ||
			filename.rfind(L"s-", 0) == 0;
		return s_prefix && extension == L".eex";
	} catch (...) {
		return false;
	}
}

}  // namespace ccz::editor_preview
