param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$previewPath = Join-Path $SourceRoot 'ScenePreviewWnd.cpp'
$headerPath = Join-Path $SourceRoot 'ScenePreviewWnd.h'

if (-not (Test-Path -LiteralPath $previewPath)) {
    throw "SCENE_MULTISELECT_TEST_FAIL: ScenePreviewWnd.cpp not found: $previewPath"
}
if (-not (Test-Path -LiteralPath $headerPath)) {
    throw "SCENE_MULTISELECT_TEST_FAIL: ScenePreviewWnd.h not found: $headerPath"
}

function Read-Utf8Text([string]$Path) {
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    return [IO.File]::ReadAllText($Path, $utf8)
}

$preview = Read-Utf8Text $previewPath
$header = Read-Utf8Text $headerPath

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "SCENE_MULTISELECT_TEST_FAIL: $Message"
    }
}

# Keep localized menu literals out of this UTF-8-without-BOM test file.  The
# source is decoded explicitly above; constructing the expected labels from
# code points keeps Windows PowerShell 5.1 from converting the test itself
# through the active ANSI code page.
function Join-CodePoints([int[]]$CodePoints) {
    return -join ([char[]]$CodePoints)
}

$labelAlign = 'L"' + (Join-CodePoints @(0x5BF9, 0x9F50)) + '"'
$labelDistribute = 'L"' + (Join-CodePoints @(0x7B49, 0x8DDD)) + '"'
$labelQuickAlign = 'L"' + (Join-CodePoints @(0x5FEB, 0x901F, 0x5BF9, 0x9F50)) + '"'
$labelOneClickDistribute = 'L"' + (Join-CodePoints @(0x4E00, 0x952E, 0x7B49, 0x8DDD)) + '"'
$labelAlignPrimary = 'L"' + (Join-CodePoints @(0x5BF9, 0x9F50, 0x4E3B, 0x89D2, 0x8272)) + '"'
$labelPreciseDistribute = 'L"' + (Join-CodePoints @(0x7CBE, 0x51C6, 0x7B49, 0x8DDD)) + '"'
$labelSameX = 'L"' + (Join-CodePoints @(0x540C, 0x20, 0x58, 0x20, 0x5750, 0x6807)) + '"'
$labelSameY = 'L"' + (Join-CodePoints @(0x540C, 0x20, 0x59, 0x20, 0x5750, 0x6807)) + '"'
$labelByX = 'L"' + (Join-CodePoints @(0x6309, 0x20, 0x58, 0x20, 0x5750, 0x6807)) + '"'
$labelByY = 'L"' + (Join-CodePoints @(0x6309, 0x20, 0x59, 0x20, 0x5750, 0x6807)) + '"'
$labelAction = 'L"' + (Join-CodePoints @(0x7EDF, 0x4E00, 0x52A8, 0x4F5C)) + '"'
$labelDirection = 'L"' + (Join-CodePoints @(0x7EDF, 0x4E00, 0x671D, 0x5411)) + '"'
$labelBatchMove = 'L"' + (Join-CodePoints @(0x6279, 0x91CF, 0x79FB, 0x52A8)) + '"'
$labelGroupMove = 'L"' + (Join-CodePoints @(0x7EC4, 0x79FB, 0x52A8)) + '"'
$labelPointMove = 'L"' + (Join-CodePoints @(0x70B9, 0x79FB, 0x52A8)) + '"'
$labelBatchDisappear = 'L"' + (Join-CodePoints @(0x6279, 0x91CF, 0x6D88, 0x5931)) + '"'
$labelSetPrimary = 'L"' + (Join-CodePoints @(0x8BBE, 0x4E3A, 0x4E3B, 0x89D2, 0x8272)) + '"'

# Task 2 contract: session selection, primary selection, blank double-click,
# and two visually distinct marker paths must exist before menu work starts.
Assert-Contains $header 'std::vector<int>\s+m_selectedCharIDs' 'selection order state is missing'
Assert-Contains $header 'int\s+m_primaryCharID\s*=\s*-1' 'primary character state is missing'
Assert-Contains $header 'OnLButtonDblClk' 'double-click handler declaration is missing'
Assert-Contains $header 'ToggleCharacterSelection' 'selection toggle helper is missing'
Assert-Contains $header 'ClearCharacterSelection' 'clear selection helper is missing'
Assert-Contains $header 'ReconcileCharacterSelection' 'selection reconciliation helper is missing'
Assert-Contains $preview 'ON_WM_LBUTTONDBLCLK' 'double-click message mapping is missing'
Assert-Contains $preview 'void\s+CRScenePreviewPage::ToggleCharacterSelection' 'selection toggle implementation is missing'
Assert-Contains $preview 'void\s+CRScenePreviewPage::ClearCharacterSelection' 'clear selection implementation is missing'
Assert-Contains $preview 'void\s+CRScenePreviewPage::ReconcileCharacterSelection' 'selection reconciliation implementation is missing'
Assert-Contains $preview 'IsCharacterSelected' 'selected marker lookup is missing'
Assert-Contains $preview 'IsPrimaryCharacter' 'primary marker lookup is missing'
# Only an actually empty canvas point may clear all character selections.
# Ordinary actors and map portraits are both non-empty hit targets; portraits
# remain outside the multi-select collection.
Assert-Contains $preview '(?s)if\s*\(\s*HitTestCharacter\(point\)\s*<\s*0\s*&&\s*HitTestMapPortrait\(point\)\s*<\s*0\s*\)' 'blank double-click guard must exclude ordinary actors and map portraits'

# Task 3 contract: preserve the original menu for zero/one selection and
# route a right-click on a selected actor to the dedicated batch menu.
Assert-Contains $preview 'm_selectedCharIDs\.size\(\)\s*>=\s*2' 'multi-selection menu threshold is missing'
Assert-Contains $preview 'IsCharacterSelected\(m_hitCharID\)' 'multi-selection menu does not require a selected hit actor'
Assert-Contains $preview $labelAlign 'batch align menu label is missing'
Assert-Contains $preview $labelDistribute 'batch distribution menu label is missing'
Assert-Contains $preview $labelQuickAlign 'quick alignment submenu label is missing'
Assert-Contains $preview $labelOneClickDistribute 'one-click distribution submenu label is missing'
Assert-Contains $preview $labelAlignPrimary 'primary alignment submenu label is missing'
Assert-Contains $preview $labelPreciseDistribute 'precise distribution submenu label is missing'
Assert-Contains $preview $labelSameX 'primary X alignment command label is missing'
Assert-Contains $preview $labelSameY 'primary Y alignment command label is missing'
Assert-Contains $preview $labelByX 'precise X distribution command label is missing'
Assert-Contains $preview $labelByY 'precise Y distribution command label is missing'
Assert-Contains $preview $labelAction 'batch action menu label is missing'
Assert-Contains $preview $labelDirection 'batch direction menu label is missing'
Assert-Contains $preview $labelBatchMove 'batch move menu label is missing'
Assert-Contains $preview $labelGroupMove 'group move submenu label is missing'
Assert-Contains $preview $labelPointMove 'point move submenu label is missing'
Assert-Contains $preview $labelBatchDisappear 'batch disappear menu label is missing'
Assert-Contains $preview $labelSetPrimary 'set-primary menu label is missing'
Assert-Contains $preview 'AppendMenuW\(MF_SEPARATOR' 'multi-menu separator is missing'
Assert-Contains $preview 'ID_SCENE_MULTI_ALIGN' 'batch align command is missing'
Assert-Contains $preview 'ID_SCENE_MULTI_SET_PRIMARY' 'set-primary command is missing'
Assert-Contains $preview 'SetPrimaryCharacter\(m_hitCharID\)' 'set-primary dispatch is missing'

# Task 4 contract: position operations use the pure algorithms and the
# effective position-source commit, with one final refresh. Direction/action
# remain separate instruction insertions.
Assert-Contains $preview '#include\s+"SceneMultiSelectOps\.h"' 'layout algorithm header is not included'
Assert-Contains $preview 'AlignNearLines' 'batch alignment does not call the pure algorithm'
Assert-Contains $preview 'DistributeAxes' 'batch distribution does not call the pure algorithm'
Assert-Contains $preview 'AlignToPrimaryAxis' 'primary alignment does not call the pure algorithm'
Assert-Contains $preview 'PreciseDistributeAxis' 'precise distribution does not call the pure algorithm'
Assert-Contains $header 'CommitCharacterStates' 'shared adjustment writeback helper is missing'
Assert-Contains $preview 'bool\s+CRScenePreviewPage::CommitCharacterStates' 'shared adjustment writeback implementation is missing'
Assert-Contains $preview 'entry\.source\.source\.xIndex' 'position X is not written through the effective source'
Assert-Contains $preview 'entry\.source\.source\.yIndex' 'position Y is not written through the effective source'
Assert-Contains $preview 'InsertTurnInstruction' 'direction must use a separate instruction'
Assert-Contains $preview 'InsertActionInstruction' 'action must use a separate instruction'
Assert-Contains $preview 'ApplyMultiAlignment\(\)' 'batch alignment helper is missing'
Assert-Contains $preview 'ApplyMultiDistribution\(\)' 'batch distribution helper is missing'
Assert-Contains $preview 'ApplyPrimaryAlignment' 'primary alignment helper is missing'
Assert-Contains $preview 'ApplyPreciseDistribution' 'precise distribution helper is missing'
Assert-Contains $preview 'BeginPointMove' 'point move entry is missing'
Assert-Contains $preview 'CommitPointMove' 'point move commit is missing'

'SCENE_MULTISELECT_UI_PASS'
