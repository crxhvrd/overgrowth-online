# Start a program with its window shown but NOT activated, so keys typed in
# other apps never land in it (the sandbox game used to take focus; an Esc
# typed elsewhere then quit test runs).
#   powershell -File start_noactivate.ps1 -Exe <path> -Dir <workdir> -CmdArgs "<args>"
param([string]$Exe, [string]$Dir, [string]$CmdArgs, [int]$Slot = -1)
# -Slot n: tile the window into the lower-right corner (slot 0 rightmost) and
# keep it at the bottom of the window stack, so test windows stay out of the way

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AioStart {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit,
        int flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AioFocus {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$prev = [AioFocus]::GetForegroundWindow()
$si = New-Object AioStart+STARTUPINFO
$si.cb = [System.Runtime.InteropServices.Marshal]::SizeOf($si)
$si.dwFlags = 1          # STARTF_USESHOWWINDOW
$si.wShowWindow = 4      # SW_SHOWNOACTIVATE: the first ShowWindow call uses this
$pi = New-Object AioStart+PROCESS_INFORMATION
$cmd = "`"$Exe`" $CmdArgs"

if (-not [AioStart]::CreateProcess($Exe, $cmd, [IntPtr]::Zero, [IntPtr]::Zero, $false, 0, [IntPtr]::Zero, $Dir, [ref]$si, [ref]$pi)) {
    Write-Error "CreateProcess failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    exit 1
}

[AioStart]::CloseHandle($pi.hThread) | Out-Null
[AioStart]::CloseHandle($pi.hProcess) | Out-Null
Write-Output "started pid=$($pi.dwProcessId)"

# The game still forces itself to the front once its window is up: for the
# next 30 s, whenever it holds the focus, hand it back to the window that had
# it before the launch (attached input lets a background process do that).
$placed = $false
$deadline = (Get-Date).AddSeconds(30)

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    $proc = Get-Process -Id $pi.dwProcessId -ErrorAction SilentlyContinue
    if (-not $proc) { break }
    $game = $proc.MainWindowHandle
    $fg = [AioFocus]::GetForegroundWindow()

    if ($Slot -ge 0 -and $game -ne [IntPtr]::Zero) {
        # SWP_NOSIZE | SWP_NOACTIVATE, placed behind everything (HWND_BOTTOM)
        $r = New-Object AioFocus+RECT
        [AioFocus]::GetWindowRect($game, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $h = $r.B - $r.T
        $sw = [AioFocus]::GetSystemMetrics(0); $sh = [AioFocus]::GetSystemMetrics(1)
        $col = $Slot % 3; $row = [math]::Floor($Slot / 3)
        $x = $sw - ($col + 1) * ($w + 8); $y = $sh - 48 - ($row + 1) * ($h + 8)
        if (-not $placed) {
            [AioFocus]::SetWindowPos($game, [IntPtr]1, $x, $y, 0, 0, 0x0011) | Out-Null
            $placed = $true
        } else {
            [AioFocus]::SetWindowPos($game, [IntPtr]1, 0, 0, 0, 0, 0x0013) | Out-Null
        }
    }

    if ($prev -eq [IntPtr]::Zero) { continue }

    if ($game -ne [IntPtr]::Zero -and $fg -eq $game) {
        $pidOut = 0
        $gameThread = [AioFocus]::GetWindowThreadProcessId($game, [ref]$pidOut)
        $myThread = [AioFocus]::GetCurrentThreadId()
        [AioFocus]::AttachThreadInput($myThread, $gameThread, $true) | Out-Null
        [AioFocus]::SetForegroundWindow($prev) | Out-Null
        [AioFocus]::AttachThreadInput($myThread, $gameThread, $false) | Out-Null
        Write-Output "focus returned"
    }
}
