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
        'directionChanged', 'oldDirection', 'newDirection',
        'RecordCoordinateUndo(undoLabel',
        'CommitMapPortraitPosition',
        'RecordInsertedUndo(',
        'std::move(insertedItems)',
        'transaction.insertedItems.size(); index > 0',
        'PositionSourceMatchesInstruction')) {
    if (-not $source.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: missing transactional marker $marker"
    }
}

# R adjust-position writes facing together with coordinates and its grouped
# undo validates and restores that same direction field.
foreach ($marker in @(
        'position_direction_index(entry.source.kind)',
        'data->int_data[directionIndex] = entry.state->direction',
        'data->int_data[directionIndex] != entry.newDirection',
        'data->int_data[directionIndex] = entry.oldDirection')) {
    if (-not $source.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: facing transaction marker missing $marker"
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
        'treeGeneration != pView->GetDocument()->GetPreviewTreeGeneration()',
        'FindCurrentTreeItem(tree, entry.item)',
        'FindCurrentTreeItem(tree, snapshot.item)')) {
    if (-not $source.Contains($marker)) {
        throw "PREVIEW_WRITEBACK_UNDO_FAIL: stale-state guard missing $marker"
    }
}

foreach ($marker in @('insertedSnapshots', 'UndoRefreshScope', 'removed.reserve', 'preservedUndo = std::move', 'ClearNativePreviewState(false)', 'm_undoButtonVisible = true', 'BS_OWNERDRAW')) {
    if (-not $source.Contains($marker)) { throw "PREVIEW_WRITEBACK_UNDO_FAIL: continuous undo guard missing $marker" }
}
'PREVIEW_WRITEBACK_UNDO_PASS' 
