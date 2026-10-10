$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPage = Get-Content (Join-Path $taskRoot 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$taskStart = $taskPage.IndexOf('bool RevealNativeWritebackInstruction(')
$taskEnd = $taskPage.IndexOf('// An insertion needs a selected instruction', $taskStart)
if ($taskStart -lt 0 -or $taskEnd -lt 0) { throw 'Missing production selection helper' }
$taskHelper = $taskPage.Substring($taskStart, $taskEnd - $taskStart)
$taskTemplate = Get-Content (Join-Path $PSScriptRoot 'preview_integration/battle_writeback_selection_tests.cpp') -Raw -Encoding UTF8
$taskBuild = Join-Path $taskRoot '.tmp-native-selection-test'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp = Join-Path $taskBuild 'selection.cpp'
$taskExe = Join-Path $taskBuild 'selection.exe'
Set-Content -LiteralPath $taskCpp -Encoding UTF8 -Value $taskTemplate.Replace('// PRODUCTION_HELPER', $taskHelper)
$taskDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand = '"' + $taskDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 "' + $taskCpp + '" /Fe:"' + $taskExe + '" /Fo:"' + (Join-Path $taskBuild 'selection.obj') + '" /link user32.lib comctl32.lib'
& cmd.exe /d /s /c $taskCommand
if ($LASTEXITCODE -ne 0) { throw 'Native selection test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Native selection regression failed: $LASTEXITCODE" }
