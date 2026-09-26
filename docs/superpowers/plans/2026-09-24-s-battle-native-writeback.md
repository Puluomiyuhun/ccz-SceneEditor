# S 战场原生写回实施计划

> **For agentic workers:** Execute this plan task-by-task while preserving all pre-existing uncommitted work.

**Goal:** Turn S battle placement-cache changes into a validated, reversible native document transaction without routing through R preview or writing EEX files directly.

**Architecture:** Keep the value planner separate from the MFC TreeView transaction bridge. The planner compares a fresh Section snapshot against immutable cache baselines and emits typed deltas; the bridge applies the complete delta atomically and owns native dirty/selection restoration.

**Tech Stack:** C++17, cczPreviewCore value types, MFC TreeView, Win32 Debug/Release tests.

**Spec:** `docs/superpowers/specs/2026-09-24-s-battle-unified-placement-writeback-design.md`

## Global Constraints

- Scope is one S Battle Section; R preview, R role data, R undo and direct disk EEX writes are forbidden.
- Cache identity, immutable source anchor and target deployment category remain separate.
- Any stale source, unknown-field loss, missing insert anchor or partial parse rejects the complete plan.
- Product writeback remains disabled until native reload and external `kind 300` acceptance are evidenced.

---

> 执行依据：`docs/superpowers/specs/2026-09-24-s-battle-unified-placement-writeback-design.md`。只操作独立 S Battle 页；保留当前工作区所有未提交内容。

## 当前边界

现有 `BattleEditorOps` 已在统一缓存中实现放置、删除、位置调整与缓存撤回。新增单位没有虚构来源；既有单位的来源指令、记录行、部署类别及导入字段基线分别保存。下一阶段只把缓存状态编译成可审计的 Section 变更计划，再以一次原生文档事务应用。

不得使用 `CcczEditor2Doc::AddInstruction` 或 JSON 批量接口写 0x46/0x47：它们把参数限制为 20 项；不得直接写磁盘 EEX。0x46/0x47 的 `InitData` 向量长度为 1000，而有效区分别是 20×11 和 80×12 字段；0x4B 为默认 20 项向量中的单个 5 字段记录。S 原生视图插入接口能按目标向量完整长度复制，但会选择新节点并设置 MFC dirty；文档另有自定义 `modified` 标志。提交/回滚必须一起处理树节点、两个 dirty 状态和选择状态。

## 阶段 A：值所有权变更计划（已完成）

- [x] 定义完整 Section 快照到 `BattlePreviewState` 的无 MFC 值输入；保留来源 `int_data`、逻辑键、节点顺序与类型。
- [x] 先写失败测试并实现：来源陈旧、字段/容量异常、0x4B framing、0x46/0x47 空槽填充、新增类别缺失、固定页满后新建下一页、删除、已有位置变化、未修改未知字段保留。
- [x] 计划器以 `cache_id` 找缓存实体、以不可变来源锚点定位旧记录、以独立部署类别路由新记录；不把来源指令当作目标类别。
- [x] 新单位优先填 sentinel 空行；无空行或无类别节点时生成完整默认向量待插入节点值对象。0x4B 每单位一个节点；计划器不触碰原树。
- [x] 小兵笔刷每次重新选择分配独立缓存批次 ID；批次起始号是本批实际首个 Data ID，而非笔刷范围下界。删除只压紧本批；计划器验证连续性，不重排导入单位。
- [x] 来源节点 ordinal 必须非负且唯一；新增节点的来源 ordinal 明确为 `-1`；插入父/前置锚点必须限定在当前 Section。
- [x] `BuildBattleWritebackPlan` 只生成值计划并始终返回 `product_writeback_allowed=false`，没有接入原生 TreeView。

## 阶段 B：S 原生树事务桥（内部实现完成）

- [x] S 页保留生成布阵缓存时的完整文档/Section 基线；值适配器可将基线与新鲜捕获配对，映射本 Section 的 0x4B/0x46/0x47 原始字段、TreeView 句柄与序号。文档/Section 版本不一致时拒绝，未知终止结构不生成插入锚点。
- [x] S 页提供只读 `BuildPendingWritebackPlan` 预检：每次使用时对当前剧本树重新捕获，并与缓存加载基线、Section 作用域和全部出场来源核对后生成值计划；此方法不改树、不改 dirty、不开放写回。
- [x] 将该预检接入带完整证据参数的内部原生事务入口；在任何树变更前解析全部逻辑键与父/插入位置，存在陈旧节点或范围越界则整份拒绝。入口仍是私有方法，没有新增产品写回控件。
- [x] 增加 Battle 专用“父节点 + 插入后位置”视图 API：只接受 0x4B/0x46/0x47、严格要求 20/1000 项完整向量，不走通用截断接口，也不改变选择或 dirty 标志。
- [x] 插入前将编辑器全局 `cur_code_ord` 与完整文档最大现存序号比较；只允许序号大于最大值（允许删除后留空洞），拒绝重复/陈旧计数器和缺失的现存序号；纯值测试覆盖这些边界。
- [x] 将专用插入 API 纳入原子事务；批次结束后统一设置 MFC 与文档自定义 dirty。事务期间抑制选择通知引起的预览重载；缓存已是提交后的最终布阵，因此不额外重放预览。
- [x] 捕获受影响节点完整数据、TreeView 选择、`cur_code_ord`、自定义 `modified` 与 MFC `IsModified()`；提交失败时按逆序恢复；提交后若撤回本身中途失败，也恢复到已提交状态。
- [x] 成功后只建立一个 S“写回撤回”项；撤回应恢复相关节点、选择与 dirty，缓存保留最终布阵作为未写回状态。撤回内容修订号不计当前选择路径；但当前预览刷新会主动清除一次性原生句柄撤回记录，不承诺跨 Section 切换保留。
- [x] 不复用 R 页撤回栈、角色状态或写入辅助函数；文档重载时清除失效句柄与撤回项。
- [x] 以失败注入 fake host 覆盖混合更新/删除/连续插入、首条新指令已插入而第二条插入失败后的完整逆滚、陈旧撤回及撤回中途失败；fake host 与 MFC 桥共用删除资格策略，覆盖 0x4B/0x46/0x47 新增节点在撤回/回滚中的删除。撤回中途失败用例还逐项核验 0x46→0x47 的树顺序、各 1000 项字段向量及选择/dirty/修订状态；临时错误兄弟锚点变异会触发该断言。真实 MFC TreeView 窗口验证仍留在阶段 C。

## 阶段 C：证据门与验收（写回入口保持关闭）

- [x] 以当前编辑器源码建立 0x46/0x47/0x4B 结构字段类型、记录步长/容量及原生插入完整向量长度合同；这只证明 cczEditor 的结构/API 契约，不等于已证明所有语义。
- [x] 0x4B 首字段语义已按源码、S_10 样本和普罗 kind 0 导出 IL 修正为 `DeploymentIndex`，与角色 `DataId` 分离；parser 和字段规范回归覆盖该区别。
- [x] 值计划器在 0x4B 新增或删除改变槽布局时，按原有来源 ordinal 压紧幸存槽号，再按缓存顺序为新增项续号；只改坐标且槽序已规范时不额外改编号。重编号的既有缓存项会进入受影响 ID 与绑定结果。纯值测试覆盖删除中间槽后续号及新增接号。
- [x] 0x4B 的导入槽没有可持久化角色身份；显示按格式能力区分：新放置时使用用户已要求的所选 S 形象半透明预览，导入槽使用 `#槽号` 中性标记，调整既有槽时悬浮显示同号半透明标记。该编号是 0-based DeploymentIndex，不映射 Data ID，也不暗示角色身份已持久化。
- [ ] 用 Debug-only QA 按钮验证 **cczEditor 自己生成的写回结果**：以用户已给的普罗 `测试.eex` 只作输入种子副本，在副本中新增/调整布阵，通过编辑器原生事务写入内存，再由编辑器正常保存；关闭并重开这个 cczEditor 产物，核验 0x46/0x47/0x4B 顺序、位置和 0x4B 槽号。不得再要求用户重复验证普罗原始导出文件能否被原生编辑器读取。
- [x] 定向纯值测试与 R/S 隔离合同通过；Win32 Debug/Release 均在独立目录构建成功，没有覆盖正在运行的 EXE。
- [x] 2026-09-25 增补后，Release Win32 PreviewCore 全套 257 项测试通过；MFC Release Win32 在 `.tmp-s-battle/marker-ui/bin/s.exe` 隔离重建成功（exit 0，0 errors；全量构建统计 351 条 warning，未逐条归属），没有覆盖用户正在运行的版本。
- [x] 事务层 fake host 的失败注入覆盖部分提交回滚；新增“第一条插入成功、第二条插入失败”用例，断言已插入节点、既有更新/删除、序号、dirty 标志和文档修订号全部恢复。
- [x] 2026-09-25 修正原生撤回/回滚缺陷：MFC `DeleteBattleNode` 原先仅接受 0x4B，导致新增 0x46/0x47 无法被事务撤回或部分失败回滚。统一策略仅允许完整 20/1000 项、无子节点/长文本/结构链接的 0x4B/0x46/0x47 部署节点；源 0x46/0x47 的主动删除仍由计划器拒绝。先以 fake-host 限制复现 RED，再修复后 Release Win32 CoreTests 全套 258 项通过、`test_battle_preview_writeback.ps1` 通过，MFC Release Win32 隔离构建 0 errors；产物位于 `.tmp-s-battle/mfc-final-check/editor/s.exe`，未覆盖在用 EXE。
- [x] 2026-09-25 后续复核：MFC Debug Win32 也在 `.tmp-s-battle/mfc-debug-check/editor/s.exe` 隔离构建成功（0 errors）；`test_battle_preview_port.ps1` 样本路径、大小、SHA-256 和编辑器基线合同全通过。
- [x] 2026-09-25 再运行写回门禁时，旧 runner 因早于源码被 freshness guard 正确拒绝；在 `.tmp-s-battle/writeback-review-20260925/` 独立重建 Release Win32 测试 runner 后，完整 CoreTests `258 passed / 0 failed`，`test_battle_preview_writeback.ps1` 与 `test_battle_preview_port.ps1` 均通过。此项没有改写产品 EXE 或剧本。
- [ ] 尚未对 MFC `CTreeCtrl` 适配层本身做真实窗口集成验证；隔离 QA 控件仅允许对 EXE 旁有标记的 S 副本运行，共用正式事务实现但不伪造证据门。
- [ ] 用户在 QA Debug 版中触发事务并用编辑器正常保存、关闭重开 QA 副本；此处验收对象是 cczEditor 新写入的文件，不是普罗已验证过的源文件。
- [x] 内部值计划/事务层覆盖来源陈旧、字段保留、固定页容量、缺失或越界锚点、未知插入位置、dirty/选择/序号恢复及部分失败逆滚；拒绝非法整批，回滚失败注入用例已覆盖。
- [x] 2026-09-25 静态复核 Puro `kind=300`：`Piece.kind` 1/2/0 分别写 0x46/0x47/0x4B，三组都会反转；0x46 每页 20 槽、0x47 每页 80 槽，固定页不足处以 0xFFFF 哨兵补齐；0x4B 为逐槽 28 字节记录并按输出顺序重新生成 0-based 编号。`Convert4/Convert5` 的 IL 确认小端编码及 16 位字段切分。详见项目报告 `../../../../../REVERSE-普罗工具战场布置器.md`（含“kind 0 反转”纠正）。
- [x] 2026-09-25 收到用户提供的 Puro `测试.eex`（5704 字节，SHA-256 `B62DE448F0C2F256DFB0C762A1217A03AB03321E820FC476E33E5DD5D10C23F9`）并完成只读结构核对：一个 0x46 固定页（6 个有效单位）、一个 0x47 固定页（9 个有效单位）、5 条 0x4B（槽号 0–4）；记录字段布局、空槽哨兵、页距与静态 writer 结论吻合。详见 `workspace/REVERSE-普罗工具战场布置器.md` 的样本核验节。
- [ ] Puro `测试.eex` 的静态结构已核对，且用户确认普罗导出由原生编辑器读取已反复验证；它只作为 QA 测试输入，不再把“原生编辑器能否读 Puro 文件”列为待验收事项。待验的是 cczEditor 写回事务输出是否经自身正常保存、关闭重载后保持结构与预览一致。
- [ ] 当前样本没有删除中间 0x4B 的操作历史，不能证明删除后的紧凑化；若后续需要实证，只需 3 个单位删除中间一个并导出独立副本。
- [x] 撤销“导出超过 20 个 0x4B”的压力测试要求。用户确认我军最多 20 人；若确需核对上限，只能测试合法边界 20，不再要求不可能的第 21 个。
