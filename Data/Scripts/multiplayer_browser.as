//-----------------------------------------------------------------------------
//           Name: multiplayer_browser.as
//    Script Type: Menu
//    Description: Overgrowth Online: the server browser. The public games of
//                 Overgrowth Online anywhere (Steam lobbies: Online_PublicGames),
//                 nearest first, with who hosts, what, how many play, the
//                 version and the ping; Join. Hosts choose public or friends
//                 only on the Multiplayer screen.
//        License: Apache-2.0
//-----------------------------------------------------------------------------

#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;

const vec4 kTitleColor = vec4(0.95f, 0.78f, 0.35f, 1.0f);
const vec4 kDimColor = vec4(0.65f, 0.65f, 0.65f, 1.0f);
const vec4 kWarnColor = vec4(1.0f, 0.45f, 0.35f, 1.0f);
const vec4 kGoodColor = vec4(0.55f, 0.85f, 0.45f, 1.0f);
const float kRefreshSeconds = 15.0f;

class PublicGame {
    string lobby;
    string host;
    string game;
    string players;
    string version;
    string mods;     // same | other | "" (unknown)
    int ping;        // ms, -1 unknown
}

array<PublicGame@> games;
string state = "none";   // busy | ready | failed | none (no Steam)
float refresh_timer = 0.0f;
string notice;

bool HasFocus() {
    return false;
}

void Ask() {
    Online_RequestPublicGames();
    refresh_timer = 0.0f;
}

void Read() {
    array<string>@ lines = Online_PublicGames().split("\n");
    state = lines.length() > 0 ? lines[0] : "failed";
    games.resize(0);

    for(uint i = 1; i < lines.length(); ++i) {
        array<string>@ f = lines[i].split("\t");
        if(f.length() < 7) {
            continue;
        }
        PublicGame@ g = PublicGame();
        g.lobby = f[0];
        g.host = f[1];
        g.game = f[2];
        g.players = f[3];
        g.version = f[4];
        g.mods = f[5];
        g.ping = atoi(f[6]);

        // Nearest first (unknown pings last)
        uint at = 0;
        while(at < games.length() && Nearer(games[at], g)) {
            ++at;
        }
        games.insertAt(at, g);
    }
}

bool Nearer(PublicGame@ a, PublicGame@ b) {
    if(a.ping < 0) {
        return false;
    }
    return b.ping < 0 || a.ping <= b.ping;
}

void Initialize() {
    @imGUI = CreateIMGUI();
    PlaySong("overgrowth_main");
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);
    imGUI.setup();
    setBackGround();
    Ask();
    Read();
}

void Dispose() {
    imGUI.clear();
}

bool CanGoBack() {
    return true;
}

void Update() {
    UpdateKeyboardMouse();
    imGUI.update();
    UpdateController();

    refresh_timer += time_step;
    if(refresh_timer > kRefreshSeconds) {
        Ask();
    }
    Read();
}

void Resize() {
    imGUI.doScreenResize();
    setBackGround();
}

void ScriptReloaded() {
    imGUI.clear();
    Initialize();
}

void DrawGUI() {
    imGUI.render();
    DrawBrowser();
}

void Draw() {
}

void Init(string str) {
}

string PingText(int ping) {
    return ping < 0 ? "?" : ping + " ms";
}

vec4 PingColor(int ping) {
    return ping < 0 ? kDimColor : (ping < 90 ? kGoodColor : (ping < 180 ? kTitleColor : kWarnColor));
}

void DrawBrowser() {
    float sw = float(GetScreenWidth());
    float sh = float(GetScreenHeight());
    float w = min(1100.0f, sw * 0.92f);
    ImGui_SetNextWindowPos(vec2((sw - w) * 0.5f, sh * 0.1f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(w, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Browse games", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);
    float scale = sh >= 1000.0f ? 1.25f : 1.05f;
    ImGui_SetWindowFontScale(scale);
    ImGui_PushTextWrapPos(0.0f);

    if(state == "none") {
        ImGui_TextWrapped("The server browser needs Steam: start Overgrowth from Steam, with Steam running.");
    } else if(state == "failed") {
        ImGui_TextWrapped("Steam didn't answer. Refresh to try again.");
    } else if(games.length() == 0) {
        ImGui_TextWrapped(state == "busy" ? "Looking for games..." : "No public games right now. Host one: pick a story or the arena on the Multiplayer screen.");
    } else {
        // Columns: host, game, players, ping, join
        float row_h = 40.0f * scale;
        ImGui_Columns(5, false);
        ImGui_SetColumnWidth(0, w * 0.22f);
        ImGui_SetColumnWidth(1, w * 0.40f);
        ImGui_SetColumnWidth(2, w * 0.11f);
        ImGui_SetColumnWidth(3, w * 0.11f);
        ImGui_TextColored(kDimColor, "Host");
        ImGui_NextColumn();
        ImGui_TextColored(kDimColor, "Game");
        ImGui_NextColumn();
        ImGui_TextColored(kDimColor, "Players");
        ImGui_NextColumn();
        ImGui_TextColored(kDimColor, "Ping");
        ImGui_NextColumn();
        ImGui_NextColumn();
        ImGui_Separator();

        for(uint i = 0; i < games.length(); ++i) {
            PublicGame@ g = games[i];
            ImGui_TextColored(kTitleColor, g.host == "" ? "Someone" : g.host);
            ImGui_NextColumn();
            ImGui_Text(g.game == "" ? "Choosing a level" : g.game);
            ImGui_NextColumn();
            ImGui_Text(g.players == "" ? "1" : g.players);
            ImGui_NextColumn();
            ImGui_TextColored(PingColor(g.ping), PingText(g.ping));
            ImGui_NextColumn();
            if(g.mods == "other") {
                ImGui_TextColored(kWarnColor, g.version != "" ? "v" + g.version : "Other mods");
                if(ImGui_IsItemHovered()) {
                    ImGui_SetTooltip("Different mods or version (theirs " + (g.version == "" ? "unknown" : g.version) + "): their game would turn you away.");
                }
            } else if(ImGui_Button("Join##" + g.lobby, vec2(130.0f * scale, row_h * 0.8f))) {
                if(Online_JoinPublicGame(g.lobby)) {
                    this_ui.SendCallback("multiplayer_menu_connecting.as");
                } else {
                    notice = "Couldn't join that game.";
                }
            }
            ImGui_NextColumn();
        }
        ImGui_Columns(1);
        ImGui_Separator();
    }

    if(notice != "") {
        ImGui_TextColored(kWarnColor, notice);
    }

    ImGui_Spacing();
    ImGui_TextColored(kDimColor, "Public games of Overgrowth Online anywhere, nearest first. Others find yours here while your "
                      "hosting is public (the Multiplayer screen).");
    ImGui_Separator();

    if(ImGui_Button(state == "busy" ? "Refreshing..." : "Refresh", vec2(220.0f, 36.0f)) && state != "busy") {
        notice = "";
        Ask();
    }
    ImGui_SameLine();
    if(ImGui_Button("Back", vec2(160.0f, 36.0f))) {
        this_ui.SendCallback("back");
    }

    ImGui_PopTextWrapPos();
    ImGui_End();
}
