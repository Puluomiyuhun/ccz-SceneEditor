# R 剧本文档预览会话

## 状态

用户已批准将现有按文档隔离的 S 会话方案扩展至 R 剧本。本规格限定在 R 页面新增独立会话；S 页面继续使用自己的会话实现。

## 根因与目标

当前 MDI 子页面激活会进入 `CcczEditor2View::OnActivateView`，并同步调用完整的 `RefreshScenePreview()`。R 预览页只持有一个 `PreviewSession`，因此切换 R 文档时会捕获完整树、重建变量/Section 模型和预览帧；`SkipRepaint` 发生在这些工作之后。此次修改要让每个仍打开的 R 文档拥有独立的预览会话，并让未变化文档的再次激活恢复缓存状态。

用户确认的 QA 截图对应的测试包也漏编译了写回入口：`BattlePreviewPage.cpp` 的 `QA写回` 控件受 `_DEBUG` 与 `CCZ_S_BATTLE_WRITEBACK_QA` 双重编译门控制。QA 剧本旁的标记文件不能把编译时移除的控件补回来。Debug QA 构建必须传入 `/p:CCZSBattleWritebackQa=true`；Release 不包含 QA 写回按钮。

## 行为契约

1. 每个打开的 R 文档按进程内文档实例 ID 保存独立的 `PreviewSession`、最近成功的输入快照、当前静态场景状态和播放时间线锚点。
2. MDI 激活只有在文档 ID、路径、内容 epoch、树 generation、TreeView 句柄和当前选中目标都与缓存吻合时，才可直接恢复会话；该路径不得捕获完整文档、重建模型或调用原生场景重放。
3. 首次打开、内容 epoch 改变、TreeView 被重建或选中目标改变时，使用当前树执行现有 R 刷新流程，并更新该文档自己的缓存。版本或句柄校验失败时不使用缓存句柄。
4. 文档关闭只清除其 R 会话；树重建使该文档旧树句柄失效，但不影响其他 R 文档。S 会话与 R 会话不共享可变预览状态。
5. 快速恢复后，条件树、变量控制、静态场景、文本覆盖和播放控件都必须对应目标 R 文档。切换文档时停止正在运行的动态计时器，恢复其静态目标状态。
6. R 的现有只读资源解析仍由 R 页面拥有。缓存是否可复用必须按其 Mod 根目录判断；不可复用时走正确的资源加载路径，不能显示另一 Mod 的素材。
7. QA 写回按钮只在带 `CCZSBattleWritebackQa=true` 的 Debug Win32 构建中出现，并且仍受 QA 剧本标记门控；Release Win32 不出现该 QA 入口。

## 设计

- `CcczEditor2Doc` 暴露通用 `PreviewDocumentId`，保留现有 S 专用访问器作为兼容入口。
- `CRScenePreviewPage` 维护自己的 R 文档会话表，键为通用文档 ID；`CScenePreviewWnd` 仍只负责在独立 R/S 页面之间路由。
- `OnActivateView` 使用“激活”请求；树选择变化和编辑操作继续使用强制刷新请求。二者共享当前 R 刷新实现，但激活可以在完整捕获之前命中缓存。
- 会话保存逻辑预览状态和值拥有的捕获快照。`SceneState` 内的树句柄仅在 TreeView 句柄和 tree generation 均匹配时可恢复。
- 关闭文档与树重建分别清除或失效该文档会话。不会在 S/R 页面切换时销毁仍打开文档的缓存。
- 共享的只读素材缓存按根目录/素材身份使用；R 不依赖 S 的页面类或 S 写回逻辑。

## 验收

1. R-A → R-B → R-A 热切换后，A/B 各自恢复对应的条件树变量、静态场景和播放锚点。
2. 未变化文档热激活计数为零：完整文档捕获、模型重建、原生重放均不执行。
3. 选中节点变化仍刷新到新目标；内容 epoch 或 tree generation 变化会刷新或安全失效旧缓存。
4. 关闭 B 不影响 A；S→R→S 保留 S 会话，R→S→R 保留 R 会话。
5. QA Debug Win32 构建记录包含 `CCZ_S_BATTLE_WRITEBACK_QA`，QA 包底栏显示“QA写回”；Release Win32 不包含该按钮。
6. 自动化源码/core 测试与 Win32 Debug/Release 构建通过。真实 MFC 标签页切换和按钮点击仍需桌面验收；没有实测的耗时不报告为改善。

## 明确排除

- 不改 R 预览的指令解析、坐标、写回或撤回语义。
- 不把 R 状态移入 S 页面或让 S 页面调用 R 预览代码。
- 不把 QA 写回入口加入 Release 产品构建。
- 不提交、清理或覆盖工作区中原有的未提交文件和测试包。
