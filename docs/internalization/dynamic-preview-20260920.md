# 动态预览阶段报告（2026-09-20）

## 范围

本阶段把动态预览接入内置 `cczPreviewCore → PreviewSession → CScenePreviewWnd` 链路，未加载外置 DLL，也不把播放中的临时位置写回剧本。

播放栏采用图标控件，提供两种范围：从开头播放到当前节点、从当前节点播放到结尾；同时提供播放/暂停、停止、前后单步、速度循环和进度显示。Esc、树刷新、节点编辑、条件/变量操作以及写回操作都会停止当前播放并回到静态预览。

## 回放语义

- 时间线保留当前 Section 的完整可达执行路径，并用 `range_begin/range_end` 表达“从头到选定”和“从选定到结尾”；条件和 Choice 使用静态预览同一套状态，Choice 没有显式选择时沿用 Case1 默认值。
- `0x32` 武将移动使用内场景逻辑坐标插值；`0x2A` 地图头像移动使用外场景像素坐标插值。
- 对话/信息类节点作为可观察停留段；无画面变化的普通指令和 `0x09` 延时以瞬时事件快速通过。
- “当前节点到结尾”模式保存目标前一事件的逻辑键，先恢复目标前状态，再执行目标节点，避免只显示当前一步。
- 播放默认倍率为 1x，单个有时长事件基础时长为 1200ms；速度按钮显示当前倍率并循环切换 1x、2x、4x、0.5x。静态进度条以 Section 推演起点为 0，两类特殊 Scene 也只在当前 Section 内步进/步退。
- 插值只作用于绘制用的临时 `SceneState`；段完成后才提交内存中的预览状态，`PositionWritebackSource` 始终保持原写回来源。

## Scene 形态边界

普通 Scene 沿用当前 Section 的静态回放；出战界面和有限自由 R 通过现有 Scene-pair resolver 解析，范围限制在相邻配对 Scene，并在 Section 粒度保存触发/出现节点身份。自由 R 使用独立 `FreeRReplayComposer`：先把匹配的“武将出现” Section 推演到可达终点作为继承场景，再叠加当前点击/退出 Section；当前 Section 出现新的 0x27 背景时从最后一个背景重置场景。普通 Scene 与出战界面不经过该合成器。

## 验证

已通过：

- `tests/test_dynamic_preview.ps1`
- `tests/test_dynamic_preview_regressions.ps1`
- `tests/test_preview_catalog_and_controls.ps1`
- `tests/test_preview_core_boundaries.ps1`
- `tests/test_preview_session.ps1`
- `tests/test_preview_refresh_routing.ps1`
- `tests/test_native_preview_plan_integration.ps1`

补充的全量预览契约组（core project/parity、无外置运行时、dialogue/条件/变量、场景背景/坐标、多选写回、撤回和针对性审核）也全部通过；Release x86 编译为 0 错误，保留工程既有 24 条编译警告。

本阶段还补充了 0x2A 像素移动、0x09 瞬时事件和对话停留段的时间线 fixture。刷新路由测试已更新为允许动态播放专用 `OnTimer`，但禁止计时器绕过统一刷新协调器。

## 交付边界

Debug 重点测试、Release x86 构建和 staging 复制已通过；真实窗口人工验收仍是最后的产品验收项。在该项完成前，不删除旧 DLL、启动器或历史 staging。

本轮 Release `s.exe` SHA-256：`0CF769BC2E02BC0A49F14E9A993EF4BCC4476CA8E67B22B7046C554FFAD8B599`。对应 staging：`workspace/ccz_preview_ext/staging-test/ccz-editor-dynamic-preview-20260920`。
