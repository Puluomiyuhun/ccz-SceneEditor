$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Assert-File([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "PREVIEW_PAGE_ISOLATION_FAIL: missing $relativePath"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Assert-Text([string]$text, [string]$pattern, [string]$label) {
    if ($text -notmatch $pattern) {
        throw "PREVIEW_PAGE_ISOLATION_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

function Assert-NotText([string]$text, [string]$pattern, [string]$label) {
    if ($text -match $pattern) {
        throw "PREVIEW_PAGE_ISOLATION_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

$hostHeader = Assert-File 'cczEditor2\ScenePreviewHost.h'
$hostSource = Assert-File 'cczEditor2\ScenePreviewHost.cpp'
$rHeader = Assert-File 'cczEditor2\ScenePreviewWnd.h'
$rSource = Assert-File 'cczEditor2\ScenePreviewWnd.cpp'
$battleHeader = Assert-File 'cczEditor2\BattlePreviewPage.h'
$battleSource = Assert-File 'cczEditor2\BattlePreviewPage.cpp'
$battleAdapter = Assert-File 'cczEditor2\PreviewIntegration\BattlePreviewAdapter.cpp'

Assert-Text $hostHeader 'class\s+CScenePreviewWnd\s*:\s*public\s+CDockablePane' `
    'dock pane remains a lightweight page host'
Assert-Text $rHeader 'class\s+CRScenePreviewPage\s*:\s*public\s+CWnd' `
    'R preview is a distinct child page'
Assert-Text $battleHeader 'class\s+CSBattlePreviewPage\s*:\s*public\s+CWnd' `
    'S battle preview is a distinct child page'
Assert-Text $hostSource 'PageForPath|fileName\.rfind\(L"S[_-]"' `
    'host routes a document before dispatching page refresh'
Assert-Text $hostSource 'fileName\.rfind\(L"R[_-]"' `
    'host retains an explicit R route'
Assert-Text $hostSource 'return Page::Neutral;' `
    'unknown document prefixes do not fall through to either preview page'
Assert-Text $hostSource 'L"请加载R剧本或S剧本"' `
    'neutral preview state asks the user to load an R or S script'
Assert-Text $hostSource 'SW_HIDE|ShowWindow\(SW_' `
    'host switches child-page visibility without replacing the dock pane'
Assert-Text $hostSource 'InitCharList' 'existing character list API remains forwarded for R compatibility'
Assert-NotText $hostSource 'm_sPage->InitCharList|m_sPage\.InitCharList' `
    'host never forwards the R character table to S'

$battleOnlyDependencies = 'm_pPer2|InitCharList|PopulateBattleCharacterCatalog|IDC_SCENE_CHAR_LIST|CczString\.ini|PreviewAssetCache|PreviewSession|m_previewCatalog|DialogueProfile|DynamicPlayback'
Assert-NotText $battleHeader $battleOnlyDependencies `
    'S page declaration has no R table, cache, session, catalog or playback state'
Assert-NotText $battleSource $battleOnlyDependencies `
    'S page implementation has no R table, cache, session, catalog or playback path'
Assert-Text $battleSource 'CaptureEditorTreeSnapshot|CapturedEditorTree' `
    'S page captures the generic value-owned editor tree directly'
Assert-Text $battleSource 'parse_battle_instructions' `
    'S page invokes the Battle parser independently'
Assert-Text $battleSource 'BattleAssetCache|map_jpg_path|ResolveUnitSprite' `
    'S page owns Battle map and Unit_mov asset resolution'
Assert-Text $battleSource 'WS_HSCROLL|WS_VSCROLL|OnHScroll|OnVScroll' `
    'S page owns its scrollable viewport'
Assert-Text $battleSource 'm_panActive|OnMButtonDown|OnMButtonUp' `
    'S page owns middle-button panning'
Assert-Text $battleSource 'm_zoom|Zoom' 'S page owns map zoom state'
Assert-NotText $battleSource 'm_pPer2|InitCharList|PopulateBattleCharacterCatalog|IDC_SCENE_CHAR_LIST' `
    'S page has no R-derived character table; its own role-list pagination is allowed'
Assert-NotText $battleAdapter 'BuildEditorPreviewInput\s*\(' `
    'Battle adapter does not route S snapshots through the R semantic adapter'
Assert-Text $battleAdapter 'CapturedEditorTree|document_roots' `
    'Battle adapter converts the generic captured tree directly'
$rRefreshStart = $rSource.IndexOf('void CRScenePreviewPage::ExecutePreviewRefresh(')
$rRefreshEnd = $rSource.IndexOf('void CRScenePreviewPage::ExecutePendingPreviewRefresh(', $rRefreshStart)
if ($rRefreshStart -lt 0 -or $rRefreshEnd -le $rRefreshStart) {
    throw 'PREVIEW_PAGE_ISOLATION_FAIL: cannot isolate the R refresh entrypoint'
}
$rRefresh = $rSource.Substring($rRefreshStart, $rRefreshEnd - $rRefreshStart)
Assert-NotText $rRefresh 'BuildBattlePreviewInput|parse_battle_instructions|CczString\.ini 未加载' `
    'R refresh cannot classify S files or display S-only diagnostics'
$rBattleLegacyPath = 'BattleAssetCache|BattleEditorOps|BattleInteraction|BattlePreviewState|BattleCoord|m_battle|ID_BATTLE|parse_battle_instructions|BuildBattlePreviewInput'
Assert-NotText ($rHeader + $rSource) $rBattleLegacyPath `
    'R page contains no legacy S state, parser, assets, controls, or interaction paths'

$mainFrame = Assert-File 'cczEditor2\MainFrm.h'
Assert-Text $mainFrame 'ScenePreviewHost\.h' `
    'MainFrame embeds the split-page host rather than the old mixed pane'

Write-Output 'PREVIEW_PAGE_ISOLATION_PASS'
