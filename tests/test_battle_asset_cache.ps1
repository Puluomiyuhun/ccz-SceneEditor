$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$adapter = Join-Path $root 'cczEditor2\PreviewIntegration\BattleAssetCache.cpp'
$header = Join-Path $root 'cczEditor2\PreviewIntegration\BattleAssetCache.h'
$battlePage = Join-Path $root 'cczEditor2\BattlePreviewPage.cpp'
$rPage = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$hostSource = Join-Path $root 'cczEditor2\ScenePreviewHost.cpp'
$project = Join-Path $root 'cczEditor2\cczEditor2.vcxproj'

function Assert-Text($path, $pattern, $label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) { throw "BATTLE_ASSET_CACHE_FAIL: $label" }
    Write-Output "[PASS] $label"
}

foreach ($path in @($adapter, $header, $battlePage, $rPage, $hostSource, $project)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "BATTLE_ASSET_CACHE_FAIL: source missing: $path"
    }
}

Assert-Text $adapter 'Data\.e5' 'Data.e5 path is explicit'
Assert-Text $adapter 'root \+ L"Hexzmap\.e5"' 'Hexzmap.e5 is loaded from the Mod root'
Assert-Text $adapter 'map\\\\m' 'S battle map path uses the Mod map folder'
Assert-Text $adapter 'L"\.jpg"' 'S battle map path uses loose JPG files'
Assert-Text $adapter 'std::setw\(3\)' 'S battle map path keeps the three-digit filename convention'
Assert-Text $header 'map_jpg_path' 'cache exposes the resolved battle JPG path'
Assert-Text $header 'ResolveUnitSprite' 'cache exposes the S Unit_mov resolver'
Assert-Text $header 'unit_mov_path' 'cache exposes the resolved Unit_mov path'
Assert-Text $header 'unit_sprite_ready' 'cache exposes S sprite readiness separately'
Assert-Text $adapter '3 - direction' 'S visual resolver applies Pro raw-to-visual direction conversion'
$assetText = Get-Content -LiteralPath $adapter -Raw -Encoding UTF8
$expectedFacingMappings = @(
    @{ Direction = 0; Frame = 4; Flip = 'false' },
    @{ Direction = 1; Frame = 0; Flip = 'false' },
    @{ Direction = 2; Frame = 4; Flip = 'true' },
    @{ Direction = 3; Frame = 2; Flip = 'false' }
)
foreach ($mapping in $expectedFacingMappings) {
    $direction = $mapping.Direction
    $frame = $mapping.Frame
    $flip = $mapping.Flip
    $pattern = "(?s)case $direction`:\s*result->frame\s*=\s*$frame;\s*result->flip_x\s*=\s*$flip;\s*break;"
    if ($assetText -notmatch $pattern) {
        throw "BATTLE_ASSET_CACHE_FAIL: Pro visual direction $direction must map to frame $frame with flip_x=$flip"
    }
    Write-Output "[PASS] Pro visual direction $direction maps to frame $frame with flip_x=$flip"
}
if ($assetText -match 'Pmap\.e5|Mmap\.e5') {
    throw 'BATTLE_ASSET_CACHE_FAIL: battle cache must not use E5 map archives'
}

Assert-Text $battlePage 'parse_battle_instructions' 'independent S page calls the Battle parser'
Assert-Text $battlePage 'm_assets\.LoadForDocument' 'S page loads its independent Battle assets'
Assert-Text $battlePage 'decode_battle_hexzmap' 'S page decodes the selected Hexzmap terrain before placement'
Assert-Text $battlePage 'map_jpg_path' 'S page loads the loose JPG path'
Assert-Text $battlePage 'Bitmap::FromFile' 'S page decodes JPG with GDI+'
Assert-Text $battlePage 'fields_decoded' 'S page draws only fully decoded units'
Assert-Text $battlePage 'IsBattleFixedEmptySlot' 'S page excludes fixed empty slots'
Assert-Text $battlePage 'm_editorState\.battle\.units\.size\(\)' `
	'S page visits the complete unit collection without paging'
Assert-Text $battlePage 'm_assets\.ResolveUnitSprite' 'S page uses the independent S sprite resolver'
Assert-Text $battlePage 'ApplyMagentaTransparency' 'S BMP frames use Pro-compatible transparency'
Assert-Text $battlePage 'spriteRef->frame \* kCellPixels' 'S frame order follows the top-down sheet contract'
Assert-Text $battlePage 'ON_WM_HSCROLL|ON_WM_VSCROLL' 'S viewport has native scrollbar handlers'
Assert-Text $battlePage 'WS_HSCROLL \| WS_VSCROLL' 'S page owns the native scrollbar styles'
Assert-Text $battlePage 'OnMButtonDown|OnMButtonUp' 'S viewport supports middle-button panning'
Assert-Text $battlePage 'm_panActive' 'S viewport tracks middle-button drag state'
Assert-Text $battlePage 'ShowScrollBar' 'S viewport shows scrollbars only when content exceeds the client area'
Assert-Text $battlePage 'ZoomAt' 'S viewport keeps cursor-anchored zoom separate from scrolling'
Assert-Text $battlePage 'SetClip' 'S painter clips the native-pixel viewport'
Assert-Text $battlePage 'unit, unit\.direction' 'S painter passes the raw unit direction to the resolver'
Assert-Text $battlePage 'm_assets\.Clear\(' 'S page clears only its own independent assets'
$battleText = Get-Content -LiteralPath $battlePage -Raw -Encoding UTF8
if ($battleText -match 'labelFont|m_pPer2\[unit|BattleWritebackAdapter|BuildBattleWriteback|WriteBattle|SetBattlePage|BattlePageCount|ClearBattleFixedEmptySlot') {
	throw 'BATTLE_ASSET_CACHE_FAIL: S page still uses R names, battle paging, or document writeback APIs'
}
Write-Output '[PASS] S battle page omits unit names, battle pagination, and document writeback APIs'

$rPageText = Get-Content -LiteralPath $rPage -Raw -Encoding UTF8
$refreshStart = $rPageText.IndexOf('void CRScenePreviewPage::ExecutePreviewRefresh(')
$refreshEnd = $rPageText.IndexOf('void CRScenePreviewPage::ExecutePendingPreviewRefresh(', $refreshStart)
if ($refreshStart -lt 0 -or $refreshEnd -le $refreshStart) {
    throw 'BATTLE_ASSET_CACHE_FAIL: R refresh method cannot be isolated'
}
$rRefresh = $rPageText.Substring($refreshStart, $refreshEnd - $refreshStart)
if ($rRefresh -match 'BuildBattlePreviewInput|parse_battle_instructions|战场预览诊断|CczString\.ini 未加载') {
    throw 'BATTLE_ASSET_CACHE_FAIL: R refresh path still classifies/parses S or emits S diagnostics'
}
Write-Output '[PASS] R refresh has no Battle parser or S diagnostic fallback'
Assert-Text $hostSource 'PageForPath|fileName\.rfind\(L"S[_-]"' 'dock host routes S documents to the independent page'
Assert-Text $hostSource 'fileName\.rfind\(L"R[_-]"' 'dock host retains explicit R routing'
Assert-Text $project 'BattlePreviewPage\.cpp' 'independent S page source is in the editor project'
Assert-Text $rPage '\{0x0a, L"' 'R fallback catalog continues to recognize native opcode 0x0A'
Write-Output 'BATTLE_ASSET_CACHE_PASS'
