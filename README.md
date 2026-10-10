# ccz-SceneEditor 增强版

本分支在 [Puluomiyuhun/ccz-SceneEditor](https://github.com/Puluomiyuhun/ccz-SceneEditor) 的基础上加入 R/S 场景预览、战场编辑辅助、模板库和输入辅助等功能。原作者仓库及本 Fork 的 `main` 保留原版；增强版源码位于本分支。

## 编译

使用 Visual Studio 2022（含 C++ 桌面开发、MFC 和 Windows SDK）打开 `cczEditor2.sln`，选择 `Release | x86` 编译。工程包括编辑器、`cczPreviewCore` 和 `cczPreviewCoreTests`。运行预览时需要使用者自行配置相应的游戏资源；源码仓库不包含游戏数据。

## 主要增强

- R/S 场景预览、对白与战场动态预览
- S 战场角色、障碍物、Gate、地图工具及写回辅助
- 模板库、代码检索、换色与换行辅助
- 模拟变量、条件树与预览控制

本分支以原作者仓库 `main` 的 `e75b2e1` 为基线，集中提交增强版源码，便于审阅。原项目未附许可证；使用与再分发规则请以权利人说明为准。
