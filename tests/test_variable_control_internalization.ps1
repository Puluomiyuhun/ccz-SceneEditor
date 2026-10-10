param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$paneHeader = Join-Path $root 'cczEditor2\PreviewIntegration\VariableControlPane.h'
$paneSource = Join-Path $root 'cczEditor2\PreviewIntegration\VariableControlPane.cpp'
$sceneHeader = Join-Path $root 'cczEditor2\ScenePreviewWnd.h'
$sceneSource = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$sessionHeader = Join-Path $root 'cczEditor2\PreviewIntegration\PreviewSession.h'
$sessionSource = Join-Path $root 'cczEditor2\PreviewIntegration\PreviewSession.cpp'
$project = Join-Path $root 'cczEditor2\cczEditor2.vcxproj'
$filters = Join-Path $root 'cczEditor2\cczEditor2.vcxproj.filters'
$stateHeader = Join-Path $root 'cczPreviewCore\include\ccz\preview\variable_control_state.h'
$stateSource = Join-Path $root 'cczPreviewCore\src\variable_control_state.cpp'
$sessionTests = Join-Path $root 'tests\preview_integration\preview_session_tests.cpp'

foreach ($path in @($paneHeader, $paneSource, $sceneHeader, $sceneSource,
        $sessionHeader, $sessionSource, $project, $filters, $stateHeader,
        $stateSource, $sessionTests)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: missing $path"
    }
}

$utf8 = [Text.Encoding]::UTF8
$headerText = [IO.File]::ReadAllText($paneHeader, $utf8)
$paneText = [IO.File]::ReadAllText($paneSource, $utf8)
$sceneText = [IO.File]::ReadAllText($sceneSource, $utf8)
$sceneHeaderText = [IO.File]::ReadAllText($sceneHeader, $utf8)
$sessionText = [IO.File]::ReadAllText($sessionSource, $utf8)
$sessionHeaderText = [IO.File]::ReadAllText($sessionHeader, $utf8)
$projectText = [IO.File]::ReadAllText($project, $utf8)
$filtersText = [IO.File]::ReadAllText($filters, $utf8)
$stateText = [IO.File]::ReadAllText($stateSource, $utf8)
$sessionTestText = [IO.File]::ReadAllText($sessionTests, $utf8)

foreach ($token in @('FindWindow', 'EnumWindows', 'SetWindowSubclass',
        'GetModuleHandleEx', 'CreateThread', 'cczPreviewExt',
        'WriteProcessMemory', 'CreateRemoteThread')) {
    if ($headerText.Contains($token) -or $paneText.Contains($token)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: forbidden token $token"
    }
}

$trueLabel = [string]([char]0x8bbe + [char]0x4e3a) + 'True'
$falseLabel = [string]([char]0x8bbe + [char]0x4e3a) + 'False'
$integerLabel = [string]([char]0x6574 + [char]0x578b + [char]0x503c)
$assignLabel = [string]([char]0x53d8 + [char]0x91cf + [char]0x8d4b + [char]0x503c)
$resetLabel = [string]([char]0x91cd + [char]0x7f6e + [char]0x4e3a) + '0'
$booleanLabel = [string]([char]0x5e03 + [char]0x5c14 + [char]0x53d8 + [char]0x91cf)
foreach ($contract in @(
        'class CVariableControlPane : public CWnd',
        'using ActionHandler',
        'CreatePane(CWnd* owner',
        'UpdateRows(',
        'ShowPane(bool show)',
        'ScriptVariableIndex',
        'SimulationEnvironment',
        'VariableControlRow',
        'build_variable_control_rows',
        'VariableControlKind',
        'VariableControlAction',
        $integerLabel, $assignLabel, $resetLabel, $booleanLabel,
        $trueLabel, $falseLabel,
        'ES_AUTOHSCROLL', 'LVS_REPORT', 'EN_CHANGE', 'LVN_ITEMCHANGED')) {
    if (-not $headerText.Contains($contract) -and -not $paneText.Contains($contract)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: pane contract missing $contract"
    }
}

if (-not $headerText.Contains('std::vector<ccz::preview::VariableControlRow>') -or
    $headerText.Contains('SimulationEnvironment m_environment') -or
    $headerText.Contains('std::map<std::int32_t') -or
    $paneText.Contains('m_environment')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: pane owns a second variable state'
}

if (-not $paneText.Contains('build_variable_control_rows(') -or
    -not $paneText.Contains('m_searchEdit') -or
    -not $paneText.Contains('m_rows')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: search/filter or row binding is not pane-owned'
}

foreach ($contract in @(
        'frame.variable_index',
        'm_previewSession.options().simulated_variables',
        'UpdateRows(frame.variable_index',
        'ApplyVariableAction',
        'RequestVariableActionRefresh',
        'PostMessage(kVariableActionMessage, 0, 0)')) {
    if (-not $sceneText.Contains($contract) -and -not $sceneHeaderText.Contains($contract)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: ScenePreviewWnd session contract missing $contract"
    }
}

if (-not $sceneText.Contains('PostMessage(kVariableActionMessage, 0, 0) == FALSE') -or
    -not $sceneText.Contains('RequestVariableActionRefresh()')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: failed variable-action post has no refresh fallback'
}

if ($paneText.Contains('MessageBox') -or $paneText.Contains('MessageBeep')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: pane emits more than one diagnostic path'
}
if (-not $sceneText.Contains('diagnostic') -or
    -not $sceneText.Contains('AppendDiagnostic')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: action failure has no single diagnostic path'
}

if (-not $paneText.Contains('wcstoll') -or
    -not $paneText.Contains('*end != L') -or
    -not $paneText.Contains('INT_MIN') -or
    -not $paneText.Contains('INT_MAX')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: integer action is not full-string decimal parsed'
}

foreach ($contract in @(
        'VariableControlAction::SetBooleanTrue',
        'VariableControlAction::SetBooleanFalse',
        'VariableControlAction::SetIntegerValue',
        'VariableControlAction::RestoreIntegerDefault')) {
    if (-not $paneText.Contains($contract)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: typed action missing $contract"
    }
}

if (-not $paneText.Contains('UpdateActionControls') -or
    -not $paneText.Contains('row->kind == ccz::preview::VariableControlKind::Boolean') -or
    -not $paneText.Contains('row->kind == ccz::preview::VariableControlKind::Integer')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: typed row operation controls missing'
}
if (-not $paneText.Contains('SetWindowTextW') -or -not $paneText.Contains('L""')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: empty integer selection is not blank'
}

$headerIndex = $projectText.IndexOf('PreviewIntegration\VariableControlPane.h')
$sourceIndex = $projectText.IndexOf('PreviewIntegration\VariableControlPane.cpp')
if ($headerIndex -lt 0 -or $sourceIndex -lt 0) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: project entries missing'
}
if (-not $filtersText.Contains('PreviewIntegration\VariableControlPane.h') -or
    -not $filtersText.Contains('PreviewIntegration\VariableControlPane.cpp')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: filter entries missing'
}

if (-not $stateText.Contains('build_variable_control_rows') -or
    -not $stateText.Contains('format_variable_control_row')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: pure filtering semantics missing'
}
if (-not $sessionTestText.Contains('3000') -or
    -not $sessionTestText.Contains('condition_tree_predicate_branch_reachable') -or
    -not $sessionTestText.Contains('simulated_variables')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: 3000/shared condition projection fixture missing'
}

# The values remain in PreviewSession::options(), while pane rows are rebuilt
# from the published frame/options after one event-driven refresh.
if (-not $sessionText.Contains('options_ = std::move(candidate)') -or
    -not $sessionText.Contains('apply_variable_control_action(')) {
    throw 'VARIABLE_CONTROL_INTERNALIZATION_FAIL: transactional Session variable action missing'
}

foreach ($contract in @(
        'CVariableControlPane', 'm_variableControlPane',
        'ApplyVariableAction', 'RequestPreviewRefresh', 'OnCreate', 'OnSize',
        'UpdateRows', 'ShowPane')) {
    if (-not $sceneText.Contains($contract) -and -not $sceneHeaderText.Contains($contract)) {
        throw "VARIABLE_CONTROL_INTERNALIZATION_FAIL: ScenePreviewWnd contract missing $contract"
    }
}

'VARIABLE_CONTROL_INTERNALIZATION_PASS'
