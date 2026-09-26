# 预览内置化基线冻结记录

日期：2026-09-18

本记录只冻结内置化开始前的文件和测试状态，不包含功能代码变更。基线文件用于后续差分和回滚验证，不能被内置化工作流覆盖。

## 冻结文件

| 文件 | SHA-256 |
| --- | --- |
| `../../ccz_preview_ext/releases/phase1-20260916/cczEditor_v0.24.exe` | `781A3833B6031D216392591FCE5D5F43D899BAA5B3ABC148A999EC6D2B0B45C8` |
| `../../ccz_preview_ext/releases/phase1-20260916/cczPreviewExt.dll` | `929DA194A7732163743D29C0CF570EB1535E566CFEFC716625CFE1331A724FF3` |
| `../../ccz_preview_ext/releases/phase1-20260916/cczPreviewExtLauncher.exe` | `25B89C8111C327580D1E4C97ED32E87F786EDC82CA8EAAFADE06ABB2D0A3E87E` |
| `Release/s.exe` | `5A3CB3DE8AA4F71A9A602A8A5F9EE874E5818C4F04F037B0E1A7A3BC3C39E3AD` |

对应的机器可校验清单是：
`tests/fixtures/preview_internalization/baseline.json`。

既有可回滚 staging 目录为：
`../../ccz_preview_ext/staging-test/ccz-editor-three-review-fixes-20260918/`。

以下两个范围均按只读参考处理，Task 1 没有向其中写入、移动或删除文件：

- `../../ccz_preview_ext/releases/phase1-20260916/*`
- `../../ccz_preview_ext/staging-test/ccz-editor-three-review-fixes-20260918/*`

## 实际验证命令与结果

### 1. 失败前置验证

在创建 manifest 前执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_internalization_baseline.ps1
```

结果：按预期失败，输出 `PREVIEW_INTERNALIZATION_BASELINE_FAIL: manifest missing`，退出码为 1。

### 2. 基线清单门禁

创建 `baseline.json` 后再次执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_internalization_baseline.ps1
```

结果：`PREVIEW_INTERNALIZATION_BASELINE_PASS`。

### 3. DLL 现有测试套件

直接在普通 PowerShell 中执行简报原命令时，因没有加载 VS x86 开发环境，第一步失败于 `cstdint` 找不到；没有把这次环境错误记为源码通过。实际使用 x86 Developer Command Prompt 环境执行了等价命令：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x86
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset ccz-preview-x86 --clean-first -- -j1
```

结果：`[68/68]` 完成，DLL、启动器和测试程序均成功链接。

随后执行：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x86
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --preset ccz-preview-x86
```

结果：`1/1` 测试通过，`100% tests passed, 0 tests failed out of 1`，总耗时约 7.34 秒。

### 4. 当前编辑器回归脚本快照

Task 1 没有修改编辑器源码，也没有重新生成 `Release/s.exe`。为记录内置化前状态，执行了现有 `test_*.ps1` 脚本（排除本基线脚本）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_combo_notification_routing.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_integration.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_selection_commit.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_ux_regressions.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_long_char_data_safety.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_remaining_combo_review_fixes.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_remaining_scene_config_fixes.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_background_preview.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_multiselect_ui.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_multiselect_writeback.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_position_sources.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_targeted_review_contract.ps1
```

结果：9 个通过，3 个失败。失败均来自 Task 1 之前已经存在的编辑器工作树/测试状态，Task 1 未修复它们：

- `test_scene_multiselect_ui.ps1`：`primary X alignment command label is missing`；
- `test_scene_position_sources.ps1`：脚本自身在末尾存在字符串未闭合的 PowerShell ParserError；
- `test_targeted_review_contract.ps1`：既有 staging README 缺少 `针对性 PowerShell 回归脚本全部通过` 标记。

这些失败不能被 DLL 基线测试的通过结果掩盖，后续任务应分别处理或明确重新取证。

### 5. Release x86 构建门禁

Task 1 为非变更基线任务，没有执行会覆盖 `Release/s.exe` 的重建命令；否则会改变本记录中的第四个冻结输入。后续需要重新构建时使用精确命令：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86 /m:1
```

当前 `Release/s.exe` 已通过基线 SHA-256 门禁，未被 Task 1 改写。

## 取证时的工作树状态

下面是创建 Task 1 文件前的 `git status --short` 原样快照；这些已有修改属于用户工作树，Task 1 没有重置、清理或覆盖：

```text
 M cczEditor2/CMyComboBox.cpp
 M cczEditor2/CMyComboBox.h
 M cczEditor2/ScenePreviewWnd.cpp
 M cczEditor2/ScenePreviewWnd.h
 M cczEditor2/cczEditor2.cpp
 M cczEditor2/cczEditor2.rc
 M cczEditor2/cczEditor2.vcxproj
 M cczEditor2/cczEditor2.vcxproj.filters
 M cczEditor2/cczEditor2Doc.cpp
 M cczEditor2/cczEditor2Doc.h
 M cczEditor2/cczEditor2View.cpp
 M cczEditor2/cczEditor2View.h
 M cczEditor2/resource.h
?? cczEditor2/IndexedComboSearch.h
?? cczEditor2/LongCharData.h
?? cczEditor2/MapPortraitState.h
?? cczEditor2/SceneBackgroundResolver.h
?? cczEditor2/SceneCoordinate.h
?? cczEditor2/SceneMultiSelectOps.h
?? cczEditor2/ScenePositionSource.h
?? cczEditor2/TextWrapAssist.cpp
?? cczEditor2/TextWrapAssist.h
?? docs/
?? tests/
```

Task 1 新增的三个文件为：

- `tests/test_preview_internalization_baseline.ps1`
- `tests/fixtures/preview_internalization/baseline.json`
- `docs/internalization/preview-internalization-baseline-20260918.md`

没有创建 staging 副本，也没有更新旧 staging；本任务没有可供启动的新版 EXE 链接。

DONE_WITH_CONCERNS
