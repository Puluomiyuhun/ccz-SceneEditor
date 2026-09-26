param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$headerPath = Join-Path $SourceRoot 'ScenePositionSource.h'
$coordinatePath = Join-Path $SourceRoot 'SceneCoordinate.h'
$previewPath = Join-Path $SourceRoot 'ScenePreviewWnd.cpp'
$previewHeaderPath = Join-Path $SourceRoot 'ScenePreviewWnd.h'

foreach ($path in @($headerPath, $coordinatePath, $previewPath, $previewHeaderPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "SCENE_POSITION_SOURCE_TEST_FAIL: missing $path"
    }
}

$source = Get-Content -LiteralPath $headerPath -Raw
$coordinate = Get-Content -LiteralPath $coordinatePath -Raw
$preview = Get-Content -LiteralPath $previewPath -Raw
$previewHeader = Get-Content -LiteralPath $previewHeaderPath -Raw

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "SCENE_POSITION_SOURCE_TEST_FAIL: $Message"
    }
}

Assert-Contains $source 'ActorAppear' 'actor appear source kind is missing'
Assert-Contains $source 'ActorMove' 'actor move source kind is missing'
Assert-Contains $source 'PortraitShow' 'portrait show source kind is missing'
Assert-Contains $source 'PortraitMove' 'portrait move source kind is missing'
Assert-Contains $source 'recordActor' 'actor source tracker is missing'
Assert-Contains $source 'recordPortrait' 'portrait source tracker is missing'
Assert-Contains $source 'hidePortrait' 'portrait hide reset is missing'

Assert-Contains $coordinate 'OuterPixels' 'outer pixel coordinate space is missing'
Assert-Contains $coordinate 'InnerLogical' 'inner logical coordinate space is missing'
Assert-Contains $coordinate 'screen_to_scene_point' 'coordinate conversion helper is missing'

Assert-Contains $previewHeader 'positionSource' 'character position provenance is missing'
Assert-Contains $previewHeader 'mapPortraitPositionSources' 'portrait position provenance is missing'
Assert-Contains $preview 'ProcessInstruction\(ItemData\* data, HTREEITEM sourceItem\)' 'instruction source handle is not propagated'
Assert-Contains $preview 'case 50: // 0x32: 武将移动' 'actor move source update is missing'
Assert-Contains $preview 'case 42: // 0x2A: 地图头像移动' 'portrait move source update is missing'
Assert-Contains $preview '\.positionSource\s*=\s*MakePositionSource' 'actor position source is not stored'
Assert-Contains $preview 'mapPortraitPositionSources' 'portrait source is not stored'
Assert-Contains $preview 'm_state\.coordinateSpace' 'scene coordinate space is not stored'
Assert-Contains $preview 'OuterPixels' 'outer coordinate branch is missing'
Assert-Contains $preview 'InnerLogical' 'inner coordinate branch is missing'
Assert-Contains $preview 'm_bPortraitAdjustMode' 'portrait position adjustment mode is missing'
Assert-Contains $preview 'HitTestMapPortrait' 'portrait hit testing is missing'
Assert-Contains $preview 'CommitMapPortraitPosition' 'portrait position commit is missing'
# Keep this test ASCII-only so it runs consistently under Windows PowerShell
# 5.1, whose no-BOM script decoding follows the system code page.
Assert-Contains $preview 'OuterPixels' 'outer coordinate label branch is missing'
Assert-Contains $preview 'ID_SCENE_ADJUST' 'portrait adjustment menu command is missing'

# Position-only writeback must use the tracked source indices, not hard-code 0x30.
Assert-Contains $preview 'entry\.source\.source\.xIndex' 'position writeback does not use source X index'
Assert-Contains $preview 'entry\.source\.source\.yIndex' 'position writeback does not use source Y index'
Assert-Contains $preview 'source\.source\.instructionId' 'position writeback does not validate source instruction'

'SCENE_POSITION_SOURCE_TEST_PASS'
