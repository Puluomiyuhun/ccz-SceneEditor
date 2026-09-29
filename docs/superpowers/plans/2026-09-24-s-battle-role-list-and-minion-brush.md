# S 战场角色列表与“小兵笔刷”实施计划

> 执行依据：`docs/superpowers/specs/2026-09-23-s-battle-role-list-and-minion-brush-design.md`。只增量修改当前 Battle 工作，保留所有原有未提交更改，不触碰 R 预览路径。

**目标：** 在独立 S Battle 页接入 S `Data.e5` 角色目录、普罗式连续小兵笔刷、拖按盖章、右键删除及可撤回的内存布阵；当前阶段不操作 TreeView/EEX/磁盘。

**工作区约束：** 现有 checkout 在 `main` 且已有大量用户未提交文件。为保留这些依赖中的源码，实施在当前目录原位推进；不得 reset/checkout/清理既有输出或覆盖用户文件。所有新编辑都走增量补丁。

## Task 1：角色目录和小兵组规则的失败测试 ✅

**测试文件：** `tests/preview_integration/battle_character_catalog_tests.cpp`、`cczPreviewCoreTests/cczPreviewCoreTests.vcxproj`。

- 建立小型合成 `Data.e5` fixtures：396-byte header、32-byte records、8-byte CP936 name、arms at +26；名称宽度、分组与 arm_kind 默认值按普罗工具解析。
- 先断言当前不存在的解析/分组 API，运行目标测试确认失败。
- 测试 512/1024 记录边界、截断、无效/空名称回退标签、ASCII 数字规范化、连续组最小长度 3、非连续/键变化断组与 Data 排序。

## Task 2：实现独立 S `Data.e5` 目录 ✅

**生产文件：** `cczEditor2/PreviewIntegration/BattleCharacterCatalog.h/.cpp`。

- 增加纯值结果和解析 API；CP936 名称解码使用 Windows 936 code page，不接触 `m_pPer2` 或 R cache。
- 根据已确认的普罗 512/1024 大表分支和文件长度严格决定记录数；对所有偏移、长度先做边界检查。
- 产生角色条目与独立 Brush 条目；组键仅忽略 ASCII 数字，不修改显示原名；空名展示 `Data n`。
- 用真实本地 `Data.e5` 做一次只读样本核对：角色名/arms/总条数，不写入该文件。

## Task 3：接入笔刷选择、ID 分配、落笔/删除/撤回语义 ✅

**生产文件：** `PreviewIntegration/BattleEditorOps.h/.cpp`；**测试：** `battle_editor_ops_tests.cpp`。

- 把当前已占用 Data 号定义为当前 Battle 作用域内已解析且非 sentinel 的单位；每次计算候选时即时扫描，不维护会分叉的 used 缓存。
- 普通角色选择和 brush 选择共用落笔上下文；brush 下一个候选为组范围内最小未占用 Data 号。
- 拒绝重复角色 Data ID。放置失败不消耗 ID；成功后自动选出下一号；耗尽时无回绕且状态明确。
- 删除 fixed row 只清 sentinel，record_index 不动；可变行保持现有 `0x4B` erase 语义。同组未来放置复用最小空闲 Data ID，幸存者 ID 不变。
- 一次 Place/Delete 是一条 S undo 事务。Undo 后从恢复的 Battle 状态重算可用 ID。所有状态均仍只在内存。
- 覆盖 Data-ID 与 `record_index` 不混淆、0x46/0x47 固定槽使用、ID 复用/稳定、重复禁止、撤回后 ID 恢复。

## Task 4：页面侧栏和选择 UI ✅

**生产文件：** `BattlePreviewPage.h/.cpp`、编辑器 `.vcxproj`/`.filters`。

- 在 S 页创建独立侧栏：角色/笔刷页签、检索框、带滚动的列表、角色分页、来源指令选择和隐藏/恢复入口。
- 只用 `BattleAssetCache::data_e5()` 与独立 `BattleCharacterCatalog` 初始化目录；preview sprite 继续经 S `BattleAssetCache::ResolveUnitSprite` 绘制。
- 从当前 captured Section 枚举可写入的语义完整 0x46/0x47 来源；来源组合框只选 S 页面当前作用域，不允许跨 Section。不能证明来源时禁用落笔并解释原因。
- 侧栏隐藏/恢复不重建 `BattleEditorState`、不重置 zoom/scroll/query/tab/selection。
- 地图绘制不加角色名。放置 hover sprite 只显示可用 Data ID 的 S 形象；无可用号或上下文失效则清除幽灵图并显示状态。

## Task 5：左键按住盖章、右键删除和同步重绘 ✅

**生产文件：** `BattlePreviewPage.h/.cpp`；**静态合同：** 新增 `tests/test_battle_role_brush_ui.ps1`。

- 左键 down 在首个有效 cell 放置一次并 SetCapture；左键仍按住时仅在坐标变化且该格可放置时再放置。MouseUp/capture loss/destroy 均解除状态。
- 右键在地图逻辑格命中唯一单位时调用 S `DeleteAt`；多命中歧义/空格/非 map 区域不删除。
- Place/Delete/Undo 成功后立即刷新 S Battle canvas、列表占用状态和 ghost Data ID；不调用 R undo、View document setters 或 EEX mutations。
- 静态合同断言无 R role/cache/session 引用，无 TreeView/EEX write path，且属性刷子控件不存在。

## Task 6：写回门与验收 ✅（自动化完成；待真实窗口验收）

- 本任务只验证写回按钮/命令保持关闭：现有 `BattleWritebackEvidence` 的五项事实仍未完全闭合时，不添加产品级 document writeback。不得用测试构造的全 true evidence 替代实机/原生回读证据。
- 执行目录/ops/现有 Battle 回归、R 路由/隔离合同和 PreviewCore 测试；执行 Win32 Debug、Release 构建，Release 输出到新目录，不覆盖用户正在运行或已交付 exe。
- 记录真实样本与构建产物摘要；最终报告区分自动测试、静态检查和仍需用户真实窗口确认的鼠标操作/布局。

## 本轮实施结果

- S `Data.e5` 独立目录、角色搜索/分页、小兵笔刷、左键拖按盖章、右键删除、撤回、来源选择及侧栏隐藏/恢复已接入独立 S 页面；所有编辑仍是临时内存状态，文档写回入口未开放。
- 只读检查 `D:\AI曹操传项目群\main engine\Data.e5`：文件 61,379 字节；按已确认的 396 字节头、32 字节记录解析出 1024 条，全部 1024 条名称非空。Data 0 为“云筱”/arms 0；Data 358–371 为连续“羽林刀兵1–14”/arms 6，符合编号连续分组所需的样本结构。
- Debug/Release Win32 EXE 与 CoreTests 均输出到 `.tmp-s-battle\bin\`，未覆盖现有交付 EXE。CoreTests 两配置各 216 项通过；Battle 写回门定向测试 8 项通过；S 角色目录/页面接线、Battle 视口、资源和 R/S 隔离静态合同通过。
- MSBuild 有既存窄化转换警告，并因临时构建参数令项目引用共享 IntDir 出现 MSB8028；没有编译错误。需由用户在真实窗口确认控件布局、隐藏/恢复、拖动盖章、右键删除与撤回手感。
- 更新 `test_preview_page_isolation.ps1`：原断言误把 S 自有角色分页也当作 R UI 泄漏，现改为只禁止 R 角色表与其控件引用。
