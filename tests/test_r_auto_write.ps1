$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskSource=Get-Content (Join-Path $taskRoot 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding utf8
$taskFunctions=''
foreach($taskName in @('CommitMultiMove','CommitPointMove','InsertMoveInstruction','InsertDisappearInstruction','CompleteAppearWrite')){
 $taskStart=$taskSource.IndexOf('CRScenePreviewPage::'+$taskName+'(');$taskStart=$taskSource.LastIndexOf("`n",$taskStart)+1
 $taskOpen=$taskSource.IndexOf('{',$taskStart);$taskDepth=1;$taskEnd=$taskOpen+1
 while($taskDepth -gt 0){if($taskSource[$taskEnd] -eq '{'){++$taskDepth};if($taskSource[$taskEnd] -eq '}'){--$taskDepth};++$taskEnd}
 $taskFunctions+=$taskSource.Substring($taskStart,$taskEnd-$taskStart)+"`n"
}
$taskBuild=Join-Path $taskRoot '.tmp-r-auto-write'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskTemplate=Get-Content (Join-Path $PSScriptRoot 'preview_integration/r_auto_write_tests.cpp') -Raw -Encoding utf8
$taskCpp=Join-Path $taskBuild 'auto.cpp';$taskExe=Join-Path $taskBuild 'auto.exe'
# Generated source resolves fixture-relative production headers from its build directory.
$taskTemplate=$taskTemplate.Replace('../../cczEditor2/', '../cczEditor2/')
Set-Content -LiteralPath $taskCpp -Value $taskTemplate.Replace('// PRODUCTION_FUNCTIONS',$taskFunctions) -Encoding utf8
$taskDev='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
$taskCmd='"'+$taskDev+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /Y- "'+$taskCpp+'" /Fe:"'+$taskExe+'" /Fo:"'+$taskBuild+'/" /link /subsystem:console'
& cmd.exe /d /s /c $taskCmd
if($LASTEXITCODE -ne 0){throw 'Auto-write build failed'}
& $taskExe
if($LASTEXITCODE -ne 0){throw 'Auto-write tests failed'}
