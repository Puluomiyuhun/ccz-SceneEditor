$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$pageHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$sessionHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattlePreviewSession.h') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$conditionHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.h') -Raw -Encoding UTF8
$conditionSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.cpp') -Raw -Encoding UTF8
$conditionTests = Get-Content -LiteralPath (Join-Path $root 'cczPreviewCoreTests/tests/battle_condition_tree_tests.cpp') -Raw -Encoding UTF8
$writebackSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp') -Raw -Encoding UTF8
$viewSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$pattern, [string]$label) {
    if ($text -notmatch $pattern) { throw "BATTLE_CONDITION_TREE_FAIL: $label" }
    Write-Output "[PASS] $label"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_CONDITION_TREE_FAIL: missing $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_CONDITION_TREE_FAIL: missing body for $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_CONDITION_TREE_FAIL: unbalanced body for $signature"
}

Assert-Contains $pageHeader 'CConditionTreePane\s+m_conditionTreePane' 'S preview owns the shared condition-tree pane'
Assert-Contains $sessionHeader 'ccz::preview::ReplayOptions\s+condition_options' 'each S document session owns independent condition options'
Assert-Contains $sessionHeader 'std::map<ccz::preview::LogicalNodeKey,[\s\S]*std::unique_ptr<BattlePreviewConditionWorkspace>>\s+condition_workspaces' 'each S document session stores branch-local battle workspaces'
if ($pageSource -match 'm_previewSession') { throw 'BATTLE_CONDITION_TREE_FAIL: S preview depends on the R PreviewSession instance' }
Write-Output '[PASS] S condition choices do not use the R preview session instance'

$treeClick = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::OnConditionTreeClicked()'
Assert-Contains $treeClick 'ActiveSession\(\)\.condition_button_visible' 'condition-tree button is gated by controllable S branches in the active document session'
Assert-Contains $treeClick 'ShowPane\(true\)' 'condition-tree button opens the shared pane'

$actionRefresh = Get-FunctionBlock $pageSource 'LRESULT CSBattlePreviewPage::OnConditionActionMessage('
Assert-Contains $actionRefresh 'BuildBattleConditionTree\(' 'condition actions rebuild the S branch projection'
Assert-Contains $actionRefresh 'ActivateConditionScope\(condition\.scope\)' 'condition actions switch to that branch workspace'
Assert-Contains $conditionSource 'execution_nodes_reach_target|execution_node_reaches_target|execution_case_reaches_target' 'S reachability uses the canonical typed execution model'
Assert-Contains $conditionSource "separator == L'\|'" 'S scope retains legacy pipe-separated synthetic paths'
Assert-Contains $conditionSource "separator == L'/'" 'S scope accepts native slash-separated editor node paths'

 $discard = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::ConfirmDiscardSessionChanges('
Assert-Contains $discard 'session\.condition_workspaces' 'discard check examines all branch workspaces in the target document session'
Assert-Contains $discard 'operations->CanUndo\(' 'discard check detects edits held by inactive branches'

$activate = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::ActivateConditionScope('
Assert-Contains $activate 'previous->second->state\s*=\s*ActiveSession\(\)\.editor_state' 'switching branches saves the outgoing layout in the active session'
Assert-Contains $activate 'ActiveSession\(\)\.editor_state\s*=\s*existing->second->state' 'switching branches restores the incoming layout into the active session'
Assert-Contains $activate 'std::make_unique<ccz::editor_preview::BattleEditorOps>\(ActiveSession\(\)\.editor_state\)' 'each branch retains its own undo object bound to the stable session state'

$writeback = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::BuildPendingWritebackPlan('
Assert-Contains $writeback 'ActiveSession\(\)\.active_condition_scope\.writeback_scope_resolved' 'unresolved condition paths fail closed before writeback planning'
Assert-Contains $writeback 'BattleWritebackReachabilityScope' 'writeback receives the active S branch scope'
Assert-Contains $writeback 'reachable_deployment_keys\s*=\s*ActiveSession\(\)\.active_condition_scope\.reachable_deployment_keys' 'writeback filters by the active branch source keys'
Assert-Contains $writeback '&reachabilityScope' 'writeback passes branch anchors into the snapshot adapter'
Assert-Contains $writeback 'ActiveSession\(\)\.ui\.quick_mode_enabled\s*\?\s*ccz::editor_preview::BattleWritebackRevisionScope::PinnedSection' 'branch writeback keeps Scene1 pinned revision validation'

Assert-Contains $writebackSource 'parent\.has_children' 'native transaction validates a real child-event insertion parent'
if ($writebackSource -match 'parent\.instruction_id\s*!=\s*0') {
    throw 'BATTLE_CONDITION_TREE_FAIL: nested predicate/else event parents are still rejected'
}
$insertView = Get-FunctionBlock $viewSource 'HTREEITEM CcczEditor2View::InsertBattleInstructionAt('
Assert-Contains $insertView 'GetChildItem\(parent\)' 'native TreeView insert accepts a nested event parent only after checking children'
Assert-Contains $insertView 'terminalData->id\s*!=\s*0' 'nested insertion still requires a final 0x00 event terminator'
Assert-Contains $conditionHeader 'reachable_deployment_keys' 'condition scope exports reachable deployment identities'
Assert-Contains $conditionHeader 'std::vector<BattleDeploymentInsertionAnchor>\s+insertion_anchors' 'condition scope exports an insertion destination for each deployment instruction type'
Assert-Contains $conditionTests 'battle_condition_tree_resolves_sample_style_0x05_deployments' 'S_12-style boolean predicates have true/false reachability coverage'
Assert-Contains $conditionTests 'battle_condition_tree_accepts_native_editor_node_path_keys' 'native editor node path regression covers S deployment reachability'
Assert-Contains $conditionTests 'battle_condition_tree_gives_all_deployment_kinds_a_shared_anchor' 'a missing deployment type can use the unique common active child event'
Assert-Contains $conditionTests 'battle_condition_tree_does_not_merge_distinct_deployment_kind_anchors' 'different deployment types retain their own reachable child-event destinations'
Assert-Contains $writebackSource 'anchor\.instruction_id\s*!=\s*instruction_id' 'writeback resolves the insertion destination by command type'
Assert-Contains $writebackSource 'typed_anchor->resolved' 'an unresolved destination blocks only insertion of its matching command type'

Write-Output 'BATTLE_CONDITION_TREE_PASS'
