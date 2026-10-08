$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp'
$pageHeaderPath = Join-Path $repo 'cczEditor2/BattlePreviewPage.h'
$sessionHeaderPath = Join-Path $repo 'cczEditor2/PreviewIntegration/BattlePreviewSession.h'
$hostPath = Join-Path $repo 'cczEditor2/ScenePreviewHost.cpp'
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$pageHeader = Get-Content -LiteralPath $pageHeaderPath -Raw -Encoding UTF8
$sessionHeader = Get-Content -LiteralPath $sessionHeaderPath -Raw -Encoding UTF8
$hostSource = Get-Content -LiteralPath $hostPath -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) {
        throw "BATTLE_DYNAMIC_PERSISTENCE_FAIL: $message"
    }
    Write-Output "[PASS] $message"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_DYNAMIC_PERSISTENCE_FAIL: Missing function: $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_DYNAMIC_PERSISTENCE_FAIL: Missing function body: $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_DYNAMIC_PERSISTENCE_FAIL: Unbalanced function: $signature"
}

$bind = Get-FunctionBlock $page 'void CSBattlePreviewPage::BindSession('
$unbind = Get-FunctionBlock $page 'void CSBattlePreviewPage::UnbindSession()'
$saveUi = Get-FunctionBlock $page 'void CSBattlePreviewPage::SaveActiveSessionUiState()'
$restoreUi = Get-FunctionBlock $page 'void CSBattlePreviewPage::RestoreActiveSessionUiState()'
$clearDocument = Get-FunctionBlock $page 'void CSBattlePreviewPage::ClearDocument()'
$requestRefresh = Get-FunctionBlock $page 'bool CSBattlePreviewPage::RequestPreviewRefresh('
$dynamicToggle = Get-FunctionBlock $page 'void CSBattlePreviewPage::OnDynamicPreviewClicked()'

Assert-Text $sessionHeader 'struct BattlePreviewUiState\s*\{[\s\S]*bool dynamic_preview_enabled\{\};' `
    'Dynamic preview activation is stored with its S document session.'
Assert-Text $saveUi 'ui\.dynamic_preview_enabled\s*=\s*m_dynamicPreviewEnabled' `
    'Parking a document saves its dynamic-preview activation state.'
Assert-Text $restoreUi 'm_dynamicPreviewEnabled\s*=\s*ui\.dynamic_preview_enabled' `
    'Rebinding a document restores its dynamic-preview activation state.'
Assert-Text $bind 'if\s*\(sessionChanged\)\s*RefreshDynamicToggle\(\)' `
    'Rebinding a document synchronizes the dynamic-preview button label with that session.'
Assert-Text $hostSource 'BattlePreviewActivationCheck::ReuseSession\s*&&\s*!session\.ui\.dynamic_preview_enabled' `
    'The host does not take its unchanged-session fast path when the dynamic snapshot must be rebuilt.'
Assert-Text $unbind 'SaveActiveSessionUiState\(\);[\s\S]*m_dynamicPreviewReady\s*=\s*false' `
    'Unbinding saves the active mode before dropping transient preview data.'
Assert-Text $clearDocument 'm_dynamicPreviewReady\s*=\s*false' `
    'Clearing a stale snapshot invalidates the result that must be rebuilt.'
if ($clearDocument -match 'm_dynamicPreviewEnabled\s*=\s*false') {
    throw 'BATTLE_DYNAMIC_PERSISTENCE_FAIL: Clearing/reloading a snapshot must not close dynamic preview.'
}
Write-Output '[PASS] Clearing/reloading a snapshot does not close dynamic preview.'
Assert-Text $dynamicToggle 'ActiveSession\(\)\.ui\.dynamic_preview_enabled\s*=\s*false' `
    'Only the explicit close action clears the session activation state.'
Assert-Text $dynamicToggle 'ActiveSession\(\)\.ui\.dynamic_preview_enabled\s*=\s*true' `
    'Opening dynamic preview records the active mode in the session.'
if ([regex]::Matches($page, 'm_dynamicPreviewEnabled\s*=\s*false').Count -ne 1) {
    throw 'BATTLE_DYNAMIC_PERSISTENCE_FAIL: Runtime deactivation must occur only in the explicit close action.'
}
Write-Output '[PASS] No refresh, rebind, or stale-state path silently deactivates dynamic preview.'

Assert-Text $requestRefresh 'if\s*\(m_dynamicPreviewEnabled\s*&&\s*m_dynamicPreviewReady[\s\S]*RefreshDynamicPreview\(\s*tree,\s*selectedItem,\s*documentPath\s*\)' `
    'Section selection refreshes the active snapshot without re-running ordinary preview capture when its source version is unchanged.'
Assert-Text $requestRefresh 'if\s*\(ActiveSession\(\)\.stale\)\s*\{\s*if\s*\(m_dynamicPreviewEnabled\)\s*RefreshDynamicPreview\(\s*tree,\s*selectedItem,\s*documentPath\s*\)' `
    'A stale editor baseline does not suppress read-only dynamic preview refresh.'
Assert-Text $requestRefresh 'CheckBattlePreviewActivation\([\s\S]*if\s*\(m_dynamicPreviewEnabled\)\s*RefreshDynamicPreview\(\s*tree,\s*selectedItem,\s*documentPath\s*\)' `
    'A changed source version continues through ordinary freshness checks, then refreshes the dynamic snapshot.'
if ($requestRefresh -match '剧本内容或树结构已变化，动态预览结果已失效') {
    throw 'BATTLE_DYNAMIC_PERSISTENCE_FAIL: A source-version mismatch must not latch the dynamic preview into an invalid state.'
}
Write-Output 'BATTLE_DYNAMIC_PREVIEW_PERSISTENCE_PASS'
