# S Battle Condition Tree Integration Plan

> **For agentic workers:** Implement this plan task-by-task with focused test checkpoints. Keep the current shared worktree and preserve all pre-existing uncommitted changes.

**Goal:** Add an S-only condition tree that previews reachable 0x4B/0x46/0x47 deployments, preserves staged layouts per reachable branch, and routes QA writeback to the uniquely identified child event.

**Architecture:** Reuse the existing typed `SectionExecutionModel`, condition actions, and `CConditionTreePane`, but give the S page its own `ReplayOptions` and condition model. Resolve reachable deployment source keys and an insertion target from the pinned Scene1 snapshot. Store a battle editor workspace per effective reachable scope, and pass its source keys plus target event anchors to the existing value-only writeback planner and native QA transaction.

**Tech Stack:** C++17, MSVC Win32/MFC, `cczPreviewCore`, `cczPreviewCoreTests`, PowerShell source-contract tests.

**Spec:** `docs/2026-09-26-S战场布置器中期报告.md`, Section VI item 6; the user-approved discussion requires nested 0x4B/0x46/0x47 reachability, branch-local cached placement, child-event insertion, and fail-closed writeback.

## Global Constraints

- Keep S battle state and condition choices independent from the R preview session.
- Condition reachability must use the same typed condition model and evaluation rules as the existing R condition tree.
- Only reachable 0x4B/0x46/0x47 deployment instructions enter the active S layout; battle map and initialization parsing remain Section-scoped.
- Switching condition branches must preserve each branch's staged S layout.
- Existing deployment edits use their original source-node identity; new deployments require a unique, validated target child event and terminal insertion anchor.
- An unconditional container alone is not a condition branch.
- If a condition or insertion target is unresolved or ambiguous, preserve the staged cache and reject only the unsafe writeback operation with a diagnostic.
- Keep product writeback disabled; all native mutations remain behind the existing Debug QA-copy gate.
- Preserve the existing quick-mode default and Scene1 source pinning.
- Add each behavioral test before its production implementation and observe the expected failure.
- Do not stage or commit the shared worktree.

---

### Task 1: Resolve reachable S deployment scope from the shared condition model

**Files:**
- Create: `cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.h/.cpp`
- Modify: `cczPreviewCore/include/ccz/preview/battle_instruction_parser.h`
- Modify: `cczPreviewCore/src/battle_instruction_parser.cpp`
- Create: `cczPreviewCoreTests/tests/battle_condition_tree_tests.cpp`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj` and `.filters`
- Modify: `tests/test_battle_preview_writeback.ps1` only if it owns the C++ runner inventory

**Interfaces:**
- Build a `SectionExecutionModel` from the pinned S Section's converted `RawReplayNode` children and the shared editor instruction catalog.
- Resolve an immutable S condition scope containing a stable scope key, reachable deployment instruction keys, and either one validated insertion event/terminal pair or an explicit non-insertable diagnostic.
- Reachability decisions must use `execution_node_reaches_target`, `execution_case_reaches_target`, and the existing condition branch helpers rather than a second predicate evaluator.
- Extend battle parsing with an optional deployment-key filter; an empty/default filter preserves existing callers exactly.

- [x] **Step 1: Add failing tests** for true/else deployment reachability, nested Choice selection, unconditional-container behavior, unreachable/unsupported predicates, and ambiguous child-event targets.
- [x] **Step 2: Build and run only the new focused test cases**; confirm failures are missing scope resolution/filter behavior, not fixture errors.
- [x] **Step 3: Implement the value-only scope resolver and parser filter**, preserving map/init records outside the deployment filter.
- [x] **Step 4: Rebuild CoreTests and run the focused scope tests plus existing battle parser/condition-tree tests.**

### Task 2: Add an independent S condition session and condition-tree UI

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.cpp` only to expose the shared editor instruction-catalog builder
- Modify: `cczEditor2/cczEditor2.vcxproj` and `.filters` if a new adapter source is added
- Create: `tests/test_battle_condition_tree.ps1`

**Interfaces:**
- The S page owns its own `ReplayOptions`, current `SectionExecutionModel`, `CConditionTreePane`, and condition-pane button; it never reads or resets the R page's `PreviewSession`.
- Opening quick mode initializes the model from the pinned Scene1 Section without changing the native TreeView selection.
- Condition actions update only S condition options, rebuild the S reachable scope, and refresh the active S layout.
- Hide the condition-tree entry when there are no controllable predicate/Choice nodes; an unconditional Section remains the ordinary single-scope S preview.

- [x] **Step 1: Add source-contract tests** for S-only condition state, shared pane/model wiring, action-triggered battle refresh, and no-control visibility; run and observe failure.
- [x] **Step 2: Add the shared editor catalog entry point** and wire an S-only condition model/action route through `CConditionTreePane`.
- [x] **Step 3: Add condition button/pane layout and refresh integration**, keeping quick mode and existing R routing intact.
- [x] **Step 4: Run the new S condition-tree source-contract test and existing R condition-tree tests.**

### Task 3: Preserve each reachable branch's S layout and undo history

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Modify: `cczEditor2/PreviewIntegration/BattleConditionTreeAdapter.h/.cpp`
- Modify: `tests/test_battle_condition_tree.ps1`
- Add focused tests to: `cczPreviewCoreTests/tests/battle_condition_tree_tests.cpp`

**Interfaces:**
- Key a branch workspace by the effective set of reachable deployment source keys plus its target event identity, so visually/writeback-equivalent states reuse one workspace.
- Each workspace stores a `BattleEditorState`, its own `BattleEditorOps` undo stack, reachable source keys, and target event anchors.
- Switching the active scope saves the current workspace and restores or creates the next one; it does not clear another branch's staged placements.
- Discard prompts inspect pending edits and undo records across every workspace, not only the currently visible branch.

- [x] **Step 1: Add failing state tests** proving a staged unit survives true→else→true switching and distinct branch undo histories do not overwrite one another.
- [x] **Step 2: Implement branch workspace storage and active-workspace switching** while keeping `BattleEditorOps` bound to the stable S page state object.
- [x] **Step 3: Extend lifecycle guards** to detect dirty workspaces on document/mode/close transitions.
- [x] **Step 4: Run branch-state tests plus quick-mode, session-preservation, brush, and QA-boundary suites.**

### Task 4: Scope writeback to reachable sources and child-event anchors

**Files:**
- Modify: `cczEditor2/PreviewIntegration/BattleWritebackAdapter.h/.cpp`
- Modify: `cczEditor2/BattlePreviewPage.cpp`
- Modify: `cczPreviewCoreTests/tests/battle_writeback_tests.cpp`
- Modify: `tests/test_battle_preview_writeback.ps1`
- Modify: `tests/test_battle_condition_tree.ps1`

**Interfaces:**
- Pass the active workspace's reachable source keys and unique insertion parent/terminal keys into `BuildBattleWritebackSectionSnapshot`.
- Compare only the active branch's deployment nodes while still validating the pinned Scene1 Section revision and full-document native undo revision.
- Existing updates/deletes resolve by their original node keys; new insertions must resolve to the target event's immediate final 0x00 terminator.
- Missing source mappings, duplicate keys, unresolved conditions, or ambiguous event anchors fail before native mutation and retain every workspace cache.

- [x] **Step 1: Add failing adapter tests** for nested-branch update/delete, child-event insertion, ambiguity rejection, and cache-preserving failure.
- [x] **Step 2: Implement branch-scoped snapshot collection and child-event anchor validation.**
- [x] **Step 3: Route Debug QA preflight/apply/undo through the active workspace scope without enabling product writes.**
- [x] **Step 4: Run writeback CoreTests, QA boundary tests, and branch integration source-contract tests.**

### Task 5: Verify S_12 coverage, update status, and build

**Files:**
- Modify: `docs/2026-09-26-S战场布置器中期报告.md`
- Modify: this plan

- [x] **Step 1: Read-only parse representative S_12 samples** to confirm real nested predicate deployment structure; leave source EEX files untouched. Synthetic condition tests exercise true/false reachability and branch-local preview state.
- [x] **Step 2: Run the full CoreTests suite, all new S condition-tree gates, and existing S/R regression gates.**
- [x] **Step 3: Build an isolated MFC Debug Win32 executable** without overwriting existing user binaries.
- [x] **Step 4: Record automated/build/sample evidence and keep the product QA gate closed.**
- [ ] **Manual QA remains pending:** open an isolated S_12 QA copy in the Debug UI, switch condition branches, edit and preview deployment instructions, perform gated writeback, save/reopen, and verify undo. Native UI automation is unavailable in this session, so no live-window result is claimed.
