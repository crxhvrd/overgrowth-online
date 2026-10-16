"""Sandboxed engine instances for automated tests.

Every instance: its own write dir under test/run/<name>/ (config, mods,
logs), a small window that never takes focus, a test-bridge port, and a PID.
Only processes named OvergrowthOnline.exe (or OverhaulOnline.exe, its old
name) whose command line contains this
repo's test folder are ever stopped. The user's own game (Overgrowth.exe from
Steam) is never touched.
"""

import os
import re
import shutil
import subprocess
import time

from .bridge import Bridge

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))).replace("\\", "/")
EXE_NAME = "OvergrowthOnline.exe"
EXE_NAMES = ("OvergrowthOnline.exe", "OverhaulOnline.exe")  # (the old name too: an older install)
EXE_FILTER = " OR ".join(f"Name='{n}'" for n in EXE_NAMES)
EXE = ROOT + "/Build/Release/" + EXE_NAME


def exe_and_dir():
    """The exe and working dir to run: the GNS test build in the repo, or
    (OGMP_EXE / OGMP_WORKDIR) e.g. the Steam build in a release layout."""
    return os.environ.get("OGMP_EXE", EXE), os.environ.get("OGMP_WORKDIR", ROOT).rstrip("/")
TEST_DIR = ROOT + "/test"
RUN_DIR = TEST_DIR + "/run"
RESULTS = TEST_DIR + "/results"
TOOLS = ROOT + "/tools"
BASE_CONFIG = "D:/OvergrowthAI/test/writedir/Data/config.txt"  # known-good 1.4 settings
BASE_PORT = 42710

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


def _powershell(*args):
    return subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass"] + list(args),
                          capture_output=True, text=True)


def our_processes():
    """(pid, command line) of every running test instance of ours."""
    out = _powershell("-Command", f"Get-CimInstance Win32_Process -Filter \"{EXE_FILTER}\" | "
                                  "ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }").stdout
    found = []

    for line in out.splitlines():
        if "|" not in line:
            continue
        pid, cmd = line.split("|", 1)
        if TEST_DIR.lower() in cmd.replace("\\", "/").lower():
            found.append((int(pid), cmd))

    return found


def stop_all():
    stopped = []
    for pid, _ in our_processes():
        subprocess.run(["taskkill.exe", "/F", "/PID", str(pid)], capture_output=True)
        stopped.append(pid)
    return stopped


def user_game_running():
    """The user's own game is running: the stock exe (Overgrowth.exe, or
    Overgrowth_stock.exe once Overgrowth Online is installed), or their Overgrowth
    Online (an OvergrowthOnline.exe or, older, OverhaulOnline.exe that isn't one
    of our test instances). A dead
    process that a leftover handle keeps alive (a few KB) does not count."""
    out = _powershell("-Command", "@(Get-Process Overgrowth,Overgrowth_stock -ErrorAction SilentlyContinue | "
                                  "Where-Object { $_.WorkingSet64 -gt 50MB }).Count").stdout.strip()
    if out not in ("", "0"):
        return True
    out = _powershell("-Command", f"Get-CimInstance Win32_Process -Filter \"{EXE_FILTER}\" | "
                                  "ForEach-Object { $_.CommandLine }").stdout
    return any(line.strip() and TEST_DIR.lower() not in line.replace("\\", "/").lower() for line in out.splitlines())


class Instance:
    def __init__(self, name, index=0, level="", keys=None, width=800, height=450, mods=None, extra_args=""):
        self.name = name
        self.index = index
        self.level = level
        self.port = BASE_PORT + index
        self.dir = f"{RUN_DIR}/{name}"
        self.writedir = self.dir + "/writedir/"
        self.keys = dict(DEFAULT_KEYS)
        self.keys.update({"screenwidth": str(width), "screenheight": str(height)})
        self.keys.update(keys or {})
        self.mods = mods or []
        self.extra_args = extra_args
        self.pid = None
        self.bridge = None

    # Set-up ---------------------------------------------------------------------------

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
        ids = list(STOCK_MODS)

        for mod in self.mods:
            dest = self.writedir + "Data/Mods/" + os.path.basename(mod.rstrip("/"))
            shutil.copytree(mod, dest)
            m = re.search(r"<Id>([^<]+)</Id>", open(mod + "/mod.xml", encoding="utf-8").read())
            ids.append((m.group(1), "0.1.0"))

        rows = "".join(f'    <ModInstance id="{i}" activated="true" modsource="local" version="{v}" />\n' for i, v in ids)
        open(self.writedir + "modconfig.xml", "w", encoding="utf-8", newline="\n").write(
            '<?xml version="2.0" ?>\n<ActiveMods>\n' + rows + "</ActiveMods>\n")

    # Life cycle -----------------------------------------------------------------------

    def start(self, bridge_timeout=150.0):
        self.prepare()
        exe, work = exe_and_dir()
        args = (f"--write-dir {self.writedir} --working-dir {work}/ --clear-log --no-dialogues "
                f"--test-bridge {self.port} {self.extra_args} {os.environ.get('OGMP_EXTRA', '')}")
        if self.level:
            args += f" -l {self.level}"
        # The launcher stays alive ~30 s after the start (it hands focus back
        # and keeps the window in its corner): read the PID, don't wait for it
        self.launcher = subprocess.Popen(
            ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", TOOLS + "/start_noactivate.ps1",
             "-Exe", exe, "-Dir", work, "-CmdArgs", args, "-Slot", str(self.index)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        seen = ""
        m = None

        for line in self.launcher.stdout:
            seen += line
            m = re.search(r"started pid=(\d+)", line)
            if m:
                break

        if m is None:
            raise RuntimeError(f"{self.name}: launch failed: {seen}")

        self.pid = int(m.group(1))
        self.bridge = Bridge(self.port).connect(timeout=bridge_timeout, alive=self.alive)
        return self

    def kill(self):
        """Hard kill (no quit): a crashed or unplugged player."""
        if self.bridge is not None:
            self.bridge.close()
            self.bridge = None
        if self.pid is not None:
            subprocess.run(["taskkill.exe", "/F", "/PID", str(self.pid)], capture_output=True)
        self.pid = None

    def alive(self):
        return self.pid is not None and any(pid == self.pid for pid, _ in our_processes())

    def stop(self, graceful=True):
        if self.bridge is not None and graceful:
            try:
                self.bridge.call("quit", timeout=5)
            except Exception:
                pass
            self.bridge.close()
            self.bridge = None
            end = time.time() + 8
            while time.time() < end and self.alive():
                time.sleep(0.5)

        if self.pid is not None and self.alive():
            subprocess.run(["taskkill.exe", "/F", "/PID", str(self.pid)], capture_output=True)

        self.pid = None

    # Output ---------------------------------------------------------------------------

    def log(self):
        path = self.writedir + "logfile.txt"
        return open(path, encoding="utf-8", errors="replace").read() if os.path.exists(path) else ""

    def capture(self, out):
        """Window screenshot without taking focus (works even if the bridge is busy)."""
        res = _powershell("-File", TOOLS + "/capture_pid.ps1", "-ProcessId", str(self.pid), "-Out", out.replace("/", "\\"))
        return res.returncode == 0


def log_errors(log):
    counts = {}
    for line in log.splitlines():
        if re.search(r"\[e\]|\[f\]|ERR:|WARN:", line):
            key = re.sub(r"^[0-9/: ]+", "", line)[:220]
            counts[key] = counts.get(key, 0) + 1
    return sorted(counts.items(), key=lambda kv: -kv[1])
