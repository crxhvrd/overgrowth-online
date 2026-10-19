"""Crash reports: every way the game dies leaves a report.

For each kind (an access violation on another thread, one on the main thread,
abort(), a stack overflow) an instance crashes on purpose through the test
bridge. Its write folder must then hold crashes/crash_*.dmp (cdb opens it at
the crash), a .txt naming the exception and a copy of the log, and the log
must end with the bridge's "crashing" line followed by the crash line (no
lines lost in a buffer). One instance also times 3000 log lines (each one is
flushed to the OS now). WER's own dumps of these crashes (LocalDumps on this
PC) are deleted afterwards.

    python -I Tools/ogmp/crash_report_check.py
"""

import glob
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ogmp.instance import Instance, user_game_running  # noqa: E402
from ogmp.scenarios import LEVEL  # noqa: E402

CDB = "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe"
SYMBOLS = "D:/OvergrowthMP/Build/Release"
WER_DUMPS = os.path.expandvars("%LOCALAPPDATA%/CrashDumps")
# kind: (code in the summary, on the main thread, a frame the dump's stack must show)
KINDS = {
    "main": ("0xC0000005", True, "CrashReportTestCrash"),
    "thread": ("0xC0000005", False, "CrashingThread"),
    "abort": ("0xE0000001", True, "abort"),
    "stack": ("0xC00000FD", True, "Recurse"),
}


def log_cost(inst):
    """Microseconds per log line (minus building the same strings)."""
    inst.bridge.call("load_level", path="Data/Levels/" + LEVEL)
    inst.bridge.wait(lambda: inst.bridge.status().get("level_loaded"), 150, step=1.0, what="level loaded")
    time.sleep(2.0)
    times = {}
    for name, body in (("build", 'string t = "flush cost " + i;'), ("log", 'Log(info, "flush cost " + i);')):
        t0 = time.perf_counter()
        inst.bridge.call("exec", target="level", code=f"for(int i = 0; i < 3000; ++i) {{ {body} }}", timeout=60)
        times[name] = time.perf_counter() - t0
    return round((times["log"] - times["build"]) / 3000 * 1e6, 1)


def check(kind, index):
    code, main, frame = KINDS[kind]
    r, fails = {"kind": kind}, []
    inst = Instance(f"crashcheck_{kind}", index=index)
    inst.start()
    if kind == "main":
        r["log_line_us"] = log_cost(inst)
    pid = inst.pid
    try:
        inst.bridge.call("crash", kind=kind, timeout=3)
    except Exception:
        pass  # (no reply: it crashed)
    end = time.time() + 90
    while time.time() < end and inst.alive():
        time.sleep(0.5)
    r["exited"] = not inst.alive()
    inst.kill()

    reports = sorted(glob.glob(inst.writedir + "crashes/crash_*"))
    dumps = [p for p in reports if p.endswith(".dmp")]
    summaries = [p for p in reports if p.endswith(".txt") and not p.endswith("_log.txt")]
    copies = [p for p in reports if p.endswith("_log.txt")]
    if not (dumps and summaries and copies):
        fails.append(f"report files missing: {[os.path.basename(p) for p in reports]}")
    else:
        r["dump_mb"] = round(os.path.getsize(dumps[-1]) / 1e6, 1)
        summary = open(summaries[-1], encoding="utf-8", errors="replace").read()
        r["summary"] = summary.splitlines()[2] if len(summary.splitlines()) > 2 else summary
        if code not in summary or ("main thread" in summary) != main or "Dump written: yes" not in summary:
            fails.append("summary: " + summary.replace("\r\n", " | "))
        cdb = subprocess.run([CDB, "-z", dumps[-1], "-y", SYMBOLS, "-c", ".ecxr; kn 12; q"],
                             capture_output=True, text=True, errors="replace", timeout=180).stdout
        r["stack_has_" + frame] = frame in cdb
        if frame not in cdb:
            fails.append(f"the dump's stack lacks {frame}")

    lines = [line for line in inst.log().splitlines() if line.strip()]
    marker = next((i for i in range(len(lines) - 1, -1, -1) if f"test bridge: crashing ({kind})" in lines[i]), None)
    crashed = next((i for i in range(len(lines) - 1, -1, -1) if "Overgrowth Online crashed:" in lines[i]), None)
    r["log_tail_kept"] = marker is not None and crashed is not None and crashed > marker
    if not r["log_tail_kept"]:
        fails.append("the log lost its last lines: " + " | ".join(lines[-3:]))

    for p in glob.glob(f"{WER_DUMPS}/OvergrowthOnline.exe.{pid}.dmp"):
        os.remove(p)  # (this PC's LocalDumps copy of the same crash)
    shutil.rmtree(inst.writedir + "crashes", ignore_errors=True)
    return r, fails


def main():
    if user_game_running():
        sys.exit("the user's Overgrowth is running: not now")
    failed = False
    for i, kind in enumerate(KINDS):
        r, fails = check(kind, i)
        print(("PASS" if not fails else "FAIL"), r)
        for f in fails:
            print("   ", f)
        failed = failed or bool(fails)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
