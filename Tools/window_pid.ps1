# Resize, minimise or restore one process's game window without taking focus
# (the renderer's swapchain tests: resizes, minimised windows). The game window is
# the process's SDL window (class SDL_app), not its console window.
#   powershell -File window_pid.ps1 -ProcessId <pid> -Op resize -Width 1024 -Height 600
#   powershell -File window_pid.ps1 -ProcessId <pid> -Op minimize | restore
param([int]$ProcessId, [string]$Op, [int]$Width = 0, [int]$Height = 0)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class OgWinOps {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hgt, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public static IntPtr GameWindow(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, p) => {
            uint pid;
            GetWindowThreadProcessId(h, out pid);
            if (pid != processId) return true;
            var name = new StringBuilder(64);
            GetClassName(h, name, name.Capacity);
            if (name.ToString() != "SDL_app") return true;
            found = h;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
"@

[OgWinOps]::SetProcessDPIAware() | Out-Null
$handle = [OgWinOps]::GameWindow([uint32]$ProcessId)
if ($handle -eq [IntPtr]::Zero) {
    Write-Error "no game window"
    exit 1
}

function RectText() {
    $r = New-Object OgWinOps+RECT
    [OgWinOps]::GetWindowRect($handle, [ref]$r) | Out-Null
    return "$($r.R - $r.L)x$($r.B - $r.T)"
}

$SWP_NOMOVE = 0x0002
$SWP_NOZORDER = 0x0004
$SWP_NOACTIVATE = 0x0010
$before = RectText
switch ($Op) {
    "resize" { $ok = [OgWinOps]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, $Width, $Height, $SWP_NOMOVE -bor $SWP_NOZORDER -bor $SWP_NOACTIVATE) }
    "minimize" { $ok = [OgWinOps]::ShowWindow($handle, 7) }  # SW_SHOWMINNOACTIVE
    "restore" { $ok = [OgWinOps]::ShowWindow($handle, 4) }  # SW_SHOWNOACTIVATE
    default { Write-Error "unknown op $Op"; exit 1 }
}
Start-Sleep -Milliseconds 300
Write-Output "$Op window 0x$($handle.ToString('X')) returned $ok, $before -> $(RectText)"
