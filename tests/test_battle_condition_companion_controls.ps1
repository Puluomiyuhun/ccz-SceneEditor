$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$pageHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$sessionHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattlePreviewSession.h') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$pattern, [string]$label) {
    if ($text -notmatch $pattern) { throw "BATTLE_CONDITION_COMPANIONS_FAIL: $label" }
    Write-Output "[PASS] $label"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_CONDITION_COMPANIONS_FAIL: missing $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_CONDITION_COMPANIONS_FAIL: missing body for $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_CONDITION_COMPANIONS_FAIL: unbalanced body for $signature"
}

Assert-Contains $pageHeader '#include "PreviewIntegration/VariableControlPane.h"' 'S page reuses the shared R variable pane'
Assert-Contains $pageHeader 'CButton\s+m_activationButton' 'S page owns the quick activation control'
Assert-Contains $pageHeader 'CButton\s+m_variableControlButton' 'S page owns the variable control button'
Assert-Contains $pageHeader 'CVariableControlPane\s+m_variableControlPane' 'S page owns the shared variable pane'
Assert-Contains $sessionHeader 'ScriptVariableIndex\s+condition_variable_index' 'variable index is stored per S document session'
Assert-Contains $sessionHeader 'condition_activation_target' 'activation target is stored per S document session'
Assert-Contains $sessionHeader 'activation_button_visible' 'activation shortcut visibility follows whether the current selected event can be activated'

$load = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::LoadCapturedBattle('
Assert-Contains $load 'scan_script_variables\(' 'loading a document indexes variables from its captured nodes'
Assert-Contains $load 'synchronize_boolean_defaults\(' 'loading synchronizes the boolean variable scope'
Assert-Contains $load 'synchronize_integer_scope\(' 'loading synchronizes the integer variable scope'
Assert-Contains $load 'UpdateConditionActivationTarget\(' 'loading resolves the currently selected S node for activation'

$refresh = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::RefreshConditionTree('
Assert-Contains $refresh 'UpdateRows\(' 'the variable pane receives the current session variable snapshot'
Assert-Contains $refresh 'condition_tree_model_can_activate_target\(' 'activation visibility uses the same canonical reachability check as R preview'
Assert-Contains $refresh 'activation_button_visible\s*=\s*activationButtonVisible' 'activation shortcut visibility follows whether the selected event is actionable'
Assert-Contains $refresh 'm_activationButton\.ShowWindow\(DynamicActivationButtonVisible\(\)' 'the S toolbar shows the shortcut only for an actionable target'

$activate = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::OnActivationButtonClicked()'
Assert-Contains $activate 'activate_condition_tree_target\(' 'S activation reuses the canonical condition-tree activation plan'
Assert-Contains $activate 'PostMessage\(kConditionActionMessage' 'activation feeds the existing S branch refresh transaction'

$variable = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::ApplyVariableActionAndRefresh('
Assert-Contains $variable 'apply_variable_control_action\(' 'S variable changes use shared variable action semantics'
Assert-Contains $variable 'PostMessage\(kConditionActionMessage' 'variable changes rebuild S reachability and the active branch'

$normalRefresh = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::RequestPreviewRefresh('
Assert-Contains $normalRefresh 'UpdateConditionActivationTarget\(\s*capture\.selected_target_key\)' 'normal mode passes the selected logical key to activation refresh'
Assert-Contains $normalRefresh 'BuildBattleConditionTree\(\s*capture,[\s\S]*?condition_model\s*=\s*selectedCondition\.model;[\s\S]*?RefreshConditionTree\(\)' 'same-Section selection rebuilds and refreshes the condition model before checking activation'
$quickRefresh = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh('
Assert-Contains $quickRefresh 'find_battle_node_key_by_runtime_handle\([\s\S]*ActiveSession\(\)\.baseline_capture\.input\.nodes[\s\S]*tree\.GetSelectedItem\(\)' 'quick mode resolves the live selection against the captured source tree without searching the execution model'
$activationTarget = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::UpdateConditionActivationTarget('
Assert-Contains $activationTarget 'const ccz::preview::LogicalNodeKey& target_key' 'activation refresh receives an R-style logical node key'
Assert-Contains $activationTarget 'condition_tree_model_can_activate_target\(' 'activation refresh evaluates the selected key against the current model'

$preserveDraft = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::LoadQuickModeBaselinePreservingDraft('
Assert-Contains $preserveDraft 'oldConditionVariableIndex' 'failed quick-mode rebase snapshots the old variable index'
Assert-Contains $preserveDraft 'condition_variable_index\s*=\s*std::move\(oldConditionVariableIndex\)' 'failed quick-mode rebase restores the document-owned variable index'
Assert-Contains $preserveDraft 'condition_activation_target\s*=\s*\s*std::move\(oldConditionActivationTarget\)' 'failed quick-mode rebase restores the prior activation target'

$toolbar = Get-FunctionBlock $pageSource 'void CSBattlePreviewPage::LayoutPreviewControlToolbar('
Assert-Contains $toolbar 'DynamicActivationButtonVisible\(\)[\s\S]*m_activationButton' 'preview toolbar exposes activation only when the selected target is actionable'
Assert-Contains $toolbar 'DynamicVariableButtonVisible\(\)[\s\S]*m_variableControlButton' 'preview toolbar exposes variable control when available'

Write-Output 'BATTLE_CONDITION_COMPANIONS_PASS'
