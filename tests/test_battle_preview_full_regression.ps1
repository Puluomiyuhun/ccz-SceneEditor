param(
    [switch]$SkipBuild,
    [switch]$SkipExistingRegression
)

$ErrorActionPreference = 'Stop'

$editorRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$coreProject = Join-Path $editorRoot 'cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'
$editorProject = Join-Path $editorRoot 'cczEditor2\cczEditor2.vcxproj'
$opsHeader = Join-Path $editorRoot 'cczEditor2\PreviewIntegration\BattleEditorOps.h'
$opsSource = Join-Path $editorRoot 'cczEditor2\PreviewIntegration\BattleEditorOps.cpp'
$opsTests = Join-Path $editorRoot 'tests\preview_integration\battle_editor_ops_tests.cpp'
$testMain = Join-Path $editorRoot 'cczPreviewCoreTests\tests\test_main.cpp'
$windowHeader = Join-Path $editorRoot 'cczEditor2\ScenePreviewWnd.h'
$windowSource = Join-Path $editorRoot 'cczEditor2\ScenePreviewWnd.cpp'
$battlePageHeader = Join-Path $editorRoot 'cczEditor2\BattlePreviewPage.h'
$battlePageSource = Join-Path $editorRoot 'cczEditor2\BattlePreviewPage.cpp'
$hostSource = Join-Path $editorRoot 'cczEditor2\ScenePreviewHost.cpp'
$filters = Join-Path $editorRoot 'cczPreviewCoreTests\cczPreviewCoreTests.vcxproj.filters'
$resourceHeader = Join-Path $editorRoot 'cczEditor2\resource.h'
$reportPath = Join-Path $editorRoot '.superpowers\sdd\2026-09-21-battle-preview-port\task-6-implementation-report.md'

function Assert-Text([string]$path, [string]$pattern, [string]$label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

function Assert-NotText([string]$path, [string]$pattern, [string]$label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -match $pattern) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

foreach ($path in @($coreProject, $editorProject, $opsHeader, $opsSource,
        $opsTests, $testMain, $windowHeader, $windowSource, $filters, $resourceHeader,
        $battlePageHeader, $battlePageSource, $hostSource, $reportPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: missing $path"
    }
}

Assert-Text $opsHeader 'BattlePainterMode' 'six painter API is declared'
foreach ($painter in @('Appearance', 'Hidden', 'Direction', 'Level',
        'UnitType', 'Target')) {
    Assert-Text $opsHeader $painter "painter mode $painter is declared"
}
Assert-Text $opsSource 'BattleWritebackTransaction' 'Task 5 value transaction is used'
Assert-NotText $opsSource 'Task 5 写回门仍关闭' `
    'preview edit status no longer claims that product writeback is disabled'
Assert-Text $opsSource 'BattleEditorOps::TerrainAllows' 'terrain placement gate is centralized in the editor operations'
Assert-Text $opsSource 'battle_movement_terrain_cost' 'terrain placement uses the selected Data character movement class'
Assert-Text $opsSource 'if \(\*cost == 0xff\)' 'impassable movement cost rejects placement'
Assert-Text $opsSource 'view=all' 'all source-record groups share one battle view'
Assert-Text $opsSource 'InScope' 'battle scope filter is implemented'
Assert-Text $opsSource 'ClearUndo' 'temporary reset can clear the undo stack'
Assert-Text $opsHeader 'BattlePageCount' 'Battle record page count is separate from character pages'
Assert-Text $opsHeader 'SetBattlePage' 'Battle record page switch API is declared'
Assert-Text $opsSource 'IsBattleFixedEmptySlot' 'fixed-slot sentinel gate is used'
Assert-Text $opsSource 'editor_pending_delete\s*=\s*true' `
    'fixed-slot delete remains staged as a tombstone for writeback and undo'
Assert-Text $opsSource 'state\.placement_defaults = BattlePlacementDefaults\{\}' `
    'character scope changes reset all placement defaults'
Assert-Text $opsSource 'state\.direction_locked = direction_locked' `
    'character scope changes preserve independent direction lock'
Assert-Text $opsSource 'Appearance is intentionally not marked dirty' 'appearance remains a visual-only bridge field'
Assert-Text $battlePageHeader 'class\s+CSBattlePreviewPage\s*:\s*public\s+CWnd' `
    'S preview is its own native child page'
Assert-Text $battlePageSource 'BuildBattlePreviewInput' `
    'S page consumes the direct generic-tree battle adapter'
Assert-Text $battlePageSource 'parse_battle_instructions' `
    'S page owns its battle parser call'
Assert-Text $battlePageSource 'm_assets\.LoadForDocument' `
    'S page owns the battle-specific asset cache'
Assert-Text $battlePageSource 'fields_decoded' `
    'S page renders only fully decoded units'
Assert-Text $battlePageSource 'm_assets\.ResolveUnitSprite' `
    'S page resolves Unit_mov sprites and direction independently'
Assert-Text $battlePageSource 'ActiveSession\(\)\.editor_state\.battle\.units\.size\(\)' `
	'S page renders the complete unit set from its active document session instead of paging'
Assert-Text $battlePageSource 'm_editorOps->SelectMinionBrush|SelectMinionBrush\(' `
	'S page binds the S-only minion brush through its battle editor model'
Assert-Text $battlePageSource 'm_editorOps->SelectCharacter|SelectCharacter\(' `
	'S page binds the S-only role list through its battle editor model'
Assert-Text $battlePageSource 'm_editorOps->SetDeploymentInstruction|SetDeploymentInstruction\(' `
	'S page selects and binds a concrete 0x46/0x47 placement instruction'
Assert-Text $battlePageSource 'OnLButtonDown[\s\S]*?StampAt\(' `
	'S page routes left-button placement into brush stamping'
Assert-Text $battlePageSource 'OnRButtonDown[\s\S]*?DeleteAt\(' `
	'S page routes right-button removal into a temporary delete transaction'
Assert-Text $battlePageSource 'WS_HSCROLL \| WS_VSCROLL' `
    'S page owns map scrolling styles'
Assert-NotText $battlePageSource 'm_pPer2|InitCharList|PopulateBattleCharacterCatalog|RCharacter|RRole|RPreviewSession|RResourceCache|SetBattlePage|BattlePageCount|ClearBattleFixedEmptySlot' `
	'S page has no R role/resource/session leak or legacy R battle paging'
Assert-Text $battlePageSource 'm_nativeWritebackTransaction|BuildBattleWritebackNativeBatch' `
	'S-only QA writeback remains on the independent battle page'
Assert-Text $hostSource 'PageForPath|fileName\.rfind\(L"S[_-]"' `
    'host routes S documents by filename prefix'
Assert-Text $hostSource 'm_sPage\.RequestPreviewRefresh' `
    'S documents refresh only the independent S page'
Assert-NotText $hostSource 'm_sPage->InitCharList|m_sPage\.InitCharList' `
    'host never forwards the R role table to S'
$windowSourceText = Get-Content -LiteralPath $windowSource -Raw -Encoding UTF8
$rRefreshStart = $windowSourceText.IndexOf('void CRScenePreviewPage::ExecutePreviewRefresh(')
$rRefreshEnd = $windowSourceText.IndexOf('void CRScenePreviewPage::ExecutePendingPreviewRefresh(', $rRefreshStart)
if ($rRefreshStart -lt 0 -or $rRefreshEnd -le $rRefreshStart) {
    throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: R refresh method cannot be isolated'
}
$rRefresh = $windowSourceText.Substring($rRefreshStart, $rRefreshEnd - $rRefreshStart)
if ($rRefresh -match 'BuildBattlePreviewInput|parse_battle_instructions|PopulateBattleCharacterCatalog') {
    throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: S processing remains in the R refresh path'
}
Write-Output '[PASS] R refresh does not classify, parse, or load S character data'
Assert-Text $testMain '\[FAIL\] no tests selected' 'unmatched CoreTests filters fail instead of passing green'
Assert-Text $filters 'battle_editor_ops_tests.cpp' 'Task 6 focused test is present in project filters'
if ($opsSourceText = Get-Content -LiteralPath $opsSource -Raw -Encoding UTF8) {
    if ($opsSourceText -match 'SceneMultiSelectOps|ApplyMulti|MultiMove|PrimaryCharacter') {
        throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: R multi-select/group-move code leaked into BattleEditorOps'
    }
}
Assert-Text $opsTests 'battle_editor_placement_preview_and_terrain_gate_are_atomic' `
    'placement and terrain gate test exists'
Assert-Text $opsTests 'battle_editor_place_works_with_decoded_hexzmap_terrain' `
    'decoded Hexzmap data reaches the actual placement preview gate'
Assert-Text $opsTests 'battle_editor_character_search_clears_place_context_and_rejects_stale_place' `
    'character search clears stale Place state test exists'
Assert-Text $opsTests 'battle_editor_character_page_clears_place_context_and_rejects_stale_place' `
    'character page clears stale Place state test exists'
Assert-Text $opsTests 'battle_editor_painters_filter_cell_and_scope' `
    'six painter all-unit-cell/scope test exists'
Assert-Text $opsTests 'battle_editor_wheel_direction_and_lock_only_sync_current_defaults' `
    'wheel direction/lock test exists'
Assert-Text $opsTests 'battle_editor_fixed_47_delete_stages_tombstone_and_undo_restores_row' `
	'0x47 complete 80-slot delete/undo/build test exists'
Assert-Text $opsTests 'battle_editor_partial_47_delete_undo_and_build_rejects_missing_row' `
    '0x47 partial-group delete/undo/build rejection test exists'
Assert-Text $opsTests 'battle_editor_appearance_is_visual_only_and_undo_restores_without_raw_output' `
    'appearance read-only/raw preservation contract is tested'
Assert-Text $opsTests 'value_adapter_test_evidence' `
    'value adapter evidence fixture is explicitly synthetic'
Assert-Text $resourceHeader 'ID_BATTLE_TEMP_RESET' `
    'Battle temporary reset toolbar command is declared'
Assert-Text $resourceHeader 'ID_BATTLE_PAGE_NEXT' `
    'Battle temporary page command is declared'

$msbuildCandidates = @(
    (Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$msbuild = $msbuildCandidates | Select-Object -First 1
if (-not $SkipBuild) {
    if (-not $msbuild) {
        throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: MSBuild not found'
    }
    foreach ($configuration in @('Debug', 'Release')) {
        foreach ($project in @($coreProject, $editorProject)) {
            $buildOutput = & $msbuild $project /t:Build "/p:Configuration=$configuration" `
                /p:Platform=Win32 /m:1 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: $configuration build failed for $project`n$($buildOutput -join [Environment]::NewLine)"
            }
            Write-Output "[PASS] $configuration Win32 build: $(Split-Path $project -Leaf)"
        }
    }
}

$runners = @(
    (Join-Path $editorRoot 'cczPreviewCoreTests\Debug\cczPreviewCoreTests.exe'),
    (Join-Path $editorRoot 'cczPreviewCoreTests\Release\cczPreviewCoreTests.exe')
)
$expectedBattleEditorTests = @(
    'battle_editor_character_list_pages_and_search_without_cross_scope',
    'battle_editor_character_search_clears_place_context_and_rejects_stale_place',
    'battle_editor_character_page_clears_place_context_and_rejects_stale_place',
    'battle_editor_placement_preview_and_terrain_gate_are_atomic',
    'battle_editor_place_works_with_decoded_hexzmap_terrain',
    'battle_editor_modify_delete_and_undo_preserve_raw_fields',
    'battle_editor_wheel_direction_and_lock_only_sync_current_defaults',
    'battle_editor_painters_filter_cell_and_scope',
    'battle_editor_target_painter_resolves_scope_data_or_cell',
    'battle_editor_pan_is_clamped_to_proven_map_bounds',
    'battle_editor_rejects_unknown_or_cross_battle_scope_records',
    'battle_editor_fixed_slots_choose_first_middle_last_and_preserve_framing',
    'battle_editor_fixed_delete_stages_tombstone_and_undo_restores_row',
    'battle_editor_appearance_is_visual_only_and_undo_restores_without_raw_output',
    'battle_editor_shows_all_battle_record_groups_without_visible_paging',
    'battle_editor_fixed_47_delete_stages_tombstone_and_undo_restores_row',
    'battle_editor_partial_47_delete_undo_and_build_rejects_missing_row',
    'battle_editor_character_search_clears_selected_minion_brush'
)
foreach ($runner in $runners) {
    if (-not (Test-Path -LiteralPath $runner)) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: runner missing $runner"
    }
    $focusedOutput = & $runner 'battle_editor' 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: focused BattleEditorOps tests failed ($runner)`n$($focusedOutput -join [Environment]::NewLine)"
    }
    $focusedText = $focusedOutput -join [Environment]::NewLine
    foreach ($testName in $expectedBattleEditorTests) {
        if ($focusedText -notmatch ('\[PASS\]\s+' + [regex]::Escape($testName))) {
            throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: focused runner omitted/failed $testName ($runner)"
        }
    }
    $fullOutput = & $runner 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: full CoreTests failed ($runner)`n$($fullOutput -join [Environment]::NewLine)"
    }
    $fullText = $fullOutput -join [Environment]::NewLine
    foreach ($testName in $expectedBattleEditorTests) {
        if ($fullText -notmatch ('\[PASS\]\s+' + [regex]::Escape($testName))) {
            throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: full runner omitted/failed $testName ($runner)"
        }
    }
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $emptyFilterOutput = & $runner '__task6_no_such_test__' 2>&1
    $emptyFilterExit = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($emptyFilterExit -eq 0 -or
        (($emptyFilterOutput -join [Environment]::NewLine) -notmatch '\[FAIL\]\s+no tests selected')) {
        throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: unmatched filter was not rejected ($runner)"
    }
    Write-Output "[PASS] unmatched CoreTests filter fails: $(Split-Path (Split-Path $runner -Parent) -Leaf)"
    Write-Output "[PASS] focused/full CoreTests: $(Split-Path (Split-Path $runner -Parent) -Leaf)"
}

if (-not $SkipExistingRegression) {
    # Prefer PowerShell 7 so UTF-8-without-BOM regression scripts are parsed
    # consistently; Windows PowerShell 5 can decode these source files using
    # the active ANSI code page when launched with -File.
    $powershell = (Get-Command pwsh.exe -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Source -First 1)
    if (-not $powershell) {
        $powershell = (Get-Command powershell.exe -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Source -First 1)
    }
    if (-not $powershell) {
        throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: child PowerShell not found'
    }
    # Audit the directory instead of maintaining a silently stale allow-list.
    # The aggregate itself cannot call itself, and the two multi-select tests
    # exercise the R-only feature explicitly outside Task 6's original Battle
    # base scope.  Every other existing test_*.ps1 script is included below.
    $excludedRegressionScripts = [ordered]@{
        'test_battle_preview_full_regression.ps1' = 'aggregate self-recursion'
        'test_scene_multiselect_ui.ps1' = 'R-only multi-select UI; outside S Battle base'
        'test_scene_multiselect_writeback.ps1' = 'R-only multi-select writeback; native write gate remains closed'
        'test_preview_internalization_baseline.ps1' = 'pre-Task6 Release/s.exe SHA baseline; Task6 UI changes intentionally alter the artifact'
        'test_targeted_review_contract.ps1' = 'external staging s.exe hash gate is outside Task6 files and remains stale'
    }
    $discovered = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'test_*.ps1' |
        Sort-Object Name)
    $existing = @(
        foreach ($script in $discovered) {
            if (-not $excludedRegressionScripts.Contains($script.Name)) {
                $script.Name
            }
        }
    )
    foreach ($script in $discovered) {
        if ($excludedRegressionScripts.Contains($script.Name)) {
            Write-Output "[INFO] aggregate exclusion: $($script.Name) ($($excludedRegressionScripts[$script.Name]))"
        }
    }
    if ($existing.Count -eq 0) {
        throw 'BATTLE_PREVIEW_FULL_REGRESSION_FAIL: regression audit discovered no included scripts'
    }
    Write-Output "[PASS] aggregate regression audit: discovered $($discovered.Count), included $($existing.Count), excluded $($excludedRegressionScripts.Count)"
    foreach ($name in $existing) {
        $path = Join-Path $PSScriptRoot $name
        if (-not (Test-Path -LiteralPath $path)) {
            throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: existing regression missing $name"
        }
        # Run each legacy script in a child host.  Several pre-existing
        # scripts launch cmd/cl fixtures and may alter host-level variables;
        # isolation keeps this aggregate gate deterministic.
        $legacyOutput = & $powershell -NoProfile -ExecutionPolicy Bypass -File $path 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "BATTLE_PREVIEW_FULL_REGRESSION_FAIL: existing regression failed $name`n$($legacyOutput -join [Environment]::NewLine)"
        }
        $legacyOutput | Write-Output
        Write-Output "[PASS] existing regression: $name"
    }
}

'BATTLE_PREVIEW_FULL_REGRESSION_PASS_WITH_NATIVE_GATES_CLOSED'
