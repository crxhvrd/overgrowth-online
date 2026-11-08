param([int]$ProcessId, [double]$Seconds = 5.0, [string]$Out = "")
# Every thread's CPU cycles over a few seconds (QueryThreadCycleTime: the cycle counter, not
# the clock tick), as a share of one core: one second of a busy loop here gives a core's cycles.
# (Not Process.TotalProcessorTime: tick-based, it showed the game's busy main thread at 18%.)
# Used by Tools/ogmp/perf_suite.py; by hand: powershell -File Tools/thread_cycles.ps1 -ProcessId <pid>
$src = @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class Cycles {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(int access, bool inherit, int id);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool QueryThreadCycleTime(IntPtr h, out ulong cycles);
    [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentThread();
    [DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(IntPtr h, int cls, out IntPtr info, int len, IntPtr ret);
    public static ulong Of(int id) {
        IntPtr h = OpenThread(0x0800, false, id);  // THREAD_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return 0;
        ulong c; bool ok = QueryThreadCycleTime(h, out c);
        CloseHandle(h);
        return ok ? c : 0;
    }
    public static long StartAddress(int id) {
        IntPtr h = OpenThread(0x0040, false, id);
        if (h == IntPtr.Zero) return 0;
        IntPtr addr; int r = NtQueryInformationThread(h, 9, out addr, IntPtr.Size, IntPtr.Zero);
        CloseHandle(h);
        return r == 0 ? addr.ToInt64() : 0;
    }
    // One second of a busy loop on this thread: a busy core's cycles per second
    public static double BusyCyclesPerSecond() {
        ulong a, b; QueryThreadCycleTime(GetCurrentThread(), out a);
        var sw = Stopwatch.StartNew(); double x = 0;
        while (sw.ElapsedMilliseconds < 1000) { x += Math.Sqrt(x + 1.0); }
        QueryThreadCycleTime(GetCurrentThread(), out b);
        return (b - a) / sw.Elapsed.TotalSeconds + (x < 0 ? 1 : 0);
    }
}
"@
Add-Type -TypeDefinition $src -Language CSharp

$core = [Cycles]::BusyCyclesPerSecond()
$proc = Get-Process -Id $ProcessId
$modules = @($proc.Modules | ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Base = $_.BaseAddress.ToInt64(); End = $_.BaseAddress.ToInt64() + $_.ModuleMemorySize } })
function ModuleOf([long]$a) { foreach ($m in $modules) { if ($a -ge $m.Base -and $a -lt $m.End) { return $m.Name } }; return "?" }

$before = @{}
foreach ($t in $proc.Threads) { $before[$t.Id] = [Cycles]::Of($t.Id) }
$sw = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Milliseconds ([int]($Seconds * 1000))
$after = @{}
$proc.Refresh()
foreach ($t in $proc.Threads) { $after[$t.Id] = [Cycles]::Of($t.Id) }
$wall = $sw.Elapsed.TotalSeconds

$rows = foreach ($id in $after.Keys) {
    $was = if ($before.ContainsKey($id)) { $before[$id] } else { 0 }
    [pscustomobject]@{ Id = $id; Core = [math]::Round(100.0 * ($after[$id] - $was) / $wall / $core, 1); Module = ModuleOf ([Cycles]::StartAddress($id)) }
}
$total = ($rows | Measure-Object -Property Core -Sum).Sum
"busy core: {0:N2} GHz; process: {1:N2} cores of {2}; {3} threads" -f ($core / 1e9), ($total / 100.0), [Environment]::ProcessorCount, $rows.Count
$sorted = @($rows | Sort-Object Core -Descending)
if ($Out) { [pscustomobject]@{ BusyGHz = $core / 1e9; Cores = $total / 100.0; Threads = $sorted } | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Out }
$sorted | Select-Object -First 15 | Format-Table -AutoSize
