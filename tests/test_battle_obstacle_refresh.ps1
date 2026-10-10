$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskBuild=Join-Path $taskRoot '.tmp-battle-obstacle-refresh'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskTemplate=Get-Content (Join-Path $PSScriptRoot 'preview_integration/battle_obstacle_refresh_fixture.cpp') -Raw -Encoding utf8
$taskSourceText=Get-Content (Join-Path $taskRoot 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding utf8
$taskStart=$taskSourceText.IndexOf('bool can_rebase_obstacle_drafts(')
$taskEnd=$taskSourceText.IndexOf('bool build_fresh_editor_state_for_scope(', $taskStart)
if($taskStart -lt 0 -or $taskEnd -lt 0){throw 'Obstacle refresh helper missing'}
$taskHandler=$taskSourceText.Substring($taskStart,$taskEnd-$taskStart)
$taskSource=Join-Path $taskBuild 'refresh.cpp'
Set-Content -LiteralPath $taskSource -Value $taskTemplate.Replace('// PRODUCTION_OBSTACLE_REFRESH',$taskHandler) -Encoding utf8
$taskExe=Join-Path $taskBuild 'refresh.exe'
$taskDev='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
$taskCmd='"'+$taskDev+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /Y- "'+$taskSource+'" /Fe:"'+$taskExe+'" /Fo:"'+$taskBuild+'/" /link /subsystem:console'
& cmd.exe /d /s /c $taskCmd
if($LASTEXITCODE -ne 0){throw 'Obstacle refresh fixture build failed'}
& $taskExe
if($LASTEXITCODE -ne 0){throw 'Obstacle refresh test failed'}
