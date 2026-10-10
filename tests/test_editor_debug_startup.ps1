[CmdletBinding()]
param(
    [string]$EditorExe
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($EditorExe)) {
    $editorRoot = Split-Path -Parent $PSScriptRoot
    $candidates = @()
    $ancestor = Get-Item -LiteralPath $editorRoot
    while ($null -ne $ancestor) {
        $qaRoot = Join-Path $ancestor.FullName '.tmp-s-battle'
        if (Test-Path -LiteralPath $qaRoot -PathType Container) {
            $candidates += Join-Path $qaRoot 'writeback-qa-debug\bin\s.exe'
            $candidates += Get-ChildItem -LiteralPath $qaRoot -Directory -Filter 'viewclass-fix-debug-*' |
                ForEach-Object { Join-Path $_.FullName 'bin\s.exe' }
        }
        $ancestor = $ancestor.Parent
    }
    $EditorExe = $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } -Descending |
        Select-Object -First 1
    if (-not $EditorExe) {
        throw 'EDITOR_STARTUP_FAIL: Debug editor missing; pass -EditorExe to a Debug|Win32 build.'
    }
}

if (-not (Test-Path -LiteralPath $EditorExe -PathType Leaf)) {
    throw "EDITOR_STARTUP_FAIL: editor executable missing ($EditorExe)"
}
$EditorExe = (Resolve-Path -LiteralPath $EditorExe).Path

if (-not ('CczStartupWindowProbe' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class CczStartupWindowProbe
{
    private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static string[] GetVisibleWindowTitles(int targetProcessId)
    {
        var titles = new List<string>();
        EnumWindows(delegate(IntPtr window, IntPtr parameter)
        {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId == (uint)targetProcessId && IsWindowVisible(window))
            {
                var text = new StringBuilder(512);
                GetWindowText(window, text, text.Capacity);
                titles.Add(text.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return titles.ToArray();
    }

    public static void RequestCloseToVisibleWindows(int targetProcessId)
    {
        EnumWindows(delegate(IntPtr window, IntPtr parameter)
        {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId == (uint)targetProcessId && IsWindowVisible(window))
                PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero); // WM_CLOSE
            return true;
        }, IntPtr.Zero);
    }
}
'@
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $EditorExe
$startInfo.WorkingDirectory = Split-Path -Parent $EditorExe
$process = [System.Diagnostics.Process]::Start($startInfo)
if ($null -eq $process) {
    throw 'EDITOR_STARTUP_FAIL: Debug editor process did not start.'
}

try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $applicationWindowSeen = $false
    $applicationWindowSeenAt = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) {
            throw "EDITOR_STARTUP_FAIL: Debug editor exited during startup (exit $($process.ExitCode))."
        }

        $titles = @([CczStartupWindowProbe]::GetVisibleWindowTitles($process.Id))
        $assertionTitle = $titles | Where-Object { $_ -like '*Microsoft Visual C++ Runtime Library*' } | Select-Object -First 1
        if ($assertionTitle) {
            throw "EDITOR_STARTUP_FAIL: MFC debug assertion dialog appeared ($assertionTitle)."
        }

        if ($titles | Where-Object { $_ -like '*cczSceneEditor2*' }) {
            if (-not $applicationWindowSeen) {
                $applicationWindowSeen = $true
                $applicationWindowSeenAt = [DateTime]::UtcNow
            }
        }
        if ($applicationWindowSeen -and
            ([DateTime]::UtcNow - $applicationWindowSeenAt).TotalSeconds -ge 3) {
            break
        }
        Start-Sleep -Milliseconds 200
    }

    if (-not $applicationWindowSeen) {
        $lastTitles = @([CczStartupWindowProbe]::GetVisibleWindowTitles($process.Id)) -join ' | '
        throw "EDITOR_STARTUP_FAIL: cczSceneEditor2 main window was not observed; visible windows: $lastTitles"
    }

    Write-Output '[PASS] Debug editor reaches its main window without an MFC assertion dialog.'
    Write-Output 'EDITOR_STARTUP_PASS'
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        [CczStartupWindowProbe]::RequestCloseToVisibleWindows($process.Id)
        if (-not $process.WaitForExit(2000)) {
            $process.Refresh()
            if (-not $process.HasExited -and
                [System.IO.Path]::GetFullPath($process.MainModule.FileName) -eq $EditorExe) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
            }
        }
    }
    $process.Dispose()
}
