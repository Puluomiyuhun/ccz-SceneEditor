param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$header = Join-Path $root 'cczEditor2\PreviewIntegration\PreviewRefreshStateMachine.h'
$source = Join-Path $root 'tests\preview_refresh_state_machine_tests.cpp'
$outDir = Join-Path $root 'tests\.tmp-preview-refresh-state-machine'
$exe = Join-Path $outDir 'preview_refresh_state_machine_tests.exe'

foreach ($path in @($header, $source)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "PREVIEW_REFRESH_STATE_MACHINE_FAIL: missing $path"
    }
}

if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "PREVIEW_REFRESH_STATE_MACHINE_FAIL: VsDevCmd missing: $vsDevCmd"
}

$args = @(
    '/nologo', '/std:c++17', '/EHsc', '/MTd', '/D_DEBUG',
    '/D_UNICODE', '/DUNICODE', '/utf-8',
    "/I$($root)\cczEditor2\PreviewIntegration",
    $source, "/Fe:$exe"
)
$command = '"' + $vsDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe ' + ($args -join ' ')
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw 'PREVIEW_REFRESH_STATE_MACHINE_FAIL: fixture compilation failed'
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw 'PREVIEW_REFRESH_STATE_MACHINE_FAIL: fixture failed'
}

'PREVIEW_REFRESH_STATE_MACHINE_PASS'
