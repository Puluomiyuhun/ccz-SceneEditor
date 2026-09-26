# cczEditor2 DLL 内化：第二轮逐入口差异审查报告

日期：2026-09-21  
审查性质：只读差异审查、调用点核对、核心回归复核  
本轮结论：**未确认新的 P0/P1 源码回归；存在 3 项 P2 验证缺口，发布前仍需真实窗口门禁。**

## 1. Routing Decision

本轮按 `docs/superpowers/plans/2026-09-20-ccz-editor-comprehensive-review.md` 的 AUDIT/SWEEP 路径执行，范围是：

- 外置 DLL 源码 `workspace/ccz_preview_ext/src/preview_runtime` 与内化核心 `cczPreviewCore/src` 的逐入口、头文件和调用点对照；
- `ScenePreviewWnd`、`PreviewAssetCache`、编辑器适配层与原 DLL bridge 的连接关系；
- 历史上出现过回归的 `0x20`、`&*.174`、三个操作按钮、前置推演、地图头像、自由 R 第二 Scene；
- 旧测试名与新测试名的覆盖差异，以及当前 Release/staging 产物一致性。

本轮不修改产品源码、旧 DLL、immutable baseline 或历史 staging；真实 MFC 窗口验收单独作为后续门禁，不用静态证据替代运行时结论。

## 2. Evidence Summary

### 2.1 产物

当前 Release 与工作 staging 的 `s.exe` SHA-256 均为：

`6927F1A2CBFE289355897FA832E56316C417FF4C86C43D0C71B893782B8C8072`

第一轮发现的 staging 不同步已处理；immutable baseline 仍保留旧 hash，属于预期漂移，未修改。

### 2.2 自动化

- `cczPreviewCoreTests.exe`：退出码 0，dialogue、condition、execution model、native replay plan、FreeR resolver/composer 等测试全部通过；
- PowerShell 5.1 全量 31 个脚本：30 PASS，唯一失败是 immutable baseline 的旧 hash 断言；
- 当前没有把旧 DLL 的原始窗口行为作为可自动化的真实 GUI 对照，因此静态/核心通过不等于 MFC 窗口验收通过。

### 2.3 运行时调用点

旧 bridge 的 `hook_lifecycle.cpp` 仅在两处调用新的 typed `replay_section_to`；没有运行时调用已移除的 raw/legacy replay API。因而旧兼容 API 的删除目前没有得到“现行运行时仍依赖”的证据，但仍留下测试覆盖缺口（见 `DLL-DIFF-001`）。

## 3. Findings

### P0/P1：未确认

本轮没有发现可由源码差异直接确认的 P0/P1 功能回归。以下问题都是验证或兼容性风险，不应在没有运行时复现时升级为产品缺陷。

### P2 `DLL-DIFF-001`：移除 legacy/raw replay API 后，旧契约测试没有等价替代

**证据**

- 内化头文件移除了 `ReplayNodeId`、`ConditionChoice`、`ReplayNode`、`DialogueReplayOptions`、`DialogueReplayResult` 及 raw replay 入口；
- `condition_tree` 的 legacy normalizer/compatibility builder 也被删除；
- 当前 bridge 只调用 typed `SectionExecutionModel` 路径，`rg` 未发现生产代码调用旧入口；
- 旧测试集合相对新测试少了 raw/legacy 条件语义、空 legacy initializer、不完整 section 等 6 个测试。

**影响判断**

这更像是内化后的 API 收缩，而不是已证实的运行时回归。若 DLL 外仍有第三方调用者，可能出现编译/行为兼容问题；在当前编辑器内部调用边界下，尚未发现活跃调用者。

**建议**

先不恢复旧 API。若项目仍承诺对外提供旧头文件，则补一个兼容层和等价测试；若不再对外暴露，则在开发文档中明确“legacy/raw API 已封存，内部统一 typed replay”，并为 typed 路径补覆盖说明。

### P2 `DLL-DIFF-002`：CczString.ini 的 CP936 解码实现发生了运行时环境变化

**证据**

旧实现使用 Windows `MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, ...)`；内化实现改为 `std::codecvt_byname<...>(".936")`，异常时返回空结果并继续走未加载/回退路径。当前 headless 测试的 ASCII/CP936 fixture 通过，但没有测试目标 Windows 安装是否始终提供 `.936` locale，也没有用真实发布目录的 CczString.ini 做窗口级验证。

**影响判断**

在当前机器和 fixture 上没有复现问题；在不同 Windows locale 或缺少相应 locale facet 的环境中，可能退化为指令名/资源名无法解析。用户已明确“CczString.ini 未加载”暂不作为本轮修复目标，因此本项不升级为当前源码缺陷。

**建议**

保留为发布验证项：在目标 Windows 发行目录放入实际 CczString.ini，启动一次并确认诊断、指令目录和预览名称；若出现 locale 依赖，再单独恢复 Win32 代码页转换或提供明确 fallback，不与本轮差异清理混做。

### P2 `DLL-DIFF-003`：真实 MFC 窗口的按钮/资源/推演联动仍未自动验收

**证据**

静态代码显示条件树、激活、变量和撤回按钮具有消息映射、创建、`EnableWindow(TRUE)`、布局和处理器；内化路径也包含 0x20 face state、`&*.174` 多对话、0x29/0x2A/0x2B 地图头像以及 Free R 第二 Scene 的 typed replay/composer。核心测试均通过，但没有自动化地点击真实窗口并确认：

- 三个操作按钮实际可见、可点击且不被 fallback 隐藏；
- 停止/暂停/步进/步退后左侧选择与预览帧同步；
- 0x20、`&*.174`、地图头像显示/移动/消失在发布目录资源下仍一致；
- 自由 R 的“布景 section → 触发 section”在第二 Scene 中按预期继承、重置和继续动态推演。

**影响判断**

这是验收空缺，不是从源码中确认的回归。静态证据足以说明“被误删/无条件禁用”的旧问题已经有对应代码，但不能证明 Windows 消息泵和资源加载路径在真实窗口中工作。

**建议**

在发布前做一次最小 MFC smoke，并保留截图/操作序列：普通 Scene、出战界面、自由 R 第二 Scene、动态播放、暂停/停止同步、三按钮和地图头像各一条。

## 4. 已确认保留或已覆盖的行为

| 功能/回归点 | 当前证据 | 结论 |
|---|---|---|
| `0x20` 武将状态/头像状态 | `face_state_instruction_id=32`；`dialogue_replay` 在 typed replay 中应用 face state | 内化核心未删，headless 通过 |
| `&*.174` 数据角色台词 | `parse_data_speaker_token` 与多对话集成测试覆盖最终 `*.174` | 未发现回归 |
| 单指令多对话 | `DialogueSegment`、0x14 拆分、首/末 segment 选择测试 | 已纳入 typed 路径 |
| 前置推演/选中节点 | typed `PreviewSession`、native plan 以及 selected command apply | 核心测试通过 |
| 地图头像 0x29/0x2A/0x2B | native plan 分类、timeline、ScenePreview 显示/移动/消失/写回入口 | 新逻辑存在，需窗口 smoke |
| 自由 R 第二 Scene | `build_native_replay_plan_to_end`、resolver/composer 的前置 appearance、背景重置、触发 section 回放测试 | headless 已覆盖主要契约 |
| 三个操作按钮 | 消息映射、创建、布局和多处 `EnableWindow(TRUE)` | 静态上未见无条件灰掉/删掉 |
| face archive、portrait resolver/overlay、script-variable index | 新旧源文件 API/行为基本一致 | 未见实质性差异 |

## 5. Refactor or Slimming Signals

1. 旧 raw/legacy replay walker 在现行 bridge 中没有生产调用点，是最明显的减法候选；但应先确认不再承诺外部 DLL API，再决定删除或转为兼容测试桩。
2. `ScenePreviewWnd` 同时保留 typed internal replay 与无效输入时的旧 `EvaluateToItem` fallback。两条路径目前由有效性判断隔离，不能在真实窗口验收前合并或删除。
3. `PreviewAssetCache` 的模块目录资源规则与旧 DLL 的多候选路径不同。它符合当前 EXE 内化设计和用户已确认的目录边界，本轮不建议扩大搜索路径。

## 6. Do Not Touch Yet

- 不修改 immutable baseline hash；
- 不恢复旧 DLL 或修改旧 staging；
- 不在本轮为 CczString.ini 改造资源搜索策略；
- 不删除 typed replay 的 fallback，也不以“旧 API 行数减少”作为功能缺失结论；
- 不把未做过的真实窗口点击结果写成自动化 PASS。

## 7. Verification Gaps

1. 目标 Windows locale 下的真实 CP936/CczString.ini 加载；
2. 真实 MFC 按钮可见性、启用状态、消息路由和悬浮窗交互；
3. 发布目录资源下的 `0x20`、`&*.174`、地图头像和自由 R 第二 Scene；
4. 停止/暂停/步进/步退与左侧选中节点的端到端同步；
5. 若继续对外发布旧 DLL 头文件，需补 legacy/raw API 兼容契约。

## 8. Gate Recommendation

| 门禁 | 结论 |
|---|---|
| Headless/core 功能门禁 | **GREEN**：核心测试通过 |
| 内化边界/无外置 DLL 依赖 | **GREEN**：当前扫描未发现运行时 import |
| 源码逐入口差异 | **YELLOW/GREEN**：无 P0/P1；legacy API 测试缺口为 P2 |
| Release/staging 一致性 | **GREEN**：两者 hash 已一致 |
| CczString.ini 目标环境 | **YELLOW**：用户暂不要求修复，需发布时实机确认 |
| 真实 MFC 窗口验收 | **RED**：尚未完成，不能宣称最终发布通过 |

下一步最小路径：先做一次真实窗口 smoke，记录按钮、暂停/步进同步、0x20/`&*.174`、地图头像和自由 R 第二 Scene；若 smoke 全部通过，再把 `DLL-DIFF-001/002` 标记为“已知边界/无需本版本处理”，进入最终发布审查。

本轮没有提交产品源码变更。
