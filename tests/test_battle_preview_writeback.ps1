[CmdletBinding()]
param(
    [string]$CoreTestsRunner
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$adapter = Join-Path $root 'cczEditor2\PreviewIntegration\BattleWritebackAdapter.cpp'
$header = Join-Path $root 'cczEditor2\PreviewIntegration\BattleWritebackAdapter.h'
$qaGate = Join-Path $root 'cczEditor2\PreviewIntegration\BattleWritebackQaGate.h'
$ops = Join-Path $root 'cczEditor2\PreviewIntegration\BattleEditorOps.cpp'
$view = Join-Path $root 'cczEditor2\cczEditor2View.cpp'
$page = Join-Path $root 'cczEditor2\BattlePreviewPage.cpp'
$pageHeader = Join-Path $root 'cczEditor2\BattlePreviewPage.h'
$test = Join-Path $root 'cczPreviewCoreTests\tests\battle_writeback_tests.cpp'
$qaBoundary = Join-Path $PSScriptRoot 'test_battle_writeback_qa_boundary.ps1'
$fixture = Join-Path $root 'tests\fixtures\battle_preview\writeback-manifest.json'

function Assert-Text($path, $pattern, $label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) { throw "BATTLE_WRITEBACK_FAIL: $label" }
    Write-Output "[PASS] $label"
}

foreach ($path in @($adapter, $header, $qaGate, $ops, $test, $qaBoundary, $fixture)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "BATTLE_WRITEBACK_FAIL: missing $path" }
}
Assert-Text $adapter 'native_reload_verified' 'native reload gate is enforced'
Assert-Text $adapter 'kind300_external_acceptance_verified' 'kind300 gate is enforced'
Assert-Text $adapter 'product_writeback_allowed = false' 'adapter never enables product writeback'
Assert-Text $adapter 'CommitToMemory' 'memory transaction has explicit commit'
Assert-Text $adapter 'Rollback' 'memory transaction has explicit rollback'
Assert-Text $adapter 'raw_fields' 'builder starts from raw fields for preservation'
Assert-Text $adapter 'editor_minion_brush_batch_id' 'planner validates independent brush batches'
Assert-Text $adapter 'BuildBattleWritebackSectionSnapshot' 'baseline and live Section captures are bridged by a value snapshot'
Assert-Text $ops 'next_minion_brush_batch_id' 'brush selections receive unique cache batches'
Assert-Text $ops 'editor_minion_brush_batch_id' 'deletion compacts only its batch'
Assert-Text $ops '战场单位已临时放置（仍在缓存，尚未写入剧本）' 'temporary placement does not claim the QA writeback gate is closed'
Assert-Text (Join-Path $root 'cczEditor2\BattlePreviewPage.cpp') 'm_writebackBaselineCapture = capture' 'S page retains the cache-load baseline capture'
Assert-Text $page 'QA 写回成功' 'successful QA writeback replaces stale placement diagnostics'
Assert-Text $view 'InsertBattleInstructionAt' 'native battle insertion API is separate from generic insertion'
Assert-Text $view 'intData.size\(\) != expectedSize' 'native battle insertion rejects truncated field vectors'
Assert-Text $view 'instructionId != 0x46' 'native battle insertion is restricted to deployment commands'
Assert-Text $view 'fileName.Left\(2\).*L"S_"' 'native battle insertion rejects non-S scripts'
Assert-Text $view 'fileName.Left\(2\).*L"S-"' 'native battle insertion accepts only explicit S script prefixes'
Assert-Text $page 'DeleteBattleNode[\s\S]{0,600}CanDeleteBattleWritebackNativeNode' 'MFC transaction bridge shares the 0x4B/0x46/0x47 delete policy used by rollback and undo'
Assert-Text $page 'RestoreBattleNodeBefore[\s\S]{0,500}CanDeleteBattleWritebackNativeNode[\s\S]{0,1000}make_unique<ItemData>[\s\S]{0,120}node\.int_data\.size\(\)' 'MFC transaction rollback restores each deployment type using its full native vector'
Assert-Text $pageHeader 'BuildPendingWritebackPlan' 'S page exposes a value-only writeback preflight'
Assert-Text $pageHeader '(?s)private:.*?bool ApplyPendingNativeWritebackForEvidence' 'native writeback evidence entry remains private'
Assert-Text $page 'BuildBattleWritebackSectionSnapshot' 'preflight revalidates baseline against a fresh capture'
Assert-Text $page 'BuildBattleWritebackPlan' 'preflight compiles the cache into a Section delta'
Assert-Text $page 'BuildBattleWritebackNativeBatch' 'preflight resolves the complete native batch before mutation'
Assert-Text $page 'ValidateBattleWritebackNextOrdinal' 'native insert counter is checked against all document instructions'
Assert-Text $adapter 'insert_before_handle' 'native batch translates the terminal anchor to the TreeView sibling contract'
Assert-Text $adapter 'original_int_data' 'native batch carries source preimages for stale-target rejection'
$nativeWritebackEntrypointReferences = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'cczEditor2') -Recurse -File |
        Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' } |
        Select-String -SimpleMatch 'ApplyPendingNativeWritebackForEvidence'
)
if ($nativeWritebackEntrypointReferences.Count -ne 2) {
    throw 'BATTLE_WRITEBACK_FAIL: native evidence-gated writeback entry must remain unconnected to product controls'
}
Write-Output '[PASS] evidence-gated native writeback has no product-control caller'
Assert-Text $test 'battle_writeback_rejects_each_unclosed_gate' 'gate rejection test exists'
Assert-Text $test 'battle_writeback_transaction_rolls_back' 'rollback snapshot test exists'
Assert-Text $test 'group_order_verified = false' 'group-order gate test exists'
Assert-Text $test 'battle_writeback_preserves_first_seen_group_order' 'first-seen output order test exists'
Assert-Text $test 'battle_writeback_rejects_multiple_4b_rows_from_one_source' '0x4B framing test exists'
Assert-Text $test 'battle_writeback_plan_rejects_insert_anchor_outside_scope' 'new nodes cannot escape their Section'
Assert-Text $test 'battle_writeback_plan_rejects_duplicate_source_ordinals' 'source-node order must be unambiguous'
Assert-Text $test 'battle_writeback_native_batch_resolves_all_handles_before_mutation' 'native source and insertion handles resolve before mutation'
Assert-Text $test 'battle_writeback_native_batch_rejects_stale_or_duplicate_nodes_atomically' 'stale or duplicate source nodes reject the complete batch'
Assert-Text $test 'battle_writeback_native_batch_rejects_nonterminal_insert_anchor' 'only the terminal event anchor may receive insertions'
Assert-Text $test 'battle_editor_brush_delete_compacts_only_its_selection_batch' 'brush deletion isolates placement sessions'

# The native MFC route is enabled only for a marked S QA copy in an explicit
# Debug|Win32 build. Keep its UI, copy boundary, memory commit, and normal
# save/reopen instructions in the same focused writeback gate.
& $qaBoundary
Write-Output '[PASS] Debug-only S QA-copy writeback boundary'

# Source checks are only the boundary contract.  If a built focused runner is
# available, execute the behavioral tests as well; a missing runner is a hard
# failure rather than a false green.  Both Debug and Release are accepted so
# this remains usable on either local build configuration. An explicit path
# wins; otherwise prefer the newest local runner, including the isolated
# workspace QA-root output used by the current native-writeback plan.
$runnerOverride = $CoreTestsRunner
if ([string]::IsNullOrWhiteSpace($runnerOverride)) {
    $runnerOverride = $env:CCZ_TEST_RUNNER
}
if (-not [string]::IsNullOrWhiteSpace($runnerOverride)) {
    if (-not (Test-Path -LiteralPath $runnerOverride -PathType Leaf)) {
        throw "BATTLE_WRITEBACK_FAIL: explicit CoreTests runner missing ($runnerOverride)"
    }
    $runner = (Resolve-Path -LiteralPath $runnerOverride).Path
} else {
    $runnerCandidates = @(
        (Join-Path $root 'cczPreviewCoreTests\Debug\cczPreviewCoreTests.exe'),
        (Join-Path $root 'cczPreviewCoreTests\Release\cczPreviewCoreTests.exe')
    )
    $ancestor = Get-Item -LiteralPath $root
    while ($null -ne $ancestor) {
        $runnerCandidates += Join-Path $ancestor.FullName `
            '.tmp-s-battle\qa-root-green\bin\cczPreviewCoreTests.exe'
        $ancestor = $ancestor.Parent
    }
    $runner = $runnerCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } -Descending |
        Select-Object -First 1
}
# Only files compiled into CoreTests participate in the runner freshness check.
# MFC boundary sources are checked above and by the independent Win32 app build.
$sourcePaths = @(
    $adapter,
    $header,
    $qaGate,
    $ops,
    $test,
    $fixture,
    (Join-Path $root 'cczPreviewCore\src\battle_instruction_parser.cpp'),
    (Join-Path $root 'cczPreviewCore\include\ccz\preview\battle_instruction_parser.h'),
    (Join-Path $root 'cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'),
    (Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj')
)
$sourceStamp = ($sourcePaths |
    ForEach-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } |
    Measure-Object -Maximum).Maximum
if (-not $runner) {
    throw 'BATTLE_WRITEBACK_FAIL: focused CoreTests runner missing; build it or pass -CoreTestsRunner before claiming behavioral coverage'
}
$runner = (Resolve-Path -LiteralPath $runner).Path
Write-Output "[INFO] focused CoreTests runner: $runner"
if ((Get-Item -LiteralPath $runner).LastWriteTimeUtc -lt $sourceStamp) {
    throw "BATTLE_WRITEBACK_FAIL: focused CoreTests runner is older than writeback sources ($runner)"
}
$output = & $runner 'battle_writeback' 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "BATTLE_WRITEBACK_FAIL: focused CoreTests failed (exit $LASTEXITCODE)`n$($output -join [Environment]::NewLine)"
}
$outputText = ($output -join [Environment]::NewLine)
$expectedTests = @(
    'battle_writeback_capture_builds_scoped_snapshot_and_insert_anchors',
    'battle_writeback_capture_collects_each_deployment_kind_in_scope',
    'battle_writeback_capture_leaves_unknown_insert_layout_unanchored',
    'battle_writeback_capture_rejects_stale_or_cross_scope_snapshot',
    'battle_writeback_native_batch_resolves_all_handles_before_mutation',
    'battle_writeback_native_batch_rejects_stale_or_duplicate_nodes_atomically',
    'battle_writeback_native_batch_rejects_nonterminal_insert_anchor',
    'battle_writeback_qa_gate_only_allows_marked_s_script_copies',
    'battle_writeback_qa_copy_root_is_fixed_next_to_executable',
    'battle_writeback_native_transaction_applies_and_undoes_one_atomic_batch',
    'battle_writeback_native_delete_policy_supports_every_deployment_type',
    'battle_writeback_next_ordinal_allows_gaps_beyond_document_maximum',
    'battle_writeback_next_ordinal_rejects_stale_or_colliding_counter',
    'battle_writeback_next_ordinal_rejects_missing_existing_ordinal',
    'battle_writeback_native_transaction_rolls_back_when_a_late_insert_fails',
    'battle_writeback_native_transaction_rolls_back_after_first_insert_when_second_fails',
    'battle_writeback_native_transaction_rejects_stale_undo_without_mutating',
    'battle_writeback_native_transaction_rolls_back_a_partial_undo',
    'battle_writeback_rejects_each_unclosed_gate',
    'battle_writeback_builds_fixed_rows_and_preserves_unedited_values',
    'battle_writeback_rejects_incomplete_fixed_group_without_mutating_source',
    'battle_writeback_preserves_first_seen_group_order_and_4b_sources',
    'battle_writeback_rejects_multiple_4b_rows_from_one_source',
    'battle_writeback_preserves_raw_values_until_explicit_dirty_mark',
    'battle_writeback_transaction_rolls_back_battle_modified_and_selection',
    'battle_writeback_transaction_rejects_invalid_states_and_undoes_commit',
    'battle_writeback_plan_updates_existing_position_without_losing_other_fields',
    'battle_writeback_plan_creates_missing_categories_from_source_less_cache',
    'battle_writeback_plan_compacts_4b_slots_after_delete_and_appends_new_slots',
    'battle_writeback_plan_fills_first_fixed_empty_row_before_adding_page',
    'battle_writeback_plan_reuses_empty_data_id_slot_with_stale_payload',
    'battle_writeback_plan_rejects_stale_source_snapshot_atomically',
    'battle_writeback_plan_deletes_single_record_and_clears_fixed_row',
    'battle_writeback_plan_rejects_category_migration_without_lossless_mapping',
    'battle_writeback_plan_adds_page_when_fixed_source_is_full',
    'battle_writeback_plan_rejects_missing_insert_anchor_without_partial_changes',
    'battle_writeback_plan_rejects_insert_anchor_outside_scope',
    'battle_writeback_plan_rejects_duplicate_source_ordinals',
    'battle_writeback_plan_rejects_undecoded_staged_unit',
    'battle_writeback_plan_rejects_duplicate_cache_id',
    'battle_writeback_plan_rejects_noncontiguous_minion_brush_batch',
    'battle_writeback_plan_accepts_distinct_batches_starting_after_occupied_ids'
)
$actualTests = [regex]::Matches($outputText, '(?m)^\[PASS\] (battle_writeback_[a-z0-9_]+)\r?$') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
$expectedTestNames = @($expectedTests | Sort-Object -Unique)
$unexpectedTests = @($actualTests | Where-Object { $_ -notin $expectedTestNames })
$missingTests = @($expectedTestNames | Where-Object { $_ -notin $actualTests })
if ($unexpectedTests.Count -gt 0 -or $missingTests.Count -gt 0) {
    throw "BATTLE_WRITEBACK_FAIL: focused CoreTests inventory differs from the approved design. Missing: $($missingTests -join ', '); unexpected: $($unexpectedTests -join ', ')"
}
foreach ($name in $expectedTests) {
    if ($outputText -notmatch ("\[PASS\] " + [regex]::Escape($name))) {
        throw "BATTLE_WRITEBACK_FAIL: focused CoreTests did not pass $name"
    }
}
Write-Output '[PASS] focused CoreTests behavioral writeback gate'
$brushOutput = & $runner 'battle_editor_brush' 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "BATTLE_WRITEBACK_FAIL: focused brush tests failed (exit $LASTEXITCODE)`n$($brushOutput -join [Environment]::NewLine)"
}
$brushText = ($brushOutput -join [Environment]::NewLine)
foreach ($name in @(
    'battle_editor_brush_batch_start_uses_first_available_id',
    'battle_editor_brush_delete_compacts_only_its_selection_batch',
    'battle_editor_brush_uses_smallest_free_id_and_advances_only_after_place'
)) {
    if ($brushText -notmatch ("\[PASS\] " + [regex]::Escape($name))) {
        throw "BATTLE_WRITEBACK_FAIL: focused brush tests did not pass $name"
    }
}
$actualBrushTests = [regex]::Matches($brushText, '(?m)^\[PASS\] (battle_editor_brush_[a-z0-9_]+)\r?$') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
$expectedBrushTests = @(
    'battle_editor_brush_batch_start_uses_first_available_id',
    'battle_editor_brush_delete_compacts_only_its_selection_batch',
    'battle_editor_brush_uses_smallest_free_id_and_advances_only_after_place'
) | Sort-Object -Unique
$unexpectedBrushTests = @($actualBrushTests | Where-Object { $_ -notin $expectedBrushTests })
$missingBrushTests = @($expectedBrushTests | Where-Object { $_ -notin $actualBrushTests })
if ($unexpectedBrushTests.Count -gt 0 -or $missingBrushTests.Count -gt 0) {
    throw "BATTLE_WRITEBACK_FAIL: focused brush test inventory differs from the approved design. Missing: $($missingBrushTests -join ', '); unexpected: $($unexpectedBrushTests -join ', ')"
}
Write-Output '[PASS] focused CoreTests brush batch identity gate'
$manifest = Get-Content -LiteralPath $fixture -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.native_reload_verified -or $manifest.kind300_external_acceptance_verified -or $manifest.product_writeback_enabled) {
    throw 'BATTLE_WRITEBACK_FAIL: real runtime fixture must keep product writeback disabled'
}
Write-Output 'BATTLE_WRITEBACK_PASS'
