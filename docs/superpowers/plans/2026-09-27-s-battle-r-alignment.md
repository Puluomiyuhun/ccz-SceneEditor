# S Battle Writeback and R Alignment Implementation Plan

> **For agentic workers:** Use the `executing-plans` workflow to implement this plan task by task. Preserve the shared worktree and all existing changes; do not stage or commit during implementation unless the user asks.

**Goal:** Make S battle QA writeback produce visible, verifiable changes in the editor's real TreeView while aligning its commit and refresh semantics with R preview and removing avoidable copies and transaction layers.

**Architecture:** Keep S's unified battle cache, explicit Debug QA writeback entry, independent battle records, and independent condition workspaces. On QA writeback, preflight the current reachable S branch, mutate the corresponding native `ItemData` records through one S-only transaction, refresh and select the changed instructions, mark the document dirty, and keep the existing QA-copy gate. Reuse R's semantic pattern of provenance, whole-operation validation, native mutation, dirty state, undo, and scheduled refresh; do not reuse R's scene state or coordinate types.

**Tech Stack:** C++17, MSVC Win32/MFC, `cczPreviewCore`, `cczPreviewCoreTests`, PowerShell source-contract gates.

**Spec:** `docs/superpowers/specs/2026-09-24-s-battle-unified-placement-writeback-design.md`; also preserve the Scene1 and condition-tree contracts in `docs/2026-09-26-S战场布置器中期报告.md`.

## Global Constraints

- Scope is S battlefield preview and writeback. Do not change R preview behavior or share R's scene state, coordinate state, or undo records.
- Keep the S QA writeback button available in Debug QA builds and restricted to explicitly marked QA copies. Product writeback stays disabled until its evidence gates close.
- A successful QA writeback must change native TreeView instruction data in memory. The editor's normal Save command persists it to disk; the feature must not write EEX files directly.
- Build and validate the complete change set before the first native mutation. Any failed source check or native operation must restore fields, inserted/deleted nodes, dirty state, selection, and the staged S cache.
- Use the same logical deployment keys and condition reachability for preview filtering and writeback routing. Reject ambiguous sources or insertion anchors without discarding staged work.
- The QA writeback button submits the active condition workspace. Other workspace drafts remain pending and must not be silently committed or discarded.
- Preserve the verified `0x4B`, `0x46`, and `0x47` record sizes and unknown fields. Never route S deployment insertion through the R generic insertion helper.
- Compare executable size only across matching Release x86 configurations. A Debug QA executable is not a valid size comparator for the F-drive Release build.

## Design Choice

| Approach | Result | Trade-off |
|---|---|---|
| A. Patch only the existing QA result/reveal path | Smallest source change | Does not address redundant planning, snapshot copies, or the R/S commit mismatch unless the native tree was already changed correctly. |
| B. Keep the staged S cache and QA button, but align the native commit contract with R and slim duplicated machinery | Recommended | Preserves the established S workflow and test entry while making the actual tree mutation observable and keeping S record rules isolated. |
| C. Write every mouse action directly to the native tree | Closest interaction timing to R | Removes the staged-writeback boundary the S workflow and QA entry currently depend on; raises the risk of partial multi-record operations and branch-cache conflicts. |

This plan follows **B**. “Align with R” means the same source-check → mutate native fields → update display/dirty state → undo → refresh contract. It does not mean reusing R's model or replacing the S batch planner with R position code.

## Implementation Tasks

### Task 1: Reproduce the missing visible instruction change on the exact QA build

**Files:**
- Read: `cczEditor2/BattlePreviewPage.cpp`, `cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp`, `cczEditor2/PreviewIntegration/BattleWritebackAdapter.h`
- Modify: `cczPreviewCoreTests/tests/battle_writeback_tests.cpp`
- Modify: `tests/test_battle_preview_writeback.ps1`
- Record results in: `docs/2026-09-26-S战场布置器中期报告.md`

**Interfaces:**
- Trace the live chain from `CSBattlePreviewPage::OnQaWriteback` through `BuildPendingWritebackPlan`, `BattleWritebackNativeTransaction::Apply`, and `MfcBattleWritebackNativeHost`.
- Record the QA executable path/hash, QA-copy path/hash, selected branch, changed cache IDs, source instruction keys, original row values, post-apply `ItemData::int_data`, rendered tree label, and document dirty flag.
- Treat the current failure as unresolved until the actual MFC tree reflects the changed/new instruction; a value-only planner PASS is insufficient.

- [ ] **Step 1: Add a native-host transaction test** that starts with an existing S deployment node, applies one coordinate-field change, and asserts that the host now contains the new field values while neighboring rows and unknown fields remain unchanged.
- [ ] **Step 2: Add insertion and failure tests** for one new `0x46` node under a child event and an injected failure after the first mutation; require exact rollback of node order, fields, selection, and dirty flags.
- [ ] **Step 3: Run the focused CoreTests runner** and confirm the tests exercise `Apply` rather than only `BuildBattleWritebackPlan`.
- [ ] **Step 4: Build a fresh isolated Debug x86 QA executable and paired test copy**, launch that exact path, then reproduce update and insertion in the MFC editor. Capture the visible TreeView before and after QA writeback.
- [ ] **Step 5: Save, close, reopen, and reparse the QA copy**. Record whether the exact changed instruction and fields survived. Do not infer success from the status message or preview canvas alone.

### Task 2: Align S native commit behavior with the R commit contract

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp/.h`
- Modify: `cczEditor2/cczEditor2View.cpp/.h` only for the S-native TreeView bridge
- Modify: `cczPreviewCoreTests/tests/battle_writeback_tests.cpp`
- Modify: `tests/test_battle_preview_writeback.ps1`

**Interfaces:**
- Keep the existing value planner responsible for S-specific row packing and change descriptions. Keep native handle resolution and rollback in the S adapter.
- The MFC host must update the actual `ItemData::int_data` or insert/delete the exact S instruction node; after successful apply it must call `UpdateShow`, mark the document modified, and expose the changed instruction to the user.
- A writeback summary must identify instruction ID, source/insert location, and changed fields. After successful QA writeback, select and expand a changed node so the user can inspect the command in the editor tree.

- [ ] **Step 1: Add failing host-contract assertions** for update, insert, delete, rendered-label refresh, document dirty state, and selecting the resulting instruction.
- [ ] **Step 2: Fix the first failing boundary in the traced MFC call chain**; do not add another serialization or snapshot layer until the test proves the current layer cannot mutate the required node.
- [ ] **Step 3: Keep the transaction all-or-nothing** by validating every source node, record stride, parent, terminal anchor, and preimage before mutation; preallocate rollback state before the first native change.
- [ ] **Step 4: Make successful apply rebase the committed workspace from the newly captured native tree** so another QA writeback does not reuse stale source baselines or report a false no-op.
- [ ] **Step 5: Run focused writeback tests and the QA-boundary PowerShell gate**; confirm ordinary S scripts and Release builds still expose no product writeback route.

### Task 3: Keep condition-tree routing precise and safe during native writes

**Files:**
- Modify only if tests expose a gap: `cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.cpp/.h`
- Modify only if tests expose a gap: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczPreviewCoreTests/tests/battle_condition_tree_tests.cpp`
- Modify: `cczPreviewCoreTests/tests/battle_writeback_tests.cpp`
- Modify: `tests/test_battle_condition_tree.ps1`

**Interfaces:**
- Reuse `SectionExecutionModel`, `ReplayOptions`, `CConditionTreePane`, and `ApplyConditionTreeActionToOptions` for shared condition semantics.
- Keep the S adapter responsible for mapping reachable logical nodes to `0x4B/0x46/0x47` rows and for resolving the selected child-event insertion anchor.
- One QA writeback commits only the active workspace. Inactive branch drafts remain pending; if their source keys overlap nodes just written, the next submission must detect the changed preimage and fail closed while retaining that draft.

- [ ] **Step 1: Add a true/else branch writeback test** proving an active branch update changes only reachable source keys and cannot mutate the unreachable branch's command.
- [ ] **Step 2: Add a nested insertion test based on the S_12 shape** proving a new command is inserted before the unique child-event terminator, not at a guessed top-level event.
- [ ] **Step 3: Add ambiguity and stale-source tests**; require no native mutation and preserved active/inactive workspace caches when reachability, source identity, or insertion parent is unresolved.
- [ ] **Step 4: Run condition-tree, quick-mode, and session-preservation gates** and compare tree output for the active branch with the S preview state.

### Task 4: Remove avoidable refresh, conversion, and undo duplication

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattlePreviewAdapter.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattleEditorOps.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp/.h` only after a usage audit
- Modify: `tests/test_preview_refresh_routing.ps1`
- Modify: `tests/test_battle_preview_session_preservation.ps1`
- Modify: `tests/preview_integration/battle_editor_ops_tests.cpp`

**Interfaces:**
- Pass a built `BattlePreviewCapture` into the load path instead of building it, discarding it, and converting the same full tree again.
- Where ownership permits, move a captured snapshot into the S capture rather than deep-copying the complete `document_roots` tree. Keep one immutable source baseline and one parser representation only where each has a demonstrated consumer.
- Give S refresh its own instance of the existing coalescing refresh-state pattern used by R; do not share R session state. Coalesce repeated condition-tree actions so one event burst causes one S model rebuild.
- Replace the pure-memory `BattleWritebackTransaction` triplicate snapshots with the smallest undo record that preserves affected battle state, selection, brush counters, and cache identities. Preserve native transaction preimages because they are required for rollback.

- [ ] **Step 1: Add a refresh-count test** showing that selecting another instruction in the already loaded Section does not reconvert the full document or reload assets.
- [ ] **Step 2: Remove the duplicate `BuildBattlePreviewInput` call** on normal load and on quick-mode pinned-Section reload; pass the already validated capture through to `LoadCapturedBattle`.
- [ ] **Step 3: Add move-aware snapshot construction** for callers that no longer use the captured document after conversion; keep const-reference callers unchanged until their ownership requirements are verified.
- [ ] **Step 4: Add a queued-action test** proving rapid condition changes produce one final model refresh using the last `ReplayOptions` state.
- [ ] **Step 5: Replace the three retained in-memory transaction states with one before-image or affected-record undo payload**; cover coordinate move, modify, delete/renumber, minion brush counters, and undo restoration.
- [ ] **Step 6: Search all production and test references before removing any legacy writeback builder or transaction method**. Keep a helper if it remains the sole testable S serializer for a required field rule.
- [ ] **Step 7: Run the refresh-state-machine, routing, writeback-undo, battle-editor-ops, and condition-tree gates.**

### Task 5: Verify runtime behavior, regressions, and executable size

**Files:**
- Update: `docs/2026-09-26-S战场布置器中期报告.md`
- Add or update: `tests/test_battle_preview_full_regression.ps1`
- Build outputs: isolated `.tmp-s-battle/r-alignment/` only

**Interfaces:**
- Compare the new Release x86 executable with the F-drive baseline using the same configuration, compiler/linker options, and package contents. Report EXE bytes, package bytes, source LOC by S module, and the linker map's major object contributions separately.
- The MFC QA copy remains isolated and complete with the EXE and its required INI/data/asset files; never test against the user's production EEX.

- [ ] **Step 1: Run the full CoreTests suite and all S writeback, condition-tree, quick-mode, branch-cache, refresh, QA-boundary, and undo PowerShell gates.**
- [ ] **Step 2: Run the existing R position-writeback, R condition-tree, and R refresh-routing gates** to prove the R path was not changed.
- [ ] **Step 3: Build fresh Debug QA and Release x86 executables in isolated directories**; do not use stale `Release-next` output as evidence for current source.
- [ ] **Step 4: In the QA copy, verify one existing-row update and one new insertion for each supported instruction class (`0x4B`, `0x46`, `0x47`) where sample evidence permits; verify S_12 active/inactive nested branches, undo, save, close, reopen, and reparse.
- [ ] **Step 5: Record before/after command values and timing for S_10 open, same-Section selection, cross-Section navigation, and condition toggles.**
- [ ] **Step 6: Compare same-configuration executable and package sizes to the F-drive baseline, explain any remaining size delta by linked objects/resources, and update the report with passed and pending evidence separately.**

## Acceptance Criteria

1. After clicking **QA写回**, the editor TreeView visibly shows the changed or newly inserted S instruction, displays its new values, selects the relevant node, and marks the document dirty.
2. Saving through the editor, closing, and reopening the QA copy reproduces the same S units and instruction values in the S preview.
3. A failed source check or injected native failure leaves no partial field/node changes and preserves the staged cache and undo behavior.
4. Only the currently active reachable condition workspace is submitted; unreachable branch records remain byte-for-byte unchanged, and stale inactive drafts are preserved and diagnosed rather than silently applied.
5. R preview code behavior remains unchanged and its focused regression gates pass.
6. Same-Section tree selection does not perform a full-document conversion; a normal S reload and pinned-section reload each use one converted capture.
7. Undo storage no longer retains duplicate full before/proposed/committed snapshots for every S edit.
8. Executable-size comparison uses matching Release x86 builds and identifies the source of any remaining growth; no claim relies on a Debug-versus-Release comparison.
9. Product writeback remains disabled until the documented evidence gates for all three command classes and real save/reload behavior are satisfied.
