$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$comboPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\CMyComboBox.h'
$viewPath = Join-Path $sourceRoot 'cczEditor2\cczEditor2View.cpp'
$combo = Get-Content -Raw -Encoding UTF8 $comboPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath
$view = Get-Content -Raw -Encoding UTF8 $viewPath

if ($combo -notmatch 'ON_CONTROL_REFLECT_EX\(CBN_SELCHANGE, OnSelectionChangeReflect\)') {
    throw 'CBN_SELCHANGE must use the extended reflection handler so ordinary combos can forward the notification'
}

if ($header -notmatch 'BOOL OnSelectionChangeReflect\(\)') {
    throw 'CMyComboBox is missing the BOOL reflection gate'
}

$handler = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionChangeReflect\(\).*?\n\}')
if (-not $handler.Success) {
    throw 'CMyComboBox::OnSelectionChangeReflect implementation is missing'
}
if ($handler.Value -notmatch 'indexed_mode_\s*==\s*IndexedMode::None') {
    throw 'reflection gate does not distinguish ordinary combos from indexed combos'
}
if ($handler.Value -notmatch 'return FALSE') {
    throw 'ordinary combo notifications are not forwarded to the parent'
}
if ($handler.Value -notmatch 'return TRUE') {
    throw 'indexed combo notifications are not consumed after internal handling'
}

if ($view -notmatch 'ON_CBN_SELCHANGE\(IDC_COMBO1, &Dialog_50::OnCbnSelchangeCombo1\)') {
    throw 'Dialog_50 parent selection handler is missing'
}

Write-Output 'combo_notification_routing: PASS'
