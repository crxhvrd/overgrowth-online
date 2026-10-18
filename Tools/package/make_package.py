"""Overgrowth Online package (public test builds): dist/OvergrowthOnline_<version>/ and its zip.

    python -I Tools/package/make_package.py [--build Build_steam/Release] [--name suffix]

Contents:
  OvergrowthOnline/ the exe and its DLLs (+ the VC++ runtime), steam_appid.txt,
                    our Data overlay (Scripts, GLSL, root files), licences
  launcher/         Overgrowth.exe (the stand-in Steam starts)
  mod/overhaul/     the Overhaul mod (D:/OvergrowthAI)
  install.bat, uninstall.bat (+ .ps1), README.txt
Never any game assets: those come from the player's own Overgrowth.
"""
import glob
import os
import re
import shutil
import sys

ROOT = "D:/OvergrowthMP"
MOD = "D:/OvergrowthAI/mod/overhaul"
HERE = ROOT + "/Tools/package"

args = sys.argv[1:]
build = ROOT + "/" + (args[args.index("--build") + 1] if "--build" in args else "Build_steam/Release")
suffix = args[args.index("--name") + 1] if "--name" in args else ""
version = re.search(r"<Version>([^<]+)</Version>", open(MOD + "/mod.xml", encoding="utf-8").read()).group(1)
name = f"OvergrowthOnline_{version}{suffix}"
out = f"{ROOT}/dist/{name}"

if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(out + "/OvergrowthOnline")
online = out + "/OvergrowthOnline"

# The exe and its runtime files
for f in ["OvergrowthOnline.exe", "steam_appid.txt"] + [os.path.basename(p) for p in glob.glob(build + "/*.dll")]:
    shutil.copy2(f"{build}/{f}", online)
crt = sorted(glob.glob("R:/vs/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"))[-1]
for f in ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]:
    shutil.copy2(f"{crt}/{f}", online)

# Our Data overlay: scripts, shaders and the small files at the top
for d in ["Scripts", "GLSL"]:
    shutil.copytree(f"{ROOT}/Data/{d}", f"{online}/Data/{d}", ignore=shutil.ignore_patterns("*.bytecode"))
for f in os.listdir(ROOT + "/Data"):
    if os.path.isfile(f"{ROOT}/Data/{f}"):
        shutil.copy2(f"{ROOT}/Data/{f}", f"{online}/Data/{f}")

# Licences: the engine (Apache-2.0) and the libraries shipped as DLLs
os.makedirs(online + "/licenses")
shutil.copy2(ROOT + "/LICENSE", online + "/LICENSE.txt")
for lib, src in [("SDL2", "SDL2-2.0.20/COPYING.txt"), ("SDL2_net", "SDL2_net-2.0.1/COPYING.txt"),
                 ("OpenAL-soft", "openal-soft-1.18.2-bin/COPYING"), ("libogg", "libogg-1.3.3/COPYING"),
                 ("libvorbis", "libvorbis-1.3.6/COPYING"), ("openvr", "openvr-master/LICENSE")]:
    shutil.copy2(f"{ROOT}/Libraries/{src}", f"{online}/licenses/{lib}.txt")
shutil.copy2(HERE + "/THIRD_PARTY.txt", online + "/licenses/THIRD_PARTY.txt")

# The launcher, the mod, the installer
os.makedirs(out + "/launcher")
shutil.copy2(ROOT + "/Build/launcher/Overgrowth.exe", out + "/launcher/Overgrowth.exe")
shutil.copytree(MOD, out + "/mod/overhaul")
for f in ["install.bat", "install.ps1", "uninstall.bat", "uninstall.ps1", "find_game.ps1"]:
    shutil.copy2(f"{HERE}/{f}", out)
readme = open(f"{HERE}/README.txt", encoding="utf-8", newline="").read().replace("{VERSION}", version)
open(f"{out}/README.txt", "w", encoding="utf-8", newline="").write(readme)  # (its version filled in)

zip_path = shutil.make_archive(f"{ROOT}/dist/{name}", "zip", f"{ROOT}/dist", name)
size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(out) for f in fs)
print(f"{out}  ({size / 1e6:.1f} MB)\n{zip_path}  ({os.path.getsize(zip_path) / 1e6:.1f} MB)")
