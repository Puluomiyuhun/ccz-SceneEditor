$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot '.tmp-r-replay-tree-lookup'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$dev = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$exe = Join-Path $output 'current_tree_item_index_tests.exe'
$source = Join-Path $PSScriptRoot 'preview_integration/current_tree_item_index_tests.cpp'
$include = Join-Path $repo 'cczEditor2/PreviewIntegration'
$command = '"' + $dev + '" -arch=x86 -host_arch=x86 && cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE /I"' + $include + '" "' + $source + '" /Fo:"' + $output + '/current_tree_item_index_tests.obj" /Fe:"' + $exe + '" user32.lib comctl32.lib'
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'TreeView lookup fixture compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'TreeView lookup fixture failed' }
# Both bulk replay paths must use one transaction-local index, not one full
# TreeView walk per command. Single-item undo/playback lookup is unchanged.
$page = Get-Content (Join-Path $repo 'cczEditor2/ScenePreviewWnd.cpp') -Raw
foreach ($start in @('bool CRScenePreviewPage::ApplyFreeRPrefixOnly(', 'NativePlanApplyResult CRScenePreviewPage::ApplyNativeReplayPlan(')) {
    $begin = $page.IndexOf($start)
    $end = $page.IndexOf("`n}", $begin)
    $block = $page.Substring($begin, $end - $begin)
    if ($block -notmatch 'CurrentTreeItemIndex currentItems' -or $block -notmatch 'currentItems.Find' -or $block -match 'FindCurrentTreeItem\(') {
        throw 'Bulk R replay still walks the full tree for each command'
    }
}
'R_REPLAY_TREE_LOOKUP_PASS'
