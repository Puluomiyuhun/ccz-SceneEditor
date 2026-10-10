$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$viewSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8
$hostSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding UTF8
$pageHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewWnd.h') -Raw -Encoding UTF8
$documentHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2Doc.h') -Raw -Encoding UTF8
$documentSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2Doc.cpp') -Raw -Encoding UTF8
$sessionHeader = Get-Content -LiteralPath (Join-Path $repo 'cczPreviewCore/include/ccz/preview/r_document_session.h') -Raw -Encoding UTF8
$sessionTests = Get-Content -LiteralPath (Join-Path $repo 'cczPreviewCoreTests/tests/r_document_session_tests.cpp') -Raw -Encoding UTF8

function Assert-Contract([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) {
        throw "R_PREVIEW_DOCUMENT_SESSIONS_FAIL: $message"
    }
    Write-Output "[PASS] $message"
}

function Assert-NoContract([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) {
        throw "R_PREVIEW_DOCUMENT_SESSIONS_FAIL: $message"
    }
    Write-Output "[PASS] $message"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "R_PREVIEW_DOCUMENT_SESSIONS_FAIL: missing $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "R_PREVIEW_DOCUMENT_SESSIONS_FAIL: missing body for $signature" }
    $depth = 0
    for ($index = $open; $index -lt $text.Length; $index++) {
        if ($text[$index] -eq '{') { $depth++ }
        elseif ($text[$index] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $index - $start + 1) }
        }
    }
    throw "R_PREVIEW_DOCUMENT_SESSIONS_FAIL: unbalanced $signature"
}

$viewActivation = Get-FunctionBlock $viewSource 'void CcczEditor2View::OnActivateView('
$rActivation = Get-FunctionBlock $pageSource 'bool CRScenePreviewPage::ActivateDocument('
$rPark = Get-FunctionBlock $pageSource 'void CRScenePreviewPage::ParkActiveDocument('
$treeDestroyed = Get-FunctionBlock $pageSource 'bool CRScenePreviewPage::EditorTreeDestroyed('
$treeRebuild = Get-FunctionBlock $pageSource 'bool CRScenePreviewPage::PrepareForTreeRebuild('
$closeDocument = Get-FunctionBlock $pageSource 'bool CRScenePreviewPage::CloseDocument('
$invalidateDocument = Get-FunctionBlock $pageSource 'void CRScenePreviewPage::InvalidateDocumentPreview('
$resolveTree = Get-FunctionBlock $pageSource 'CTreeCtrl* CRScenePreviewPage::ResolvePreviewTree('
$hostActivation = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::ActivateScriptPreview('
$hostRebuild = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::PrepareForTreeRebuild('
$hostTreeDestroyed = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::EditorTreeDestroyed('
$requestRefresh = Get-FunctionBlock $pageSource 'void CRScenePreviewPage::RequestPreviewRefresh('
$documentClose = Get-FunctionBlock $documentSource 'void CcczEditor2Doc::OnCloseDocument('

Assert-Contract $documentHeader 'GetPreviewDocumentId\(\)\s*const' `
    'each open document exposes an instance identity independent of its path'
Assert-Contract $sessionHeader 'document_id[\s\S]*normalized_path_key[\s\S]*content_epoch[\s\S]*tree_generation[\s\S]*tree_window[\s\S]*selected_target' `
    'the activation key includes document, content, tree, window, and selection identity'
Assert-Contract $sessionTests 'same_path_different_id|content_or_tree_version|another_tree|selected_target' `
    'core tests cover document isolation, version changes, and stale TreeView handles'
Assert-Contract $viewActivation 'ActivateScriptPreview\s*\(' `
    'MDI activation enters the document-aware preview route'
Assert-NoContract $viewActivation 'RefreshScenePreview\s*\(' `
    'MDI activation does not force a full R replay'
Assert-Contract $hostActivation 'm_rPage\.ActivateDocument[\s\S]*!reusedUnchangedSession[\s\S]*m_rPage\.RequestPreviewRefresh' `
    'unchanged R sessions skip the full refresh and new or changed sessions refresh'
Assert-Contract $rActivation 'm_documentSessions\.find\(current\.document_id\)[\s\S]*DecideRDocumentActivation[\s\S]*RDocumentActivationDecision::Reuse' `
    'R session lookup validates the cached document snapshot before reuse'
Assert-NoContract $rActivation 'CaptureEditorPreviewInput|UpdatePreview\s*\(|ApplyNativeReplayPlan' `
    'R activation restore itself performs no document capture or native replay'
Assert-Contract $pageHeader 'std::map<ccz::editor_preview::PreviewDocumentId, RDocumentPreviewSession>' `
    'R page owns independent session values keyed by document instance ID'
Assert-Contract $rPark 'm_documentSessions\.insert_or_assign\(documentId' `
    'switching away parks the current R state in its document session'
Assert-Contract $rPark 'm_pendingPreviewRequest\.clear\(\)[\s\S]*m_previewRefreshState\.Invalidate\(\)' `
    'parking invalidates only the shared page queue so stale requests cannot cross documents'
Assert-Contract $resolveTree 'request\.activation\.document_id[\s\S]*GetPreviewContentEpoch[\s\S]*GetPreviewTreeGeneration[\s\S]*tree\.GetSelectedItem\(\)\s*!=\s*request\.selectedItem[\s\S]*TreeView_GetItem' `
    'queued refreshes validate the active document and every native tree handle before use'
Assert-Contract $hostRebuild 'm_rPage\.PrepareForTreeRebuild\(document\)' `
    'a tree rebuild invalidates the owning R document session only'
Assert-Contract $treeRebuild 'InvalidateDocumentPreview\(document\.GetPreviewDocumentId\(\)\)' `
    'R tree rebuild cleanup is targeted by document ID'
Assert-Contract $treeDestroyed 'm_documentSessions\.erase\(documentId\)' `
    'destroying a document view drops its cached tree-backed R state'
Assert-Contract $closeDocument 'm_documentSessions\.erase\(documentId\)' `
    'closing an inactive R document drops only its own session'
Assert-Contract $invalidateDocument 'm_documentSessions\.erase\(documentId\)[\s\S]*if \(m_activeDocumentId != documentId\) return' `
    'invalidating an inactive document leaves the active R session alone'
Assert-Contract $requestRefresh 'CaptureEditorPreviewInput|m_previewRefreshState\.Submit' `
    'selection and edit refreshes still use the normal capture pipeline'
Assert-Contract $documentClose 'CloseRDocument\s*\(\s*GetPreviewDocumentId\(\)\s*\)' `
    'document close explicitly evicts its R session'
Assert-Contract $hostTreeDestroyed 'm_rPage\.EditorTreeDestroyed\(treeWindow,\s*document\)' `
    'host routes R tree teardown with the owning document identity'

Write-Output 'R_PREVIEW_DOCUMENT_SESSIONS_PASS'
