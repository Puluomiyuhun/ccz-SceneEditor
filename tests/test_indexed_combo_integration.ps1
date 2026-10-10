$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$viewPath = Join-Path $sourceRoot 'cczEditor2\cczEditor2View.cpp'
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$view = Get-Content -Raw -Encoding UTF8 $viewPath
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

$personEnableCount = ([regex]::Matches($view, '\.EnablePersonSearch\(\)')).Count
$itemEnableCount = ([regex]::Matches($view, '\.EnableItemSearch\(\)')).Count
$segmentEnableCount = ([regex]::Matches($view, '\.EnableItemSegmentSearch\(')).Count
if ($personEnableCount -ne 43) {
    throw "expected 43 person selectors, found $personEnableCount"
}
if ($itemEnableCount -ne 5) {
    throw "expected 5 full item selectors, found $itemEnableCount"
}
if ($segmentEnableCount -ne 3) {
    throw "expected 3 segmented equipment selectors, found $segmentEnableCount"
}

if ($view -notmatch 'combo2\.EnableItemSegmentSearch\(0\)') {
    throw 'weapon segment does not preserve global item origin'
}
if ($view -notmatch 'combo4\.EnableItemSegmentSearch\(pView->set_weapon\)') {
    throw 'armor segment does not preserve global item origin'
}
if ($view -notmatch 'combo6\.EnableItemSegmentSearch\(pView->set_weapon \+ pView->set_armor\)') {
    throw 'assist segment does not preserve global item origin'
}
if ($header -notmatch 'std::vector<ccz::editor::IndexedOption> indexed_options_') {
    throw 'indexed model is missing from CMyComboBox'
}
if ($combo -notmatch 'CreateSearchEdit\(\)') {
    throw 'editable search overlay is not wired'
}
$dialog114 = [regex]::Match($view, '(?s)BOOL Dialog_114::OnInitDialog\(\).*?(?=BEGIN_MESSAGE_MAP\(Dialog_115)')
if ($dialog114.Success -and $dialog114.Value -match 'Enable(Person|Item)Search') {
    throw 'non-target Dialog_114 list selectors were accidentally changed'
}

Write-Output "indexed_combo_integration: PASS (person=$personEnableCount item=$itemEnableCount segment=$segmentEnableCount)"
