$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

$preTranslate = [regex]::Match($combo, '(?s)BOOL CMyComboBox::PreTranslateMessage\(MSG\* pMsg\).*?\n\}')
if (-not $preTranslate.Success -or
    $preTranslate.Value -notmatch 'VK_ESCAPE' -or
    $preTranslate.Value -notmatch 'RestoreLastSelectionText' -or
    $preTranslate.Value -notmatch 'RestoreFullOptionList') {
    throw 'indexed combo Esc handling does not restore the last committed value when no native popup cancellation is emitted'
}

if ($combo -notmatch 'ON_CONTROL_REFLECT_EX\(CBN_SELENDCANCEL, OnSelectionEndCancelReflect\)') {
    throw 'indexed combos must observe CBN_SELENDCANCEL so Esc never commits a highlighted row'
}
if ($header -notmatch 'BOOL OnSelectionEndCancelReflect\(\)') {
    throw 'CMyComboBox is missing the native dropdown cancel reflection handler'
}
if ($header -notmatch 'dropdown_open_selection_') {
    throw 'dropdown opening selection snapshot is missing'
}
$cancel = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionEndCancelReflect\(\).*?\n\}')
if (-not $cancel.Success -or
    $cancel.Value -notmatch 'SetCanonicalSelection|RestoreLastSelectionText' -or
    $cancel.Value -notmatch 'dropdown_open_') {
    throw 'Esc cancellation does not restore the pre-open selection without committing it'
}
if ($cancel.Value -match 'NotifySelectionChange') {
    throw 'Esc cancellation must not notify the parent as if a new selection was committed'
}

$selectionChange = [regex]::Match($combo, '(?s)void CMyComboBox::OnSelectionChange\(\).*?\n\}')
if (-not $selectionChange.Success -or $selectionChange.Value -match 'last_notified_selection_\s*=') {
    throw 'intermediate native highlight changes must not mark the parent notification as delivered'
}
$endOk = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionEndOkReflect\(\).*?\n\}')
if (-not $endOk.Success -or $endOk.Value -notmatch 'NotifySelectionChange') {
    throw 'accepted native selections do not notify the parent dialog'
}
$notify = [regex]::Match($combo, '(?s)void CMyComboBox::NotifySelectionChange\(\).*?\n\}')
if (-not $notify.Success -or $notify.Value -notmatch 'last_notified_selection_') {
    throw 'parent notification lacks duplicate-selection suppression state'
}

if ($header -notmatch 'void AppendTypeaheadChar\(wchar_t') {
    throw 'bounded typeahead append helper is missing'
}
$preTranslate = [regex]::Match($combo, '(?s)BOOL CMyComboBox::PreTranslateMessage\(MSG\* pMsg\).*?\n\}')
if (-not $preTranslate.Success) { throw 'CMyComboBox::PreTranslateMessage implementation is missing' }
if ($preTranslate.Value -match 'inputs\[cnt\+\+\]\s*=') {
    throw 'typeahead still writes inputs[cnt++] without a capacity guard'
}
$append = [regex]::Match($combo, '(?s)void CMyComboBox::AppendTypeaheadChar\(wchar_t.*?\n\}')
if (-not $append.Success -or
    $append.Value -notmatch 'cnt\s*>=.*_countof\(inputs\)' -or
    $append.Value -notmatch 'cnt\s*=\s*0') {
    throw 'typeahead append helper does not reset before overflowing its fixed buffer'
}

$indexedSearchPath = Join-Path $sourceRoot 'cczEditor2\IndexedComboSearch.h'
$indexedSearch = Get-Content -Raw -Encoding UTF8 $indexedSearchPath
if ($indexedSearch -notmatch 'numeric_resolvable') {
    throw 'indexed numeric options do not distinguish searchable entries from directly resolvable entries'
}
if ($indexedSearch -notmatch 'numeric_resolvable\s*\)') {
    throw 'ResolveDecimalCode does not honor the numeric resolvability flag'
}
if ($combo -notmatch 'mode == IndexedMode::ItemSegment' -or
    $combo -notmatch 'numeric_resolvable\s*=\s*false') {
    throw 'item-segment reserved item numbers are not excluded from numeric submission'
}

Write-Output 'remaining_combo_review_fixes: PASS'
