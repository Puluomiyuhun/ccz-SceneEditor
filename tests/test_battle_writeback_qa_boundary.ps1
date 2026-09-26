$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repo 'cczEditor2\cczEditor2.vcxproj'
$pagePath = Join-Path $repo 'cczEditor2\BattlePreviewPage.cpp'
$headerPath = Join-Path $repo 'cczEditor2\BattlePreviewPage.h'
$qaGatePath = Join-Path $repo 'cczEditor2\PreviewIntegration\BattleWritebackQaGate.h'
$project = [xml](Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8)
$page = Get-Content -LiteralPath $pagePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$qaGate = Get-Content -LiteralPath $qaGatePath -Raw -Encoding UTF8

function Assert-Condition([bool]$condition, [string]$message) {
    if (-not $condition) { throw "BATTLE_WRITEBACK_QA_FAIL: $message" }
    Write-Output "[PASS] $message"
}

$macroGroups = @()
foreach ($group in $project.Project.ItemDefinitionGroup) {
    $definitions = $group.ClCompile.PreprocessorDefinitions
    if ($definitions -and $definitions.Contains('CCZ_S_BATTLE_WRITEBACK_QA')) {
        $macroGroups += $group
    }
}
Assert-Condition ($macroGroups.Count -eq 1) `
    'QA macro is declared in exactly one build configuration'
$condition = $macroGroups[0].Condition
Assert-Condition ($condition -match "Debug\|Win32" -and
    $condition -match "CCZSBattleWritebackQa" -and
    $condition -match "true") `
    'QA macro requires explicit Debug Win32 QA build property'
Assert-Condition ($header -match '(?s)#if defined\(_DEBUG\) && defined\(CCZ_S_BATTLE_WRITEBACK_QA\).*?OnQaWriteback') `
    'QA command handler is excluded from ordinary builds'
Assert-Condition ($header -match '(?s)#if defined\(_DEBUG\) && defined\(CCZ_S_BATTLE_WRITEBACK_QA\).*?m_qaWritebackButton') `
    'QA button storage is excluded from ordinary builds'
Assert-Condition ($page -match '(?s)#if defined\(_DEBUG\) && defined\(CCZ_S_BATTLE_WRITEBACK_QA\).*?ON_BN_CLICKED\(kQaWritebackId, &CSBattlePreviewPage::OnQaWriteback\)') `
    'QA message route is compile-time gated'
Assert-Condition ($page -match 'IsBattleWritebackQaCopy\(') `
    'QA handler checks the marked S-copy boundary'
Assert-Condition ($qaGate -match 'BattleWritebackQaCopyRootForExecutable[\s\S]*?writeback-qa-copies' -and
    $qaGate -match 'CCZ_S_BATTLE_WRITEBACK_QA_V1') `
    'QA copies use the executable-adjacent folder and exact marker content'
Assert-Condition ($qaGate -match 'weakly_canonical' -and
    $qaGate -match 'normalized\(document\.parent_path\(\)\)\s*!=\s*normalized\(root\)' -and
    $qaGate -match 's_prefix\s*&&\s*extension\s*==\s*L"\.eex"') `
    'QA gate accepts only direct S_*.eex / S-*.eex files in the marked folder'
Assert-Condition ($qaGate -notmatch '\b(std::ofstream|CreateFileW|WriteFile)\b') `
    'QA-copy boundary check is read-only'
Assert-Condition ($page -match 'ApplyPendingNativeWritebackTransaction\(') `
    'QA path and evidence path share the native transaction core'
Assert-Condition ($page -match 'ApplyPendingNativeWritebackForEvidence[\s\S]*?native_writeback_evidence_ready[\s\S]*?ApplyPendingNativeWritebackTransaction') `
    'product evidence gate remains ahead of the shared transaction core'
Assert-Condition ($page -match 'ApplyPendingNativeWritebackForEvidence[\s\S]*?ApplyPendingNativeWritebackTransaction\(view, selectedItem,[\s\S]*?diagnostic, false\)') `
    'product evidence path preserves the existing empty-batch contract'
Assert-Condition ($page -match 'IsCurrentQaCopy\(\) const[\s\S]*?IsBattleWritebackQaCopy' -and
    $page -match 'ApplyPendingNativeWritebackForQaCopy[\s\S]*?IsCurrentQaCopy\(\)[\s\S]*?ApplyPendingNativeWritebackTransaction\(view, section, diagnostic, true\)') `
    'QA path applies only after the marked-copy gate'
Assert-Condition ($page -match 'm_qaWritebackButton\.EnableWindow\(m_editorOps != nullptr && IsCurrentQaCopy\(\)\)') `
    'QA writeback stays disabled until an S preview and marked QA copy are active'
$qaHandlerMatch = [regex]::Match($page,
    '(?s)void CSBattlePreviewPage::OnQaWriteback\(\)\s*\{(.*?)\n\}\s*#endif')
Assert-Condition ($qaHandlerMatch.Success -and
    $qaHandlerMatch.Groups[1].Value -match 'MB_YESNO' -and
    $qaHandlerMatch.Groups[1].Value -match '不会直接改写磁盘文件' -and
    $qaHandlerMatch.Groups[1].Value -match '编辑器正常保存' -and
    $qaHandlerMatch.Groups[1].Value -match '关闭并重新打开' -and
    $qaHandlerMatch.Groups[1].Value -notmatch '\b(CreateFileW|WriteFile|std::ofstream|fopen)\b') `
    'QA transaction is confirmed in memory; the user saves and reopens through cczEditor'
Assert-Condition ($page -notmatch 'ApplyPendingNativeWritebackForQaCopy[\s\S]{0,1200}BattleWritebackEvidence\s*\{') `
    'QA path does not fabricate release evidence'

Write-Output 'BATTLE_WRITEBACK_QA_BOUNDARY_PASS'
