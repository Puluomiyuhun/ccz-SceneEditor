param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2'),
    [string]$StagingRoot = (Join-Path (Join-Path (Join-Path (Join-Path (Join-Path $PSScriptRoot '..') '..') '..') 'ccz_preview_ext') 'staging-test\ccz-editor-dynamic-preview-20260920'),
    [string]$LauncherFileName = 'START-INTERNALIZED-PREVIEW-EDITOR.cmd',
    [string]$ExpectedReadmeMarker = ''
)

$ErrorActionPreference = 'Stop'

function Read-Utf8Text([string]$Path) {
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    return [IO.File]::ReadAllText($Path, $utf8)
}

if ([string]::IsNullOrEmpty($ExpectedReadmeMarker)) {
    $ExpectedReadmeMarker = -join ([char[]](0x5185, 0x7F6E, 0x9884, 0x89C8, 0x5B8C,
        0x6574, 0x56DE, 0x5F52, 0x901A, 0x8FC7))
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "TARGETED_REVIEW_CONTRACT_FAIL: $Message" }
}

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    Assert-True ($Text -match $Pattern) $Message
}

function Assert-NotContains([string]$Text, [string]$Pattern, [string]$Message) {
    Assert-True ($Text -notmatch $Pattern) $Message
}

$docPath = Join-Path $SourceRoot 'cczEditor2Doc.cpp'
$comboPath = Join-Path $SourceRoot 'CMyComboBox.cpp'
$comboHeaderPath = Join-Path $SourceRoot 'CMyComboBox.h'
$indexedPath = Join-Path $SourceRoot 'IndexedComboSearch.h'
$cppRegressionPath = Join-Path (Split-Path -Parent $SourceRoot) 'tests\indexed_combo_search_tests.cpp'
$encodingRegressionPath = Join-Path (Split-Path -Parent $SourceRoot) 'tests\txt_export_encoding_tests.cpp'
$planPath = Join-Path (Split-Path -Parent $SourceRoot) 'tests\targeted-review-test-plan-20260918.md'

foreach ($path in @($docPath, $comboPath, $comboHeaderPath, $indexedPath, $cppRegressionPath, $encodingRegressionPath, $planPath)) {
    Assert-True (Test-Path -LiteralPath $path) "missing required test input: $path"
}

$doc = Read-Utf8Text $docPath
$combo = Read-Utf8Text $comboPath
$comboHeader = Read-Utf8Text $comboHeaderPath
$indexed = Read-Utf8Text $indexedPath
$cppRegression = Read-Utf8Text $cppRegressionPath
$encodingRegression = Read-Utf8Text $encodingRegressionPath

# T: TXT export must be capacity-driven and must not write its terminating NUL.
$export = [regex]::Match($doc, '(?s)void CcczEditor2Doc::recur_export\(.*?\n\}')
Assert-True $export.Success 'recur_export function is missing'
Assert-NotContains $export.Value 'char\s+tmp\[3000\]' 'fixed 3000-byte TXT export buffer remains'
Assert-Contains $export.Value 'WideCharToMultiByte\(\s*936\s*,\s*0\s*,\s*text\s*,\s*-1\s*,\s*NULL\s*,\s*0' 'TXT export does not query CP936 capacity'
Assert-Contains $export.Value 'std::vector<char>\s+encoded' 'TXT export does not allocate the queried capacity'
Assert-Contains $export.Value 'written\s*-\s*1' 'TXT export still writes the trailing NUL'

# E: Esc has two non-overlapping cancellation paths and neither notifies.
$escBranch = [regex]::Match(
    $combo,
    '(?s)if\s*\(\s*pMsg->message\s*==\s*WM_KEYDOWN\s*&&\s*pMsg->wParam\s*==\s*VK_ESCAPE.*?\n\s*\}')
Assert-True $escBranch.Success 'indexed combo has no explicit no-popup Esc branch'
Assert-Contains $escBranch.Value '!dropdown_open_' 'no-popup Esc branch does not guard dropdown_open_'
Assert-Contains $escBranch.Value 'GetDroppedState\(\)' 'no-popup Esc branch does not guard native popup state'
Assert-Contains $escBranch.Value 'RestoreFullOptionList' 'no-popup Esc does not restore the complete list'
Assert-Contains $escBranch.Value 'RestoreLastSelectionText' 'no-popup Esc does not restore committed text'
Assert-Contains $escBranch.Value 'return\s+TRUE' 'no-popup Esc is not consumed by the combo'
Assert-NotContains $escBranch.Value 'CommitIndexedInput|NotifySelectionChange' 'no-popup Esc can commit or notify a selection'

$cancel = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionEndCancelReflect\(\).*?\n\}')
Assert-True $cancel.Success 'native popup cancel reflection handler is missing'
Assert-Contains $cancel.Value 'SetCanonicalSelection|RestoreLastSelectionText' 'native cancel does not restore the opening snapshot'
Assert-Contains $cancel.Value 'dropdown_open_' 'native cancel does not distinguish an opened popup'
Assert-NotContains $cancel.Value 'NotifySelectionChange' 'native cancel sends a false selection notification'

$accepted = [regex]::Match($combo, '(?s)BOOL CMyComboBox::OnSelectionEndOkReflect\(\).*?\n\}')
Assert-True $accepted.Success 'native popup accepted handler is missing'
Assert-Contains $accepted.Value 'NotifySelectionChange' 'accepted native selection does not notify the parent'

# N: keep searchability and direct numeric resolvability as separate contracts.
Assert-Contains $indexed 'bool\s+numeric_resolvable' 'numeric_resolvable flag is missing'
$resolve = [regex]::Match($indexed, '(?s)inline bool ResolveDecimalCode\(.*?\n\}')
Assert-True $resolve.Success 'ResolveDecimalCode implementation is missing'
Assert-Contains $resolve.Value 'numeric_resolvable' 'ResolveDecimalCode ignores the numeric_resolvable flag'
Assert-Contains $combo 'mode == IndexedMode::ItemSegment' 'segmented item mode is not configured'
Assert-Contains $combo 'option\.numeric_code\s*%\s*256\s*==\s*255' 'item reserved-number rule is not expressed as a modulo-256 boundary'
Assert-Contains $combo 'numeric_resolvable\s*=\s*false' 'reserved item numbers are still directly resolvable'
Assert-Contains $combo '(?s)mode == IndexedMode::Item\s*\|\|\s*mode == IndexedMode::ItemSegment' 'reserved-number rule may accidentally cover person lists'

# C++ regression matrix must retain both item negatives and person positives.
Assert-Contains $cppRegression '255:铁枪|255,\s*255,\s*false,\s*false' 'item 255 negative case is missing'
Assert-Contains $cppRegression '511' 'item/segment 511 boundary case is missing'
Assert-Contains $cppRegression 'peopleWith255' 'person 255 positive case is missing'
Assert-Contains $cppRegression 'selection\s*==\s*255' 'person 255 direct-resolution assertion is missing'
Assert-Contains $encodingRegression '3500' 'long CP936-byte behavior case is missing'
Assert-Contains $encodingRegression 'EncodeForTxt' 'encoding regression does not exercise the dynamic conversion algorithm'

# Artifact gate: staging must be the exact Release binary that the test report names.
$releaseExe = Join-Path (Split-Path -Parent $SourceRoot) 'Release\s.exe'
$stagingExe = Join-Path $StagingRoot 's.exe'
$sumPath = Join-Path $StagingRoot 'SHA256SUM.txt'
$readmePath = Join-Path $StagingRoot 'README.md'
$launcherPath = Join-Path $StagingRoot $LauncherFileName
foreach ($path in @($releaseExe, $stagingExe, $sumPath, $readmePath, $launcherPath)) {
    Assert-True (Test-Path -LiteralPath $path) "missing staging/build artifact: $path"
}
$releaseHash = (Get-FileHash -LiteralPath $releaseExe -Algorithm SHA256).Hash.ToUpperInvariant()
$stagingHash = (Get-FileHash -LiteralPath $stagingExe -Algorithm SHA256).Hash.ToUpperInvariant()
Assert-True ($releaseHash -eq $stagingHash) 'staging s.exe differs from Release s.exe'
$sum = Get-Content -LiteralPath $sumPath -Raw -Encoding ASCII
Assert-Contains $sum $stagingHash 'SHA256SUM.txt does not match staging s.exe'
Assert-Contains (Read-Utf8Text $readmePath) $ExpectedReadmeMarker 'staging README lacks the expected verification marker'

Write-Output 'targeted_review_contract: PASS'
