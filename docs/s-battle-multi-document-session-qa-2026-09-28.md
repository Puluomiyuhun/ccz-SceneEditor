# S 战场多剧本文档会话 QA 报告

日期：2026-09-28
范围：S 战场预览多文档会话、树生命周期、资源复用和 R/S 路由隔离。

## 实现结果

- 每个 `CcczEditor2Doc` 实例取得进程内唯一 ID。S 预览注册表按 ID 保存战场基线、临时编辑、撤回事务、条件树工作区和可恢复界面状态；单一 S 页面按活动文档临时绑定。
- 文档内容 epoch 与树 generation 用于检测语义编辑和 TreeView 替换。相同文档切回时只有 ID、版本和剧本路径都匹配才跳过捕获；另存为到新路径会推进内容 epoch，并拒绝复用旧 Mod 资源根。
- 树重建、窗口树销毁和真实文档关闭分开处理。关闭只按目标文档 ID 移除对应会话；S 内容编辑不会再经通用失效入口清空自己的会话草稿。
- 树生命周期回调现在接收所属文档。R 文档事件才清理 R 页；S 文档事件只停车或保留对应 S 会话，避免打开/关闭 S 剧本时抹掉 R 条件树和撤回状态。
- S 激活时会取消排队中的 R 刷新并停止后台动态播放，但不调用会清空 R 页的完整失效函数，因此保留当前 R 帧、条件树与撤回事务状态。
- 地图和单位帧通过规范化路径、文件版本和子资源键共享。LRU 目标为 128 MiB，当前活动图像使用 pin 防止淘汰；文档关闭解除 pin，编辑器退出释放缓存。

## 自动化和构建

- Debug CoreTests：300 项通过，0 项失败。完整输出保存在 `cczEditor2/.tmp-s-battle/multi-document-sessions/final/logs/coretests-debug-full.log`。
- PowerShell 回归：19 项通过，包括会话注册、文档版本、关闭生命周期、陈旧来源、快速模式、条件树、原生写回边界、资产缓存、R/S 刷新路由、R 多选/位置/条件树回归。使用项目 PowerShell 7 运行器；最终日志为 `cczEditor2/.tmp-s-battle/multi-document-sessions/final-rs-preserving/logs/powershell-regression-pwsh7-r-preserving.log`。
- Debug Win32 QA 编辑器：17,768,448 字节，SHA-256 `DD74524E7CA8E8365A6B8249A220B909EC2E3BDDDC5D560054AACB6EF56AC587`。
- Release Win32 编辑器：5,411,840 字节，SHA-256 `B552770578E63AEA6B82C555B66666152726419946AA9591E8E490A8BF331352`。同 checkout Release 基线为 5,386,240 字节，SHA-256 `78FEC867596D7CCF49B721B7AF5AFFBE4DB94F2CDE13B31024E779A82DFD782E`；净增 25,600 字节，约 0.48%。这是同配置比较。Debug QA 与 Release 体积不用于互相比值。
- `git diff --check` 通过；Git 只提示现存混合换行风格将来可能规范化，没有空白错误。

## QA 包

- 新测试目录：[S战场多文档会话QA_20260928-RS隔离修复](C:/Users/LanceYuki/OneDrive/Desktop/S战场多文档会话QA_20260928-RS隔离修复)
- 压缩包：[S战场多文档会话QA_20260928-RS隔离修复.zip](C:/Users/LanceYuki/OneDrive/Desktop/S战场多文档会话QA_20260928-RS隔离修复.zip)
- 压缩包 31 个文件均已解压流 SHA-256 对照验证；大小 26,298,660 字节，SHA-256 `FA85F6105D0ADC7C873DD38C27CFBE6C51C73447119B2E5F6515EEC603E26BF3`。包含三份 INI、QA 标记、两份 S_10 剧本副本和 18 张地图图像。QA 的 `CczSceneEditor2.ini` 指向包内 `Ekd5.exe`。
- `S_10_A.eex` 和 `S_10_B.eex` 是两个独立文件，当前均为干净源副本哈希 `D38BD56590CE0F4AA60B3DB8226FC6B93C862DFA1CE287769B0CD26065CCCF1D`。
- 先前桌面 QA 包和正在运行的进程 PID 32760 未被覆盖或关闭。

## 尚待真实窗口确认

- 本轮没有操作 MFC 窗口，没有实际执行 A→B→A、S/R 往返、关闭 B 保留 A、取消批量关闭或保存重开。
- 计划中的 20 次交替切换计时尚未执行，因此没有中位数、P95 或工作集峰值数据；当前只能确认结构上热切换可跳过完整捕获/重建的门禁与源码路径，不能据此宣称用户体感延迟已量化改善。
- 图像缓存以 4 字节/像素估算驻留量。活跃 pin 不会被淘汰，因此当活跃资源总量超过 128 MiB 时，驻留量会暂时超过目标；还需在真实工作集下记录峰值。
- 真实 MFC 的关闭时序和 R/S 状态隔离仍需使用本包进行窗口验收。产品写回入口保持 QA-only。
