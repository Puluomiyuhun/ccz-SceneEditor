$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$viewPath = Join-Path $sourceRoot 'cczEditor2\cczEditor2View.cpp'
$headerPath = Join-Path $sourceRoot 'cczEditor2\cczEditor2View.h'
$view = Get-Content -Raw -Encoding UTF8 $viewPath
$header = Get-Content -Raw -Encoding UTF8 $headerPath

$modify = [regex]::Match($view, '(?s)void CcczEditor2View::OnEditModify\(\).*?(?=void CcczEditor2View::OnEditAdd\()')
if (-not $modify.Success -or
    $modify.Value -notmatch 'Dialog_39\s+d\s*;\s*d\.DoModal\(\)' -or
    $modify.Value -notmatch 'RefreshScenePreview\s*\(\)') {
    throw 'editing 0x27 does not route through the current preview refresh coordinator'
}

if ($header -notmatch 'NormalizeItemSegmentCounts\(\)') {
    throw 'item segment count normalization helper is missing'
}
$defaults = [regex]::Match($view, '(?s)void CcczEditor2View::ReadDataDefault\(\).*?\n\}')
if (-not $defaults.Success -or $defaults.Value -notmatch 'NormalizeItemSegmentCounts\(\)') {
    throw 'item segment count normalization is not applied after defaults/configuration'
}
$normalize = [regex]::Match($view, '(?s)void CcczEditor2View::NormalizeItemSegmentCounts\(\).*?\n\}')
if (-not $normalize.Success -or
    $normalize.Value -notmatch '512' -or
    $normalize.Value -notmatch 'set_weapon' -or
    $normalize.Value -notmatch 'set_armor' -or
    $normalize.Value -notmatch 'set_product') {
    throw 'item segment normalization does not clamp all three segments to the 512-slot item table'
}

Write-Output 'remaining_scene_config_fixes: PASS'
