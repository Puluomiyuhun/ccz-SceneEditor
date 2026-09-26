# Dynamic Scene Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变现有静态回放语义的前提下，为当前 Section 增加带移动插值的播放器式动态预览。

**Architecture:** `cczPreviewCore` 生成已经解析好的有序播放时间线；编辑器侧 `DynamicPlaybackSession` 管理游标、播放范围和计时；`CScenePreviewWnd` 只负责图标控件、计时器和绘制临时帧。静态 `PreviewSession` 仍是普通节点选择的唯一状态来源，动态层不会改变 TreeView 或写回栈。

**Tech Stack:** Visual C++ 2022、MFC、C++17、x86、GDI+、现有轻量 C++ 测试框架和 PowerShell 回归脚本。

**Spec:** `docs/superpowers/specs/2026-09-20-dynamic-preview-design.md`

## Global Constraints

- 保持现有 `PreviewCore → EditorPreviewAdapter → PreviewSession → CScenePreviewWnd` 分层。
- 动态播放不加载外置 DLL，不写入剧本，不修改静态回放契约。
- `cczPreviewCore` 不依赖 MFC、HWND 或 HTREEITEM。
- 第一阶段播放边界是当前已解析 Section，不猜测未决条件、Choice 或自由R跳转。
- 0x32 使用内场景逻辑坐标插值，0x2A 使用外场景像素坐标插值。
- 每个新行为先写失败测试，再写最小实现。

---

### Task 1: Add the Core Playback Timeline

**Files:**
- Create: `cczPreviewCore/include/ccz/preview/playback_timeline.h`
- Create: `cczPreviewCore/src/playback_timeline.cpp`
- Modify: `cczPreviewCore/cczPreviewCore.vcxproj`
- Modify: `cczPreviewCore/cczPreviewCore.vcxproj.filters`
- Test: `tests/preview_integration/playback_timeline_tests.cpp`
- Modify: `tests/test_preview_core_boundaries.ps1`

**Interfaces:**
- `enum class PlaybackRange { StartToTarget, TargetToEnd };`
- `enum class PlaybackSegmentKind { Instant, Hold, CharacterMove, PortraitMove };`
- `struct PlaybackEvent { LogicalNodeKey logical_key; DialogueCommandView command; PlaybackSegmentKind kind; std::int32_t duration_units{}; };`
- `struct PlaybackTimeline { std::vector<PlaybackEvent> events; std::size_t target_index{}; LogicalNodeKey pre_target_key; bool complete{}; std::wstring diagnostic; };`
- `PlaybackTimeline build_playback_timeline(const SectionExecutionModel&, const LogicalNodeKey&, PlaybackRange, const ReplayOptions&);`

- [x] **Step 1: Write failing tests** for start-to-target inclusion, selected-to-end inclusion without duplicate target, default Case1 path, inactive target rejection, and unsupported-branch diagnostic.
- [x] **Step 2: Run the focused test executable and verify the new tests fail for the missing API/behavior.**
- [x] **Step 3: Implement a branch-aware execution-path collector by reusing the same predicate and Choice decisions as native replay; classify only visible/stateful commands as timeline events and preserve semantic commands as zero-duration events.**
- [x] **Step 4: Run the focused tests and the existing preview-core boundary script.**
- [x] **Step 5: Add project entries and rerun the x86 core build.**

### Task 2: Add the Editor Dynamic Playback Session

**Files:**
- Create: `cczEditor2/PreviewIntegration/DynamicPlaybackSession.h`
- Create: `cczEditor2/PreviewIntegration/DynamicPlaybackSession.cpp`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`
- Test: `tests/preview_integration/dynamic_playback_session_tests.cpp`
- Modify: `tests/test_preview_session.ps1`

**Interfaces:**
- `struct PlaybackClock { std::int64_t elapsed_ms{}; };`
- `struct PlaybackCommand { bool active{}; bool finished{}; std::size_t event_index{}; double progress{}; };`
- `class DynamicPlaybackSession { bool Start(const PlaybackTimeline&, PlaybackRange); void Pause(); void Stop(); PlaybackCommand Tick(std::int64_t elapsed_ms); bool StepForward(); bool StepBackward(); };`

- [x] **Step 1: Write failing tests** for play/pause/stop, mode start indices, speed-independent normalized progress, and cancellation after a document generation change.
- [x] **Step 2: Run the focused tests and verify expected failures.**
- [x] **Step 3: Implement the session as a value-owned cursor/timer state machine; it must not know about MFC or writeback sources.**
- [x] **Step 4: Add movement duration policy with a minimum/maximum clamp and speed multiplier.**
- [x] **Step 5: Run focused and existing PreviewSession tests.**

### Task 3: Integrate Temporary Scene/Portrait Interpolation

**Files:**
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Test: `tests/preview_integration/dynamic_scene_interpolation_tests.cpp`

- [x] **Step 1: Write failing tests** for 0x32 logical-coordinate endpoints, 0x2A pixel-coordinate endpoints, target direction only at arrival, and no writeback during an active transition.
- [x] **Step 2: Run the focused tests and verify expected failures.**
- [x] **Step 3: Add a temporary render-state copy separate from `m_state`; interpolate only during paint/timer ticks and commit the final state at segment completion.**
- [x] **Step 4: Keep `PositionWritebackSource` unchanged until the segment finishes and invalidate the temporary frame on Stop/Esc.**
- [x] **Step 5: Run focused tests and compile the editor.**

### Task 4: Add the Icon-Based Player Bar

**Files:**
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Modify: `cczEditor2/resource.h`
- Modify: `cczEditor2/cczEditor2.rc`
- Modify: `tests/test_preview_catalog_and_controls.ps1`

- [x] **Step 1: Add control-contract assertions** for two range buttons, play/pause, stop, step back/forward, speed, and progress controls with tooltip text.
- [x] **Step 2: Run the control script and verify the new contract fails before controls are added.**
- [x] **Step 3: Create the dark bottom bar and vector/GDI icons; use blue active, gray disabled, and red warning states.**
- [x] **Step 4: Wire range selection, playback commands, speed cycling, tooltips, and progress display without changing TreeView selection.**
- [x] **Step 5: Make node edits, condition changes, variable changes, writeback, document refresh, and Esc stop playback.**
- [x] **Step 6: Run the control script and editor build.**

### Task 5: Regression and Packaging Gate

**Files:**
- Create: `tests/test_dynamic_preview_regressions.ps1`
- Modify: `tests/test_preview_full_parity.ps1`
- Create: `docs/internalization/dynamic-preview-20260920.md`

- [x] **Step 1: Add fixture coverage** for ordinary Scene, deployment Scene pair, limited freeR, inactive branch, Choice Case1/Case2, 0x32, 0x2A, dialogue and 0x09 delay.
- [x] **Step 2: Run the new regression script and full existing preview suite.**
- [x] **Step 3: Build Release x86 and verify static preview regression output.**
- [x] **Step 4: Produce a date-versioned staging copy without overwriting an occupied staging directory, then report the launcher link and verification hashes.**

### Follow-up: FreeR strategy and revised playback contract

- [x] Keep FreeR composition behind `FreeRReplayComposer`, gated by the resolver's `ScenePreviewKind::FreeR`; ordinary Scene and deployment Scene continue to use their existing paths.
- [x] Build the inherited prefix by replaying the matching “武将出现” Section to its reachable end, then append the current click/exit Section.
- [x] Reset the inherited snapshot only when the current Section reaches its last 0x27 background; keep the prefix for non-background trigger content.
- [x] Use a complete Section timeline with `range_begin/range_end`: progress is anchored at Section origin, target-to-end starts at the target index, and both step directions remain Section-local.
- [x] Set default playback to 1x = 1200ms per timed unit and expose the current multiplier on the speed button.
