$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

if ($combo -notmatch 'ON_CONTROL_REFLECT_EX\(CBN_SELENDOK, OnSelectionEndOkReflect\)') {
    throw 'indexed combos must observe CBN_SELENDOK before closeup commits stale search text'
}
if ($header -notmatch 'bool CommitNativeDropDownSelection\(\)') {
    throw 'native dropdown selection commit helper is missing'
}
if ($header -notmatch 'dropdown_selection_accepted_') {
    throw 'dropdown selection acceptance state is missing'
}
if ($header -notmatch 'dropdown_open_native_row_') {
    throw 'dropdown opening row state is missing'
}

$endOk = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionEndOkReflect\(\).*?\n\}')
if (-not $endOk.Success -or $endOk.Value -notmatch 'OnSelectionChange\(\)') {
    throw 'CBN_SELENDOK does not canonicalize the selected native row'
}

$commit = [regex]::Match($combo, '(?s)bool CMyComboBox::CommitIndexedInput\(\).*?\n\}')
if (-not $commit.Success -or $commit.Value -notmatch 'ResolveFirstMatchingOption') {
    throw 'multi-candidate Enter submission does not use the first visible match'
}

$close = [regex]::Match($combo, '(?s)void CMyComboBox::OnCloseUp\(\).*?\n\}')
if (-not $close.Success) { throw 'OnCloseUp implementation is missing' }
$nativeCall = $close.Value.IndexOf('CommitNativeDropDownSelection()')
$textCall = $close.Value.IndexOf('CommitIndexedInput()')
if ($nativeCall -lt 0 -or $textCall -lt 0 -or $nativeCall -gt $textCall) {
    throw 'OnCloseUp must prefer an accepted native row before parsing search text'
}

$dropDown = [regex]::Match($combo, '(?s)void CMyComboBox::OnDropDown\(\).*?\n\}')
if (-not $dropDown.Success -or
    $dropDown.Value -notmatch 'dropdown_open_native_row_' -or
    $dropDown.Value -notmatch 'dropdown_selection_accepted_') {
    throw 'OnDropDown does not capture the pre-open row and reset acceptance state'
}

Write-Output 'indexed_combo_selection_commit: PASS'
