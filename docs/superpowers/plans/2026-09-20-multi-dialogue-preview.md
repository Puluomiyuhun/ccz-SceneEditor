# 14号单指令多对白预览 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use inline execution with the TDD cycle below. Each step is independently testable.

**Goal:** Expand 0x14 dialogue commands into virtual dialogue playback events while preserving the original script tree, writeback format, and non-0x14 preview behavior.

**Architecture:** Normalize text once, decode 0x14 into a vector of dialogue segments, and expand only the playback timeline. Each virtual event retains its parent `LogicalNodeKey` plus a segment index; scene replay runs the parent command once and the dynamic overlay selects the segment.

**Tech Stack:** C++17, MSVC/Win32, existing `cczPreviewCore` static library, `cczPreviewCoreTests`, MFC editor preview integration.

**Spec:** `docs/superpowers/specs/2026-09-20-multi-dialogue-preview-design.md`

## Global Constraints

- Only instruction `0x14` is split; `0x15`, `0x16`, `0x7A`, and all other instructions retain current behavior.
- A marker is recognized only at text start or immediately after a normalized newline.
- Malformed markers fall back to the existing single-dialogue path and preserve diagnostics.
- Do not modify DLL code, EEX structure, script writeback, or wrap-assist settings.
- Timeline expansion occurs during timeline construction, never on each playback tick.

### Task 1: Add failing dialogue-segment tests

**Files:**
- Modify: `cczPreviewCoreTests/tests/dialogue_model_tests.cpp`
- Modify: `cczPreviewCoreTests/tests/playback_timeline_tests.cpp` if the project uses that file; otherwise add assertions to the existing timeline test source listed in the project file.

**Interfaces:**
- Tests will call the new `split_dialogue_segments(const std::wstring&)` helper and inspect `PlaybackEvent.dialogue_segment_index` after the implementation is introduced.

- [x] **Step 1: Add tests for two and three 0x14 segments.**
- [x] **Step 2: Add tests proving ordinary body newlines are preserved.**
- [x] **Step 3: Add tests for `&*.174` and malformed marker fallback.**
- [x] **Step 4: Add a timeline fixture asserting one parent node expands to three events and range boundaries use the span.**
- [x] **Step 5: Build the test project and verify the new tests fail because the API/metadata do not exist yet.**

### Task 2: Implement pure multi-dialogue decoding

**Files:**
- Modify: `cczPreviewCore/include/ccz/preview/dialogue_model.h`
- Modify: `cczPreviewCore/src/dialogue_model.cpp`
- Modify: `cczPreviewCoreTests/tests/dialogue_model_tests.cpp` only if a test helper needs a small fixture correction.

**Interfaces:**
- Produce `DialogueSegment` and `std::vector<DialogueSegment> split_dialogue_segments(const std::wstring&)`.
- Produce `std::vector<DialogueState> decode_dialogue_segments(const DialogueCommandView&, const DialogueProfile&)`.
- Keep `decode_dialogue_command(...)` as a compatibility wrapper returning the first/only segment.

- [x] **Step 1: Implement marker scanning after `normalize_dialogue_text`.**
- [x] **Step 2: Reuse existing `*.N` speaker token parsing and person-reference decoding for each segment.**
- [x] **Step 3: Preserve malformed input as one decoded state with a diagnostic.**
- [x] **Step 4: Run the dialogue-model tests and verify they pass.**

### Task 3: Expand the playback timeline without changing execution nodes

**Files:**
- Modify: `cczPreviewCore/include/ccz/preview/playback_timeline.h`
- Modify: `cczPreviewCore/src/playback_timeline.cpp`
- Modify: `cczPreviewCoreTests/tests/playback_timeline_tests.cpp`

**Interfaces:**
- Add `dialogue_segment_index`, `dialogue_segment_count`, and target span fields to the playback model.
- Keep `target_index` as the first event of the selected node for source compatibility; add the one-past `target_end_index` for multi-segment ranges.

- [x] **Step 1: Make `append_event` return one event per 0x14 segment and inherit the parent command/runtime handle.**
- [x] **Step 2: Set `StartToTarget` to `[0, target_end_index)` and `TargetToEnd` to `[target_index, events.size())`.**
- [x] **Step 3: Preserve one-event behavior for all non-0x14 nodes and one-segment 0x14 nodes.**
- [x] **Step 4: Run the timeline tests, including existing Choice/FreeR boundary tests.**

### Task 4: Route the virtual segment into dynamic preview overlays

**Files:**
- Modify: `cczEditor2/PreviewIntegration/PreviewSession.h/.cpp` as needed for a segment-aware frame request.
- Modify: `cczEditor2/ScenePreviewWnd.h/.cpp`.
- Modify: `cczEditor2/PreviewIntegration/DynamicPlaybackSession.*` only if cursor metadata must be exposed.
- Modify: `tests/preview_integration/*` for dynamic segment and step tests.

**Interfaces:**
- Change the dynamic frame helper to accept a `PlaybackEvent` or equivalent parent-key-plus-segment cursor.
- Scene replay continues to use the parent logical node; dialogue overlay selection uses the segment index.

- [x] **Step 1: Add a failing integration test for a second segment showing its own speaker/body/portrait.**
- [x] **Step 2: Implement segment-aware `BuildPreviewFrame`/overlay selection without replaying the parent scene twice.**
- [x] **Step 3: Verify step forward/backward moves through virtual segments and later commands retain the last segment.**
- [x] **Step 4: Verify both playback ranges and persistent progress use the expanded event span.**
- [x] **Step 5: Run the full preview integration test set.**

### Task 5: Build, package, and regression-check the internalized EXE

**Files:**
- Modify: only build outputs and the staging/launcher metadata required by the repository workflow.

- [x] **Step 1: Build `cczPreviewCoreTests` and the Win32 editor solution.**
- [x] **Step 2: Run all core and integration tests plus the existing PowerShell regression scripts.**
- [ ] **Step 3: Launch the internalized EXE against a real R script containing multi-dialogue 0x14 text and verify portrait transitions manually.**
- [x] **Step 4: Record the exact executable path and quick-start link for staging.**
- [x] **Step 5: Review the final diff for accidental DLL or script-format changes.**
