param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$sessionHeader = Join-Path $root 'cczEditor2\PreviewIntegration\PreviewSession.h'
$sessionSource = Join-Path $root 'cczEditor2\PreviewIntegration\PreviewSession.cpp'
$testSource = Join-Path $root 'tests\preview_integration\preview_session_tests.cpp'
$mainSource = Join-Path $root 'cczPreviewCoreTests\tests\test_main.cpp'
$coreProject = Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj'
$coreLib = Join-Path $root 'cczPreviewCore\Debug\cczPreviewCore.lib'
$outDir = Join-Path $root 'tests\.tmp-preview-session'
$exe = Join-Path $outDir 'preview_session_tests.exe'

foreach ($path in @($sessionHeader, $sessionSource, $testSource, $mainSource)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "PREVIEW_SESSION_TEST_FAIL: missing $path"
    }
}

if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "PREVIEW_SESSION_TEST_FAIL: MSBuild missing: $msbuild"
}

& $msbuild $coreProject /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $coreLib)) {
    throw 'PREVIEW_SESSION_TEST_FAIL: core Debug library build failed'
}

$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "PREVIEW_SESSION_TEST_FAIL: VsDevCmd missing: $vsDevCmd"
}

$args = @(
    '/nologo', '/std:c++17', '/EHsc', '/MTd', '/D_DEBUG',
    '/D_UNICODE', '/DUNICODE', '/DCCZ_PREVIEW_SESSION_TESTING', '/utf-8',
    "/I$($root)\cczPreviewCore\include",
    "/I$($root)\cczPreviewCoreTests\tests",
    "/I$($root)\cczEditor2",
    $testSource, $mainSource, $sessionSource,
    $coreLib, "/Fe:$exe"
)
$command = '"' + $vsDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe ' + ($args -join ' ')
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw 'PREVIEW_SESSION_TEST_FAIL: session test compilation failed'
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw 'PREVIEW_SESSION_TEST_FAIL: session tests failed'
}

'PREVIEW_SESSION_PASS'
