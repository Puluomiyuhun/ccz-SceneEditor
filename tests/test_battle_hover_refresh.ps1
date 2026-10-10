$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPage = Get-Content (Join-Path $taskRoot 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$taskStart = $taskPage.IndexOf('void CSBattlePreviewPage::OnMouseMove(')
$taskEnd = $taskPage.IndexOf('void CSBattlePreviewPage::OnMouseLeave()', $taskStart)
if ($taskStart -lt 0 -or $taskEnd -lt 0) { throw 'Missing production mouse handler' }
$taskHelper = $taskPage.Substring($taskStart, $taskEnd - $taskStart)
$taskTemplate = Get-Content (Join-Path $PSScriptRoot 'preview_integration/battle_hover_refresh_tests.cpp') -Raw -Encoding UTF8
$taskBuild = Join-Path $taskRoot '.tmp-hover-test'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp = Join-Path $taskBuild 'hover.cpp'
$taskExe = Join-Path $taskBuild 'hover.exe'
Set-Content -LiteralPath $taskCpp -Encoding UTF8 -Value $taskTemplate.Replace('// PRODUCTION_HELPER', $taskHelper)
$taskDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand = '"' + $taskDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 "' + $taskCpp + '" /Fe:"' + $taskExe + '" /Fo:"' + (Join-Path $taskBuild 'hover.obj') + '" /link user32.lib comctl32.lib'
& cmd.exe /d /s /c $taskCommand
if ($LASTEXITCODE -ne 0) { throw 'Hover refresh test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Hover refresh regression failed: $LASTEXITCODE" }

