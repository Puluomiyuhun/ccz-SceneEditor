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
$hostSourcePath = Join-Path $SourceRoot 'ScenePreviewHost.cpp'
$stateMachineHeaderPath = Join-Path $SourceRoot 'PreviewIntegration\PreviewRefreshStateMachine.h'
$decisionHeaderPath = Join-Path $SourceRoot 'PreviewIntegration\PreviewRefreshDecision.h'
$documentSourcePath = Join-Path $SourceRoot 'cczEditor2Doc.cpp'

foreach ($path in @($viewHeaderPath, $viewSourcePath, $previewHeaderPath, $previewSourcePath, $hostSourcePath, $stateMachineHeaderPath, $decisionHeaderPath, $documentSourcePath)) {
    Assert-True (Test-Path -LiteralPath $path) "missing source file: $path"
}

$viewHeader = Get-Content -LiteralPath $viewHeaderPath -Raw -Encoding UTF8
$view = Get-Content -LiteralPath $viewSourcePath -Raw -Encoding UTF8
$previewHeader = Get-Content -LiteralPath $previewHeaderPath -Raw -Encoding UTF8
$preview = Get-Content -LiteralPath $previewSourcePath -Raw -Encoding UTF8
$hostSource = Get-Content -LiteralPath $hostSourcePath -Raw -Encoding UTF8
$appSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'cczEditor2.cpp') -Raw -Encoding UTF8
$appOpen = Get-FunctionBlock $appSource 'CDocument* CcczEditor2App::OpenDocumentFile(LPCTSTR path, BOOL addToMru)'
Assert-Contains $appOpen 'BeginDocumentOpen\(\)[\s\S]*~PreviewOpenScope\(\)[\s\S]*EndDocumentOpen\(\)[\s\S]*CWinAppEx::OpenDocumentFile\(path, addToMru\)' `
    'all MFC document opens must use an exception-safe preview publication scope'
foreach ($signature in @('void CScenePreviewWnd::ShowPage(', 'bool CScenePreviewWnd::RequestPreviewRefresh(', 'bool CScenePreviewWnd::ActivateScriptPreview(', 'void CScenePreviewWnd::InitCharList(')) {
    $block = Get-FunctionBlock $hostSource $signature
    Assert-Contains $block '\{\s*if \(m_documentOpenDepth != 0\) return' `
        'intermediate open callbacks must not publish or rebind a preview'
}
$openComplete = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::EndDocumentOpen()'
Assert-Contains $openComplete '--m_documentOpenDepth != 0[\s\S]*MDIGetActive\(\)[\s\S]*GetActiveView\(\)[\s\S]*m_previewTreeInitialized[\s\S]*RefreshScenePreview\(\)' `
    'the outermost completed open must refresh only the current initialized MDI owner'
$stateMachine = Get-Content -LiteralPath $stateMachineHeaderPath -Raw -Encoding UTF8
$decision = Get-Content -LiteralPath $decisionHeaderPath -Raw -Encoding UTF8
$documentSource = Get-Content -LiteralPath $documentSourcePath -Raw -Encoding UTF8

Assert-Contains $viewHeader 'bool\s+RefreshScenePreview\s*\(\s*\)' `
    'CcczEditor2View does not declare RefreshScenePreview'
Assert-Contains $view 'bool\s+CcczEditor2View::RefreshScenePreview\s*\(\s*\)' `
    'CcczEditor2View does not define RefreshScenePreview'
Assert-Contains $previewHeader 'RequestPreviewRefresh\s*\(' `
    'CScenePreviewWnd does not declare RequestPreviewRefresh'
Assert-Contains $preview 'void\s+CRScenePreviewPage::RequestPreviewRefresh\s*\(' `
    'CScenePreviewWnd does not define RequestPreviewRefresh'

$selectionBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnTvnSelchanged('
$editBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnEditModify('
$refreshBlock = Get-FunctionBlock $view 'bool CcczEditor2View::RefreshScenePreview('
$createRootBlock = Get-FunctionBlock $view 'void CcczEditor2View::CreateRoot('
$viewDestroyBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnDestroy('
$viewActivateBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnActivateView('
$initialUpdateBlock = Get-FunctionBlock $view 'void CcczEditor2View::OnInitialUpdate()'
Assert-Contains $viewHeader 'm_previewTreeInitialized\s*=\s*false' `
    'view preview readiness must start false before initial MFC activation'
Assert-Contains $viewActivateBlock 'activate && m_previewTreeInitialized[\s\S]*ActivateScriptPreview' `
    'initial activation must not display a preview before the script tree is complete'
Assert-Contains $initialUpdateBlock 'm_previewTreeInitialized\s*=\s*false[\s\S]*CTreeView::OnInitialUpdate\(\)[\s\S]*CreateFileTree\(\)[\s\S]*CreateNewTree\(\)[\s\S]*m_previewTreeInitialized\s*=\s*true[\s\S]*RefreshScenePreview\(\)' `
    'first preview refresh must run only after tree construction finishes'
Assert-Contains $refreshBlock 'if \(!m_previewTreeInitialized\) return true;[\s\S]*RequestPreviewRefresh' `
    'selection notifications during tree initialization must not bind partial previews'
$treeDestroyedBlock = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::EditorTreeDestroyed('
$treeRebuildBlock = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::PrepareForTreeRebuild('
$scriptActivationBlock = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::ActivateScriptPreview('
$activationButtonBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::OnActivationButtonClicked()'
$conditionActionRefreshBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::RequestConditionActionRefresh()'
$invalidateBlock = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::InvalidatePendingPreviewRefresh('
$hostRefreshBlock = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::RequestPreviewRefresh('
$sBattleRefreshStart = $hostRefreshBlock.IndexOf('if (page == Page::SBattle)')
$sBattleRefreshEnd = $hostRefreshBlock.IndexOf('ParkActiveSBattleDocument();', $sBattleRefreshStart)
Assert-True ($sBattleRefreshStart -ge 0 -and $sBattleRefreshEnd -gt $sBattleRefreshStart) `
    'S battle activation branch could not be isolated for R/S routing checks'
$sBattleRefreshBlock = $hostRefreshBlock.Substring(
    $sBattleRefreshStart, $sBattleRefreshEnd - $sBattleRefreshStart)

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
Assert-Contains $viewActivateBlock 'ActivateScriptPreview\s*\(' `
    'MDI page activation does not route through per-document preview sessions'
Assert-NotContains $viewActivateBlock 'RefreshScenePreview\s*\(' `
    'MDI activation still forces the full preview refresh path'
Assert-Contains $scriptActivationBlock 'm_rPage\.ActivateDocument[\s\S]*!reusedUnchangedSession[\s\S]*m_rPage\.RequestPreviewRefresh' `
    'R activation checks the document session before requesting a full refresh'
Assert-Contains $activationButtonBlock 'ActivateTarget[\s\S]*PostMessage\s*\(\s*kConditionActionMessage' `
    'R activation defers its full preview refresh to a later window-message turn'
Assert-Contains $activationButtonBlock 'if\s*\(\s*PostMessage\s*\(\s*kConditionActionMessage[\s\S]*==\s*FALSE\s*\)\s*\{\s*RequestConditionActionRefresh\s*\(\s*\)' `
    'R activation falls back to an immediate refresh only when message posting fails'
Assert-Contains $conditionActionRefreshBlock 'GetActiveEditorView\(\)[\s\S]*GetTreeCtrl\(\)[\s\S]*GetSelectedItem\(\)[\s\S]*RequestPreviewRefresh\([^;]*\*view->GetDocument\(\)' `
    'R condition actions refresh from the active document tree and current selection'
Assert-NotContains $conditionActionRefreshBlock 'RequestPreviewRefreshForCurrentDocument\(\*m_pTreeCtrl' `
    'R condition actions do not silently depend on a cached tree pointer'
Assert-NotContains $sBattleRefreshBlock 'm_rPage\.InvalidatePendingPreviewRefresh\s*\(' `
    'S battle activation clears R preview state instead of preserving independent R state'
Assert-Contains $sBattleRefreshBlock 'm_rPage\.ParkActiveDocument\s*\(' `
    'S battle activation parks R into its document-owned session before switching pages'
Assert-Contains $createRootBlock 'PrepareForTreeRebuild\s*\(\s*\*GetDocument\(\)\s*\)' `
    'tree rebuild does not park S state while invalidating delayed preview requests'
Assert-Contains $viewDestroyBlock 'EditorTreeDestroyed\s*\([^;]*\*GetDocument\(\)' `
    'view destruction routes targeted S teardown and invalidates delayed preview requests'
 $documentCloseBlock = Get-FunctionBlock $documentSource 'void CcczEditor2Doc::OnCloseDocument('
Assert-Contains $documentCloseBlock 'EditorTreeDestroyed\s*\([^;]*\*this' `
    'closing a script document explicitly clears its S preview session'
Assert-Contains $documentCloseBlock 'CloseRDocument\s*\(\s*GetPreviewDocumentId\(\)\s*\)' `
    'closing an R document evicts its own cached preview session'
Assert-True ($documentCloseBlock.IndexOf('EditorTreeDestroyed') -lt $documentCloseBlock.IndexOf('recur_close')) `
    'S preview cache is cleared before document-owned tree data is released'
Assert-Contains $treeDestroyedBlock 'm_rPage\.EditorTreeDestroyed\(treeWindow,\s*document\)' `
    'tree destruction evicts only the R session belonging to the destroyed document'
Assert-Contains $treeRebuildBlock 'ownerPage\s*==\s*Page::RScene[\s\S]*m_rPage\.PrepareForTreeRebuild\(document\)' `
    'R preview rebuild handling invalidates only the owning document session'
Assert-NotContains $invalidateBlock 'm_rPage\.InvalidatePendingPreviewRefresh\(\)[\s\S]*m_sPage\.ClearDocument\(\)' `
    'S content edits preserve the document-owned session instead of clearing it through the generic invalidation path'

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
