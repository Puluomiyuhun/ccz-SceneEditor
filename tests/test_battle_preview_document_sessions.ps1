$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$hostHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.h') -Raw -Encoding UTF8
$hostSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$pageHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$viewSource = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8

function Assert-Contract([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "BATTLE_PREVIEW_DOCUMENT_SESSIONS_FAIL: $Message"
    }
    Write-Output "[PASS] $Message"
}

function Assert-NoLegacyOwner([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "BATTLE_PREVIEW_DOCUMENT_SESSIONS_FAIL: $Message"
    }
    Write-Output "[PASS] $Message"
}

Assert-Contract $hostHeader 'BattlePreviewSessionRegistry\s+m_sBattleSessions' `
    'preview host owns an ID-keyed registry for open S document sessions'
Assert-Contract $hostHeader 'SPreviewDocumentId\s+m_activeSBattleDocumentId' `
    'preview host records which S session is bound to the single page'
Assert-Contract $hostHeader 'CcczEditor2Doc\s*&\s*document' `
    'activation API receives document identity independently of its path'
Assert-Contract $pageHeader 'BindSession\s*\(\s*ccz::editor_preview::BattlePreviewDocumentSession\s*&' `
    'the MFC page explicitly binds to registry-owned session state'
Assert-Contract $pageHeader 'BattlePreviewDocumentSession\s*\*\s*m_activeSession' `
    'the page keeps only a temporary active binding, not the session owner'
Assert-Contract $hostHeader 'ActivateSBattleDocument\s*\(' `
    'host activation is a named per-document operation before any page refresh'
Assert-Contract $pageHeader 'UnbindSession\s*\(' `
    'the page explicitly drops UI and tree handles when parking a session'
Assert-Contract $pageSource 'SaveActiveSessionUiState\s*\(' `
    'the page snapshots its control values before parking an S session'
Assert-Contract $pageSource 'RestoreActiveSessionUiState\s*\(' `
    'the page restores control values from the activated document session'
Assert-NoLegacyOwner $pageHeader 'm_editorState\s*;' `
    'legacy page-owned S editor model has been removed'
Assert-NoLegacyOwner $pageHeader 'm_(writebackBaselineCapture|nativeWritebackTransaction|conditionWorkspaces|conditionCatalog|conditionModel|activeConditionScope|quickModeSectionRevision)\s*;' `
    'writeback baseline, condition model, undo history, and quick-mode revision are session-owned'
Assert-Contract $pageSource 'if \(!ui\.initialized\)[\s\S]*ui\.quick_mode_enabled\s*=\s*AfxGetApp\(\)->GetProfileInt[\s\S]*_T\("QuickMode"\), 1' `
    'a new session initializes quick mode to the configured default'
Assert-Contract $pageSource 'session\.editor_state\s*=\s*ActiveSession\(\)\.editor_state|workspace->second->state\s*=\s*session\.editor_state' `
    'condition workspace state remains owned by its document session'
Assert-Contract $hostSource 'm_sPage\.BindSession\s*\(' `
    'S activation binds the document-specific session before refreshing'
Assert-Contract $hostSource 'sameDocumentPath[\s\S]*CompareNoCase\(session\.document_path\.c_str\(\)\)[\s\S]*reusedUnchangedSession\s*=\s*switchingDocument\s*&&\s*sameDocumentPath' `
    'hot session reuse is refused when the same document instance has moved to a different script path'
Assert-Contract $hostSource 'Page::RScene[\s\S]*ParkActiveSBattleDocument\s*\(' `
    'switching to R parks S state instead of destroying the session'
Assert-Contract $hostSource 'Page::SBattle[\s\S]*document\.GetSPreviewDocumentId\s*\(' `
    'S session selection uses the stable document ID, not the file path'
Assert-Contract $viewSource 'RequestPreviewRefresh\s*\([\s\S]*\*GetDocument\(\)' `
    'the document view supplies the active document object to the host'
Assert-Contract $hostSource 'm_sBattleSessions\.GetOrCreate\s*\(' `
    'same-path open documents resolve to separate session objects by ID'
Assert-Contract $pageSource 'LoadForDocument\(session\.baseline_capture\.input\.document_path' `
    'reactivating a session rebinds the correct document asset root'
Assert-Contract $pageSource 'm_restoringSessionUi\s*=\s*true[\s\S]*m_searchEdit\.SetWindowText[\s\S]*m_restoringSessionUi\s*=\s*false' `
    'restoring search text does not clear the session character selection'

Write-Output 'BATTLE_PREVIEW_DOCUMENT_SESSIONS_PASS'
