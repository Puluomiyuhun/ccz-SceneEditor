$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskBuild=Join-Path $taskRoot '.tmp-battle-sidebar-transition'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskTemplate=Get-Content (Join-Path $PSScriptRoot 'preview_integration/battle_sidebar_transition_tests.cpp') -Raw -Encoding utf8
$taskSourceText=Get-Content (Join-Path $taskRoot 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding utf8
$taskStart=$taskSourceText.IndexOf('void CSBattlePreviewPage::OnCharacterSelectionChanged()')
$taskEnd=$taskSourceText.IndexOf('void CSBattlePreviewPage::OnDeploymentSelectionChanged()', $taskStart)
$taskHandler=$taskSourceText.Substring($taskStart,$taskEnd-$taskStart).Replace('CSBattlePreviewPage::','TestSidebar::')
$taskSource=Join-Path $taskBuild 'sidebar.cpp'
Set-Content -LiteralPath $taskSource -Value $taskTemplate.Replace('// PRODUCTION_SELECTION',$taskHandler) -Encoding utf8
$taskExe=Join-Path $taskBuild 'sidebar.exe'
$taskDev='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
$taskCmd='"'+$taskDev+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /Y- "'+$taskSource+'" /Fe:"'+$taskExe+'" /Fo:"'+$taskBuild+'/" /link /subsystem:console'
& cmd.exe /d /s /c $taskCmd
if($LASTEXITCODE -ne 0){throw 'Sidebar build failed'}
& $taskExe
if($LASTEXITCODE -ne 0){throw 'Sidebar transition test failed'}
