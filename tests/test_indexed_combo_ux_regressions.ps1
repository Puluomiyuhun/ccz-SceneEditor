$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

# The complete model remains in indexed_options_.  The native popup is a
# filtered view while the user is typing; a manual arrow click restores the
# complete legacy list.  This is the current searchable-combo contract.
$rebuild = [regex]::Match($combo, '(?s)void CMyComboBox::RebuildVisibleOptions\(.*?\n\}')
if (-not $rebuild.Success) { throw 'RebuildVisibleOptions implementation is missing' }
if ($rebuild.Value -notmatch 'FilterIndexedOptions\(indexed_options_, query_text\)') {
    throw 'typing does not filter the indexed option model'
}
if ($rebuild.Value -notmatch 'CComboBox::ResetContent' -or
    $rebuild.Value -notmatch 'CComboBox::AddString' -or
    $rebuild.Value -notmatch 'visible_option_indexes_') {
    throw 'filtered candidates are not rebuilt into the native popup list'
}
if ($rebuild.Value -notmatch 'CComboBox::SetCurSel\(-1\)') {
    throw 'typing does not clear the native row while preserving the unfinished query'
}
if ($rebuild.Value -match 'SetCanonicalSelection|NotifySelectionChange|RestoreLastSelectionText') {
    throw 'typing commits or restores a selection instead of preserving the unfinished query'
}
if ($rebuild.Value -notmatch 'ShowDropDown\(TRUE\)' -or
    $rebuild.Value -notmatch '!matches\.empty\(\)') {
    throw 'a matching result is not shown automatically while preserving the query'
}
if ($rebuild.Value -notmatch 'auto_filter_dropdown_') {
    throw 'automatic popup state is not separated from a manual dropdown click'
}

$dropDownStart = $combo.IndexOf('void CMyComboBox::OnDropDown(')
$dropDownEnd = $combo.IndexOf('bool CMyComboBox::CommitNativeDropDownSelection(', $dropDownStart)
$dropdown = if ($dropDownStart -ge 0 -and $dropDownEnd -gt $dropDownStart) {
    $combo.Substring($dropDownStart, $dropDownEnd - $dropDownStart)
} else {
    ''
}
if ([string]::IsNullOrEmpty($dropdown) -or
    $dropdown -notmatch 'keep_filtered_results\s*=\s*auto_filter_dropdown_' -or
    $dropdown -notmatch 'if \(!keep_filtered_results\) RestoreFullOptionList' -or
    $dropdown -notmatch 'BringSearchMatchIntoView') {
    throw 'manual dropdown does not restore the complete list while automatic popup keeps filtered rows'
}

$restore = [regex]::Match($combo, '(?s)void CMyComboBox::RestoreFullOptionList\(.*?\n\}')
if (-not $restore.Success -or
    $restore.Value -notmatch 'CComboBox::ResetContent' -or
    $restore.Value -notmatch 'CComboBox::AddString' -or
    $restore.Value -notmatch 'visible_option_indexes_') {
    throw 'full-list restoration does not rebuild all native rows and model indexes'
}

if ($combo -notmatch 'CComboBox::SetTopIndex') {
    throw 'search results are not brought into view without changing the selection'
}

$configure = [regex]::Match($combo, '(?s)void CMyComboBox::ConfigureIndexedMode\(.*?\n\}')
if (-not $configure.Success -or
    $configure.Value -notmatch 'mode == IndexedMode::Person' -or
    $configure.Value -notmatch 'ExtractLeadingDecimalCode\(option\.label, code\).*?code >= 0 && code <= 1023' -or
    $configure.Value -notmatch 'mode == IndexedMode::Item' -or
    $configure.Value -notmatch 'IsNoItemLabel\(option\.label\)' -or
    $configure.Value -notmatch 'option\.numeric_code = option\.selection_value' -or
    $configure.Value -notmatch 'option\.numeric_searchable = false') {
    throw 'person/item numeric search contract (including the item-only 255 exception) is missing'
}

$setText = [regex]::Match($combo, '(?s)void CMyComboBox::SetSearchEditText\(.*?\n\}')
if (-not $setText.Success -or $setText.Value -notmatch 'GetSel' -or $setText.Value -notmatch 'SetSel') {
    throw 'search edit text updates do not preserve the caret/selection'
}

if ($header -notmatch 'ForwardMouseWheel') {
    throw 'search edit has no mouse-wheel forwarding path'
}

Write-Output 'indexed_combo_ux_regressions: PASS'
