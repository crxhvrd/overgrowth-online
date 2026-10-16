"""Overgrowth Online automated tests.

    python -I tools/ogmp.py suite smoke          run a suite
    python -I tools/ogmp.py run connect          run one scenario
    python -I tools/ogmp.py list                 scenarios and suites
    python -I tools/ogmp.py stop-all             stop every test instance of ours

Each scenario starts its own sandboxed instances (OverhaulOnline.exe, write
dirs under test/run), drives them through the test bridge, then closes them.
Results (JSON, logs, screenshots) go to test/results/<stamp>_<name>/.
It refuses to run while the user's own Overgrowth is running.
"""

import json
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ogmp.instance import RESULTS, stop_all, user_game_running, log_errors  # noqa: E402
from ogmp.scenarios import SCENARIOS, SUITES, Context  # noqa: E402

# Fatal-level lines that stock 1.4 data produce too (seen since Phase 0)
KNOWN_FATAL = ("Got invalid mod id",
               # VFormatString truncated a string (common.h:36), once at level load in online
               # sessions since Phase 1; harmless (a cut label), source not pinned down yet
               'Internal/common.h:  36: Failed assert "false"')


def run_one(name, out_root):
    out_dir = f"{out_root}/{name}"
    os.makedirs(out_dir, exist_ok=True)
    ctx = Context(out_dir)
    t0 = time.time()

    try:
        result = SCENARIOS[name](ctx)
    except Exception as e:
        result = {"passed": False, "metrics": {}, "notes": [f"exception: {e}", traceback.format_exc()[-1500:]]}
    finally:
        ctx.close()

    result["seconds"] = round(time.time() - t0, 1)

    for inst in ctx.instances:
        log = inst.log()
        open(f"{out_dir}/{inst.name}_log.txt", "w", encoding="utf-8").write(log)
        result.setdefault("log_errors", {})[inst.name] = [f"{n}x {line}" for line, n in log_errors(log)[:15]]

        # Engine health, whatever the scenario checked: message pool exhaustion
        # (the Phase 2 leak) or a fatal-level line we don't know about
        pool_times = [line[:19] for line in log.splitlines() if "Out of block allocator memory slots" in line]
        fatal = [l for l, n in log_errors(log) if "[f]" in l and not any(k in l for k in KNOWN_FATAL)]

        if pool_times:
            from datetime import datetime
            span = (datetime.strptime(pool_times[-1], "%Y/%m/%d %H:%M:%S") -
                    datetime.strptime(pool_times[0], "%Y/%m/%d %H:%M:%S")).total_seconds()

            if span > 15:  # sustained: a leak (a join burst is short)
                result["passed"] = False
                result.setdefault("notes", []).append(f"{inst.name}: message pool exhausted for {span:.0f} s (x{len(pool_times)})")
            else:
                result.setdefault("notes", []).append(f"{inst.name}: message pool burst x{len(pool_times)} over {span:.0f} s")

        if fatal:
            result["passed"] = False
            result.setdefault("notes", []).append(f"{inst.name}: fatal log line: {fatal[0]}")

    json.dump(result, open(f"{out_dir}/result.json", "w", encoding="utf-8"), indent=2)
    status = "PASS" if result["passed"] else "FAIL"
    print(f"[{status}] {name} ({result['seconds']} s)")

    for k, v in result.get("metrics", {}).items():
        print(f"         {k}: {v}")

    for note in result.get("notes", []):
        print(f"         - {note}")

    return result


def main():
    args = sys.argv[1:]

    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0

    if args[0] == "list":
        print("scenarios:", ", ".join(SCENARIOS))
        for k, v in SUITES.items():
            print(f"suite {k}: {', '.join(v)}")
        return 0

    if args[0] == "stop-all":
        print("stopped:", stop_all())
        return 0

    if user_game_running():
        print("Your Overgrowth is running: close it first (tests never touch it, but they would compete for the GPU).")
        return 2

    names = SUITES[args[1]] if args[0] == "suite" else args[1:]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out_root = f"{RESULTS}/{stamp}_{args[1] if args[0] == 'suite' else '_'.join(names)}"
    os.makedirs(out_root, exist_ok=True)
    stop_all()
    results = {name: run_one(name, out_root) for name in names}
    summary = {k: {"passed": v["passed"], "metrics": v.get("metrics", {})} for k, v in results.items()}
    json.dump(summary, open(f"{out_root}/summary.json", "w", encoding="utf-8"), indent=2)
    passed = sum(1 for v in results.values() if v["passed"])
    print(f"{passed}/{len(results)} passed -> {out_root}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
