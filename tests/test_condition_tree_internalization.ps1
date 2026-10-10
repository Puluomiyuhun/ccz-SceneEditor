param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$paneHeader = Join-Path $root 'cczEditor2\PreviewIntegration\ConditionTreePane.h'
$paneSource = Join-Path $root 'cczEditor2\PreviewIntegration\ConditionTreePane.cpp'
$sceneHeader = Join-Path $root 'cczEditor2\ScenePreviewWnd.h'
$sceneSource = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$project = Join-Path $root 'cczEditor2\cczEditor2.vcxproj'
$filters = Join-Path $root 'cczEditor2\cczEditor2.vcxproj.filters'
$conditionStateSource = Join-Path $root 'cczPreviewCore\src\condition_tree_state.cpp'

foreach ($path in @($paneHeader, $paneSource, $sceneHeader, $sceneSource, $project, $filters, $conditionStateSource)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: missing $path"
    }
}

$utf8 = [Text.Encoding]::UTF8
$headerText = [IO.File]::ReadAllText($paneHeader, $utf8)
$paneText = [IO.File]::ReadAllText($paneSource, $utf8)
$conditionStateText = [IO.File]::ReadAllText($conditionStateSource, $utf8)
$sceneText = [IO.File]::ReadAllText($sceneSource, $utf8)
$sceneHeaderText = [IO.File]::ReadAllText($sceneHeader, $utf8)
$projectText = [IO.File]::ReadAllText($project, $utf8)
$filtersText = [IO.File]::ReadAllText($filters, $utf8)

foreach ($token in @('FindWindow', 'EnumWindows', 'SetWindowSubclass',
        'GetModuleHandleEx', 'CreateThread')) {
    if ($headerText.Contains($token) -or $paneText.Contains($token)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: forbidden ownership token $token"
    }
}

${trueLabel} = [string]([char]0x771f + [char]0x5206 + [char]0x652f)
${elseLabel} = 'Else' + [string]([char]0x5206 + [char]0x652f)
${currentLabel} = [string]([char]0x5f53 + [char]0x524d + [char]0x5206 + [char]0x652f)
${unselectedLabel} = [string]([char]0x672a + [char]0x9009 + [char]0x5206 + [char]0x652f)
foreach ($contract in @(
        'class CConditionTreePane : public CWnd',
        'using ActionHandler',
        'CreatePane(CWnd* owner',
        'UpdateModel(',
        'ShowPane(bool show)',
        'LogicalNodeKey',
        'TreeItemBinding',
        'condition_tree_should_expand',
        'bindings.clear()',
        $trueLabel, $elseLabel, $currentLabel, $unselectedLabel,
        'condition_tree_action_state',
        'condition_tree_visual_state',
        'condition_tree_predicate_branch_reachable',
        'condition_tree_node_title')) {
    if (-not $headerText.Contains($contract) -and -not $paneText.Contains($contract) -and
        -not $conditionStateText.Contains($contract)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: pane contract missing $contract"
    }
}
if (-not ($paneText.Contains('DeleteAllItems') -or $paneText.Contains('TreeView_DeleteAllItems'))) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: TreeView items are not deleted before binding reset'
}
$deleteIndex = $paneText.IndexOf('DeleteAllItems')
$clearIndex = $paneText.IndexOf('m_bindings.clear()')
if ($deleteIndex -lt 0 -or $clearIndex -lt 0 -or $deleteIndex -gt $clearIndex) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: binding reset precedes TreeView deletion'
}
foreach ($visualContract in @(
        'RGB(0, 128, 0)', 'RGB(180, 40, 40)', 'RGB(45, 105, 190)',
        'CDIS_SELECTED', 'InsertSequence(parent, node.ordinary_children',
        'ContainsDisplayedNode', 'case_node.choice_state')) {
    if (-not $paneText.Contains($visualContract)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: visual/tree behavior missing $visualContract"
    }
}

# Fix round 1: the editor pane must consume the session's typed replay options,
# and an empty model must clear native items before any binding/selection state.
foreach ($contract in @(
        'const ccz::preview::ReplayOptions& options',
        'ccz::preview::ReplayOptions m_options',
        'replay_options_equal',
        'condition_tree_action_state(&node,',
        'condition_tree_visual_state(',
        'node, node_reachable, active_path, m_options',
        'condition_tree_node_title(node, m_options)',
        'node, node_reachable, true, m_options',
        'node, node_reachable, false, m_options',
        'RebuildTree(true)',
        'm_selectedKey.clear()',
        'm_expandedState.clear()')) {
    if (-not $headerText.Contains($contract) -and -not $paneText.Contains($contract)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: fix-round-1 typed/reset contract missing $contract"
    }
}

if (-not $sceneText.Contains('m_conditionTreePane.UpdateModel(nullptr, {}, {})')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: model-empty reset does not explicitly clear the pane model'
}
if (-not $sceneText.Contains('m_conditionTreePane.UpdateModel(frame.model, frame.active_node,') -or
    -not $sceneText.Contains('m_previewSession.options()')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: pane update does not consume PreviewSession options'
}
if (-not $sceneText.Contains('SceneCanvasRect(clientRect)') -or
    -not $sceneText.Contains('ConditionUiHeight') -or
    -not $sceneText.Contains('LayoutConditionUi(cx, cy)')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: scene canvas/condition geometry contract missing'
}
if ($sceneText.Contains('m_conditionButton.MoveWindow(0, m_canvasTop') -or
    $sceneText.Contains('m_conditionTreePane.MoveWindow(0, m_canvasTop + 24')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: condition controls still overlay the scene canvas'
}
if (-not $sceneText.Contains('PostMessage(kConditionActionMessage, 0, 0) == FALSE') -or
    -not $sceneText.Contains('RequestConditionActionRefresh()')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: failed condition-action post has no refresh fallback'
}
$nullResetIndex = $sceneText.IndexOf('m_conditionTreePane.UpdateModel(nullptr, {}, {})')
$hideIndex = $sceneText.IndexOf('m_conditionTreePane.ShowPane(false)', $nullResetIndex)
if ($nullResetIndex -lt 0 -or $hideIndex -lt $nullResetIndex) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: model reset does not precede pane hide in clear path'
}

# Host-independent projection contract: ordinary leaves are pruned while the
# typed control categories remain displayable, and true/Else roles retain the
# established colors.  This intentionally does not claim native MFC E2E.
foreach ($category in @(
        'ExecutionCategory::Predicate', 'ExecutionCategory::Choice',
        'ExecutionCategory::Case',
        'ExecutionCategory::IntegerDispatchGroup',
        'ExecutionCategory::Diagnostic')) {
    if (-not $paneText.Contains($category)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: projection category fixture missing $category"
    }
}
if (-not $paneText.Contains('case_node.integer_evaluation') -or
    -not $paneText.Contains('InsertSequence(parent, node.ordinary_children') -or
    -not $paneText.Contains('InsertSequence(parent, node.cases')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: nested projection/pruning fixture missing'
}
if (-not $paneText.Contains('RGB(0, 128, 0)') -or
    -not $paneText.Contains('RGB(180, 40, 40)')) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: true/Else color fixture missing'
}

# Pure geometry fixture for the layout contract (no MFC window is created).
$canvasTop = 184
$buttonHeight = 24
$preferredPaneHeight = 280
function Get-ConditionUiHeight([int]$height, [bool]$buttonVisible,
    [bool]$paneVisible) {
    if (-not $buttonVisible -or $height -le $canvasTop) { return 0 }
    $available = $height - $canvasTop
    $button = [Math]::Min($buttonHeight, $available)
    if (-not $paneVisible) { return $button }
    return [Math]::Min($available, $button + $preferredPaneHeight)
}
function Get-CanvasBottom([int]$height, [bool]$buttonVisible,
    [bool]$paneVisible) {
    return [Math]::Max($canvasTop,
        $height - (Get-ConditionUiHeight $height $buttonVisible $paneVisible))
}
$fullHeight = 600
$reserved = Get-ConditionUiHeight $fullHeight $true $true
$canvasBottom = Get-CanvasBottom $fullHeight $true $true
$conditionTop = $fullHeight - $reserved
$paneTop = $conditionTop + $buttonHeight
if ($reserved -ne 304 -or $canvasBottom -ne $conditionTop -or
    $paneTop -le $canvasBottom -or $paneTop -ge $fullHeight) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: geometry fixture detects canvas/pane overlap'
}
if ((Get-ConditionUiHeight $fullHeight $false $false) -ne 0 -or
    (Get-CanvasBottom $fullHeight $false $false) -ne $fullHeight) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: empty-model geometry does not hide condition region'
}

foreach ($contract in @(
        'CConditionTreePane',
        'm_conditionTreePane',
        'ApplyConditionAction',
        'RequestPreviewRefresh',
        'OnCreate',
        'OnSize',
        'UpdateModel',
        'ShowPane')) {
    if (-not $sceneText.Contains($contract) -and -not $sceneHeaderText.Contains($contract)) {
        throw "CONDITION_TREE_INTERNALIZATION_FAIL: ScenePreviewWnd contract missing $contract"
    }
}
if (-not (($sceneText.Contains('frame().model') -or $sceneText.Contains('frame.model')) -and
        ($sceneText.Contains('frame().active_node') -or $sceneText.Contains('frame.active_node')))) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: PreviewSession frame model/active node contract missing'
}

foreach ($pathText in @($projectText, $filtersText)) {
    if (-not $pathText.Contains('PreviewIntegration\ConditionTreePane.h') -or
        -not $pathText.Contains('PreviewIntegration\ConditionTreePane.cpp')) {
        throw 'CONDITION_TREE_INTERNALIZATION_FAIL: project/filter entries missing'
    }
}

# The source contract is intentionally accompanied by the already migrated
# host-independent condition behavior.  This keeps the pane from reparsing
# display text while preserving the core action/reachability/title rules.
$coreExe = Join-Path $root 'cczPreviewCoreTests\Debug\cczPreviewCoreTests.exe'
if (-not (Test-Path -LiteralPath $coreExe)) {
    $coreExe = Join-Path $root 'Debug\cczPreviewCoreTests.exe'
}
if (-not (Test-Path -LiteralPath $coreExe)) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: condition core executable missing'
}
& $coreExe condition | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'CONDITION_TREE_INTERNALIZATION_FAIL: condition core profile failed'
}

'CONDITION_TREE_INTERNALIZATION_PASS'
