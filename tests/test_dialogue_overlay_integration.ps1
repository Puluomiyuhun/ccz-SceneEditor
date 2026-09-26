param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$sources = @(
    (Join-Path $root 'tests\preview_integration\preview_asset_cache_tests.cpp'),
    (Join-Path $root 'cczPreviewCoreTests\tests\test_main.cpp'),
    (Join-Path $root 'cczEditor2\PreviewIntegration\PreviewAssetCache.cpp'),
    (Join-Path $root 'cczEditor2\PreviewIntegration\DialogueOverlayRenderer.cpp'),
    (Join-Path $root 'cczEditor2\PreviewIntegration\PreviewSession.cpp')
)
$requiredFiles = $sources + @(
    (Join-Path $root 'cczEditor2\PreviewIntegration\PreviewRefreshDecision.h'),
    (Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp')
)
$outDir = Join-Path $root 'tests\.tmp-dialogue-overlay-integration'
$exe = Join-Path $outDir 'dialogue_overlay_integration_tests.exe'
foreach ($path in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "DIALOGUE_OVERLAY_INTEGRATION_FAIL: missing $path"
    }
}
$scenePreviewSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp') -Raw
foreach ($contract in @(
    'DecidePreviewRefreshAction',
    'ClearNativePreviewState',
    'PaintDialogueOverlay'
)) {
    if ($scenePreviewSource.IndexOf($contract, [StringComparison]::Ordinal) -lt 0) {
        throw "DIALOGUE_OVERLAY_INTEGRATION_FAIL: ScenePreviewWnd contract missing: $contract"
    }
}
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "DIALOGUE_OVERLAY_INTEGRATION_FAIL: VsDevCmd missing: $vsDevCmd"
}
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$coreProject = Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj'
$coreLib = Join-Path $root 'cczPreviewCore\Debug\cczPreviewCore.lib'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "DIALOGUE_OVERLAY_INTEGRATION_FAIL: MSBuild missing: $msbuild"
}
& $msbuild $coreProject /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $coreLib)) {
    throw 'DIALOGUE_OVERLAY_INTEGRATION_FAIL: core Debug library build failed'
}
$args = @(
    '/nologo', '/std:c++17', '/EHsc', '/MTd', '/D_DEBUG', '/D_UNICODE',
    '/DUNICODE', '/utf-8',
    "/I$($root)\cczPreviewCore\include",
    "/I$($root)\cczPreviewCoreTests\tests",
    "/I$($root)\cczEditor2",
    "/I$($root)\cczEditor2\PreviewIntegration",
    ($sources -join ' '), $coreLib, "/Fe:$exe"
)
$command = '"' + $vsDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe ' + ($args -join ' ')
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw 'DIALOGUE_OVERLAY_INTEGRATION_FAIL: test compilation failed'
}
& $exe
if ($LASTEXITCODE -ne 0) {
    throw 'DIALOGUE_OVERLAY_INTEGRATION_FAIL: test execution failed'
}
'DIALOGUE_OVERLAY_INTEGRATION_BOUNDARY: Session/decision/geometry executable; OnPaint source contract checked, native window smoke deferred to Task12'
'DIALOGUE_OVERLAY_INTEGRATION_PASS'
