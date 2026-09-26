$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$editor = Join-Path $repo 'workspace\references\ccz-SceneEditor'
$oracle = Join-Path $repo 'workspace\ccz_preview_ext\workareas\phase2-native-static-recovery-20260916\build\x86-release\cczPreviewExt.dll'
$phase1 = '929DA194A7732163743D29C0CF570EB1535E566CFEFC716625CFE1331A724FF3'
$phase2 = 'FD27764ABEBAC1E3246AA287FE5245B27442A8441B29583CF60FFF7434E58BA4'

$failures = [System.Collections.Generic.List[string]]::new()
function Require([bool]$condition, [string]$message) {
    if ($condition) {
        Write-Output "[PASS] $message"
    } else {
        $failures.Add($message)
        Write-Output "[FAIL] $message"
    }
}
function Read-Source([string]$relative) {
    return [System.IO.File]::ReadAllText((Join-Path $editor $relative))
}
function Require-Text([string]$relative, [string]$literal, [string]$message) {
    $text = Read-Source $relative
    Require $text.Contains($literal) $message
}

Require (Test-Path -LiteralPath $oracle) 'phase2 DLL oracle exists'
if (Test-Path -LiteralPath $oracle) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $oracle).Hash.ToUpperInvariant()
    Require ($hash -eq $phase2) "phase2 oracle SHA-256 is $phase2"
}

$sourceFiles = Get-ChildItem (Join-Path $editor 'cczPreviewCore'),
    (Join-Path $editor 'cczEditor2') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.h', '.vcxproj' }
$phase1Reference = $sourceFiles | Select-String -SimpleMatch $phase1
Require ($null -eq $phase1Reference) 'internalized source does not use phase1 DLL hash'

Require-Text 'cczPreviewCore\src\dialogue_replay.cpp' 'if (node.logical_key == target_node)' 'selected replay target is checked before stop'
Require-Text 'cczPreviewCore\src\dialogue_replay.cpp' 'data_name_overrides' 'section-local rename map is retained'
Require-Text 'cczPreviewCore\src\dialogue_replay.cpp' 'IntegerDispatchGroup' 'integer dispatch groups retain sequential replay'
Require-Text 'cczPreviewCore\src\dialogue_profile.cpp' 'if (!entry.portrait_required' 'plain narration profile entries are validated without a portrait'
Require-Text 'cczEditor2\PreviewIntegration\PreviewSession.cpp' 'data_name_overrides.find' 'frame resolves the latest section rename'
Require-Text 'cczEditor2\PreviewIntegration\PreviewSession.cpp' 'show_portrait' 'frame carries plain-text portrait visibility'
$renderer = Read-Source 'cczEditor2\PreviewIntegration\DialogueOverlayRenderer.cpp'
Require ($renderer -match 'if\s*\(\s*model\.visible\s*&&\s*model\.show_portrait\s*\)') 'renderer honors plain-text overlay flags'
Require-Text 'cczEditor2\ScenePreviewWnd.cpp' '{0x2c, DialogueKind::Narration, false' 'profile includes map-text narration'
Require-Text 'cczEditor2\ScenePreviewWnd.cpp' '{0x69, DialogueKind::Narration, false' 'profile keeps 0x69 as plain narration'
Require-Text 'cczEditor2\ScenePreviewWnd.cpp' 'm_activationButton.Create(' 'editor exposes the activation operation'
Require-Text 'cczEditor2\ScenePreviewWnd.cpp' 'm_previewSession.ActivateTarget' 'activation routes through the editor-owned session'
Require-Text 'cczEditor2\PreviewIntegration\ConditionTreePane.cpp' 'state.can_restore_choice_auto' 'condition pane keeps choice controls'
Require-Text 'cczEditor2\PreviewIntegration\ConditionTreePane.cpp' 'state.can_restore_predicate' 'condition pane keeps predicate controls'
Require-Text 'cczEditor2\PreviewIntegration\PreviewAssetCache.cpp' 'CczString.ini' 'asset cache loads the editor-package instruction catalog'
Require-Text 'cczEditor2\PreviewIntegration\PreviewAssetCache.cpp' 'catalog_revision_' 'catalog cache invalidates after file revision changes'
Require-Text 'cczEditor2\PreviewIntegration\PreviewAssetCache.cpp' 'GetFileAttributesExW' 'catalog revision uses file metadata'

$paneText = Read-Source 'cczEditor2\PreviewIntegration\ConditionTreePane.cpp'
$hasReachabilityInVisibility = $paneText -match '(?s)bool CConditionTreePane::HasControllableNodes\(\).*?return scan\(m_model->entry_gates\).*?can_make_reachable'
Require (-not $hasReachabilityInVisibility) 'condition-pane visibility does not absorb activation-only capability'

$testExeCandidates = @(
    (Join-Path $editor 'cczPreviewCoreTests\Release\cczPreviewCoreTests.exe'),
    (Join-Path $editor 'Release\cczPreviewCoreTests.exe')
)
$testExe = $testExeCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
Require ($null -ne $testExe) 'core parity test binary exists'

if ($failures.Count -gt 0) {
    Write-Error ("full parity contract failed: " + ($failures -join '; '))
    exit 1
}
Write-Output ('full parity contract passed: ' + $sourceFiles.Count + ' source files scanned')
