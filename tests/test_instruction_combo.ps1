$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot '.tmp-instruction-combo-tests'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskExe = Join-Path $taskBuild 'combo.exe'
$taskSource = Join-Path $PSScriptRoot 'preview_integration/instruction_combo_tests.cpp'
$taskProduction = Join-Path $taskRoot 'cczEditor2/CMyComboBox.cpp'
$taskDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand = '"' + $taskDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /Y- "' + $taskSource + '" "' + $taskProduction + '" /Fe:"' + $taskExe + '" /Fo:"' + $taskBuild + '/" /link /subsystem:console'
& cmd.exe /d /s /c $taskCommand
if ($LASTEXITCODE -ne 0) { throw 'Production combo regression build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Production combo regression failed: $LASTEXITCODE" }
