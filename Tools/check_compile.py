"""Compile-check engine sources with the build's own flags, without building:
cl /Zs (syntax and types only, writes nothing), so several agents or edits can
check their files while nothing touches the build tree. A header given is
checked through the .cpp of the same name, if there is one.

    python -I Tools/check_compile.py Source/Graphics/particles.cpp [more ...]
    python -I Tools/check_compile.py --changed        (every changed .cpp/.h vs HEAD)
    python -I Tools/check_compile.py --warnings ...   (warnings too, not only errors)
"""

import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace("\\", "/")
VCXPROJ = ROOT + "/Build/Overgrowth.vcxproj"
VCVARS = "R:/vs/VC/Auxiliary/Build/vcvars64.bat"
ENV_CACHE = os.path.join(tempfile.gettempdir(), "ogmp_vcvars_env.json")


def vc_env():
    """vcvars64's environment, cached (running it takes seconds)."""
    stamp = os.path.getmtime(VCVARS)
    try:
        cached = json.load(open(ENV_CACHE, encoding="utf-8"))
        if cached.get("stamp") == stamp:
            return cached["env"]
    except (OSError, ValueError):
        pass
    out = subprocess.run(["cmd", "/c", "call", VCVARS.replace("/", "\\"), ">nul", "&&", "set"], capture_output=True, text=True).stdout
    env = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    if not any(k.upper() == "INCLUDE" for k in env):
        sys.exit("vcvars64 gave no compiler environment: " + out[-2000:])
    env["VSLANG"] = "1033"  # (English messages, where that language pack is installed)
    json.dump({"stamp": stamp, "env": env}, open(ENV_CACHE, "w", encoding="utf-8"))
    return env


def release_flags():
    """The Release|x64 ClCompile settings of the generated project."""
    text = open(VCXPROJ, encoding="utf-8").read()
    block = re.search(r"<ItemDefinitionGroup Condition=\"'\$\(Configuration\)\|\$\(Platform\)'=='Release\|x64'\">\s*<ClCompile>(.*?)</ClCompile>", text, re.S).group(1)

    def tag(name):
        m = re.search(f"<{name}>(.*?)</{name}>", block, re.S)
        return m.group(1) if m else ""

    flags = ["/nologo", "/Zs", "/EHsc", "/MD", "/GR", "/permissive-", "/external:W0", "/W3", "/w44267"]
    for inc in tag("AdditionalIncludeDirectories").split(";"):
        if inc and not inc.startswith("%(") and not inc.startswith("\\"):
            flags.append("/I" + inc)
    for d in tag("PreprocessorDefinitions").split(";"):
        if d and not d.startswith("%("):
            flags.append("/D" + d.replace("&quot;", '"'))
    for m in re.finditer(r'/external:I "([^"]+)"', tag("AdditionalOptions")):
        flags += ["/external:I", m.group(1)]
    return flags


def cl_path(env):
    path = next((v for k, v in env.items() if k.upper() == "PATH"), "")
    cl = shutil.which("cl", path=path)  # (Windows looks a program up on our own PATH, not env's)
    if not cl:
        sys.exit("cl.exe not on vcvars64's PATH")
    return cl


def check(path, flags, env, warnings):
    # (cl writes in the console's code page)
    r = subprocess.run([cl_path(env)] + flags + [path], capture_output=True, text=True, encoding="oem", errors="replace", env=env, cwd=ROOT)
    lines = [l for l in (r.stdout + r.stderr).splitlines() if l.strip() and not l.strip().endswith(os.path.basename(path))]
    keep = [l for l in lines if " error " in l or "fatal error" in l or (warnings and " warning " in l)]
    return path, r.returncode == 0, keep


def changed_files():
    out = subprocess.run(["git", "-C", ROOT, "status", "--porcelain"], capture_output=True, text=True).stdout
    return [l[3:].strip() for l in out.splitlines() if l[3:].strip().endswith((".cpp", ".h")) and l[3:].strip().startswith("Source/")]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    args = sys.argv[1:]
    warnings = "--warnings" in args
    files = [a for a in args if not a.startswith("--")]
    if "--changed" in args:
        files += changed_files()
    cpps = []
    for f in files:
        f = f.replace("\\", "/")
        if f.endswith(".h"):
            cpp = f[:-2] + ".cpp"
            if os.path.exists(os.path.join(ROOT, cpp)):
                f = cpp
            else:
                print(f"(no {os.path.basename(cpp)} for {f}: check a file that includes it)")
                continue
        if f not in cpps:
            cpps.append(f)
    if not cpps:
        print(__doc__)
        return 2
    flags, env = release_flags(), vc_env()
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as pool:
        for path, ok, lines in pool.map(lambda f: check(f, flags, env, warnings), cpps):
            print(("OK   " if ok else "FAIL ") + path)
            for l in lines[:20]:
                print("     " + l)
            failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
