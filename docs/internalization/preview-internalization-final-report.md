# Preview DLL internalization — Task 12 final evidence

Status: `DONE_WITH_CONCERNS`

This report closes the source/build/staging portion of Task 12. The old DLL,
launcher, phase-1 release, immutable baseline, candidate quarantine, and old
staging directories were not deleted or overwritten. No Git commit was made:
the repository has no configured `user.name`/`user.email`.

## Task 12 changes

- Added `tests/test_no_external_preview_runtime.ps1`. It scans only editor
  source/project files (`cczEditor2`, 62 files) for actual external-runtime
  tokens and checks the Release x86 import table with the installed x86
  `dumpbin`.
- Added `tests/test_migration_only_preview_cleanup.ps1` as a focused cleanup
  contract. RED found the uncalled `ResolveLimitedFreeRPreview` compatibility
  overload; the overload was removed from
  `cczEditor2/PreviewIntegration/FreeRPreviewResolver.h`. A caller scan found
  no remaining callers. No old DLL fixture was touched.
- Generalized `tests/test_targeted_review_contract.ps1` with optional
  `LauncherFileName` and `ExpectedReadmeMarker` parameters. The existing
  defaults remain `START-THREE-REVIEW-FIXES-EDITOR.cmd` and
  `针对性 PowerShell 回归脚本全部通过`.
- Added this report and the self-contained working staging metadata.

## RED → GREEN evidence

| Contract | RED evidence | GREEN evidence |
|---|---|---|
| Migration-only compatibility cleanup | `powershell -ExecutionPolicy Bypass -File .\\tests\\test_migration_only_preview_cleanup.ps1` → exit 1: dead `ResolveLimitedFreeRPreview` overload remained | Same command → exit 0, `MIGRATION_ONLY_PREVIEW_CLEANUP_PASS` |
| External runtime prohibition | Pre-cleanup scan → exit 0; no actual editor-runtime reference existed, so no artificial failing fixture was introduced | Same gate after cleanup/rebuild → exit 0, 62 source files scanned and Release imports checked; no `cczPreviewExt` import |
| Targeted review contract | Default old staging invocation → exit 1 because its preserved `s.exe` differs from the newly rebuilt Release EXE | New staging invocation with `-LauncherFileName START-INTERNALIZED-PREVIEW-EDITOR.cmd -ExpectedReadmeMarker 内置预览完整回归通过` → exit 0, `targeted_review_contract: PASS` |

## Automated gates

The following are fresh results from the Task 12 run. Exit codes are included;
the four non-zero outcomes are one expected immutable-baseline hash drift plus
three known legacy scene checks documented by the Task 9/11 reviews; none was
caused by Task 12.

### Scoped fix loop after release/staging mismatch

An independent review caught a P1 publication mismatch: a fresh Release build
had not been copied into working staging. The same agent then ran one fresh
Release x86 `/t:Rebuild` (exit 0), copied only `Release/s.exe` into the working
staging, and updated only the `s.exe` line in `SHA256SUM.txt`. No source,
baseline, old DLL, launcher, or old staging file was changed.

| Fix-loop command/check | Result |
|---|---|
| `MSBuild.exe .\\cczEditor2.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86 /m:1` | exit 0 |
| New targeted-review contract with final launcher/marker parameters | PASS, exit 0 |
| `test_no_external_preview_runtime.ps1` | PASS, exit 0 |
| `test_preview_core_project.ps1` / `test_preview_core_boundaries.ps1` | PASS, exit 0 / exit 0 |
| `test_native_preview_plan_integration.ps1` | PASS, exit 0; 10/10 |
| `Release\\cczPreviewCoreTests.exe` | PASS, exit 0; 119 PASS / 0 FAIL |
| Release/staging/SHA256SUM hash and exact-manifest check | PASS; all three hashes match, no missing/extra files |

The previously recorded working-staging hash is superseded; it must not be
used as final evidence.

| Command | Result |
|---|---|
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_internalization_baseline.ps1` | FAIL, exit 1: immutable baseline expects old `Release/s.exe` hash `5A3CB3DE...`; rebuilt EXE is intentionally different |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_core_project.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_core_boundaries.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_editor_preview_adapter.ps1` | PASS, exit 0; 10 adapter cases |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_session.ps1` | PASS, exit 0; 21 session cases |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_refresh_routing.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_preview_refresh_state_machine.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_dialogue_overlay_integration.ps1` | PASS, exit 0; headless boundary pass, real window deferred |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_condition_tree_internalization.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_variable_control_internalization.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_native_preview_plan_integration.ps1` | PASS, exit 0; 10/10 runtime contract cases |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_no_external_preview_runtime.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_combo_notification_routing.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_indexed_combo_integration.ps1` | PASS, exit 0; person=43, item=5, segment=3 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_indexed_combo_selection_commit.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_indexed_combo_ux_regressions.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_long_char_data_safety.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_remaining_combo_review_fixes.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_scene_background_preview.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_scene_multiselect_writeback.ps1` | PASS, exit 0 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_scene_multiselect_ui.ps1` | FAIL, exit 1: known old `primary X alignment command label is missing` assertion |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_scene_position_sources.ps1` | FAIL, exit 1 under Windows PowerShell 5.1 due the existing line-60 UTF-8/no-BOM parser error; same script PASS, exit 0 under bundled pwsh 7 |
| `powershell -ExecutionPolicy Bypass -File .\\tests\\test_remaining_scene_config_fixes.ps1` | FAIL, exit 1: known old Task7 `0x27` shape assertion; current event-driven path uses `RequestPreviewRefresh` |
| `.\\Debug\\cczPreviewCoreTests.exe` | PASS, exit 0; 119 PASS / 0 FAIL |
| `git diff --check` | PASS, exit 0; only existing ScenePreviewWnd LF/CRLF conversion notices |

The Release rebuild command was:

```text
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86 /m:1
```

It completed with exit code 0 and produced `Release/s.exe`. A quiet follow-up
build also returned exit code 0. The verbose rebuild emitted existing compiler
warnings (notably C4305/C4244/C4018/C4828); it emitted no errors. The warnings
are not hidden or reclassified as a clean warning-free build.

## Final Release and staging hashes

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `ccz-SceneEditor/Release/s.exe` | 4,991,488 | `86A3F2A92BB7BF83901686F1C3FB41721ABB3B3A9EC7ECEADAFD433F1A848D13` |
| `ccz_preview_ext/staging-test/ccz-editor-internalization-working/s.exe` | 4,991,488 | `86A3F2A92BB7BF83901686F1C3FB41721ABB3B3A9EC7ECEADAFD433F1A848D13` |

`SHA256SUM.txt` was checked against every listed file and the exact staging
manifest passed. The staging contains exactly:

```text
s.exe
CczCustom.ini
CczSceneEditor2.ini
CczString.ini
README.md
TEST-PLAN.md
SHA256SUM.txt
START-INTERNALIZED-PREVIEW-EDITOR.cmd
```

The launcher contains the required line:

```bat
start "" "%~dp0s.exe" %*
```

No `cczPreviewExt.dll` or `cczPreviewExtLauncher.exe` is present in this
staging. The old `START-INTERNALIZATION-WORKING.cmd` was removed only from
this working staging because the final artifact contract requires the named
launcher; no older staging directory was changed.

## Preserved rollback references

- Phase-1 DLL: `workspace/ccz_preview_ext/releases/phase1-20260916/cczPreviewExt.dll`, 420,352 bytes, SHA-256 `929DA194A7732163743D29C0CF570EB1535E566CFEFC716625CFE1331A724FF3`.
- Phase-1 launcher: `workspace/ccz_preview_ext/releases/phase1-20260916/cczPreviewExtLauncher.exe`, 123,392 bytes, SHA-256 `25B89C8111C327580D1E4C97ED32E87F786EDC82CA8EAAFADE06ABB2D0A3E87E`.
- Immutable reference DLL: `workspace/ccz_preview_ext/baseline/phase1-binary-929DA194-20260916/cczPreviewExt.phase1.reference.dll`, same DLL SHA-256 `929DA194...`.
- Older editor staging retained: `workspace/ccz_preview_ext/staging-test/ccz-editor-three-review-fixes-20260918/s.exe`, 4,781,056 bytes, SHA-256 `5A3CB3DE8AA4F71A9A602A8A5F9EE874E5818C4F04F037B0E1A7A3BC3C39E3AD`.

Rollback means launching the preserved phase-1 release through its original
launcher/staging pair; the final internalized working staging is independent
and does not overwrite those files.

## Accepted differences and concerns

- Accepted: the Release EXE hash differs from the immutable historical
  baseline because Task 12 rebuilt the internalized editor. The baseline
  manifest was not edited to conceal that difference.
- Accepted: the external-runtime source/import gate is scoped to editor
  runtime source/project files. Immutable baseline/docs and old DLL fixtures
  remain available as evidence and are not treated as editor dependencies.
- Severity P2, verification-only: the current Computer Use binding cannot
  reliably drive a real MFC editor window. The 15-item manual acceptance is
  `DEFERRED`, not PASS. Headless/direct-plan/core/artifact gates do not replace
  that smoke test.
- Severity P2, legacy test debt: the old multi-select label assertion, old
  Task7 0x27 source-shape assertion, and Windows PowerShell 5.1 parser failure
  remain as listed above. The position-source script passes under pwsh 7.
- Severity P2, build hygiene: existing narrowing/encoding warnings remain in
  the source-built Release build; no new Task12 warning cleanup was attempted.
- Severity P2, process: Git identity is unavailable, so the requested commit
  was intentionally not attempted.

No P0/P1 functional regression was found in the fresh automated/core/direct
plan evidence. A reviewer fix loop is not required for the Task12 source or
artifact changes; only the deferred real-window smoke and the explicitly
listed legacy verification debt remain.
