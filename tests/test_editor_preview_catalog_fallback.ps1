$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$catalogPath = Join-Path $repo 'cczEditor2/PreviewIntegration/EditorPreviewCatalog.cpp'
$catalog = Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8
$expected = '{0x05, L"' + [char]0x53D8 + [char]0x91CF + [char]0x6D4B + [char]0x8BD5 + '"}'
if (-not $catalog.Contains($expected)) {
    throw 'EDITOR_PREVIEW_CATALOG_FAIL: fallback catalog must recognize 0x05 boolean variable tests when CczString.ini is unavailable.'
}

Write-Output '[PASS] Fallback instruction catalog recognizes 0x05 boolean variable tests.'
