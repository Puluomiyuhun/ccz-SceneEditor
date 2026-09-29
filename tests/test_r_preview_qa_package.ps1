$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$project = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2.vcxproj') -Raw -Encoding UTF8
$page = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$buildScriptPath = Join-Path $repo 'cczEditor2/.tmp-s-battle/all-script-sessions/build-qa.ps1'
if (-not (Test-Path -LiteralPath $buildScriptPath)) {
    throw 'BATTLE_WRITEBACK_PACKAGE_FAIL: the isolated build script is missing'
}
$buildScript = Get-Content -LiteralPath $buildScriptPath -Raw -Encoding UTF8

function Assert-Contract([string]$text, [string]$pattern, [string]$message,
    [switch]$Negate) {
    $matched = $text -match $pattern
    if (($Negate -and $matched) -or (-not $Negate -and -not $matched)) {
        throw "BATTLE_WRITEBACK_PACKAGE_FAIL: $message"
    }
    Write-Output "[PASS] $message"
}

Assert-Contract $project "CCZSBattlePreviewPerf[\s\S]*CCZ_S_BATTLE_PREVIEW_PERF" `
    'performance instrumentation uses a separate opt-in build property'
Assert-Contract $project "CCZ_S_BATTLE_WRITEBACK_QA|CCZSBattleWritebackQa" `
    'product writeback has no QA-only project property' -Negate
Assert-Contract $page 'm_writebackButton\.Create\(_T\("写回"\)' `
    'the standard writeback button is created without a QA-specific caption'
Assert-Contract $buildScript "'/p:CCZSBattlePreviewPerf=true'" `
    'only the diagnostic Debug build enables performance instrumentation'
Assert-Contract $buildScript 'Invoke-ProjectBuild\s+\$editorProject\s+''Release''[\s\S]*?''release-editor\.log''' `
    'the ordinary Release build is emitted without the diagnostic property'
Assert-Contract $buildScript 'Assert-PageCompileMacro\s+\$debugCommands\s+\$true[\s\S]*Assert-PageCompileMacro\s+\$releaseCommands\s+\$false' `
    'the build verifies the actual Debug and Release BattlePreviewPage compile records'
Assert-Contract $buildScript 'Assert-WritebackButtonCaption\s+\$debugExe\s+\$true[\s\S]*Assert-WritebackButtonCaption\s+\$releaseExe\s+\$true' `
    'both Debug and Release binaries are checked for the standard writeback caption'
Assert-Contract $buildScript 'Copy-Item\s+-LiteralPath\s+\$releaseExe\s+-Destination\s+\(Join-Path\s+\$packageBin\s+''s\.exe''\)' `
    'the self-contained test package uses the ordinary Release executable'
Assert-Contract $buildScript "Name -ne '\.ccz-s-battle-writeback-qa'" `
    'the S script fixture is staged without a QA admission marker'
Assert-Contract $buildScript 'Copy-Item\s+-Destination\s+\$scriptPackage' `
    'the packaged S script retains its companion game data and map resources'
Assert-Contract $buildScript 'scenarioIniText[\s\S]*ExePath=.*scenarioExe[\s\S]*UTF8Encoding' `
    'the packaged scenario INI points to its own companion game executable using UTF-8'

Write-Output 'BATTLE_WRITEBACK_PACKAGE_PASS'
