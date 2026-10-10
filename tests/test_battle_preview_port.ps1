$ErrorActionPreference = 'Stop'

$editorRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (Resolve-Path (Join-Path $editorRoot '..\..\..')).Path
$manifestPath = Join-Path $editorRoot 'tests\fixtures\battle_preview\manifest.json'
$summaryPath = Join-Path $editorRoot 'tests\fixtures\battle_preview\sample_summaries.json'
$failures = [System.Collections.Generic.List[string]]::new()

function Require([bool]$condition, [string]$message) {
    if ($condition) {
        Write-Output "[PASS] $message"
    } else {
        $failures.Add($message)
        Write-Output "[FAIL] $message"
    }
}

Require (Test-Path -LiteralPath $summaryPath) 'battle sample summaries exist'
if (Test-Path -LiteralPath $summaryPath) {
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    Require ($summary.samples.Count -eq 3) 'three pinned S-script summaries exist'
    foreach ($sample in $summary.samples) {
        $path = Join-Path $repoRoot ($sample.path -replace '/', '\')
        Require (Test-Path -LiteralPath $path) "summary source exists: $($sample.path)"
        if (Test-Path -LiteralPath $path) {
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
            Require ($hash -eq $sample.sha256) "summary source SHA-256 matches: $($sample.path)"
        }
        Require ($sample.battle_scope.scene -eq 0 -and
                 $sample.battle_scope.section -eq 0) "summary scope is explicit: $($sample.path)"
        Require ($sample.records.'0x44'.count -ge 1 -and
                 $sample.records.'0x27'.count -ge 1) "summary has battle boundary/map evidence: $($sample.path)"
    }
}

Require (Test-Path -LiteralPath $manifestPath) 'battle corpus manifest exists'
if (Test-Path -LiteralPath $manifestPath) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($sample in $manifest.samples) {
        $path = Join-Path $repoRoot ($sample.path -replace '/', '\')
        Require (Test-Path -LiteralPath $path) "sample exists: $($sample.path)"
        if (Test-Path -LiteralPath $path) {
            $file = Get-Item -LiteralPath $path
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
            Require ($file.Length -eq [int64]$sample.size) "sample size is stable: $($sample.path)"
            Require ($hash -eq $sample.sha256) "sample SHA-256 is stable: $($sample.path)"
        }
    }
    $release = Join-Path $repoRoot ($manifest.editor_release.path -replace '/', '\')
    Require (Test-Path -LiteralPath $release) 'current Release editor exists'
    if (Test-Path -LiteralPath $release) {
        $releaseHash = (Get-FileHash -LiteralPath $release -Algorithm SHA256).Hash.ToUpperInvariant()
        Require ($releaseHash -eq $manifest.editor_release.sha256) 'current Release editor SHA-256 is stable'
    }
}

# Task 1 contract: the classifier and value-owned battle adapter must be
# present before later parser/asset tasks are allowed to start.
$classifierHeader = Join-Path $editorRoot 'cczPreviewCore\include\ccz\preview\battle_mode_classifier.h'
$classifierSource = Join-Path $editorRoot 'cczPreviewCore\src\battle_mode_classifier.cpp'
$adapterHeader = Join-Path $editorRoot 'cczEditor2\PreviewIntegration\BattlePreviewAdapter.h'
$adapterSource = Join-Path $editorRoot 'cczEditor2\PreviewIntegration\BattlePreviewAdapter.cpp'
$battleHeader = Join-Path $editorRoot 'cczPreviewCore\include\ccz\preview\battle_preview.h'
$parserHeader = Join-Path $editorRoot 'cczPreviewCore\include\ccz\preview\battle_instruction_parser.h'
$parserSource = Join-Path $editorRoot 'cczPreviewCore\src\battle_instruction_parser.cpp'
$fieldSpecHeader = Join-Path $editorRoot 'cczPreviewCore\include\ccz\preview\battle_field_spec.h'
$fieldSpecSource = Join-Path $editorRoot 'cczPreviewCore\src\battle_field_spec.cpp'
Require (Test-Path -LiteralPath $classifierHeader) 'battle mode classifier header exists'
Require (Test-Path -LiteralPath $classifierSource) 'battle mode classifier source exists'
Require (Test-Path -LiteralPath $adapterHeader) 'battle preview adapter header exists'
Require (Test-Path -LiteralPath $adapterSource) 'battle preview adapter source exists'
Require (Test-Path -LiteralPath $battleHeader) 'battle preview state header exists'
Require (Test-Path -LiteralPath $parserHeader) 'battle instruction parser header exists'
Require (Test-Path -LiteralPath $parserSource) 'battle instruction parser source exists'
Require (Test-Path -LiteralPath $fieldSpecHeader) 'battle field specification header exists'
Require (Test-Path -LiteralPath $fieldSpecSource) 'battle field specification source exists'
if ((Test-Path -LiteralPath $fieldSpecSource) -and (Test-Path -LiteralPath $parserSource)) {
    $fieldSpecText = Get-Content -LiteralPath $fieldSpecSource -Raw -Encoding UTF8
    $parserText = Get-Content -LiteralPath $parserSource -Raw -Encoding UTF8
    Require ($fieldSpecText.Contains('{0x46,') -and $fieldSpecText.Contains('{0x47,') -and $fieldSpecText.Contains('{0x4b,')) `
        'battle field specification owns 0x46/0x47/0x4B layouts'
    Require ($parserText.Contains('get_battle_field_spec') -and -not $parserText.Contains('RecordSpec')) `
        'battle parser uses field specification instead of local strides'
    Require (-not $parserText.Contains('Instructions.json') -and -not $fieldSpecText.Contains('Instructions.json')) `
        'battle specification and parser do not rely on Instructions.json'
}

if ($failures.Count -gt 0) {
    Write-Error ('BATTLE_PREVIEW_PORT_FAIL: ' + ($failures -join '; '))
    exit 1
}

'BATTLE_PREVIEW_PORT_PASS'
