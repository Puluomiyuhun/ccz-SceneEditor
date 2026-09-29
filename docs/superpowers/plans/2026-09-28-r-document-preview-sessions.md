# R Document Preview Sessions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every open R script restore its own unchanged preview session on MDI activation, while preserving the existing independent S session path and QA writeback gate.

**Architecture:** `CRScenePreviewPage` owns an R-only session map keyed by the process-local document instance ID. An activation request checks document/content/tree/selection identity before capture; an unchanged hit restores the cached R model and view state, while edits and tree rebuilds use the current full refresh path. S keeps its own page and session registry. Debug QA packaging explicitly enables the existing writeback compile gate.

**Tech Stack:** C++17, MSVC Win32/MFC, `cczPreviewCoreTests`, PowerShell source-contract gates.

**Spec:** `docs/superpowers/specs/2026-09-28-r-document-preview-sessions.md`

## Global Constraints

- Preserve every pre-existing uncommitted file and generated QA artifact; do not reset, clean, overwrite, stage, or commit existing changes.
- Keep R and S mutable preview state in separate page-owned sessions.
- Use `CcczEditor2Doc` instance identity, content epoch, and tree generation; a path alone is not a session key.
- Unchanged activation must perform no full document capture, model rebuild, or native scene replay.
- Selection changes and modified documents must continue through the validated existing refresh path.
- QA writeback stays Debug-only and requires `/p:CCZSBattleWritebackQa=true`; Release Win32 omits the QA button.
- Put new builds, logs, and test copies under `cczEditor2/.tmp-s-battle/all-script-sessions/`.
- Report GUI acceptance separately from automated tests and source checks.

---

### Task 1: Capture the QA button regression and activation baseline

**Files:**
- Read: `cczEditor2/.tmp-writeback-perf-20260928/obj/s.tlog/CL.command.1.tlog`
- Read: `cczEditor2/.tmp-s-battle/multi-document-sessions/debug/obj/editor/s.tlog/CL.command.1.tlog`
- Test: `tests/test_battle_writeback_qa_boundary.ps1`
- Test: `tests/test_preview_refresh_routing.ps1`

**Interfaces:**
- Confirm whether the compile command for `BattlePreviewPage.cpp` contains `CCZ_S_BATTLE_WRITEBACK_QA`.
- Confirm the R activation path and the existing Debug QA gate before editing.

- [x] **Step 1: Compare the two recorded compile commands.** The performance package command contains `_DEBUG` but lacks `CCZ_S_BATTLE_WRITEBACK_QA`; the earlier QA build contains both.
- [x] **Step 2: Compare the layout branches.** The screenshot’s full-width undo button matches the compile-time `#else`; the QA branch allocates a second `QA写回` button.
- [x] **Step 3: Run the focused writeback boundary and refresh routing tests before code changes.** Outputs are preserved under the task temporary root.

### Task 2: Add document-keyed R activation decisions

**Files:**
- Modify: `cczEditor2/PreviewIntegration/BattlePreviewSession.h` (generic document ID alias only; preserve S aliases)
- Modify: `cczEditor2/cczEditor2Doc.h/.cpp`
- Create: `cczPreviewCore/include/ccz/preview/r_document_session.h`
- Create: `cczPreviewCore/src/r_document_session.cpp`
- Create: `cczPreviewCoreTests/tests/r_document_session_tests.cpp`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj/.filters`

**Interfaces:**
- `PreviewDocumentId` is a `std::uint64_t` process-local identity; existing `SPreviewDocumentId` remains an alias.
- `RDocumentActivationSnapshot` contains document ID, normalized path key, content epoch, tree generation, TreeView identity, and selected-target identity.
- `DecideRDocumentActivation(cached, current)` returns `Reuse`, `Refresh`, or `RejectStaleHandles`; missing/changed version or target never returns `Reuse`.

- [x] **Step 1: Add tests for exact-match reuse, same-path different-ID isolation, content epoch change, tree-generation change, TreeView change, and selected-target change.**
- [x] **Step 2: Run the new focused test filter and confirm the missing API fails to compile or the cases fail.** The red baseline and green helper logs are preserved.
- [x] **Step 3: Add the generic ID alias/accessor without changing S call-site behavior.**
- [x] **Step 4: Implement the small R activation decision helper.**
- [x] **Step 5: Run the focused helper test and the S session test filter.**

### Task 3: Store and restore R page sessions

**Files:**
- Modify: `cczEditor2/ScenePreviewWnd.h/.cpp`
- Modify: `cczEditor2/ScenePreviewHost.h/.cpp`
- Modify: `cczEditor2/cczEditor2View.h/.cpp`
- Modify: `tests/test_preview_refresh_routing.ps1`
- Create: `tests/test_r_preview_document_sessions.ps1`

**Interfaces:**
- `CRScenePreviewPage::ActivateDocument(CTreeCtrl&, HTREEITEM, CcczEditor2Doc&)` checks identity and restores a matching cached session before requesting a refresh.
- `CRScenePreviewPage::ParkActiveDocument()` saves the current logical R state before switching to another document.
- `CRScenePreviewPage::CloseDocumentSession(PreviewDocumentId)` evicts only one R session.
- `CScenePreviewWnd` routes R activation to `CRScenePreviewPage`; S routing remains unchanged.
- `OnActivateView` submits an activation request; selection/edit refreshes remain forced refreshes.

- [x] **Step 1: Add source-contract cases proving activation uses the document session check before `CaptureEditorPreviewInput`, while selection changes still force refresh.**
- [x] **Step 2: Run the new source-contract test and confirm it fails against the single-session R page.**
- [x] **Step 3: Add the R-only session value and registry keyed by `PreviewDocumentId`; store preview session, last capture/snapshot, scene state, cached resolution, visible overlay, and static playback anchor.**
- [x] **Step 4: Park the active document before R-to-R or R-to-S routing; restore matching cached state and update condition/variable/playback controls without calling full refresh.**
- [x] **Step 5: On cache miss or mismatch, execute the existing refresh and refresh only that document’s cached value after successful capture.**
- [x] **Step 6: Validate the active TreeView and selected item before restoring any cached native handle.**
- [x] **Step 7: Run R route/session tests, existing refresh state-machine tests, R preview integration contracts, and all S session preservation tests.**

### Task 4: Bind cache lifetime to document and tree lifetime

**Files:**
- Modify: `cczEditor2/ScenePreviewHost.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Modify: `cczEditor2/cczEditor2Doc.cpp`
- Modify: `cczEditor2/cczEditor2View.cpp`
- Modify: `tests/test_battle_document_close_lifecycle.ps1`
- Modify: `tests/test_r_preview_document_sessions.ps1`

**Interfaces:**
- A tree rebuild invalidates that document’s cached native handles before the old TreeView is destroyed.
- `OnCloseDocument` erases only the closing document’s R session after normal save/close acceptance.
- `CScenePreviewWnd` keeps R and S cleanup calls separate and idempotent.

- [x] **Step 1: Add failing source checks for R tree-rebuild invalidation, document-close eviction, and cross-document preservation.**
- [x] **Step 2: Run the checks and confirm the R page currently lacks document-targeted cleanup.**
- [x] **Step 3: Add per-document R detach/rebuild/close methods and call them from the existing MFC lifecycle boundaries.**
- [x] **Step 4: Ensure cancellation and failed close do not erase a document session.**
- [x] **Step 5: Run close-lifecycle tests plus S session close tests.**

### Task 5: Restore the QA writeback entry in the test build

**Files:**
- Modify: `cczEditor2/.tmp-s-battle/all-script-sessions/build-qa.ps1`
- Create: `tests/test_r_preview_qa_package.ps1`

**Interfaces:**
- The build script rebuilds Debug Win32 using `/p:CCZSBattleWritebackQa=true`, then copies the fresh executable and QA resources to a new isolated package directory.
- The package keeps `.ccz-s-battle-writeback-qa` beside the QA EEX copies; this marker is the runtime writeback admission check.

- [x] **Step 1: Add a package-source check that requires the build property and QA marker to be independently present.**
- [x] **Step 2: Run it and confirm the previous performance package fails because its compile log lacks the macro.** The old executable also lacks the `QA写回` caption.
- [x] **Step 3: Add the isolated build script using the existing Visual Studio BuildTools MSBuild path and separate `debug/bin` and `debug/obj` directories.**
- [x] **Step 4: Build Debug Win32 with the QA property and verify `BattlePreviewPage.cpp`’s compile record contains `/D CCZ_S_BATTLE_WRITEBACK_QA`.**
- [x] **Step 5: Build Release Win32 without the QA property and verify its compile record lacks the macro.**
- [x] **Step 6a: Stage a fresh 29-file package and verify executable/resource hashes without overwriting `.tmp-writeback-perf-20260928`.**
- [ ] **Step 6b: Inspect the two bottom-row controls and click `QA写回` in a real MFC window.**

### Task 6: Full verification and handoff

**Files:**
- Modify: `docs/superpowers/plans/2026-09-28-r-document-preview-sessions.md`
- Create: `docs/r-document-preview-sessions-qa-2026-09-28.md`
- Builds only: `cczEditor2/.tmp-s-battle/all-script-sessions/`

**Interfaces:**
- QA report separates automated tests, build results, measured timings, and manual MFC checks.
- No numeric performance gain is claimed without same-machine switch measurements.

- [x] **Step 1: Run the new R session CoreTests and all R routing/session freshness contracts.**
- [x] **Step 2: Run R preview parity, writeback, selection, dynamic playback, and asset-cache regression gates.**
- [x] **Step 3: Run S quick-mode, condition-tree, writeback, session-preservation, close-lifecycle, and refresh-routing gates.**
- [x] **Step 4: Build isolated Debug QA and Release Win32 binaries, recording hashes and compile definitions.**
- [ ] **Step 5: Compare 20 alternating R-A/R-B activations before/after; record median, P95, full-capture count, replay count, and working-set peak.**
- [ ] **Step 6: Request the user’s real MFC confirmation for R/R and S/R tab switching and visible QA writeback interaction; record manual status without treating source tests as GUI acceptance.**
- [x] **Step 7: Review only the planned diff, preserve all pre-existing dirty state, and update this plan’s checkboxes and QA report.**
