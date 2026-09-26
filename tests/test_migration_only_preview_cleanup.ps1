$ErrorActionPreference = 'Stop'

$sourceRoot = Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2'
$resolverHeader = Join-Path $sourceRoot 'PreviewIntegration\FreeRPreviewResolver.h'
if (-not (Test-Path -LiteralPath $resolverHeader -PathType Leaf)) {
    throw "MIGRATION_ONLY_PREVIEW_CLEANUP_FAIL: missing resolver header: $resolverHeader"
}

$text = Get-Content -LiteralPath $resolverHeader -Raw -Encoding UTF8
if ($text -match '\bResolveLimitedFreeRPreview\b') {
    throw 'MIGRATION_ONLY_PREVIEW_CLEANUP_FAIL: dead ResolveLimitedFreeRPreview compatibility overload remains'
}

Write-Output 'MIGRATION_ONLY_PREVIEW_CLEANUP_PASS: no confirmed dead external-runtime compatibility overloads'
