"""Phase 6, part 1: starting the way Steam and the launcher start us.

- Steam starts the game with "+connect_lobby <lobby id>" for an accepted
  invite. The argument parser refuses arguments it doesn't know, so it is
  taken out first and kept (Online's g_launch_lobby) for the Steam code.
- Release layout: Overgrowth/OverhaulOnline/OverhaulOnline.exe with our Data
  overlay; the game's own Data is one folder up. Found at start, so no
  machine-specific AUX_DATA is compiled in.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


if "g_launch_lobby" not in open(S + "Online/online.h", encoding="utf-8").read():
  patch("Online/online.h", [
("""class Online {""", """// Overhaul Online: the Steam lobby to join at start ("+connect_lobby <id>": an accepted invite)
extern std::string g_launch_lobby;

class Online {"""),
])

patch("Online/online.cpp", [
("""#include <Internal/config.h>
""", """#include <Internal/config.h>

std::string g_launch_lobby;  // (online.h)
"""),
])

patch("Main/main.cpp", [
("""    try {
        std::string full_version = std::string(GetBuildVersion()) + "_" + GetBuildIDString();""",
 """    // Overhaul Online: Steam adds "+connect_lobby <lobby id>" for an accepted
    // invite; kept for the Steam code and taken out (the parser would refuse it)
    for (int i = 0; i < argc;) {
        if (strcmp(argv[i], "+connect_lobby") == 0) {
            int n = i + 1 < argc ? 2 : 1;
            if (n == 2) {
                g_launch_lobby = argv[i + 1];
            }
            for (int j = i; j + n < argc; ++j) {
                argv[j] = argv[j + n];
            }
            argc -= n;
        } else {
            ++i;
        }
    }

    try {
        std::string full_version = std::string(GetBuildVersion()) + "_" + GetBuildIDString();"""),
])

patch("Compat/platformsetup.cpp", [
("""#ifdef AUX_DATA
    AddPath(AUX_DATA, kDataPaths);
#endif
""", """#ifdef AUX_DATA
    AddPath(AUX_DATA, kDataPaths);
#endif

    // Overhaul Online release layout: we run from Overgrowth/OverhaulOnline/,
    // the game's own Data is one folder up
    {
        string up;
        WorkingDir(&up);
        while (!up.empty() && (up.back() == '/' || up.back() == '\\\\')) {
            up.pop_back();
        }
        size_t slash = up.find_last_of("/\\\\");
        if (slash != string::npos) {
            up = up.substr(0, slash + 1);
            if (CheckFileAccess((up + "Data/Levels").c_str()) || CheckFileAccess((up + "Data/Textures").c_str())) {
                AddPath(up.c_str(), kDataPaths);
            }
        }
    }
"""),
])
print("ok")
