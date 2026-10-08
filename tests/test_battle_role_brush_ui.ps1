$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$opsPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEditorOps.cpp'
$opsHeaderPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEditorOps.h'
$panePath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleUnitParametersPane.cpp'
$paneHeaderPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleUnitParametersPane.h'
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$ops = Get-Content -LiteralPath $opsPath -Raw -Encoding UTF8
$opsHeader = Get-Content -LiteralPath $opsHeaderPath -Raw -Encoding UTF8
$pane = Get-Content -LiteralPath $panePath -Raw -Encoding UTF8
$paneHeader = Get-Content -LiteralPath $paneHeaderPath -Raw -Encoding UTF8

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
Assert-Text $stamp 'm_editorOps->Place\(coordinate\)[\s\S]*?ShowOperationResult\(result\)' `
	'Placement operations must not trigger a roster rebuild.'
$refresh = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshEditorControls('
if ($refresh -match 'RefreshCharacterList\(|RefreshDeploymentList\(|LayoutEditorControls\(') {
	throw 'Ordinary control refresh must not rebuild lists or relayout child controls.'
}
Assert-Text $refresh 'RefreshCharacterListSelection\(\)[\s\S]*?RefreshDeploymentSelection\(\)' `
	'Ordinary control refresh must synchronize selections in place.'
$characterSelection = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshCharacterListSelection('
Assert-Text $characterSelection 'GetCurSel\(\) != desired\)\s*m_characterList\.SetCurSel\(desired\)' `
	'Unchanged roster selection must not be cleared and re-applied during ordinary operations.'
$deploymentSelection = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshDeploymentSelection('
Assert-Text $deploymentSelection 'GetCurSel\(\) != desired\)\s*m_deploymentCombo\.SetCurSel\(desired\)' `
	'Unchanged deployment selection must not be reset during ordinary operations.'
$showOperation = Get-FunctionBlock $page 'void CSBattlePreviewPage::ShowOperationResult('
if ($showOperation -match 'RefreshCharacterList\(|RefreshDeploymentList\(|LayoutEditorControls\(') {
	throw 'Operation results must not rebuild or relayout the right-side controls.'
}
$headerRefresh = [regex]::Match($header, 'void RefreshEditorControls\([^;]*;')
Assert-Text $headerRefresh.Value 'void RefreshEditorControls\(\);' `
	'Control refresh must not implicitly opt into rebuilding the placement lists.'
$headerOperation = [regex]::Match($header, 'void ShowOperationResult\([\s\S]*?\);')
Assert-Text $headerOperation.Value 'ShowOperationResult\([\s\S]*?result\s*\)\s*;' `
	'Operation-result display must not have a default list-refresh path.'
$rightClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnRButtonDown('
$menuOpenAt = $rightClick.IndexOf('TrackPopupMenu', [System.StringComparison]::Ordinal)
$rightClickBeforeMenu = if ($menuOpenAt -ge 0) {
	$rightClick.Substring(0, $menuOpenAt)
} else {
	$rightClick
}
if ($rightClickBeforeMenu -match 'RefreshCharacterList\(|RefreshDeploymentList\(|LayoutEditorControls\(') {
	throw 'A plain right-click must not rebuild or relayout the right-side controls before opening its menu.'
}
Assert-Text $opsHeader 'CancelPlacementBrush\(\)' `
    'BattleEditorOps must expose an explicit placement-brush cancel operation.'
Assert-Text $rightClick 'hasContextTarget[\s\S]*?BattleEditorMode::Place[\s\S]*?CancelPlacementBrush\(\)[\s\S]*?return;' `
    'Right mouse down must route a hit target first and cancel placement only when the cell is empty.'
$cancelIndex = $rightClick.IndexOf('CancelPlacementBrush()', [System.StringComparison]::Ordinal)
$hitTestIndex = $rightClick.IndexOf('HitTestUnit(*coordinate)', [System.StringComparison]::Ordinal)
if ($cancelIndex -lt 0 -or $hitTestIndex -lt 0 -or $hitTestIndex -gt $cancelIndex) {
    throw 'Right click must hit-test the cell before deciding whether to cancel placement.'
}
Write-Output '[PASS] right-click target routing precedes brush cancellation'
Assert-Text $rightClick 'm_leftStampActive\s*=\s*false[\s\S]*?m_lastStampCell\.reset\(\)[\s\S]*?GetCapture\(\) == this[\s\S]*?ReleaseCapture\(\)' `
    'Right-click cancellation must stop an in-progress drag stamp and release mouse capture.'
Assert-Text $rightClick 'DeleteAt\(' `
    'Right mouse down outside active placement mode must retain the unit context menu.'
Assert-Text $rightClick 'contextUnit\s*&&\s*!group_context\s*&&\s*state\.mode\s*!=\s*ccz::editor_preview::BattleEditorMode::Select[\s\S]*?SelectAt\(\*coordinate\)' `
    'Right-clicking a role in multi-select mode must not convert or clear the current selection group.'
Assert-Text $rightClick 'splitContext\s*&&\s*!group_context\s*&&\s*contextUnit\s*&&\s*state\.mode\s*!=\s*ccz::editor_preview::BattleEditorMode::Select[\s\S]*?SelectAt\(\*coordinate\)' `
    'Choosing a role action from an overlap menu must preserve multi-selection mode state.'
$wheel = [regex]::Match($page, 'BOOL CSBattlePreviewPage::OnMouseWheel\([\s\S]*?\n\}')
Assert-Text $wheel.Value 'BattleEditorMode::Place' `
	'The placement canvas wheel must cycle the active placement direction.'
Assert-Text $wheel.Value 'BattleEditorMode::AdjustPosition' `
	'The position-adjustment canvas wheel must rotate the moving unit.'
Assert-Text $wheel.Value 'ShowOperationResult\(m_editorOps->CycleDirection\(notches\)\)' `
	'Direction changes must update the preview without rebuilding the roster.'
$searchChanged = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnSearchChanged('
Assert-Text $searchChanged 'RefreshCharacterList\(\)' `
	'Search changes must explicitly rebuild the visible roster.'
$previousPage = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnPreviousCharacterPage('
Assert-Text $previousPage 'RefreshCharacterList\(\)' `
	'Previous-page navigation must explicitly rebuild the visible roster.'
$nextPage = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnNextCharacterPage('
Assert-Text $nextPage 'RefreshCharacterList\(\)' `
	'Next-page navigation must explicitly rebuild the visible roster.'
$brushTab = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnBrushTab('
Assert-Text $brushTab 'RefreshCharacterList\(\)' `
	'Switching to the brush tab must rebuild its different roster contents.'
$characterTab = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnCharacterTab('
Assert-Text $characterTab 'RefreshCharacterList\(\)' `
	'Switching to the character tab must rebuild its different roster contents.'
Assert-Text $opsHeader 'RefreshSelectedBrushDataId\(bool reset_direction\s*=\s*false\)' `
	'Brush Data changes must preserve a manually chosen direction between stamps.'
Assert-Text $ops 'const int retained_direction = state_->mouse_direction[\s\S]*?state_->placement_defaults\.direction = reset_direction[\s\S]*?WrappedDirection\(retained_direction\)' `
	'Advancing the brush Data ID retains the wheel-selected direction.'
Assert-Text $page 'OnUndo[\s\S]*?UndoNativeWriteback[\s\S]*?m_editorOps->Undo\(' `
	'Undo button must route to the active native transaction or temporary BattleEditorOps transaction.'
Assert-Text $page 'const bool undoEnabled = !ActiveSession\(\)\.stale &&\s*!m_dynamicPreviewEnabled &&\s*\(ActiveSession\(\)\.native_writeback_transaction\.undo_available\(\) \|\|' `
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
Assert-Text $page 'm_writebackButton\.Create\(_T\("[^"]+"\)' `
	'S battle preview must expose a normal writeback control.'
Assert-Text $page 'ON_BN_CLICKED\(kWritebackId, &CSBattlePreviewPage::OnWriteback\)' `
	'The normal writeback button must route to the product handler.'
Assert-Text $page 'ApplyPendingNativeWritebackForCurrentSection\(\*view, diagnostic\)' `
	'The product handler must apply the active S Section through the native transaction.'
Assert-Text $page 'status \+= diagnostic\.c_str\(\)' `
	'The writeback status must include the native transaction result.'

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

$createControls = Get-FunctionBlock $page 'void CSBattlePreviewPage::CreateEditorControls('
Assert-Text $createControls 'm_uiFont\.CreateFont\(20,[\s\S]*?_T\("Microsoft YaHei"\)\)' `
    'S sidebar controls must use the same 20-pixel Microsoft YaHei font as R preview.'
Assert-Text $createControls 'm_unitParametersButton\.Create\(_T\("默认参数"\)' `
    'The dedicated parameter button must be labeled for future-placement defaults.'
$defaultsHandler = Get-FunctionBlock $pane 'void CBattleUnitParametersPane::OnApplyDefaults('
Assert-Text $defaultsHandler 'm_defaultsHandler\(0x47, profile\)' `
    'The maximum enemy parameter template must be saved once as the shared placement profile.'
$refreshDefaults = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshEditorControls('
Assert-Text $refreshDefaults 'const bool defaultsEnabled = deploymentEnabled;' `
    'The shared future-placement defaults manager must not depend on the active deployment category.'
$openDefaults = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnUnitParametersClicked('
if ($openDefaults -match 'instruction != 0x46|instruction != 0x47|默认参数仅适用于敌军和友军') {
    throw 'The shared defaults manager must remain accessible while another faction is selected.'
}
Assert-Text $openDefaults 'ShowDefaultsState\(ActiveSession\(\)\.editor_state\)[\s\S]*?ShowPane\(true\)' `
    'The defaults button must open the shared profile editor independently from unit selection.'
Assert-Text $header 'CFont\s+m_uiFont' `
    'S controls must retain their shared font for the child-window lifetime.'
Assert-Text $createControls 'm_characterList\.SetFont\(&m_uiFont\)' `
    'The role and brush list must receive the explicit S preview font.'

$layout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutEditorControls('
$globalToolbar = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutPreviewControlToolbar('
$obstacleLayout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutObstacleSidebar('
Assert-Text $layout 'LayoutPreviewControlToolbar\(\);[\s\S]*?if\s*\(m_obstaclePageActive\)' `
	'Global preview controls must be placed before choosing the role or map-tools page.'
Assert-Text $globalToolbar 'm_writebackButton\.MoveWindow\(writebackX, toolbarY[\s\S]*?m_quickModeButton[\s\S]*?m_dynamicPreviewButton[\s\S]*?m_previewModeButton' `
	'Quick mode, dynamic preview, preview display, and writeback stay in the canvas-wide control row.'
if ($layout -match 'm_dynamicPreviewButton\.MoveWindow\(left') {
	throw 'Dynamic preview must not be embedded in the role/brush page.'
}
if ($obstacleLayout -match 'm_dynamicPreviewButton') {
	throw 'Dynamic preview must not be embedded in the map-tools page.'
}
Assert-Text $obstacleLayout 'm_obstaclePageTitle\.MoveWindow[\s\S]*?m_mapOperationStatus\.MoveWindow' `
	'The map-tools page has a separate title above its obstacle instructions.'
if ($layout -match 'm_writebackButton\.MoveWindow\(left \+ undoWidth') {
	throw 'Writeback must no longer share the bottom row with Undo.'
}

$listRefresh = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshCharacterList('
Assert-Text $listRefresh 'character\.data_id\s*<<\s*L"\."\s*<<\s*character\.name' `
    'Character labels must put the stable Data ID before the name.'
Assert-Text $listRefresh '已出场' `
    'Characters already deployed in the current scope must be visibly marked.'
Assert-Text $listRefresh 'brush\.name[\s\S]*?剩余[\s\S]*?remaining' `
    'Brush labels must show remaining capacity instead of the Data ID range.'
Assert-Text $listRefresh 'CurrentDeployedCharacterDataIds\(' `
    'Role occupancy and brush availability must use current-scope deployment records.'

$listUsageRefresh = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshCharacterListIfDeploymentChanged('
Assert-Text $listUsageRefresh 'CurrentDeployedCharacterDataIds\(\)[\s\S]*?!=\s*m_rosterDeployedDataIds[\s\S]*?RefreshCharacterList\(\)' `
    'Occupancy labels must rebuild only when the unique deployed Data ID set changes.'
Assert-Text $header 'm_rosterDeployedDataIds' `
    'S preview must retain the last rendered occupancy set to avoid redundant list resets.'
Assert-Text $opsHeader 'kBattleCharacterPageSize\s*=\s*50' `
    'S role pages must use a shared page size of 50.'
$activateScope = Get-FunctionBlock $page 'bool CSBattlePreviewPage::ActivateConditionScope('
Assert-Text $activateScope 'characters\.page_size\s*=\s*ccz::editor_preview::kBattleCharacterPageSize' `
    'Every newly activated condition branch must use the 50-role page size.'
$stamp = Get-FunctionBlock $page 'void CSBattlePreviewPage::StampAt('
if ($stamp -match 'RefreshCharacterListIfDeploymentChanged\(') {
    throw 'A brush stroke must not rebuild the list for every stamped unit.'
}
$mouseUp = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonUp('
Assert-Text $mouseUp 'RefreshCharacterListIfDeploymentChanged\(\)' `
    'Brush availability must refresh once after the current stamp gesture ends.'
$captureChanged = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnCaptureChanged('
Assert-Text $captureChanged 'interruptedStamp[\s\S]*?RefreshCharacterListIfDeploymentChanged\(\)' `
    'An interrupted brush gesture must refresh remaining counts if it changed deployment occupancy.'

Assert-Text $page 'kStatusHeaderHeight\s*=\s*60' `
    'The normal status header must retain its existing height.'
Assert-Text $page 'kPreviewToolbarHeight\s*=\s*28' `
	'Global preview controls reserve one compact row below the status text.'
Assert-Text $page 'kStatusFooterHeight\s*=\s*26' `
    'The map viewport must reserve the bottom diagnostic strip.'
$layout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutEditorControls('
Assert-Text $layout 'm_canvasRect\.SetRect\(0, headerHeight,[\s\S]*?footerTop\)' `
    'Map drawing and hit testing must use the area below and above the status bands.'
Assert-Text $layout 'kStatusHeaderHeight\s*\+\s*kPreviewToolbarHeight' `
	'Every side-page state reserves the same global preview toolbar row.'
Assert-Text $layout 'LayoutPreviewControlToolbar\(\)' `
	'The preview control row stays available with the sidebar open or collapsed.'
Assert-Text $globalToolbar 'm_writebackButton\.MoveWindow\(writebackX, toolbarY[\s\S]*?controls\.push_back\(\{ &m_quickModeButton[\s\S]*?controls\.push_back\(\{ &m_dynamicPreviewButton[\s\S]*?control\.button->MoveWindow\(x, toolbarY' `
	'The global toolbar owns the preview modes and writeback control.'
Assert-Text $globalToolbar 'const int toolbarY\s*=\s*kStatusHeaderHeight\s*\+\s*3[\s\S]*?const int toolbarHeight\s*=\s*22' `
	'Global preview controls fit inside the reserved toolbar row below the status band.'
Assert-Text $globalToolbar 'm_writebackButton\.ShowWindow\(SW_SHOW\)' `
	'Writeback remains available while either side page is hidden.'
$modeControls = Get-FunctionBlock $page 'void CSBattlePreviewPage::CreateEditorControls()'
Assert-Text $modeControls 'm_quickModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER[\s\S]*?m_copyPersonModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER[\s\S]*?m_unrestrictedTerrainModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER' `
    'All three preview-mode checkboxes must use single-line text with vertical centering.'
if ($modeControls -match 'm_(quickMode|copyPersonMode|unrestrictedTerrainMode)Button\.Create[\s\S]*?BS_MULTILINE') {
    throw 'Preview-mode checkbox captions must not wrap onto multiple lines.'
}
Assert-Text $globalToolbar 'const int toolbarY\s*=\s*kStatusHeaderHeight\s*\+\s*3[\s\S]*?const int toolbarHeight\s*=\s*22' `
	'Global controls fit inside the reserved toolbar row below the status band.'
Assert-Text $globalToolbar 'm_writebackButton\.ShowWindow\(SW_SHOW\)' `
	'Writeback remains visible with the role/brush page open or hidden.'
Assert-Text $page 'CBattlePrimaryActionButton|BS_OWNERDRAW' `
    'Writeback must use a visually distinct primary-action button.'
Assert-Text $page 'RGB\(194,\s*87,\s*33\)' `
    'The primary writeback action must have an accent color distinguishable from ordinary controls.'
$viewport = Get-FunctionBlock $page 'void CSBattlePreviewPage::UpdateViewport('
Assert-Text $viewport 'availableHeight\s*=\s*footerTop\s*-\s*headerHeight' `
    'Vertical scrollbar visibility must use the reserved map viewport height.'
Assert-Text $viewport 'kStatusHeaderHeight\s*\+\s*kPreviewToolbarHeight' `
	'Scrollbar calculations must always account for the global preview toolbar.'
Assert-Text $viewport 'CRect clientBefore;[\s\S]*?ShowScrollBar\([\s\S]*?CRect clientAfter;[\s\S]*?if\s*\(clientBefore\.Width\(\)\s*!=\s*clientAfter\.Width\(\)\s*\|\|\s*clientBefore\.Height\(\)\s*!=\s*clientAfter\.Height\(\)\)[\s\S]*?LayoutEditorControls\(\)' `
    'Map pan must not relayout sidebar children unless scrollbar changes alter the client area.'
$drawCanvas = Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawCanvas('
Assert-Text $drawCanvas 'if\s*\(m_canvasRect\.top\s*>\s*kStatusHeaderHeight\)[\s\S]*?kPreviewToolbarHeight' `
	'The global toolbar background remains visible with either sidebar page open.'
Assert-Text $drawCanvas 'FillRectangle\(&banner, m_canvasRect\.left, 0,[\s\S]*?m_canvasRect\.top' `
    'The top title strip must be drawn outside the clipped map viewport.'
Assert-Text $drawCanvas 'm_canvasRect\.bottom[\s\S]*?visibleDiagnostic' `
    'The diagnostic strip must be drawn below the clipped map viewport.'
Assert-Text $drawCanvas 'SetClip\(Gdiplus::Rect\(m_canvasRect\.left,[\s\S]*?m_canvasRect\.bottom,[\s\S]*?visibleDiagnostic' `
    'Diagnostic text must stay inside the reserved map footer and not overlap the sidebar.'

Assert-Text $page 'battlefield_index' `
    'The hover status must use the separately parsed battlefield number, never the Data ID.'
Assert-Text $drawCanvas 'terrain_names|terrainLabels|terrain_names\[' `
    'The top hover status must resolve the hovered cell terrain name.'
Assert-Text $drawCanvas 'battlefield_index|待分配' `
    'The top hover status must show the unit battle number or an explicit pending value.'
Assert-Text $drawCanvas 'Color\(255,\s*255,\s*80,\s*80\)|Color\(255,\s*100,\s*165,\s*255\)|Color\(255,\s*255,\s*165,\s*55\)' `
    'Hovered unit labels must distinguish own, enemy, and ally colors.'
Assert-Text $header 'm_selectionGestureActive|m_selectionDragStarted' `
    'The S canvas must keep independent transient state for left-drag selection.'
$leftDown = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonDown('
Assert-Text $leftDown 'ToggleSelectionAt\(\*coordinate\)[\s\S]*?ShowSelectionOperationResult\(result\)' `
    'A unit click must toggle selection immediately on mouse-down without refreshing unrelated editor controls.'
$leftUp = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonUp('
if ($leftUp -match 'ToggleSelectionAt|HitTestUnit|RemoveSelectionAt\(\*coordinate\)|AddSelectionAt\(\*coordinate\)') {
    throw 'Mouse-up must not repeat the click hit-test or toggle already handled on mouse-down.'
}
$selectionResult = Get-FunctionBlock $page 'void CSBattlePreviewPage::ShowSelectionOperationResult('
if ($selectionResult -match 'RefreshEditorControls|RefreshSelectedUnitIndex') {
    throw 'Selection updates must not refresh unrelated controls or rescan a primary unit.'
}
$doubleClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonDblClk('
Assert-Text $doubleClick 'coordinate && !occupied[\s\S]*?ClearSelectionGroup\(\)' `
    'A double-click on an empty cell must clear the entire multi-selection group.'
Assert-Text $doubleClick 'coordinate && occupied[\s\S]*?ToggleSelectionAt\(\*coordinate\)[\s\S]*?ShowSelectionOperationResult\(result\)' `
    'A double-click on a unit must process its second click instead of swallowing it.'
$mouseMove = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnMouseMove('
Assert-Text $mouseMove 'BattleSelectionPathCells\(' `
    'Drag selection must interpolate cells so fast pointer movement cannot skip units.'
Assert-Text $mouseMove 'AddSelectionAt\(' `
    'Every unit crossed by the drag path must enter the selection group.'
$rightClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnRButtonDown('
Assert-Text $rightClick 'FilterSelectionByDeployment\(0x4b\)[\s\S]*?FilterSelectionByDeployment\(0x47\)' `
    'The multi-selection submenu must filter by all three deployment factions.'
Assert-Text $rightClick 'RemoveSelectionAt\(' `
    'The multi-selection menu must remove the pointed character from the group.'
Assert-Text $rightClick 'ClearSelectionGroup\(' `
    'The multi-selection menu must expose clear-all selection.'
Assert-Text $rightClick 'DeleteSelectionGroup\(' `
    'The multi-selection menu must expose one-transaction batch deletion.'
Assert-Text $rightClick 'BeginPositionAdjustment\(' `
    'The multi-selection menu must reuse position adjustment for batch moves.'
Assert-Text $rightClick 'kModifyGroupParametersCommand[\s\S]*?OpenBatchUnitParameters\(\)' `
    'The multi-selection menu must expose batch parameter editing.'
Assert-Text $rightClick 'kModifyUnitParametersCommand[\s\S]*?OpenSingleUnitParameters\(cacheId\)' `
    'The single-unit menu must expose parameter editing.'
$batchParameters = Get-FunctionBlock $page 'void CSBattlePreviewPage::OpenBatchUnitParameters('
Assert-Text $batchParameters 'differing_batch_parameter_names\([\s\S]*?if \(!differences\.empty\(\)\)' `
    'The batch warning must be driven by parameter differences within the selected group.'
Assert-Text $batchParameters 'CBatchParameterWarningDialog[\s\S]*?choose_batch_parameters[\s\S]*?selection\.proceed' `
    'The parameter difference warning must offer overwrite, only-uniform, or cancel decisions.'
Assert-Text $page 'IDC_BATTLE_BATCH_OVERWRITE[\s\S]*?BattleBatchWarningChoice::OverwriteAll[\s\S]*?IDC_BATTLE_BATCH_UNIFORM[\s\S]*?BattleBatchWarningChoice::OnlyUniform' `
    'The batch warning buttons must map directly to the two explicit selection policies.'
$parameterPane = $pane
Assert-Text $parameterPane 'const DWORD checkStyle = child \| BS_AUTOCHECKBOX \| BS_VCENTER;' `
    'Every checkbox in the parameter pane must use single-line text with vertical centering.'
Assert-Text $parameterPane 'status\.Format\(L"批量修改 %u 个单位；勾选要修改的参数，其他参数保留原值"' `
    'The visible batch status must explain that checkboxes select which fields are applied.'
Assert-Text $parameterPane 'const int rowContext = top \+ 8;' `
    'The shared parameter forms must not reserve an empty group-caption row above their first control.'
Assert-Text $parameterPane 'm_unitHiddenBatchValue\.Create\(comboStyle[\s\S]*?m_unitReinforcementBatchValue\.Create\(comboStyle' `
    'Batch hidden and reinforcement values must use dropdowns to avoid confusion with apply checkboxes.'
Assert-Text $parameterPane 'm_batchApplyDirection\.Create[\s\S]*?m_batchApplyTarget\.Create' `
    'Batch mode must expose an independent application checkbox for every editable parameter category.'
Assert-Text $parameterPane 'm_batchSelectionPending[\s\S]*?SetCheck\([\s\S]*?m_batchSelectionPending = false' `
    'Initial batch checkbox choices must be applied once and survive later pane refreshes.'
$parameterUpdate = Get-FunctionBlock $pane 'void CBattleUnitParametersPane::UpdateState('
Assert-Text $parameterUpdate 'SelectComboValue\(m_defaultsPolicy, m_defaultsSnapshot\.ai_policy\)' `
    'Default policy must use the passive baseline without synthesizing an unknown -1 option.'
if ($parameterUpdate -match 'SetComboValue\(m_defaultsPolicy') {
    throw 'Default parameters must not add an unknown action-policy choice.'
}
Assert-Text $opsHeader 'struct BattlePlacementDefaults[\s\S]*?int unit_type\{2\}[\s\S]*?int ai_policy\{0\}' `
    'New units must default to high troop tier and passive policy.'
$batchDifference = Get-FunctionBlock $page 'std::wstring differing_batch_parameter_names('
Assert-Text $batchDifference 'const auto first = get_value\(\*units\.front\(\)\)[\s\S]*?get_value\(\*unit\) != first' `
    'Batch warning differences must compare each selected unit with the original baseline value.'
if ($batchDifference -match 'BattleUnitPatch|new_value|proposed') {
    throw 'The batch warning must not compare requested new values with old values.'
}
$applyBatch = Get-FunctionBlock $pane 'void CBattleUnitParametersPane::OnApplyUnit('
if ($applyBatch -match 'usesTarget\s*\|\|\s*batch') {
    throw 'Batch apply must not rewrite hidden, policy-inapplicable target fields.'
}
Assert-Text $applyBatch 'needs_update[\s\S]*?std::any_of\(m_unitSnapshots' `
    'Batch apply must submit only fields that differ from at least one selected unit.'
Assert-Text $applyBatch 'should_apply\(ccz::editor_preview::BattleBatchParameter::Direction\)[\s\S]*?should_apply\(ccz::editor_preview::BattleBatchParameter::Target\)' `
    'Batch apply must honor each parameter checkbox independently.'
if ($applyBatch -match 'if \(batch \|\|') {
    throw 'Uniform batch values must not create dirty fields when every selected unit already has that value.'
}
Assert-Text $applyBatch '\(policy == 3 \|\| policy == 5\)[\s\S]*?patch\.target = target' `
    'Only attack/follow policies may write a target Data ID.'
Assert-Text $applyBatch '\(policy == 4 \|\| policy == 6\)[\s\S]*?patch\.target_x = targetX[\s\S]*?patch\.target_y = targetY' `
    'Only go-to/escape policies may write target coordinates.'
$previewMenu = Get-FunctionBlock $page 'void CSBattlePreviewPage::ShowPreviewModeMenu('
if ($previewMenu -match 'Mode::Direction|L"朝向"') {
    throw 'Facing direction must not be offered as a map overlay.'
}
$drawCanvasForHover = $page
Assert-Text $drawCanvasForHover 'unit\.ai_policy == 3 \|\| unit\.ai_policy == 5' `
    'The hover card must show a Data target only for attack/follow policies.'
Assert-Text $drawCanvasForHover 'unit\.ai_policy == 4 \|\| unit\.ai_policy == 6' `
    'The hover card must show coordinates only for go-to/escape policies.'
$hoverCard = Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawHoveredUnitCard('
if ($hoverCard -match 'unit\.direction|朝向') {
    throw 'Facing direction is already visible in the unit model and must not be duplicated in the hover card.'
}
if ($hoverCard -match 'const int cardHeight = \(std::min\)\(250, canvasHeight - 12\)') {
    throw 'The hover card height must shrink when the displayed parameter list is shorter.'
}
$hoverLinesIndex = $hoverCard.IndexOf('std::vector<std::wstring> lines', [System.StringComparison]::Ordinal)
$hoverHeightIndex = $hoverCard.IndexOf('const int cardHeight', [System.StringComparison]::Ordinal)
if ($hoverLinesIndex -lt 0 -or $hoverHeightIndex -lt 0 -or $hoverHeightIndex -lt $hoverLinesIndex) {
    throw 'The hover card must measure its content before computing its height and placement.'
}
Assert-Text $hoverCard 'lines\.size\(\)[\s\S]*?lineHeight' `
    'The hover card height must be calculated from the number of visible parameter rows.'
$drawCanvas = Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawCanvas('
Assert-Text $drawCanvas 'selection_group' `
    'Every selected unit must have a visible group highlight.'
Assert-Text $drawCanvas 'placement\.units' `
    'Group movement preview must render ghosts for every moved unit.'
Assert-Text $ops 'RebaseBattleEditorDraft[\s\S]*?selection_group' `
    'Quick-mode rebase must preserve group membership by stable unit identity.'

Assert-Text $page 'm_copyPersonModeButton\.Create\(_T\("复制人"\)' `
    'The S sidebar must expose the copy-person special mode.'
Assert-Text $page 'm_unrestrictedTerrainModeButton\.Create\(_T\("无地形限制"\)' `
    'The S sidebar must expose the unrestricted-terrain special mode.'
Assert-Text $opsHeader 'bool copy_person_mode\{\};[\s\S]*?bool unrestricted_terrain_mode\{\};' `
    'Both special modes must default off in each document editor state.'
$copyModeHandler = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnCopyPersonModeClicked('
Assert-Text $copyModeHandler 'SetCopyPersonMode\(enabled\)[\s\S]*?condition_workspaces' `
    'The copy-person toggle must update active operations and cached condition workspaces.'
$terrainModeHandler = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnUnrestrictedTerrainModeClicked('
Assert-Text $terrainModeHandler 'SetUnrestrictedTerrainMode\(enabled\)[\s\S]*?condition_workspaces' `
    'The terrain toggle must update active operations and cached condition workspaces.'
Assert-Text $ops 'if \(state_->copy_person_mode\) return brush\.first_data_id;' `
    'Copy-person mode must make the brush start from its first Data ID again.'
Assert-Text $ops 'if \(!state_->copy_person_mode && IsCurrentUnit' `
    'Copy-person mode must bypass only the duplicate Data ID placement guard.'
Assert-Text $ops 'if \(state_->unrestricted_terrain_mode\) return true;' `
    'Unrestricted-terrain mode must bypass the shared terrain gate.'

$warningDialog = [regex]::Match($page,
    'class CBatchParameterWarningDialog[\s\S]*?\n};').Value
Assert-Text $warningDialog 'DECLARE_MESSAGE_MAP\(\)' `
    'The batch warning dialog must declare an MFC message map.'
$warningMap = [regex]::Match($page,
    'BEGIN_MESSAGE_MAP\(CBatchParameterWarningDialog,[\s\S]*?END_MESSAGE_MAP').Value
Assert-Text $warningMap 'ON_BN_CLICKED\(IDC_BATTLE_BATCH_OVERWRITE[\s\S]*?ON_BN_CLICKED\(IDC_BATTLE_BATCH_UNIFORM' `
    'Both custom warning buttons must dispatch click notifications.'
Assert-Text $page 'void CBatchParameterWarningDialog::OnOverwrite\(\)[\s\S]*?EndDialog\(IDC_BATTLE_BATCH_OVERWRITE\)' `
    'The overwrite button must return the overwrite decision from the modal dialog.'
Assert-Text $page 'void CBatchParameterWarningDialog::OnUniform\(\)[\s\S]*?EndDialog\(IDC_BATTLE_BATCH_UNIFORM\)' `
    'The uniform-only button must return its decision from the modal dialog.'
Assert-Text $page 'void CBatchParameterWarningDialog::OnCancelClicked\(\)[\s\S]*?EndDialog\(IDCANCEL\)' `
    'The warning dialog must preserve cancel-as-no-change behavior.'
Assert-Text $page 'm_quickModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER[\s\S]*?m_copyPersonModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER[\s\S]*?m_unrestrictedTerrainModeButton\.Create[\s\S]*?BS_AUTOCHECKBOX \| BS_VCENTER' `
    'The preview toolbar checkboxes must use single-line text with vertical centering.'
Assert-Text $pane 'const DWORD checkStyle = child \| BS_AUTOCHECKBOX \| BS_VCENTER;' `
    'The parameter pane checkboxes must use single-line text with vertical centering.'
Assert-Text $pane 'CComboBox m_defaultsTarget|m_defaultsTarget\.Create\(comboStyle' `
    'The default target character must be selected from a dropdown.'
Assert-Text $pane 'CComboBox m_unitTarget|m_unitTarget\.Create\(comboStyle' `
    'The unit and batch target character must be selected from a dropdown.'
Assert-Text $pane 'populate_target_choices\(m_defaultsTarget, state\.characters\.entries[\s\S]*?populate_target_choices\(m_unitTarget, state\.characters\.entries' `
    'Target dropdowns must show role names and preserve their Data IDs.'
Assert-Text $paneHeader 'CStatic m_parameterPlaceholders\[7\]' `
    'Single/default modes need one placeholder control for every batch parameter column.'
Assert-Text $pane 'L"— 朝向"[\s\S]*?L"— 目标"[\s\S]*?show\(&placeholder, !batch\)' `
    'Single/default modes must show a noninteractive placeholder in each batch checkbox column.'
Assert-Text $pane 'const int height = 240;' `
    'All parameter modes should use the compact height that fits the aligned controls.'
if ($pane -match 'FormMode::Defaults \? 320') {
    throw 'The defaults panel must not retain the oversized fixed height.'
}
Assert-Text $pane 'footer \+ 28[\s\S]*?m_defaultsGroup\.MoveWindow\(margin, top, right - margin, groupHeight' `
    'The parameter group box must size to its content instead of leaving a large blank area.'
Assert-Text $pane 'm_defaultsDirection\.MoveWindow\(104, row1, 132, 180\)' `
    'Default values must align to the batch direction field.'
if ([regex]::Matches($pane, 'm_unitDirection\.MoveWindow\(104, row1, 132, 180\)').Count -lt 2) {
    throw 'Single-unit and batch direction fields must share the same grid position.'
}
Assert-Text $pane 'label\(0x7FAE, 108, row3, 76\)[\s\S]*?m_unitTarget\.MoveWindow\(188, row3[\s\S]*?label\(0x7FAF, 318, row3, 20\)[\s\S]*?label\(0x7FB0, 462, row3, 20\)' `
    'The 武将, X, and Y target fields must each have explicit labels in the batch row.'

Write-Output 'PASS: S role/brush controls are isolated and wired to temporary BattleEditorOps.'
