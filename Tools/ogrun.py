"""Run Overgrowth Online test instances (our engine build) in sandboxes.

Each instance gets its own write dir under test/run/<name>/, a small window
that never takes focus, and is tracked by PID. Only processes whose command
line contains this repo's test folder are ever stopped: the user's own game
(stock Overgrowth.exe from Steam) is never touched.

    python -I tools/ogrun.py boot <level> [--secs N] [--shots N] [--set key=value ...]

Library use: Instance(...).start(), .wait_log(...), .capture(...), .stop().
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace("\\", "/")
EXE = ROOT + "/Build/Release/Overgrowth.exe"
TEST_DIR = ROOT + "/test"
RUN_DIR = TEST_DIR + "/run"
RESULTS = TEST_DIR + "/results"
BASE_CONFIG = "D:/OvergrowthAI/test/writedir/Data/config.txt"  # known-good 1.4 sandbox settings
TOOLS = ROOT + "/tools"

STOCK_MODS = [("com-wolfire-arena", "0.0.1"), ("com-wolfire-drika", "1.0.0"), ("com-wolfire-lugaru-campaign", "1.0.0"),
              ("com-wolfire-machine-translations", "0.0.1"), ("com-wolfire-overgrowth-campaign", "1.0.0"),
              ("com-wolfire-overgrowth-core", "1.0.0"), ("com-wolfire-sandbox-levels", "0.0.1"),
              ("com-wolfire-timbles-therium", "rc"), ("com-wolfire-versus", "0.0.1")]

DEFAULT_KEYS = {
    "fullscreen": "0",
    "vsync": "0",
    "skip_loading_pause": "true",
    "aio_debug_log": "true",
}


def ps(script, *args):
    cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script] + list(args)
    return subprocess.run(cmd, capture_output=True, text=True)


def process_cmdline(pid):
    out = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                          f"(Get-CimInstance Win32_Process -Filter 'ProcessId={pid}').CommandLine"],
                         capture_output=True, text=True).stdout.strip()
    return out


def our_pids():
    """PIDs of running engine instances started from this repo's test folder."""
    out = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                          "Get-CimInstance Win32_Process -Filter \"Name='Overgrowth.exe'\" | "
                          "ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }"],
                         capture_output=True, text=True).stdout
    pids = []

    for line in out.splitlines():
        if "|" not in line:
            continue
        pid, cmd = line.split("|", 1)
        if TEST_DIR.lower() in cmd.replace("\\", "/").lower():
            pids.append(int(pid))

    return pids


class Instance:
    def __init__(self, name, level="", keys=None, width=960, height=540, extra_args="", mods=None):
        self.name = name
        self.mods = mods or []
        self.level = level
        self.dir = f"{RUN_DIR}/{name}"
        self.writedir = self.dir + "/writedir/"
        self.keys = dict(DEFAULT_KEYS)
        self.keys.update({"screenwidth": str(width), "screenheight": str(height)})
        self.keys.update(keys or {})
        self.extra_args = extra_args
        self.pid = None

    def prepare(self):
        if os.path.isdir(self.dir):
            shutil.rmtree(self.dir)
        os.makedirs(self.writedir + "Data", exist_ok=True)
        lines = open(BASE_CONFIG, encoding="utf-8").read().splitlines() if os.path.exists(BASE_CONFIG) else []
        seen = set()
        out = []

        for line in lines:
            key = line.split(":", 1)[0].strip()
            if key in self.keys:
                out.append(f"{key}: {self.keys[key]}")
                seen.add(key)
            elif not key.startswith(("aio_test", "aiob_", "otr_", "ocam_", "ofin_", "ohud_")):
                out.append(line)

        out += [f"{k}: {v}" for k, v in self.keys.items() if k not in seen]
        open(self.writedir + "Data/config.txt", "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")

        # Mods: copied into this instance's write dir and switched on with the stock ones
        ids = list(STOCK_MODS)

        for mod in self.mods:
            dest = self.writedir + "Data/Mods/" + os.path.basename(mod.rstrip("/"))
            shutil.copytree(mod, dest)
            m = re.search(r"<Id>([^<]+)</Id>", open(mod + "/mod.xml", encoding="utf-8").read())
            ids.append((m.group(1), "0.1.0"))

        rows = "".join(f'    <ModInstance id="{i}" activated="true" modsource="local" version="{v}" />\n' for i, v in ids)
        open(self.writedir + "modconfig.xml", "w", encoding="utf-8", newline="\n").write(
            '<?xml version="2.0" ?>\n<ActiveMods>\n' + rows + "</ActiveMods>\n")

    def start(self):
        self.prepare()
        args = f"--write-dir {self.writedir} --working-dir {ROOT}/ --clear-log --no-dialogues {self.extra_args}"
        if self.level:
            args += f" -l {self.level}"
        res = ps(TOOLS + "/start_noactivate.ps1", "-Exe", EXE, "-Dir", ROOT, "-CmdArgs", args)
        m = re.search(r"started pid=(\d+)", res.stdout)
        if not m:
            raise RuntimeError("launch failed: " + res.stdout + res.stderr)
        self.pid = int(m.group(1))
        return self.pid

    def alive(self):
        return self.pid is not None and self.pid in our_pids()

    def log(self):
        path = self.writedir + "logfile.txt"
        return open(path, encoding="utf-8", errors="replace").read() if os.path.exists(path) else ""

    def wait_log(self, pattern, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if re.search(pattern, self.log()):
                return True
            if not self.alive():
                return False
            time.sleep(1)
        return False

    def capture(self, out):
        return ps(TOOLS + "/capture_pid.ps1", "-ProcessId", str(self.pid), "-Out", out.replace("/", "\\")).returncode == 0

    def stop(self):
        if self.pid is None:
            return
        cmd = process_cmdline(self.pid)
        if TEST_DIR.lower() in cmd.replace("\\", "/").lower():
            subprocess.run(["taskkill.exe", "/F", "/PID", str(self.pid)], capture_output=True)
        self.pid = None


def errors(log):
    lines = [l for l in log.splitlines() if re.search(r"\[e\]|\[f\]|ERR:|WARN:", l)]
    counts = {}
    for l in lines:
        key = re.sub(r"^[0-9/: ]+", "", l)[:220]
        counts[key] = counts.get(key, 0) + 1
    return sorted(counts.items(), key=lambda kv: -kv[1])


def cmd_boot(a):
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out_dir = f"{RESULTS}/{stamp}_boot"
    os.makedirs(out_dir, exist_ok=True)
    keys = dict(kv.split("=", 1) for kv in a.set)
    inst = Instance("boot", a.level, keys, mods=a.mod)
    inst.start()
    print(f"pid={inst.pid} writedir={inst.writedir}")
    loaded = inst.wait_log(r"Level loaded|Calling void Init\(string level_name\)|level loaded", a.load_timeout)
    print("level loaded:", loaded)

    for i in range(a.shots):
        time.sleep(a.secs / max(1, a.shots))
        if inst.alive():
            shot = f"{out_dir}/shot_{i + 1}.png"
            print("shot:", shot if inst.capture(shot) else "failed")

    alive = inst.alive()
    log = inst.log()
    inst.stop()
    open(out_dir + "/logfile.txt", "w", encoding="utf-8").write(log)
    print("still running at the end:", alive)
    print("--- errors (count, line) ---")
    for line, n in errors(log)[:40]:
        print(f"{n:5d}  {line}")
    print("results:", out_dir)


def cmd_mp(a):
    """Baseline of the stock online code: one host, N clients over loopback TCP."""
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out_dir = f"{RESULTS}/{stamp}_mp"
    os.makedirs(out_dir, exist_ok=True)
    mods = [ROOT + "/test/mods/mptest"] + a.mod
    keys = dict(kv.split("=", 1) for kv in a.set)
    host = Instance("host", a.level, dict(keys, mpt_role="host", playername="Host"), mods=mods)
    clients = [Instance(f"client{i + 1}", a.level, dict(keys, mpt_role="client", mpt_ip="127.0.0.1",
                                                          playername=f"Client{i + 1}"), mods=mods)
               for i in range(a.clients)]
    everyone = [host] + clients

    try:
        host.start()
        print("host pid", host.pid, "hosting:", host.wait_log(r"\[MPT\] hosting", a.load_timeout))

        for c in clients:
            c.start()
            print(c.name, "pid", c.pid, "connecting:", c.wait_log(r"\[MPT\] connecting", a.load_timeout))

        for i in range(a.shots):
            time.sleep(a.secs / max(1, a.shots))
            for inst in everyone:
                if inst.alive():
                    inst.capture(f"{out_dir}/{inst.name}_{i + 1}.png")

        alive = {inst.name: inst.alive() for inst in everyone}
    finally:
        for inst in everyone:
            inst.stop()

    for inst in everyone:
        log = inst.log()
        open(f"{out_dir}/{inst.name}_log.txt", "w", encoding="utf-8").write(log)
        print(f"=== {inst.name}: still running={alive.get(inst.name)}")
        mpt = [l.split("[MPT] ", 1)[1] for l in log.splitlines() if "[MPT] " in l]
        for line in mpt[-12:]:
            print("   ", line)
        for line, n in errors(log)[:12]:
            print(f"   {n:4d}  {line}")

    print("results:", out_dir)


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot")
    b.add_argument("level", nargs="?", default="")
    b.add_argument("--secs", type=float, default=20)
    b.add_argument("--shots", type=int, default=2)
    b.add_argument("--load-timeout", type=float, default=120)
    b.add_argument("--set", nargs="*", default=[])
    b.add_argument("--mod", action="append", default=[], help="mod folder to install into the instance")
    m = sub.add_parser("mp")
    m.add_argument("level")
    m.add_argument("--clients", type=int, default=1)
    m.add_argument("--secs", type=float, default=60)
    m.add_argument("--shots", type=int, default=2)
    m.add_argument("--load-timeout", type=float, default=120)
    m.add_argument("--set", nargs="*", default=[])
    m.add_argument("--mod", action="append", default=[])
    s = sub.add_parser("stop-all")
    a = p.parse_args()

    if a.cmd == "boot":
        cmd_boot(a)
    elif a.cmd == "mp":
        cmd_mp(a)
    elif a.cmd == "stop-all":
        for pid in our_pids():
            subprocess.run(["taskkill.exe", "/F", "/PID", str(pid)], capture_output=True)
            print("stopped", pid)


if __name__ == "__main__":
    sys.exit(main())
