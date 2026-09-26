# 自由 R 跨 Section 动态回放修复

## 现象

- 点击测试配对只覆盖当前/首个 Section，触发 Scene 后续 Section 无法找到配对。
- 部分剧本把布景 Section 也放在第二个（触发）Scene 内；其中先以一个点击测试 Section 构建场景，再由后续点击测试 Section 进入对白。此时不能只去前一个 Scene 找武将出现。
- 自由 R 触发 Section 被当成静态快照，0x31/0x32/0x33/0x34 不影响后续预览。
- 初始布景中“出现后消失”的武将，在后续快照中被错误复活。

## 根因

旧 resolver 只从当前 Section 向前扫描，并把匹配的武将出现固定限制在前一个 Scene；旧 compositor 只拼接武将出现 Section 与当前 Section；执行层的 `m_limitedFreeRReplay` 又过滤了清除/消失/移动/转向/动作指令。

## 修复契约

1. 在当前触发 Scene 的直接 Section 源顺序内扫描 0x2D，配对范围不越过相邻的自由 R 两个 Scene。
2. 优先在选中 Section 之前的当前触发 Scene 内寻找同角色 0x30；这类布景 Section 的完整前置计划只回放一次。找不到时才回退到前一个 Scene 的唯一匹配 0x30。
3. 触发 Scene 中选中 Section 之前的每个 Section 按条件树可达路径完整推演，再拼接当前 Section 的计划。
4. 初始布景及其后续动态指令按源顺序回放；触发路径中最后一个 0x27 之后的内容成为新场景，覆盖继承状态。
5. 不再启用旧的“有限快照”动态过滤；普通 Scene/出战界面仍走各自原有策略。

## 验证

- `cczPreviewCoreTests` Debug x86：全部通过。
- 新增“触发 Scene 内布景 Section 提供 0x30、后续 Section 以 0x2D 触发”回归用例并通过。
- 动态预览、原生计划应用、预览会话、刷新路由、目录/控件、完整 parity、无外置 DLL 运行时检查：全部通过。
- `cczEditor2` Release x86：0 错误；工程既有 38 条警告。
- 真实窗口验收见 `staging-test/ccz-editor-freer-dynamic-fix-20260920/TEST-PLAN.md`。
