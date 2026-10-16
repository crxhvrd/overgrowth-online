"""A release layout for tests: test/release/ = a stand-in Overgrowth folder.

    test/release/OvergrowthOnline/  the package's OvergrowthOnline folder (exe, DLLs, overlay)

Tests then run with OGMP_EXE / OGMP_WORKDIR pointing there (see instance.py)
and --game-data naming the installed game (OGMP_EXTRA), the way a player's
install runs but without a link into the game folder (no AUX_DATA).

    python -I Tools/ogmp/release_layout.py dist/OvergrowthOnline_<version>
"""
import os
import shutil
import sys

ROOT = "D:/OvergrowthMP"
GAME_DATA = "R:/SteamLibrary/steamapps/common/Overgrowth/Data"
LAYOUT = ROOT + "/test/release"


def make(package):
    os.makedirs(LAYOUT, exist_ok=True)
    online = LAYOUT + "/OvergrowthOnline"
    if os.path.isdir(online):
        shutil.rmtree(online)
    shutil.copytree(package + "/OvergrowthOnline", online)
    return online + "/OvergrowthOnline.exe", online


def env():
    """The environment for a test run on the release layout."""
    return {"OGMP_EXE": LAYOUT + "/OvergrowthOnline/OvergrowthOnline.exe", "OGMP_WORKDIR": LAYOUT + "/OvergrowthOnline",
            "OGMP_EXTRA": "--game-data " + GAME_DATA[:-len("/Data")] + "/"}


if __name__ == "__main__":
    pkg = sys.argv[1] if len(sys.argv) > 1 else None
    if pkg is None:
        sys.exit("usage: release_layout.py <package folder>")
    if not os.path.isabs(pkg):
        pkg = ROOT + "/" + pkg
    print(make(pkg))
