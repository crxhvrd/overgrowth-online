"""Phase 6, part 4: --game-data <folder>: the Overgrowth install whose Data to
use, for a release build run from somewhere else (tests, a game installed
outside Steam). Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Compat/platformsetup.h", [
("""void SetUpEnvironment(char* program_path, const char* overloaded_write_dir, const char* overloaded_working_dir);""",
 """void SetUpEnvironment(char* program_path, const char* overloaded_write_dir, const char* overloaded_working_dir);
extern std::string g_game_data_dir;  // Overhaul Online: --game-data <the Overgrowth install>"""),
])

patch("Compat/platformsetup.cpp", [
("""void SetUpEnvironment(char* program_path, const char* overloaded_write_dir, const char* overloaded_working_dir) {""",
 """std::string g_game_data_dir;

void SetUpEnvironment(char* program_path, const char* overloaded_write_dir, const char* overloaded_working_dir) {"""),
("""    // Overhaul Online release layout: we run from Overgrowth/OverhaulOnline/,
    // the game's own Data is one folder up
    {""", """    // Overhaul Online: --game-data, the Overgrowth install to read the game from
    if (!g_game_data_dir.empty()) {
        string dir = g_game_data_dir;
        if (dir.back() != '/' && dir.back() != '\\\\') {
            dir += "/";
        }
        AddPath(dir.c_str(), kDataPaths);
    }

    // Overhaul Online release layout: we run from Overgrowth/OverhaulOnline/,
    // the game's own Data is one folder up
    {"""),
])

patch("Main/main.cpp", [
("""        TCLAP::ValueArg<int> testBridgeArg(""", """        TCLAP::ValueArg<std::string> gameDataArg("", "game-data", "The Overgrowth install whose Data to use (Overhaul Online).", false, "", "string");
        cmd.add(gameDataArg);

        TCLAP::ValueArg<int> testBridgeArg("""),
("""        overloadedWorkingDir = workingDirArg.getValue();""", """        overloadedWorkingDir = workingDirArg.getValue();
        g_game_data_dir = gameDataArg.getValue();"""),
])
print("ok")
