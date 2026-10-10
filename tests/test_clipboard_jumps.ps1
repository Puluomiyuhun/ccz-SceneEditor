$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProduction = Get-Content (Join-Path $taskRoot 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8
$taskStart = $taskProduction.IndexOf('void recur_paste(')
$taskEnd = $taskProduction.IndexOf('void CcczEditor2View::OnCut()')
if ($taskEnd -lt 0) { throw 'Production template editing implementation not found' }
$taskTemplate = Get-Content (Join-Path $PSScriptRoot 'preview_integration/clipboard_jump_tests.cpp') -Raw -Encoding UTF8
$taskBuild = Join-Path $taskRoot '.tmp-clipboard-jump-tests'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp = Join-Path $taskBuild 'native.cpp'
$taskExe = Join-Path $taskBuild 'native.exe'
# Resolve project-relative production headers before putting the fixture in a build directory.
$taskTemplate = $taskTemplate.Replace('../../cczEditor2/', '../cczEditor2/')
Set-Content -LiteralPath $taskCpp -Encoding UTF8 -Value $taskTemplate.Replace('// PRODUCTION_CLIPBOARD', $taskProduction.Substring($taskStart,$taskEnd-$taskStart))
$taskDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand = '"' + $taskDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 "' + $taskCpp + '" /Fe:"' + $taskExe + '" /Fo:"' + (Join-Path $taskBuild 'native.obj') + '" /link user32.lib comctl32.lib'
& cmd.exe /d /s /c $taskCommand
if ($LASTEXITCODE -ne 0) { throw 'Native template test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Native template test failed: $LASTEXITCODE" }
