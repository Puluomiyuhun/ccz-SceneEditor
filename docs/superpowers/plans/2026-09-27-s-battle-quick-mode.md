# S Battle Quick Mode Implementation Plan

> **For agentic workers:** Use the executing-plans workflow. The resolver, MFC lifecycle, and writeback path depend on the same pinned Section identity, so execute them in sequence in the current authorized workspace.

**Goal:** Add an S battle quick mode that starts enabled by default, loads the unique valid battle Section in Scene1 when an S script opens, and keeps its preview/edit state bound there while the user browses other Sections.

**Architecture:** Add a value-only resolver that examines the first Scene in a captured document and accepts exactly one Section whose scoped S battle parse contains a valid map. Capture the complete editor tree independently from the current selection, then pin the chosen Section identity and source tree in the S preview page. Track a content revision for the pinned Scene1 Section: edits there reload the preview after protecting pending preview edits, while navigation or edits in other Sections leave its cache intact. Writeback plans target that pinned Section and compare its revision; the native transaction's one-step undo continues to use a whole-document revision to avoid undoing over later document edits. A profile-backed S-page checkbox defaults to enabled.

**Tech Stack:** C++17, MSVC Win32, MFC TreeView, `cczPreviewCoreTests`, PowerShell source-contract tests.

**Spec:** `docs/2026-09-26-S战场布置器中期报告.md`, item 5 (approved quick-mode design).

## Global Constraints

- “打开 S 剧本时自动定位并读取 Scene1 的战场 Section；浏览其他 Section 时保留该战场的地图、布置缓存、未写回状态与可用的 QA 撤回。”
- “Scene1 是预览与写回的修改基线：其中的指令变化要更新预览；其他 Section 的浏览或内容变化不得改换该来源。”
- “写回始终以绑定的 Scene1 来源为目标，并在提交前重新校验该 Section；来源变化时保留缓存并给出诊断。”
- “切换剧本、退出快速模式或重新加载前处理未写回改动。自动定位失败时明确报错，不猜测其他 Section 为战场。”
- Quick mode is enabled by default on first launch; a user's later toggle choice is persisted in application profile settings.
- Keep S battle state and parsing separate from R scene state. Do not change R scene behavior.
- Add each behavioral test before its production implementation and observe the expected failure.
- Preserve all pre-existing review fixes and uncommitted workspace files; do not stage or commit the shared worktree.

---

### Task 1: Resolve Scene1's unique battle Section

**Files:**
- Create: `cczEditor2/PreviewIntegration/BattleQuickModeResolver.h`
- Create: `cczEditor2/PreviewIntegration/BattleQuickModeResolver.cpp`
- Modify: `cczPreviewCoreTests/tests/battle_quick_mode_tests.cpp`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj.filters`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Produce `BattleQuickModeResolution ResolveScene1BattleSection(const CapturedEditorTree&)`, with a resolved flag, selected Section handle/path/snapshot, and diagnostic.
- Produce `bool SelectCapturedBattleSection(const CapturedEditorTree& document, std::uintptr_t section_handle, const std::vector<std::uint32_t>& section_path, CapturedEditorTree& selected, std::wstring& diagnostic)` for rebinding a fresh full-document snapshot to the pinned Section.
- Scene1 is the first Scene container in the document root. Only its direct Section children are candidates. A candidate must parse successfully in its own scope and contain a valid nonnegative battle map. Zero and multiple candidates produce explicit diagnostics.

- [x] **Step 1: Add tests first** for a unique Scene1 map Section, candidate only in Scene2, no candidate, multiple Scene1 candidates, and rebinding a fresh snapshot to the pinned Section.
- [x] **Step 2: Build and run the new focused CoreTests filter**; confirm the resolver tests fail because the resolver is not yet implemented.
- [x] **Step 3: Implement the value-only resolver and snapshot rebinding helper**, then register production/test source files in the Visual Studio projects.
- [x] **Step 4: Rebuild and run the focused tests**, then run the full CoreTests runner.

### Task 2: Capture full documents and pin the S preview source

**Files:**
- Modify: `cczEditor2/PreviewIntegration/EditorTreeSnapshot.h/.cpp`
- Modify: `cczEditor2/BattlePreviewPage.h/.cpp`
- Modify: `cczEditor2/ScenePreviewHost.h/.cpp`
- Modify: `cczEditor2/cczEditor2View.h/.cpp`
- Modify: `tests/test_battle_quick_mode.ps1`

**Interfaces:**
- Produce `CaptureEditorDocumentSnapshot(CTreeCtrl&, const std::wstring&, CapturedEditorTree&, std::wstring&)` to copy the full document without requiring a selected Section.
- Track the source TreeView window and pinned Scene1 baseline in `CSBattlePreviewPage`.
- In quick mode, same-document Section navigation captures the full document and rebinds the pinned Scene1 Section. Unrelated Section changes preserve map/cache/undo. A changed pinned Section revision reloads the preview after asking before discarding staged preview edits; declining keeps the existing cache and diagnoses the mismatch.
- On initial S-document tree readiness, invoke the resolver without changing the user's tree selection; show an explicit diagnostic when Scene1 has no unique target.

- [x] **Step 1: Add source-contract tests** for whole-document capture on open, selection-independent quick routing, first-Scene-only candidate resolution, and default-on profile initialization; run them and observe the expected failure.
- [x] **Step 2: Implement document-only capture** while preserving the current idle-capture behavior for existing R callers.
- [x] **Step 3: Wire initial tree readiness and S-page quick navigation** through the existing preview host; keep R page routing unchanged.
- [x] **Step 4: Add the S-page quick-mode checkbox**, use `GetProfileInt(..., 1)` on first launch, save later user choices, and keep access to the toggle while the sidebar is hidden.
- [x] **Step 5: Build the MFC Debug Win32 project and run the quick-mode source-contract tests.**

### Task 3: Route Scene1 writeback and QA undo through the pinned baseline

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattlePreviewAdapter.cpp/.h`
- Modify: `cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp/.h`
- Modify: `cczPreviewCoreTests/tests/battle_quick_mode_tests.cpp`
- Modify: `tests/test_battle_quick_mode.ps1`

**Interfaces:**
- In quick mode, `BuildPendingWritebackPlan` and native QA writeback rebind each fresh document snapshot to the pinned Scene1 Section before planning.
- Writeback planning checks the pinned Section revision. Edits in other Sections do not reject the Scene1 delta. Native transaction undo still checks the full document revision before applying its inverse.
- The native transaction and its undo continue to use the original Scene1 Section handles, regardless of the currently selected Section.

- [x] **Step 1: Add tests first** showing another Section's content change leaves the pinned Section revision unchanged, a Scene1 edit changes it, and writeback accepts unrelated Section revisions but rejects a changed pinned Section; run and observe failure.
- [x] **Step 2: Route quick-mode writeback preflight and QA operations through the pinned source**; leave selection-based writeback unchanged when quick mode is off.
- [x] **Step 3: Run focused writeback CoreTests, the existing battle writeback gate, and the quick-mode integration source-contract tests.**

### Task 4: Guard mode, document, and view lifecycle transitions

**Files:**
- Modify: `cczEditor2/BattlePreviewPage.cpp/.h`
- Modify: `cczEditor2/ScenePreviewHost.cpp/.h`
- Modify: `cczEditor2/cczEditor2View.cpp/.h`
- Modify: `cczEditor2/ChildFrm.cpp/.h`
- Modify: `tests/test_battle_quick_mode.ps1`

**Interfaces:**
- Switching scripts, disabling quick mode, and closing the TreeView that owns pending S battle edits must ask before discarding; the warning defaults to No.
- Declining a Section transition restores the prior selection. Declining a document/mode transition keeps the cached preview and its source identity intact.
- Closing an unrelated editor view must not clear the S page bound to another still-open script.
- Disabling quick mode returns the S page to the current selected Section; enabling it resolves Scene1 again.

- [x] **Step 1: Add source-contract tests** for default-No transitions, mode-toggle rollback on cancel, targeted TreeView close handling, and MDI activation refresh; run them and observe the expected failure.
- [x] **Step 2: Implement transition and close routing** in the S page, preview host, editor view, and MDI child frame.
- [x] **Step 3: Run the lifecycle source-contract suite and existing session-preservation, refresh-routing, role-brush, and writeback gates.**

### Task 5: Update the S battle report and verify the combined change

**Files:**
- Modify: `docs/2026-09-26-S战场布置器中期报告.md`

- [x] **Step 1: Mark quick mode implemented only after its automated tests and builds pass**, record the default-on behavior, resolver failure behavior, and exact test results.
- [x] **Step 2: Build fresh isolated Debug Win32 CoreTests and MFC editor binaries** under `.tmp-review-fixes-20260927/quick-mode/` without overwriting existing Debug/Release binaries.
- [x] **Step 3: Run focused and full CoreTests, the new quick-mode gate, and the existing S battle regression scripts.**
- [x] **Step 4: Review the final diff against the approved report item, verify all review-fix changes remain present, and report that interactive MFC/QA-copy save-reopen acceptance remains for a live editor session.**
