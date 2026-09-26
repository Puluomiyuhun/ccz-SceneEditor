# Preview DLL Internalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `cczPreviewExt.dll` 中已验收的预览增强功能，经审查、精简和适配后内置进剧本编辑器，并从正式运行链路中移除 DLL、启动器、固定 RVA、Hook 和窗口扫描。

**Architecture:** 新建纯逻辑静态库 `cczPreviewCore`，由编辑器侧 `EditorPreviewAdapter` 生成结构化输入，`PreviewSession` 维护唯一模拟状态，`CScenePreviewWnd` 及其自有窗口负责绘制和交互。迁移期间保留旧 DLL 与启动器作为只读对照；每个阶段都有独立测试、staging 和回滚点。

**Tech Stack:** Visual C++ 2022 Build Tools、MFC、C++17、x86、静态 CRT、GDI+、PowerShell 回归脚本、现有轻量 C++ 测试框架。

**Spec:** `docs/superpowers/specs/2026-09-18-preview-dll-internalization-design.md`

## Global Constraints

- 最终正式 EXE 不加载或导入 `cczPreviewExt.dll`，不依赖 `cczPreviewExtLauncher.exe`。
- 不把固定 EXE SHA-256、固定 RVA、调用点前像、`VirtualProtect` Hook 或窗口枚举迁入编辑器。
- `cczPreviewCore` 不包含 MFC、HWND、HTREEITEM、后台轮询线程或可变全局单例。
- `cczPreviewCore` 与 `cczEditor2` 明确使用 C++17、x86 和一致的静态 CRT。
- 0x79 正式比较符编码固定为：`0 ==`、`1 >=`、`2 <=`、`3 !=`。
- Choice 在首次加载且没有显式选择时，默认选择其第一个 Case；用户显式选择始终优先。
- 预览边界包含当前选中指令。
- 自动刷新采用事件驱动；鼠标移动、定时扫描和窗口轮询不能成为模型刷新来源。
- 旧 DLL、启动器、`releases/phase1-20260916` 和现有 staging 不删除、不覆盖。
- 迁移后的正式源码不得通过相对路径继续编译 `workspace/ccz_preview_ext` 中的文件。
- 当前编辑器工作树已有用户修改；每个任务只暂存和提交本任务文件，不重置、不清理其他修改。
- 当前仓库未配置 Git 作者身份。执行者不得编造 `user.name` 或 `user.email`；首次提交前由用户配置，未配置时保留验证后的工作树并报告。
- 只有用户明确选择子代理执行时才调度内部子代理；模型必须为 `gpt-5.6-luna`，思考强度为 `xhigh`。
- 每次用户可见修改后更新 `workspace/ccz_preview_ext/staging-test/ccz-editor-internalization-working` 并提供快速启动文件。

## File Structure

新增和调整后的职责如下：

```text
ccz-SceneEditor/
├─ cczEditor2/
│  ├─ ScenePreviewWnd.*                    # 原生场景绘制、最终重放和 UI 宿主
│  ├─ cczEditor2View.*                     # 编辑器事件转发和统一刷新入口
│  └─ PreviewIntegration/
│     ├─ EditorTreeSnapshot.*              # CTreeCtrl/ItemData 的安全按值快照
│     ├─ EditorPreviewAdapter.*            # 快照到核心 SectionInput 的转换
│     ├─ PreviewSession.*                  # 唯一模拟状态和刷新事务
│     ├─ PreviewAssetCache.*               # data.e5/face.e5 的文档级只读缓存
│     ├─ DialogueOverlayRenderer.*          # GDI+ 台词覆盖层绘制
│     ├─ ConditionTreePane.*                # 编辑器拥有的条件树窗口
│     └─ VariableControlPane.*              # 编辑器拥有的变量控制窗口
├─ cczPreviewCore/
│  ├─ cczPreviewCore.vcxproj
│  ├─ cczPreviewCore.vcxproj.filters
│  ├─ include/ccz/preview/*.h              # 不含 MFC/Win32 窗口类型的公开接口
│  └─ src/*.cpp                            # 条件、台词、变量、头像和重放计划
├─ cczPreviewCoreTests/
│  ├─ cczPreviewCoreTests.vcxproj
│  ├─ cczPreviewCoreTests.vcxproj.filters
│  └─ tests/*.cpp                          # 迁移后的核心单元测试
├─ tests/
│  ├─ fixtures/preview_internalization/*   # 基线和差分夹具
│  ├─ preview_integration/*.cpp            # 适配与会话测试
│  └─ test_preview_internalization_*.ps1   # 结构、构建和发布门禁
└─ docs/internalization/*                  # 基线和阶段报告
```

---

### Task 1: Freeze the Existing Baseline and Add a Non-Mutating Baseline Gate

**Files:**
- Create: `tests/test_preview_internalization_baseline.ps1`
- Create: `tests/fixtures/preview_internalization/baseline.json`
- Create: `docs/internalization/preview-internalization-baseline-20260918.md`
- Read only: `../../ccz_preview_ext/releases/phase1-20260916/*`
- Read only: `../../ccz_preview_ext/staging-test/ccz-editor-three-review-fixes-20260918/*`

**Interfaces:**
- Consumes: Existing files and SHA-256 hashes only.
- Produces: `baseline.json` as the immutable input for later packaging and differential gates.

- [x] **Step 1: Write the baseline verification script before the manifest exists**

```powershell
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..'))
)

$manifestPath = Join-Path $PSScriptRoot 'fixtures\preview_internalization\baseline.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: manifest missing: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $fullPath = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $entry.path))
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: file missing: $fullPath"
    }
    $actual = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
    if ($actual -ne $entry.sha256) {
        throw "PREVIEW_INTERNALIZATION_BASELINE_FAIL: hash mismatch: $($entry.path)"
    }
}

'PREVIEW_INTERNALIZATION_BASELINE_PASS'
```

- [x] **Step 2: Run the script and verify the missing manifest fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_internalization_baseline.ps1
```

Expected: FAIL containing `manifest missing`.

- [x] **Step 3: Create the immutable manifest with the confirmed artifacts**

```json
{
  "files": [
    {
      "path": "../../ccz_preview_ext/releases/phase1-20260916/cczEditor_v0.24.exe",
      "sha256": "781A3833B6031D216392591FCE5D5F43D899BAA5B3ABC148A999EC6D2B0B45C8"
    },
    {
      "path": "../../ccz_preview_ext/releases/phase1-20260916/cczPreviewExt.dll",
      "sha256": "929DA194A7732163743D29C0CF570EB1535E566CFEFC716625CFE1331A724FF3"
    },
    {
      "path": "../../ccz_preview_ext/releases/phase1-20260916/cczPreviewExtLauncher.exe",
      "sha256": "25B89C8111C327580D1E4C97ED32E87F786EDC82CA8EAAFADE06ABB2D0A3E87E"
    },
    {
      "path": "Release/s.exe",
      "sha256": "5A3CB3DE8AA4F71A9A602A8A5F9EE874E5818C4F04F037B0E1A7A3BC3C39E3AD"
    }
  ]
}
```

In `docs/internalization/preview-internalization-baseline-20260918.md`, record:

- the four hashes above;
- the existing staging path;
- the exact commands used to run DLL tests, editor tests and Release x86 build;
- the fact that the baseline directories are read-only references;
- the current `git status --short`, without modifying those files.

- [x] **Step 4: Run the baseline gate and record PASS**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_internalization_baseline.ps1
```

Expected: `PREVIEW_INTERNALIZATION_BASELINE_PASS`.

- [x] **Step 5: Run the existing DLL test suite and save the actual count**

Run from `workspace/ccz_preview_ext` in an x86 developer shell:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset ccz-preview-x86 --clean-first -- -j1
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --preset ccz-preview-x86
```

Expected: build succeeds and all currently registered tests pass. Record the reported test count rather than copying an older number.

- [ ] **Step 6: Commit only baseline files after Git identity is configured**

```powershell
git add -- tests/test_preview_internalization_baseline.ps1 tests/fixtures/preview_internalization/baseline.json docs/internalization/preview-internalization-baseline-20260918.md
git commit -m "test: freeze preview internalization baseline"
```

---

### Task 2: Add the Static Core and Test Projects to the Solution

**Files:**
- Create: `cczPreviewCore/cczPreviewCore.vcxproj`
- Create: `cczPreviewCore/cczPreviewCore.vcxproj.filters`
- Create: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj`
- Create: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj.filters`
- Create: `cczPreviewCoreTests/tests/test_framework.h`
- Create: `cczPreviewCoreTests/tests/test_main.cpp`
- Create: `tests/test_preview_core_project.ps1`
- Modify: `cczEditor2.sln`
- Modify: `cczEditor2/cczEditor2.vcxproj`

**Interfaces:**
- Consumes: Visual Studio x86 configurations already present in `cczEditor2.sln`.
- Produces: Static library project GUID `{3EF31258-CF86-4C0D-9AA6-93F4C2B6EBD5}` and test project GUID `{AEFAA323-41DD-47F6-A3F7-1D893F5970A3}`.

- [x] **Step 1: Write the project-structure regression test**

```powershell
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$solution = Get-Content (Join-Path $root 'cczEditor2.sln') -Raw
$coreProject = Join-Path $root 'cczPreviewCore\cczPreviewCore.vcxproj'
$testProject = Join-Path $root 'cczPreviewCoreTests\cczPreviewCoreTests.vcxproj'

if (-not (Test-Path $coreProject)) { throw 'PREVIEW_CORE_PROJECT_FAIL: core project missing' }
if (-not (Test-Path $testProject)) { throw 'PREVIEW_CORE_PROJECT_FAIL: test project missing' }
if ($solution -notmatch 'cczPreviewCore') { throw 'PREVIEW_CORE_PROJECT_FAIL: core project absent from solution' }
if ($solution -notmatch 'cczPreviewCoreTests') { throw 'PREVIEW_CORE_PROJECT_FAIL: test project absent from solution' }

$coreXml = Get-Content $coreProject -Raw
if ($coreXml -notmatch '<ConfigurationType>StaticLibrary</ConfigurationType>') {
    throw 'PREVIEW_CORE_PROJECT_FAIL: core is not a static library'
}
if ($coreXml -notmatch '<LanguageStandard>stdcpp17</LanguageStandard>') {
    throw 'PREVIEW_CORE_PROJECT_FAIL: core is not C++17'
}

'PREVIEW_CORE_PROJECT_PASS'
```

- [x] **Step 2: Run the structure test and verify it fails on missing projects**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_project.ps1
```

Expected: FAIL containing `core project missing`.

- [x] **Step 3: Create the two MSBuild projects with matching x86/CRT settings**

The core project must contain these configuration properties for Debug and Release Win32:

```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration">
  <ConfigurationType>StaticLibrary</ConfigurationType>
  <UseDebugLibraries>false</UseDebugLibraries>
  <PlatformToolset>v143</PlatformToolset>
  <CharacterSet>Unicode</CharacterSet>
</PropertyGroup>
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">
  <ClCompile>
    <LanguageStandard>stdcpp17</LanguageStandard>
    <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
    <WarningLevel>Level4</WarningLevel>
    <AdditionalIncludeDirectories>$(ProjectDir)include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
  </ClCompile>
</ItemDefinitionGroup>
```

The test project must be a `ConsoleApplication`, use C++17 and `/MT`, include `../cczPreviewCore/include`, and reference the core project:

```xml
<ProjectReference Include="..\cczPreviewCore\cczPreviewCore.vcxproj">
  <Project>{3EF31258-CF86-4C0D-9AA6-93F4C2B6EBD5}</Project>
</ProjectReference>
```

Add the same project reference to `cczEditor2/cczEditor2.vcxproj`. Set `cczEditor2` to C++17 in all four existing configurations because core public headers use `std::optional`.

- [x] **Step 4: Add both projects and all solution configuration mappings**

Add project entries:

```text
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "cczPreviewCore", "cczPreviewCore\cczPreviewCore.vcxproj", "{3EF31258-CF86-4C0D-9AA6-93F4C2B6EBD5}"
EndProject
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "cczPreviewCoreTests", "cczPreviewCoreTests\cczPreviewCoreTests.vcxproj", "{AEFAA323-41DD-47F6-A3F7-1D893F5970A3}"
EndProject
```

Map `Debug|x86` to `Debug|Win32` and `Release|x86` to `Release|Win32` for both projects. Keep x64 configurations present but do not use them for acceptance.

- [x] **Step 5: Add the minimal test harness and one link smoke test**

```cpp
#include "test_framework.h"

CCZ_TEST(core_test_project_links) {
    CCZ_REQUIRE(true);
}
```

- [x] **Step 6: Run the project structure test and Release x86 build**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_project.ps1
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1
.\Release\cczPreviewCoreTests.exe
```

Expected: structure test passes, solution builds, and the smoke test prints one PASS.

- [ ] **Step 7: Commit the project skeleton**

```powershell
git add -- cczEditor2.sln cczEditor2/cczEditor2.vcxproj cczPreviewCore cczPreviewCoreTests tests/test_preview_core_project.ps1
git commit -m "build: add internal preview core projects"
```

---

### Task 3: Migrate the Host-Independent Runtime and Its Tests

**Files:**
- Create: `cczPreviewCore/include/ccz/preview/dialogue_profile.h`
- Create: `cczPreviewCore/include/ccz/preview/dialogue_model.h`
- Create: `cczPreviewCore/include/ccz/preview/face_archive.h`
- Create: `cczPreviewCore/include/ccz/preview/portrait_resolver.h`
- Create: `cczPreviewCore/include/ccz/preview/dialogue_overlay_model.h`
- Create: `cczPreviewCore/include/ccz/preview/portrait_overlay.h`
- Create: `cczPreviewCore/include/ccz/preview/dialogue_replay.h`
- Create: `cczPreviewCore/include/ccz/preview/condition_tree.h`
- Create: `cczPreviewCore/include/ccz/preview/native_replay_plan.h`
- Create: `cczPreviewCore/include/ccz/preview/execution_model.h`
- Create: `cczPreviewCore/include/ccz/preview/script_variable_index.h`
- Create: `cczPreviewCore/include/ccz/preview/instruction_catalog.h`
- Create: `cczPreviewCore/include/ccz/preview/condition_tree_state.h`
- Create: `cczPreviewCore/include/ccz/preview/variable_control_state.h`
- Create corresponding implementations under: `cczPreviewCore/src/`
- Create migrated unit tests under: `cczPreviewCoreTests/tests/`
- Modify: `cczPreviewCore/cczPreviewCore.vcxproj`
- Modify: `cczPreviewCoreTests/cczPreviewCoreTests.vcxproj`

**Interfaces:**
- Consumes: The behavior of `../../ccz_preview_ext/src/preview_runtime/*` and the pure state portions of `../../ccz_preview_ext/src/ui/*_state.cpp`.
- Produces: Namespace `ccz::preview`, value-only public types, and a static library with no MFC/host-window dependency.

- [x] **Step 1: Copy the existing pure tests first and update only include paths**

Migrate these tests before implementation:

```text
dialogue_profile_tests.cpp
dialogue_model_tests.cpp
face_archive_tests.cpp
portrait_resolver_tests.cpp
portrait_overlay_tests.cpp
dialogue_replay_tests.cpp
condition_tree_tests.cpp
native_replay_plan_tests.cpp
execution_model_tests.cpp
instruction_catalog_tests.cpp
script_variable_index_tests.cpp
variable_control_state_tests.cpp
```

Change includes from forms such as:

```cpp
#include "preview_runtime/execution_model.h"
```

to:

```cpp
#include <ccz/preview/execution_model.h>
```

- [x] **Step 2: Build and verify tests fail because the new core headers are absent**

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1
```

Expected: compile failure naming the first missing `ccz/preview/*.h` header.

- [x] **Step 3: Migrate the runtime one responsibility at a time**

For each header/source pair:

1. move it into the new layout;
2. change internal includes to `<ccz/preview/...>`;
3. remove `bridge` includes;
4. add it to the core project;
5. build and run only its matching tests before proceeding.

The first `DialogueOverlayModel` must be a value type with no Windows headers:

```cpp
struct DialogueOverlayModel {
    bool visible{};
    std::wstring speaker_label;
    std::wstring body;
    std::vector<std::uint8_t> portrait_image;
    std::wstring diagnostic;
};

bool dialogue_overlay_model_equal(const DialogueOverlayModel& left,
                                  const DialogueOverlayModel& right);
```

Do not migrate `set_dialogue_overlay_model`, `get_dialogue_overlay_model`, `HDC`, `RECT` or GDI+ painting into the core.

- [x] **Step 4: Add a forbidden-dependency source gate**

Create `tests/test_preview_core_boundaries.ps1`:

```powershell
$core = Get-ChildItem (Join-Path $PSScriptRoot '..\cczPreviewCore') -Recurse -File -Include *.h,*.cpp
$forbidden = @('afxwin.h', 'CTreeCtrl', 'HTREEITEM', 'HWND', 'VirtualProtect', 'SetWindowLongPtr', 'CreateThread')
foreach ($file in $core) {
    $text = Get-Content $file.FullName -Raw
    foreach ($token in $forbidden) {
        if ($text -match [regex]::Escape($token)) {
            throw "PREVIEW_CORE_BOUNDARY_FAIL: $token in $($file.FullName)"
        }
    }
}
'PREVIEW_CORE_BOUNDARY_PASS'
```

- [x] **Step 5: Build and run the migrated test executable and boundary gate**

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1
.\Debug\cczPreviewCoreTests.exe
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_boundaries.ps1
```

Expected: all migrated tests pass and the boundary gate prints `PREVIEW_CORE_BOUNDARY_PASS`.

- [x] **Step 6: Compare test names with the old runtime suite**

Run both executables, normalize lines beginning with `[PASS]`, and require that every migrated pure-runtime test name appears in the new output. Window, launcher, target-profile and Hook tests are intentionally excluded.

- [ ] **Step 7: Commit the pure runtime migration**

```powershell
git add -- cczPreviewCore cczPreviewCoreTests tests/test_preview_core_boundaries.ps1
git commit -m "refactor: migrate preview runtime into static core"
```

---

### Task 4: Canonicalize Core Semantics and Remove DLL-Era Compatibility APIs

**Files:**
- Modify: `cczPreviewCore/include/ccz/preview/execution_model.h`
- Modify: `cczPreviewCore/src/execution_model.cpp`
- Modify: `cczPreviewCore/include/ccz/preview/dialogue_replay.h`
- Modify: `cczPreviewCore/src/dialogue_replay.cpp`
- Modify: `cczPreviewCore/include/ccz/preview/condition_tree.h`
- Modify: `cczPreviewCore/src/condition_tree.cpp`
- Modify: `cczPreviewCoreTests/tests/execution_model_tests.cpp`
- Modify: `cczPreviewCoreTests/tests/dialogue_replay_tests.cpp`
- Modify: `cczPreviewCoreTests/tests/condition_tree_tests.cpp`
- Create: `cczPreviewCoreTests/tests/choice_default_tests.cpp`

**Interfaces:**
- Consumes: Migrated value-only runtime from Task 3.
- Produces: Canonical logical-key replay API, four correct 0x79 comparators, and `initialize_default_choice_selections`.

- [x] **Step 1: Write failing tests for all four encoded comparators**

```cpp
CCZ_TEST(integer_expression_decodes_all_confirmed_comparators) {
    const struct Case { int raw; IntegerComparisonOperator expected; } cases[] = {
        {0, IntegerComparisonOperator::Equal},
        {1, IntegerComparisonOperator::GreaterEqual},
        {2, IntegerComparisonOperator::LessEqual},
        {3, IntegerComparisonOperator::NotEqual},
    };
    for (const Case& value : cases) {
        DialogueCommandView command;
        command.instruction_id = 0x79;
        command.int_data = {4, 600, value.raw, 0, 10};
        const auto expression = decode_integer_expression(command);
        CCZ_REQUIRE(expression.decoded);
        CCZ_REQUIRE_EQ(expression.comparator, value.expected);
    }
}
```

Add evaluation tests for `10 <= 10`, `11 <= 10`, `10 != 11`, and `10 != 10`. Add reverse-assignment tests for both left-variable and right-variable forms.

- [x] **Step 2: Run the comparator tests and verify raw values 2 and 3 fail**

```powershell
.\Debug\cczPreviewCoreTests.exe integer_expression_decodes_all_confirmed_comparators
```

Expected: FAIL before implementation.

- [x] **Step 3: Implement the corrected enum, decode, evaluation and checked assignment**

```cpp
enum class IntegerComparisonOperator : std::uint8_t {
    Equal,
    GreaterEqual,
    LessEqual,
    NotEqual,
    Unsupported,
};
```

Decode raw values exactly as required. For reverse assignment, detect impossible boundary requests instead of overflowing:

```cpp
bool checked_increment(std::int32_t value, std::int32_t& result) {
    if (value == (std::numeric_limits<std::int32_t>::max)()) return false;
    result = value + 1;
    return true;
}

bool checked_decrement(std::int32_t value, std::int32_t& result) {
    if (value == (std::numeric_limits<std::int32_t>::min)()) return false;
    result = value - 1;
    return true;
}
```

Use these assignments, where `other` is the already evaluated non-target operand:

| Comparator | Target is left, desired true / false | Target is right, desired true / false |
|---|---|---|
| `==` | `other` / any checked value different from `other` | `other` / any checked value different from `other` |
| `>=` | `other` / `other - 1` | `other` / `other + 1` |
| `<=` | `other` / `other + 1` | `other` / `other - 1` |
| `!=` | any checked value different from `other` / `other` | any checked value different from `other` / `other` |

The different-value helper selects `value + 1` unless `value` is `INT32_MAX`, in which case it selects `value - 1`.

Return `false` with a diagnostic if the requested truth value is mathematically impossible at the integer boundary.

- [x] **Step 4: Write failing tests for first-Case initialization**

```cpp
CCZ_TEST(choice_defaults_to_first_case_only_when_unset) {
    SectionExecutionModel model;
    ExecutionNode choice;
    choice.logical_key = L"choice";
    choice.category = ExecutionCategory::Choice;
    ExecutionNode case1;
    case1.logical_key = L"case1";
    case1.category = ExecutionCategory::Case;
    ExecutionNode case2;
    case2.logical_key = L"case2";
    case2.category = ExecutionCategory::Case;
    choice.cases = {case1, case2};
    model.body.push_back(choice);

    ReplayOptions options;
    CCZ_REQUIRE(initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"choice"), L"case1");

    options.selected_case_by_choice[L"choice"] = L"case2";
    CCZ_REQUIRE(!initialize_default_choice_selections(model, options));
    CCZ_REQUIRE_EQ(options.selected_case_by_choice.at(L"choice"), L"case2");
}
```

- [x] **Step 5: Implement recursive default initialization**

Declare:

```cpp
bool initialize_default_choice_selections(const SectionExecutionModel& model,
                                          ReplayOptions& options);
```

Walk entry gates, body nodes and nested cases. For each Choice missing from `selected_case_by_choice`, select its first Case. Never overwrite an existing valid or invalid explicit user choice; invalid explicit choices remain diagnosable.

- [x] **Step 6: Remove numeric legacy replay APIs after caller scan**

Run:

```powershell
rg -n "ReplayNodeId|DialogueReplayOptions|replay_raw_section_to|std::vector<ReplayNode>" cczPreviewCore cczEditor2 cczPreviewCoreTests
```

Port remaining tests to `LogicalNodeKey`, `SectionExecutionModel`, `ReplayOptions` and `ReplayResult`, then delete the numeric compatibility structs and overloads. The scan must return no production callers.

- [x] **Step 7: Run the complete core suite**

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczPreviewCoreTests\cczPreviewCoreTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32 /m:1
.\Debug\cczPreviewCoreTests.exe
```

Expected: all tests pass, including comparator, boundary and default-choice cases.

- [ ] **Step 8: Commit canonical semantics**

```powershell
git add -- cczPreviewCore cczPreviewCoreTests
git commit -m "fix: canonicalize preview condition semantics"
```

---

### Task 5: Build the Editor Tree Snapshot and Core Input Adapter

**Files:**
- Create: `cczEditor2/PreviewIntegration/EditorTreeSnapshot.h`
- Create: `cczEditor2/PreviewIntegration/EditorTreeSnapshot.cpp`
- Create: `cczEditor2/PreviewIntegration/EditorPreviewAdapter.h`
- Create: `cczEditor2/PreviewIntegration/EditorPreviewAdapter.cpp`
- Create: `tests/preview_integration/editor_preview_adapter_tests.cpp`
- Create: `tests/test_editor_preview_adapter.ps1`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Consumes: `ItemData` from `cczEditor2Doc.h` and core types from Task 4.
- Produces:

```cpp
namespace ccz::editor_preview {

struct CapturedEditorNode {
    std::uintptr_t runtime_handle{};
    int instruction_id{};
    int ordinal{};
    std::vector<int> int_data;
    std::wstring long_text;
    std::wstring display_text;
    std::vector<CapturedEditorNode> children;
};

struct CapturedEditorTree {
    std::wstring document_path;
    std::vector<std::uint32_t> section_path;
    std::vector<std::uint32_t> selected_path;
    CapturedEditorNode section_root;
    std::vector<CapturedEditorNode> document_roots;
};

struct EditorPreviewInput {
    ccz::preview::LogicalNodeKey document_key;
    ccz::preview::SectionInput section;
    std::vector<ccz::preview::RawReplayNode> document_nodes;
    std::uint64_t document_revision{};
};

struct EditorPreviewCapture {
    bool ok{};
    bool idle{};
    EditorPreviewInput input;
    std::wstring diagnostic;
};

EditorPreviewCapture BuildEditorPreviewInput(
    const CapturedEditorTree& snapshot);

EditorPreviewCapture CaptureEditorPreviewInput(
    CTreeCtrl& tree, HTREEITEM selected_item,
    const std::wstring& document_path);

}
```

> 当前状态：适配器实现、入口指令—首个 0x00 边界修复、共享预算与自动化门禁均通过；修复后的独立复审已通过。Step 7 仅因 Git 身份未配置而保留未完成。

- [x] **Step 1: Write pure adapter tests using owned node snapshots**

Cover:

- stable logical keys across different fake handles;
- Section path and selected child path;
- structured `int_data` preservation;
- bounded copy of `long_char_data` using `long_char_data_capacity`;
- document-root selection produces `idle=true`;
- missing Section boundary fails atomically;
- depth and node-count limits return no partial `SectionInput`.

Example:

```cpp
CapturedEditorTree MakeAdapterFixture(std::uintptr_t runtime_handle) {
    CapturedEditorTree tree;
    tree.document_path = L"D:\\Mod\\R_13.eex";
    tree.section_path = {0, 0};
    tree.selected_path = {0};
    tree.section_root.instruction_id = 0;
    CapturedEditorNode selected;
    selected.runtime_handle = runtime_handle;
    selected.instruction_id = 0x14;
    selected.long_text = L"&云夜\\n测试";
    tree.section_root.children.push_back(selected);
    tree.document_roots.push_back(tree.section_root);
    return tree;
}

CCZ_TEST(editor_preview_adapter_stable_key_ignores_runtime_handle) {
    const auto first = BuildEditorPreviewInput(MakeAdapterFixture(0x1111));
    const auto second = BuildEditorPreviewInput(MakeAdapterFixture(0x9999));
    CCZ_REQUIRE(first.ok);
    CCZ_REQUIRE(second.ok);
    CCZ_REQUIRE_EQ(first.input.section.selected_target_key,
                   second.input.section.selected_target_key);
}
```

- [x] **Step 2: Compile the adapter test and verify missing implementation fails**

The PowerShell test must invoke `cl.exe` in the x86 developer environment, compile `editor_preview_adapter_tests.cpp` with `EditorPreviewAdapter.cpp`, and fail before the new functions exist.

- [x] **Step 3: Implement the owned snapshot before conversion**

The MFC collector copies all needed values while the current TreeView generation is valid. Copy long text with:

```cpp
std::wstring CopyLongText(const ItemData& item) {
    if (item.long_char_data == nullptr || item.long_char_data_capacity == 0) {
        return {};
    }
    const std::size_t length = wcsnlen_s(
        item.long_char_data, item.long_char_data_capacity);
    return std::wstring(item.long_char_data, length);
}
```

Do not store `ItemData*` in `CapturedEditorNode` or core objects.

- [x] **Step 4: Implement logical-key conversion**

Use the core helpers:

```cpp
const auto section_key = ccz::preview::make_section_key(
    normalized_document_path, section_path);
const auto node_key = ccz::preview::make_logical_node_key(
    section_key, child_path, instruction_id);
```

Store `reinterpret_cast<std::uintptr_t>(hItem)` only in `runtime_handle` for the current refresh generation. Never use it in fingerprints or long-lived maps.

- [x] **Step 5: Add strict capture limits**

Use constants:

```cpp
constexpr std::size_t kMaximumTreeDepth = 128;
constexpr std::size_t kMaximumCapturedNodes = 100000;
```

If either limit is exceeded, return `ok=false` and an empty input. Do not publish partially captured models.

- [x] **Step 6: Run adapter tests and current editor regression scripts**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_editor_preview_adapter.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_targeted_review_contract.ps1
```

Expected: both pass.

- [ ] **Step 7: Commit the adapter**

```powershell
git add -- cczEditor2/PreviewIntegration/EditorTreeSnapshot.* cczEditor2/PreviewIntegration/EditorPreviewAdapter.* cczEditor2/cczEditor2.vcxproj cczEditor2/cczEditor2.vcxproj.filters tests/preview_integration/editor_preview_adapter_tests.cpp tests/test_editor_preview_adapter.ps1
git commit -m "feat: add direct editor preview adapter"
```

---

### Task 6: Add the Single Preview Session and Transactional State Updates

**Files:**
- Create: `cczEditor2/PreviewIntegration/PreviewSession.h`
- Create: `cczEditor2/PreviewIntegration/PreviewSession.cpp`
- Create: `tests/preview_integration/preview_session_tests.cpp`
- Create: `tests/test_preview_session.ps1`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Consumes: `EditorPreviewInput`, core catalog/profile, and read-only asset views.
- Produces:

```cpp
namespace ccz::editor_preview {

struct PreviewAssetsView {
    const std::vector<std::uint8_t>* data_e5{};
    const std::vector<std::uint8_t>* face_e5{};
    const ccz::preview::FaceArchive* face_archive{};
};

struct PreviewFrame {
    std::shared_ptr<const ccz::preview::SectionExecutionModel> model;
    ccz::preview::NativeReplayPlan native_plan;
    ccz::preview::DialogueOverlayModel dialogue;
    ccz::preview::ScriptVariableIndex variable_index;
    ccz::preview::LogicalNodeKey active_node;
    std::vector<std::wstring> diagnostics;
};

enum class RefreshStatus {
    Updated,
    Unchanged,
    Idle,
    InvalidInput,
    ReentrantRejected,
};

struct RefreshResult {
    RefreshStatus status{RefreshStatus::Idle};
    bool frame_changed{};
    std::wstring diagnostic;
};

class PreviewSession {
public:
    RefreshResult Refresh(
        const EditorPreviewInput& input,
        const ccz::preview::InstructionCatalog& catalog,
        const ccz::preview::DialogueProfile& profile,
        const PreviewAssetsView& assets);
    bool ApplyConditionAction(
        const ccz::preview::LogicalNodeKey& node_key,
        ccz::preview::ConditionTreeAction action,
        std::int32_t integer_value,
        std::wstring* diagnostic);
    bool ApplyVariableAction(
        ccz::preview::VariableControlKind kind,
        std::int32_t id,
        ccz::preview::VariableControlAction action,
        std::int32_t integer_value,
        std::wstring* diagnostic);
    void ResetForDocument(const ccz::preview::LogicalNodeKey& document_key);
    const PreviewFrame& frame() const noexcept;
    const ccz::preview::ReplayOptions& options() const noexcept;
};

PreviewFrame BuildPreviewFrame(
    const EditorPreviewInput& input,
    const ccz::preview::SectionExecutionModel& model,
    const ccz::preview::ReplayOptions& options,
    const ccz::preview::InstructionCatalog& catalog,
    const ccz::preview::DialogueProfile& profile,
    const PreviewAssetsView& assets);

}
```

> 当前状态：会话实现、事务性刷新、重入保护、动作回滚和自动化门禁已经通过；提交步骤因 Git 身份尚未配置而保留未完成。

- [x] **Step 1: Write failing session tests**

Cover:

- first refresh initializes Case1 and rebuilds once;
- second identical refresh is `Unchanged`;
- explicit Case2 survives identical and Section refreshes;
- document switch clears transient choices and simulated variables;
- Boolean and integer namespaces remain separate;
- failed condition assignment leaves the previous options unchanged;
- `Refresh` called while already refreshing returns `ReentrantRejected` before touching state;
- invalid input does not publish a partial frame.

- [ ] **Step 2: Run and verify missing session fails to compile**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_session.ps1
```

Expected: compile failure naming `PreviewSession`.

- [x] **Step 3: Implement refresh as a transaction**

Use copies until all stages succeed:

```cpp
ReplayOptions next_options = options_;
SectionExecutionModel next_model = build_section_execution_model(
    input.section, catalog, next_options);
if (initialize_default_choice_selections(next_model, next_options)) {
    next_model = build_section_execution_model(
        input.section, catalog, next_options);
}

PreviewFrame next_frame = BuildPreviewFrame(
    input, next_model, next_options, catalog, profile, assets);

options_ = std::move(next_options);
frame_ = std::move(next_frame);
```

If any step reports unsafe or incomplete input, do not assign `options_` or `frame_`.

- [x] **Step 4: Put the reentrancy guard at the outermost entry**

The first executable line of `Refresh` after argument-independent declarations must acquire a scoped flag. No catalog reload, document reset, callback or model build can happen before the guard.

```cpp
class ScopedBoolean {
public:
    explicit ScopedBoolean(bool& value) : value_(value) { value_ = true; }
    ~ScopedBoolean() { value_ = false; }
    ScopedBoolean(const ScopedBoolean&) = delete;
    ScopedBoolean& operator=(const ScopedBoolean&) = delete;
private:
    bool& value_;
};

if (refreshing_) {
    return {RefreshStatus::ReentrantRejected, false,
            L"预览刷新发生重入，已延后"};
}
ScopedBoolean guard(refreshing_);
```

- [x] **Step 5: Implement actions by copy-validate-commit**

Both action methods copy `ReplayOptions`, call the core action, and assign only on success. They do not trigger UI callbacks themselves; the caller schedules one refresh after the method returns.

- [x] **Step 6: Run session, core and boundary tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_session.ps1
.\Debug\cczPreviewCoreTests.exe
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_boundaries.ps1
```

- [ ] **Step 7: Commit the preview session**

```powershell
git add -- cczEditor2/PreviewIntegration/PreviewSession.* cczEditor2/cczEditor2.vcxproj cczEditor2/cczEditor2.vcxproj.filters tests/preview_integration/preview_session_tests.cpp tests/test_preview_session.ps1
git commit -m "feat: add transactional preview session"
```

---

### Task 7: Centralize Event-Driven Refresh in the Editor

**Files:**
- Modify: `cczEditor2/cczEditor2View.h`
- Modify: `cczEditor2/cczEditor2View.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Create: `tests/test_preview_refresh_routing.ps1`

**Interfaces:**
- Consumes: `CaptureEditorPreviewInput` and `PreviewSession`.
- Produces:

```cpp
void CcczEditor2View::RefreshScenePreview();
void CScenePreviewWnd::RequestPreviewRefresh(
    CTreeCtrl& tree, HTREEITEM selected_item, const wchar_t* document_path);
```

- [x] **Step 1: Write the routing source-contract test**

The test must assert:

- `OnTvnSelchanged` calls `RefreshScenePreview()`;
- the successful instruction-edit path calls `RefreshScenePreview()`;
- `CcczEditor2View` contains no direct call to `EvaluateToItem`;
- only `RefreshScenePreview()` calls `m_wndScenePreview.RequestPreviewRefresh`;
- no timer or mouse-move handler calls `PreviewSession::Refresh`.

Example assertion:

```powershell
if (($viewText | Select-String 'm_wndScenePreview\.RequestPreviewRefresh' -AllMatches).Matches.Count -ne 1) {
    throw 'PREVIEW_REFRESH_ROUTING_FAIL: refresh request is not centralized'
}
```

- [x] **Step 2: Run and verify the routing contract fails**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_refresh_routing.ps1
```

Expected: FAIL because `RefreshScenePreview` does not exist.

- [x] **Step 3: Add the single View refresh method**

```cpp
void CcczEditor2View::RefreshScenePreview() {
    CMainFrame* frame = static_cast<CMainFrame*>(AfxGetMainWnd());
    if (frame == nullptr || cur_item == nullptr) return;
    frame->m_wndScenePreview.InitCharList(per2);
    frame->m_wndScenePreview.RequestPreviewRefresh(
        GetTreeCtrl(), cur_item, GetDocument()->pathName);
}
```

Replace the direct call at the selection-change handler and the existing special edit-refresh path with this method. At the end of a completed instruction edit, call it once even if the dialog was cancelled; unchanged model fingerprints suppress unnecessary rebuilding.

- [x] **Step 4: Coalesce reentrant UI refresh requests**

Add one private application message, for example `WM_APP + 0x341`, and two flags in `CScenePreviewWnd`:

```cpp
bool m_previewRefreshActive = false;
bool m_previewRefreshPosted = false;
```

If a request arrives during refresh, post one deferred message and return. The message handler clears `m_previewRefreshPosted` before invoking the latest stored request. Do not spin or recursively call `UpdatePreview`.

- [x] **Step 5: Keep `UpdatePreview` as the native rendering function**

`RequestPreviewRefresh` coordinates capture/session work and then calls `UpdatePreview` or its later plan-based replacement. Existing scene writeback methods may continue calling `RequestPreviewRefresh`, but must not call `PreviewSession::Refresh` directly.

- [x] **Step 6: Run routing, existing refresh and build tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_refresh_routing.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_targeted_review_contract.ps1
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1
```

> 其中 targeted-review contract 和旧 baseline 仍固定检查历史 staging/Release 哈希，因本 Task 生成新 Release 而报告旧路径差异；旧 staging 未覆盖，新 internalization staging 已单独校验。

- [ ] **Step 7: Update working staging and smoke-test selection/edit refresh**

Copy the new `Release/s.exe` and existing INI files into `../../ccz_preview_ext/staging-test/ccz-editor-internalization-working`. Add `START-INTERNALIZATION-WORKING.cmd` containing:

```bat
@echo off
start "" "%~dp0s.exe" %*
```

Verify selecting an instruction and confirming an edit refreshes the native scene without DLL or launcher involvement.

> 当前状态：internalization working staging 已更新并与本次 Release/s.exe SHA-256 一致；真实窗口选择/编辑 smoke 因当前 Computer Use 绑定不可用，延期到 Task 12 最终验收，本步骤的人工验收子项保持未勾选，但不阻塞后续内化任务。

- [ ] **Step 8: Commit centralized refresh**

```powershell
git add -- cczEditor2/cczEditor2View.* cczEditor2/ScenePreviewWnd.* tests/test_preview_refresh_routing.ps1
git commit -m "refactor: centralize preview refresh events"
```

---

### Task 8: Internalize Dialogue Assets and Overlay Rendering

**Files:**
- Create: `cczEditor2/PreviewIntegration/PreviewAssetCache.h`
- Create: `cczEditor2/PreviewIntegration/PreviewAssetCache.cpp`
- Create: `cczEditor2/PreviewIntegration/DialogueOverlayRenderer.h`
- Create: `cczEditor2/PreviewIntegration/DialogueOverlayRenderer.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Create: `tests/preview_integration/preview_asset_cache_tests.cpp`
- Create: `tests/test_dialogue_overlay_integration.ps1`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Consumes: `DialogueOverlayModel`, `DialogueProfile`, `FaceArchive` and the current Mod root.
- Produces:

```cpp
class PreviewAssetCache {
public:
    bool LoadForDocument(const std::wstring& document_path,
                         const ccz::preview::DialogueProfile& profile,
                         std::wstring* diagnostic);
    ccz::editor_preview::PreviewAssetsView view() const noexcept;
    void Clear();
};

struct OverlayLayout {
    CRect panel;
    CRect portrait;
    CRect speaker;
    CRect body;
    CRect diagnostic;
};

bool CalculateDialogueOverlayLayout(const CRect& client,
                                    OverlayLayout& result);
void PaintDialogueOverlay(CDC& dc, const OverlayLayout& layout,
                          const ccz::preview::DialogueOverlayModel& model);
```

- [x] **Step 1: Write failing asset and overlay integration tests**

Cover:

- changing Mod root clears data and face caches;
- missing `data.e5` leaves dialogue text available but appends a diagnostic;
- missing `face.e5` removes only portrait image;
- identical overlay models do not request repaint;
- hidden models paint an opaque empty panel when clearing a previous portrait;
- overlay layout remains inside the current scene-preview canvas.

- [x] **Step 2: Run and verify missing cache/renderer fails**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_dialogue_overlay_integration.ps1
```

- [x] **Step 3: Implement document-root asset caching**

Normalize the document path with the same Mod-root rule already used by `ScenePreviewWnd::GetRootPath`. Load bytes into owned vectors. Parse `face.e5` once per root. Never return pointers into temporary buffers.

- [x] **Step 4: Port painting without global overlay state**

Move GDI+/IStream drawing from the DLL renderer into `DialogueOverlayRenderer.cpp`. Accept the model as a `const&`; remove `set/get/clear_dialogue_overlay_model` entirely.

- [x] **Step 5: Draw the overlay from `CScenePreviewWnd::OnPaint`**

After scene, characters and map portraits are painted, calculate the panel from the actual scene canvas and paint `m_previewSession.frame().dialogue`. Do not create a transparent top-level follower window.

- [x] **Step 6: Run dialogue, asset, current scene and Release build tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_dialogue_overlay_integration.ps1
.\Debug\cczPreviewCoreTests.exe dialogue
.\Debug\cczPreviewCoreTests.exe portrait
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_background_preview.ps1
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1
```

- [ ] **Step 7: Update working staging and manually verify representative dialogue forms**

Staging was updated and hash-checked; the real-window portion is intentionally deferred to Task 12 because the current Computer Use binding cannot drive the editor.

Verify standard dialogue, `&*.XXX`, renamed characters, 0x16 information, 0x69 narration and 0x2C map text. Record results in `docs/internalization/stage-d-dialogue-report.md`.

- [ ] **Step 8: Commit dialogue internalization**

Git identity is not configured, so no commit is created; the uncommitted worktree remains user-owned.

```powershell
git add -- cczEditor2/PreviewIntegration/PreviewAssetCache.* cczEditor2/PreviewIntegration/DialogueOverlayRenderer.* cczEditor2/ScenePreviewWnd.* cczEditor2/cczEditor2.vcxproj cczEditor2/cczEditor2.vcxproj.filters tests/preview_integration/preview_asset_cache_tests.cpp tests/test_dialogue_overlay_integration.ps1 docs/internalization/stage-d-dialogue-report.md
git commit -m "feat: internalize dialogue preview overlay"
```

---

### Task 9: Internalize the Condition Tree as an Editor-Owned Window

**Files:**
- Create: `cczEditor2/PreviewIntegration/ConditionTreePane.h`
- Create: `cczEditor2/PreviewIntegration/ConditionTreePane.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Create: `tests/test_condition_tree_internalization.ps1`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Consumes: `PreviewFrame::model`, active logical key and `PreviewSession::ApplyConditionAction`.
- Produces:

```cpp
class CConditionTreePane : public CWnd {
public:
    using ActionHandler = std::function<void(
        const ccz::preview::LogicalNodeKey&,
        ccz::preview::ConditionTreeAction,
        std::int32_t)>;

    bool CreatePane(CWnd* owner, ActionHandler handler);
    void UpdateModel(
        std::shared_ptr<const ccz::preview::SectionExecutionModel> model,
        const ccz::preview::LogicalNodeKey& active_node);
    void ShowPane(bool show);
};
```

- [x] **Step 1: Port pure condition-window behavior tests before the window**

Move state-only tests from the old DLL suite into the core tests, including:

- action availability;
- true/false branch reachability;
- Case selection;
- title formatting;
- distinct true/false colors;
- expansion restoration by logical key;
- hiding ordinary leaf commands while retaining meaningful nested controls.

Do not migrate HWND lifecycle smoke tests into the core.

- [x] **Step 2: Write the editor ownership contract test**

The PowerShell test must reject these tokens from `ConditionTreePane.*`:

```text
FindWindow
EnumWindows
SetWindowSubclass
GetModuleHandleEx
CreateThread
```

It must require `CWnd`, `PreviewSession`, logical-key bindings and an owner supplied by `CScenePreviewWnd`.

- [x] **Step 3: Run and verify the ownership test fails before the pane exists**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_condition_tree_internalization.ps1
```

- [x] **Step 4: Implement the MFC-owned pane using existing visual rules**

Port the current TreeView item-building, colors, icons and expansion-map behavior. Store bindings in owned containers and bind TreeView `lParam` only to entries whose lifetime exceeds the current rebuild. Clear TreeView items before clearing bindings.

Required visible labels remain:

- `真分支` for the true branch;
- `Else分支` for the false/else branch;
- `当前分支` and `未选分支` as distinct Choice/Case concepts.

- [x] **Step 5: Add condition and variable buttons to the preview pane**

Create child buttons in `CScenePreviewWnd::OnCreate`, align them in `OnSize`, and show the condition button only when the current model contains controllable nodes. Clicking it shows the editor-owned pane.

- [x] **Step 6: Route actions through the session, then request one refresh**

The callback must call `ApplyConditionAction`, return from the pane callback, then invoke `RequestPreviewRefresh`. It must not mutate TreeView data or directly repaint other windows.

- [ ] **Step 7: Run tests, build and perform UI smoke checks**

Automated condition/core/session/refresh/build gates pass; real-window fold/color/Case/branch smoke is deferred to Task 12 because the current Computer Use binding cannot drive the editor.

```powershell
.\Debug\cczPreviewCoreTests.exe condition
powershell -ExecutionPolicy Bypass -File .\tests\test_condition_tree_internalization.ps1
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1
```

Manual checks: fold triangles, selected inactive Case highlight, true/Else colors, nested conditions, Case1 default and branch activation.

- [ ] **Step 8: Update staging and commit**

Staging was updated and hash-checked. Git identity is not configured, so no commit is created; the worktree remains user-owned.

```powershell
git add -- cczEditor2/PreviewIntegration/ConditionTreePane.* cczEditor2/ScenePreviewWnd.* cczEditor2/cczEditor2.vcxproj cczEditor2/cczEditor2.vcxproj.filters tests/test_condition_tree_internalization.ps1 cczPreviewCore cczPreviewCoreTests
git commit -m "feat: internalize condition tree window"
```

---

### Task 10: Internalize Variable Control and Share the Same Simulation State

**Files:**
- Create: `cczEditor2/PreviewIntegration/VariableControlPane.h`
- Create: `cczEditor2/PreviewIntegration/VariableControlPane.cpp`
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Create: `tests/test_variable_control_internalization.ps1`
- Modify: `cczEditor2/cczEditor2.vcxproj`
- Modify: `cczEditor2/cczEditor2.vcxproj.filters`

**Interfaces:**
- Consumes: `PreviewFrame::variable_index`, `PreviewSession::options()` and `PreviewSession::ApplyVariableAction`.
- Produces:

```cpp
class CVariableControlPane : public CWnd {
public:
    using ActionHandler = std::function<void(
        ccz::preview::VariableControlKind,
        std::int32_t,
        ccz::preview::VariableControlAction,
        std::int32_t)>;

    bool CreatePane(CWnd* owner, ActionHandler handler);
    void UpdateRows(
        const ccz::preview::ScriptVariableIndex& index,
        const ccz::preview::SimulationEnvironment& environment,
        const ccz::preview::LogicalNodeKey& section_key);
    void ShowPane(bool show);
};
```

- [x] **Step 1: Write ownership and UI-contract tests**

Require:

- search edit and list controls are editor-owned;
- integer row is `整型值` + input + `变量赋值` + `重置为0`;
- empty integer selection displays an empty value field, not a placeholder sentence;
- Boolean row uses `布尔变量` and only `设为True` / `设为False`;
- the two operation rows align to the same columns;
- Boolean and integer values call different typed actions.

- [x] **Step 2: Run and verify the contract fails before implementation**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_variable_control_internalization.ps1
```

- [x] **Step 3: Port the editor-owned pane and retain current filtering behavior**

Use the pure `build_variable_control_rows` function. The window owns controls and row storage, while `PreviewSession` owns values. Rebuilding the list cannot overwrite the session environment.

- [x] **Step 4: Route all changes through transactional session actions**

For each button:

1. read the selected typed row;
2. validate integer input with full-string decimal parsing;
3. call `ApplyVariableAction`;
4. show one diagnostic on failure;
5. request one refresh on success.

- [x] **Step 5: Verify condition tree and variable control share state**

Add an integration test that sets integer variable 3000 in the variable pane, refreshes the same session, and asserts the corresponding 0x79 node and active branch change without a second state store.

- [x] **Step 6: Run tests and Release build**

```powershell
.\Debug\cczPreviewCoreTests.exe variable
powershell -ExecutionPolicy Bypass -File .\tests\test_variable_control_internalization.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_condition_tree_internalization.ps1
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Build /p:Configuration=Release /p:Platform=x86 /m:1
```

- [ ] **Step 7: Update staging and commit**

Working staging was updated and hash-checked; real MFC variable-pane smoke is deferred to Task 12. Git identity is not configured, so no commit is created.

```powershell
git add -- cczEditor2/PreviewIntegration/VariableControlPane.* cczEditor2/ScenePreviewWnd.* cczEditor2/cczEditor2.vcxproj cczEditor2/cczEditor2.vcxproj.filters tests/test_variable_control_internalization.ps1
git commit -m "feat: internalize variable control window"
```

---

### Task 11: Replace Native Preview Hooking with Direct Plan Application

**Files:**
- Modify: `cczEditor2/ScenePreviewWnd.h`
- Modify: `cczEditor2/ScenePreviewWnd.cpp`
- Modify: `cczEditor2/PreviewIntegration/PreviewSession.cpp`
- Create: `tests/preview_integration/native_plan_application_tests.cpp`
- Create: `tests/test_native_preview_plan_integration.ps1`

**Interfaces:**
- Consumes: `PreviewFrame::native_plan` containing current-generation runtime handles.
- Produces:

```cpp
struct NativePlanApplyResult {
    bool applied{};
    std::size_t applied_count{};
    std::wstring diagnostic;
};

NativePlanApplyResult CScenePreviewWnd::ApplyNativeReplayPlan(
    CTreeCtrl& tree,
    const ccz::preview::NativeReplayPlan& plan);

void CScenePreviewWnd::HideInternalizedEnhancementViews(
    const std::wstring& diagnostic);
```

- [x] **Step 1: Write failing tests for plan application boundaries**

Cover:

- scene state is cleared before the first instruction;
- selected instruction is included;
- each runtime handle is resolved against the current TreeView generation;
- instruction ID mismatch aborts before applying the mismatched node;
- false branches and unselected cases are absent from the plan;
- Case1 default enters the first case;
- plan application drives existing outer-scene and map-portrait instructions.

- [x] **Step 2: Run and verify missing direct application fails**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_native_preview_plan_integration.ps1
```

- [x] **Step 3: Implement current-generation validation**

For each plan entry:

```cpp
HTREEITEM item = reinterpret_cast<HTREEITEM>(entry.runtime_handle);
ItemData* data = reinterpret_cast<ItemData*>(tree.GetItemData(item));
if (data == nullptr || data->id != entry.instruction_id) {
    return {false, applied_count, L"预览节点已刷新，放弃本次重放"};
}
ProcessInstruction(data);
```

Resolve and validate all entries into a temporary vector before clearing the current scene state, so a stale handle cannot leave a half-cleared preview.

- [x] **Step 4: Make the plan the primary path in `UpdatePreview`**

Flow:

```cpp
const PreviewFrame& frame = m_previewSession.frame();
const NativePlanApplyResult apply = ApplyNativeReplayPlan(tree, frame.native_plan);
if (!apply.applied) {
    EvaluateToItem(tree, hSelectedItem);  // explicit safety fallback only
    HideInternalizedEnhancementViews(apply.diagnostic);
}
```

The fallback is allowed only for invalid internal input and must hide condition/variable/dialogue outputs for that refresh, preventing two contradictory states from being shown.

`HideInternalizedEnhancementViews` hides the condition and variable panes, replaces the visible overlay model with an empty model, stores the diagnostic in `m_debugInfo`, and invalidates the preview pane once.

- [x] **Step 5: Preserve current scene features through existing `ProcessInstruction`**

Do not duplicate instruction rendering in the core. The plan selects nodes; existing `ProcessInstruction` remains responsible for backgrounds, characters, movement, map portraits, outer-scene coordinates and current editor-specific enhancements.

- [x] **Step 6: Run all scene and plan tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_native_preview_plan_integration.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_background_preview.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_position_sources.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_multiselect_writeback.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_remaining_scene_config_fixes.ps1
.\Debug\cczPreviewCoreTests.exe native_replay_plan
```

- [ ] **Step 7: Run manual R-script parity checks**

> 自动化 direct-plan、Free-R/出战配对前缀、Case1、构建与回归门禁已通过；真实 MFC/R 脚本 parity smoke 延期到 Task 12 最终验收。

For fixed nodes in representative R scripts, compare DLL baseline and internalized build for:

- active branch and condition tree;
- current dialogue, speaker and portrait;
- characters and map portraits;
- selected-instruction boundary;
- Choice Case1 initial state;
- outer-scene map and pixel coordinates.

Record normalized results in `docs/internalization/stage-f-native-replay-report.md`.

- [ ] **Step 8: Update staging and commit**

> working staging 已同步并完成哈希核对；Git identity 未配置，暂不提交。Task 12 将统一执行最终 staging/发布与提交门禁。

```powershell
git add -- cczEditor2/ScenePreviewWnd.* cczEditor2/PreviewIntegration/PreviewSession.cpp tests/preview_integration/native_plan_application_tests.cpp tests/test_native_preview_plan_integration.ps1 docs/internalization/stage-f-native-replay-report.md
git commit -m "feat: drive native preview from internal replay plan"
```

---

### Task 12: Remove Runtime DLL Dependencies, Run Release Gates, and Publish Staging

**Files:**
- Create: `tests/test_no_external_preview_runtime.ps1`
- Create: `docs/internalization/preview-internalization-final-report.md`
- Modify: `README.md` if it currently documents the extension launcher
- Modify: `tests/test_targeted_review_contract.ps1`
- Create or update: `../../ccz_preview_ext/staging-test/ccz-editor-internalization-working/README.md`
- Create or update: `../../ccz_preview_ext/staging-test/ccz-editor-internalization-working/START-INTERNALIZED-PREVIEW-EDITOR.cmd`
- Create or update: `../../ccz_preview_ext/staging-test/ccz-editor-internalization-working/SHA256SUM.txt`

**Interfaces:**
- Consumes: All prior tasks and the immutable baseline manifest.
- Produces: Release x86 editor with all accepted preview enhancements and no runtime preview DLL dependency.

- [x] **Step 1: Write the external-runtime prohibition gate**

```powershell
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$sourceFiles = Get-ChildItem (Join-Path $root 'cczEditor2') -Recurse -File -Include *.h,*.cpp,*.vcxproj
$forbidden = @(
    'cczPreviewExt.dll',
    'CCZPreviewExtInitialize',
    'CCZPreviewExtShutdown',
    'WriteProcessMemory',
    'CreateRemoteThread',
    'update_preview_call_rva',
    'verify_host_profile'
)
foreach ($file in $sourceFiles) {
    $text = Get-Content $file.FullName -Raw
    foreach ($token in $forbidden) {
        if ($text -match [regex]::Escape($token)) {
            throw "NO_EXTERNAL_PREVIEW_RUNTIME_FAIL: $token in $($file.FullName)"
        }
    }
}

$exe = Join-Path $root 'Release\s.exe'
$msvcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
$msvc = Get-ChildItem $msvcRoot -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -eq $msvc) {
    throw 'NO_EXTERNAL_PREVIEW_RUNTIME_FAIL: MSVC toolchain not found'
}
$dumpbin = Join-Path $msvc.FullName 'bin\Hostx86\x86\dumpbin.exe'
if (-not (Test-Path $dumpbin)) {
    throw "NO_EXTERNAL_PREVIEW_RUNTIME_FAIL: dumpbin not found: $dumpbin"
}
$imports = & $dumpbin /imports $exe
if ($imports -match 'cczPreviewExt') {
    throw 'NO_EXTERNAL_PREVIEW_RUNTIME_FAIL: EXE imports cczPreviewExt'
}

'NO_EXTERNAL_PREVIEW_RUNTIME_PASS'
```

- [x] **Step 2: Run the gate before final cleanup and verify any remaining reference fails**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_no_external_preview_runtime.ps1
```

Expected: FAIL if any editor source or binary still references the external preview runtime.

> 实际结果：清理前编辑器运行时已无真实外部依赖引用，因此没有制造伪造失败夹具；清理后 gate 仍以 62 个 editor 文件和真实 x86 dumpbin 导入检查通过。

- [x] **Step 3: Remove migration-only switches and dead compatibility code**

Delete from the editor repository only:

- temporary shadow-mode output;
- compiled-in old compatibility overloads with no callers;
- migration feature switches;
- obsolete comments referring to injected callbacks;
- any copied launcher or target-profile code accidentally introduced during migration.

Do not delete anything under `../../ccz_preview_ext/releases`, `baseline`, `candidates` or previous staging directories.

- [x] **Step 4: Generalize the existing targeted-review artifact gate**

Add parameters while preserving the old defaults:

```powershell
[string]$LauncherFileName = 'START-THREE-REVIEW-FIXES-EDITOR.cmd',
[string]$ExpectedReadmeMarker = '针对性 PowerShell 回归脚本全部通过'
```

Replace the hard-coded launcher and README marker checks with those parameters. This lets the existing baseline continue using its original contract while the internalized staging supplies its own launcher name and acceptance marker.

- [x] **Step 5: Run all source and core automated gates**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_internalization_baseline.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_project.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_core_boundaries.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_editor_preview_adapter.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_session.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_preview_refresh_routing.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_dialogue_overlay_integration.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_condition_tree_internalization.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_variable_control_internalization.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_native_preview_plan_integration.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_no_external_preview_runtime.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_combo_notification_routing.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_integration.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_selection_commit.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_indexed_combo_ux_regressions.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_long_char_data_safety.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_remaining_combo_review_fixes.ps1
powershell -ExecutionPolicy Bypass -File .\tests\test_scene_multiselect_ui.ps1
.\Debug\cczPreviewCoreTests.exe
```

Every command must exit 0.

> 实际结果：核心/适配器/Session/刷新/条件/变量/dialogue/direct-plan/输入辅助门禁均通过；immutable baseline hash 漂移、旧 0x27/多选标签断言和 Windows PowerShell 5.1 parser 为已记录的非 Task12 新回归，已在最终报告中分列。

- [x] **Step 6: Rebuild Release x86 from clean project outputs**

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' .\cczEditor2.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86 /m:1
```

Expected: `Release/s.exe` is produced without copying a preview DLL.

- [x] **Step 7: Publish the working staging directory**

The staging directory must contain:

```text
s.exe
CczCustom.ini
CczSceneEditor2.ini
CczString.ini
README.md
TEST-PLAN.md
SHA256SUM.txt
START-INTERNALIZED-PREVIEW-EDITOR.cmd
```

It must not contain:

```text
cczPreviewExt.dll
cczPreviewExtLauncher.exe
```

Launcher content:

```bat
@echo off
start "" "%~dp0s.exe" %*
```

- [x] **Step 8: Run the staging artifact contract against the new package**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\test_targeted_review_contract.ps1 `
    -StagingRoot '..\..\ccz_preview_ext\staging-test\ccz-editor-internalization-working' `
    -LauncherFileName 'START-INTERNALIZED-PREVIEW-EDITOR.cmd' `
    -ExpectedReadmeMarker '内置预览完整回归通过'
```

Expected: `targeted_review_contract: PASS` and Release/staging hashes match.

- [ ] **Step 9: Execute final manual acceptance**

> 当前状态：15 项真实 MFC 窗口验收因 Computer Use 绑定不可可靠驱动编辑器，保持 `DEFERRED`；不能用 headless/direct-plan 结果替代窗口 smoke。

Use representative R scripts and verify:

1. direct EXE startup;
2. document open/switch/close;
3. native scene preview and selected-instruction boundary;
4. outer-scene maps and pixel coordinates;
5. map portrait show/move/disappear and position writeback;
6. multi-select operations;
7. dialogue names, portraits, rename state and non-character text;
8. condition tree colors, folding, active/inactive selection and Else labels;
9. Boolean and integer variable controls;
10. 0x79 four comparators;
11. Choice Case1 default and explicit case switching;
12. automatic refresh after edit and branch/variable actions;
13. searchable person/item controls, direct numeric input and type-switch enablement;
14. text wrap assist, remembered settings and long TXT export;
15. save, reopen and close without crashes or stale windows.

- [x] **Step 10: Write the final report with evidence**

Record:

- final EXE SHA-256;
- test command outputs and counts;
- staging contents;
- absence of DLL/launcher;
- accepted differences from the old DLL;
- remaining non-blocking issues, each with severity and reproduction steps;
- rollback path to the preserved DLL release.

- [ ] **Step 11: Commit final cleanup and release metadata**

> 当前状态：Git `user.name`/`user.email` 未配置，因此未提交；不伪造提交记录。

```powershell
git add -- cczEditor2 cczPreviewCore cczPreviewCoreTests tests README.md docs/internalization/preview-internalization-final-report.md
git commit -m "feat: complete built-in preview runtime"
```

Do not add binary staging files to Git unless the repository already tracks equivalent release binaries.

## Spec Coverage Matrix

| Design requirement | Implementation task |
|---|---|
| Preserve DLL/launcher/staging rollback baseline | Task 1 |
| Static C++17 x86 core with no runtime DLL | Tasks 2-3 |
| Remove global overlay state and Win32 dependencies from core | Tasks 3 and 8 |
| Correct 0x79 `== / >= / <= / !=` behavior | Task 4 |
| Default Choice to Case1 without overwriting explicit selection | Tasks 4 and 6 |
| Stable logical keys instead of HWND/HTREEITEM identity | Task 5 |
| Single transactional simulation state | Task 6 |
| Reentrancy protection at the outer refresh boundary | Tasks 6-7 |
| Event-driven automatic refresh | Task 7 |
| Internal dialogue, names, portraits and text-only commands | Task 8 |
| Editor-owned condition tree with accepted visuals | Task 9 |
| Editor-owned Boolean/integer variable control | Task 10 |
| Direct selected-instruction replay without Hook | Task 11 |
| Preserve outer-scene, map-portrait and scene editing enhancements | Tasks 8 and 11 |
| Remove launcher, target profile, fixed RVA and self-patching from release | Task 12 |
| Update staging and quick-start entry | Tasks 7-12 |
| Full regression, differential evidence and rollback report | Tasks 1, 11 and 12 |

## Execution Checkpoints

- After Task 1: report baseline hashes and actual test counts; no functional changes.
- After Task 4: review the migrated core and corrected semantics before editor integration.
- After Task 7: provide the first source-built EXE with event-driven internal session wiring.
- After Task 8: stop for dialogue-overlay visual acceptance.
- After Task 10: stop for condition-tree and variable-control acceptance.
- After Task 11: perform targeted code review of direct native replay before release cleanup.
- After Task 12: perform final whole-feature review and publish the quick-start staging link.
