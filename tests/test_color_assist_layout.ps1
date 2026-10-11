$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskBuild=Join-Path $taskRoot '.tmp-color-assist-layout'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskTemplate=Get-Content (Join-Path $PSScriptRoot 'preview_integration/color_assist_layout_tests.cpp') -Raw -Encoding utf8
$taskHost=Get-Content (Join-Path $taskRoot 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding utf8
$taskContext=$taskHost.Substring($taskHost.IndexOf('void CScenePreviewWnd::OnContextMenu(')).Replace('CScenePreviewWnd::','TestDockPane::')
$taskScene=Get-Content (Join-Path $taskRoot 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding utf8
$taskMoveStart=$taskScene.IndexOf('static void r_preview_move(')
$taskMoveEnd=$taskScene.IndexOf('void CRScenePreviewPage::LayoutConditionUi(', $taskMoveStart)
$taskMove=$taskScene.Substring($taskMoveStart,$taskMoveEnd-$taskMoveStart)
$taskPlaybackStart=$taskScene.IndexOf('void CRScenePreviewPage::LayoutPlaybackUi(')
$taskPlaybackEnd=$taskScene.IndexOf('CTreeCtrl* CRScenePreviewPage::ResolvePreviewTree(', $taskPlaybackStart)
$taskPlayback=$taskScene.Substring($taskPlaybackStart,$taskPlaybackEnd-$taskPlaybackStart).Replace('CRScenePreviewPage::','TestPlayback::')
$taskConstants=([regex]::Matches($taskScene,'constexpr int k(?:Playback|Condition)\w+ = \d+;') | ForEach-Object {$_.Value}) -join "`n"
$taskPlaybackFixture=Get-Content (Join-Path $PSScriptRoot 'preview_integration/r_playback_layout_fixture.inl') -Raw -Encoding utf8
$taskPlaybackFixture=$taskConstants+"`n"+$taskMove+"`n"+$taskPlaybackFixture.Replace('// PRODUCTION_LAYOUT',$taskPlayback)
$taskTemplate=$taskTemplate.Replace('// PRODUCTION_CONTEXT',$taskContext).Replace('// PRODUCTION_PLAYBACK',$taskPlaybackFixture).Replace('../../cczEditor2/','../cczEditor2/')
$taskSource=Join-Path $taskBuild 'layout.cpp'
Set-Content -LiteralPath $taskSource -Value $taskTemplate -Encoding utf8
$taskExe=Join-Path $taskBuild 'layout.exe'
$taskDev='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
$taskCmd='"'+$taskDev+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /Y- "'+$taskSource+'" /Fe:"'+$taskExe+'" /Fo:"'+$taskBuild+'/" /link /subsystem:console'
& cmd.exe /d /s /c $taskCmd
if($LASTEXITCODE -ne 0){throw 'Layout build failed'}
& $taskExe
if($LASTEXITCODE -ne 0){throw 'Layout test failed'}
