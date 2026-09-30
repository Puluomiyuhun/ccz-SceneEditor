$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$adapter = Join-Path $root 'cczEditor2\PreviewIntegration\BattleAssetCache.cpp'
$header = Join-Path $root 'cczEditor2\PreviewIntegration\BattleAssetCache.h'
$battlePage = Join-Path $root 'cczEditor2\BattlePreviewPage.cpp'
$rPage = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$catalog = Join-Path $root 'cczEditor2\PreviewIntegration\EditorPreviewCatalog.cpp'
$hostSource = Join-Path $root 'cczEditor2\ScenePreviewHost.cpp'
$project = Join-Path $root 'cczEditor2\cczEditor2.vcxproj'
$bitmapPolicy = Join-Path $root 'cczEditor2\PreviewIntegration\BattleBitmapCachePolicy.cpp'
$bitmapPolicyHeader = Join-Path $root 'cczEditor2\PreviewIntegration\BattleBitmapCachePolicy.h'
$sessionHeader = Join-Path $root 'cczEditor2\PreviewIntegration\BattlePreviewSession.h'
$terrainCacheHeader = Join-Path $root 'cczEditor2\PreviewIntegration\BattleMovementTerrainResourceCache.h'
$opsHeader = Join-Path $root 'cczEditor2\PreviewIntegration\BattleEditorOps.h'

function Assert-Text($path, $pattern, $label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) { throw "BATTLE_ASSET_CACHE_FAIL: $label" }
    Write-Output "[PASS] $label"
}

foreach ($path in @($adapter, $header, $battlePage, $rPage, $catalog, $hostSource, $project,
    $bitmapPolicy, $bitmapPolicyHeader, $sessionHeader, $terrainCacheHeader, $opsHeader)) {
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
Assert-Text $header 'unit_mov_revision' 'cache exposes the Unit_mov file revision for sprite invalidation'
Assert-Text $header 'unit_sprite_ready' 'cache exposes S sprite readiness separately'
Assert-Text $adapter '3 - direction' 'S visual resolver applies Pro raw-to-visual direction conversion'
Assert-Text $adapter 'const bool same_root\s*=\s*key == root_key_ && revision == root_revision_' `
    'changing the map within one unchanged Mod root does not reload its large resources'
Assert-Text $adapter 'root_resource_revision\(root\)[\s\S]*L"Data\.e5"[\s\S]*L"Hexzmap\.e5"[\s\S]*L"Unit_mov\.e5"[\s\S]*L"Ekd5\.exe"' `
    'Mod-root resource caches invalidate when any data, terrain, unit, or appearance file changes'
Assert-Text $adapter 'character_catalog_\s*=\s*ParseBattleCharacterCatalog\(data_e5_\)' `
    'S Data.e5 character and brush records are parsed once per Mod root'
Assert-Text $header 'BattleMovementTerrainResourceCache movement_terrain_cache_' `
    '兵种地形表缓存归 BattleAssetCache 的 Mod 资源层持有'
Assert-Text $terrainCacheHeader 'normalized_mod_root,\s*std::uint64_t data_e5_revision' `
    '兵种地形表缓存按 Mod 根目录与 Data.e5 修订号索引'
Assert-Text $adapter 'movement_terrain_cache_\.Preload\(\s*key,\s*file_revision\(data_path\),\s*data_e5_\)' `
    'Data.e5 已加载后立即预载资源4兵种地形表'
Assert-Text $opsHeader 'std::shared_ptr<const ccz::preview::BattleMovementTerrainTable>\s*movement_terrain' `
    '每个剧本编辑状态只持有共享只读矩阵引用'
Assert-Text $battlePage 'session\.editor_state\.movement_terrain = m_assets\.movement_terrain\(\)[\s\S]*workspace\.second->state\.movement_terrain =\s*m_assets\.movement_terrain\(\)' `
    '激活剧本时把 Mod 缓存引用绑定到会话和条件工作区'
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
Assert-Text $battlePage 'ActiveSession\(\)\.editor_state\.battle\.units\.size\(\)' `
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
Assert-Text $battlePage 'battle_visual_arm_kind\(unit\)' `
    'S sprite faction is derived from the effective deployment instruction, not BattleUnit.unit_type'
$battlePageText = Get-Content -LiteralPath $battlePage -Raw -Encoding UTF8
$clearStart = $battlePageText.IndexOf('void CSBattlePreviewPage::ClearDocument()')
$clearEnd = $battlePageText.IndexOf('void CSBattlePreviewPage::LoadCapturedBattle(', $clearStart)
if ($clearStart -lt 0 -or $clearEnd -le $clearStart) {
    throw 'BATTLE_ASSET_CACHE_FAIL: ClearDocument method cannot be isolated'
}
$clearBody = $battlePageText.Substring($clearStart, $clearEnd - $clearStart)
if ($clearBody -match 'm_assets\.Clear\(|m_decodedBitmaps\.clear\(') {
    throw 'BATTLE_ASSET_CACHE_FAIL: switching S documents must retain reusable resource and image caches'
}
if ($clearBody -notmatch 'ActiveSession\(\)\.baseline_capture\s*=\s*\{\};' -or
	$clearBody -notmatch 'ActiveSession\(\)\.native_writeback_transaction\.Clear\(\)' -or
	$clearBody -notmatch 'ActiveSession\(\)\.editor_state\s*=\s*\{\};' -or
	$clearBody -notmatch 'ActiveSession\(\)\.condition_workspaces\.clear\(\)' -or
	$clearBody -notmatch 'ReleaseActiveBitmapPins\(\)') {
    throw 'BATTLE_ASSET_CACHE_FAIL: document teardown must clear edit/session state while retaining reusable assets'
}
Write-Output '[PASS] document teardown clears all S edit/session records'
Assert-Text $battlePage 'kBattleBitmapCacheBudgetBytes\s*=\s*128u\s*\*\s*1024u\s*\*\s*1024u' `
	'decoded image residency has a fixed 128 MiB budget'
Assert-Text $bitmapPolicy 'RecordDecode|Pin\(|Unpin\(|Contains\(|EvictToBudget|ResidentBytes' `
	'platform-neutral cache policy tracks residency, pins, LRU lookups, and evictions'
Assert-Text $sessionHeader 'BattleBitmapCacheKey map_asset_key[\s\S]*BattleBitmapCacheKey unit_mov_asset_key' `
	'document sessions keep stable asset keys without owning GDI bitmaps'
Assert-Text $battlePage 'FindCachedBitmap\(key\)[\s\S]*StoreCachedBitmap\(key, converted\)' `
	'map preview reuses cached decodes and stores only new decodes'
Assert-Text $battlePage 'resource-[\s\S]*frame-[\s\S]*unit_mov_revision' `
	'unit sprite cache keys include Unit_mov revision, resource index, and frame'
Assert-Text $battlePage 'void CSBattlePreviewPage::UnbindSession\([\s\S]*ReleaseActiveBitmapPins\(\)' `
	'active document bitmap pins are released when its session is parked'
Assert-Text $battlePage 'currentMapImage\s*=\s*ActiveSession\(\)\.baseline_capture\.ok[\s\S]*m_activeMapKey\s*==\s*ActiveSession\(\)\.map_asset_key' `
	'cached JPG is drawn only while it belongs to the active S document'
Assert-Text $battlePage 'm_assets\.character_catalog\(\)' `
    'S character catalog reuses the per-root parsed Data.e5 result'
$editorCatalogPath = Join-Path $root 'cczEditor2\PreviewIntegration\EditorPreviewCatalog.cpp'
Assert-Text $editorCatalogPath 'static std::mutex cache_mutex[\s\S]*cached\.revision == revision' `
    'CczString.ini is read and parsed again only after its file revision changes'
$battleText = Get-Content -LiteralPath $battlePage -Raw -Encoding UTF8
if ($battleText -match 'labelFont|m_pPer2\[unit|SetBattlePage|BattlePageCount|ClearBattleFixedEmptySlot') {
	throw 'BATTLE_ASSET_CACHE_FAIL: S page still uses R names or battle paging'
}
Write-Output '[PASS] S battle page omits R unit names and battle pagination'

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
Assert-Text $catalog '\{0x0a, L"' 'R and S fallback catalog continues to recognize native opcode 0x0A'
Write-Output 'BATTLE_ASSET_CACHE_PASS'
