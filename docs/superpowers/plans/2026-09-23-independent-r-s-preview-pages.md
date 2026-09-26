# 独立 R/S 预览页迁移实施计划

> **给 Codex：** 依据 `docs/superpowers/specs/2026-09-23-independent-r-s-preview-pages.md` 执行，按任务顺序推进；保持用户当前未提交变更，不做无关重构。

**目标：** 把当前单类混合的 R/S 预览变成同一停靠宿主中的独立 R 场景页和 S 战场页。S 页保留现有成熟地图/单位预览和视口能力，完全不使用 R 预览角色表。

**架构：** `CScenePreviewWnd` 做宿主和严格前缀路由；`CRScenePreviewPage` 承接 R 预览；`CSBattlePreviewPage` 独立解析并呈现 S。两页只共享有界值拥有的编辑器树快照，不共享语义会话、资源目录、角色表、状态或绘制入口。S 当前迁移为只读预览，角色出场选择后续单独设计。

**技术栈：** MFC / Win32 C++17、GDI+、现有 `cczPreviewCore`、PowerShell 源码合同测试、MSBuild Win32。

**全局约束：**

- 不得执行 `git reset`、清理/覆盖 Release 目录或回滚任何既有工作区改动。
- 不得让 S 调用 R `PreviewSession`、`PreviewAssetCache`、`CczString.ini` 目录、R 动态播放、R 角色列表或 `m_pPer2`。
- 不得改动剧本写回契约；这轮 S 页明确只读，不承接未完成的战场角色来源功能。
- R 前缀才进入 R，S 前缀才进入 S，其他文档进入中性空页；不能依据 opcode 猜测模式。
- 先增加失败的架构合同测试，再修改生产代码。
- 保持 `CMainFrame` 中一个停靠宿主和 `CcczEditor2View` 现有调用接口。
- 每项完成后跑聚焦测试；结束前跑相关回归和 Debug/Release Win32 构建。

### Task 1：记录路由与隔离合同

**文件：** `tests/test_preview_page_isolation.ps1`、项目测试入口/计划文档。

- 增加独立架构合同，断言宿主、R 页、S 页分型，严格前缀路由，未知模式中性处理。
- 断言 S 页和 Battle adapter 不引用 `m_pPer2`、角色列表入口、R 资源缓存、R session/catalog/对话逻辑。
- 断言 Battle adapter 直接从通用树快照构造 Battle 输入，不调用 R adapter。
- 先运行测试并确认因当前结构缺失而失败。

### Task 2：建立页面宿主并保留 R 行为

**文件：** `cczEditor2/ScenePreviewWnd.h/.cpp`、新增 `ScenePreviewHost.h/.cpp` 或等价拆分、`MainFrm.h/.cpp`、项目/filters。

- 将现有场景预览实现迁为 R 子页，使用普通子窗口基类；保留 R 绘制、条件树、变量控制、对话与播放器。
- 新建停靠宿主 `CScenePreviewWnd`，仅管理两子页、前缀路由、尺寸和 View 接口转发。
- `InitCharList` 仅转给 R 页；不得被 S 页接收。
- 未知前缀清空/显示中性页，不让旧帧或旧诊断残留。
- 加入前缀判别的单元或合同测试，并保持既有窗口 ID 不变。

### Task 3：Battle adapter 脱离 R 语义适配器

**文件：** `PreviewIntegration/BattlePreviewAdapter.h/.cpp`、相关 core 测试。

- 直接遍历 `CapturedEditorTree::document_roots` 生成 `RawReplayNode`；必要时抽取无 MFC 的公共节点转换工具。
- 在 battle adapter 内生成稳定 document/scope key、修订摘要与诊断，不生成 R `EditorPreviewInput`。
- 以 S 前缀决定路由；标记校验失败仍保持 S 页面和 S 诊断。
- 增加字段、层级、scope 和缺失边界测试；确认现有 Battle parser 输出不变。

### Task 4：迁移独立 S 战场页面

**文件：** 新增 `BattlePreviewPage.h/.cpp`、Battle 页面合同测试、项目/filters。

- 独立持有 Battle snapshot、资源缓存、JPG、Unit_mov 精灵缓存、画布、缩放/滚动偏移和中键拖拽状态。
- 复用当前已验证的方向映射、48px 投影、所有已解析单位同屏呈现与视口滚动/缩放；不得显示角色名或分页。
- 只绘制 Battle parser 解码完整的单位；未解码字段只在 S 页诊断中报告。
- 不创建 R 搜索框/角色表，不出现角色页翻页、角色放置入口；不得复制 R 角色表到 `BattleEditorState`。
- 故障清理只清 S 自己的页面和资源，不能触碰 R 页面或 R 诊断。

### Task 5：拆除跨模式执行路径

**文件：** R 子页、宿主路由、Battle 页面。

- 将 `ExecutePreviewRefresh` 的分类与输入捕获移到宿主，再把请求交给恰当子页。
- R 页删除 Battle 解析/缓存/绘制分支；S 页不加载 R 资产或创建 R catalog。
- 删除 `PopulateBattleCharacterCatalog`、`BindBattlePlacementContext` 对 `m_pPer2` 的桥接；保留独立 Battle parsing/viewport 数据。
- 关闭/清理/失败/未知前缀等路径分别验证，确保切换页面不交叉改变状态。

### Task 6：回归验证与交付

**文件：** 更新的测试合同、必要的实施报告。

- 运行新隔离测试、Battle asset/cache/parser 测试、R preview routing/dynamic playback、场景撤回/写回相关回归。
- 运行 PreviewCore 与编辑器 Debug/Release Win32 构建；确认输出为新可执行文件，不把它拷到旧 Release/用户脚本目录。
- 静态检查 S 文件依赖图中不存在 R 表/缓存/session；确认 `CScenePreviewWnd` 是唯一 dock pane。
- 报告构建产物位置、测试结果、未完成的 S 角色来源设计，以及需要用户实际窗口验证的项目。
