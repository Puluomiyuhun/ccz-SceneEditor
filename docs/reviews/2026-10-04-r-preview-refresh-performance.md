# R 预览刷新性能修复（2026-10-04）

## 目标与边界

用户反馈：修复激活子事件不刷新后 R 预览变卡。仅处理 R 完整推演的句柄解析性能，保留现有工作区的 S 动态预览代码、条件语义、写回门禁与刷新版本检查。
工程检查：systematic-debugging / code-quality-workflow；A0 路由 code-quality-workflow，ST-A0，snapshot 2026-08-18。目标、改动边界与验收为本报告中的限制及验证项；未采用无关库候选。

## 调查结果

修复后的 RequestConditionActionRefresh 从活动文档取得树与当前指令，直接发起一次刷新。未发现这处修复新增循环或双重刷新；重新解析活动文档本身只涉及有限次窗口查询。
完整推演的 ApplyNativeReplayPlan 与 ApplyFreeRPrefixOnly 原来对每条计划指令调用 FindCurrentTreeItem；该函数从根枚举当前树。N 个树节点、K 条重放指令会产生最坏 O(N*K) 次访问。恢复原本丢失的刷新，会进入这一高开销路径。
已确认重复扫描是实际瓶颈，但没有用用户的运行中窗口做全链路分析，因此不能将其表述为全部卡顿的唯一原因。

## 修改

新增 CurrentTreeItemIndex：仅存活于一次同步推演事务，一次枚举当前树建立句柄集合。两处批量推演改为索引查找；仍验证 TreeView_GetItem、指令编号，并在全部校验成功后才清除并应用场景。
索引不跨消息或树重建保留。单项播放/撤回的旧查找保持原行为。没有回滚激活子事件修复，没有更改条件模型或新增写入规则。
复杂度调整为 O(N+K)；源码新增一个 Windows 控件辅助头，不增加运行时依赖。

## 验证

真实 Win32 TreeView（隐藏窗口，6,000 节点、200 个目标，Release /O2）：旧源码原样提取的查找耗时 385.603 ms；新索引含构建耗时 1.9826 ms。两次为单次测试，只证明句柄解析阶段差异，不代表全预览加速比例。
新增可执行测试涵盖嵌套节点、多根节点、外部树句柄、空句柄、删除节点、树清空后重新建索引及窗口销毁。CURRENT_TREE_ITEM_INDEX_PASS / R_REPLAY_TREE_LOOKUP_PASS。
重新执行 test_dynamic_preview_regressions.ps1，通过播放器、刷新路由、会话和原生应用回归，包括 10 项原生指令应用合同测试。
主程序 Release Win32 构建成功，退出码 0。git diff --check 无空白错误。
仍待真实编辑器中的原 R 场景验收：进入不可达子事件，激活后立即刷新；重复切换深层指令；切换 R/S 和多文档。未宣称完成真实窗口性能验收。

## 交付

桌面目录：C:\Users\LanceYuki\OneDrive\Desktop\cczEditor2_R预览性能修复_20261004
仅 s.exe 与 CczString.ini、CczCustom.ini、CczSceneEditor2.ini，四个文件。
本轮未提交 Git，未关闭电脑。
