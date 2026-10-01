$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$hostSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$hostHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.h') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$pageHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$docSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2Doc.cpp') -Raw -Encoding UTF8
$childSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ChildFrm.cpp') -Raw -Encoding UTF8
$mainSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/MainFrm.cpp') -Raw -Encoding UTF8

function Assert-Contract([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "BATTLE_DOCUMENT_CLOSE_LIFECYCLE_FAIL: $Message"
    }
    Write-Output "[PASS] $Message"
}

function Assert-NotContract([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "BATTLE_DOCUMENT_CLOSE_LIFECYCLE_FAIL: $Message"
    }
    Write-Output "[PASS] $Message"
}

function Get-FunctionBlock([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_DOCUMENT_CLOSE_LIFECYCLE_FAIL: missing $Signature" }
    $open = $Text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_DOCUMENT_CLOSE_LIFECYCLE_FAIL: missing body for $Signature" }
    $depth = 0
    for ($i = $open; $i -lt $Text.Length; $i++) {
        if ($Text[$i] -eq '{') { $depth++ }
        elseif ($Text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $Text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_DOCUMENT_CLOSE_LIFECYCLE_FAIL: unbalanced $Signature"
}

$canClose = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::CanCloseDocument('
$cancelClose = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::CancelDocumentClose('
$closeDocument = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::CloseSBattleDocument('
$treeDetached = Get-FunctionBlock $hostSource 'void CScenePreviewWnd::EditorTreeDestroyed('
$saveModified = Get-FunctionBlock $docSource 'BOOL CcczEditor2Doc::SaveModified()'
$onCloseDocument = Get-FunctionBlock $docSource 'void CcczEditor2Doc::OnCloseDocument()'
$bulkClose = Get-FunctionBlock $mainSource 'void CMainFrame::OnAllFileClose()'
$confirmSession = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::ConfirmDiscardSessionChanges('

Assert-Contract $hostHeader 'CanCloseDocument\(ccz::editor_preview::SPreviewDocumentId' `
    'the host exposes a close decision keyed by stable document ID'
Assert-Contract $canClose 'm_sBattleSessions\.Find\(documentId\)' `
    'close decisions inspect the requested document session even when it is inactive'
Assert-Contract $canClose 'm_closeApprovalDocumentIds' `
    'one accepted preview prompt is reused through the same close attempt'
Assert-Contract $canClose 'ConfirmDiscardSessionChanges' `
    'preview drafts from an inactive S document still receive a close prompt'
Assert-NotContract $confirmSession 'hasNativeUndo|native_writeback_transaction\.undo_available\(\)' `
    'a committed native writeback undo record alone does not trigger the preview-draft discard prompt'
Assert-Contract $confirmSession 'hasMapOperationState[\s\S]*if\s*\(!hasTemporaryUndo\s*&&\s*!hasMapOperationState\)\s*return true' `
	'pending preview drafts and staged map operations trigger the discard prompt'
Assert-Contract $cancelClose 'm_closeApprovalDocumentIds\.erase\(documentId\)' `
    'a canceled save clears only that document close approval'
Assert-Contract $closeDocument 'm_sBattleSessions\.Erase\(documentId\)' `
    'actual close evicts exactly the target document session'
Assert-Contract $treeDetached 'ParkActiveSBattleDocument' `
    'tree detachment parks state without erasing the document session'
Assert-NotContract $treeDetached 'm_sBattleSessions\.Erase' `
    'tree detachment does not evict a still-open document session'
Assert-Contract $saveModified 'CanCloseDocument[\s\S]*return FALSE' `
    'SaveModified preserves the session and resets preview approval when save is canceled'
Assert-Contract $saveModified 'CancelDocumentClose\(documentId\)' `
    'canceled SaveModified clears the preview approval for a retry'
Assert-Contract $saveModified 'can_close\s*=\s*false' `
    'each custom close dialog starts with a fresh decision instead of a stale can_close flag'
Assert-Contract $onCloseDocument 'CloseSBattleDocument[\s\S]*GetSPreviewDocumentId' `
    'document destruction performs ID-targeted session eviction'
Assert-NotContract $childSource 'CanCloseEditorTree|CanCloseDocument' `
    'child-window teardown no longer prompts against a singleton active page'
Assert-Contract $bulkClose 'const BOOL canClose[\s\S]*SaveModified\(\)[\s\S]*if\s*\(canClose\)[\s\S]*OnCloseDocument' `
    'bulk close destroys a document only when this SaveModified call returned TRUE'
Assert-NotContract $bulkClose 'can_close\s*\|\|' `
    'bulk close does not bypass a canceled SaveModified result through stale state'

Write-Output 'BATTLE_DOCUMENT_CLOSE_LIFECYCLE_PASS'
