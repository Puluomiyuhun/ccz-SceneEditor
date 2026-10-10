$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$sessionPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattlePreviewSession.h'
$queuePath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleMapOperations.h'
$queueImplPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleMapOperations.cpp'
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$session = Get-Content -LiteralPath $sessionPath -Raw -Encoding UTF8
$queue = Get-Content -LiteralPath $queuePath -Raw -Encoding UTF8
$queueImpl = Get-Content -LiteralPath $queueImplPath -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw "BATTLE_MAP_UI_FAIL: $message" }
    Write-Output "[PASS] $message"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_MAP_UI_FAIL: Missing function: $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_MAP_UI_FAIL: Missing function body: $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_MAP_UI_FAIL: Unbalanced function: $signature"
}

$create = Get-FunctionBlock $page 'void CSBattlePreviewPage::CreateEditorControls('
$layout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutEditorControls('
$obstacleLayout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutObstacleSidebar('
$obstaclePage = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnObstaclePageClicked('
$obstacleType = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnObstacleTypeClicked('
$leftClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonDown('
$rightClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnRButtonDown('
$keyDown = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnKeyDown('
$captureRoleOperationUnits = Get-FunctionBlock $page 'bool CSBattlePreviewPage::CaptureRoleOperationUnits('
$mouseWheel = Get-FunctionBlock $page 'BOOL CSBattlePreviewPage::OnMouseWheel('
$drawCanvas = Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawCanvas('
$hitTest = Get-FunctionBlock $page 'std::optional<std::size_t> CSBattlePreviewPage::HitTestUnit('
$refreshControls = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshEditorControls()'
$refreshCharacterList = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshCharacterList()'
$refreshCharacterListSelection = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshCharacterListSelection()'
$characterSelectionChanged = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnCharacterSelectionChanged()'
$characterTab = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnCharacterTab('
$brushTab = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnBrushTab('

Assert-Text $header 'CButton\s+m_obstaclePageButton' `
    'Obstacle editing retains its dedicated sidebar page control.'
if ($header -match 'm_roleOperationsButton|OnRoleOperationsClicked') {
    throw 'Move and revive must be accessed through the map right-click menu, not a standalone sidebar button.'
}
Assert-Text $session 'bool\s+obstacle_page_active\{\};' `
    'The independent obstacle page selection belongs to its document session.'
Assert-Text $create 'm_obstaclePageButton\.Create\(_T\("障碍物"\)' `
    'Obstacle editing has a dedicated page entry.'
if ($page -match 'ShowBattleMapOperationsMenu|m_battleMapOperationsButton|地图编辑 ▾') {
    throw 'Move, revive, and obstacles must no longer share the map-edit menu.'
}
Assert-Text $rightClick 'AppendMenu\(roleActionFlags,\s*kMoveToCommand,[\s\S]*AppendMenu\(reviveActionFlags,\s*kReviveToCommand' 'Move and revive are included in the map right-click menu with separate eligibility flags.'
Assert-Text $rightClick 'TrackPopupMenu[\s\S]*kMoveToCommand[\s\S]*kReviveToCommand' `
    'Right-click menu commands dispatch move and revive operations.'
Assert-Text $rightClick 'const bool usableRoleIdentity\s*=\s*contextUnit[\s\S]*const UINT reviveActionFlags\s*=\s*roleActionFlags\s*\|\s*\(usableRoleIdentity\s*\?\s*0\s*:\s*MF_GRAYED\)[\s\S]*AppendMenu\(reviveActionFlags,\s*kReviveToCommand' `
    'Revive is only enabled for a valid role object on the current board.'
Assert-Text $captureRoleOperationUnits 'include_selection_group[\s\S]*state\.selection_group[\s\S]*anchor_index\s*=\s*units\.size\(\)[\s\S]*append_unit\(\*found\)[\s\S]*units\[anchor_index\]\.editor_cache_id\s*!=\s*anchor_cache_id' 'Role operations snapshot the selected group and use the right-clicked selected member as their anchor.'
if ($hitTest -match 'unit\.hidden') {
    throw 'Static hidden state must not prevent selecting the already scheduled role identity for revive.'
}
Assert-Text $rightClick 'command\s*==\s*kMoveToCommand[\s\S]*CaptureRoleOperationUnits\(cacheId,\s*group_context[\s\S]*command\s*==\s*kReviveToCommand[\s\S]*CaptureRoleOperationUnits\(cacheId,\s*group_context' 'Single and multi-select move/revive both capture the map role or selected group.'
Assert-Text $leftClick 'BuildBattleRoleOperationBatch\([\s\S]*m_roleOperationAnchorIndex[\s\S]*ApplyBattleMapOperations\(instructions,\s*diagnostic\)' 'Move and revive translate the full group from the clicked anchor and write one complete instruction batch.'
Assert-Text $leftClick 'ApplyBattleMapOperations\(instructions,\s*diagnostic\)\)\s*CancelBattleMapTool\(true\)' 'Confirming a role operation explicitly preserves staged obstacle drafts.'
Assert-Text $rightClick 'm_battleMapToolMode\s*!=\s*BattleMapToolMode::ObstaclePlace\)\s*\{\s*CancelBattleMapTool\(true\)' 'Right-click cancellation of a role operation explicitly preserves staged obstacle drafts.'
Assert-Text $keyDown 'character\s*==\s*VK_ESCAPE[\s\S]*?m_battleMapToolMode\s*!=\s*BattleMapToolMode::None\)\s*\{\s*CancelBattleMapTool\(true\)' 'Escape cancellation of a role operation explicitly preserves staged obstacle drafts.'
Assert-Text (Get-FunctionBlock $page 'void CSBattlePreviewPage::OnWriteback(') 'm_obstacleDrafts\.Clear\(\);\s*CancelBattleMapTool\(false\)' 'Successful obstacle writeback explicitly discards only the committed drafts.'
Assert-Text $mouseWheel 'choosingRoleOperationTarget[\s\S]*MovePickTarget[\s\S]*RevivePickTarget[\s\S]*m_roleOperationDirection\s*\+\s*notches[\s\S]*m_roleOperationDirectionAdjusted\s*=\s*true[\s\S]*Invalidate\(FALSE\)' 'Move and revive direction changes are staged in the preview and do not mutate units before target confirmation.'
Assert-Text $drawCanvas 'BuildBattleRoleOperationBatch[\s\S]*for\s*\(std::size_t index\s*=\s*0;[\s\S]*targets\[index\][\s\S]*GetUnitSprite' 'Move and revive previews display the translated positions of every group member.'
Assert-Text $leftClick 'const auto directionOverride\s*=\s*m_roleOperationDirectionAdjusted\s*\?\s*std::optional<int>\(m_roleOperationDirection\)' 'Target confirmation forwards staged direction for both move and revive.'
Assert-Text $drawCanvas 'BattleRoleOperationKind::ReviveTo,[\s\S]*?ghost\.direction\s*=\s*m_roleOperationDirection[\s\S]*?GetUnitSprite' 'Revive preview displays the staged facing for every affected role.'
if ($page -match 'unit\+0x0C|runtimePresence|runtime_presence|执行时需不在场') {
    throw 'Preview code must not inspect or imply knowledge of the runtime presence condition.'
}
if ($rightClick -match 'RevivePickCharacter|SetSidebarVisible\(true\)|RefreshCharacterList\(\)') {
    throw 'Revive must not route through the global character roster or require a list selection.'
}
if ($header -match 'RevivePickCharacter' -or $page -match 'RevivePickCharacter') {
    throw 'The separate revive roster-picking mode and its list-selection path must be removed.'
}
if ($rightClick -match 'if\s*\(!hit\)\s*return') {
    throw 'The right-click menu must remain available on empty map cells for revive and source selection.'
}
Assert-Text $layout 'm_showSidebarButton\.MoveWindow\(left, 6, width, 24\);[\s\S]*m_obstaclePageButton\.MoveWindow\(left, 32, width, 22\);' `
    'Role/brush and obstacle page selectors occupy separate dedicated header rows.'
Assert-Text $layout 'const int buttonLeft\s*=\s*\(std::max\)\(0,[\s\S]*?client\.right\) - buttonWidth - 8\);[\s\S]*m_showSidebarButton\.MoveWindow\(buttonLeft, 6, buttonWidth, 24\);[\s\S]*m_obstaclePageButton\.MoveWindow\(buttonLeft, 32, buttonWidth, 22\);[\s\S]*m_obstaclePageButton\.ShowWindow\(SW_SHOW\);' `
    'The standalone obstacle-page entry remains visible below the collapsed role/brush button.'
Assert-Text $layout 'const bool sidePanelVisible\s*=\s*m_sidebarVisible\s*\|\|\s*m_obstaclePageActive;[\s\S]*if\s*\(m_obstaclePageActive\)\s*\{\s*LayoutObstacleSidebar\(client\);\s*\}\s*else if\s*\(m_sidebarVisible\)' `
    'Obstacle visibility reserves its own sidebar and selects a dedicated layout branch.'
Assert-Text $obstacleLayout 'static_cast<CWnd\*>\(&m_characterList\)[\s\S]*control->ShowWindow\(SW_HIDE\)[\s\S]*m_obstacleTypeButtons\[type\]\.MoveWindow[\s\S]*m_obstacleTypeButtons\[type\]\.ShowWindow\(SW_SHOW\)' `
    'The obstacle sidebar hides role-list controls before laying out clickable thumbnails.'
if ($obstacleLayout -match 'm_characterList\.ShowWindow\(SW_SHOW\)|m_searchEdit\.ShowWindow\(SW_SHOW\)|m_deploymentCombo\.ShowWindow\(SW_SHOW\)') {
    throw 'Role-list windows must not overlap the independent obstacle sidebar.'
}
Assert-Text $obstaclePage 'm_obstaclePageActive\s*=\s*true[\s\S]*LayoutEditorControls\(\)' `
    'Opening obstacles immediately activates and lays out its independent page.'
if ($obstaclePage -match 'SetSidebarVisible\(true\)') {
    throw 'Opening the obstacle sidebar must not route through the role-sidebar visibility handler.'
}
if ($layout -match 'm_roleOperationsButton') {
    throw 'The sidebar must not reserve a separate row for move and revive.'
}
Assert-Text $obstacleType 'm_obstaclePageActive\s*=\s*true[\s\S]*BattleMapToolMode::ObstaclePlace' `
    'Selecting any obstacle thumbnail reliably enters placement mode.'
Assert-Text $refreshControls 'const bool obstacleOnlyWriteback\s*=\s*!m_obstacleDrafts\.empty\(\)\s*&&\s*!HasPendingPreviewDrafts\(\);' `
    'Obstacle-only writeback has an explicit path independent of deployment draft resolution.'
Assert-Text $refreshControls 'active_condition_scope\.writeback_scope_resolved\s*\|\|\s*obstacleOnlyWriteback' `
    'Unresolved unit deployment branches do not disable explicit obstacle insertion after selection.'
Assert-Text $leftClick 'ObstaclePlace[\s\S]*m_obstacleStampActive = true[\s\S]*SetCapture[\s\S]*StampObstacleAt' `
    'Obstacle brush starts mouse capture and uses the shared stamp path.'
Assert-Text $page 'StampObstacleAt[\s\S]*m_obstacleDrafts\.Add[\s\S]*UpdateObstacleDraftControls' `
    'Successful stamps update the draft controls.'
Assert-Text $page 'void CSBattlePreviewPage::UpdateObstacleDraftControls[\s\S]*m_mapOperationStatus.SetWindowText[\s\S]*m_clearObstacleDraftsButton.ShowWindow' `
    'Shared draft control refresh maintains status and clear visibility.'
Assert-Text $rightClick 'hasObstacleAt = hasDraftAt \|\| hasNativeAt' `
    'Right-click identifies both staged and projected native obstacles.'
Assert-Text $rightClick 'hasObstacleAt && !contextUnit[\s\S]*kRemoveObstacleCommand[\s\S]*TrackPopupMenu[\s\S]*command == kRemoveObstacleCommand[\s\S]*m_obstacleDrafts\.RemoveAt' `
    'A lone obstacle opens its own context menu and removes only after the menu command is chosen.'
Assert-Text $rightClick 'splitContext = contextUnit\.has_value\(\) && hasObstacleAt[\s\S]*roleTarget = &roleMenu[\s\S]*操作角色[\s\S]*操作障碍物' `
    'A character and obstacle on the same tile first show the requested operation chooser.'
Assert-Text $rightClick 'if \(!m_dynamicPreviewEnabled && splitContext && !group_context[\s\S]*SelectAt\(\*coordinate\)' `
    'Overlapping targets do not select or mutate the character until the user chooses a role action.'
if ($rightClick -match 'if\s*\(m_obstaclePageActive\)') {
    throw 'Right-click menu routing must follow the hit target, not the currently open sidebar page.'
}
Assert-Text $rightClick 'm_mapOperationStatus\.SetWindowText\(status\)[\s\S]*m_clearObstacleDraftsButton\.ShowWindow\(\s*m_obstacleDrafts\.empty\(\)\s*\?\s*SW_HIDE\s*:\s*SW_SHOW\s*\)' `
    'Removing a staged obstacle immediately updates its count and clear control.'
Assert-Text $queue 'bool\s+RemoveAt\(' `
    'The obstacle draft queue exposes a targeted remove operation.'
Assert-Text $queueImpl 'BattleObstacleDraftQueue::RemoveAt\([\s\S]*items_\.erase' `
    'Targeted removal erases a single matching draft.'
Assert-Text $characterTab 'm_obstaclePageActive\s*=\s*false[\s\S]*LayoutEditorControls\(\)' `
    'Returning to the character tab exits the obstacle page and relayouts immediately.'
Assert-Text $brushTab 'm_obstaclePageActive\s*=\s*false[\s\S]*LayoutEditorControls\(\)' `
    'Returning to the brush tab exits the obstacle page and relayouts immediately.'

Write-Output 'BATTLE_MAP_OPERATIONS_UI_PASS'

$mouseMove = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnMouseMove('
$mouseUp = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonUp('
$edit = Get-FunctionBlock $page 'bool CSBattlePreviewPage::EditObstacle('
Assert-Text $mouseMove 'm_obstacleStampActive[\s\S]*BattleSelectionPathCells[\s\S]*StampObstacleAt' 'Held obstacle brush fills the movement path.'
Assert-Text $mouseUp 'm_obstacleStampActive = false[\s\S]*ReleaseCapture' 'Releasing the brush stops stamping and releases capture.'
Assert-Text $rightClick 'kAdjustObstacleCommand[\s\S]*kModifyObstacleCommand[\s\S]*ObstacleAdjust[\s\S]*ShowObstacleParameters' 'Obstacle right-click has position and parameter actions.'
Assert-Text $edit 'source.int_data!=target.source.source_fields[\s\S]*UpdateInstruction[\s\S]*native_writeback_transaction.Apply[\s\S]*RefreshDynamicPreview' 'Native obstacle edits validate the preimage and share transaction/refresh logic.'
Assert-Text $session 'BattleObstacleDefaults obstacle_defaults' 'Obstacle defaults belong to the document session.'

$controlIds = @{}
foreach ($match in [regex]::Matches($page, 'constexpr UINT (\w+Id) = (0x[0-9A-Fa-f]+);')) {
    $value = [Convert]::ToInt32($match.Groups[2].Value.Substring(2),16)
    if ($controlIds.ContainsKey($value)) { throw "Duplicate preview control IDs: $($controlIds[$value]) and $($match.Groups[1].Value)" }
    $controlIds[$value] = $match.Groups[1].Value
}
Write-Output 'BATTLE_PREVIEW_CONTROL_IDS_UNIQUE_PASS'
