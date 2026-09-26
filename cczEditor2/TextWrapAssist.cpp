#include "pch.h"
#include "TextWrapAssist.h"

#include <algorithm>

namespace
{
constexpr int kMaxStoredLimit = 4096;
constexpr UINT kCp936 = 936;
constexpr LPCTSTR kProfileSection = _T("TextWrapAssist");

bool IsHexDigit(wchar_t ch)
{
	return (ch >= L'0' && ch <= L'9') ||
		(ch >= L'a' && ch <= L'f') ||
		(ch >= L'A' && ch <= L'F');
}

int Cp936ByteCount(const CString& text, int start, int length)
{
	if (length <= 0)
		return 0;

	CString part = text.Mid(start, length);
	int bytes = WideCharToMultiByte(
		kCp936, 0, part.GetString(), part.GetLength(), nullptr, 0, nullptr, nullptr);
	// A malformed UTF-16 fragment should not make wrapping silently stop.  The
	// fallback counts each code unit as one byte, which is conservative for the
	// editor's CP936-oriented input.
	return bytes > 0 ? bytes : length;
}

bool ReadColorTag(const CString& text, int start, int& end, int& payloadBytes)
{
	const int length = text.GetLength();
	if (start + 4 > length || text[start] != L'[' || text[start + 1] != L'C' ||
		!IsHexDigit(text[start + 2]) || !IsHexDigit(text[start + 3]))
		return false;

	const int close = text.Find(L']', start + 4);
	if (close < 0)
	{
		// While the user is still typing a color tag, keep the unfinished
		// remainder together.  This prevents live wrapping from inserting a
		// newline between the opening marker and the eventual closing bracket.
		end = length;
		payloadBytes = Cp936ByteCount(text, start + 4, length - (start + 4));
		return true;
	}

	end = close + 1;
	payloadBytes = Cp936ByteCount(text, start + 4, close - (start + 4));
	return true;
}

void AppendWrappedLine(const CString& line, int maxBytes, CString& output)
{
	int visibleBytes = 0;
	for (int i = 0; i < line.GetLength();)
	{
		int unitEnd = i + 1;
		int unitBytes = 0;

		if (!ReadColorTag(line, i, unitEnd, unitBytes))
		{
			// Keep a UTF-16 surrogate pair together even though CP936 normally
			// maps it to the default replacement character.
			if (i + 1 < line.GetLength() &&
				line[i] >= 0xD800 && line[i] <= 0xDBFF &&
				line[i + 1] >= 0xDC00 && line[i + 1] <= 0xDFFF)
				unitEnd = i + 2;
			unitBytes = Cp936ByteCount(line, i, unitEnd - i);
		}

		// A complete color tag is the smallest legal unit.  If its payload is
		// longer than the configured limit, keep it intact rather than splitting
		// the tag syntax in the middle.
		if (visibleBytes > 0 && visibleBytes + unitBytes > maxBytes)
		{
			output += L'\n';
			visibleBytes = 0;
		}

		output += line.Mid(i, unitEnd - i);
		visibleBytes += unitBytes;
		i = unitEnd;
	}
}

LPCTSTR ProfileKey(int commandId, bool historyMode)
{
	switch (commandId)
	{
	case 20:  return _T("Dialogue1");
	case 21:  return _T("Dialogue2");
	case 122: return _T("Dialogue3");
	case 22:  return historyMode ? _T("InfoHistory") : _T("InfoOrdinary");
	case 44:  return _T("MapText");
	default:  return _T("");
	}
}

LPCTSTR EnabledProfileKey(int commandId, bool historyMode)
{
	switch (commandId)
	{
	case 20:  return _T("Dialogue1WrapEnabled");
	case 21:  return _T("Dialogue2WrapEnabled");
	case 122: return _T("Dialogue3WrapEnabled");
	case 22:  return historyMode ? _T("InfoHistoryWrapEnabled") : _T("InfoOrdinaryWrapEnabled");
	case 44:  return _T("MapTextWrapEnabled");
	default:  return _T("");
	}
}
}

namespace ccz
{
namespace text_wrap
{

bool IsSupportedCommand(int commandId)
{
	return commandId == 20 || commandId == 21 || commandId == 22 ||
		commandId == 44 || commandId == 122;
}

int DefaultLimit(int commandId, bool historyMode)
{
	switch (commandId)
	{
	case 20:
	case 21:
	case 122:
		return 38;
	case 22:
		return historyMode ? 68 : 36;
	case 44:
		return 68;
	default:
		return 0;
	}
}

int NormalizeLimit(int value, int fallback)
{
	const int safeFallback = fallback > 0 ? (std::min)(fallback, kMaxStoredLimit) : 1;
	if (value < 1)
		return safeFallback;
	return (std::min)(value, kMaxStoredLimit);
}

int ParseLimit(const CString& text, int fallback)
{
	if (text.IsEmpty())
		return NormalizeLimit(0, fallback);

	const int value = _ttoi(text);
	return NormalizeLimit(value, fallback);
}

int LoadLimit(int commandId, bool historyMode)
{
	const int fallback = DefaultLimit(commandId, historyMode);
	if (fallback <= 0 || ProfileKey(commandId, historyMode)[0] == L'\0')
		return fallback;

	CWinApp* app = AfxGetApp();
	if (app == nullptr)
		return fallback;

	return NormalizeLimit(
		app->GetProfileInt(kProfileSection, ProfileKey(commandId, historyMode), fallback),
		fallback);
}

void SaveLimit(int commandId, bool historyMode, int value)
{
	if (!IsSupportedCommand(commandId) || ProfileKey(commandId, historyMode)[0] == L'\0')
		return;

	CWinApp* app = AfxGetApp();
	if (app == nullptr)
		return;

	app->WriteProfileInt(
		kProfileSection, ProfileKey(commandId, historyMode),
		NormalizeLimit(value, DefaultLimit(commandId, historyMode)));
}

bool LoadEnabled(int commandId, bool historyMode, bool defaultEnabled)
{
	if (!IsSupportedCommand(commandId) || EnabledProfileKey(commandId, historyMode)[0] == L'\0')
		return defaultEnabled;

	CWinApp* app = AfxGetApp();
	if (app == nullptr)
		return defaultEnabled;

	return app->GetProfileInt(
		kProfileSection, EnabledProfileKey(commandId, historyMode),
		defaultEnabled ? 1 : 0) != 0;
}

void SaveEnabled(int commandId, bool historyMode, bool enabled)
{
	if (!IsSupportedCommand(commandId) || EnabledProfileKey(commandId, historyMode)[0] == L'\0')
		return;

	CWinApp* app = AfxGetApp();
	if (app == nullptr)
		return;

	app->WriteProfileInt(
		kProfileSection, EnabledProfileKey(commandId, historyMode), enabled ? 1 : 0);
}

CString ApplyAutoWrap(const CString& text, int maxBytes)
{
	if (maxBytes <= 0 || text.IsEmpty())
		return text;

	// EDIT controls return CRLF while the EEX text buffer stores LF.  Normalize
	// here so the line scanner preserves both manually entered and generated
	// breaks without ever counting CR/LF toward the byte limit.
	CString normalized;
	normalized.Preallocate(text.GetLength());
	for (int i = 0; i < text.GetLength(); ++i)
	{
		if (text[i] == L'\r')
		{
			if (i + 1 < text.GetLength() && text[i + 1] == L'\n')
				++i;
			normalized += L'\n';
		}
		else
		{
			normalized += text[i];
		}
	}

	CString output;
	output.Preallocate(normalized.GetLength() + normalized.GetLength() / 8);
	int lineStart = 0;
	while (lineStart <= normalized.GetLength())
	{
		const int newline = normalized.Find(L'\n', lineStart);
		const int lineEnd = newline >= 0 ? newline : normalized.GetLength();
		AppendWrappedLine(normalized.Mid(lineStart, lineEnd - lineStart), maxBytes, output);
		if (newline < 0)
			break;
		output += L'\n';
		lineStart = newline + 1;
	}
	return output;
}

} // namespace text_wrap
} // namespace ccz
