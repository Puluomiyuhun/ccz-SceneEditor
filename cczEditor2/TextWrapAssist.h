#pragma once

#include <afxstr.h>

namespace ccz
{
namespace text_wrap
{

// Text commands that use the long text editor and have a wrapping preset.
bool IsSupportedCommand(int commandId);

// The 0x16 information instruction has two established runtime modes.
int DefaultLimit(int commandId, bool historyMode);

// Keep the user supplied limit inside the range that can be represented by the
// editor's text buffer.  Invalid values fall back to the supplied default.
int NormalizeLimit(int value, int fallback);
int ParseLimit(const CString& text, int fallback);

// Presets are stored in the existing MFC profile/registry store.  They are
// written only after the dialog is confirmed with OK by the caller.
int LoadLimit(int commandId, bool historyMode);
void SaveLimit(int commandId, bool historyMode, int value);

// The checkbox is enabled by default.  Its last confirmed state is stored
// independently for each command/mode so changing the byte limit does not
// implicitly change whether wrapping is active.
bool LoadEnabled(int commandId, bool historyMode, bool defaultEnabled = true);
void SaveEnabled(int commandId, bool historyMode, bool enabled);

// Insert '\n' at CP936 byte boundaries.  Existing line breaks are preserved;
// [Cnn... ] color tags are copied as one unit and their [Cnn]/] syntax does
// not contribute to the visible byte count.
CString ApplyAutoWrap(const CString& text, int maxBytes);

} // namespace text_wrap
} // namespace ccz
