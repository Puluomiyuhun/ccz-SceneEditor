$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$header = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$page = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw "BATTLE_PREVIEW_PERF_FAIL: $message" }
    Write-Output "[PASS] $message"
}

function Get-Block([string]$text, [string]$start, [string]$end) {
    $startAt = $text.IndexOf($start)
    if ($startAt -lt 0) { return '' }
    $endAt = $text.IndexOf($end, $startAt + $start.Length)
    if ($endAt -lt 0) { return $text.Substring($startAt) }
    return $text.Substring($startAt, $endAt - $startAt)
}

Assert-Text $header '#if defined\(_DEBUG\) && defined\(CCZ_S_BATTLE_PREVIEW_PERF\)[\s\S]*BattlePreviewPerfCounters' `
    'performance counters are compiled only into explicitly instrumented Debug builds'
foreach ($counter in @(
    'full_document_captures', 'pinned_section_captures', 'battle_input_builds',
    'battle_loads', 'map_decodes', 'sprite_decodes', 'elapsed_ms'
)) {
    Assert-Text $header $counter "counter exists: $counter"
}

$refresh = Get-Block $page 'bool CSBattlePreviewPage::RequestPreviewRefresh(' 'bool CSBattlePreviewPage::RequestQuickModeRefresh('
$quickRefresh = Get-Block $page 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'bool CSBattlePreviewPage::SetQuickModeEnabled('
$loadBlock = Get-Block $page 'void CSBattlePreviewPage::LoadCapturedBattle(' 'bool CSBattlePreviewPage::ActivateConditionScope('
$mapBlock = Get-Block $page 'bool CSBattlePreviewPage::LoadMapImage()' 'void CSBattlePreviewPage::AppendDiagnostic('
$spriteBlock = Get-Block $page 'Gdiplus::Bitmap* CSBattlePreviewPage::GetUnitSprite(' 'void CSBattlePreviewPage::ApplyMagentaTransparency('
$paintBlock = Get-Block $page 'void CSBattlePreviewPage::OnPaint()' 'BOOL CSBattlePreviewPage::OnEraseBkgnd('

Assert-Text $page 'SPreviewPerf' 'QA copy activation records have a stable searchable prefix'
Assert-Text $refresh 'PerformanceActivationScope perfActivation\(\*this, documentPath\)' `
    'S refresh starts per-activation measurement'
Assert-Text $paintBlock 'DrawCanvas\(graphics, client\.Width\(\), client\.Height\(\)\);[\s\S]*FinishPerformanceActivation\(' `
    'the activation record includes the first paint and lazy unit-sprite decoding'
Assert-Text $quickRefresh 'RecordFullDocumentCapture\(\)[\s\S]*CaptureEditorDocumentSnapshot\(' `
    'full document snapshots increment their own counter'
Assert-Text $quickRefresh 'RecordPinnedSectionCapture\(\)[\s\S]*CaptureEditorPinnedSectionSnapshot\(' `
    'fixed Scene1 snapshots increment their own counter'
Assert-Text $refresh 'RecordBattleInputBuild\(\)[\s\S]*BuildBattlePreviewInput\(' `
    'Battle input builds increment their own counter'
Assert-Text $loadBlock 'RecordBattleLoad\(\)' `
    'Battle preview load/replay calls increment their own counter'
Assert-Text $mapBlock 'RecordMapDecode\(\)[\s\S]*Gdiplus::Bitmap::FromFile\(' `
    'map JPG decodes increment only on an actual decoder call'
Assert-Text $spriteBlock 'RecordSpriteDecode\(\)[\s\S]*LoadBitmapFromE5\(' `
    'unit sprite decodes increment only on a cache miss'

Write-Output 'BATTLE_PREVIEW_PERF_PASS'
