$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$catalogHeader = Join-Path $root 'cczEditor2/PreviewIntegration/BattleCharacterCatalog.h'
$catalogSource = Join-Path $root 'cczEditor2/PreviewIntegration/BattleCharacterCatalog.cpp'

if (-not (Test-Path -LiteralPath $catalogHeader)) {
    throw 'BattleCharacterCatalog.h is not implemented yet'
}
if (-not (Test-Path -LiteralPath $catalogSource)) {
    throw 'BattleCharacterCatalog.cpp is not implemented yet'
}

$header = Get-Content -LiteralPath $catalogHeader -Raw
$source = Get-Content -LiteralPath $catalogSource -Raw
foreach ($contract in @('BattleCharacterCatalog', 'BattleMinionBrush', 'ParseBattleCharacterCatalog')) {
    if ($header -notmatch [regex]::Escape($contract)) {
        throw "Missing catalog API contract: $contract"
    }
}
foreach ($contract in @('kDataHeader', 'kDataRecordStride', 'kNameFieldWidth', 'kMovementClassOffset', 'CP936')) {
    if ($source -notmatch [regex]::Escape($contract)) {
        throw "Missing Data.e5 parsing contract: $contract"
    }
}
if ($source -notmatch 'kNameFieldWidth\s*=\s*12\s*;') {
    throw 'Data.e5 brush names must match Pro: 12 bytes'
}
Write-Output 'S Battle character catalog contract: PASS'
