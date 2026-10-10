$core = Get-ChildItem (Join-Path $PSScriptRoot '..\cczPreviewCore') -Recurse -File -Include *.h,*.cpp
$forbidden = @(
    'afxwin.h', 'CTreeCtrl', 'HTREEITEM', 'HWND', 'HDC', 'RECT', 'gdiplus',
    'VirtualProtect', 'SetWindowLongPtr', 'CreateThread', 'windows.h',
    'bridge/', 'launcher/', 'ui/', 'preview_runtime/'
)
foreach ($file in $core) {
    $text = Get-Content $file.FullName -Raw
    foreach ($token in $forbidden) {
        # Use literal substring matching.  Regex -match makes short tokens
        # such as RECT collide with ordinary prose (for example “correctly”).
        if ($text.Contains($token)) {
            throw "PREVIEW_CORE_BOUNDARY_FAIL: $token in $($file.FullName)"
        }
    }
}
'PREVIEW_CORE_BOUNDARY_PASS'
