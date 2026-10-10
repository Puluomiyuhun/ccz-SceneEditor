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
