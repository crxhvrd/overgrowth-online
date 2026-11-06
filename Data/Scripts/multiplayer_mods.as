//-----------------------------------------------------------------------------
//           Name: multiplayer_mods.as
//    Script Type: Menu (included by multiplayer_browser.as, multiplayer_join_friend.as)
//    Description: Overgrowth Online: a listed game's mods and version, and what
//                 stands between this player and joining it. Everyone in a game
//                 needs the host's version and the same mods on.
//        License: Apache-2.0
//-----------------------------------------------------------------------------

const int kModsShown = 4;  // names on a line; all of them on hover

class GameMods {
    string match;            // same | other | "" (unknown)
    string version;          // theirs ("" unknown)
    array<string> theirs;    // every mod of theirs
    array<string> install;   // theirs, not on this machine
    array<string> enable;    // theirs, off here
    array<string> disable;   // on here, not theirs
}

array<string> ModNames(const string &in field) {
    array<string> names;
    array<string>@ parts = field.split("|");
    for(uint i = 0; i < parts.length(); ++i) {
        if(parts[i] != "") {
            names.insertLast(parts[i]);
        }
    }
    return names;
}

// The last four fields of a row, from `at`
GameMods@ ReadGameMods(array<string>@ fields, uint at, const string &in match, const string &in version) {
    GameMods@ m = GameMods();
    m.match = match;
    m.version = version;
    if(fields.length() >= at + 4) {
        m.theirs = ModNames(fields[at]);
        m.install = ModNames(fields[at + 1]);
        m.enable = ModNames(fields[at + 2]);
        m.disable = ModNames(fields[at + 3]);
    }
    return m;
}

bool WrongVersion(GameMods@ m) {
    return m.version != "" && m.version != Online_GetVersion();
}

bool CanJoin(GameMods@ m) {
    return m.match != "other" && !WrongVersion(m);
}

// "Install: A, B, C, D, +2 more", all of them on hover
void DrawModNames(const string &in label, const array<string> &in names, const vec4 &in color) {
    string line = label + ": ";
    string all = "";
    for(uint i = 0; i < names.length(); ++i) {
        all += (i > 0 ? "\n" : "") + names[i];
        if(int(i) < kModsShown) {
            line += (i > 0 ? ", " : "") + names[i];
        }
    }
    if(int(names.length()) > kModsShown) {
        line += ", +" + (int(names.length()) - kModsShown) + " more";
    }
    ImGui_TextColored(color, line);
    if(int(names.length()) > kModsShown && ImGui_IsItemHovered()) {
        ImGui_SetTooltip(all);
    }
}

// Under a game: its mods, then why this player can't join it
void DrawGameMods(GameMods@ m) {
    if(m.theirs.length() > 0) {
        DrawModNames("Mods", m.theirs, kDimColor);
    } else if(m.match != "" && m.version != "" && !WrongVersion(m)) {
        ImGui_TextColored(kDimColor, "No mods");
    }
    if(WrongVersion(m)) {
        // (first things first; and an older host's lobby has no list to compare)
        ImGui_TextColored(kWarnColor, "Version " + m.version + ": you have " + Online_GetVersion());
    } else if(m.match == "other") {
        if(m.install.length() > 0) {
            DrawModNames("Install", m.install, kWarnColor);
        }
        if(m.enable.length() > 0) {
            DrawModNames("Turn on", m.enable, kWarnColor);
        }
        if(m.disable.length() > 0) {
            DrawModNames("Turn off", m.disable, kWarnColor);
        }
        if(m.install.length() + m.enable.length() + m.disable.length() == 0) {
            ImGui_TextColored(kWarnColor, "Different mods: this game would turn you away");
        }
    }
}
