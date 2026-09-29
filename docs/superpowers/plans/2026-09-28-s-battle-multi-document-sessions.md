# S Battlefield Multi-Document Sessions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every open S script its own preview/edit session, preserve that session across document switches, and make unchanged-document reactivation skip capture, replay, and asset decoding.

**Architecture:** Assign each `CcczEditor2Doc` a stable process-local ID and content/tree generations. A C++ session registry owns the S model, writeback transaction, condition workspaces, and restorable UI values for each ID; the single MFC S page binds to one session at a time. The S host shares only immutable resources through a bounded decoded-bitmap cache, while R remains an independent page.

**Tech Stack:** C++17, MSVC Win32/MFC, `cczPreviewCoreTests`, PowerShell source-contract gates, GDI+.

**Spec:** `docs/superpowers/specs/2026-09-28-s-battle-multi-document-sessions.md`

## Global Constraints

- Scope is S battle preview sessions and their document lifecycle; preserve independent R preview state and behavior.
- New S document sessions start in Scene1 quick mode by default.
- Preview-only drafts remain memory-only and are cleared when their owning document closes or the application exits.
- S writeback must validate the current document, tree generation, pinned Section, logical source, and insertion anchors before mutation.
- Do not change S command serialization, reachable-branch semantics, QA-only writeback gating, or product writeback status.
- Preserve every existing uncommitted workspace change; do not reset, clean, overwrite, stage, or commit existing files.
- Write all new builds, logs, and QA copies below `cczEditor2/.tmp-s-battle/multi-document-sessions/`; do not overwrite `Debug/`, `Release/`, `Release-next/`, or prior QA copies.
- Compare performance using the same machine, script pair, build configuration, and switch sequence; report measurements rather than inferred speedups.
- Compare executable size only between matching Release Win32 builds from the same checkout baseline and final source; report any delta without using Debug size as a comparator.

## File Map

| File | Responsibility |
|---|---|
| Create `cczEditor2/PreviewIntegration/BattlePreviewSession.h/.cpp` | Platform-neutral per-document session data, version decision helper, and ID-keyed session registry. |
| Modify `cczEditor2/cczEditor2Doc.h/.cpp` | Stable document ID, content epoch, tree generation, mutation notifications, and document-close integration. |
| Modify `cczEditor2/ScenePreviewHost.h/.cpp` | Own the S session registry, bind/park a session, keep R routing independent, coordinate S-document close. |
| Modify `cczEditor2/BattlePreviewPage.h/.cpp` | Make the one visible S page bind to a session; save/restore its UI values; keep shared resource caches outside session state. |
| Modify `cczEditor2/cczEditor2View.h/.cpp` | Supply document identity on activation/refresh and notify completed tree rebuilds/content changes. |
| Modify `cczEditor2/ChildFrm.cpp/.h`, `cczEditor2/MainFrm.cpp` | Route MDI close, bulk close, and application close through per-document close decisions. |
| Read `cczEditor2/PreviewIntegration/BattleAssetCache.h/.cpp` | Confirm existing per-root revision-aware game-data parsing remains shared; decoded bitmap caching is implemented separately from this immutable data cache. |
| Create `cczEditor2/PreviewIntegration/BattleBitmapCachePolicy.h/.cpp` | Keep path/revision keys, LRU order, byte budget, and active-pin policy testable without GDI+. |
| Modify `cczEditor2/cczEditor2.vcxproj` and `.filters` | Register new editor source files. |
| Create `cczPreviewCoreTests/tests/battle_preview_session_tests.cpp` | Test session isolation, registry eviction, and version-state decisions without MFC. |
| Modify `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj` and `.filters` | Register session unit tests and source under test. |
| Create `tests/test_battle_preview_document_sessions.ps1` and `tests/test_battle_document_close_lifecycle.ps1` | Enforce MFC activation, stale-source, R/S routing, and close-path contracts. |
| Create `tests/test_battle_document_revision.ps1` and `tests/test_battle_preview_session_perf_counters.ps1` | Verify document mutation generations and Debug-only performance telemetry. |
| Modify existing S preview/asset regression scripts | Extend quick-mode preservation, asset reuse, and close assertions without weakening current gates. |
| Create `docs/s-battle-multi-document-session-qa-2026-09-28.md` | Store the exact test/build hashes, interactive results, and before/after performance data. |
| Modify `docs/2026-09-26-S战场布置器中期报告.md` and create a dated memory entry under `D:/AI曹操传项目群/memory/` | Record measured evidence and the completed lifecycle behavior after acceptance. |

## Task 1: Measure the existing switching path

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Create: `tests/test_battle_preview_session_perf_counters.ps1`
- Output only: `cczEditor2/.tmp-s-battle/multi-document-sessions/baseline/isolated/`

**Interfaces:**
- Add Debug/QA-only per-activation counters for full-document captures, pinned-Section captures, Battle input builds, Battle replay/load calls, JPG decodes, sprite decodes, and elapsed milliseconds.
- Write one `SPreviewPerf` log record per activation into the active QA copy directory; include the synchronous refresh and the first paint needed to display the resulting map/units so lazy sprite decoding is counted. Log output must not alter preview state or writeback decisions.

- [x] **Step 1: Add a failing source-contract test** asserting the Debug-only counter type, all seven counters, and the `SPreviewPerf` record prefix exist; run `powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\test_battle_preview_session_perf_counters.ps1` and observe the expected missing-counter failure.
- [x] **Step 2: Add the Debug-only measurement record** and instrument the current `RequestQuickModeRefresh`, `CaptureEditorDocumentSnapshot`, `CaptureEditorPinnedSectionSnapshot`, `BuildBattlePreviewInput`, `LoadCapturedBattle`, `LoadMapImage`, and unit-sprite decode paths.
- [x] **Step 3: Run the counter contract** and verify Release compilation preprocesses the measurement implementation out.
- [x] **Step 4: Build the instrumented current implementation in the isolated baseline directory**. Build projects sequentially with distinct intermediate directories to prevent cross-project output collisions.

```powershell
$multiSessionBaseline = Join-Path (Get-Location) 'cczEditor2\.tmp-s-battle\multi-document-sessions\baseline\isolated'
$multiSessionBin = Join-Path $multiSessionBaseline 'bin'
$multiSessionMsbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$multiSessionProjects = @(
    @{ Path = '.\cczPreviewCore\cczPreviewCore.vcxproj'; Name = 'core'; Qa = $false },
    @{ Path = '.\cczEditor2\cczEditor2.vcxproj'; Name = 'editor'; Qa = $true },
    @{ Path = '.\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'; Name = 'tests'; Qa = $false }
)
foreach ($project in $multiSessionProjects) {
    $projectObj = Join-Path $multiSessionBaseline ('obj\' + $project.Name)
    New-Item -ItemType Directory -Force -Path $projectObj, $multiSessionBin | Out-Null
    $buildArgs = @($project.Path, '/t:Rebuild', '/p:Configuration=Debug', '/p:Platform=Win32', "/p:OutDir=$multiSessionBin\", "/p:IntDir=$projectObj\", '/m:1', '/v:minimal')
    if ($project.Qa) { $buildArgs += '/p:CCZSBattleWritebackQa=true' }
    if ($project.Name -ne 'core') { $buildArgs += '/p:BuildProjectReferences=false' }
    & $multiSessionMsbuild @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Baseline Debug $($project.Name) build failed" }
}
```
- [x] **Step 5: Copy companion assets from the existing QA package** without copying its older `s.exe`, PDB, or libraries over the newly instrumented build. Put `CczCustom.ini` and `CczString.ini` beside `s.exe`; put the marker, `CczSceneEditor2.ini`, `Data.e5`, `Ekd5.exe`, `Hexzmap.e5`, `Unit_mov.e5`, `map/`, and `S_10_A.eex`/`S_10_B.eex` inside `writeback-qa-copies/`. Verify required files and record the instrumented EXE and EEX SHA-256 values.
- [ ] **Step 6: Run 20 alternating A/B activations** in the Debug editor and record the log's median/P95 time and operation counts. Preserve the log and package; do not use production EEX files.
- [x] **Step 7: Build the uninstrumented Release Win32 baseline** in `baseline/isolated/release/bin` with separate project object directories, then record `s.exe` byte size and SHA-256 for a matching final comparison.

```powershell
$baselineRelease = Join-Path (Get-Location) 'cczEditor2\.tmp-s-battle\multi-document-sessions\baseline\isolated\release'
$releaseBin = Join-Path $baselineRelease 'bin'
$releaseProjects = @(
    @{ Path = '.\cczPreviewCore\cczPreviewCore.vcxproj'; Name = 'core' },
    @{ Path = '.\cczEditor2\cczEditor2.vcxproj'; Name = 'editor' },
    @{ Path = '.\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'; Name = 'tests' }
)
foreach ($project in $releaseProjects) {
    $projectObj = Join-Path $baselineRelease ('obj\' + $project.Name)
    New-Item -ItemType Directory -Force -Path $projectObj, $releaseBin | Out-Null
    $buildArgs = @($project.Path, '/t:Rebuild', '/p:Configuration=Release', '/p:Platform=Win32', '/p:CCZSBattleWritebackQa=false', "/p:OutDir=$releaseBin\", "/p:IntDir=$projectObj\", '/m:1', '/v:minimal')
    if ($project.Name -ne 'core') { $buildArgs += '/p:BuildProjectReferences=false' }
    & $multiSessionMsbuild @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Baseline Release $($project.Name) build failed" }
}
Get-Item (Join-Path $releaseBin 's.exe') | Select-Object Length, FullName
Get-FileHash (Join-Path $releaseBin 's.exe') -Algorithm SHA256
```

## Task 2: Add and test the platform-neutral session registry

**Files:**
- Create: `cczEditor2/PreviewIntegration/BattlePreviewSession.h/.cpp`
- Create: `cczPreviewCoreTests/tests/battle_preview_session_tests.cpp`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj/.filters`

**Interfaces:**
- Define `using SPreviewDocumentId = std::uint64_t`.
- Define `BattlePreviewDocumentVersion { std::uint64_t content_epoch; std::uint64_t tree_generation; }`.
- Define `BattlePreviewActivationCheck { InitializeSession, ReuseSession, RevalidatePinnedSection, RebindTree }` and `CheckBattlePreviewActivation(has_baseline, saved, current)`; no baseline returns `InitializeSession`, a tree-generation mismatch returns `RebindTree`, a content-epoch mismatch returns `RevalidatePinnedSection`, and otherwise it returns `ReuseSession`.
- Define `BattlePreviewSessionRegistry::GetOrCreate(id)`, `Find(id)`, `Erase(id)`, and `Size()`. Store sessions behind stable `unique_ptr` addresses so `BattleEditorOps` can refer to session-owned `BattleEditorState`.
- A new session starts with `has_baseline == false`; only a successful initial capture sets it true.

- [x] **Step 1: Add tests for two independent IDs even when both have the same path, state mutation isolation, stable session addresses, closing only one ID, and all four activation-check outcomes.** Give the tests names containing `battle_preview_document_session` so the existing filter runner can select them.
- [x] **Step 2: Register the test file in the CoreTests project** and run this isolated RED build; the missing session header/API must cause the test target to fail before the registry implementation is added.

```powershell
$registryRed = Join-Path (Get-Location) 'cczEditor2\.tmp-s-battle\multi-document-sessions\registry-red'
$redBin = Join-Path $registryRed 'bin'
$redCoreObj = Join-Path $registryRed 'obj\core'
$redTestsObj = Join-Path $registryRed 'obj\tests'
New-Item -ItemType Directory -Force -Path $redBin, $redCoreObj, $redTestsObj | Out-Null
$multiSessionMsbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $multiSessionMsbuild '.\cczPreviewCore\cczPreviewCore.vcxproj' /t:Build /p:Configuration=Debug /p:Platform=Win32 "/p:OutDir=$redBin\" "/p:IntDir=$redCoreObj\" /m:1 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Registry RED dependency build failed' }
& $multiSessionMsbuild '.\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj' /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:BuildProjectReferences=false "/p:OutDir=$redBin\" "/p:IntDir=$redTestsObj\" /m:1 /v:minimal
$redExit = $LASTEXITCODE
if ($redExit -eq 0) { throw 'Expected registry RED build to fail before implementation' }
```
- [x] **Step 3: Implement the value-only session fields** for baseline capture, editor state/operations, native transaction, condition workspaces, source diagnostics, quick-mode values, and restorable viewport/tool values. Keep GDI+, MFC controls, HWND, and raw view pointers out of this type.
- [x] **Step 4: Implement the registry and activation-check helper** with no path-based aliasing and no implicit replacement when an ID already exists.
- [x] **Step 5: Build CoreTests into the isolated registry-green directory and run the focused filter**; require every two-document isolation assertion to pass.

```powershell
$registryGreen = Join-Path (Get-Location) 'cczEditor2\.tmp-s-battle\multi-document-sessions\registry-green'
$greenBin = Join-Path $registryGreen 'bin'
$greenCoreObj = Join-Path $registryGreen 'obj\core'
$greenTestsObj = Join-Path $registryGreen 'obj\tests'
New-Item -ItemType Directory -Force -Path $greenBin, $greenCoreObj, $greenTestsObj | Out-Null
$multiSessionMsbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $multiSessionMsbuild '.\cczPreviewCore\cczPreviewCore.vcxproj' /t:Build /p:Configuration=Debug /p:Platform=Win32 "/p:OutDir=$greenBin\" "/p:IntDir=$greenCoreObj\" /m:1 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Registry GREEN dependency build failed' }
& $multiSessionMsbuild '.\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj' /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:BuildProjectReferences=false "/p:OutDir=$greenBin\" "/p:IntDir=$greenTestsObj\" /m:1 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Registry GREEN build failed' }
& (Join-Path $greenBin 'cczPreviewCoreTests.exe') 'battle_preview_document_session'
if ($LASTEXITCODE -ne 0) { throw 'Session registry unit tests failed' }
```
- [x] **Step 6: Run `tests/test_battle_preview_document_sessions.ps1`** and add only the contract checks needed to verify the session registry is the S page's document-level state owner.

## Task 3: Assign document identity and reliable generations

**Files:**
- Modify: `cczEditor2/cczEditor2Doc.h/.cpp`
- Modify: `cczEditor2/cczEditor2View.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Modify: `cczEditor2/BattlePreviewPage.cpp`
- Create or extend: `tests/test_battle_document_revision.ps1`

**Interfaces:**
- Add `GetSPreviewDocumentId()`, `GetPreviewContentEpoch()`, and `GetPreviewTreeGeneration()` to `CcczEditor2Doc`.
- Add `MarkEditorContentChanged()` to increment the content epoch for a semantic EEX tree change; add `MarkEditorTreeRebuilt()` to increment both versions after a completed tree replacement.
- Epochs are process-local monotonic counters. View navigation and preview-only edits never increment the document content epoch.

- [ ] **Step 1: Add a failing PowerShell source-contract test** for unique document IDs, monotonic content/tree versions, and non-navigation mutation notification; run it and confirm each missing contract is reported.
- [ ] **Step 2: Inventory every semantic tree mutation** with `rg -n "InsertItem|SetItemText|SetItemData|DeleteItem|UpdateShow|int_data|SetModifiedFlag\(TRUE\)" cczEditor2\cczEditor2View.cpp cczEditor2\cczEditor2Doc.cpp cczEditor2\BattlePreviewPage.cpp cczEditor2\ScenePreviewWnd.cpp` and classify each result as load/rebuild, EEX content edit, R-only content edit, or view-only change.
- [x] **Step 3: Add the document ID and counters** to `CcczEditor2Doc`; assign the ID once in the constructor and initialize both versions to nonzero values.
- [x] **Step 4: Call `MarkEditorContentChanged()` at every audited semantic mutation**, including native S apply/undo and direct `ItemData::int_data` writes; leave selection/expand/scroll/preview-state edits unmarked.
- [x] **Step 5: Inventory every tree replacement route** with `rg -n "CreateFileTree|CreateNewTree|PrepareForTreeRebuild|DeleteAllItems|recur_close" cczEditor2\cczEditor2View.cpp cczEditor2\cczEditor2Doc.cpp`; notify rebuild completion exactly once per rebuilt tree and suppress per-node content increments while loading the replacement tree.
- [x] **Step 6: Run the revision contract plus existing writeback, quick-mode, and document-tree loading tests**; fix every missed mutation path revealed by the inventory before using the epoch as a fast path.

## Task 4: Move S mutable state into per-document sessions

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Modify: `cczEditor2/ScenePreviewHost.h/.cpp`
- Modify: `cczEditor2/cczEditor2.vcxproj/.filters`
- Modify: `tests/test_battle_preview_document_sessions.ps1`

**Interfaces:**
- `CScenePreviewWnd` owns one `BattlePreviewSessionRegistry` and the active S document ID.
- Add `ActivateSBattleDocument(CcczEditor2Doc& document, CTreeCtrl& tree)` to find/create, freshness-check, and bind the target session using the document's ID, version, and path.
- Change `RequestPreviewRefresh` to receive `CcczEditor2Doc&` alongside the tree/selection so a Section change cannot refresh whichever S session happened to be bound previously.
- Add `ParkActiveSBattleDocument()` to copy the current visible control values into the active session before changing documents/pages.
- `CSBattlePreviewPage` exposes `BindSession(BattlePreviewDocumentSession&, CcczEditor2Doc&, CTreeCtrl&)` and `UnbindSession()`; it remains a single visible MFC window.

- [ ] **Step 1: Extend the PowerShell contract** to require host registry ownership and a session-binding call before Battle load; run it and confirm failure against the singleton-only page.
- [x] **Step 2: Add the registry member and active-ID field** to `CScenePreviewWnd`; keep `CRScenePreviewPage` untouched.
- [x] **Step 3: Move each mutable S model field** from `CSBattlePreviewPage` into `BattlePreviewDocumentSession`: baseline capture, editor state/ops, native transaction, condition workspaces/model/options/catalog, quick-mode source/revision, diagnostics, and restorable UI values.
- [x] **Step 4: Keep only window/control/rendering members on `CSBattlePreviewPage`**; replace global model-member access with the active session pointer and fail closed if no session is bound.
- [x] **Step 5: Implement park/bind UI transfer** for zoom, scroll, selected unit/brush, search, active tab, deployment instruction, direction, sidebar state, and quick-mode checkbox; reset drag/hover/pressed transient state on every bind.
- [x] **Step 6: Run the new session unit tests and source-contract gate**, then run `test_battle_preview_session_preservation.ps1`, `test_battle_quick_mode.ps1`, `test_battle_role_brush_ui.ps1`, and `test_battle_condition_tree.ps1`.

## Task 5: Make activation cheap and source changes explicit

**Files:**
- Modify: `cczEditor2/ScenePreviewHost.cpp`
- Modify: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczEditor2/cczEditor2View.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattlePreviewSession.h/.cpp`
- Modify: `tests/test_battle_preview_document_sessions.ps1`
- Modify: `tests/test_battle_preview_session_preservation.ps1`

**Interfaces:**
- On activation, call `CheckBattlePreviewActivation(session.has_baseline, saved_version, current_version)` before any capture.
- `InitializeSession` performs the first full document capture and Scene1 resolution; `ReuseSession` binds the existing session without capture/build/replay.
- `RevalidatePinnedSection` captures only the current fixed Scene1 Section; equal Section revision keeps the session, while a changed revision reloads only when no preview draft is pending.
- `RebindTree` invalidates every old node handle and native undo record; reload from the replacement tree when no preview draft is pending, otherwise preserve the preview as stale and disable writeback/undo until explicit refresh or discard.
- A changed Scene1 with pending preview drafts marks the session stale and disables writes until explicit refresh/discard. Writeback always captures and validates the current tree independently of the activation fast path.

- [ ] **Step 1: Add failing tests** for unchanged activation performing no capture, unrelated content epoch changes recapturing only Scene1, changed Scene1 with pending drafts retaining state and refusing writes, changed Scene1 without drafts reloading, and tree-generation changes invalidating node handles before write/undo.
- [x] **Step 2: Implement the activation decision before `CaptureEditorDocumentSnapshot`** and return directly to session binding on `ReuseSession`.
- [x] **Step 3: Implement pinned-Section revalidation** on epoch changes; update the observed epoch when the pinned Section hash is unchanged.
- [x] **Step 4: Add a per-session stale flag/diagnostic** and make all QA writeback entry paths check it before building a native batch.
- [x] **Step 5: Keep a fresh current-tree capture and existing source/anchor checks immediately before every apply and undo**; verify the active document ID and tree generation match the session.
- [x] **Step 6: Run focused freshness tests and all quick-mode, condition-tree, writeback, and undo gates**; require the stale-session tests to prove both preservation and write refusal.

## Task 6: Separate tree detach, rebuild, document close, and bulk close

**Files:**
- Modify: `cczEditor2/ScenePreviewHost.h/.cpp`
- Modify: `cczEditor2/cczEditor2Doc.cpp/.h`
- Modify: `cczEditor2/cczEditor2View.cpp`
- Modify: `cczEditor2/ChildFrm.cpp/.h`
- Modify: `cczEditor2/MainFrm.cpp`
- Create: `tests/test_battle_document_close_lifecycle.ps1`

**Interfaces:**
- Add ID-targeted host operations `DetachSBattleTree(id)`, `CompleteSBattleTreeRebuild(id)`, `CanCloseSBattleDocument(id)`, and `CloseSBattleDocument(id)`.
- Closing confirmation is per document and covers only uncommitted preview drafts; standard document save still owns already-written native tree changes.
- A one-shot close approval is bound to one document close attempt and is cleared if normal save cancels or close does not complete.

- [ ] **Step 1: Add failing source contracts** for every close route, targeted session eviction, idempotent detach/close, canceled-save retention, and bulk-close return handling; run the script and confirm the existing `HWND`-only gate and `can_close` shortcut fail.
- [ ] **Step 2: Trace the live MFC close order** for `CChildFrame::OnClose`, `CcczEditor2Doc::SaveModified`, `CcczEditor2Doc::OnCloseDocument`, and `CMainFrame::OnAllFileClose` with Debug log markers; save the observed order in the temporary QA log before editing those paths. Confirm whether child close reaches `SaveModified` and whether bulk close uses its return value.
- [x] **Step 3: Replace HWND-only cleanup with ID-targeted detach and close operations**; make duplicate View/Doc destruction notifications safe and ensure only actual document close erases the registry entry.
- [x] **Step 4: Route preview-draft confirmation through the shared per-document close decision** and ensure `SaveModified` cancellation resets the close approval while retaining both document and session. An accepted preview decision is reused within that close attempt so the user sees the preview prompt only once.
- [x] **Step 5: Change `CMainFrame::OnAllFileClose` to close a document only when that same call's `SaveModified()` returns TRUE**; remove reliance on stale `can_close` as an alternate success path.
- [x] **Step 6: Run the close-lifecycle source gate and current session-preservation/refresh-routing tests**; verify a canceled close leaves the document ID registered and a successful close erases exactly that ID.

## Task 7: Share immutable assets through a bounded cache

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Create: `cczEditor2/PreviewIntegration/BattleBitmapCachePolicy.h/.cpp` for the platform-neutral keyed LRU policy; keep GDI+ bitmap ownership in the S page/resource adapter
- Modify: `cczEditor2/cczEditor2.vcxproj/.filters`
- Create: `cczPreviewCoreTests/tests/battle_bitmap_cache_policy_tests.cpp`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj/.filters`
- Modify: `tests/test_battle_asset_cache.ps1`

**Interfaces:**
- Define `BattleBitmapCacheKey { std::wstring normalized_path; std::uint64_t file_size; std::int64_t last_write_ticks; }` with equality/hash over all three fields.
- Cache keys contain normalized asset path and file revision (size plus last-write time, with content hashing only where metadata cannot disambiguate a required reload).
- `BattleBitmapCachePolicy::RecordDecode(key, bytes)`, `Pin(key) -> bool`, `Unpin(key)`, `Contains(key) -> bool`, `EvictToBudget(max_bytes) -> vector<key>`, and `ResidentBytes() -> size_t` manage metadata only; the S page owns actual GDI+ bitmap objects for returned eviction keys.
- Sessions store asset keys, not owning GDI+ bitmaps. The active page pins only currently used bitmaps; the shared cache evicts least-recently-used unpinned entries until it is under its measured memory budget.
- Existing per-root `Data.e5`, `Hexzmap.e5`, `Unit_mov.e5`, and `CczString.ini` parsing remains shared and revision-aware.

- [ ] **Step 1: Add policy tests** for same-key reuse, different-map separation, file-revision invalidation, LRU eviction, and pin-protected entries; register the test file and run the focused filter to observe expected failures.
- [x] **Step 2: Implement the platform-neutral LRU policy** and verify the focused unit tests pass with fake resource IDs and byte sizes.
- [x] **Step 3: Implement the GDI+ resource adapter in `BattlePreviewPage.cpp`** using policy keys plus bitmap entries; do not store GDI+ objects in `BattlePreviewDocumentSession`.
- [x] **Step 4: Replace the single `m_mapBitmap`/single-root sprite cache path** with cache-key lookups and active-page pins; release the page pins when parking a session.
- [x] **Step 5: Run bitmap-cache policy tests and `test_battle_asset_cache.ps1`**, then confirm a shared map decodes once across two sessions and a distinct map has a separate entry.

## Task 8: Full regression, MFC QA, and evidence report

**Files:**
- Modify: `docs/2026-09-26-S战场布置器中期报告.md`
- Create: `docs/s-battle-multi-document-session-qa-2026-09-28.md`
- Update only after implementation passes: a dated five-section entry under `D:/AI曹操传项目群/memory/`
- Build outputs and test copies only: `cczEditor2/.tmp-s-battle/multi-document-sessions/`

**Interfaces:**
- Debug QA build uses `/p:CCZSBattleWritebackQa=true`; Release Win32 build does not define the QA gate.
- Logs include EXE/EEX hashes, test counts, switching timings, operation counts, memory peak, and explicit manual-QA pass/pending status.

- [x] **Step 1: Run the complete CoreTests runner** from the isolated Debug output and confirm all tests pass; save the complete log.
- [x] **Step 2: Run every relevant PowerShell gate**: `test_battle_preview_document_sessions.ps1`, `test_battle_document_revision.ps1`, `test_battle_document_close_lifecycle.ps1`, `test_battle_preview_session_perf_counters.ps1`, `test_battle_preview_session_preservation.ps1`, `test_battle_quick_mode.ps1`, `test_battle_condition_tree.ps1`, `test_battle_preview_writeback.ps1`, `test_battle_writeback_qa_boundary.ps1`, `test_battle_role_brush_ui.ps1`, `test_battle_asset_cache.ps1`, `test_preview_refresh_routing.ps1`, and `test_preview_page_isolation.ps1`.
- [x] **Step 3: Run the R regression gates** `test_scene_multiselect_ui.ps1`, `test_scene_multiselect_writeback.ps1`, `test_condition_tree_internalization.ps1`, `test_preview_refresh_state_machine.ps1`, and `test_scene_position_sources.ps1`; verify no R-owned file or session behavior changed.
- [x] **Step 4: Rebuild isolated Debug QA Win32 and Release Win32 outputs** with separate `debug/bin`, `debug/obj`, `release/bin`, and `release/obj` directories beneath the task build root; record hashes and ensure the Release binary lacks the QA entry.

```powershell
$multiSessionRoot = Join-Path (Get-Location) 'cczEditor2\.tmp-s-battle\multi-document-sessions'
$multiSessionMsbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$multiSessionProjects = @(
    @{ Path = '.\cczPreviewCore\cczPreviewCore.vcxproj'; Name = 'core' },
    @{ Path = '.\cczEditor2\cczEditor2.vcxproj'; Name = 'editor' },
    @{ Path = '.\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'; Name = 'tests' }
)
foreach ($configuration in @('Debug', 'Release')) {
    $configurationRoot = Join-Path $multiSessionRoot $configuration.ToLowerInvariant()
    $configurationBin = Join-Path $configurationRoot 'bin'
    foreach ($project in $multiSessionProjects) {
        $projectObj = Join-Path $configurationRoot ('obj\' + $project.Name)
        New-Item -ItemType Directory -Force -Path $projectObj, $configurationBin | Out-Null
        $buildArgs = @($project.Path, '/t:Rebuild', "/p:Configuration=$configuration", '/p:Platform=Win32', '/p:CCZSBattleWritebackQa=false', "/p:OutDir=$configurationBin\", "/p:IntDir=$projectObj\", '/m:1', '/v:minimal')
        if ($project.Name -eq 'editor' -and $configuration -eq 'Debug') { $buildArgs += '/p:CCZSBattleWritebackQa=true' }
        if ($project.Name -ne 'core') { $buildArgs += '/p:BuildProjectReferences=false' }
        & $multiSessionMsbuild @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "$configuration $($project.Name) Win32 build failed" }
    }
}
```

- [x] **Step 5: Copy companion assets from `cczEditor2/.tmp-s-battle/r-alignment-verified/qa/bin/`** into the isolated Debug QA output without overwriting its fresh `s.exe`; include all files in `QA-资源清单.md` except the old EXE, PDB, and link libraries. Create two named S_10 EEX copies beside `CczSceneEditor2.ini`; never modify the user's production scripts.
- [ ] **Step 6: In the Debug QA editor, verify A→B→A session restoration, independent repeated add/move/write/undo, quick-mode state, condition workspace, source-stale refusal, R interleaving, map reuse, tree rebuild, closing B while A remains open, canceled bulk close, and application exit cleanup.** Save/reopen at least one copy and compare parsed instructions.
- [ ] **Step 7: Repeat the 20-switch A/B sequence used for the baseline**; report operation counts, median activation time (mean of the two middle samples), nearest-rank P95, and working-set peak side by side. Require hot activation of an unchanged session to show zero full-document capture, zero Battle rebuild, and zero decode for cached resources.
- [x] **Step 8: Compare final Release Win32 `s.exe` size/hash with the Task 1 baseline** and record the delta alongside source changes; keep Debug QA bytes in a separate row.
- [x] **Step 9: Update the report and memory record** with exact paths, hashes, tests, measurements, remaining manual gates, and any unresolved issue; do not claim an unperformed test passed.
- [ ] **Step 10: Review the complete diff and `git status --short`**, confirm only planned source/docs/tests changed and all pre-existing user edits remain intact; do not stage or commit.

## Acceptance Criteria

1. All open S documents have distinct session IDs and independent preview drafts, state, condition workspaces, and writeback undo histories.
2. Switching to an unchanged existing S session performs no full-document capture, Battle rebuild, or decode for assets still cached.
3. Scene1 mutation while a session is parked is detected; pending drafts remain visible and writeback is refused until explicit refresh/discard.
4. A tree rebuild invalidates every old node handle before any write/undo can reach the replacement tree.
5. Closing one S document evicts only its session; canceling child, bulk, or application close retains the affected document and session.
6. R preview tests pass unchanged, and S-to-R-to-S switching preserves every still-open S session.
7. Decoded image caching is bounded and does not retain per-document copies outside active resource pins.
8. Isolated Debug QA and Release Win32 builds pass, all listed automated gates pass, and manual MFC results are recorded separately.
9. No pre-existing user changes are overwritten, reset, cleaned, staged, or committed.

## Self-Review Checklist

- Spec requirements map to Tasks 2–8; current-path performance evidence maps to Task 1.
- Registry identity, content epoch, tree generation, stale-source policy, R/S isolation, close policy, asset memory bounds, and performance measurement each have explicit tests and owners.
- The bulk-close `SaveModified()` return-value bug is tested before code changes and corrected before close acceptance.
- Every generated EXE, object file, log, and QA EEX copy stays under the new task-specific temporary directory.
- This plan does not ask workers to commit or alter unrelated dirty files.
