param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "DYNAMIC_PREVIEW_REGRESSION_FAIL: $Message"
    }
}

function Assert-Literal([string]$RelativePath, [string]$Text, [string]$Message) {
    $path = Join-Path $root $RelativePath
    Assert-True (Test-Path -LiteralPath $path) "missing source: $RelativePath"
    $contents = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    Assert-True $contents.Contains($Text) $Message
}

function Assert-Regex([string]$RelativePath, [string]$Pattern, [string]$Message) {
    $path = Join-Path $root $RelativePath
    Assert-True (Test-Path -LiteralPath $path) "missing source: $RelativePath"
    $contents = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    Assert-True ($contents -match $Pattern) $Message
}

# The focused executable tests are the fixture-level gate.  They cover an
# ordinary Section, Choice Case1/Case2 reachability, inactive targets,
# dialogue/instant events, 0x32 logical movement, and 0x2A pixel movement.
$focused = Join-Path $PSScriptRoot 'test_dynamic_preview.ps1'
Assert-True (Test-Path -LiteralPath $focused) 'focused dynamic-preview test is missing'
& $focused -RepositoryRoot $root
Assert-True ($LASTEXITCODE -eq 0) 'focused dynamic-preview tests failed'

# Free-R and deployment are resolved as bounded Scene pairs.  The assertions
# below deliberately check the resolver contract rather than inventing a new
# playback path for those formats.
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.h' `
    'enum class ScenePreviewKind' 'Scene-pair kind enum is missing'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.h' `
    'Deployment' 'deployment Scene pair kind is missing'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.h' `
    'FreeR' 'free-R Scene pair kind is missing'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.cpp' `
    'last_section_has_header_instruction' 'deployment last-section boundary check is missing'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.cpp' `
    'appearance_section_handle' 'free-R appearance Section identity is missing'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'ResolveScenePreview(' 'Scene-pair resolver is not connected to the preview window'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRReplayComposer.h' `
    'ComposeFreeRReplayPlan' 'Free-R replay composition is not isolated behind a strategy helper'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRReplayComposer.cpp' `
    'instruction_id == 0x27' 'Free-R composition does not reset at the last trigger background'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'ComposeFreeRReplayPlan(' 'Free-R strategy is not wired into native replay'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'BuildFreeRTriggerSectionPrefixPlans' `
    'Free-R prior trigger Sections are not replayed before the selected Section'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.cpp' `
    'find_click_target_in_trigger_scene' `
    'Free-R click pairing is still bounded to the selected Section'
Assert-Literal 'cczEditor2\PreviewIntegration\FreeRPreviewResolver.cpp' `
    'collect_matching_appearances_before_selected' `
    'Free-R branch-local setup Section appearance lookup is missing'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'appearance_scene_handle ==' `
    'Free-R branch-local setup Section replay de-duplication is missing'
Assert-Literal 'cczEditor2\PreviewIntegration\EditorPreviewAdapter.h' `
    'BuildEditorPreviewInputForSectionToEnd' `
    'Free-R cannot build a complete prior-Section model'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'ProcessSceneAll(tree, initialScene)' `
    'Free-R initial snapshot still stops at the appearance instruction'
$scenePreviewText = Get-Content -LiteralPath (Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp') -Raw -Encoding UTF8
$processStart = $scenePreviewText.IndexOf('void CRScenePreviewPage::ProcessInstruction(')
$processEnd = $scenePreviewText.IndexOf('void CRScenePreviewPage::ProcessSceneNode(', $processStart)
Assert-True ($processStart -ge 0 -and $processEnd -gt $processStart) 'ProcessInstruction block is missing'
$processBlock = $scenePreviewText.Substring($processStart, $processEnd - $processStart)
Assert-True (-not $processBlock.Contains('m_limitedFreeRReplay')) 'Free-R still suppresses actor movement/turn/action commands'

# The two playback ranges, pre-target restore, active-branch routing, and
# temporary (non-writeback) interpolation are source-level invariants.
Assert-Literal 'cczPreviewCore\include\ccz\preview\playback_timeline.h' `
    'StartToTarget' 'start-to-target playback range is missing'
Assert-Literal 'cczPreviewCore\include\ccz\preview\playback_timeline.h' `
    'TargetToEnd' 'target-to-end playback range is missing'
Assert-Literal 'cczPreviewCore\include\ccz\preview\playback_timeline.h' `
    'pre_target_key' 'target-to-end pre-target restore key is missing'
Assert-Literal 'cczPreviewCore\src\playback_timeline.cpp' `
    'selected_case_by_choice' 'Choice selection is not reused by the timeline'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'DynamicPaintState()' 'temporary dynamic paint state is missing'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'PositionWritebackSource' 'position writeback source is not represented'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'OnTimer\s*\([^)]*\)[\s\S]*kDynamicPlaybackTimerId' `
    'dynamic playback timer is not wired'

# Stop restores the static preview by passing the pane's cached path back into
# UpdatePreview.  UpdatePreview copies that argument into m_modPath before
# loading assets; the asset calls must therefore use the owned CString buffer,
# not the original pointer (which can alias the old m_modPath buffer and become
# dangling after assignment).
$scenePreviewPath = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$scenePreviewSource = Get-Content -LiteralPath $scenePreviewPath -Raw -Encoding UTF8
$updateStart = $scenePreviewSource.IndexOf('void CRScenePreviewPage::UpdatePreview(')
# Use the ASCII function boundary instead of a localized section comment.  The
# latter is sensitive to the host PowerShell code page under Windows 5.1.
$renderMarker = $scenePreviewSource.IndexOf('void CRScenePreviewPage::OnPaint()', $updateStart)
Assert-True ($updateStart -ge 0 -and $renderMarker -gt $updateStart) `
    'UpdatePreview block is missing for stop-path safety check'
$updateBlock = $scenePreviewSource.Substring($updateStart, $renderMarker - $updateStart)
$ownedPathAssignment = $updateBlock.IndexOf('m_modPath = newModPath;')
Assert-True ($ownedPathAssignment -ge 0) `
    'UpdatePreview no longer records the owned document path'
$assetTail = $updateBlock.Substring($ownedPathAssignment)
Assert-True ($assetTail.Contains('m_modPath.GetString()')) `
    'asset reload after m_modPath assignment still uses the possibly dangling modPath pointer'
Assert-True (-not ($assetTail -match 'LoadMapImage\([^;]*,\s*modPath\s*\)')) `
    'map reload after m_modPath assignment still passes the original pointer'

# Every playback cursor transition must expose its current event in the editor
# tree.  The event already carries the original HTREEITEM handle; the UI bridge
# must select it without triggering a refresh that would destroy a live session.
Assert-Literal 'cczEditor2\ScenePreviewWnd.h' `
    'CurrentPlaybackTreeItem() const' `
    'playback cursor cannot be resolved back to an editor tree item'
Assert-Literal 'cczEditor2\ScenePreviewWnd.h' `
    'SyncPlaybackTreeSelection(bool suppress_preview_refresh)' `
    'playback tree-selection synchronizer is missing'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'bool CRScenePreviewPage::SyncPlaybackTreeSelection(bool suppress_preview_refresh)' `
    'playback tree-selection synchronizer is not implemented'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'CurrentPlaybackTreeItem[\s\S]*DynamicPlaybackState::Stopped' `
    'static preview cursor can be mistaken for an active playback cursor'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'OnPlaybackStopClicked\s*\([^)]*\)[\s\S]*SyncPlaybackTreeSelection\(true\)[\s\S]*StopDynamicPlayback\(false\)' `
    'stop does not capture/select the playback point before clearing the session'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'state == DynamicPlaybackState::Playing[\s\S]*m_dynamicPlaybackSession\.Pause\(\)[\s\S]*SyncPlaybackTreeSelection\(true\)' `
    'pause does not synchronize the current event to the editor tree'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'm_dynamicPlaybackPrepared = true;[\s\S]*SyncPlaybackTreeSelection\(true\)' `
    'prepared playback events do not synchronize step/start selection'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'tick\.completed_events\.empty\(\)[\s\S]*SyncPlaybackTreeSelection\(true\)' `
    'automatic playback does not synchronize the tree after an event transition'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'OnPlaybackStepForwardClicked[\s\S]*SyncPlaybackTreeSelection\(true\)' `
    'step-forward does not keep the editor tree aligned with the preview cursor'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'OnPlaybackStepBackClicked[\s\S]*SyncPlaybackTreeSelection\(true\)' `
    'step-back does not keep the editor tree aligned with the preview cursor'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'VK_ESCAPE[\s\S]*OnPlaybackStopClicked\(\)' `
    'escape-stop bypasses playback tree synchronization'

# Step-back must move the existing session cursor one event and restore the
# previous event's state directly.  Restarting the selected range here causes
# a visible replay from the Section origin.
Assert-Literal 'cczEditor2\ScenePreviewWnd.h' `
    'bool RewindDynamicPlaybackOneEvent()' `
    'direct one-event rewind helper is missing'
Assert-Literal 'cczEditor2\ScenePreviewWnd.cpp' `
    'bool CRScenePreviewPage::RewindDynamicPlaybackOneEvent()' `
    'direct one-event rewind helper is not implemented'
$stepBackStart = $scenePreviewSource.IndexOf('void CRScenePreviewPage::OnPlaybackStepBackClicked(')
$stepBackEnd = $scenePreviewSource.IndexOf('void CRScenePreviewPage::OnPlaybackSpeedClicked(', $stepBackStart)
Assert-True ($stepBackStart -ge 0 -and $stepBackEnd -gt $stepBackStart) `
    'step-back handler block is missing'
$stepBackBlock = $scenePreviewSource.Substring($stepBackStart, $stepBackEnd - $stepBackStart)
Assert-True (-not $stepBackBlock.Contains('StartDynamicPlayback(m_dynamicPlaybackRange)')) `
    'step-back still restarts the active timeline from its origin'
Assert-Regex 'cczEditor2\ScenePreviewWnd.cpp' `
    'RewindDynamicPlaybackOneEvent[\s\S]*StepBackward\(\)' `
    'step-back does not use the session one-event cursor transition'
Assert-Literal 'cczEditor2\cczEditor2View.h' `
    'SelectPlaybackItem(HTREEITEM item, bool suppress_preview_refresh)' `
    'editor tree playback-selection bridge is missing'
Assert-Literal 'cczEditor2\cczEditor2View.h' `
    'm_suppressPreviewRefreshOnce' `
    'one-shot preview-refresh suppression state is missing'
Assert-Regex 'cczEditor2\cczEditor2View.cpp' `
    'm_suppressPreviewRefreshOnce[\s\S]*RefreshScenePreview\(\)' `
    'tree selection change cannot suppress the refresh caused by playback sync'

# The existing refresh and UI contracts remain part of this gate.
foreach ($scriptName in @(
        'test_preview_refresh_routing.ps1',
        'test_preview_catalog_and_controls.ps1',
        'test_preview_core_boundaries.ps1',
        'test_preview_session.ps1',
        'test_native_preview_plan_integration.ps1')) {
    $scriptPath = Join-Path $PSScriptRoot $scriptName
    Assert-True (Test-Path -LiteralPath $scriptPath) "missing regression script: $scriptName"
    & $scriptPath
    Assert-True ($LASTEXITCODE -eq 0) "regression script failed: $scriptName"
}

Write-Output 'DYNAMIC_PREVIEW_REGRESSION_PASS'
