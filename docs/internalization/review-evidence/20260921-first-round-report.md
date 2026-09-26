# cczEditor2 新增代码全面 Review：第一轮证据报告

日期：2026-09-21  
审查性质：只读审查、回归扫描、发布门禁核对  
本轮结论：**源码未修改；功能门禁暂可继续，发布门禁仍阻塞**。

## 1. 本轮边界

本轮按 `docs/superpowers/plans/2026-09-20-ccz-editor-comprehensive-review.md` 执行 AUDIT/SWEEP：

- 冻结当前 `Release|x86` 产物和已有 staging/baseline 作为证据；
- 扫描内化预览、动态回放、Scene/Section/自由 R、写回/撤回、输入检索/文本辅助、资源路径与发布物；
- 执行现有 PowerShell 回归脚本和 `cczPreviewCoreTests.exe`；
- 区分源码问题、测试脚本兼容性、历史基线漂移和发布物不同步；
- 不改源码、不改旧 DLL、不覆盖旧 staging、不更新 immutable baseline。

## 2. 当前基线

| 产物 | 大小 | 时间 | SHA-256 |
|---|---:|---|---|
| `workspace/references/ccz-SceneEditor/Release/s.exe` | 5,204,992 | 2026-09-20 22:55:54 | `6927F1A2CBFE289355897FA832E56316C417FF4C86C43D0C71B893782B8C8072` |
| `workspace/ccz_preview_ext/staging-test/ccz-editor-dynamic-preview-20260920/s.exe` | 5,180,928 | 2026-09-20 11:56:26 | `0CF769BC2E02BC0A49F14E9A993EF4BCC4476CA8E67B22B7046C554FFAD8B599` |

当前 Release 与工作 staging 不是同一构建。旧 DLL、旧启动器、immutable baseline 均保留，未被本轮改动。

## 3. 自动化结果

### 3.1 PowerShell 回归

当前 Windows PowerShell 5.1 全量扫描共 31 个脚本：**26 PASS，5 FAIL**。

通过的 26 项覆盖：

- 预览核心/边界、PreviewSession、刷新路由与状态机、编辑器适配器；
- 普通 Scene 背景和位置来源、出战/自由 R 原生预览计划；
- 动态预览基础、对话 overlay、条件树、变量控制；
- 多选写回、写回撤回、地图场景；
- 人物/道具/分段装备检索、提交、通知顺序和输入生命周期；
- 长文本安全、内化后无外置运行时、迁移清理和全量 parity。

`Release/cczPreviewCoreTests.exe` 直接运行退出码为 0；对话/变量/条件/native replay plan/自由 R resolver/composer 测试全部通过。

### 3.2 失败项分级

| finding key | 脚本 | 现象 | 复核结论 |
|---|---|---|---|
| `REL-STAGE-001` | `test_targeted_review_contract.ps1` | staging `s.exe` 与 Release 不同 | **真实发布门禁问题，P1**；不代表源码功能失败 |
| `TEST-PS51-001` | `test_preview_catalog_and_controls.ps1` | Windows PowerShell 5.1 解析器报 `UnexpectedToken/Missing argument/Missing ')'` | **测试运行环境/编码兼容债务，P2**；同脚本在 PowerShell 7 通过 |
| `TEST-PS51-002` | `test_scene_multiselect_ui.ps1` | PS5.1 未识别中文菜单标签 `同 X 坐标` | **测试脚本读取编码问题，P2**；源码存在该标签，PowerShell 7 通过 |
| `TEST-PS51-003` | `test_dynamic_preview_regressions.ps1` | PS5.1 的精确源码形状断言找不到旧 `UpdatePreview` 标记 | **断言过时，P2**；PowerShell 7 运行完整回归通过 |
| `BASELINE-DRIFT-001` | `test_preview_internalization_baseline.ps1` | immutable baseline 仍期待旧 Release hash | **预期基线漂移，P2**；不得把新 hash 改回旧值 |

因此，除 staging 不同步外，本轮没有得到新的、可确认的 P0/P1 源码功能回归证据。PowerShell 7 只能消除前三项测试环境误报，不能消除 staging mismatch 或 baseline drift。

## 4. 已确认通过的关键边界

1. `test_no_external_preview_runtime.ps1` 通过：源码扫描和 Release import 检查未发现对 `cczPreviewExt.dll` 的运行时依赖。
2. `test_preview_full_parity.ps1` 通过：扫描 97 个相关源文件，内化预览核心边界存在。
3. Native replay plan 与 Free R resolver/composer 的 headless 测试通过，覆盖第二 Scene 配对、前置 appearance、触发 Section 回放、背景重置和误判防护。
4. 输入控件新增生命周期测试通过：数字/中文检索、候选提交、通知路由、光标/草稿生命周期和滚轮契约均有脚本证据。
5. 写回/撤回、多选、地图场景和条件树/变量控制的静态/核心边界测试通过。

这些结果只能证明源码和 headless 契约边界；不能替代真实 MFC 窗口验收。

## 5. 第一轮发现与处理建议

### P1：`REL-STAGE-001` 发布物不同步

当前 Release 为 `6927F1A2...B8C8072`，工作 staging 仍为 `0CF769BC...D8B599`，大小和构建时间也不同。继续从 staging 启动会验证到旧版本，无法作为当前 Release 的验收证据。

最小处理方案（留到 Gate 后执行）：只把最新 `Release/s.exe` 复制到指定 working staging，并只更新其 `SHA256SUM.txt` 中对应的 `s.exe` 行；不碰旧 DLL、旧启动器、README、baseline 或历史 staging。随后重跑 `test_targeted_review_contract.ps1`。

### P2：测试入口需统一

三项脚本在 `pwsh` 下通过，说明当前失败主要来自 Windows PowerShell 5.1 对 UTF-8/源码字面量和脚本语法的兼容性。建议后续将 CI/审查入口固定为 PowerShell 7；同时把 `test_dynamic_preview_regressions.ps1` 的精确源码标记断言改为稳定的函数/行为断言。此项不应通过改产品源码来“修复”。

### P2：immutable baseline 漂移

这是新构建产生新 hash 的正常结果。建议新增“当前 Release manifest”作为审查证据，而不是修改 immutable baseline 或把失败隐藏掉。

### P2：真实窗口验收仍缺失

当前有 core/headless/static 证据，但尚未完成真实 MFC 窗口 smoke：普通 Scene、出战界面、自由 R、动态播放同步、写回/撤回、搜索输入、资源路径和按钮启用状态仍需人工验收。按项目规则，在这一步完成前不能宣称“功能完整通过”。

### P2：工程既有编译警告

现有 Release/Core 构建仍记录窄化/编码/有符号比较等历史警告。本轮未将其归因于新增功能，也没有借审查之名扩大修改范围；后续若要处理，应单独建立 finding 和最小回归。

## 6. 门禁结论

| 门禁 | 结论 |
|---|---|
| Headless/core 功能门禁 | 暂定 GREEN：核心测试和相关静态回归通过 |
| 源码内化边界门禁 | 暂定 GREEN：无外置 DLL import，条件树/变量/Free R 等边界有证据 |
| 测试入口门禁 | YELLOW：PS5.1 有兼容性误报，需统一 runner/断言 |
| Release/staging 发布门禁 | **RED：`REL-STAGE-001` 未解决** |
| 真实窗口验收门禁 | **RED：尚未执行** |
| 全差异 review | 未完成：旧 DLL 行为逐入口对照和 Task 7 独立审查尚未开始 |

## 7. 下一轮建议

按风险最小顺序：

1. 先由 Gate 决定是否同步 working staging（不改变源码）；
2. 统一 PowerShell 7 审查入口，并修复三项过时/编码敏感的测试断言；
3. 再完成旧 DLL 与内化实现的逐入口差异表，重点检查历史上出现过的 `0x20`、`&*.174`、三个操作按钮、前置推演、地图头像和 `CczString.ini` 路径；
4. 最后做真实窗口验收，再决定是否进入独立 reviewer 和最终发布。

本轮没有提交源码变更，也没有把任何测试失败静默改成通过。

## 8. 第一轮问题处理后的复核（2026-09-21）

用户批准处理第一轮可直接修复项后，已完成以下最小变更：

- `test_preview_catalog_and_controls.ps1`、`test_scene_multiselect_ui.ps1`、`test_targeted_review_contract.ps1` 改为显式 UTF-8 解码；测试中的中文契约字面量改由 Unicode code point 构造，避免 Windows PowerShell 5.1 活动代码页误读。
- `test_dynamic_preview_regressions.ps1` 将依赖本地化注释的 `UpdatePreview` 边界改为 ASCII 函数边界 `OnPaint()`，不再受源码注释语言影响。
- working staging `ccz-editor-dynamic-preview-20260920\s.exe` 已同步当前 Release；`SHA256SUM.txt` 的 `s.exe` 行已更新为当前 Release hash。旧 DLL、旧启动器、历史 staging 和 immutable baseline 未动。

复核结果：上述三个原先受 PS5.1 影响的脚本均 PASS，`test_dynamic_preview_regressions.ps1` PASS，`test_targeted_review_contract.ps1` PASS。当前 Release/staging 一致性门禁已恢复 GREEN；immutable baseline 漂移和真实 MFC 窗口验收仍分别保留为历史证据与待验收项。

随后重新执行了 31 个 PowerShell 回归脚本：**30 PASS，1 FAIL**。唯一失败仍是 `test_preview_internalization_baseline.ps1` 的 immutable baseline hash 漂移，未修改 baseline；其余 30 项全部通过。
