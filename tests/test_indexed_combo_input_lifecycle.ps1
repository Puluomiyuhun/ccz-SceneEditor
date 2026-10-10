$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

if ($header -notmatch 'void RestoreSearchEditFocus\(\)') {
    throw 'indexed combo is missing the focus restoration helper'
}
if ($header -notmatch 'dropdown_highlight_valid_') {
    throw 'native highlight state is not tracked separately from the committed selection'
}
if ($header -notmatch 'select_all_on_next_focus_') {
    throw 'search-edit focus lifecycle state is missing'
}

$selectionChange = [regex]::Match($combo, '(?s)void CMyComboBox::OnSelectionChange\(\).*?\n\}')
if (-not $selectionChange.Success -or
    $selectionChange.Value -notmatch 'dropdown_highlight_selection_' -or
    $selectionChange.Value -notmatch 'dropdown_highlight_valid_') {
    throw 'native highlight changes are not isolated while the popup is open'
}
$highlightBranch = [regex]::Match($selectionChange.Value, '(?s)if \(dropdown_open_\).*?return;')
if (-not $highlightBranch.Success -or $highlightBranch.Value -match 'SetSearchEditText') {
    throw 'intermediate popup highlights still overwrite the search draft text'
}

$nativeCommit = [regex]::Match($combo, '(?s)bool CMyComboBox::CommitNativeDropDownSelection\(\).*?\n\}')
if (-not $nativeCommit.Success -or
    $nativeCommit.Value -notmatch 'was_auto_filter_popup' -or
    $nativeCommit.Value -notmatch '!was_auto_filter_popup' -or
    $nativeCommit.Value -notmatch 'dropdown_highlight_valid_') {
    throw 'automatic result popups can still commit a native highlight without explicit acceptance'
}

$focus = [regex]::Match($combo, '(?s)void CMyComboBox::OnSearchEditSetFocus\(\).*?\n\}')
if (-not $focus.Success -or
    $focus.Value -notmatch 'select_all_on_next_focus_' -or
    $focus.Value -notmatch 'SetSel\(0, -1\)') {
    throw 'search edit still selects all on every focus notification'
}
$restoreFocus = [regex]::Match($combo, '(?s)void CMyComboBox::RestoreSearchEditFocus\(\).*?\n\}')
if (-not $restoreFocus.Success -or
    $restoreFocus.Value -notmatch 'SetFocus' -or
    $restoreFocus.Value -notmatch 'SetSel\(start, end\)') {
    throw 'automatic filtering does not restore the original caret/selection'
}
$killFocus = [regex]::Match($combo, '(?s)void CMyComboBox::OnSearchEditKillFocus\(\).*?\n\}')
if (-not $killFocus.Success -or
    $killFocus.Value -notmatch 'dropdown_open_|GetDroppedState') {
    throw 'focus transitions into the native popup can still commit draft input'
}

$fallback = [regex]::Matches($combo, 'j\s*>=\s*txt\.GetLength\(\)\s*\|\|\s*txt\[j\]')
if ($fallback.Count -lt 2) {
    throw 'ordinary typeahead fallback still has an unchecked txt[j] access'
}

Write-Output 'indexed_combo_input_lifecycle: PASS'
