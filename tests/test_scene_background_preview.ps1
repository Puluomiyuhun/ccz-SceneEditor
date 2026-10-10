param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$previewPath = Join-Path $SourceRoot 'ScenePreviewWnd.cpp'
$resolverPath = Join-Path $SourceRoot 'SceneBackgroundResolver.h'
$viewPath = Join-Path $SourceRoot 'cczEditor2View.cpp'

if (-not (Test-Path -LiteralPath $previewPath)) {
    throw "ScenePreviewWnd.cpp not found: $previewPath"
}
if (-not (Test-Path -LiteralPath $viewPath)) {
    throw "cczEditor2View.cpp not found: $viewPath"
}
if (-not (Test-Path -LiteralPath $resolverPath)) {
    throw "SceneBackgroundResolver.h not found: $resolverPath"
}

$preview = Get-Content -LiteralPath $previewPath -Raw
$resolver = Get-Content -LiteralPath $resolverPath -Raw
$view = Get-Content -LiteralPath $viewPath -Raw

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "SCENE_BACKGROUND_TEST_FAIL: $Message"
    }
}

$processStart = $preview.IndexOf('case 39: // 0x27:')
$processEnd = $preview.IndexOf('case 48: // 0x30:', $processStart)
if ($processStart -lt 0 -or $processEnd -le $processStart) {
    throw 'SCENE_BACKGROUND_TEST_FAIL: 0x27 ProcessInstruction block not found'
}
$processBlock = $preview.Substring($processStart, $processEnd - $processStart)

# The regression contract is branch-aware: outer scenes use int_data[1],
# interior scenes use int_data[3] plus the interior Mmap.e5 base offset.
Assert-Contains $processBlock 'try_resolve_background_image_index' 'background branch resolver is not used'
Assert-Contains $processBlock 'int_data\[0\]\s*==\s*0' 'outer-scene state mapping is missing'
Assert-Contains $processBlock 'mapImageIndex' 'resolved Mmap.e5 index is not stored separately'
Assert-Contains $resolver 'case 0:' 'outer-scene archive mapping is missing'
Assert-Contains $resolver 'image_index = int_data\[1\]' 'outer-scene archive index is not int_data[1]'
Assert-Contains $resolver 'case 2:' 'interior-scene archive mapping is missing'
Assert-Contains $resolver 'image_index = int_data\[3\] \+ 40' 'interior-scene archive offset changed'

$updateStart = $preview.IndexOf('void CRScenePreviewPage::UpdatePreview(')
$updateEnd = $preview.IndexOf('// ========== 渲染 ==========', $updateStart)
if ($updateStart -lt 0 -or $updateEnd -le $updateStart) {
    throw 'SCENE_BACKGROUND_TEST_FAIL: UpdatePreview block not found'
}
$updateBlock = $preview.Substring($updateStart, $updateEnd - $updateStart)
Assert-Contains $updateBlock 'LoadMapImage\(m_state\.mapImageIndex' 'UpdatePreview does not load the resolved image index'

# cczEditor2View.cpp is the source of the serialized branch encoding.
Assert-Contains $view 'data->int_data\[0\]\s*==\s*0' 'outer-scene serialized index contract changed'
Assert-Contains $view 'data->int_data\[data->int_data\[0\]\s*\+\s*1\]\s*\+\s*1' 'outer-scene serialized offset changed'
Assert-Contains $view 'data->int_data\[0\]\s*==\s*2' 'interior-scene serialized index contract changed'
Assert-Contains $view 'data->int_data\[data->int_data\[0\]\s*\+\s*1\]\s*\+\s*41' 'interior-scene serialized offset changed'

$statusStart = $preview.IndexOf('// 状态信息')
$statusEnd = $preview.IndexOf('// Keep the controls visually grouped', $statusStart)
if ($statusStart -lt 0 -or $statusEnd -le $statusStart) {
    throw 'SCENE_BACKGROUND_TEST_FAIL: R status text block not found'
}
$statusBlock = $preview.Substring($statusStart, $statusEnd - $statusStart)
Assert-Contains $statusBlock 'try_resolve_background_script_number\(\s*renderState\.mapImageIndex,\s*displayedMapNumber\)' 'status bar does not restore the direct script map number'
Assert-Contains $statusBlock 'info\.Format\(_T\("%s\s+地图号:%d\s+角色:%d\s+地图头像:%d"\),\s*sceneLabel,\s*displayedMapNumber' 'scene label is not first or map number label is incorrect'
Assert-Contains $statusBlock 'L"外场景"[\s\S]*?L"内场景"' 'status bar does not distinguish outer and interior scenes'
Assert-Contains $statusBlock 'L"像素坐标"\s*:\s*L"逻辑坐标"' 'coordinate labels do not match their coordinate spaces'

'SCENE_BACKGROUND_TEST_PASS'
