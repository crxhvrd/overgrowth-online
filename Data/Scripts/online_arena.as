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
//                 Bots (the host's AI fighters, config "oo_arena_bots": fill the
//                 match up to that many fighters, 0 none; 2 if never set): a
//                 player who joins takes a bot's place from the next round.
//                 The randomizer (config "oo_arena_random"): every round each
//                 fighter, players and bots, becomes a random species and look
//                 with random gear; when it is switched off the players get
//                 their own characters back.
//                 Everyone has knockout shields (kArenaShields; full again at
//                 each round start).
//
//                 The host runs the matches and tells the guests:
//                   ohl arena text <text>                    the announcement ("" none)
//                   ohl arena score <max> (#<char> <points>)...
//                   ohl arena fight                          "Fight!": everyone heals
//                   ohl arena over                           the round is over
//                   ohl arena become #<char> <path>|own      the randomizer: that player's machine switches it
//                   ohl arena disarm                         empty every character's hands and sheaths (old weapons go)
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

array<int> bots;           // the host's AI fighters
int bot_serial = 0;

// The randomizer: this round's (per roster place) character and gear
bool random_round = false;
bool random_last = false;    // the round before was random (off now: own characters back)
bool become_pending = false; // this round switches characters (sent as it starts)
bool place_pending = false;  // a switching round: the fighters go to their spots once switched
float arm_at = 0.5f;         // the round's weapons, this long after its start
bool clear_pending = false;  // the old weapons go once every machine emptied its characters' hands

const int kArenaShields = 5;  // aschar.as "Knockout Shield": hits that don't knock you out
float next_shields = 0.0f;
array<string> rnd_paths;     // character file, "own" (the player's own), "" unchanged
array<string> rnd_hand;      // weapon in hand, "" none
array<bool> rnd_knife;       // a knife in the sheath too
array<bool> rnd_wolf;

const array<array<string>> kRndCharacters = {
    {"turner", "pale_turner", "male_rabbit_1", "male_rabbit_2", "male_rabbit_3", "female_rabbit_1", "female_rabbit_2",
     "female_rabbit_3", "raider_rabbit", "guard", "guard2", "base_guard", "heavy", "heavy2", "commander", "civ",
     "pale_rabbit_civ", "ancient", "femaleancient", "bijou", "jairo", "donatas", "reaper", "ghost"},
    {"cat", "male_cat", "female_cat", "striped_cat", "fancy_striped_cat", "cobus", "drika", "ferdi"},
    {"dog_soldier", "lt_dog_big", "lt_dog_female", "lt_dog_male_1", "lt_dog_male_2"},
    {"rat", "female_rat", "hooded_rat"},
    {"wolf", "male_wolf"}
};
array<int> roster;         // the players' (and bots') characters in this match
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

    if(the_time > next_shields) {
        next_shields = the_time + 1.0f;
        OwnShields();
    }

    if(Online_IsHosting()) {
        HostUpdate();
    }
}

// Knockout shields for the characters this machine runs (its player's; the
// host's also the bots)
void OwnShields() {
    for(int i = 0, num = GetNumCharacters(); i < num; ++i) {
        MovementObject@ char = ReadCharacter(i);
        if(!((char.controlled && !char.remote) || bots.find(char.GetID()) >= 0)) {
            continue;
        }
        Object@ obj = ReadObjectFromID(char.GetID());
        ScriptParams@ params = obj.GetScriptParams();
        if(!params.HasParam("Knockout Shield")) {
            params.AddInt("Knockout Shield", kArenaShields);
            obj.UpdateScriptParams();
        } else if(params.GetInt("Knockout Shield") != kArenaShields) {
            params.SetInt("Knockout Shield", kArenaShields);
            obj.UpdateScriptParams();
        }
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
        } else if(what == "disarm") {
            DisarmAll();
        } else if(what == "become" && token_iter.FindNextToken(msg)) {
            // "become #<char> <path>": ours switches here (the engine tells the others)
            string ref = token_iter.GetToken(msg);
            int id = ref.length() > 1 ? atoi(ref.substr(1)) : -1;
            if(token_iter.FindNextToken(msg) && MovementObjectExists(id) && ReadCharacterID(id).controlled && !ReadCharacterID(id).remote) {
                Become(id, token_iter.GetToken(msg));
            }
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
        if(Players().length() + WantedBots() >= 2) {
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
        if(clear_pending && t > 0.3f) {
            clear_pending = false;
            ClearWeapons();
            if(become_pending) {
                become_pending = false;
                SendBecome();
            }
        }
        if(place_pending && t > 0.7f) {
            place_pending = false;
            PlaceFighters();
        }
        if(arm_pending && t > arm_at) {
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
            BotsFight();
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

    // Bots: as many as wanted now (a joined player takes one's place)
    SyncBots();

    // Everyone here plays (late joiners from the next round on), each for themselves
    array<int> players = Players();
    for(uint k = 0; k < bots.length(); ++k) {
        players.insertLast(bots[k]);
    }
    for(uint k = 0; k < players.length(); ++k) {
        if(roster.find(players[k]) < 0) {
            roster.insertLast(players[k]);
            points.insertLast(0);
        }
    }

    // Every weapon from before goes, in hands and on the ground (this round's
    // are made new). First every machine empties the hands and sheaths of the
    // characters it has (a script kept one deleted under it: "There is no item
    // object"), then they are deleted
    DisarmAll();
    level.SendMessage("ohl arena disarm");
    clear_pending = true;

    // A randomizer round switches everyone (with the deletes) and places them
    // after (a body switched where it stood came out stuck in walls and floors)
    PlanRandomizer();
    place_pending = become_pending;
    if(become_pending) {
        arm_at = 1.4f;
    } else {
        PlaceFighters();
        arm_at = 1.0f;
    }
    arm_pending = true;  // (ArmPlayers, after the old weapons are gone everywhere)

    SendScore();
    Show(round_number == 1 ? intro : "Round " + round_number);
    SetPhase(kIntro);
}

// Every fighter to its spot, stood up (and the bots held until "Fight!")
void PlaceFighters() {
    for(uint k = 0; k < roster.length(); ++k) {
        int id = roster[k];
        vec3 pos, facing;
        SpotFor(k, pos, facing);
        Online_PlaceAvatar(id, pos, facing);
        if(bots.find(id) >= 0) {
            ReadCharacterID(id).ReceiveScriptMessage("set_combat_allowed false");  // (until "Fight!")
        }
    }
}

// The round's weapons. Half a second after the old ones were deleted: a hand
// still holding one turns a new weapon away (aschar.as HandleEditorAttachment),
// and the deletes reach every machine first (NotifyItemDetach empties the hand)
void ArmPlayers() {
    for(uint k = 0; k < roster.length(); ++k) {
        int id = roster[k];
        bool random = random_round && k < rnd_hand.length();
        string weapon = random ? rnd_hand[k] : (spot_weapons.length() > 0 ? spot_weapons[k % spot_weapons.length()] : "");
        if(!MovementObjectExists(id) || ReadCharacterID(id).GetIntVar("species") == 1 || (random && rnd_wolf[k])) {
            continue;  // (aschar.as _wolf: no hands)
        }
        if(weapon != "") {
            Arm(id, weapon, _at_grip);
        }
        if(random && rnd_knife[k]) {
            Arm(id, WeaponPath("knife"), _at_sheathe);
        }
    }

    for(uint k = 0; k < item_paths.length(); ++k) {
        int item_id = CreateObject(item_paths[k], true);
        Object@ item = ReadObjectFromID(item_id);
        item.SetTranslation(item_spots[k] + vec3(0.0f, 0.5f, 0.0f));
        item.GetScriptParams().AddIntCheckbox("No Save", true);
        round_items.insertLast(item_id);
    }
}

// Every character here lets go of what it holds and carries (every machine,
// before the host deletes the old weapons)
const string kDisarm = "for(int s = 0; s < 6; ++s) { int w = weapon_slots[s]; if(w != -1) { "
                       "if(ItemExists(w)) { this_mo.DetachItem(w); } NotifyItemDetach(w); } } ";

void DisarmAll() {
    for(int i = 0, num = GetNumCharacters(); i < num; ++i) {
        ReadCharacter(i).Execute(kDisarm);
    }
}

// The weapons the matches made (ours, and any "No Save" one: the level's own
// stay), wherever they are
void ClearWeapons() {
    array<int> @items = GetObjectIDsType(_item_object);
    for(uint k = 0; k < items.length(); ++k) {
        if(round_items.find(items[k]) >= 0 || ReadObjectFromID(items[k]).GetScriptParams().HasParam("No Save")) {
            QueueDeleteObjectID(items[k]);
        }
    }
    round_items.resize(0);
}

void Arm(int id, const string &in weapon, AttachmentType how) {
    int item_id = CreateObject(weapon, true);
    Object@ item = ReadObjectFromID(item_id);
    item.SetTranslation(ReadCharacterID(id).position);
    item.GetScriptParams().AddIntCheckbox("No Save", true);
    ReadObjectFromID(id).AttachItem(item, how, false);
    round_items.insertLast(item_id);
}

// ---- host: the randomizer ---------------------------------------------------

bool ArenaRandomizer() {
    return ConfigHasKey("oo_arena_random") && GetConfigValueBool("oo_arena_random");
}

// This round's characters and gear (on: random for everyone; switched off:
// the players their own characters back, the bots stay as they are)
void PlanRandomizer() {
    random_round = ArenaRandomizer();
    rnd_paths.resize(0);
    rnd_hand.resize(0);
    rnd_knife.resize(0);
    rnd_wolf.resize(0);
    array<string> weapons = {"", "", "knife", "knife", "sword", "sword", "big_sword", "spear", "spear"};
    for(uint k = 0; k < roster.length(); ++k) {
        if(random_round) {
            int s = rand() % int(kRndCharacters.length());
            const array<string>@ kinds = kRndCharacters[s];
            rnd_paths.insertLast("Data/Characters/" + kinds[rand() % int(kinds.length())] + ".xml");
            rnd_wolf.insertLast(s == 4);
            string w = weapons[rand() % int(weapons.length())];
            rnd_hand.insertLast(w == "" ? "" : WeaponPath(w));
            rnd_knife.insertLast(rand() % 10 < 3);
        } else {
            rnd_paths.insertLast(random_last && bots.find(roster[k]) < 0 ? "own" : "");
            rnd_wolf.insertLast(false);
            rnd_hand.insertLast("");
            rnd_knife.insertLast(false);
        }
    }
    become_pending = random_round || random_last;
    random_last = random_round;
}

// Each machine switches its own characters: the host its own and the bots
void SendBecome() {
    for(uint k = 0; k < roster.length() && k < rnd_paths.length(); ++k) {
        int id = roster[k];
        if(rnd_paths[k] == "" || !MovementObjectExists(id)) {
            continue;
        }
        MovementObject@ char = ReadCharacterID(id);
        if(bots.find(id) >= 0 || (char.controlled && !char.remote)) {
            Become(id, rnd_paths[k]);
        } else {
            level.SendMessage("ohl arena become #" + id + " " + rnd_paths[k]);
        }
    }
}

// A character this machine runs becomes `path` ("own": the player's own choice)
void Become(int id, const string &in path) {
    string p = path;
    if(p == "own") {
        p = GetConfigValueString("mp_character");
        if(p == "" || !FileExists(p)) {
            p = level.GetPath("char_player");
        }
    }
    if(p == "" || !FileExists(p)) {
        return;
    }
    ReadCharacterID(id).Execute(kDisarm + "SwitchCharacter(\"" + p + "\"); FixDiscontinuity();");  // (SwitchCharacter detaches all but kept the sheaths named)
    if(path == "own") {
        Online_RestoreOwnCharacter();  // (its colours, as the menu set them)
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
    for(int k = int(bots.length()) - 1; k >= 0; --k) {
        if(!MovementObjectExists(bots[k])) {
            bots.removeAt(k);
        }
    }
    for(int k = int(roster.length()) - 1; k >= 0; --k) {
        if(players.find(roster[k]) < 0 && bots.find(roster[k]) < 0) {
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
    if(ObjectExists(id) && ReadObjectFromID(id).GetScriptParams().HasParam("Bot Name")) {
        return ReadObjectFromID(id).GetScriptParams().GetString("Bot Name");  // (the host's, on every machine)
    }
    return "Player";
}

void HealOwn() {
    int k = GetPlayerCharacterID();  // (an index)
    if(k != -1) {
        ReadCharacter(k).ReceiveScriptMessage("restore_health");
    }
}

// ---- host: bots -----------------------------------------------------------

// How many fighters the host wants the match filled up to (0: no bots)
int ArenaFighters() {
    return ConfigHasKey("oo_arena_bots") ? min(8, GetConfigValueInt("oo_arena_bots")) : 2;
}

int WantedBots() {
    int fighters = ArenaFighters();
    return fighters <= 0 ? 0 : max(0, fighters - int(Players().length()));
}

// Bots in or out (round starts): the newest leave first
void SyncBots() {
    int want = WantedBots();
    while(int(bots.length()) > want) {
        int id = bots[bots.length() - 1];
        bots.removeLast();
        int at = roster.find(id);
        if(at >= 0) {
            roster.removeAt(at);
            points.removeAt(at);
        }
        if(ObjectExists(id)) {
            QueueDeleteObjectID(id);
        }
    }
    while(int(bots.length()) < want) {
        bots.insertLast(MakeBot());
    }
}

// An AI fighter of the arena's own kinds (multi_arena_enemy_spawn.as), middling
int MakeBot() {
    array<string> kinds = {"char_guard", "char_raider", "char_civ"};
    array<int> fur_channels = {1, 0, -1};
    int kind = rand() % 3;
    string path = level.GetPath(kinds[kind]);
    if(path == "") {
        path = level.GetPath("char_player");
        kind = 2;
    }
    int id = CreateObject(path, true);
    Object@ obj = ReadObjectFromID(id);
    obj.SetTranslation((spots.length() > 0 ? spots[0] : arena_center) + vec3(0.0f, 0.5f, 0.0f));
    if(fur_channels[kind] >= 0) {
        array<vec3> furs = {vec3(0.45f, 0.33f, 0.22f), vec3(0.3f, 0.3f, 0.3f), vec3(0.6f, 0.5f, 0.38f), vec3(0.18f, 0.14f, 0.1f), vec3(0.75f, 0.72f, 0.68f)};
        obj.SetPaletteColor(fur_channels[kind], furs[rand() % int(furs.length())]);
    }
    ++bot_serial;
    ScriptParams@ params = obj.GetScriptParams();
    params.AddIntCheckbox("No Save", true);
    params.AddString("Bot Name", "Bot " + bot_serial);
    params.AddInt("Knockout Shield", kArenaShields);
    params.SetString("Teams", "bot" + id);  // everyone for themselves
    float d = 0.5f;
    params.SetFloat("Character Scale", mix(RangedRandomFloat(0.9f, 1.0f), RangedRandomFloat(1.0f, 1.1f), d));
    params.SetFloat("Block Follow-up", mix(RangedRandomFloat(0.01f, 0.25f), RangedRandomFloat(0.75f, 1.0f), d));
    params.SetFloat("Block Skill", mix(RangedRandomFloat(0.01f, 0.25f), RangedRandomFloat(0.5f, 0.8f), d));
    params.SetFloat("Movement Speed", mix(RangedRandomFloat(0.8f, 1.0f), RangedRandomFloat(0.9f, 1.1f), d));
    params.SetFloat("Attack Speed", mix(RangedRandomFloat(0.8f, 1.0f), RangedRandomFloat(0.9f, 1.1f), d));
    float damage = mix(RangedRandomFloat(0.3f, 0.5f), RangedRandomFloat(0.9f, 1.1f), d);
    params.SetFloat("Attack Knockback", damage);
    params.SetFloat("Attack Damage", damage);
    params.SetFloat("Aggression", RangedRandomFloat(0.25f, 0.75f));
    params.SetFloat("Ground Aggression", d);
    params.SetFloat("Damage Resistance", mix(RangedRandomFloat(0.6f, 0.8f), RangedRandomFloat(0.9f, 1.1f), d));
    obj.UpdateScriptParams();
    Log(info, "Overgrowth Online: arena bot " + id + " (" + path + ")");
    return id;
}

// "Fight!": the bots may fight, and know everyone in the match
void BotsFight() {
    for(uint k = 0; k < bots.length(); ++k) {
        if(!MovementObjectExists(bots[k])) {
            continue;
        }
        MovementObject@ bot = ReadCharacterID(bots[k]);
        bot.ReceiveScriptMessage("set_combat_allowed true");
        for(uint j = 0; j < roster.length(); ++j) {
            if(roster[j] != bots[k] && MovementObjectExists(roster[j])) {
                bot.ReceiveScriptMessage("notice " + roster[j]);
            }
        }
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
