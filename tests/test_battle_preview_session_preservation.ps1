$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pageHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$page = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$hostHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.h') -Raw -Encoding UTF8
$previewHost = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$view = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw "BATTLE_PREVIEW_SESSION_FAIL: $message" }
    Write-Output "[PASS] $message"
}

function Get-Block([string]$text, [string]$start, [string]$end) {
    $startAt = $text.IndexOf($start)
    if ($startAt -lt 0) { return '' }
    $endAt = $text.IndexOf($end, $startAt + $start.Length)
    if ($endAt -lt 0) { return $text.Substring($startAt) }
    return $text.Substring($startAt, $endAt - $startAt)
}

$refreshBlock = Get-Block $page 'bool CSBattlePreviewPage::RequestPreviewRefresh(' 'bool CSBattlePreviewPage::ConfirmDiscardTemporaryChanges()'
$hostRefreshBlock = Get-Block $previewHost 'bool CScenePreviewWnd::RequestPreviewRefresh(' 'void CScenePreviewWnd::PrepareForTreeRebuild('
$parkBlock = Get-Block $previewHost 'void CScenePreviewWnd::PrepareForTreeRebuild(' 'void CScenePreviewWnd::InvalidatePendingPreviewRefresh('
$selectionBlock = Get-Block $view 'void CcczEditor2View::OnTvnSelchanged(' 'bool CcczEditor2View::RefreshScenePreview()'
$viewRefreshBlock = Get-Block $view 'bool CcczEditor2View::RefreshScenePreview()' 'BOOL CcczEditor2View::PreTranslateMessage('
$createRootBlock = Get-Block $view 'void CcczEditor2View::CreateRoot(' 'void CcczEditor2View::CreateNewTree()'

Assert-Text $pageHeader 'bool RequestPreviewRefresh\(' `
    'S page reports whether a requested scope transition was accepted'
Assert-Text $pageHeader 'bool ConfirmDiscardTemporaryChanges\(' `
    'host can ask S page before leaving its active session'
Assert-Text $refreshBlock 'BuildBattlePreviewInput[\s\S]*document_revision\s*==\s*ActiveSession\(\)\.baseline_capture\.input\.document_revision[\s\S]*scope_key\s*==\s*ActiveSession\(\)\.baseline_capture\.input\.scope_key[\s\S]*return true;' `
    'selection-only changes retain the session when its document revision and Section are unchanged'
Assert-Text $page 'ConfirmDiscardTemporaryChanges\(\)[\s\S]*MB_YESNO[\s\S]*MB_DEFBUTTON2' `
    'discarding pending temporary edits requires an explicit default-No prompt'
Assert-Text $page 'bool CSBattlePreviewPage::ConfirmDiscardSessionChanges\([\s\S]*workspace\.second->operations->CanUndo\(\)[\s\S]*if\s*\(!hasTemporaryUndo\)\s*return true' `
    'the discard prompt is driven by pending temporary editor undo history'
Assert-Text $hostHeader 'bool RequestPreviewRefresh\(' `
    'preview host propagates rejected session transitions to the view'
Assert-Text $hostRefreshBlock 'Page::RScene[\s\S]*ParkActiveSBattleDocument\(\)[\s\S]*ShowPage\(page\)' `
    'leaving S for R parks the per-document session without a discard prompt'
Assert-Text $hostRefreshBlock 'ParkActiveSBattleDocument\(\)[\s\S]*ShowPage\(Page::Neutral\)' `
    'leaving S for a non-preview document preserves its parked session'
Assert-Text $hostHeader 'PrepareForTreeRebuild\(CcczEditor2Doc& document\)' `
    'host exposes a tree-rebuild route that can park an S session'
Assert-Text $parkBlock 'ownerPage\s*==\s*Page::RScene[\s\S]*m_rPage\.PrepareForTreeRebuild\(document\)[\s\S]*ownerPage\s*==\s*Page::SBattle[\s\S]*m_activeSBattleDocumentId\s*==\s*document\.GetSPreviewDocumentId\(\)[\s\S]*ParkActiveSBattleDocument\(\)' `
    'tree rebuild routes by owner, invalidates only the target R session, and parks only the matching S document'
Assert-Text $createRootBlock 'PrepareForTreeRebuild\(\s*\*GetDocument\(\)\s*\)' `
    'document tree replacement preserves the S session until its destination is known'
Assert-Text $viewRefreshBlock 'const bool accepted = frame->m_wndScenePreview\.RequestPreviewRefresh[\s\S]*return accepted;' `
    'the document view receives preview transition acceptance'
Assert-Text $selectionBlock 'if \(!RefreshScenePreview\(\)\)[\s\S]*SelectPlaybackItem\(pNMTreeView->itemOld\.hItem, true\)' `
    'declining a Section transition restores the previous TreeView selection'

Write-Output 'BATTLE_PREVIEW_SESSION_PASS'
