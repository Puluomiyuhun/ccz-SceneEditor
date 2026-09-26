# 独立 R 场景与 S 战场预览页

## 目标与边界

将停靠的“场景预览”改为单一宿主内的两个独立子页：R 场景预览页与 S 战场预览页。宿主根据已打开剧本文件名的前缀自动选择页面：`R_` / `R-` 进入 R 页，`S_` / `S-` 进入 S 页。未识别前缀时显示中性空状态，不根据指令编号或内容猜测页面。

S 页迁移现有已验证的战场地图 JPG、战场单位解析、`Unit_mov.e5` 形象与方向映射、全单位同屏、地图滚动条、中键拖动及缩放视口。S 页拥有自己的输入捕获、解析结果、资源缓存、窗口状态与绘制路径，不加载或调用 R 的 `PreviewAssetCache`、`PreviewSession`、对话目录、`CczString.ini` 指令目录、角色名表或动态播放状态。

明确不迁移/不复用 R 预览的角色表与角色列表。S 页不得通过 `m_pPer2`、`InitCharList` 或 `PopulateBattleCharacterCatalog` 获取出场角色。角色查找、选择与“武将出现”来源留待后续单独设计；本次不显示依赖该角色表的角色页、搜索和放置入口。现有场上单位只读呈现，不承诺新增角色放置或剧本写回。

## 架构

- `CScenePreviewWnd` 继续作为 MainFrame 中唯一的 `CDockablePane` 宿主，保持现有停靠 ID、标题和布局入口。
- `CRScenePreviewPage` 是宿主内的 R 子窗口，承接既有 R 预览与 R 专属操作；只接收 R 前缀文档。
- `CSBattlePreviewPage` 是宿主内的 S 子窗口，独立持有 S 快照、Battle parser/state、`BattleAssetCache`、地图/单位位图缓存、视口与鼠标交互；只接收 S 前缀文档。
- 宿主只做前缀路由、子页显隐/尺寸、对现有 View 接口的兼容转发。`InitCharList` 只转给 R 子页。
- 两页可以接收同一个 MFC TreeView 的有界、值拥有的通用树快照，但 Battle adapter 必须从该快照直接构造 S 输入；不得先构建 R `EditorPreviewInput` 或访问 R `PreviewSession`。
- S 页使用战场自身的 `BattleAssetCache` 与 JPG/Unit_mov 资源；R 页使用现有 R 资源管线。任一页刷新或失败不得清除/绘制另一页的状态。
- S 全单位同屏，不使用原本的翻页命令。视口范围裁剪仅影响可见像素，不分页或丢弃单位。

## 兼容与安全

- 保留 View 现有 `RequestPreviewRefresh`、`InvalidatePendingPreviewRefresh`、`InitCharList` 调用接口，避免把页面拆分扩散到文档树逻辑。
- R/S 路由以文件名前缀为唯一依据。S 前缀即使 Battle 标记缺失也仍留在 S 页显示 S 专属解析诊断，绝不回退到 R；未知前缀显示中性状态。
- 本次只读迁移不增加新的 EEX 写入路径，不复用 R 的撤回栈、条件树、变量控制或播放进度。
- 保留已有工作区未提交变更；不清理构建产物、不覆盖 `Release-next` 或用户脚本。

## 验收

1. 结构测试确认宿主、R 页、S 页是不同窗口类，宿主按前缀切页。
2. 结构测试确认 S 页及 Battle adapter 不依赖 R 角色表、R 资源目录、R session / catalog / 对话状态。
3. Battle adapter 从通用捕获快照构造 Battle 输入，不经过 `BuildEditorPreviewInput`。
4. S 页将解析出来的全部单位绘于同一地图页面；分页控件/角色表入口不存在。
5. S 的 JPG/Unit_mov、方向映射、缩放和滚动行为保留；R 页继续运行现有 R 预览用例。
6. 运行现有相关 PowerShell 合同测试及 PreviewCore / 编辑器 Win32 构建。
7. 实机验收需用户在真实窗口分别打开 R、S 与未知前缀文档，验证自动切换、无跨页诊断污染、滚动缩放、方向与全单位显示。未执行实机测试前不宣称视觉验收通过。
