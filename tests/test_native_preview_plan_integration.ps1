param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$sceneHeader = Join-Path $root 'cczEditor2\ScenePreviewWnd.h'
$sceneSource = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$testSource = Join-Path $root 'tests\preview_integration\native_plan_application_tests.cpp'
$mainSource = Join-Path $root 'cczPreviewCoreTests\tests\test_main.cpp'
$coreProject = Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj'
$coreLib = Join-Path $root 'cczPreviewCore\Debug\cczPreviewCore.lib'
$outDir = Join-Path $root 'tests\.tmp-native-plan-application'
$exe = Join-Path $outDir 'native_plan_application_tests.exe'

foreach ($path in @($sceneHeader, $sceneSource, $testSource, $mainSource)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: missing $path"
    }
}

$utf8 = [Text.Encoding]::UTF8
$headerText = [IO.File]::ReadAllText($sceneHeader, $utf8)
$sceneText = [IO.File]::ReadAllText($sceneSource, $utf8)

# These source contracts make the RED phase fail before the direct native path
# exists, while the executable below exercises handle/ID validation with real
# NativeReplayPlan values rather than merely searching for those words.
foreach ($contract in @(
        'struct NativePlanApplyResult',
        'ApplyNativeReplayPlan(',
        'HideInternalizedEnhancementViews(',
        'const auto& frame = m_previewSession.frame()',
        'frame.native_plan',
        'if (!apply.applied)',
        'EvaluateToItem(tree, hSelectedItem)',
        'HideInternalizedEnhancementViews(apply.diagnostic)',
        'TreeView_GetItem',
        'tree.GetItemData',
        'entry.command.instruction_id',
        'ProcessInstruction(data, item)',
        'PrepareSpecialScenePrefix(',
        'ResolveScenePreview(',
        'ProcessSceneToSection',
        'ProcessSceneAll',
        'm_limitedFreeRReplay',
        'm_conditionTreePane.UpdateModel(nullptr, {}, {})',
        'm_variableControlPane.UpdateRows({}, {}, {})',
        'm_visibleDialogueOverlay = {}')) {
    if (-not $headerText.Contains($contract) -and -not $sceneText.Contains($contract)) {
        throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: direct-plan contract missing $contract"
    }
}

foreach ($requiredCase in @(
        'case 39:', 'case 41:', 'case 42:', 'case 43:',
        'ProcessInstruction(data, item)')) {
    if (-not $sceneText.Contains($requiredCase)) {
        throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: existing ProcessInstruction path missing $requiredCase"
    }
}

$applyStart = $sceneText.IndexOf(
    'NativePlanApplyResult CRScenePreviewPage::ApplyNativeReplayPlan(')
$applyEnd = $sceneText.IndexOf(
    'void CRScenePreviewPage::HideInternalizedEnhancementViews(', $applyStart)
if ($applyStart -lt 0 -or $applyEnd -le $applyStart) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: ApplyNativeReplayPlan block missing'
}
$applyBlock = $sceneText.Substring($applyStart, $applyEnd - $applyStart)
if ($applyBlock.Contains('EvaluateToItem(')) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: valid-plan applier calls legacy EvaluateToItem'
}
$prefixIndex = $applyBlock.IndexOf('PrepareSpecialScenePrefix(')
$replayIndex = $applyBlock.IndexOf('ProcessInstruction(entry.data, entry.item)')
if ($prefixIndex -lt 0 -or $replayIndex -le $prefixIndex) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: special prefix is not composed before direct plan replay'
}
$unresolvedIndex = $applyBlock.IndexOf(
    'SpecialScenePrefixResult::SpecialUnresolved')
$unresolvedReturnIndex = $applyBlock.IndexOf('return result;', $unresolvedIndex)
if ($unresolvedIndex -lt 0 -or $unresolvedReturnIndex -le $unresolvedIndex -or
    $replayIndex -le $unresolvedReturnIndex) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: unresolved special prefix can replay current Section'
}
if (-not $applyBlock.Contains('tree.GetParentItem(hSection)')) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: current TreeView section lookup missing'
}

$prefixStart = $sceneText.IndexOf(
    'CRScenePreviewPage::PrepareSpecialScenePrefix(')
$prefixEnd = $sceneText.IndexOf(
    'bool CRScenePreviewPage::EvaluateSpecialScene(', $prefixStart)
if ($prefixStart -lt 0 -or $prefixEnd -le $prefixStart) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: special prefix helper block missing'
}
$prefixBlock = $sceneText.Substring($prefixStart, $prefixEnd - $prefixStart)
foreach ($prefixContract in @(
        'ResolveScenePreview(', 'ProcessSceneAll(',
        'm_limitedFreeRReplay', 'SpecialUnresolved', 'PrefixApplied')) {
    if (-not $prefixBlock.Contains($prefixContract)) {
        throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: special prefix contract missing $prefixContract"
    }
}
if (-not $prefixBlock.Contains('ProcessSceneAll(tree, initialScene)')) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: Free-R initial snapshot does not replay to final Section state'
}
if ($prefixBlock.Contains('ProcessSceneSection(tree, hSection')) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: special prefix replays current Section twice'
}
$evaluateStart = $sceneText.IndexOf(
    'bool CRScenePreviewPage::EvaluateSpecialScene(')
$evaluateEnd = $sceneText.IndexOf(
    'void CRScenePreviewPage::EvaluateToItem(', $evaluateStart)
if ($evaluateStart -lt 0 -or $evaluateEnd -le $evaluateStart) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: EvaluateSpecialScene block missing'
}
$evaluateBlock = $sceneText.Substring($evaluateStart, $evaluateEnd - $evaluateStart)
if (-not $evaluateBlock.Contains('SpecialScenePrefixResult::SpecialUnresolved') -or
    -not $evaluateBlock.Contains('ProcessSceneSection(tree, hSection')) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: unresolved special-scene boundary is not preserved'
}

if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: MSBuild missing: $msbuild"
}
& $msbuild $coreProject /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $coreLib)) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: core Debug library build failed'
}
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: VsDevCmd missing: $vsDevCmd"
}
$args = @(
    '/nologo', '/std:c++17', '/EHsc', '/MTd', '/D_DEBUG', '/D_UNICODE',
    '/DUNICODE', '/utf-8',
    "/I$($root)\cczPreviewCore\include",
    "/I$($root)\cczPreviewCoreTests\tests",
    $testSource, $mainSource, $coreLib, "/Fe:$exe"
)
$command = '"' + $vsDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe ' + ($args -join ' ')
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: test compilation failed'
}
& $exe
if ($LASTEXITCODE -ne 0) {
    throw 'NATIVE_PREVIEW_PLAN_INTEGRATION_FAIL: runtime contract tests failed'
}

$passCount = ([IO.File]::ReadAllText($testSource, $utf8) | Select-String -Pattern 'CCZ_TEST\(' -AllMatches).Matches.Count
"NATIVE_PREVIEW_PLAN_INTEGRATION_PASS: $passCount runtime contract tests; MFC window smoke deferred to Task 12"
