$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$replayPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleDynamicMapReplay.cpp'
$replayHeaderPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattleDynamicMapReplay.h'
$planHeaderPath = Join-Path $repo 'cczPreviewCore/include/ccz/preview/native_replay_plan.h'
$planSourcePath = Join-Path $repo 'cczPreviewCore/src/native_replay_plan.cpp'
$projectPath = Join-Path $repo 'cczEditor2/cczEditor2.vcxproj'
$filtersPath = Join-Path $repo 'cczEditor2/cczEditor2.vcxproj.filters'
$catalogPath = Join-Path $repo 'cczEditor2/PreviewIntegration/EditorPreviewCatalog.cpp'
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$page += Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/PreviewIntegration/BattleEnhancedEditing.inl') -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$replay = Get-Content -LiteralPath $replayPath -Raw -Encoding UTF8
$replayHeader = Get-Content -LiteralPath $replayHeaderPath -Raw -Encoding UTF8
$planHeader = Get-Content -LiteralPath $planHeaderPath -Raw -Encoding UTF8
$planSource = Get-Content -LiteralPath $planSourcePath -Raw -Encoding UTF8
$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
$filters = Get-Content -LiteralPath $filtersPath -Raw -Encoding UTF8
$catalog = Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw "BATTLE_DYNAMIC_UI_FAIL: $message" }
    Write-Output "[PASS] $message"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_DYNAMIC_UI_FAIL: Missing function: $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_DYNAMIC_UI_FAIL: Missing function body: $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_DYNAMIC_UI_FAIL: Unbalanced function: $signature"
}

$refresh = Get-FunctionBlock $page 'bool CSBattlePreviewPage::RefreshDynamicPreview('
$rebuild = Get-FunctionBlock $page 'bool CSBattlePreviewPage::RebuildDynamicProjection()'
$requestRefresh = Get-FunctionBlock $page 'bool CSBattlePreviewPage::RequestPreviewRefresh('
$apply = Get-FunctionBlock $page 'void CSBattlePreviewPage::ApplyDynamicProjection()'
$spriteLoader = Get-FunctionBlock $page 'void CSBattlePreviewPage::EnsureDynamicObstacleSprites()'
$drawCanvas = Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawCanvas('
$leftClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnLButtonDown('
$rightClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnRButtonDown('
$writeback = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnWriteback()'
$layout = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutEditorControls('
$previewToolbar = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutPreviewControlToolbar('
$activationVisibility = Get-FunctionBlock $page 'bool CSBattlePreviewPage::DynamicActivationButtonVisible()'
$activationRefresh = Get-FunctionBlock $page 'void CSBattlePreviewPage::RefreshConditionTree()'
$activationClick = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnActivationButtonClicked()'
$obstacleSidebar = Get-FunctionBlock $page 'void CSBattlePreviewPage::LayoutObstacleSidebar('
$createControls = Get-FunctionBlock $page 'void CSBattlePreviewPage::CreateEditorControls('
$dynamicToggle = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnDynamicPreviewClicked('
$projection = Get-FunctionBlock $replay 'BattleDynamicMapProjection ProjectBattleDynamicMap('

Assert-Text $page 'm_dynamicPreviewButton\.Create' `
    'S preview exposes a single static dynamic-preview toggle.'
Assert-Text $previewToolbar 'controls\.push_back\(\{\s*&m_quickModeButton' `
    'Quick mode remains in the canvas-wide preview controls.'
Assert-Text $previewToolbar 'controls\.push_back\(\{\s*&m_dynamicPreviewButton' `
    'Dynamic preview is a canvas-wide mode control.'
Assert-Text $previewToolbar 'showActivation\s*=\s*DynamicActivationButtonVisible\(\)[\s\S]*if\s*\(showActivation\)[\s\S]*m_activationButton.ShowWindow\(SW_SHOW\)' `
    'The activation shortcut remains in the preview toolbar only for an actionable selected target.'
Assert-Text $activationVisibility 'm_dynamicActivationButtonVisible[\s\S]*ActiveSession\(\)\.activation_button_visible' `
    'Activation shortcut visibility follows the same R-preview reachability contract.'
Assert-Text $activationRefresh 'condition_tree_model_can_activate_target\([\s\S]*DynamicActivationButtonVisible\(\)' `
    'S uses the shared target reachability check to show or hide activation.'
Assert-Text $activationClick 'DynamicActivationButtonVisible\(\)' `
    'S activation only runs when the selected target is currently actionable.'
Assert-Text $layout 'kStatusHeaderHeight\s*\+\s*kPreviewToolbarHeight' `
    'The preview toolbar reserves canvas space whether a side page is open or collapsed.'
$toolbarCall = $layout.IndexOf('LayoutPreviewControlToolbar();', [System.StringComparison]::Ordinal)
$pageLayoutBranch = $layout.IndexOf('if (m_obstaclePageActive)', [System.StringComparison]::Ordinal)
if ($toolbarCall -lt 0 -or $pageLayoutBranch -lt 0 -or $toolbarCall -gt $pageLayoutBranch) {
    throw 'BATTLE_DYNAMIC_UI_FAIL: The global preview controls must be laid out before selecting a sidebar page.'
}
if ($obstacleSidebar -match 'm_dynamicPreviewButton') {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Dynamic preview must not be embedded in the map-tools page.'
}
Assert-Text $createControls 'm_obstaclePageTitle\.Create\(_T\("地图工具 · 战场障碍"\)' `
    'The obstacle tool page has its own visible title.'
Assert-Text $obstacleSidebar 'm_obstaclePageTitle\.MoveWindow' `
    'The map-tools page lays out its own title independently of the role/brush page.'
Assert-Text $obstacleSidebar 'm_obstaclePageButton\.MoveWindow' `
    'The map-tools page remains a top-level sidebar destination.'
if ($dynamicToggle -match 'm_conditionTreePane\.ShowPane\(false\)|m_variableControlPane\.ShowPane\(false\)') {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Dynamic preview must preserve the condition-tree and variable-control panes.'
}
Write-Output '[PASS] Dynamic preview preserves the condition-tree and variable-control panes.'
Assert-Text $dynamicToggle 'm_dynamicPreviewEnabled\s*=\s*false[\s\S]*?RefreshEditorControls\(\)[\s\S]*?UpdateViewport\(\)' `
    'Turning dynamic preview off must restore edit controls from the current editor state.'
Assert-Text $catalog '\{0x4c, L"隐藏武将出现"\}' `
    'Fallback instruction catalog recognizes hidden appearance when CczString.ini is unavailable.'
Assert-Text $catalog '\{0x55,[\s\S]*\{0x58,' `
    'Fallback instruction catalog recognizes revival and obstacle commands when CczString.ini is unavailable.'
Assert-Text $planSource 'if\s*\(!include_battle_map_commands\)\s*return false;[\s\S]*case 0x4c' `
    'The S-only 0x4C command stays outside ordinary R replay plans.'
Assert-Text $requestRefresh 'RefreshDynamicPreview\(tree,\s*selectedItem,\s*documentPath\)' `
    'Changing the TreeView selection rebuilds the snapshot through that selection.'
Assert-Text $refresh 'ResolveScene1BattleSection[\s\S]*SelectCapturedBattleSection[\s\S]*BuildBattlePreviewInput' `
    'The selected Section is paired with a fresh Scene1 baseline capture.'
Assert-Text $refresh 'build_native_battle_map_plan_to_end' `
    'Scene1 baseline uses its complete reachable setup path.'
Assert-Text $planHeader 'build_native_battle_map_plan_through_target' `
    'Core exposes a map plan that stops at the selected instruction.'
Assert-Text $planSource 'build_native_battle_map_plan_through_target[\s\S]*build_native_replay_plan_to_target_with_scope[\s\S]*true' `
    'The selected-prefix plan reuses condition-aware native traversal.'
Assert-Text $refresh 'm_dynamicTargetNode\s*=\s*m_dynamicActivationTarget' `
    'The selected instruction logical key becomes the snapshot endpoint.'
Assert-Text $refresh 'm_dynamicTargetIsSectionRoot\s*=\s*targetCapture\.snapshot\.selected_handle[\s\S]*m_dynamicTargetNode\.clear\(\)' `
    'Selecting a Section root targets that Section result instead of its first event boundary.'
Assert-Text $rebuild 'm_dynamicTargetIsSectionRoot[\s\S]*build_native_battle_map_plan_to_end' `
    'Selecting a Section root computes the reachable result through the Section end.'
Assert-Text $refresh 'm_dynamicScene1Plan\s*=\s*ccz::editor_preview::BuildBattleDynamicMapPlan\([\s\S]*scene1Plan' `
    'Reachable Scene1 move/revival/obstacle operations are retained for later Sections.'
Assert-Text $rebuild 'if\s*\(!m_dynamicTargetIsScene1\)[\s\S]*MergeBattleDynamicMapPlans\([\s\S]*m_dynamicScene1Plan' `
    'Later Sections start from Scene1 operations before applying their own selected prefix.'
Assert-Text $rebuild 'm_dynamicTargetIsSectionRoot\s*\?\s*ccz::preview::build_native_battle_map_plan_to_end[\s\S]*build_native_battle_map_plan_through_target' `
    'Section roots use an end-of-Section plan while instruction selection uses its prefix.'
Assert-Text $rebuild 'm_dynamicPlan\.events\.empty\(\)[\s\S]*m_dynamicPlan\.diagnostic\.empty\(\)[\s\S]*m_dynamicPlan\.diagnostic\s*=' `
    'A no-op message is added only after Scene1 and target Section operations are combined.'
Assert-Text $rebuild 'build_native_battle_map_plan_through_target\([\s\S]*m_dynamicTargetNode' `
    'The result includes reachable supported instructions through the selected node.'
Assert-Text $apply 'ProjectBattleDynamicMap\([\s\S]*m_dynamicPlan\)' `
    'The whole selected prefix is projected once into a single final map snapshot.'
Assert-Text $apply 'BattleDynamicObstacleBaselineForTarget\(\s*m_dynamicTargetIsScene1,\s*m_dynamicScene1Obstacles\)[\s\S]*ProjectBattleDynamicMap' `
    'Scene1 snapshots rebuild obstacles from the initial layout instead of retaining later Scene1 obstacles.'
Assert-Text $drawCanvas 'BattleDynamicObstacleBaselineForTarget\(\s*m_dynamicTargetIsScene1,\s*m_dynamicScene1Obstacles\)[\s\S]*m_dynamicProjection\.obstacles\s*:\s*obstacleBaseline' `
    'Invalid dynamic projections use the same target-specific obstacle baseline as rebuilt snapshots.'
Assert-Text $projection 'for\s*\(const BattleDynamicMapEvent& event : plan\.events\)' `
    'Snapshot projection applies every known operation without a playback cursor.'
Assert-Text $refresh 'reachable_deployment_keys[\s\S]*BuildBattleConditionTree\(targetCapture' `
    'Scene1 deployment and target Section use condition-tree reachability.'
Assert-Text $activationRefresh 'm_dynamicActivationButtonVisible\s*=\s*activationButtonVisible' `
    'Dynamic activation visibility tracks the selected target reachability.'
Assert-Text $refresh 'filter_map_nodes\s*=\s*true[\s\S]*scene1Plan\.items[\s\S]*reachable_map_keys\.push_back' `
    'Scene1 map selection follows only map commands on the chosen condition path.'
Assert-Text $refresh 'ReplayOptions scene1Options\s*=\s*ActiveSession\(\)\.condition_options' `
    'Scene1 baseline follows the S preview condition and Choice selections.'
Assert-Text $spriteLoader 'LoadBitmapFromE5\([\s\S]*?,\s*19,[\s\S]*kObstacleFrames\[\] = \{0, 4, 6\}' `
    'Obstacle snapshots load the confirmed fire, boat, and fireboat frames from U_select.e5.'
Assert-Text $drawCanvas 'm_dynamicObstacleSprites\[type\]\.get\(\)[\s\S]*draw_obstacle_marker\(obstacle.coordinate, obstacle.type, 255\)' `
    'Obstacle thumbnails are drawn by the map renderer.'
Assert-Text $drawCanvas 'm_dynamicPlan\.events\.empty\(\)[\s\S]*m_dynamicTargetIsScene1[\s\S]*else if\s*\(!m_dynamicPlan\.path_complete\)[\s\S]*m_dynamicTargetIsSectionRoot[\s\S]*Section' `
    'The map header distinguishes unchanged, partial, Scene1, and Section snapshots.'
Assert-Text $page 'm_dynamicProjection\.labels[\s\S]*label\.revived\s*\?' `
    'Move and revival outcomes retain their map labels.'
$dynamicLabelsIndex = $drawCanvas.IndexOf(
    'if (dynamicState && !m_dynamicProjection.labels.empty())',
    [System.StringComparison]::Ordinal)
$hoverCardIndex = $drawCanvas.IndexOf(
    'DrawHoveredUnitCard(graphics, *m_hoverCell, m_hoverPoint, cell)',
    [System.StringComparison]::Ordinal)
if ($dynamicLabelsIndex -lt 0 -or $hoverCardIndex -le $dynamicLabelsIndex) {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Hover card must render above dynamic preview labels.'
}
Write-Output '[PASS] The unit hover card renders above dynamic move/revival labels.'
Assert-Text (Get-FunctionBlock $page 'void CSBattlePreviewPage::DrawHoveredUnitCard(') `
    'Color\(dynamic \? 255 : 242,\s*26,\s*28,\s*32\)' `
    'The hover card uses an opaque panel over dynamic labels.'
Assert-Text $projection 'unit\.position = event\.coordinate[\s\S]*event\.kind == BattleDynamicMapEventKind::Revive' `
    'Move and revival update the existing unit record; revival clears hidden state.'
Assert-Text $projection 'HiddenAppear[\s\S]*AreaAndFaction[\s\S]*unit\.hidden = hidden' `
    'S hidden appearance/disappearance changes the existing unit visibility in the map snapshot.'
Assert-Text $leftClick 'if\s*\(m_dynamicPreviewEnabled && !m_dynamicPreviewReady\) return;[\s\S]*SelectBoardCell' `
    'Ready snapshots permit independent board selection and role placement.'
Assert-Text $rightClick '!m_dynamicPreviewEnabled[\s\S]*批量修改参数[\s\S]*kMoveToCommand[\s\S]*kReviveToCommand' `
    'Snapshot mode permits move/revive while initial deployment actions remain guarded.'
Assert-Text $writeback 'm_dynamicPreviewEnabled && \(m_obstacleDrafts.empty\(\) \|\| HasPendingPreviewDrafts\(\)\)' `
    'Snapshot writeback accepts obstacle drafts but never writes initial layout changes.'
Assert-Text $drawCanvas 'if \(projection\) \{\s*// Draft lifetime[\s\S]*m_obstacleDrafts.items\(\)' `
    'Obstacle drafts draw independently of the active sidebar and tool mode.'
Assert-Text $spriteLoader 'm_obstacleAssetsAttempted[\s\S]*SetSprite\(nullptr\)[\s\S]*SetSprite\(m_dynamicObstacleSprites\[index\]\)' `
    'Buttons share the parsed resource and failed loads stay cached until asset revision changes.'
Assert-Text (Get-FunctionBlock $page 'bool CSBattlePreviewPage::StampObstacleAt(') 'occupied[\s\S]*m_obstacleDrafts.Add' `
    'New obstacles reject cells occupied by a projected obstacle or a draft.'
if ($drawCanvas -match 'count > 1|stagedByCoordinate|L"x" \+ std::to_wstring') {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Obstacle stacking counters must be removed.'
}
Assert-Text $requestRefresh 'if\s*\(m_dynamicPreviewEnabled\)[\s\S]*RefreshDynamicPreview\(\s*tree,\s*selectedItem,\s*documentPath\s*\)' `
    'Source changes rebuild the active dynamic snapshot from the current tree.'

$playbackArtifacts = '(?i)(DynamicPlayback|DynamicPlay|DynamicStep|DynamicSpeed|DynamicStatus|DynamicProgress|DynamicPreviewTimer|playback_timeline|m_dynamicReplayCursor|m_dynamicProjectedEventCount|m_dynamicPreviewEnabled\s*\?\s*50)'
if (($page + $header) -match $playbackArtifacts) {
    throw 'BATTLE_DYNAMIC_UI_FAIL: S static snapshot still contains playback UI/state artifacts.'
}
Assert-Text (Get-FunctionBlock $page 'void CSBattlePreviewPage::OnTimer(') 'id!=0xB601[\s\S]*!m_dynamicPreviewEnabled\|\|!m_dynamicPreviewReady' 'Only the approved spell effect timer is handled and it stops outside a ready snapshot.'
Write-Output '[PASS] Static instruction snapshot has no playback timeline; spell animation is separate.'

Assert-Text $project 'PreviewIntegration\\BattleDynamicMapReplay\.cpp' `
    'The editor project compiles the S snapshot projection.'
Assert-Text $filters 'PreviewIntegration\\BattleDynamicMapReplay\.cpp' `
    'The projection implementation is registered in the Visual Studio filters.'

$undo = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnUndo()'
$nativeUndo = Get-FunctionBlock $page 'bool CSBattlePreviewPage::UndoNativeWriteback('
Assert-Text $previewToolbar 'undoX = writebackX[\s\S]*m_undoButton.MoveWindow\(undoX[\s\S]*m_undoButton.ShowWindow\(SW_SHOW\)' `
    'Undo has a reserved global toolbar slot beside writeback in every mode.'
if ($layout -match 'm_undoButton' -or $obstacleSidebar -match 'm_undoButton') {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Sidebar layout must not hide or reposition global undo.'
}
Assert-Text $createControls 'm_undoButton.SetUndoStyle\(\)[\s\S]*m_undoButton.Create\(_T\("↶ 撤回"\), WS_CHILD \| WS_VISIBLE \| BS_OWNERDRAW' `
    'Undo uses a distinct owner-drawn button with an explicit undo symbol.'
Assert-Text $undo 'undo_available\(\)[\s\S]*UndoNativeWriteback[\s\S]*!m_dynamicPreviewEnabled && m_editorOps' `
    'Dynamic mode can undo native transactions without applying initial-layout temporary undo.'
if ($undo -match 'if \(m_dynamicPreviewEnabled\) return;') {
    throw 'BATTLE_DYNAMIC_UI_FAIL: Dynamic undo must not be blocked.'
}
Assert-Text $nativeUndo 'if \(undone\)[\s\S]*CancelBattleMapTool\(true\)[\s\S]*RebaseBattlePreviewAfterNativeWriteback[\s\S]*if \(m_dynamicPreviewEnabled\)[\s\S]*RefreshDynamicPreview' `
    'Successful undo cancels stale ghosts, retains drafts, and explicitly rebuilds the dynamic snapshot.'



Assert-Text $previewToolbar 'controls.push_back\(\{ &m_variableControlButton[\s\S]*actionGroupX[\s\S]*m_activationButton.MoveWindow[\s\S]*controlsBudget' `
    'Activation occupies the right action group adjacent to undo/writeback.'
Assert-Text $createControls 'm_activationButton.SetActivationStyle\(\)[\s\S]*m_activationButton.Create\(_T\("▶ 激活子事件"\), WS_CHILD \| WS_VISIBLE \| BS_OWNERDRAW' `
    'Activation has a dedicated visual style and explicit action symbol.'
$transaction = Get-FunctionBlock $page 'bool CSBattlePreviewPage::ApplyPendingNativeWritebackTransaction('
Assert-Text $transaction 'insertingNewInstructions && !RequireBattleInsertionSelection\(view, diagnostic\)[\s\S]*return false' `
    'Every S insertion checks missing selection and shows the requested prompt before mutation.'

Assert-Text (Get-FunctionBlock $page 'bool RequireBattleInsertionSelection(') 'GetSelectedItem\(\)[\s\S]*AfxMessageBox\(L"请先选择需要插入新指令的位置"[\s\S]*return false' 'Shared missing-selection gate shows a popup before mutation.'
Write-Output 'BATTLE_DYNAMIC_PREVIEW_UI_PASS'
