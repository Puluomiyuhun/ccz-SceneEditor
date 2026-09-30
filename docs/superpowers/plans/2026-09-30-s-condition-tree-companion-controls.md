# S 条件树配套控件实施计划

## 目标与边界

在 S 战场预览中补齐 R 预览条件树的变量控制和快速激活子事件。复用既有通用窗口和 cczPreviewCore 动作语义，但所有选项、变量值、激活目标和战场工作区仍由每份 S 文档自己的 BattlePreviewDocumentSession 管理。此计划不触及武将出场参数 UI；该部分只形成单独的设计文档。

## 实施步骤

1. 增加回归契约，覆盖面板/按钮存在、变量索引按完整文档建立、变量操作回到 S 条件重建链、激活按钮只针对固定 S Section 内当前选中且可激活的节点，以及展开/收起侧栏均有入口。
2. 在每个 S 文档会话保存变量索引、条件激活目标和三个按钮可见状态；加载新基线时扫描变量并同步默认值，清空/解绑时清除旧文档 UI 数据。
3. 添加复用 R 预览的变量控制窗与激活子事件按钮。变量操作先复制 ReplayOptions，再应用通用变量动作，成功后提交并复用条件刷新消息；激活也以副本执行 activate_condition_tree_target，成功后走相同刷新链。
4. 从 ExecutionNode 的 runtime_handle 映射选中 TreeView 节点。普通模式用新捕获的 selected_handle；快速模式在 Scene1 修订未变化的快捷返回路径使用树当前选中项。节点不在固定 Scene1 条件模型中时隐藏激活按钮。
5. 在现有 S 侧栏条件树行加入激活/变量入口；侧栏收起时把可用控件加入顶部紧凑工具栏，写回按钮继续单独突出。布局按当前可见按钮计算，不遮挡地图状态栏。

## 验证

- 先运行新增的 PowerShell 源码契约并确认它在实现前失败。
- 实现后运行新增契约、现有 test_battle_condition_tree.ps1、test_battle_quick_mode.ps1、test_preview_page_isolation.ps1、test_variable_control_internalization.ps1。
- 运行 PreviewCore Release x86 测试并编译 MFC Release 产物到按项目隔离的输出目录；检查构建日志和最终差异。
- 不能由本地测试证明真实 MFC 弹窗定位、现场变量操作体验或用户剧本行为；这些会列为待用户验收项。

## 执行记录（2026-09-30）

- 已在 `BattlePreviewDocumentSession` 内增加变量索引与激活目标；通过当前完整文档快照扫描变量，布尔/整型值继续保存在该剧本的 `ReplayOptions`。
- 已复用 R 的 `CVariableControlPane`、共享变量动作和 `activate_condition_tree_target`；两个操作都重建 S 条件模型并切换至对应战场工作区。
- 普通模式用捕获快照的当前 TreeView 句柄；快速模式在固定 Scene1 未变的快捷路径直接读当前树选择，不再次捕获 Scene1。选择不属于固定 Scene1 条件模型或不可达的节点时不显示激活入口。
- 已处理快速模式重建失败回滚：恢复原会话变量索引与激活目标，避免失败后串用新基线 UI 状态。
- 新增合同测试、条件树、快速模式、变量控制、页面隔离五组 PowerShell 测试均通过；页面隔离脚本由 PowerShell 7 运行，避免 Windows PowerShell 5.1 对 BOMless UTF-8 中文续行的解析问题。
- `cczPreviewCoreTests.exe` Release x86 运行通过，341/341。
- MFC Release x86 解决方案构建通过，产物写入按项目隔离的 `.tmp-s-condition-companions-20260930/isolated/{core,editor,tests}`。临时 `Directory.Build.props` 已清理。
- 构建中原有 3 个 `C4389` 测试断言被日志重复列成 6 条；将 `record_index` 期望值改为 `std::size_t` 后，最终构建中 `C4389=0`、`LNK4020=0`、错误数为 0。仍有旧文件中的 C4018/C4101/C4129/C4244/C4305/C4554/C4806，以及 `CMyTreeCtrl.cpp` 编码告警 C4828；本轮新增的 `BattlePreviewPage.cpp` 和 `BattlePreviewSession.h` 没有编译警告。
- 已将最终 `s.exe` 更新到完整 QA 环境 `cczEditor2/.tmp-s-battle/all-script-sessions/writeback-package-unrestricted-20260929-r1/bin/`，其中已有 `CczString.ini`、`CczCustom.ini`、`scripts/CczSceneEditor2.ini`、测试剧本和预览资源；旧版 exe 保存在同目录 `s-pre-condition-controls-20260930.exe`。未生成压缩包。
- 参数控制随后按独立计划实施：分支级出场默认值、已选单位参数编辑、地图预览切换、阵营格描边与头像参数悬浮卡均已接入。当前最新 CoreTests 为 345/345；完整测试副本见 `cczEditor2/S-Battle-Parameter-Test-20260930/`。实现细节记录在 `2026-09-30-s-battle-unit-parameter-controls.md`。
- 变量面板/快速激活与参数面板的真实 MFC 布局、剧本操作及保存重开仍待用户验收。
