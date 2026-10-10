param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$previewPath = Join-Path $SourceRoot 'ScenePreviewWnd.cpp'
$headerPath = Join-Path $SourceRoot 'ScenePreviewWnd.h'
if (-not (Test-Path -LiteralPath $previewPath) -or -not (Test-Path -LiteralPath $headerPath)) {
    throw 'SCENE_MULTISELECT_WRITEBACK_TEST_FAIL: preview source files are missing'
}

$preview = Get-Content -LiteralPath $previewPath -Raw
$header = Get-Content -LiteralPath $headerPath -Raw

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "SCENE_MULTISELECT_WRITEBACK_TEST_FAIL: $Message"
    }
}

function Get-FunctionBlock([string]$Text, [string]$Start, [string[]]$NextStarts) {
    $textValue = [string]$Text
    $startValue = [string]$Start
    if ([string]::IsNullOrEmpty($startValue)) { return '' }
    $nextPatterns = @($NextStarts) |
        Where-Object { -not [string]::IsNullOrEmpty([string]$_) } |
        ForEach-Object { [regex]::Escape([string]$_) }
    $lookahead = if ($nextPatterns.Count -gt 0) {
        '(?=' + ($nextPatterns -join '|') + '|$)'
    } else {
        '(?=$)'
    }
    $pattern = '(?s)' + [regex]::Escape($startValue) + '.*?' + $lookahead
    $match = [regex]::Match($textValue, $pattern)
    if (-not $match.Success) { return '' }
    return $match.Value
}

Assert-Contains $header 'm_bMultiMoveMode' 'multi-move mode state is missing'
Assert-Contains $header 'm_multiMoveAnchor' 'primary-anchor state is missing'
Assert-Contains $header 'm_multiMoveDirection' 'unified move direction state is missing'
Assert-Contains $header 'CommitMultiMove' 'multi-move commit helper is missing'

$directionBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::ApplyMultiDirection' @(
    'void CRScenePreviewPage::ApplyMultiAction',
    'void CRScenePreviewPage::ApplyMultiDisappear',
    'void CRScenePreviewPage::BeginMultiMove'
)
$actionBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::ApplyMultiAction' @(
    'void CRScenePreviewPage::ApplyMultiDisappear',
    'void CRScenePreviewPage::BeginMultiMove'
)
$disappearBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::ApplyMultiDisappear' @(
    'void CRScenePreviewPage::BeginMultiMove',
    'void CRScenePreviewPage::InsertTurnInstruction'
)
$beginMoveBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::BeginMultiMove' @(
    'void CRScenePreviewPage::CommitMultiMove',
    'void CRScenePreviewPage::InsertTurnInstruction'
)
$moveBlock = Get-FunctionBlock $preview 'void CRScenePreviewPage::CommitMultiMove' @(
    'void CRScenePreviewPage::InsertTurnInstruction'
)

Assert-Contains $directionBlock 'InsertTurnInstruction' 'uniform direction does not reuse native turn insertion'
Assert-Contains $actionBlock 'InsertActionInstruction' 'uniform action does not reuse native action insertion'
Assert-Contains $disappearBlock 'InsertDisappearInstruction' 'batch disappear does not reuse native disappear insertion'
Assert-Contains $beginMoveBlock 'm_primaryCharID|m_multiMoveAnchor' 'batch move does not use the primary anchor'
Assert-Contains $moveBlock 'InsertMoveInstruction' 'batch move does not reuse native move insertion'
Assert-Contains $preview 'm_bMultiMoveMode' 'multi-move event handling is missing'
Assert-Contains $preview 'm_bSuppressContextMenu' 'right-click cancellation path is missing'
Assert-Contains $header 'DrawCharacterGhost' 'multi-move ghost renderer is missing'
Assert-Contains $preview 'DrawCharacterGhost' 'multi-move ghost preview branch is missing'
Assert-Contains $header 'm_bPointMoveMode' 'point-move mode state is missing'
Assert-Contains $preview 'm_bPointMoveMode' 'point-move event handling is missing'
Assert-Contains $preview 'selectedStates' 'point-move should snapshot selected states before insertion'
Assert-Contains $preview 'character\.x \+ deltaX|targetX' 'point-move target coordinate path is missing'
Assert-Contains $preview 'CycleDirection' 'unified move wheel direction does not use the shared cycle helper'
Assert-Contains $preview 'm_multiMoveDirection' 'unified move direction is not used by preview/writeback'
Assert-Contains $preview 'm_bMultiMoveMode \|\| m_bPointMoveMode' 'unified move wheel branch is missing'

'SCENE_MULTISELECT_WRITEBACK_PASS'
