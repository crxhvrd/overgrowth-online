//-----------------------------------------------------------------------------
//           Name: multiplayer_join_friend.as
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Join a friend: your Steam friends in Overgrowth, the ones
//                 hosting first, with what they host and a Join button. A
//                 host is open to its friends from the start of its level
//                 (Online::OpenToFriends); Join takes the same way as an
//                 invite (their lobby, then a P2P connection).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;

const vec4 kTitleColor = vec4(0.95f, 0.78f, 0.35f, 1.0f);
const vec4 kDimColor = vec4(0.65f, 0.65f, 0.65f, 1.0f);
const vec4 kWarnColor = vec4(1.0f, 0.45f, 0.35f, 1.0f);

class FriendGame {
    string id;
    string name;
    string state;    // host | playing | game (see SteamworksMatchmaking::FriendGames)
    string game;
    string players;
    string mods;     // same | other | "" (unknown)
}

array<FriendGame@> friend_games;
float refresh_timer = 0.0f;
string notice;

bool HasFocus() {
    return false;
}

int StateOrder(const string &in state) {
    return state == "host" ? 0 : (state == "playing" ? 1 : 2);
}

void Refresh() {
    array<string>@ lines = Online_FriendGames().split("\n");

    friend_games.resize(0);

    for(uint i = 0; i < lines.length(); ++i) {
        array<string>@ fields = lines[i].split("\t");

        if(fields.length() < 6) {
            continue;
        }

        FriendGame@ f = FriendGame();
        f.id = fields[0];
        f.name = fields[1];
        f.state = fields[2];
        f.game = fields[3];
        f.players = fields[4];
        f.mods = fields[5];

        // Hosts first, then friends in someone's game, then the rest
        uint at = 0;

        while(at < friend_games.length() && StateOrder(friend_games[at].state) <= StateOrder(f.state)) {
            ++at;
        }

        friend_games.insertAt(at, f);
    }
}

void Initialize() {
    @imGUI = CreateIMGUI();
    PlaySong("overgrowth_main");
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);
    imGUI.setup();
    setBackGround();
    Refresh();
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

    if(refresh_timer > 1.0f) {
        refresh_timer = 0.0f;
        Refresh();
    }
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
    DrawFriends();
}

void Draw() {
}

void Init(string str) {
}

string PlayersText(const string &in players) {
    return players == "" ? "" : (players == "1" ? ", 1 player" : ", " + players + " players");
}

void DrawFriends() {
    float sw = float(GetScreenWidth());
    float sh = float(GetScreenHeight());
    float w = min(760.0f, sw * 0.9f);
    ImGui_SetNextWindowPos(vec2((sw - w) * 0.5f, sh * 0.12f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(w, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Join a friend", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(sh >= 1000.0f ? 1.25f : 1.05f);
    ImGui_PushTextWrapPos(0.0f);  // (every line wraps at the window's edge)

    if(!Online_SteamFriendsAvailable()) {
        ImGui_TextWrapped("Joining friends needs Steam: start Overgrowth from Steam, with Steam running.");
    } else if(friend_games.length() == 0) {
        ImGui_TextWrapped("None of your Steam friends is in Overgrowth right now.");
    } else {
        ImGui_BeginChild("friend_games", vec2(0.0f, min(sh * 0.5f, 520.0f)), false);

        for(uint i = 0; i < friend_games.length(); ++i) {
            FriendGame@ f = friend_games[i];

            if(i > 0) {
                ImGui_Separator();
            }

            ImGui_TextColored(kTitleColor, f.name);

            if(f.state == "host") {
                ImGui_Text("Hosting " + (f.game == "" ? "a game" : f.game) + PlayersText(f.players));

                if(f.mods == "other") {
                    ImGui_TextColored(kWarnColor, "Different mods or version: their game would turn you away.");
                } else if(ImGui_Button("Join##" + f.id, vec2(160.0f, 36.0f))) {
                    if(Online_JoinFriend(f.id)) {
                        this_ui.SendCallback("multiplayer_menu_connecting.as");
                    } else {
                        notice = f.name + " isn't hosting any more.";
                        Refresh();
                    }
                }
            } else if(f.state == "playing") {
                ImGui_TextColored(kDimColor, "In someone's game: ask the host for an invite.");
            } else {
                ImGui_TextColored(kDimColor, "In Overgrowth, not hosting.");
            }
        }

        ImGui_EndChild();
    }

    if(notice != "") {
        ImGui_TextColored(kWarnColor, notice);
    }

    ImGui_Spacing();
    ImGui_TextColored(kDimColor, "To host: Multiplayer, then a story or the arena. Your friends see you here once your level has loaded.");
    ImGui_Separator();

    if(ImGui_Button("Back", vec2(160.0f, 36.0f))) {
        this_ui.SendCallback("back");
    }

    ImGui_PopTextWrapPos();
    ImGui_End();
}
