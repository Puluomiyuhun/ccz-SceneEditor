param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$ErrorActionPreference = 'Stop'

$manifestPath = Join-Path $PSScriptRoot 'fixtures\preview_internalization\baseline.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: manifest missing: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $fullPath = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $entry.path))
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: file missing: $fullPath"
    }

    $actual = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
    if ($actual -ne $entry.sha256) {
        throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: hash mismatch: $($entry.path)"
    }
}

'PREVIEW_INTERNALIZATION_BASELINE_PASS'
