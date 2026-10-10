$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# Windows PowerShell 5.1 reads a UTF-8 source file without BOM through the
# active ANSI code page when Get-Content is used without an explicit decoder.
# Read the source bytes explicitly so the same contract is checked by 5.1
# and PowerShell 7.
function Read-Utf8Text([string]$Path) {
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    return [IO.File]::ReadAllText($Path, $utf8)
}

$source = Read-Utf8Text (Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp')
$header = Read-Utf8Text (Join-Path $root 'cczEditor2\ScenePreviewWnd.h')

# Keep the test file ASCII-only so Windows PowerShell 5.1 can parse it even
# when the repository is on a legacy Chinese code page.
$playFromStart = -join ([char[]](0x4ECE, 0x5F00, 0x5934, 0x64AD, 0x653E, 0x5230,
    0x5F53, 0x524D, 0x9009, 0x5B9A, 0x8282, 0x70B9))
$playToEnd = -join ([char[]](0x4ECE, 0x5F53, 0x524D, 0x9009, 0x5B9A, 0x8282,
    0x70B9, 0x64AD, 0x653E, 0x5230, 0x7ED3, 0x5C3E))

$required = @(
    'kPlaybackStartTargetButtonId',
    'kPlaybackTargetEndButtonId',
    'kPlaybackPlayButtonId',
    'kPlaybackStopButtonId',
    'kPlaybackStepBackButtonId',
    'kPlaybackStepForwardButtonId',
    'kPlaybackSpeedButtonId',
    'm_playbackToolTip',
    'ON_WM_TIMER()',
    'ON_UPDATE_COMMAND_UI(kPlaybackStartTargetButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackTargetEndButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackResetButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackPlayButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackStopButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackStepBackButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackStepForwardButtonId',
    'ON_UPDATE_COMMAND_UI(kPlaybackSpeedButtonId',
    $playFromStart,
    $playToEnd,
    'm_playbackProgress'
)

$missing = @($required | Where-Object {
    $source.IndexOf($_, [System.StringComparison]::Ordinal) -lt 0 -and
    $header.IndexOf($_, [System.StringComparison]::Ordinal) -lt 0
})
if ($missing.Count -gt 0) {
    throw ('dynamic preview control contract missing: ' + ($missing -join ', '))
}

if ($source -notmatch 'createPlaybackButton\(m_playbackStartTargetButton') {
    throw 'start-to-target icon button is not created'
}
if ($source -notmatch 'createPlaybackButton\(m_playbackTargetEndButton') {
    throw 'target-to-end icon button is not created'
}
if ($source -notmatch 'createPlaybackButton\(m_playbackSpeedButton, L"1x"') {
    throw 'playback speed button does not expose the default 1x label'
}
if ($source -notmatch 'm_playbackSpeedButton\.SetWindowTextW\(speed_text\)') {
    throw 'playback speed button does not refresh its multiplier label'
}
if ($source -notmatch 'void CRScenePreviewPage::OnUpdatePlaybackButton\(CCmdUI\* pCmdUI\)') {
    throw 'playback buttons do not have a shared idle-update handler'
}
if ($source -notmatch 'pCmdUI->Enable\(m_playbackVisible') {
    throw 'playback idle-update handler does not mirror the visible model contract'
}
if ($header -notmatch 'm_dynamicStaticProgressIndex') {
    throw 'static preview progress cursor is not declared'
}
if ($source -notmatch 'RefreshDynamicTimelineForCurrentTarget') {
    throw 'static preview does not prepare a persistent progress timeline'
}
if ($source -notmatch '(?s)void CRScenePreviewPage::OnPlaybackStepForwardClicked\(\).*?PlaybackRange::TargetToEnd') {
    throw 'step-forward is not anchored to target-to-end playback'
}
if ($source -notmatch '(?s)void CRScenePreviewPage::OnPlaybackStepBackClicked\(\).*?PlaybackRange::StartToTarget') {
    throw 'step-back is not anchored to start-to-target playback'
}
if ($source -notmatch '(?s)if \(m_dynamicPlaybackRange == ccz::preview::PlaybackRange::StartToTarget\).*?m_dynamicStaticProgressIndex = 0;') {
    throw 'target-to-end static progress is not anchored at the replay origin'
}
if ($source -notmatch 'm_dynamicTimeline\.target_end_index') {
    throw 'start-to-target static progress does not include all embedded dialogue segments'
}

Write-Output 'dynamic preview control contract: PASS'
