$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskPage=Get-Content (Join-Path $taskRoot 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$start=$taskPage.IndexOf('Gdiplus::Bitmap* CSBattlePreviewPage::GetUnitSprite(')
$end=$taskPage.IndexOf('const int key = spriteRef->resource_index',$start)
$guard=$taskPage.Substring($start,$end-$start)+"return nullptr;`n}`n"
$start=$taskPage.IndexOf('void CSBattlePreviewPage::OnKeyDown(')
$end=$taskPage.IndexOf('void CSBattlePreviewPage::OnCaptureChanged(',$start)
$keyboard=$taskPage.Substring($start,$end-$start)
$start=$taskPage.IndexOf('void CSBattlePreviewPage::CancelConflictingBattleEditorMode()')
$end=$taskPage.IndexOf('void CSBattlePreviewPage::CancelBattleMapTool(',$start)
$handoff=$taskPage.Substring($start,$end-$start)
$fixture=Get-Content (Join-Path $PSScriptRoot 'preview_integration/battle_review_paths_tests.cpp') -Raw -Encoding UTF8
$taskBuild=Join-Path $taskRoot '.tmp-review-paths-tests'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp=Join-Path $taskBuild 'paths.cpp';$taskExe=Join-Path $taskBuild 'paths.exe'
$fixture.Replace('// SPRITE_GUARD',$guard).Replace('// KEY_HANDLER',$keyboard).Replace('// TOOL_HANDOFF',$handoff) | Set-Content -LiteralPath $taskCpp -Encoding UTF8
$taskDevCmd='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand='"'+$taskDevCmd+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 "'+$taskCpp+'" /Fe:"'+$taskExe+'" /Fo:"'+(Join-Path $taskBuild 'paths.obj')+'" /link user32.lib'
& cmd.exe /d /s /c $taskCommand
if($LASTEXITCODE -ne 0){throw 'Review path fixture compilation failed'}
& $taskExe
if($LASTEXITCODE -ne 0){throw 'Review path regression failed'}
