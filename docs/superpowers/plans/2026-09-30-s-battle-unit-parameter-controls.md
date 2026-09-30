# S Battle Unit Parameter Controls Implementation Plan

## Goal

Implement the approved S-only unit parameter editor and visual preview while preserving existing document/session boundaries and the R preview implementation.

## Steps

1. **[x] Placement profiles and mutation contracts**
   - Add branch-scoped optional default overrides per deployment instruction (`0x46`, `0x47`, `0x4B`).
   - Apply overrides after character/brush defaults are loaded; reset must clear overrides without discarding the selected character.
   - Add focused tests for profile preservation, type-specific supported fields, atomic target references, and existing-unit patch undo/dirty semantics.
2. **[x] Nonmodal S parameter pane**
   - Add an S-owned pane for future-placement defaults; defaults apply only to future enemy/ally placements (`0x46`/`0x47`).
   - Move current-unit parameter editing to the map context menu; expose single-unit and selected-group batch editing.
   - Warn only when original parameter values differ across the selected group, not when a uniform old value is changed to a new value.
   - Keep the existing deployment/character controls intact; do not alter R dialogs or TreeView editing.
   - Refresh the pane from the active document/branch/selection and submit one `BattleUnitPatch` transaction per Apply; exclude identity and position from batch edits, and skip fields already uniform at the requested value.
3. **[x] Per-document map preview selector**
   - Add the approved `BattlePreviewMode` state to each S document session.
   - Add the selector with the modes `战场编号/等级/兵种级别/行动方针/援军/无`; facing has no overlay mode.
   - Keep faction cell outlines always visible and preserve current selection outlines.
4. **[x] Map overlays and hover card**
   - Render role-colored logical-cell outlines, selected optional badges, and unambiguous AI target links from the active branch state.
   - Render the map-contained read-only hover card, including portrait resolution through the existing portrait resolver and a neutral fallback.
   - Hide the card during pan and on empty/outside cells; explicitly handle overlapping units.
   - In the hover card, show a Data target only for attack/follow policies and coordinates only for go-to/escape policies.
5. **[x] Selection interaction alignment**
   - Click an unselected unit to add it and click a selected unit to remove it.
   - Double-click an empty cell to clear the group; preserve drag selection.
6. **[x] Verification and handoff**
   - Run focused model and UI contract tests, then build the Win32 editor and test executable.
   - Prepare the updated executable and required INI resources in the existing test environment; do not create a ZIP.
   - Record the implementation and verification results in project memory for the user's return review.

## Execution Notes

- The nested editor repository already contains unrelated uncommitted S-battle work. Preserve it; do not reset, clean, or rebase.
- Do not change the R preview behavior or its UI.
- UI/manual script round-trip validation remains a user acceptance step when the user returns.

## Completion Evidence

- Fresh Release x86 editor build completed successfully; output is `Release/s.exe`.
- Fresh `cczPreviewCoreTests.exe`: 347 passed, 0 failed.
- `test_battle_role_brush_ui.ps1`, `test_battle_condition_companion_controls.ps1` (25 checks), `test_battle_quick_mode.ps1`, and `test_battle_writeback_qa_boundary.ps1` all passed.
- The test copy at `cczEditor2/S-Battle-Parameter-Test-20260930/` contains the new `s.exe`, the three INI files, `S_10.eex`, and Mod-root preview assets; all 12 manifest entries pass SHA-256 verification.
- Real MFC interaction and native save/reopen remain for the user's acceptance pass.
