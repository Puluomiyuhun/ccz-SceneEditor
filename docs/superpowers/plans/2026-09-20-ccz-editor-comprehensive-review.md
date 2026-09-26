# cczEditor2 全量新增功能 Review Plan

> **For agentic workers:** 本计划只负责审查、证据收集和回归验证；发现问题后必须先进入 Gate，再按用户授权实施最小修复。

**目标：** 对内化预览、动态回放、场景编辑写回、输入/文本辅助和发布产物进行一次可追溯的全差异审查，区分新回归、历史遗留和验证缺口，最终达到可发布或明确阻塞项。

**架构：** 先冻结当前 Release 与旧 DLL/staging 的回滚基线，再按五个风险面进行只读审查；每个发现必须绑定源码证据、行为契约、复现步骤和回归测试。修复阶段只处理 P0/P1 以及用户明确接受的 P2，修复后由独立审查重新检查同一 finding key。

**技术栈：** Visual Studio 2022 BuildTools、MSBuild x86、C++/MFC、cczPreviewCore 单元测试、PowerShell 回归脚本、dumpbin、真实 MFC 窗口人工验收。

**依据：** `docs/internalization/preview-internalization-final-report.md`、`docs/internalization/dynamic-preview-20260920.md`、`CLAUDE.md` 及当前源码和测试目录。

## 全局约束

- 目标发布架构固定为 `Release|x86`，正式产物为 `Release/s.exe`。
- 不删除或覆盖旧 DLL、旧启动器、历史 staging 和 immutable baseline。
- 不把 Release 新 hash 改写成旧 baseline hash；二者必须作为不同证据保存。
- 未经 Gate 明确批准，不进行重写、跨模块重构、格式迁移或顺手清理。
- 预览临时状态不得写回剧本；只有明确的写回操作才可改变 EEX。
- 真实窗口未完成前不得声称“功能完整通过”；headless/static PASS 只能证明相应边界。

## 当前冻结基线

- 当前 Release：`Release/s.exe`，2026-09-20 22:55 构建，SHA-256 `6927F1A2CBFE289355897FA832E56316C417FF4C86C43D0C71B893782B8C8072`。
- 旧内化基线及 DLL 参照见 `docs/internalization/preview-internalization-final-report.md`，只读使用。
- 当前已知验证债务：真实窗口 smoke 尚未完成；旧多选 UI 标签断言、旧 scene-config 形状断言、Windows PowerShell 5.1 的 position-source 解析问题，以及工程既有编译警告，需要在报告中单独标注，不得伪装成新回归。

---

### Task 1：建立功能契约与变更清单

**范围：** 只读梳理用户需求、设计文档、当前源码和测试，形成“功能—实现—测试—风险”矩阵。

**重点文件：**

- `cczEditor2/ScenePreviewWnd.{h,cpp}`
- `cczEditor2/PreviewIntegration/*`
- `cczPreviewCore/include/ccz/preview/*`
- `cczPreviewCore/src/*`
- `cczEditor2/CMyComboBox.{h,cpp}`、`IndexedComboSearch.h`
- `cczEditor2/TextWrapAssist.{h,cpp}`、`LongCharData.h`
- `cczEditor2/cczEditor2View.{h,cpp}`、`CMyTreeCtrl.{h,cpp}`

**必须登记的功能契约：**

1. 人物、道具、分段装备组合框：数字/中文检索、手动输入、点击/Enter 提交、滚轮、Esc、Data 号/战场号、仅道具列表的 255 保留限制。
2. 换行辅助：对话1/2/3、信息两种模式、地图文字显示；字节预设/自定义、颜色标签排除、开关和上次设置持久化、多对话 `&` 拆分。
3. 普通 Scene 静态推演：背景、人物出现/移动/消失/动作/朝向、地图头像显示/移动/消失、坐标体系和动态位置来源。
4. 出战界面 Scene：菜单处理与特殊 Section 的边界、Section 开头到第一个 `0x00` 的条件扫描、出战测试识别范围。
5. 自由 R：两个 Scene 的配对、跨 Scene 的精确 Section 匹配、武将出现 Section 前缀场景、点击/退出触发 Section、背景重置、后续事件动态推演、范围不越过控制 Scene。
6. 场景编辑：单选/多选、主角色、快速/高级对齐、快速/高级等距、组移动/点移动、统一动作/朝向、批量消失、单个取消、地图头像操作、写回和撤回。
7. 动态播放：从头到当前/从当前到结尾、以 Section 推演起点为进度 0、步进/步退、暂停/停止/Esc、树选中同步、1x=1200ms、倍率显示和插值临时状态。
8. 内化运行时：条件树、变量控制、对话头像/文本 overlay、资源路径、无外置 DLL/import、按钮可用性、未激活子事件提示。

**输出：** `review-evidence/20260920-contract-matrix.md`，每一行包含契约、实现入口、现有测试、人工场景、状态和证据路径。

---

### Task 2：核心推演与旧 DLL 全差异审查

**目标：** 确认内化 `cczPreviewCore → PreviewSession → ScenePreviewWnd` 没有错误删减旧 DLL 的解析或状态转换。

**审查切片：**

- 指令目录与参数解码：`instruction_catalog.*`、`execution_model.*`。
- 台词/头像：`dialogue_model.*`、`dialogue_replay.*`、`dialogue_overlay_model.*`、`portrait_*.*`、`face_archive.*`。
- 条件/变量：`condition_tree.*`、`condition_tree_state.*`、`script_variable_index.*`、`variable_control_state.*`。
- Scene/Section：`native_replay_plan.*`、`FreeRPreviewResolver.*`、`FreeRReplayComposer.*`。
- UI 调度：`EditorPreviewAdapter.*`、`PreviewSession.*`、`PreviewRefresh*`。

**方法：**

1. 以旧 DLL 可观察行为和历史 staging 为参照，不以旧二进制 hash 作为源码等价证明。
2. 为每个旧 DLL 导出的功能建立“入口—状态—绘制—写回边界”记录。
3. 对每个缺失/新增分支标为 `confirmed regression`、`intentional internalization` 或 `verification gap`。
4. 重点核查曾出现过的差异：`0x20`、`&*.174` 台词头像、三个操作按钮、前置步骤推演、地图头像残留、`CczString.ini` 路径和 Free R 第二 Scene。

**输出：** `review-evidence/20260920-dll-diff-report.md`，不得直接修改源码。

---

### Task 3：Section/Scene/动态回放语义审查

**目标：** 证明普通 Scene、出战界面 Scene、自由 R Scene 三类路径不会串用错误的检索和状态。

**必须覆盖的案例：**

- 普通 Section：从 Scene/Section 起点构建前缀，选择任意中间指令仍包含此前背景、人物和变量状态。
- 出战界面：只在控制它的两个 Scene 范围内找特殊 Section；条件扫描限定为 Section 开头到第一个 `0x00`；不把正文 `0x07` 误判为出战测试。
- 自由 R：配对不限定在第一个 Scene；第二 Scene 的布景 Section 与触发 Section 分开处理；按 Section 精确匹配武将出现与武将点击测试；继承前缀场景后继续动态执行；当前事件出现新背景时重置，单纯后续指令不丢失前缀场景。
- 动态播放：起点/终点、步进/步退、单指令多对话、跨 Section 禁止、暂停/停止同步树节点、进度条和速度倍率。

**测试入口：**

- `tests/test_dynamic_preview.ps1`
- `tests/test_dynamic_preview_regressions.ps1`
- `tests/test_native_preview_plan_integration.ps1`
- `tests/test_preview_session.ps1`
- `tests/test_preview_refresh_routing.ps1`
- `tests/test_preview_refresh_state_machine.ps1`
- `cczPreviewCoreTests.exe` 的 dialogue/condition/variable/native_replay_plan 组。

**输出：** Section/Scene 状态转移表、每个失败案例的最小剧本片段和 `20260920-replay-report.md`。

---

### Task 4：预览写回、撤回和多选操作审查

**目标：** 检查所有可视化操作的目标指令、坐标来源、Section 范围和撤回事务，防止“预览看对但写错位置”。

**重点入口：** `ScenePreviewWnd.cpp` 中的多选状态、主角色、对齐/等距/组移动/点移动、地图头像操作、`PreviewUndoTransaction` 和 `PositionWritebackSource`。

**逐项验证：**

1. 单人调整位置：优先使用当前回放路径中最近一次有效移动，否则回退到出现指令；动作/朝向保持独立指令契约。
2. 多选：点击未选中加入，点击已选中移除；主角色顺序稳定；组移动保留相对位置；点移动共用主角色逻辑坐标；统一朝向按主角色；单个/批量消失不误改选择状态。
3. 地图头像：显示/移动/消失分别对应 `0x29/0x2A/0x2B`；移动和调整位置按外场景像素坐标；最近一次有效移动作为动态目标。
4. 撤回：一次用户操作对应一个事务；插入、坐标修改、批量修改可恢复原指令和树选中；撤回不影响未涉及节点。
5. 预览临时插值不得写入 EEX，只有确认写回才产生文档修改。

**测试入口：**

- `tests/test_scene_multiselect_ui.ps1`
- `tests/test_scene_multiselect_writeback.ps1`
- `tests/test_preview_writeback_undo.ps1`
- `tests/test_scene_position_sources.ps1`
- `tests/test_scene_background_preview.ps1`

**输出：** 写回矩阵、撤回事务日志和 `20260920-writeback-report.md`。

---

### Task 5：输入框、搜索和文本辅助审查

**目标：** 覆盖输入控件完整生命周期，而不只检查过滤算法。

**输入控件检查：**

- `CMyComboBox` 的草稿文本、原生高亮、逻辑选中、通知和焦点是独立状态。
- 自动搜索只过滤和展开；点击候选或 Enter 才提交；Esc、点击箭头、点击确定/取消的提交顺序明确。
- 中文检索与数字检索拥有相同的自动刷新；滚轮不破坏过滤；输入/删除不把光标移到最左侧。
- 人物列表允许完整编号范围；道具列表仅保留原有 255 约束；分段装备仍写回原道具编号。
- 检查 `CBN_SELCHANGE/CBN_SELENDOK/CBN_SELENDCANCEL/CBN_CLOSEUP/EN_KILLFOCUS` 的真实通知顺序。

**文本辅助检查：**

- 字节计算排除颜色标签（如 `[C28半山亭]`）。
- 信息指令模式一/模式二切换不串预设；对话3沿用对话1/2上限。
- 换行开关默认为启用并持久化；字节上限自定义值持久化；预览和导出文本一致。
- 单指令多对话按每个 `&` 段独立步进、步退和显示。

**测试入口：**

- `tests/test_indexed_combo_integration.ps1`
- `tests/test_indexed_combo_selection_commit.ps1`
- `tests/test_indexed_combo_ux_regressions.ps1`
- `tests/test_indexed_combo_input_lifecycle.ps1`
- `tests/test_combo_notification_routing.ps1`
- `tests/test_remaining_combo_review_fixes.ps1`
- `tests/test_long_char_data_safety.ps1`
- `tests/test_dialogue_overlay_integration.ps1`

**输出：** 输入生命周期时序图、文本辅助契约表和 `20260920-input-text-report.md`。

---

### Task 6：资源路径、内化边界和发布产物审查

**目标：** 确认新版 EXE 可独立运行，且资源加载路径与项目实际目录结构一致。

**检查项：**

- `CczString.ini` 等资源按编辑器实际 Mod 根目录结构加载，兜底路径只在契约允许时使用；缺失提示不能遮蔽有效资源。
- 源码和 Release import table 不再依赖外置 `cczPreviewExt.dll` 或启动器。
- 条件树、变量控制、按钮启用状态、悬浮窗和字体资源均存在于正式 Release 路径。
- Release/staging 清单、启动器、INI、README、SHA256SUM 一致；旧 staging 与旧 DLL 不被覆盖。
- Debug/Release 不混淆；交付只使用 `Release/s.exe`。

**测试入口：**

- `tests/test_no_external_preview_runtime.ps1`
- `tests/test_migration_only_preview_cleanup.ps1`
- `tests/test_preview_full_parity.ps1`
- `tests/test_targeted_review_contract.ps1`
- `tests/test_preview_internalization_baseline.ps1`（预期只报告历史 hash 差异，不得把它当作功能回归）

**输出：** Release manifest、import 扫描、SHA-256 和回滚说明。

---

### Task 7：独立审查与修复 Gate

**审查方式：** 主代理不先解释实现；将 Task 1–6 的原始证据、源码切片和测试结果交给一名隔离上下文的独立 reviewer，要求其按同一 finding key 输出 P0–P3 报告。只有用户明确要求时才派出该 subagent。

**报告格式：**

```text
finding_key:
severity: P0/P1/P2/P3
surface:
evidence_files:
reproduction:
expected:
actual:
regression_or_legacy:
minimal_fix:
required_test:
```

**Gate 规则：**

- P0：崩溃、数据损坏、错误写回、不可启动，阻塞发布。
- P1：核心推演/Section 配对/输入提交/写回语义错误，阻塞发布。
- P2：可见 UI、测试缺口、兼容性和已知警告；必须明确是否接受，不得静默忽略。
- P3：纯整理和非功能性改进，进入后续 backlog，不混入收尾修复。

修复时每个 finding 先写失败测试，再做最小变更；修复后只对原 finding key 做针对性 rereview，最后再跑全量门禁。

---

### Task 8：真实窗口验收与最终发布

**人工样本：** 使用 R_00、R_02、R_13、R_15、R_80 以及参考引擎中对应的普通 Scene、出战界面和自由 R 样本。

**人工操作矩阵：**

1. 普通 Scene：选中中间对话、背景、武将移动和地图头像节点，确认前缀状态完整。
2. 出战界面：进入特殊 Section，确认条件树、队伍状态和场景范围正确。
3. 自由 R：从第一 Scene 布景到第二 Scene 点击/退出触发，确认配对、继承、背景重置和动态指令。
4. 播放：两种范围、播放/暂停/停止、步进/步退、倍率、进度条、树同步、Esc。
5. 写回：单选、多选、地图头像显示/移动/消失、撤回；对照树中的实际指令。
6. 输入：人物/道具/分段装备数字和中文、滚轮、箭头、Enter、Esc、确定/取消。
7. 文本：换行开关/持久化、信息两模式、颜色标签、多对话拆分。
8. 资源：Mod 根目录有效 INI、缺失资源提示、重开 EXE 后状态。

**最终发布门禁：**

- Release x86 `/t:Rebuild` 返回 0。
- Core tests 和全部相关 PowerShell 测试通过；已知旧失败逐项登记并说明原因。
- 无新增 P0/P1；P2 有明确接受结论。
- Release 产物 hash、staging hash 和清单一致。
- 旧 DLL/旧启动器/旧 staging 可回滚。
- 真实窗口人工验收由用户确认通过。

## 预期交付物

- `review-evidence/20260920-contract-matrix.md`
- `review-evidence/20260920-dll-diff-report.md`
- `review-evidence/20260920-replay-report.md`
- `review-evidence/20260920-writeback-report.md`
- `review-evidence/20260920-input-text-report.md`
- `review-evidence/20260920-release-manifest.md`
- 一份合并后的 P0–P3 review report 和最终验收表。

## 停止条件

如果审查发现需要重写 `ScenePreviewWnd`、改变 EEX 数据格式、恢复外置 DLL 依赖，或需要用户未授权的发布目录替换，立即停止在 Gate，先提交独立方案，不直接改代码。
