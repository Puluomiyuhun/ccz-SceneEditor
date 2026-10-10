param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$coreProject = Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj'
$coreLib = Join-Path $root 'cczPreviewCore\Debug\cczPreviewCore.lib'
$mainSource = Join-Path $root 'cczPreviewCoreTests\tests\test_main.cpp'
$timelineSource = Join-Path $root 'tests\preview_integration\playback_timeline_tests.cpp'
$sessionSource = Join-Path $root 'tests\preview_integration\dynamic_playback_session_tests.cpp'
$interpolationSource = Join-Path $root 'tests\preview_integration\dynamic_scene_interpolation_tests.cpp'
$dynamicSessionSource = Join-Path $root 'cczEditor2\PreviewIntegration\DynamicPlaybackSession.cpp'
$outDir = Join-Path $root 'tests\.tmp-dynamic-preview'
$exe = Join-Path $outDir 'dynamic_preview_tests.exe'

# Compile the actual timeline construction statements used by the play button.
$production = Get-Content (Join-Path $root 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding utf8
$playStart = $production.IndexOf('bool CRScenePreviewPage::StartDynamicPlayback(')
$buildStart = $production.IndexOf('m_dynamicTimeline = ccz::preview::build_playback_timeline(', $playStart)
$buildEnd = $production.IndexOf('if (!m_dynamicTimeline.complete)', $buildStart)
$liveBuild = $production.Substring($buildStart, $buildEnd-$buildStart)
$timelineFixture = Get-Content $timelineSource -Raw -Encoding utf8
$generatedTimeline = Join-Path $outDir 'live_playback_timeline_tests.cpp'
$timelineFixture = $timelineFixture.Replace('// PRODUCTION_START_TIMELINE', $liveBuild)

foreach ($path in @($mainSource, $timelineSource, $sessionSource,
        $interpolationSource, $dynamicSessionSource)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "DYNAMIC_PREVIEW_TEST_FAIL: missing $path"
    }
}
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}
Set-Content $generatedTimeline $timelineFixture -Encoding utf8

$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $msbuild) -or
    -not (Test-Path -LiteralPath $vsDevCmd)) {
    throw 'DYNAMIC_PREVIEW_TEST_FAIL: Visual C++ build tools missing'
}

& $msbuild $coreProject /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $coreLib)) {
    throw 'DYNAMIC_PREVIEW_TEST_FAIL: core Debug library build failed'
}

$args = @(
    '/nologo', '/std:c++17', '/EHsc', '/MTd', '/D_DEBUG',
    '/D_UNICODE', '/DUNICODE', '/utf-8',
    "/I$($root)\cczPreviewCore\include",
    "/I$($root)\cczPreviewCoreTests\tests",
    "/I$($root)\cczEditor2",
    $generatedTimeline, $sessionSource, $interpolationSource,
    $dynamicSessionSource, $mainSource, $coreLib, "/Fe:$exe"
)
$command = '"' + $vsDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe ' + ($args -join ' ')
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw 'DYNAMIC_PREVIEW_TEST_FAIL: focused test compilation failed'
}
& $exe
if ($LASTEXITCODE -ne 0) {
    throw 'DYNAMIC_PREVIEW_TEST_FAIL: focused tests failed'
}

'DYNAMIC_PREVIEW_PASS'
