$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$window = Join-Path $root 'cczEditor2\BattlePreviewPage.cpp'
$header = Join-Path $root 'cczPreviewCore\include\ccz\preview\battle_preview.h'

function Assert-Text([string]$path, [string]$pattern, [string]$label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) {
        throw "BATTLE_CANVAS_PROJECTION_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

if (-not (Test-Path -LiteralPath $window) -or
    -not (Test-Path -LiteralPath $header)) {
    throw 'BATTLE_CANVAS_PROJECTION_FAIL: source missing'
}

# This is a source-contract test, not a substitute for the pending S-window
# smoke test. It protects the independent S page from accidentally falling
# back to the old text-only Battle summary or routing interaction through R.
Assert-Text $window 'constexpr int kCellPixels = 48' `
    'S baseline keeps the documented 48px logical cell evidence'
Assert-Text $window 'width % kCellPixels != 0' `
    'JPG dimensions must prove an integral cell projection'
Assert-Text $window 'ActiveSession\(\)\.editor_state\.battle\.map\.width = width / kCellPixels' `
	'JPG projection publishes logical map width'
Assert-Text $window 'ActiveSession\(\)\.editor_state\.battle\.map\.projection_valid = true' `
	'proven JPG projection enables read-only cell interaction'
Assert-Text $window 'for \(int x = 0; x <= ActiveSession\(\)\.editor_state\.battle\.map\.width; \+\+x\)' `
	'battle canvas paints a logical grid'
Assert-Text $window 'm_hoverCell' `
    'battle canvas paints in-memory hover state'
Assert-Text $window 'identity_label = instruction == 0x4b[\s\S]*?L"【我军】"[\s\S]*?L"【" \+ std::to_wstring\(unit\.data_id\) \+ L"】"' `
    'hover identity column distinguishes own faction from real Data IDs'
Assert-Text $window 'label_body = \(battlefield_index >= 0[\s\S]*?\)\s*\+\s*L"\."\s*\+\s*character_name' `
    'all factions use the same battlefield-number and name body layout'
Assert-Text $window 'MeasureString\(L"【1023】"[\s\S]*?MeasureString\(L"【我军】"[\s\S]*?identity_column_width[\s\S]*?PointF\(hoverOrigin\.X \+ identity_column_width' `
    'hover text reserves a shared fixed-width identity column before the aligned body'
Assert-Text $window 'GetUnitSprite\([\s\S]*?unit, unit\.direction' `
    'decoded battle units request their Unit_mov sprite through the raw-direction resolver'
Assert-Text $window 'std::optional<std::size_t> CSBattlePreviewPage::HitTestUnit' `
    'battle hit-test is implemented by the independent S page'
Assert-Text $window 'const auto coordinate = BattleCoordFromScreen\(point\);[\s\S]*?HitTestUnit\(\*coordinate\)' `
    'battle click resolves a logical cell without entering R handlers'
Assert-Text $header '48px-per-cell map JPG grid' `
    'projection contract records the evidence boundary'

'BATTLE_CANVAS_PROJECTION_PASS_SOURCE_CONTRACT_ONLY'
