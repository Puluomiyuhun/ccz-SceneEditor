param([switch]$LegacyPageGeneration)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSource = Get-Content (Join-Path $taskRoot 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding UTF8
$taskFunctions = ''
foreach ($taskName in @('RecordCoordinateUndo','RecordInsertedUndo','UndoLastPreviewOperation')) {
    $taskStart = $taskSource.IndexOf('CRScenePreviewPage::' + $taskName + '(')
    $taskStart = $taskSource.LastIndexOf("`n", $taskStart) + 1
    $taskOpen = $taskSource.IndexOf('{', $taskStart)
    $taskDepth = 1; $taskEnd = $taskOpen + 1
    while ($taskDepth -gt 0) {
        if ($taskSource[$taskEnd] -eq '{') { ++$taskDepth }
        if ($taskSource[$taskEnd] -eq '}') { --$taskDepth }
        ++$taskEnd
    }
    $taskFunctions += $taskSource.Substring($taskStart, $taskEnd - $taskStart) + "`n"
}
if ($LegacyPageGeneration) {
    $taskFunctions = $taskFunctions.Replace('GetActiveEditorView()->GetDocument()->GetPreviewTreeGeneration()', 'm_previewTreeGeneration').Replace('pView->GetDocument()->GetPreviewTreeGeneration()', 'm_previewTreeGeneration')
}
$taskTemplate = Get-Content (Join-Path $PSScriptRoot 'preview_integration/r_preview_undo_tests.cpp') -Raw -Encoding UTF8
$taskBuild = Join-Path $taskRoot '.tmp-r-native-undo-test'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskCpp = Join-Path $taskBuild 'undo.cpp'
$taskExe = Join-Path $taskBuild 'undo.exe'
Set-Content -LiteralPath $taskCpp -Encoding UTF8 -Value $taskTemplate.Replace('// PRODUCTION_FUNCTIONS', $taskFunctions)
$taskDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$taskCommand = '"' + $taskDevCmd + '" -arch=x86 -host_arch=x86 && cl.exe /nologo /std:c++17 /EHsc /utf-8 "' + $taskCpp + '" /Fe:"' + $taskExe + '" /Fo:"' + (Join-Path $taskBuild 'undo.obj') + '" /link user32.lib comctl32.lib'
& cmd.exe /d /s /c $taskCommand
if ($LASTEXITCODE -ne 0) { throw 'R undo native fixture build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "R undo native fixture failed: $LASTEXITCODE" }
