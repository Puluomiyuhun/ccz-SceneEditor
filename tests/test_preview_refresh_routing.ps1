param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "PREVIEW_REFRESH_ROUTING_FAIL: $Message"
    }
}

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    Assert-True ($Text -match $Pattern) $Message
}

function Assert-NotContains([string]$Text, [string]$Pattern, [string]$Message) {
    Assert-True ($Text -notmatch $Pattern) $Message
}

function Get-FunctionBlock([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [System.StringComparison]::Ordinal)
    Assert-True ($start -ge 0) "missing function: $Signature"
    $open = $Text.IndexOf('{', $start)
    Assert-True ($open -gt $start) "missing function body: $Signature"

    $depth = 0
    for ($i = $open; $i -lt $Text.Length; $i++) {
        if ($Text[$i] -eq '{') {
            $depth++
        } elseif ($Text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) {
                return $Text.Substring($start, $i - $start + 1)
            }
        }
    }
    throw "PREVIEW_REFRESH_ROUTING_FAIL: unbalanced function: $Signature"
}

$viewHeaderPath = Join-Path $SourceRoot 'cczEditor2View.h'
$viewSourcePath = Join-Path $SourceRoot 'cczEditor2View.cpp'
$previewHeaderPath = Join-Path $SourceRoot 'ScenePreviewWnd.h'
$previewSourcePath = Join-Path $SourceRoot 'ScenePreviewWnd.cpp'
$stateMachineHeaderPath = Join-Path $SourceRoot 'PreviewIntegration\PreviewRefreshStateMachine.h'
$decisionHeaderPath = Join-Path $SourceRoot 'PreviewIntegration\PreviewRefreshDecision.h'

foreach ($path in @($viewHeaderPath, $viewSourcePath, $previewHeaderPath, $previewSourcePath, $stateMachineHeaderPath, $decisionHeaderPath)) {
    Assert-True (Test-Path -LiteralPath $path) "missing source file: $path"
}

$viewHeader = Get-Content -LiteralPath $viewHeaderPath -Raw -Encoding UTF8
$view = Get-Content -LiteralPath $viewSourcePath -Raw -Encoding UTF8
$previewHeader = Get-Content -LiteralPath $previewHeaderPath -Raw -Encoding UTF8
$preview = Get-Content -LiteralPath $previewSourcePath -Raw -Encoding UTF8
$stateMachine = Get-Content -LiteralPath $stateMachineHeaderPath -Raw -Encoding UTF8
$decision = Get-Content -LiteralPath $decisionHeaderPath -Raw -Encoding UTF8

Assert-Contains $viewHeader 'void\s+RefreshScenePreview\s*\(\s*\)' `
    'CcczEditor2View does not declare RefreshScenePreview'
Assert-Contains $view 'void\s+CcczEditor2View::RefreshScenePreview\s*\(\s*\)' `
    'CcczEditor2View does not define RefreshScenePreview'
Assert-Contains $previewHeader 'RequestPreviewRefresh\s*\(' `
    'CScenePreviewWnd does not declare RequestPreviewRefresh'
Assert-Contains $preview 'void\s+CRScenePreviewPage::RequestPreviewRefresh\s*\(' `
    'CScenePreviewWnd does not define RequestPreviewRefresh'

$selectionBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnTvnSelchanged('
$editBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnEditModify('
$refreshBlock = Get-FunctionBlock $view 'void CcczEditor2View::RefreshScenePreview('
$createRootBlock = Get-FunctionBlock $view 'void CcczEditor2View::CreateRoot('
$viewDestroyBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnDestroy('

Assert-Contains $selectionBlock 'RefreshScenePreview\s*\(\s*\)' `
    'OnTvnSelchanged does not route through RefreshScenePreview'
Assert-Contains $editBlock 'RefreshScenePreview\s*\(\s*\)' `
    'instruction-edit path does not route through RefreshScenePreview'
Assert-Contains $selectionBlock 'if\s*\(\s*data\s*\)' `
    'selection handler no longer preserves the ItemData status-bar branch'
Assert-True ($selectionBlock.LastIndexOf('RefreshScenePreview') -gt $selectionBlock.LastIndexOf('SetPaneText')) `
    'root/document selection does not refresh after the optional ItemData branch'
Assert-NotContains $selectionBlock 'UpdatePreview\s*\(' `
    'selection handler bypasses the refresh coordinator'
Assert-NotContains $editBlock 'UpdatePreview\s*\(' `
    'instruction-edit handler bypasses the refresh coordinator'
Assert-NotContains $view 'EvaluateToItem\s*\(' `
    'CcczEditor2View directly calls EvaluateToItem'
Assert-Contains $refreshBlock 'RequestPreviewRefresh\s*\(' `
    'RefreshScenePreview does not own the single preview request call'
Assert-Contains $createRootBlock 'InvalidatePendingPreviewRefresh\s*\(' `
    'tree rebuild does not invalidate delayed preview requests'
Assert-Contains $viewDestroyBlock 'InvalidatePendingPreviewRefresh\s*\(' `
    'view destruction does not invalidate delayed preview requests'

$requestCount = ([regex]::Matches($view, 'm_wndScenePreview\.RequestPreviewRefresh\s*\(')).Count
Assert-True ($requestCount -eq 1) `
    "preview refresh request is not centralized (found $requestCount View call sites)"

Assert-Contains $preview 'PostMessage\s*\(' `
    'reentrant refresh does not post a deferred request'
Assert-Contains $preview 'm_previewRefreshState\.Submit|m_previewRefreshState\.OnPostedMessage' `
    'UI refresh path does not use the extracted coalescing state machine'
Assert-Contains $preview 'm_previewRefreshState\.Invalidate' `
    'UI refresh path does not invalidate the extracted state machine'
Assert-Contains $preview 'CaptureEditorPreviewInput' `
    'refresh request does not capture the editor input'
Assert-Contains $preview 'm_previewSession\.Refresh\s*\(' `
    'refresh request does not use the shared PreviewSession'
Assert-Contains $preview 'ResetForDocument\s*\(' `
    'idle/capture failure does not clear the shared PreviewSession frame'
Assert-Contains $preview 'DecidePreviewRefreshAction' `
    'PreviewSession refresh result does not pass through repaint decision'
Assert-Contains $preview 'ClearNativePreviewState' `
    'failed/invalid refresh path does not clear native scene state'
Assert-Contains $decision 'RefreshStatus::InvalidInput|RefreshStatus::ReentrantRejected' `
    'repaint decision helper does not handle failed PreviewSession statuses'
Assert-Contains $preview 'PendingPreviewRequest' `
    'delayed refresh does not use a value-owned request record'
Assert-Contains $preview 'treeWindow|treeGeneration' `
    'delayed refresh request has no TreeView lifecycle identity'
Assert-Contains $preview 'IsWindow\s*\(|TreeView_GetItem' `
    'delayed refresh does not validate the TreeView/item before dereference'
Assert-Contains $preview 'GetActiveEditorView' `
    'delayed refresh does not re-resolve the current editor view/document'
Assert-NotContains $preview 'm_pendingPreviewTree|m_pendingPreviewItem|m_pendingPreviewPath' `
    'delayed refresh stores a raw tree pointer or separate stale handle fields'
Assert-Contains $preview 'InvalidatePendingPreviewRefresh\s*\(' `
    'preview window has no pending-request invalidation hook'
Assert-Contains $stateMachine 'if\s*\(!post\(\)\)' `
    'state machine does not retain a failed PostMessage retry'
Assert-Contains $stateMachine 'while\s*\(pending_\.valid\(\)\)' `
    'state machine does not drain retries with a non-recursive loop'
Assert-Contains $stateMachine 'retry_pending_' `
    'state machine has no retry state'
Assert-NotContains $preview 'ProcessPendingPreviewRefresh\s*\([^)]*false' `
    'UI refresh path still uses recursive retry entry'
Assert-NotContains $stateMachine 'Pump\s*\([^)]*Pump\s*\(' `
    'state machine contains recursive Pump entry'
Assert-NotContains $stateMachine 'HWND|CTreeCtrl|HTREEITEM|afx_msg|MFC' `
    'state machine helper leaks UI/MFC types'

$mouseStart = $preview.IndexOf('void CRScenePreviewPage::OnMouseMove(')
$rightClickStart = $preview.IndexOf('void CRScenePreviewPage::OnRButtonDown(')
if ($mouseStart -ge 0 -and $rightClickStart -gt $mouseStart) {
    $mouseBlock = $preview.Substring($mouseStart, $rightClickStart - $mouseStart)
    Assert-NotContains $mouseBlock 'PreviewSession::Refresh|m_previewSession\.Refresh' `
        'mouse-move handler refreshes PreviewSession'
}
$timerBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::OnTimer('
Assert-Contains $timerBlock 'kDynamicPlaybackTimerId' `
    'timer handler does not identify the dynamic playback timer'
Assert-NotContains $timerBlock 'm_previewSession\.Refresh|RequestPreviewRefresh\s*\(' `
    'dynamic playback timer bypasses the refresh coordinator'

$stateMachineTest = Join-Path $PSScriptRoot 'test_preview_refresh_state_machine.ps1'
Assert-True (Test-Path -LiteralPath $stateMachineTest) `
    'missing executable refresh state-machine fixture'
& $stateMachineTest
if ($LASTEXITCODE -ne 0) {
    throw 'PREVIEW_REFRESH_ROUTING_FAIL: executable state-machine fixture failed'
}

Write-Output 'preview_refresh_routing: PASS'
