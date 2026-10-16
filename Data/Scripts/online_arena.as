//-----------------------------------------------------------------------------
//           Name: online_arena.as
//    Script Type: Level
//    Description: Overgrowth Online: the arenas (Waterfall Cave, Magma Arena,
//                 Rock Cave, Stucco Courtyard), player against player. The
//                 engine runs it instead of multi_arena.as / arena_level.as
//                 when the level is played online (Level::Initialize).
//
//                 The arena's rules without its AI: a match takes one of the
//                 level's battle setups (spots, weapons, items, intro); a
//                 weapon match is a fight to the death (one point), the others
//                 are two points; the last one standing takes the round;
//                 everyone for themselves. The crowd and the arena music.
//
//                 The host runs the matches and tells the guests:
//                   ohl arena text <text>                    the announcement ("" none)
//                   ohl arena score <max> (#<char> <points>)...
//                   ohl arena fight                          "Fight!": everyone heals
//                   ohl arena over                           the round is over
//                 and places every player where it runs (Online_PlaceAvatar).
//        License: Apache-2.0 (as the Wolfire scripts it draws from)
//-----------------------------------------------------------------------------

#include "threatcheck.as"
#include "music_load.as"
#include "multi_arena/multi_arena_music.as"

MusicSegmentHandler msh;

// ---- the match (host) ----------------------------------------------------

enum ArenaPhase {
    kWaiting,    // fewer than two players
    kIntro,      // players placed, the announcements
    kFighting,
    kRoundOver,
    kMatchOver
}

ArenaPhase phase = kWaiting;
float phase_time = 0.0f;   // the_time when the phase began
int intro_step = 0;

array<int> roster;         // the players' characters in this match
array<int> points;         // their points
int max_points = 2;
bool weapons_match = false;
string intro = "";
int round_number = 0;
int round_winner = -1;

array<vec3> spots;           // where players stand at a round start, in order
array<string> spot_weapons;  // what each spot's player holds ("" nothing)
array<string> item_paths;    // the setup's weapons on the ground
array<vec3> item_spots;
vec3 arena_center;
array<int> round_items;      // weapons made for this round
bool arm_pending = false;    // the round's weapons come once the old ones are gone everywhere

float next_broadcast = 0.0f;

// Spawn markers: arena_spawn (by LocName) or character_spawn (Stucco Courtyard)
array<int> marker_ids;
array<string> marker_names;
array<int> marker_types;     // character_spawn game_type, -1 none

// ---- shown (everyone) ----------------------------------------------------

string shown_text = "";
float text_visible = 0.0f;
int text_id = -1;
int score_id = -1;
array<int> score_ids;        // the scoreboard: characters here
array<int> score_points;
int score_max = 2;
bool score_dirty = true;

// The crowd: each machine from what it sees
float audience_excitement = 0.0f;
int audience_sound_handle = -1;
float crowd_cheer_amount = 0.0f;
float crowd_cheer_vel = 0.0f;

// ---- level script entry points --------------------------------------------

void Init(string str) {
    PlaySong("sub-arena-loop");
    QueueSegment("fight1-ver1");
    text_id = TextInit(512, 512);
    score_id = TextInit(512, 512);
    audience_sound_handle = PlaySoundLoop(level.GetPath("crowd_sound"), 0.0f);
    level.SendMessage("disable_retry");
    FindMarkers();

    if(Online_IsClient()) {
        return;  // the host's matches, the host's characters
    }

    // The levels have no character: the host's own, on the first marker
    // (players who join get theirs from the engine, beside it)
    if(marker_ids.length() > 0) {
        Object@ marker = ReadObjectFromID(marker_ids[0]);
        int id = CreateObject(level.GetPath("char_player"), true);
        Object@ obj = ReadObjectFromID(id);
        obj.SetTranslation(marker.GetTranslation());
        vec4 r = marker.GetRotationVec4();
        obj.SetRotation(quaternion(r.x, r.y, r.z, r.a));
        obj.SetPlayer(true);
        Online_AnnounceObject(id, level.GetPath("char_player"));  // made while loading: guests get it now, before its team
        ScriptParams@ params = obj.GetScriptParams();
        params.AddIntCheckbox("No Save", true);
        params.SetString("Teams", "player" + id);
    }
    SetPhase(kWaiting);
}

bool HasFocus() {
    return false;
}

void DrawGUI() {
    {   // the announcement
        HUDImage @image = hud.AddImage();
        image.SetImageFromText(level.GetTextElement(text_id));
        image.position.x = GetScreenWidth() / 2 - 256;
        image.position.y = GetScreenHeight() - 500;
        image.position.z = 4;
        image.color = vec4(1, 1, 1, text_visible);
    }

    if(score_ids.length() > 0) {  // the points
        HUDImage @image = hud.AddImage();
        image.SetImageFromText(level.GetTextElement(score_id));
        image.position.x = 40;
        image.position.y = GetScreenHeight() - 532;  // (bottom-left anchored: the canvas top 20 px down)
        image.position.z = 4;
        image.color = vec4(1, 1, 1, 0.9f);
    }
}

void Update() {
    msh.Update();
    UpdateCrowd();

    if(score_dirty) {
        score_dirty = false;
        DrawScoreboard();
    }

    if(shown_text != "") {
        text_visible = min(1.0f, text_visible + time_step * 2.0f);
    } else {
        text_visible = max(0.0f, text_visible - time_step * 2.0f);
    }

    if(Online_IsHosting()) {
        HostUpdate();
    }
}

void ReceiveMessage(string msg) {
    TokenIterator token_iter;
    token_iter.Init();
    if(!token_iter.FindNextToken(msg)) {
        return;
    }
    string token = token_iter.GetToken(msg);

    if(token == "ohl" && Online_IsClient()) {
        if(!token_iter.FindNextToken(msg) || token_iter.GetToken(msg) != "arena" || !token_iter.FindNextToken(msg)) {
            return;
        }
        string what = token_iter.GetToken(msg);
        if(what == "text") {
            ShowLocal(msg.length() > 15 ? msg.substr(15) : "");  // "ohl arena text " + text
        } else if(what == "score") {
            ReceiveScore(msg);
        } else if(what == "fight") {
            HealOwn();
            msh.StartFight();
        } else if(what == "over") {
            msh.MatchEnded();
        }
        return;
    }

    if(token == "dispose_level") {
        StopSound(audience_sound_handle);
    } else if(token == "character_died") {
        audience_excitement += 4.0f;
    } else if(token == "character_knocked_out") {
        audience_excitement += 3.0f;
    } else if(token == "character_start_flip" || token == "character_start_roll") {
        audience_excitement += 0.4f;
    } else if(token == "character_failed_flip") {
        audience_excitement += 1.0f;
    } else if(token == "item_hit") {
        audience_excitement += 1.5f;
    } else if(token == "knocked_over" || token == "character_thrown") {
        audience_excitement += 1.5f;
    } else if(token == "passive_blocked") {
        audience_excitement += 0.5f;
    } else if(token == "active_blocked" || token == "dodged" || token == "character_throw_escape") {
        audience_excitement += 0.7f;
    } else if(token == "character_attack_feint" || token == "character_attack_missed") {
        audience_excitement += 0.4f;
    } else if(token == "cut") {
        audience_excitement += 2.0f;
    }
}

// ---- host: the match -------------------------------------------------------

void SetPhase(ArenaPhase p) {
    phase = p;
    phase_time = the_time;
    intro_step = 0;
}

void HostUpdate() {
    float t = the_time - phase_time;
    DropGonePlayers();

    if(phase == kWaiting) {
        if(Players().length() >= 2) {
            if(shown_text != "") {
                Show("");
            }
            if(t > 3.0f) {
                NewMatch();
            }
        } else {
            phase_time = the_time;
            if(shown_text != "Waiting for another player") {
                Show("Waiting for another player");
            }
        }
    } else if(roster.length() < 2) {
        // Someone left and one is alone: wait for the next
        roster.resize(0);
        points.resize(0);
        SendScore();
        SetPhase(kWaiting);
    } else if(phase == kIntro) {
        if(arm_pending && t > 0.5f) {
            arm_pending = false;
            ArmPlayers();
        }
        if(round_number == 1 && intro_step == 0 && t > 2.0f) {
            Show(weapons_match ? "This is a fight to the death!" : "Two points to win!");
            intro_step = 1;
        }
        float fight_at = round_number == 1 ? 4.0f : 2.0f;
        if(t > fight_at && (AllUp() || t > fight_at + 3.0f)) {
            Show("Fight!");
            HealOwn();
            level.SendMessage("ohl arena fight");
            msh.StartFight();
            SetPhase(kFighting);
        }
    } else if(phase == kFighting) {
        if(t > 2.0f && shown_text == "Fight!") {
            Show("");
        }
        array<int> up;
        for(uint k = 0; k < roster.length(); ++k) {
            if(Up(roster[k])) {
                up.insertLast(roster[k]);
            }
        }
        if(up.length() <= 1) {
            round_winner = up.length() == 1 ? up[0] : -1;
            if(round_winner != -1) {
                ++points[roster.find(round_winner)];
                Show(PlayerName(round_winner) + " wins the round!");
            } else {
                Show("Nobody wins the round!");
            }
            SendScore();
            level.SendMessage("ohl arena over");
            msh.MatchEnded();
            SetPhase(kRoundOver);
        }
    } else if(phase == kRoundOver) {
        if(t > 3.0f) {
            int at = roster.find(round_winner);
            if(at >= 0 && points[at] >= max_points) {
                Show(PlayerName(round_winner) + " wins the match!");
                SetPhase(kMatchOver);
            } else {
                StartRound();
            }
        }
    } else if(phase == kMatchOver) {
        if(t > 4.0f) {
            NewMatch();
        }
    }

    // Late joiners see where things stand
    if(the_time > next_broadcast) {
        next_broadcast = the_time + 2.0f;
        level.SendMessage("ohl arena text " + shown_text);
        SendScore();
    }
}

void NewMatch() {
    SetUpMatch();
    roster.resize(0);
    points.resize(0);
    round_number = 0;
    StartRound();
}

void StartRound() {
    ++round_number;

    // Everyone here plays (late joiners from the next round on), each for themselves
    array<int> players = Players();
    for(uint k = 0; k < players.length(); ++k) {
        if(roster.find(players[k]) < 0) {
            roster.insertLast(players[k]);
            points.insertLast(0);
        }
    }

    // A weapon match arms everyone again each round (a draw replays it)
    for(uint k = 0; k < round_items.length(); ++k) {
        if(ObjectExists(round_items[k])) {
            QueueDeleteObjectID(round_items[k]);
        }
    }
    round_items.resize(0);

    for(uint k = 0; k < roster.length(); ++k) {
        int id = roster[k];
        vec3 pos, facing;
        SpotFor(k, pos, facing);
        Online_PlaceAvatar(id, pos, facing);
    }
    arm_pending = true;  // (ArmPlayers, half a second on)

    SendScore();
    Show(round_number == 1 ? intro : "Round " + round_number);
    SetPhase(kIntro);
}

// The round's weapons. Half a second after the old ones were deleted: a hand
// still holding one turns a new weapon away (aschar.as HandleEditorAttachment),
// and the deletes reach every machine first (NotifyItemDetach empties the hand)
void ArmPlayers() {
    for(uint k = 0; k < roster.length(); ++k) {
        int id = roster[k];
        string weapon = spot_weapons.length() > 0 ? spot_weapons[k % spot_weapons.length()] : "";
        if(weapon == "" || !MovementObjectExists(id) || ReadCharacterID(id).GetIntVar("species") == 1) {
            continue;  // (aschar.as _wolf: no hands)
        }
        int item_id = CreateObject(weapon, true);
        Object@ item = ReadObjectFromID(item_id);
        item.SetTranslation(ReadCharacterID(id).position);
        item.GetScriptParams().AddIntCheckbox("No Save", true);
        ReadObjectFromID(id).AttachItem(item, _at_grip, false);
        round_items.insertLast(item_id);
    }

    for(uint k = 0; k < item_paths.length(); ++k) {
        int item_id = CreateObject(item_paths[k], true);
        Object@ item = ReadObjectFromID(item_id);
        item.SetTranslation(item_spots[k] + vec3(0.0f, 0.5f, 0.0f));
        item.GetScriptParams().AddIntCheckbox("No Save", true);
        round_items.insertLast(item_id);
    }
}

// The players' characters here, in join order
array<int> Players() {
    array<int> ids;
    array<PlayerState>@ states = GetPlayerStates();
    for(uint k = 0; k < states.length(); ++k) {
        int id = states[k].avatar_id;
        if(id >= 0 && MovementObjectExists(id) && ids.find(id) < 0) {
            ids.insertLast(id);
        }
    }
    return ids;
}

// Players who left drop out; one who just joined fights for themselves
// (the engine gave them the host's team)
void DropGonePlayers() {
    array<int> players = Players();
    for(int k = int(roster.length()) - 1; k >= 0; --k) {
        if(players.find(roster[k]) < 0) {
            roster.removeAt(k);
            points.removeAt(k);
            score_dirty = true;
        }
    }
    for(uint k = 0; k < players.length(); ++k) {
        OwnTeam(players[k]);
    }
}

void OwnTeam(int id) {
    Object@ obj = ReadObjectFromID(id);
    ScriptParams@ params = obj.GetScriptParams();
    if(params.GetString("Teams") != "player" + id) {
        params.SetString("Teams", "player" + id);
        obj.UpdateScriptParams();
    }
}

bool Up(int id) {
    return MovementObjectExists(id) && ReadCharacterID(id).GetIntVar("knocked_out") == _awake;
}

bool AllUp() {
    for(uint k = 0; k < roster.length(); ++k) {
        if(!Up(roster[k])) {
            return false;
        }
    }
    return true;
}

string PlayerName(int id) {
    array<PlayerState>@ states = GetPlayerStates();
    for(uint k = 0; k < states.length(); ++k) {
        if(states[k].avatar_id == id && states[k].playername != "") {
            return states[k].playername;
        }
    }
    return "Player";
}

void HealOwn() {
    int k = GetPlayerCharacterID();  // (an index)
    if(k != -1) {
        ReadCharacter(k).ReceiveScriptMessage("restore_health");
    }
}

// ---- host: the match setup -------------------------------------------------

void FindMarkers() {
    marker_ids.resize(0);
    marker_names.resize(0);
    marker_types.resize(0);
    array<int> @ids = GetObjectIDs();
    for(uint i = 0; i < ids.length(); ++i) {
        ScriptParams@ params = ReadObjectFromID(ids[i]).GetScriptParams();
        if(!params.HasParam("Name")) {
            continue;
        }
        string name = params.GetString("Name");
        if(name == "arena_spawn" && params.HasParam("LocName") && params.GetString("LocName") != "") {
            marker_ids.insertLast(ids[i]);
            marker_names.insertLast(params.GetString("LocName"));
            marker_types.insertLast(-1);
        } else if(name == "character_spawn") {
            marker_ids.insertLast(ids[i]);
            marker_names.insertLast("");
            marker_types.insertLast(params.HasParam("game_type") ? atoi(params.GetString("game_type")) : -1);
        }
    }
}

string WeaponPath(const string &in name) {
    string w = name;
    if(w == "any") {
        array<string> any = {"knife", "big_sword", "sword", "spear"};
        w = any[rand() % 4];
    }
    if(w == "knife" || w == "big_sword" || w == "sword" || w == "spear") {
        return level.GetPath("weap_" + w);
    }
    return "";
}

vec3 MarkerPos(int at) {
    return ReadObjectFromID(marker_ids[at]).GetTranslation();
}

// Spots, weapons and items from one of the level's battle setups (as
// multi_arena.as picks them), or Stucco Courtyard's spawn markers
void SetUpMatch() {
    FindMarkers();
    spots.resize(0);
    spot_weapons.resize(0);
    item_paths.resize(0);
    item_spots.resize(0);
    weapons_match = false;
    intro = "Time to fight!";
    array<int> used;

    if(!SetUpFromBattle(used)) {
        // Stucco Courtyard: a game type's markers first; a third of the
        // matches everyone gets the same weapon (arena_level.as)
        intro = "Welcome to the arena!";
        int game_type = rand() % 3;
        for(uint i = 0; i < marker_ids.length(); ++i) {
            if(marker_types[i] == game_type) {
                used.insertLast(i);
            }
        }
        if(rand() % 3 == 0) {
            spot_weapons.insertLast(WeaponPath("any"));
            weapons_match = true;
        }
    }

    // The rest of the markers for more players (not on top of a used one)
    for(uint i = 0; i < marker_ids.length(); ++i) {
        bool near = false;
        for(uint k = 0; k < used.length(); ++k) {
            if(distance(MarkerPos(i), MarkerPos(used[k])) < 1.0f) {
                near = true;
            }
        }
        if(!near) {
            used.insertLast(i);
        }
    }

    arena_center = vec3(0.0f);
    for(uint k = 0; k < used.length(); ++k) {
        spots.insertLast(MarkerPos(used[k]));
        arena_center += spots[k];
    }
    if(spots.length() > 0) {
        arena_center /= float(spots.length());
    }
    max_points = weapons_match ? 1 : 2;
}

bool SetUpFromBattle(array<int> &inout used) {
    array<int> @ids = GetObjectIDs();
    for(uint i = 0; i < ids.length(); ++i) {
        ScriptParams@ params = ReadObjectFromID(ids[i]).GetScriptParams();
        if(!params.HasParam("Name") || params.GetString("Name") != "arena_battle" || !params.HasParam("Battles")) {
            continue;
        }
        JSON battles_json = params.GetJSON("Battles");
        JSONValue battles = battles_json.getRoot()["data"]["battles"];
        if(battles.size() == 0) {
            return false;
        }
        JSONValue battle = battles[int(uint(rand()) % battles.size())];
        JSONValue teams = battle["teams"];

        // Its fighters' spots, a team's first, the next team's first, ...
        int most = 0;
        for(uint t = 0; t < teams.size(); ++t) {
            most = max(most, int(teams[t]["members"].size()));
        }
        for(int m = 0; m < most; ++m) {
            for(uint t = 0; t < teams.size(); ++t) {
                JSONValue members = teams[t]["members"];
                if(m >= int(members.size())) {
                    continue;
                }
                JSONValue member = members[m];
                int at = marker_names.find(member["location"].asString());
                if(at < 0 || used.find(at) >= 0) {
                    continue;
                }
                used.insertLast(at);
                string weapon = member.isMember("weapon") ? WeaponPath(member["weapon"].asString()) : "";
                spot_weapons.insertLast(weapon);
                if(weapon != "") {
                    weapons_match = true;
                }
            }
        }
        bool armed = false;
        for(uint k = 0; k < spot_weapons.length(); ++k) {
            armed = armed || spot_weapons[k] != "";
        }
        if(!armed) {
            spot_weapons.resize(0);
        }

        // Its weapons on the ground
        JSONValue items = battle["items"];
        for(uint k = 0; k < items.size(); ++k) {
            JSONValue item = items[k];
            int at = marker_names.find(item["location"].asString());
            string weapon = WeaponPath(item.isMember("type") ? item["type"].asString() : "any");
            if(at >= 0 && weapon != "") {
                item_paths.insertLast(weapon);
                item_spots.insertLast(MarkerPos(at));
                weapons_match = true;
            }
        }

        JSONValue attributes = battle["attributes"];
        if(attributes.isMember("intro") && attributes["intro"].asString() != "") {
            intro = attributes["intro"].asString();
        }
        return true;
    }
    return false;
}

// Player k's spot, facing the middle; more players than spots: beside them
void SpotFor(uint k, vec3 &out pos, vec3 &out facing) {
    if(spots.length() == 0) {
        pos = vec3(0.0f);
        facing = vec3(0.0f, 0.0f, 1.0f);
        return;
    }
    vec3 base = spots[k % spots.length()];
    vec3 to_middle = arena_center - base;
    to_middle.y = 0.0f;
    facing = length(to_middle) > 0.1f ? normalize(to_middle) : vec3(0.0f, 0.0f, 1.0f);
    pos = k < spots.length() ? base : Online_SafeSpotNear(base, facing, int(k / spots.length()));
}

// ---- shown ----------------------------------------------------------------

void Show(const string &in text) {
    ShowLocal(text);
    level.SendMessage("ohl arena text " + text);
}

void ShowLocal(const string &in text) {
    if(text == shown_text) {
        return;
    }
    shown_text = text;
    if(text != "") {
        DrawText(text_id, text, 48);
    }
}

void SendScore() {
    string msg = "ohl arena score " + max_points;
    for(uint k = 0; k < roster.length(); ++k) {
        msg += " #" + roster[k] + " " + points[k];
    }
    level.SendMessage(msg);

    score_ids = roster;
    score_points = points;
    score_max = max_points;
    score_dirty = true;
}

// "ohl arena score <max> #<char> <points> ..."
void ReceiveScore(const string &in msg) {
    TokenIterator token_iter;
    token_iter.Init();
    for(int k = 0; k < 4; ++k) {
        if(!token_iter.FindNextToken(msg)) {
            return;
        }
    }
    score_max = atoi(token_iter.GetToken(msg));
    score_ids.resize(0);
    score_points.resize(0);
    while(token_iter.FindNextToken(msg)) {
        string id = token_iter.GetToken(msg);
        if(!token_iter.FindNextToken(msg)) {
            break;
        }
        if(id.length() > 1 && id.substr(0, 1) == "#") {
            score_ids.insertLast(atoi(id.substr(1)));
            score_points.insertLast(atoi(token_iter.GetToken(msg)));
        }
    }
    score_dirty = true;
}

void DrawScoreboard() {
    TextCanvasTexture @text = level.GetTextElement(score_id);
    text.ClearTextCanvas();
    if(score_ids.length() == 0) {
        text.UploadTextCanvasToTexture();
        return;
    }
    string font_str = level.GetPath("font");
    TextStyle style;
    style.font_face_id = GetFontFaceID(font_str, 36);
    text.SetPenColor(255, 255, 255, 255);
    text.SetPenRotation(0.0f);

    int own = GetPlayerCharacterID();
    int own_id = own != -1 ? ReadCharacter(own).GetID() : -1;
    int line = 0;
    text.SetPenPosition(vec2(0, 40));
    text.AddText(score_max == 1 ? "First to 1 point" : "First to " + score_max + " points", style, UINT32MAX);
    for(uint k = 0; k < score_ids.length() && k < 8; ++k) {
        ++line;
        text.SetPenPosition(vec2(0, 40 + line * 40));
        text.AddText(PlayerName(score_ids[k]) + (score_ids[k] == own_id ? " (you)" : "") + ":  " + score_points[k], style, UINT32MAX);
    }
    text.UploadTextCanvasToTexture();
}

int TextInit(int width, int height) {
    int id = level.CreateTextElement();
    TextCanvasTexture @text = level.GetTextElement(id);
    text.Create(width, height);
    return id;
}

void DrawText(int id, const string &in str, int size) {
    TextCanvasTexture @text = level.GetTextElement(id);
    text.ClearTextCanvas();
    string font_str = level.GetPath("font");
    TextStyle style;
    style.font_face_id = GetFontFaceID(font_str, size);
    text.SetPenPosition(vec2(0, 256));
    text.SetPenColor(255, 255, 255, 255);
    text.SetPenRotation(0.0f);
    text.AddText(str, style, UINT32MAX);
    text.UploadTextCanvasToTexture();
}

// ---- the crowd (multi_arena.as) -------------------------------------------

void UpdateCrowd() {
    float total_char_speed = 0.0f;
    int num = GetNumCharacters();
    for(int i = 0; i < num; ++i) {
        MovementObject@ char = ReadCharacter(i);
        if(char.GetIntVar("knocked_out") == _awake) {
            total_char_speed += length(char.velocity);
        }
    }
    float excitement_decay_rate = 3.0f / (1.0f + total_char_speed / 14.0f);
    audience_excitement *= pow(0.05f, 0.001f * excitement_decay_rate);
    float target_crowd_cheer_amount = audience_excitement * 0.1f + 0.15f;
    crowd_cheer_vel += (target_crowd_cheer_amount - crowd_cheer_amount) * time_step * 10.0f;
    if(crowd_cheer_vel > 0.0f) {
        crowd_cheer_vel *= 0.99f;
    } else {
        crowd_cheer_vel *= 0.95f;
    }
    crowd_cheer_amount += crowd_cheer_vel * time_step;
    crowd_cheer_amount = max(crowd_cheer_amount, 0.1f);
    if(audience_sound_handle != -1) {
        SetSoundGain(audience_sound_handle, crowd_cheer_amount * 2.0f);
        SetSoundPitch(audience_sound_handle, min(0.8f + crowd_cheer_amount * 0.5f, 1.2f));
    }
}
