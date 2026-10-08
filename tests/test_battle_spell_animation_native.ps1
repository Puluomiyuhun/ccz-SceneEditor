param(
 [string]$Archive = 'D:\AI曹操传项目群\main engine\E5\Meff.e5',
 [string]$Reference = 'F:\00个人文件夹\01各类编辑工具\MeffEDIT\UnPackMeff.e5',
 [string]$ModRoot = 'D:\AI曹操传项目群\main engine'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskBuild=Join-Path $taskRoot '.tmp-meff-native'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp=Join-Path $PSScriptRoot 'preview_integration/battle_spell_animation_native_tests.cpp'
$taskExe=Join-Path $taskBuild 'test.exe'
$taskDev='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCompile='"'+$taskDev+'" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 /MD /D_AFXDLL /D_UNICODE /DUNICODE "'+$taskCpp+'" /Fe:"'+$taskExe+'" /Fo:"'+(Join-Path $taskBuild 'test.obj')+'" /link gdiplus.lib'
& cmd.exe /d /s /c $taskCompile
if($LASTEXITCODE -ne 0){throw 'Meff native test compile failed'}
Push-Location $taskRoot
try { & $taskExe $Archive $Reference $ModRoot; if($LASTEXITCODE -ne 0){throw 'Meff native tests failed'} }
finally { Pop-Location }
