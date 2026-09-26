# cczEditor2 独立代码审查报告

日期：2026-09-21  
审查方式：隔离上下文 subagent 独立审查  
范围：Task 1–6；排除真实窗口人工验收（Task 8）  
独立审查阶段产品源码变更：无  
审查后按本报告建议实施了一个最小修复，见文末复核记录。

## 总结

- 未发现新的 P0。
- 未发现新的 P1。
- 确认 1 个当前实现引入的 P2 UI 回归。
- 复核 3 个已有 P2 验证/契约缺口。
- 核心源码与 headless 测试覆盖较高，但真实窗口和目标资源环境仍未验证。

## Findings

### `UI-MULTI-DOUBLECLICK-001`

- severity: P2
- surface: 预览窗口多选交互
- evidence: `cczEditor2/ScenePreviewWnd.cpp:4551-4574`
- reproduction: 已选中多个角色时双击普通角色。
- expected: 地图头像不参与多选；双击空白处取消全部；普通角色的选择状态只由单击 Toggle 逻辑改变，双击普通角色不应清空其他已选角色。
- actual: `OnLButtonDblClk` 只判断 `HitTestMapPortrait(point) < 0`。这里的地图头像命中只是“非空白区域”保护，并没有把地图头像加入多选集合；普通角色命中时地图头像命中仍为负值，因此普通角色会被误判为空白并进入 `ClearCharacterSelection()`。
- classification: 当前多选快捷键实现引入的确认回归。
- minimal fix: 保持地图头像不进入多选，只把双击清空条件改为 `HitTestCharacter(point) < 0 && HitTestMapPortrait(point) < 0`。
- required test: 单击未选角色加入、单击已选角色移除、双击空白清空全部、双击普通角色不清空其他选择。

### `TEXT-WRAP-CAPACITY-001`

- severity: P2
- surface: 换行辅助/长文本输入
- evidence: `cczEditor2/LongCharData.h`、`cczEditor2/cczEditor2View.cpp:2576-2591`、`cczEditor2/TextWrapAssist.cpp:8`
- reproduction: 将换行辅助字节上限设为 4096，输入超过 2999 个 UTF-16 字符。
- expected: 自定义换行上限与实际全文可提交范围一致，或明确提示“每行字节上限”和“全文字符上限”是两个不同契约。
- actual: 换行辅助允许 4096，但底层长文本存储上限为 2999 个 UTF-16 字符；提交会安全拒绝并报错，不发生越界。
- classification: 既有存储契约被新 UI 暴露，属于契约缺口，不是内存安全问题。
- recommendation: 后续统一 UI 上限或增加全文长度提示，并补 2999/3000/4096 的提交测试。

### `DLL-DIFF-001`

- severity: P2
- surface: DLL 到内化核心的 legacy/raw replay API
- evidence: `docs/internalization/review-evidence/20260921-dll-diff-report.md`
- actual: 旧 raw/legacy 接口已移除；当前生产 bridge 未发现调用方，但旧兼容测试没有完全替代。
- classification: 兼容性/验证缺口，暂无生产路径回归证据。
- recommendation: 明确 API 已废弃；只有仍承诺外部调用兼容时才补适配层。

### `DLL-DIFF-002`

- severity: P2
- surface: `CczString.ini` 编码与资源加载
- evidence: `docs/internalization/review-evidence/20260921-first-round-report.md`、`docs/internalization/review-evidence/20260921-dll-diff-report.md`
- actual: 从 Win32 `MultiByteToWideChar(936, ...)` 改为 `.936` locale facet，存在目标环境依赖风险。
- classification: 当前没有实际回归复现，属于实现差异和环境验证缺口；按用户要求暂不改动。
- recommendation: 发布前用实际目录和实际 INI 做一次目标 Windows 验证。

### `WINDOW-SMOKE-001`

- severity: P2
- surface: 真实 MFC 窗口、按钮、焦点和通知顺序
- actual: 静态代码和 headless 测试显示按钮创建、消息映射、布局、启用状态存在，但未完成真实窗口 smoke。
- classification: 验证缺口，不是已确认产品错误。
- recommendation: 后续人工验证按钮、搜索输入、播放控件、悬浮条件树/变量控制和资源加载。

## 已确认无问题

独立审查认为以下项目已有源码和 headless 证据支持：

- 普通 Scene、出战界面、自由 R 第二 Scene 的核心分流；
- 前置推演、动态播放时间线、步进/步退的核心逻辑；
- `0x20` 人物状态、`&*.174` 头像解析、单指令多对话拆分；
- 地图头像 `0x29/0x2A/0x2B`；
- 武将出现/移动/消失/动作/朝向写回；
- 多选、主角色、组移动/点移动、对齐/等距、统一动作/朝向；
- 写回撤回事务基本校验；
- 道具 255 与人物编号范围区分；
- 输入筛选、提交、取消、ESC 恢复的静态生命周期；
- 换行辅助的 CP936 字节计算、颜色标签排除和模式保存；
- 当前生产版本无外置 `cczPreviewExt.dll` 依赖。

## 计划覆盖率判断

- Task 1：契约和源码覆盖基本完成，但正式契约矩阵仍需整理；
- Task 2：DLL/内化核心静态对照完成，legacy API 兼容性仍是 P2 缺口；
- Task 3：headless 回放/Section/动态预览覆盖完成；
- Task 4：写回、多选、撤回 headless 覆盖完成，真实窗口未验；
- Task 5：输入、文本、换行辅助源码和测试覆盖，全文长度契约仍需明确；
- Task 6：无外置 DLL、Release/staging 同步等静态门禁完成；
- Task 7：本次独立审查完成；
- Task 8：真实窗口人工验收未执行。

## 建议顺序

1. 先修复 `UI-MULTI-DOUBLECLICK-001` 并补四项交互回归测试；
2. 决定 `TEXT-WRAP-CAPACITY-001` 是统一上限还是增加全文上限提示；
3. 暂不改 `.936`、旧 DLL API 和真实窗口路径问题；
4. 最后进行真实窗口 smoke，再进入最终发布 Gate。

## 修复后复核

针对 `UI-MULTI-DOUBLECLICK-001` 已先加入失败断言，原实现确实失败；随后将双击清空条件改为同时要求 `HitTestCharacter(point) < 0 && HitTestMapPortrait(point) < 0`。

复核结果：

- `test_scene_multiselect_ui.ps1`：PASS；
- `test_scene_multiselect_writeback.ps1`：PASS；
- `test_preview_writeback_undo.ps1`：PASS；
- `test_dynamic_preview_regressions.ps1`：PASS；
- `test_dynamic_preview.ps1`：PASS；
- Release|x86 编译：0 警告、0 错误；
- Release/staging SHA-256 已重新同步；
- `test_targeted_review_contract.ps1`：PASS。

地图头像仍未加入多选集合；本修复只改变“双击空白”的命中判定。
