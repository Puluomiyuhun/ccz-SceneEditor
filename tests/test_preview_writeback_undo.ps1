param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RepositoryRoot)
$sceneSource = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$sceneHeader = Join-Path $root 'cczEditor2\ScenePreviewWnd.h'
foreach ($path in @($sceneSource, $sceneHeader)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: missing $path"
    }
}

$utf8 = [Text.Encoding]::UTF8
$source = [IO.File]::ReadAllText($sceneSource, $utf8)
$header = [IO.File]::ReadAllText($sceneHeader, $utf8)

foreach ($marker in @(
        'PreviewUndoTransaction', 'UndoLastPreviewOperation',
        'RecordCoordinateUndo', 'RecordInsertedUndo',
        'm_previewUndoStack', 'OnUndoButtonClicked',
        'ON_BN_CLICKED(kUndoButtonId', 'm_undoButton.Create')) {
    if (-not $source.Contains($marker) -and -not $header.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: missing $marker"
    }
}

# Coordinate writes must snapshot old/new values, while inserted commands are
# grouped once per user operation rather than recording every member as a
# separate undo item.
foreach ($marker in @(
        'const int oldX =', 'const int oldY =',
        'RecordCoordinateUndo(undoLabel',
        'CommitMapPortraitPosition',
        'RecordInsertedUndo(',
        'std::move(insertedItems)',
        'transaction.insertedItems.rbegin()',
        'PositionSourceMatchesInstruction')) {
    if (-not $source.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: missing transactional marker $marker"
    }
}

# Batch insertion paths explicitly disable per-item recording and publish one
# transaction after ApplyBatchTransaction succeeds.
if (-not $source.Contains('InsertTurnInstruction(') -or
    -not $source.Contains('direction, anchor, false') -or
    -not $source.Contains('action, anchor, false') -or
    -not $source.Contains('anchor, m_multiMoveDirection, false')) {
    throw 'PREVIEW_WRITEBACK_UNDO_FAIL: batch writebacks are not grouped'
}

# Stale tree handles or externally changed coordinates must reject the undo;
# silently overwriting unrelated edits is not acceptable.
foreach ($marker in @(
        'treeGeneration != m_previewTreeGeneration',
        'FindCurrentTreeItem(tree, entry.item)',
        'FindCurrentTreeItem(tree, item)')) {
    if (-not $source.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: stale-state guard missing $marker"
    }
}

'PREVIEW_WRITEBACK_UNDO_PASS'
