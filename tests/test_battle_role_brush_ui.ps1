$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$opsPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEditorOps.cpp'
$opsHeaderPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEditorOps.h'
$page = Get-Content -LiteralPath $pagePath -Raw
$header = Get-Content -LiteralPath $headerPath -Raw
$ops = Get-Content -LiteralPath $opsPath -Raw
$opsHeader = Get-Content -LiteralPath $opsHeaderPath -Raw

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing function: $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "Missing function body: $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "Unbalanced function: $signature"
}

Assert-Text $header 'BattlePreviewDocumentSession\s+m_unboundSession[\s\S]*BattlePreviewDocumentSession\s*\*\s*m_activeSession' `
	'S role/brush UI must bind to an ID-owned BattleEditorState session.'
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
$stamp = Get-FunctionBlock $page 'void CSBattlePreviewPage::StampAt('
Assert-Text $stamp 'm_editorOps->Place\(coordinate\)[\s\S]*?selected_minion_brush\.has_value\(\)[\s\S]*?ShowOperationResult\(result,\s*refreshPlacementChoices\)' `
    'Brush stamping must avoid rebuilding the character and deployment lists while the selected brush remains active.'
$refresh = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshEditorControls('
Assert-Text $refresh 'if \(refreshPlacementChoices\)\s*\{[\s\S]*?RefreshCharacterList\(\);[\s\S]*?RefreshDeploymentList\(\);[\s\S]*?\}' `
    'The normal control refresh must still rebuild lists when placement choices actually change.'
$rightClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnRButtonDown('
Assert-Text $opsHeader 'CancelPlacementBrush\(\)' `
    'BattleEditorOps must expose an explicit placement-brush cancel operation.'
Assert-Text $rightClick 'BattleEditorMode::Place[\s\S]*?CancelPlacementBrush\(\)[\s\S]*?return;' `
    'Right mouse down must cancel an active placement brush before unit hit testing or the delete menu.'
$cancelIndex = $rightClick.IndexOf('CancelPlacementBrush()', [System.StringComparison]::Ordinal)
$hitTestIndex = $rightClick.IndexOf('HitTestUnit(*coordinate)', [System.StringComparison]::Ordinal)
if ($cancelIndex -lt 0 -or $hitTestIndex -lt 0 -or $cancelIndex -gt $hitTestIndex) {
    throw 'Right click must cancel placement before attempting unit hit testing.'
}
Write-Output '[PASS] brush cancellation precedes right-click unit actions'
Assert-Text $rightClick 'm_leftStampActive\s*=\s*false[\s\S]*?m_lastStampCell\.reset\(\)[\s\S]*?GetCapture\(\) == this[\s\S]*?ReleaseCapture\(\)' `
    'Right-click cancellation must stop an in-progress drag stamp and release mouse capture.'
Assert-Text $rightClick 'DeleteAt\(' `
    'Right mouse down outside active placement mode must retain the unit context menu.'
$wheel = [regex]::Match($page, 'BOOL CSBattlePreviewPage::OnMouseWheel\([\s\S]*?\n\}')
Assert-Text $wheel.Value 'BattleEditorMode::Place' `
	'The placement canvas wheel must cycle the active placement direction.'
Assert-Text $wheel.Value 'BattleEditorMode::AdjustPosition' `
	'The position-adjustment canvas wheel must rotate the moving unit.'
Assert-Text $opsHeader 'RefreshSelectedBrushDataId\(bool reset_direction\s*=\s*false\)' `
	'Brush Data changes must preserve a manually chosen direction between stamps.'
Assert-Text $ops 'const int retained_direction = state_->mouse_direction[\s\S]*?state_->placement_defaults\.direction = reset_direction[\s\S]*?WrappedDirection\(retained_direction\)' `
	'Advancing the brush Data ID retains the wheel-selected direction.'
Assert-Text $page 'OnUndo[\s\S]*?UndoNativeWriteback[\s\S]*?m_editorOps->Undo\(' `
	'Undo button must route to the active native transaction or temporary BattleEditorOps transaction.'
Assert-Text $page 'm_undoButton\.EnableWindow\(!ActiveSession\(\)\.stale &&\s*\(ActiveSession\(\)\.native_writeback_transaction\.undo_available\(\) \|\|' `
	'Undo button must expose an available native or temporary undo.'
Assert-Text $page 'void CSBattlePreviewPage::SetSidebarVisible\(bool visible\)[\s\S]*?LayoutEditorControls\(\)' `
	'Sidebar visibility must relayout the canvas.'
Assert-Text $page 'void CSBattlePreviewPage::LayoutEditorControls\(\)[\s\S]*?ShowWindow\(SW_HIDE\)[\s\S]*?ShowWindow\(SW_SHOW\)' `
	'Sidebar layout must hide/show controls without reconstructing editor state.'
Assert-Text $page 'ActiveSession\(\)\.editor_state\.battle\.units\.size\(\)' `
    'The canvas must render the full S battle state without paging.'
if ($page -match 'm_pPer2|RCharacter|RRole|RPreviewSession|RResourceCache') {
    throw 'S role/brush UI must not read R character/session/resource state.'
}
Assert-Text $page 'm_writebackButton\.Create\(_T\("写回"\)' `
	'S battle preview must expose a normal writeback control.'
Assert-Text $page 'ON_BN_CLICKED\(kWritebackId, &CSBattlePreviewPage::OnWriteback\)' `
	'The normal writeback button must route to the product handler.'
Assert-Text $page 'ApplyPendingNativeWritebackForCurrentSection\(\*view, diagnostic\)' `
	'The product handler must apply the active S Section through the native transaction.'
Assert-Text $page '布阵改动已写入编辑器内存中的剧本树' `
	'The writeback status must describe its in-memory editor-tree effect.'

Assert-Text $header 'afx_msg void OnContextMenu\(CWnd\* pWnd, CPoint point\);' `
    'The S battle page must own the context-menu message instead of forwarding it to the dock host.'
Assert-Text $page 'ON_WM_CONTEXTMENU\(\)' `
    'The S battle page must route WM_CONTEXTMENU to its local suppression handler.'
$contextMenu = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnContextMenu('
if ($contextMenu -match '__super::OnContextMenu|CWnd::OnContextMenu|TrackPopupMenu|SendMessage') {
    throw 'The S battle page must suppress the docking-pane menu without forwarding WM_CONTEXTMENU.'
}

$clear = [regex]::Match($ops, 'void clear_character_dependent_state\([\s\S]*?\n\}')
Assert-Text $clear.Value 'selected_minion_brush\.reset\(\)' `
    'Changing role query/page must clear stale brush selection.'

Write-Output 'PASS: S role/brush controls are isolated and wired to temporary BattleEditorOps.'
