# 战场布置器移植 Task 0 基线

日期：2026-09-21

## 范围

本阶段只固定样本、哈希和模式契约，不修改 S/R 剧本，不修改现有预览源码、EXE、DLL 或 F 盘测试副本。

## 只读样本

样本及 SHA-256 记录见 `tests/fixtures/battle_preview/manifest.json`。主引擎样本为 `main engine/RS/S_00.eex`、`S_13.eex`、`S_15.eex`；参考引擎样本为对应的 `reference engine/RS` 文件。R_00/R_13/R_15 作为模式路由回归样本一并固定。

## 当前 Release 基线

- `workspace/references/ccz-SceneEditor/Release/s.exe`
- Size: 5,205,504 bytes
- SHA-256: `14519FE626023BA762DE4DA6543D1AD0D61D0852730125D98661AA3060A4A39D`

## 已执行的只读验证

- `tests/test_preview_core_project.ps1`：PASS
- `tests/test_preview_full_parity.ps1`：PASS，报告显示 97 个源码文件通过契约扫描
- `tests/test_dynamic_preview_regressions.ps1`：运行期间核心、动态回放、刷新状态机、路由和边界测试均输出 PASS；MSVC 链接器仅报告既有 LNK4075 警告
- `tests/test_preview_session.ps1`：核心 PreviewSession 用例均输出 PASS

## Task 0 契约

- S_00/S_13/S_15 只能先判定为战场候选；确认 0x44/0x46/0x47/0x4B 战场证据后才能进入 Battle 模式。
- R_00/R_13/R_15 不得因为出现相同指令编号而切换到 Battle。
- 当前战场模式的解析范围必须是当前战场作用域，不能跨 Scene、R Scene 或其他战斗文件补全单位。
- `tests/test_battle_preview_port.ps1` 当前应在缺少 classifier/adapter 时失败；Task 1 完成后才允许变绿。

## 限制

报告已经确认战场模块与 R 模块独立，但 0x46/0x47/0x4B 完整字段表、分组/页码语义和原生重载结果仍待验证。Task 0 不开放写回。

## Task 1 结果（2026-09-21）

- 新增 `cczPreviewCore/include/ccz/preview/battle_mode_classifier.h` 与
  `cczPreviewCore/src/battle_mode_classifier.cpp`。
- 新增 `cczEditor2/PreviewIntegration/BattlePreviewAdapter.h/.cpp`，从现有
  `CapturedEditorTree` 生成完整文档的值所有 Battle 输入；不会替换 Section
  专用适配器，也不会把数据送进 R 的 `SceneState`。
- `S_`/`S-` 仅作为战场候选；递归发现 0x44/0x46/0x47/0x4B 之一后才判定
  `Battle`。R 文档即使出现这些指令号仍保持 `Ordinary`；缺少前缀或标记时返回
  `Unresolved` 和诊断。
- `CScenePreviewWnd` 已增加只读路由点：识别到 Battle 或不确定的战场候选时，
  清理旧 R 状态并显示“战场预览暂不可用/诊断”，当前不会解释 0x46/0x47/0x4B，
  也不会写回剧本。
- 新增 classifier/adapter 单元测试，并纳入两个 Visual Studio 工程。

验证结果：

- `cczPreviewCoreTests.exe battle_mode`：5 项通过。
- `cczPreviewCoreTests.exe battle_adapter`：2 项通过。
- `cczPreviewCoreTests.exe` 全量：现有测试与新测试全部通过。
- `tests/test_battle_preview_port.ps1`、`test_preview_core_project.ps1`、
  `test_preview_full_parity.ps1`：通过。
- Debug Win32 `cczEditor2.vcxproj`：构建成功；仅保留工程原有的 24 个警告，
  无 Task 1 新增错误。

Task 2 仍必须补上 Battle 作用域限制、BattleCoord、地图/地形/单位字段解析；
在此之前 Battle 页面保持只读不可用，不进入编辑和写回。

## Task 2 结果（2026-09-21）

- 新增 `cczPreviewCore/include/ccz/preview/battle_preview.h` 与
  `src/battle_preview.cpp`：定义独立 `BattleCoord`、地图/单位/状态对象，保留
  正负坐标的 0.5 阈值、`(offset.x, -offset.y)` 偏移、地形拒绝值和地图边界校验。
- 新增 `cczPreviewCore/include/ccz/preview/battle_instruction_parser.h` 与
  `src/battle_instruction_parser.cpp`：只在显式 Section 作用域内读取 0x27、0x44、
  0x46、0x47、0x4B；0x44 不读取伪造地图字段，0x27 只确认战场模式 3 和地图字段，
  三类单位记录按已确认的 11/12/5 步长保留顺序、重复项和原始字段。
- `BattlePreviewInput` 现在携带值所有文档快照中的 `scope_key`；由当前 Section
  的 runtime handle 反查逻辑键前缀，避免把完整文档快照误当成全局战场状态。
- 0x46/0x47/0x4B 字段的完整语义仍未确认，解析结果显式返回
  `field_table_complete=false`，每个候选单位 `fields_decoded=false` 并附诊断；这
  允许只读层展示原始记录摘要，但不会开放编辑或写回。
- 新增 `tests/fixtures/battle_preview/sample_summaries.json`，固定 S_00/S_13/S_15
  的场景/Section 范围、0x27/0x44/0x46/0x47/0x4B 数量、记录步长和字段 0 候选计数；
  摘要与样本 SHA-256 绑定，不改写原始 eex。

验证结果：

- `cczPreviewCoreTests.exe` 全量：通过；新增 battle mode/adapter/coordinate/parser/state
  测试全部通过。
- `tests/test_battle_preview_port.ps1`：通过，包含样本摘要哈希、作用域和源文件契约。
- `test_preview_core_project.ps1`、`test_preview_full_parity.ps1`、
  `test_dynamic_preview_regressions.ps1`、`test_preview_session.ps1`：通过；仅有既有
  MSVC LNK4075 警告，无新增错误。

Task 2 阶段限制：当时 Battle 页面尚未接入地图/形象/Hexzmap 资源缓存；未知字段
不会被绘制成已解码单位，后续 Task 3 只允许先接入资源和只读画布。

## Task 3 首轮结果（2026-09-21）

- 新增 `cczEditor2/PreviewIntegration/BattleAssetCache.h/.cpp`，按当前剧本的
  Mod 根目录独立加载 `Data.e5`、根目录 `Hexzmap.e5`，并将 S 战场地图解析为
  `map\\m###.jpg` 的松散 JPG 路径；不会扫描其他 Mod，也不会通过 R 场景缓存
  补全战场资源。
- `CScenePreviewWnd` 已接入 Battle 只读解析和资源加载分支。可读地图归档时复用
  现有 GDI+ 图像基础设施直接解码地图 JPG，并在画布上显示地图编号、原始单位
  记录数和“字段未解码，不可编辑”提示；Battle 状态不会写入 R 的 `SceneState`。
- 新增 `tests/test_battle_asset_cache.ps1`，验证资源路径、缓存分支、未知字段只读
  契约和工程接入。

随后按主引擎实际目录复核修正了资源语义：`Hexzmap.e5` 位于 Mod 根目录，S
战场背景来自 `map` 文件夹中的 `m###.jpg`，不是 E5 中的地图记录；缓存现已按
该结构读取，未添加未知目录扫描或跨 Mod 兜底。R 场景的 `E5\\Mmap.e5` 仍由其
原有 `LoadMapImage` 路径独立使用，不与 S 战场地图缓存混用。

验证结果：

- `tests/test_battle_asset_cache.ps1`：通过。
- `tests/test_preview_full_parity.ps1`：通过，当前扫描 107 个源码文件。
- `tests/test_dynamic_preview_regressions.ps1`、`tests/test_preview_session.ps1`、
  `tests/test_preview_core_project.ps1`：通过；仅保留既有 MSVC LNK4075/工程警告。
- Debug Win32 `cczEditor2.vcxproj`：构建成功，0 个新增错误。

当前 Task 3 尚未完成单位形象/地形网格绘制与样本截图对照；因为 0x46/0x47/0x4B
字段表仍未闭合，暂不把原始记录渲染成具体单位，也不开放交互或写回。

## 证据修订（2026-09-21）

上一节“字段表仍未闭合”描述的是 Task 2 当时的实现状态，不应继续解释为“本地没有字段来源”。重新核对本地工程后，确认：

- `D:\AI曹操传项目群\workspace\references\ccz-SceneEditor\cczEditor2` 包含编辑器 C++ 源码、工程文件和资源定义；`cczEditor2View.h/.cpp`、`cczEditor2Doc.cpp` 已给出 0x46/0x47/0x4B 的原始字段类型、重复步长、记录容量、读写方式和对话框控件映射。
- 0x46 为 11 字段、20 条记录；0x47 为 12 字段、80 条记录；0x4B 为 5 字段，`Dialog_75` 的资源标签明确为“编号、x、y、朝向、隐藏”。
- `D:\普罗工具v0.3\普罗工具v0.3_Data\Managed\Assembly-CSharp.dll` 是普罗工具的 IL/元数据来源。普罗工具发行包没有随附 C# 文本源码或 PDB，但这不构成业务逻辑缺失。

因此后续应把 0x46/0x47/0x4B 的基础字段解析从“待寻找来源”调整为“按本地编辑器源码固化规范”，再用普罗工具 DLL、S_00/S_13/S_15 和原生重载结果验证写出顺序、分组、空槽和页码语义。Task 2A 之前的只读限制仍然有效，但不再因为不存在源码而阻塞字段规范化。

## Task 5 前置静态取证（2026-09-21，仍未开放写回）

本轮只读核对了 cczEditor2 源码、普罗工具 `Assembly-CSharp.dll` 的 IL，以及主引擎 S 样本；没有修改剧本、EXE、DLL 或 F 盘副本。

### 已闭合的格式规则

- `cczEditor2Doc.cpp::int2ch` 与 `cczEditor2View.cpp::ch2int` 均按小端序读写；0x04 字段为 4 字节，其余普通字段为 2 字节。
- DLL 的 `Piece.kind` 分组与记录号已经从 `MyButton2.onPress(kind==300)` 的 IL 闭合：`kind==1 → 0x46`、`kind==2 → 0x47`、`kind==0 → 0x4B`。三组列表在写出前都会 `Reverse()`；其中 kind 0 / 0x4B 的反转是在 0x47 序列化完成后、0x4B 写出前执行。此结论已由 2026-09-25 对应 IL 位置复核，替代此前“kind 0 不反转”的记录。
- 0x46 每页固定 20 行，0x47 每页固定 80 行；DLL 在不足容量时写出空槽，而不是截短页面。空槽的已确认规范化值为：DataId/目标 DataId = -1，坐标 = 0，朝向 = -1，兵种类型 = 2，AI/目标标志 = 1/0（其余按字段类型写零）。0x4B 是逐单位记录，不使用 20/80 固定分页。
- DLL 的字段写出槽位顺序与编辑器字段表一致：0x46 为 DataId、隐藏、X、Y、朝向、等级、兵种类型、AI、目标 DataId、目标 X、目标 Y；0x47 在此基础上插入援军标志；0x4B 为部署编号、X、Y、朝向、隐藏。0x4B 的部署编号由 kind 0 分支的局部计数器从 0 递增写入，并非 `Piece.data_id`。方向写出使用 `3-dir`，等级按 DLL 的负等级编码分支写出。

### 样本交叉结果

按编辑器实际 `EditData` 规则重新读取 `S_00/S_13/S_15`，三份样本均出现固定页长度：S_00 为 0x46×20、0x47×80；S_13 为 0x46×20、0x47×80×2；S_15 为 0x46×20、0x47×80。每份样本的末行均呈现上述空槽值；0x4B 分别为 1、5、12 条独立记录。0x46/0x47 首行 DataId、坐标、方向、隐藏等值可由 cczEditor 字段类型直接解码；0x4B 首字段按“部署编号”解释。普罗导出 IL 证明其序号来源是 kind 0 局部计数器，但副本保存重载仍需验证编号/节点顺序。

### 尚未闭合的两个硬门槛

1. 还没有把一份副本通过新的写回适配器保存，再用当前 cczEditor 和原生/参考引擎重新打开，验证单位顺序、页面、隐藏、等级、兵种和目标完全一致。
2. 还没有证明 DLL 的 `kind==300` 输出对当前编辑器/引擎是可接受的最小记录格式，而不仅是普罗工具自身的保存缓存格式。

因此 Task 5 当前仍停在“写回前置证据”阶段；下一步可以先写拒绝未闭合契约的失败测试和纯值构建器，但在上述两个门槛完成前，产品 UI 继续保持不可写回。

## Task 5 纯值构建器（2026-09-21，仍未接入产品写回）

- 新增 `PreviewIntegration/BattleWritebackAdapter`。它只从
  `BattlePreviewState` 生成可审计的临时记录和小端字节向量；不接触 TreeView、
  文档缓冲区或 EEX 文件。字段值一律从原始字段数组起步，只覆盖已由
  `BattleFieldSpec` 明确命名的角色，故未修改字段不会被零填充。
- 0x46/0x47 必须各自拥有完整的 20/80 槽、连续的记录索引和完整原始字段，
  否则构建失败；0x4B 保持来源记录顺序并按可变长度构建。构建器不把普罗界面的
  `3-dir` 显示换算误写成编辑器字段值。
- `BattleWritebackEvidence` 将字段表、样本/组顺序、native 重载、kind 300
  外部接受性分成独立门禁。随附的 `writeback-manifest.json` 显式把后两项保留为
  `false`，所以当前调用必定拒绝实际写回；即使测试中传入完整假设证据，返回值也
  仅标记为“临时字节已构建”，不会开放产品命令。
- 新增内存事务快照，覆盖战场状态、文档修改标记和选择状态；rollback 可原子恢复
  这三项，但目前只服务于测试和未来的文档桥，不能声称具有 EEX 撤回能力。

验证：`tests/test_battle_preview_writeback.ps1`、
`cczPreviewCoreTests.exe battle_writeback` 通过。真实副本保存、编辑器重开和
原生/参考引擎重开仍是后续实机验证的唯一开放条件。

## Task 6 原版战场布置器基底（2026-09-21）

Task 6 的实现单元是 `cczEditor2/PreviewIntegration/BattleEditorOps`，只覆盖
S 战场原版基底，不移植 R 场景的多选、主角色、对齐、等距、组移动或批量写回。
该层是纯数据操作：角色页码/检索、当前战场全单位视图、逻辑格放置预览、放置/修改/删除、
滚轮朝向和方向锁、地形/边界/当前战场占用限制、视口边界，以及外观/隐藏/朝向/等级/
类型/目标六种 painter。每次数据变更都经过 Task 5 的
`BattleWritebackTransaction` 内存快照和撤回入口；不调用 EEX、TreeView 或 DLL
写回。外观字段没有独立的已证实 `BattleFieldRole`，因此 painter 只修改临时
显示值，不标脏原始字段，避免猜测写回字段。

固定容量的 0x46/0x47 组按已确认的空槽 sentinel 工作：真实完整 20/80 rows
放置选择第一个空槽并保留 vector 长度、`record_index`、来源组 `page` 和 raw framing；
删除只清空原 slot，不 erase/重排，Task 5 value transaction 可撤回同一 slot 的
raw/semantic 状态。0x4B 仍维持单来源、不可合并规则。空槽 sentinel 只来自本文件
前述 cczEditor 字段证据，不由 UI 猜测。

`ScenePreviewWnd` 只接入安全桥：Battle 解析快照创建 `BattleEditorOps`，界面提供
“战场重置”临时命令；摘要命中、已证明逻辑格命中和滚轮朝向都路由到 Ops。0x47/0x4B
来源组的 `page` 仅用于解析和写回排序，不参与显示分页；战场画布一次显示当前 Battle
作用域的全部有效单位，角色列表页码保持独立。右键在 Battle 模式独立早退，不进入 R
context menu。像素投影和 Hexzmap
地形证据仍未闭合时，UI 不把未知屏幕坐标转换成逻辑格，明确安全拒绝，也不开放产品
写回。

新增 `tests/preview_integration/battle_editor_ops_tests.cpp` 覆盖上述纯数据契约，
并新增 `tests/test_battle_preview_full_regression.ps1` 作为 Debug/Release Win32
构建、focused/full CoreTests 与既有战场回归的统一入口。Task 6 的实现报告位于
`.superpowers/sdd/2026-09-21-battle-preview-port/task-6-implementation-report.md`。

当前结论：Task 6 纯数据基底和安全桥接已完成；Task 5 的 native reload、kind-300
外部接受性以及 S 实机窗口/原生重载验证仍未完成，产品写回保持关闭，不能据此宣称
战场产品已完成。

## 地形加载闭环修复（2026-09-24）

S 页面此前只用 `map\\m###.jpg` 建立投影尺寸，没有把 Mod 根目录的
`Hexzmap.e5` 解码到 `BattleState::terrain`。放置 Ops 对没有完整地形网格的状态
按设计拒绝操作，因此角色列表虽可选，画布放置会被“地图/地形未闭合”门禁拦截。

新增 `cczPreviewCore` 的 `decode_battle_hexzmap`：从 `0x110` 开始读取 12 字节
索引项，以 32 位大端读取资源长度和偏移；资源头两字节是像素网格尺寸，按每格
3 单位换算并要求与 JPG 推导的逻辑尺寸完全匹配，随后读取 Y 优先、行优先的地形
字节。偏移使用完整 32 位，覆盖大于 64 KiB 的 Mod 资源；任何索引、长度或尺寸
不匹配都会失败并清空地形，不以“全可通行”兜底。`CSBattlePreviewPage` 在建立
`BattleEditorOps` 前加载 JPG 并解码对应地图地形，失败仍保留安全拒绝。

主引擎只读样本闭环：`Hexzmap.e5` 地图 10 资源偏移 7922、长度 962、资源尺寸
120×72，得到 40×24=960 格；配套 `map\\m010.jpg` 为 1920×1152，即 40×24 个
48 像素逻辑格。新增合成 E5 解码/截断拒绝测试与 Ops 放置集成测试。Debug/Release
Win32 编辑器和 CoreTests 构建、全量 CoreTests、`test_battle_asset_cache.ps1`、
`test_battle_preview_full_regression.ps1 -SkipExistingRegression` 均通过。真实窗口
中的鼠标放置仍需用户在 S 剧本上验证；没有修改剧本或打开写回。
