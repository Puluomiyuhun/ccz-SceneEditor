$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$opsPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEditorOps.cpp'
$page = Get-Content -LiteralPath $pagePath -Raw
$header = Get-Content -LiteralPath $headerPath -Raw
$ops = Get-Content -LiteralPath $opsPath -Raw

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

Assert-Text $header 'BattleEditorState m_editorState' `
    'S role/brush UI must use the isolated BattleEditorState.'
Assert-Text $header 'CListBox m_characterList' `
    'The S preview page must own its role/brush list control.'
Assert-Text $page 'SelectMinionBrush\(' `
    'The brush UI must route selection through BattleEditorOps.'
Assert-Text $page 'SelectCharacter\(' `
    'The character UI must route selection through BattleEditorOps.'
Assert-Text $page 'SetDeploymentInstruction\(' `
    'Placement must bind the selected deployment category even when its command does not yet exist.'
Assert-Text $page 'OnLButtonDown[\s\S]*?StampAt\(' `
    'Left mouse down must begin brush stamping.'
Assert-Text $page 'OnMouseMove[\s\S]*?StampAt\(' `
    'Holding left mouse while moving must stamp each newly crossed cell.'
Assert-Text $page 'OnRButtonDown[\s\S]*?DeleteAt\(' `
    'Right mouse down must delete one unambiguous unit.'
Assert-Text $page 'OnUndo[\s\S]*?UndoNativeWriteback[\s\S]*?m_editorOps->Undo\(' `
	'Undo button must route to the active native transaction or temporary BattleEditorOps transaction.'
Assert-Text $page 'm_undoButton\.EnableWindow\(m_nativeWritebackTransaction\.undo_available\(\) \|\|' `
	'Undo button must expose an available native or temporary undo.'
Assert-Text $page 'void CSBattlePreviewPage::SetSidebarVisible\(bool visible\)[\s\S]*?LayoutEditorControls\(\)' `
	'Sidebar visibility must relayout the canvas.'
Assert-Text $page 'void CSBattlePreviewPage::LayoutEditorControls\(\)[\s\S]*?ShowWindow\(SW_HIDE\)[\s\S]*?ShowWindow\(SW_SHOW\)' `
	'Sidebar layout must hide/show controls without reconstructing editor state.'
Assert-Text $page 'm_editorState\.battle\.units\.size\(\)' `
    'The canvas must render the full S battle state without paging.'
if ($page -match 'm_pPer2|RCharacter|RRole|RPreviewSession|RResourceCache') {
    throw 'S role/brush UI must not read R character/session/resource state.'
}
Assert-Text $page 'ApplyPendingNativeWritebackForEvidence' `
	'Product writeback path must remain behind the closed evidence gate.'
Assert-Text $page 'ApplyPendingNativeWritebackForQaCopy' `
	'Debug QA writeback must use an isolated marked S copy.'
Assert-Text $page 'CCZ_S_BATTLE_WRITEBACK_QA' `
	'The QA writeback controls and handler must be compile-time gated.'

$clear = [regex]::Match($ops, 'void clear_character_dependent_state\([\s\S]*?\n\}')
Assert-Text $clear.Value 'selected_minion_brush\.reset\(\)' `
    'Changing role query/page must clear stale brush selection.'

Write-Output 'PASS: S role/brush controls are isolated and wired to temporary BattleEditorOps.'
