# Capture one process's main window to a PNG without bringing it to the front
# (PrintWindow with PW_RENDERFULLCONTENT), so focus stays where it is.
#   powershell -File capture_pid.ps1 -ProcessId <pid> -Out <path.png>
param([int]$ProcessId, [string]$Out)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OgWin {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing

[OgWin]::SetProcessDPIAware() | Out-Null
# (the process's SDL window, class SDL_app: its "main window" is sometimes its console)
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class OgWinFind {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
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
$handle = [OgWinFind]::GameWindow([uint32]$ProcessId)
if ($handle -eq [IntPtr]::Zero) {
    $handle = (Get-Process -Id $ProcessId -ErrorAction Stop).MainWindowHandle
}

if ($handle -eq [IntPtr]::Zero) {
    Write-Error "no window"
    exit 1
}

$rect = New-Object OgWin+RECT
[OgWin]::GetWindowRect($handle, [ref]$rect) | Out-Null
$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
[OgWin]::PrintWindow($handle, $hdc, 2) | Out-Null
$graphics.ReleaseHdc($hdc)
$bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Output "saved $Out"
