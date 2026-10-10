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
$rPage = Join-Path $root 'cczEditor2\ScenePreviewWnd.cpp'
$page = Join-Path $root 'cczEditor2\BattlePreviewPage.cpp'
$pageHeader = Join-Path $root 'cczEditor2\BattlePreviewPage.h'
$document = Join-Path $root 'cczEditor2\cczEditor2Doc.cpp'
$test = Join-Path $root 'cczPreviewCoreTests\tests\battle_writeback_tests.cpp'
$qaBoundary = Join-Path $PSScriptRoot 'test_battle_writeback_qa_boundary.ps1'
$conditionTreeIntegration = Join-Path $PSScriptRoot 'test_battle_condition_tree.ps1'
$fixture = Join-Path $root 'tests\fixtures\battle_preview\writeback-manifest.json'

function Assert-Text($path, $pattern, $label) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) { throw "BATTLE_WRITEBACK_FAIL: $label" }
    Write-Output "[PASS] $label"
}

foreach ($path in @($adapter, $header, $qaGate, $ops, $test, $qaBoundary, $document, $rPage,
        $conditionTreeIntegration, $fixture)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "BATTLE_WRITEBACK_FAIL: missing $path" }
}
Assert-Text $adapter 'native_reload_verified' 'native reload gate is enforced'
Assert-Text $adapter 'kind300_external_acceptance_verified' 'kind300 gate is enforced'
Assert-Text $adapter 'product_writeback_allowed = false' 'pure-value serialization remains inert until the native MFC transaction commits it'
Assert-Text $adapter 'CommitToMemory' 'memory transaction has explicit commit'
Assert-Text $adapter 'Rollback' 'memory transaction has explicit rollback'
Assert-Text $adapter 'raw_fields' 'builder starts from raw fields for preservation'
Assert-Text $adapter 'editor_minion_brush_batch_id' 'planner validates independent brush batches'
Assert-Text $adapter 'BuildBattleWritebackSectionSnapshot' 'baseline and live Section captures are bridged by a value snapshot'
Assert-Text $ops 'next_minion_brush_batch_id' 'brush selections receive unique cache batches'
Assert-Text $ops 'editor_minion_brush_batch_id' 'deletion compacts only its batch'
$stagedPlacementStatus = -join @(
    [char]0x6218, [char]0x573A, [char]0x5355, [char]0x4F4D,
    [char]0x5DF2, [char]0x4E34, [char]0x65F6, [char]0x653E,
    [char]0x7F6E, [char]0xFF08, [char]0x4ECD, [char]0x5728,
    [char]0x7F13, [char]0x5B58, [char]0xFF0C, [char]0x5C1A,
    [char]0x672A, [char]0x5199, [char]0x5165, [char]0x5267,
    [char]0x672C, [char]0xFF09
)
Assert-Text $ops ([regex]::Escape($stagedPlacementStatus)) 'temporary placement reports that its changes remain staged'
Assert-Text (Join-Path $root 'cczEditor2\BattlePreviewPage.cpp') 'ActiveSession\(\)\.baseline_capture = std::move\(capture\)' 'S document session retains the cache-load baseline capture without copying it'
Assert-Text $page '\u0053\u0020\u6218\u573A\u5B57\u6BB5\u5DF2\u5199\u5165\u539F\u751F\u5267\u672C\u6811\u5E76\u9010\u6761\u8BFB\u56DE\u786E\u8BA4' 'successful native writeback reports verified instruction fields'
Assert-Text $page 'describe_native_writeback\(batch\)' 'writeback status shows instruction-level field changes'
Assert-Text $page 'RevealNativeWritebackInstruction' 'successful native writeback reveals a changed instruction in the editor tree'
Assert-Text $page 'RevealNativeWritebackInstruction\(\s*view,\s*batch,\s*mapOperations != nullptr\s*\? host\.last_inserted_handle\(\) : host\.first_inserted_handle\(\)' 'map operations reveal the final committed insertion; ordinary writeback keeps its existing target'
Assert-Text $page 'tree\.EnsureVisible\(item\)' 'revealed instruction expands its collapsed parent events'
Assert-Text $page 'tree\.SelectItem\(item\)' 'revealed instruction remains selected for visual inspection'
Assert-Text $page 'advance_document_ordinal\)[\s\S]{0,500}IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting' 'new 0x4B groups validate the actual TreeView predecessor; undo restoration can restore the original position'
Assert-Text $page 'data->int_data\.swap\(int_data\)' 'writeback mutates the native ItemData vector used by the editor tree'
Assert-Text $page 'bool TreeItemExists\(std::uintptr_t handle\) const' 'document root selection is validated as a TreeView item, not as a script node'
Assert-Text $page 'if \(selected != 0 && !TreeItemExists\(selected\)\)' 'selection restore accepts valid tree items whose lParam is zero'
Assert-Text $document 'data->int_data\[k - long_char_sum\]' 'EEX save serializes native fields from the same TreeView ItemData vector'
Assert-Text $document 'f\.Write\(int2ch\(data->int_data\[k - long_char_sum\], 4\), 4\)' 'EEX save emits the 0x4B coordinate fields as native four-byte values'
Assert-Text $view 'InsertBattleInstructionAt' 'native battle insertion API is separate from generic insertion'
Assert-Text $view 'IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting' 'R and S native preview insertion block gaps after event-end and subevent markers'
Assert-Text $view 'IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting\(\s*afterData->id\s*\)' 'R preview checks the actual instruction immediately before its insertion point'
Assert-Text $view 'IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting\(\s*predecessorData->id\s*\)' 'S native tree insertion repeats the structural-marker guard'
$rPageText = Get-Content -LiteralPath $rPage -Raw -Encoding UTF8
Assert-Text $rPage 'InsertInstructionAt\(' 'R preview routes instruction additions through the guarded shared insertion API'
if ($rPageText -match 'CreateItem\(|CreateZsj\(|InsertBattleInstructionAt\(') {
    throw 'BATTLE_WRITEBACK_FAIL: R preview contains an insertion route that bypasses InsertInstructionAt'
}
Write-Output '[PASS] R preview has no direct instruction insertion bypass'
Assert-Text $view 'intData.size\(\) != expectedSize' 'native battle insertion rejects truncated field vectors'
Assert-Text $view 'case 0x32:\s*case 0x55:\s*expectedSize = 6U; break;' 'native battle insertion accepts the six-field move and revive commands'
Assert-Text $view 'case 0x58:\s*expectedSize = 7U; break;' 'native battle insertion accepts the seven-field obstacle command'
Assert-Text $view 'sectionEntryEventBoundary = instructionId == 0x32[\s\S]*isSection' 'map operations can be anchored directly before a Section event container'
Assert-Text $view 'fileName.Left\(2\).*L"S_"' 'native battle insertion rejects non-S scripts'
Assert-Text $view 'fileName.Left\(2\).*L"S-"' 'native battle insertion accepts only explicit S script prefixes'
Assert-Text $view 'wcscpy_s\(show, code\[id\]\)' 'tree items retain their native instruction label'
Assert-Text $view 'data->int_data = std::move\(intData\)' 'new deployment fields move into native tree data without a second full-vector copy'
if ($view -match 'wcscat_s\(show, L"槽位:"\)') {
    throw 'BATTLE_WRITEBACK_FAIL: 0x4B display label must not be rewritten with placement fields'
}
Assert-Text $page 'DeleteBattleNode[\s\S]{0,600}CanDeleteBattleWritebackNativeNode' 'MFC transaction bridge shares the exact-size deployment and map-operation delete policy used by rollback and undo'
Assert-Text $page 'RestoreBattleNodeBefore[\s\S]{0,500}CanDeleteBattleWritebackNativeNode[\s\S]{0,1000}make_unique<ItemData>[\s\S]{0,120}node\.int_data\.size\(\)' 'MFC transaction rollback restores supported instructions using their full native vectors'
Assert-Text $pageHeader 'BuildPendingWritebackPlan' 'S page exposes a value-only writeback preflight'
Assert-Text $pageHeader '(?s)private:.*?bool ApplyPendingNativeWritebackForCurrentSection' 'the S page exposes its current Section through a private native writeback entry'
Assert-Text $page 'BuildBattleWritebackSectionSnapshot' 'preflight revalidates baseline against a fresh capture'
Assert-Text $page 'BuildBattleWritebackPlan' 'preflight compiles the cache into a Section delta'
Assert-Text $page 'BuildBattleWritebackNativeBatch' 'preflight resolves the complete native batch before mutation'
Assert-Text $page 'ValidateBattleWritebackNextOrdinal' 'native insert counter is checked against all document instructions'
Assert-Text $page 'ValidateBattleWritebackNextOrdinal\(\s*capture\.snapshot\s*,' 'native insert ordinal validation reads the retained preflight snapshot'
Assert-Text $adapter 'last_inserted_by_anchor' 'native insert predecessor chains are indexed instead of rescanned for every unit'
$adapterText = Get-Content -LiteralPath $adapter -Raw -Encoding UTF8
Assert-Text $adapter 'IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting\(\s*previous\.instruction_id\s*\)' 'S batch preflight rejects insertion after event-end and subevent markers'
Assert-Text $adapter 'IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting\(\s*after\.instruction_id\s*\)' 'S native transaction rechecks both structural-marker boundaries'
Assert-Text $adapter 'surviving_after != 0\)[\s\S]{0,550}IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting\(\s*surviving_predecessor\.instruction_id\s*\)' 'S native transaction also checks the normalized insertion gap after scheduled deletions'
Assert-Text (Join-Path $root 'cczEditor2\PreviewIntegration\PreviewWritebackGate.h') 'previous_instruction_id == 0 \|\| previous_instruction_id == 1' 'R and S preview writeback share the ID 0 and ID 1 insertion predicate'
Assert-Text (Join-Path $root 'cczEditor2\Instructions.json') '"id": 0, "name": "事件结束"[\s\S]{0,140}"can_nest": false, "top_level": true' 'instruction metadata classifies event end as a top-level structural marker'
Assert-Text (Join-Path $root 'cczEditor2\Instructions.json') '"id": 1, "name": "子事件设定"[\s\S]{0,140}"can_nest": true, "top_level": false' 'instruction metadata classifies subevent setting as a nesting marker'
Assert-Text $view 'combo1\.GetCurSel\(\) < 2' 'the editor dialog prevents manually creating structural instruction IDs 0 and 1'
Assert-Text $view 'GetItemData\(last\)\)->id == 1' 'the original manual insertion gap guard explicitly covers ID 1'
if ($adapterText -match 'count_if\(\s*prepared\.begin\(') {
    throw 'BATTLE_WRITEBACK_FAIL: native insert ordinals must be assigned in one linear pass'
}
Write-Output '[PASS] native insert ordinal assignment does not rescan the prepared batch'
Assert-Text $page 'preflight_state\.document_revision\s*=\s*capture\.input\.document_revision' 'transaction baseline reuses the already validated preflight document revision'
Assert-Text $page 'SWritebackPerf;capture_us=' 'opt-in Debug diagnostics record capture, plan, transaction, and rebase timings'
Assert-Text $page 'TakeCommittedBattleCapture' 'successful native transactions expose the committed preview capture for reuse'
Assert-Text $page 'committed_capture_pointer' 'writeback passes its validated committed capture into the rebase step'
Assert-Text $page 'last_captured_battle_ = std::move\(capture\)' 'native host retains the exact preview capture used for the committed revision'
Assert-Text $page 'GetPreviewContentEpoch\(\) != last_capture_content_epoch_' 'rebase only reuses the captured preview while its document content version is current'
Assert-Text $page 'last_capture_content_epoch_ = document_->GetPreviewContentEpoch\(\)' 'dirty-state restoration accounts for its known content-epoch bump without recapturing nodes'
if ((Get-Content -LiteralPath $page -Raw -Encoding UTF8) -match 'UpdateQuickModeObservedSectionRevision') {
    throw 'BATTLE_WRITEBACK_FAIL: quick-mode revision must reuse the rebased capture, not recapture its pinned section'
}
if ((Get-Content -LiteralPath $pageHeader -Raw -Encoding UTF8) -match 'UpdateQuickModeObservedSectionRevision') {
    throw 'BATTLE_WRITEBACK_FAIL: obsolete quick-mode section recapture declaration remains'
}
Write-Output '[PASS] quick-mode rebase reuses its captured section revision'
Assert-Text $adapter 'insert_before_handle' 'native batch translates the terminal anchor to the TreeView sibling contract'
Assert-Text $adapter 'original_int_data' 'native batch carries source preimages for stale-target rejection'
$nativeWritebackEntrypointReferences = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'cczEditor2') -Recurse -File |
        Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' } |
        Select-String -SimpleMatch 'ApplyPendingNativeWritebackForCurrentSection'
)
if ($nativeWritebackEntrypointReferences.Count -ne 3) {
    throw 'BATTLE_WRITEBACK_FAIL: the standard writeback entry must have one declaration, definition, and product-control caller'
}
Write-Output '[PASS] product control reaches the validated native Section transaction'
Assert-Text $test 'battle_writeback_rejects_each_unclosed_gate' 'gate rejection test exists'
Assert-Text $test 'battle_writeback_transaction_rolls_back' 'rollback snapshot test exists'
Assert-Text $test 'battle_writeback_native_transaction_rejects_miswritten_update' 'native updates must be read back before success'
Assert-Text $test 'battle_writeback_native_transaction_preserves_non_node_document_root_selection' 'writeback preserves the valid document-root selection across commit and undo'
Assert-Text $test 'capture_document_state_calls, 2' 'successful writeback uses only the baseline and committed document captures'
Assert-Text $test 'battle_writeback_native_transaction_preserves_large_insert_batch_order' 'large insertion batches preserve instruction order'
Assert-Text $test 'battle_writeback_position_adjustment_reaches_native_instruction_plan' 'position adjustment reaches instruction fields through the real editor operation'
Assert-Text $test 'group_order_verified = false' 'group-order gate test exists'
Assert-Text $test 'battle_writeback_preserves_first_seen_group_order' 'first-seen output order test exists'
Assert-Text $test 'battle_writeback_rejects_multiple_4b_rows_from_one_source' '0x4B framing test exists'
Assert-Text $test 'battle_writeback_plan_rejects_insert_anchor_outside_scope' 'new nodes cannot escape their Section'
Assert-Text $test 'battle_writeback_plan_rejects_duplicate_source_ordinals' 'source-node order must be unambiguous'
Assert-Text $test 'battle_writeback_native_batch_resolves_all_handles_before_mutation' 'native source and insertion handles resolve before mutation'
Assert-Text $test 'battle_writeback_native_batch_rejects_stale_or_duplicate_nodes_atomically' 'stale or duplicate source nodes reject the complete batch'
Assert-Text $test 'battle_writeback_native_batch_accepts_selected_sibling_insert_anchor' 'a user-selected same-Section sibling may anchor a new instruction'
Assert-Text $test 'battle_writeback_native_transaction_inserts_under_nested_condition_event' 'native transaction inserts and undoes a deployment in a nested condition event'
Assert-Text $test 'battle_editor_brush_delete_compacts_only_its_selection_batch' 'brush deletion isolates placement sessions'

# The native MFC route is available in ordinary Debug and Release builds.
# Keep the UI, active S document boundary, in-memory commit, and normal editor
# save contract in the same focused writeback gate.
& $qaBoundary
Write-Output '[PASS] unrestricted S battle writeback boundary'
& $conditionTreeIntegration
Write-Output '[PASS] S condition-tree preview and child-event writeback integration'

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
        (Join-Path $root 'cczEditor2\.tmp-s-battle\multi-document-sessions\debug\bin\cczPreviewCoreTests.exe'),
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
    'battle_writeback_capture_limits_nodes_and_anchor_to_active_child_event',
    'battle_writeback_capture_leaves_unknown_insert_layout_unanchored',
    'battle_writeback_capture_rejects_stale_or_cross_scope_snapshot',
    'battle_writeback_capture_allows_changes_outside_pinned_section',
    'battle_writeback_native_batch_resolves_all_handles_before_mutation',
    'battle_writeback_native_batch_rejects_stale_or_duplicate_nodes_atomically',
    'battle_writeback_native_batch_accepts_selected_sibling_insert_anchor',
    'battle_writeback_capture_uses_selected_anchor_without_condition_scope',
    'battle_writeback_capture_uses_selected_instruction_when_no_deployments_exist',
    'battle_writeback_native_transaction_inserts_and_undoes_atomic_4b_group',
    'battle_writeback_native_transaction_rolls_back_new_4b_group_after_late_failure',
    'battle_writeback_plan_merges_disjoint_live_and_preview_fields',
    'battle_writeback_plan_rejects_true_same_field_conflict',
    'battle_writeback_qa_gate_only_allows_marked_s_script_copies',
    'battle_writeback_qa_copy_root_is_fixed_next_to_executable',
    'battle_writeback_native_transaction_applies_and_undoes_one_atomic_batch',
    'battle_writeback_native_transaction_preserves_large_insert_batch_order',
    'battle_writeback_native_transaction_allows_sequential_commits_and_lifo_undo',
    'battle_writeback_native_transaction_preserves_non_node_document_root_selection',
    'battle_writeback_native_transaction_rejects_miswritten_update',
    'battle_writeback_native_delete_policy_supports_every_deployment_type',
    'battle_writeback_next_ordinal_allows_gaps_beyond_document_maximum',
    'battle_writeback_next_ordinal_rejects_stale_or_colliding_counter',
    'battle_writeback_next_ordinal_rejects_missing_existing_ordinal',
    'battle_writeback_native_transaction_rolls_back_when_a_late_insert_fails',
    'battle_writeback_native_transaction_inserts_under_nested_condition_event',
    'battle_writeback_native_batch_rejects_insert_after_subevent_marker',
    'battle_writeback_native_batch_rejects_insert_after_event_end_marker',
    'battle_writeback_native_batch_allows_insert_before_subevent_marker',
    'battle_writeback_native_batch_allows_insert_before_event_end_marker',
    'battle_writeback_native_batch_rejects_four_b_group_after_subevent_marker',
    'battle_writeback_native_transaction_rejects_insert_after_subevent_marker',
    'battle_writeback_native_transaction_rejects_insert_after_event_end_marker',
    'battle_writeback_native_transaction_rejects_deleted_anchor_that_moves_insert_after_event_end',
    'battle_writeback_native_transaction_allows_insert_before_subevent_marker',
    'battle_writeback_native_transaction_allows_insert_before_event_end_marker',
    'battle_writeback_native_transaction_rejects_four_b_group_after_subevent_marker',
    'battle_writeback_shared_gate_rejects_insertion_after_event_end_or_subevent_setting',
    'battle_writeback_native_transaction_inserts_all_three_new_unit_types',
    'battle_writeback_native_transaction_inserts_and_undoes_map_operations_in_order',
    'battle_writeback_native_transaction_rejects_map_operation_in_own_deployment_group',
    'battle_writeback_native_transaction_allows_map_operation_in_other_subevent',
    'battle_writeback_native_transaction_allows_map_operation_in_non_deployment_info_group',
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
    'battle_writeback_position_adjustment_reaches_native_instruction_plan',
    'battle_writeback_plan_creates_missing_categories_from_source_less_cache',
    'battle_writeback_plan_routes_new_instruction_types_to_distinct_child_events',
    'battle_writeback_plan_rejects_only_matching_unresolved_type_anchor',
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
    'battle_editor_brush_stops_at_occupied_batch_boundary',
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
    'battle_editor_brush_stops_at_occupied_batch_boundary',
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
