$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$hostSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8

function Assert-Contract([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "BATTLE_PREVIEW_SESSION_FRESHNESS_FAIL: $Message"
    }
    Write-Output "[PASS] $Message"
}

function Get-FunctionBlock([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_PREVIEW_SESSION_FRESHNESS_FAIL: missing $Signature" }
    $open = $Text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_PREVIEW_SESSION_FRESHNESS_FAIL: missing body for $Signature" }
    $depth = 0
    for ($i = $open; $i -lt $Text.Length; $i++) {
        if ($Text[$i] -eq '{') { $depth++ }
        elseif ($Text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $Text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_PREVIEW_SESSION_FRESHNESS_FAIL: unbalanced $Signature"
}

$activate = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::ActivateSBattleDocument('
$hostRefresh = Get-FunctionBlock $hostSource 'bool CScenePreviewWnd::RequestPreviewRefresh('
$bind = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::BindSession('
$quickRefresh = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh('
$writebackPlan = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::BuildPendingWritebackPlan('
$writebackApply = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::ApplyPendingNativeWritebackTransaction('
$undo = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::UndoNativeWriteback('

Assert-Contract $activate 'CheckBattlePreviewActivation\(' `
    'activation compares the saved document and tree generations before binding'
Assert-Contract $activate 'GetPreviewContentEpoch\(\)[\s\S]*GetPreviewTreeGeneration\(\)' `
    'activation reads both live version counters from the current document'
Assert-Contract $activate 'ReuseSession' `
    'unchanged document activation is identified before any capture request'
Assert-Contract $hostRefresh 'reusedUnchangedSession[\s\S]*return true[\s\S]*m_sPage\.RequestPreviewRefresh' `
    'unchanged parked sessions return directly without recapture, replay, or decode'
Assert-Contract $bind 'tree_generation[\s\S]*GetPreviewTreeGeneration\(\)[\s\S]*m_sourceTreeWindow' `
    'old runtime TreeView handles are adopted only when the generation still matches'
Assert-Contract $quickRefresh 'native_writeback_transaction\.Clear\(\)[\s\S]*LoadQuickModeBaselinePreservingDraft|LoadQuickModeBaselinePreservingDraft[\s\S]*native_writeback_transaction\.Clear\(\)' `
	'changed pinned Scene1 content expires committed QA undo and refreshes the preview baseline'
Assert-Contract $quickRefresh 'freshSectionRevision == ActiveSession\(\)\.quick_mode_section_revision[\s\S]*documentContentChanged[\s\S]*native_writeback_transaction\.Clear\(\)' `
	'unchanged Scene1 with edits elsewhere keeps its preview but expires document-wide QA undo'
Assert-Contract $pageSource 'bool CSBattlePreviewPage::HasPendingPreviewDrafts\(\)[\s\S]*operations->CanUndo\(\)' `
	'only uncommitted condition-workspace edits count as preview drafts'
Assert-Contract $pageSource 'bool CSBattlePreviewPage::HasPendingPreviewChanges\(\)[\s\S]*HasPendingPreviewDrafts\(\)[\s\S]*native_writeback_transaction\.undo_available\(\)' `
	'stale-source invalidation still accounts for committed QA undo history independently of discard confirmation'
Assert-Contract $writebackPlan 'ActiveSession\(\)\.stale' `
    'stale sessions fail before writeback-plan construction'
Assert-Contract $writebackApply 'ActiveSession\(\)\.stale' `
    'stale sessions fail before native apply preflight'
Assert-Contract $undo 'ActiveSession\(\)\.stale' `
    'stale sessions cannot reach native undo'
Assert-Contract $writebackApply 'GetSPreviewDocumentId\(\)[\s\S]*GetPreviewTreeGeneration\(\)' `
    'writeback validates the active document ID and tree generation immediately before mutation'

Write-Output 'BATTLE_PREVIEW_SESSION_FRESHNESS_PASS'
