$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repo 'cczEditor2\cczEditor2.vcxproj'
$pagePath = Join-Path $repo 'cczEditor2\BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2\BattlePreviewPage.h'
$qaGatePath = Join-Path $repo 'cczEditor2\PreviewIntegration\BattleWritebackQaGate.h'
$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8

function Assert-Condition([bool]$condition, [string]$message) {
    if (-not $condition) { throw "BATTLE_WRITEBACK_PRODUCT_FAIL: $message" }
    Write-Output "[PASS] $message"
}

Assert-Condition ($project -notmatch 'CCZSBattleWritebackQa|CCZ_S_BATTLE_WRITEBACK_QA') `
    'product writeback is no longer compile-gated by a QA-only property'
Assert-Condition ($header -notmatch 'CCZ_S_BATTLE_WRITEBACK_QA|OnQaWriteback|m_qaWritebackButton') `
    'writeback handler and button are present in ordinary Debug and Release builds'
Assert-Condition ($page -notmatch 'CCZ_S_BATTLE_WRITEBACK_QA|OnQaWriteback|QA写回') `
    'writeback UI and transaction path contain no QA-only compile or caption gate'
Assert-Condition ($page -match 'Create\(_T\("写回"\)') `
    'the S preview exposes a normal writeback button'
Assert-Condition ($page -match 'void CSBattlePreviewPage::OnWriteback\(') `
    'the standard writeback button has a normal command handler'
Assert-Condition ($page -notmatch 'IsCurrentQaCopy|ApplyPendingNativeWritebackForQaCopy') `
    'writeback admission no longer depends on a QA marker or QA-copy folder'
Assert-Condition ($page -match 'ApplyPendingNativeWritebackForCurrentSection[\s\S]*?ApplyPendingNativeWritebackTransaction\(view, section, diagnostic, true\)') `
    'the product button routes nonempty changes through the existing native transaction'
Assert-Condition ($page -match 'const bool obstacleOnlyWriteback = !m_obstacleDrafts\.empty\(\) &&\s*!HasPendingPreviewDrafts\(\);[\s\S]*?const bool writebackEnabled = m_editorOps != nullptr &&\s*ActiveSession\(\)\.baseline_capture\.ok &&\s*\(ActiveSession\(\)\.active_condition_scope\.writeback_scope_resolved \|\|\s*obstacleOnlyWriteback\) &&\s*!ActiveSession\(\)\.stale && !ActiveSession\(\)\.native_writeback_rebase_required;[\s\S]*?m_writebackButton\.EnableWindow\(writebackEnabled\)') `
    'unit writeback stays condition-scope gated while obstacle-only writes use the selected instruction anchor and retain stale-session and failed-rebase guards'
Assert-Condition ($page -match 'ApplyPendingNativeWritebackTransaction[\s\S]*?ActiveSession\(\)\.stale[\s\S]*?document_id[\s\S]*?ValidateBattleWritebackNextOrdinal') `
    'native writeback retains session identity, stale-state, and ordinal validation'
Assert-Condition ($page -match 'if \(applied\)[\s\S]*?RebaseBattlePreviewAfterNativeWriteback\(') `
    'each successful write rebuilds the preview baseline from the updated native TreeView'
Assert-Condition ($page -match 'bool CSBattlePreviewPage::UndoNativeWriteback[\s\S]*?RebaseBattlePreviewAfterNativeWriteback\(') `
    'native undo continues to rebase preview state from the reverted TreeView'
Assert-Condition ($page -match 'void CSBattlePreviewPage::OnWriteback\([\s\S]*?ApplyPendingNativeWritebackForCurrentSection[\s\S]*?编辑器正常保存') `
    'the normal handler reports that editor Save persists the in-memory tree changes'
Assert-Condition ($page -notmatch 'ApplyPendingNativeWritebackForCurrentSection[\s\S]{0,1800}\b(CreateFileW|WriteFile|std::ofstream|fopen)\b') `
    'writeback updates editor memory and leaves disk serialization to the normal Save command'
Assert-Condition (Test-Path -LiteralPath $qaGatePath) `
    'the isolated QA-copy utility remains available for any separate debug tooling'

Write-Output 'BATTLE_WRITEBACK_PRODUCT_BOUNDARY_PASS'
