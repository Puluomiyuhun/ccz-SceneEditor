param(
    [string]$EditorRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2'),
    [string]$ReleaseExe = (Join-Path (Join-Path $PSScriptRoot '..') 'Release\s.exe')
)

$ErrorActionPreference = 'Stop'

function Fail-Gate([string]$Message) {
    throw "NO_EXTERNAL_PREVIEW_RUNTIME_FAIL: $Message"
}

if (-not (Test-Path -LiteralPath $EditorRoot -PathType Container)) {
    Fail-Gate "editor source root not found: $EditorRoot"
}

$sourceFiles = @(
    Get-ChildItem -LiteralPath $EditorRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.h', '.hpp', '.cpp', '.c', '.vcxproj', '.vcxproj.filters') }
)
$forbidden = @(
    'cczPreviewExt\.dll',
    'cczPreviewExtLauncher\.exe',
    'CCZPreviewExtInitialize',
    'CCZPreviewExtShutdown',
    'WriteProcessMemory',
    'CreateRemoteThread',
    'update_preview_call_rva',
    'verify_host_profile'
)

foreach ($file in $sourceFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($token in $forbidden) {
        if ($text -match $token) {
            Fail-Gate "$token in $($file.FullName)"
        }
    }
}

if (-not (Test-Path -LiteralPath $ReleaseExe -PathType Leaf)) {
    Fail-Gate "Release EXE not found: $ReleaseExe"
}

$msvcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
if (-not (Test-Path -LiteralPath $msvcRoot -PathType Container)) {
    Fail-Gate "MSVC toolchain root not found: $msvcRoot"
}
$msvc = Get-ChildItem -LiteralPath $msvcRoot -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -eq $msvc) {
    Fail-Gate "MSVC toolchain version directory not found under: $msvcRoot"
}
$dumpbin = Join-Path $msvc.FullName 'bin\Hostx86\x86\dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) {
    Fail-Gate "x86 dumpbin not found: $dumpbin"
}

$imports = & $dumpbin /nologo /imports $ReleaseExe 2>&1
if ($LASTEXITCODE -ne 0) {
    Fail-Gate "dumpbin /imports failed with exit code $LASTEXITCODE for $ReleaseExe"
}
if (($imports -join "`n") -match '(?i)cczPreviewExt(?:Launcher)?(?:\.dll|\.exe)?') {
    Fail-Gate "Release EXE imports or names the external preview runtime"
}

Write-Output ("NO_EXTERNAL_PREVIEW_RUNTIME_PASS: source_files={0}; dumpbin={1}; imports_checked={2}" -f `
    $sourceFiles.Count, $dumpbin, $ReleaseExe)
