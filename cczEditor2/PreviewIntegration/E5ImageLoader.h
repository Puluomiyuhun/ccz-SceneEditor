#pragma once

#include <afxstr.h>
#include <GdiPlus.h>

// Shared binary-image decoding utility.  It knows the E5 container format
// only; callers keep their R/S asset tables and resource-selection logic
// separate.
Gdiplus::Bitmap* LoadBitmapFromE5(const CString& e5_file_path,
	int image_index, CString& diagnostic,
	bool apply_top_left_transparency = true);
