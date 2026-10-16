//-----------------------------------------------------------------------------
//           Name: aio_battle.as
//    Description: Overhaul "Large Battles": runs inside the level the setup
//                 menu (aio_battle_menu.as) loaded. Part of the level hook
//                 (included by overhaul_menu.as). Clears the level's NPCs, places
//                 the teams on the navmesh, spawns them in batches, counts
//                 down, runs the fight (reinforcement waves, body clean-up,
//                 stalemate breaking), declares the winner. The player either
//                 spectates with a free / follow camera or fights on a team.
//-----------------------------------------------------------------------------

#include "aio_battle_common.as"

enum BattlePhase {
    kBattleOff,
    kBattleWait,       // level settling
    kBattlePlace,      // the player places the teams by hand
    kBattleSpawn,      // spawning in batches
    kBattleCountdown,
    kBattleFight,
    kBattleOver
}

class BattleFighter {
    int id = -1;
    int team = 0;
    float spawn_time = 0.0f;
    bool set_up = false;   // colours, facing, hold-fire applied (needs the script running)
    float ko_time = -1.0f;
    array<int> items;
}

class BattleTeamState {
    vec3 base;
    vec3 facing;
    array<vec3> slots;
    int next_slot = 0;
    int spawned = 0;
    int alive = 0;
    int fallen = 0;
    array<float> reinforce_due;  // replacement arrival times
}

BattleSettings battle;
BattlePhase battle_phase = kBattleOff;
float battle_phase_start = 0.0f;
array<BattleFighter@> battle_fighters;
array<BattleTeamState@> battle_teams;
array<int> battle_spawn_queue;      // team index per fighter still to spawn
array<int> battle_items;            // every weapon we created
int battle_query_id = -1;           // character answering navmesh questions
int battle_player_id = -1;
vec3 battle_center;
bool battle_center_set = false;     // kept across rematches (the player moves)
bool battle_center_floating = false;
int battle_winner = -1;             // team index, -2 draw
float battle_fight_start = 0.0f;
float battle_fight_end = 0.0f;
float battle_next_tick = 0.0f;
float battle_next_hunt = 0.0f;
bool battle_panel_open = false;
bool battle_spectating = false;
bool battle_player_out = false;
float battle_player_ko_time = -1.0f;
int battle_last_go = 4;

// Online (overhaul_online.as): the host's other players, and a client's mirror
array<int> battle_remote_ids;       // host: other players' characters
array<int> battle_remote_teams;
bool battle_mirror = false;         // client: showing the host's battle
array<int> battle_net_reserve;      // client: each team's fighters still to come
float battle_next_sync = 0.0f;
float battle_next_cfg = 0.0f;

// Online lobby: before the battle the host lists the teams and who picked
// which ("aiob lobby"); players pick ("ohr aiob pick"); the host starts it
array<int> battle_pick_ids;         // host: players' picks (their characters)
array<int> battle_pick_teams;       // -1 = any team
bool battle_lobby_start = false;    // host: Start pressed
float battle_lobby_next = 0.0f;
float battle_lobby_seen = -100.0f;  // client: the host's last lobby message
array<string> battle_lobby_names;   // client: the host's teams
array<vec3> battle_lobby_colors;
array<int> battle_lobby_ids;        // both: every player and their pick, as the host has it
array<int> battle_lobby_teams;
bool battle_lobby_open = true;      // the panel is up (it takes the mouse)

// Manual placement
array<vec3> battle_place_pos;
array<bool> battle_place_ok;
bool battle_place_loaded = false;   // saved spots read once per level load (rematches keep the current ones)
int battle_place_team = 0;
bool battle_place_looking = false;  // right mouse held: looking around
string battle_place_msg;
float battle_place_msg_until = 0.0f;

// Spectator camera
vec3 battle_cam_pos;
float battle_cam_yaw = 0.0f;
float battle_cam_pitch = -25.0f;
int battle_follow = -1;
float battle_cam_last = 0.0f;

void BattleLog(const string &in text) {
    Log(info, "[AIOB] " + text);
}

bool BattleActive() {
    return battle_phase != kBattleOff;
}

void BattleSetPhase(BattlePhase phase) {
    battle_phase = phase;
    battle_phase_start = the_time;
}

// ---- level hook entry points ------------------------------------------------

// Init comes too early (no level path yet, and the hook's state does not
// survive to the first update): the pending battle is picked up there instead
void AIOBattleInit(const string &in level_name) {
    battle_pending_checked = false;
}

bool battle_pending_checked = false;

void BattleCheckPending() {
    battle_pending_checked = true;

    if(!ConfigHasKey("aiob_pending") || !GetConfigValueBool("aiob_pending")) {
        return;
    }

    SetConfigValueBool("aiob_pending", false);
    SaveConfig();
    battle.Load();
    battle_center_set = false;
    battle_check_level = true;
    BattleSetPhase(kBattleWait);
    BattleLog("battle pending");
}

// First update: the setup menu set "pending" right before loading its map, so
// this is the battle level unless the path says otherwise
bool battle_check_level = false;

bool BattleLevelOk() {
    battle_check_level = false;
    string have = GetCurrLevelRelPath();
    BattleLog("level=" + have + " want=" + battle.level_path);

    if(have != "" && BattleLevelKey(have) != BattleLevelKey(battle.level_path)) {
        BattleLog("not the battle level, staying out");
        battle_phase = kBattleOff;
        return false;
    }

    return true;
}

void AIOBattleReceive(const string &in message) {
    if(message.length() > 9 && message.substr(0, 9) == "ohl aiob ") {
        BattleMirrorReceive(MPTokens(message));
        return;
    }

    if(MPHost() && message.length() > 4 && message.substr(0, 4) == "ohr " && message.findFirst(" aiob pick ") > 0) {
        BattleLobbyPick(MPTokens(message));
        return;
    }

    if(BattleActive() && message == "reset") {
        BattleLog("level reset: restarting the battle");
        BattleRestart();
    }
}

bool AIOBattleHasFocus() {
    return (battle_phase == kBattleOver && battle_panel_open) || (battle_phase == kBattlePlace && !battle_place_looking) ||
        (BattleLobbyShown() && battle_lobby_open);
}

void AIOBattleUpdate(int is_paused) {
    if(!battle_pending_checked) {
        BattleCheckPending();
    }

    if(BattleLobbyShown() && GetInputPressed(0, "skip_dialogue")) {
        battle_lobby_open = !battle_lobby_open;
    }

    if(battle_mirror) {
        if(battle_phase == kBattleOver && !battle_panel_open && GetInputPressed(0, "skip_dialogue")) {
            battle_panel_open = true;
        }
        return;  // the host runs it
    }

    if(!BattleActive()) {
        return;
    }

    if(MPHost() && battle_phase != kBattleWait && battle_phase != kBattlePlace && the_time >= battle_next_sync) {
        battle_next_sync = the_time + 0.5f;
        BattleSendState();

        if(the_time >= battle_next_cfg) {
            battle_next_cfg = the_time + 3.0f;  // late joiners
            BattleSendConfig();
        }
    }

    if(battle_check_level && !BattleLevelOk()) {
        return;
    }

    if(battle_phase == kBattleWait) {
        // Online: the lobby holds it until the host starts it (or, with
        // aiob_online_wait, until that many players are in: the tests)
        bool wait_cfg = ConfigHasKey("aiob_online_wait") && GetConfigValueBool("aiob_online_wait");
        int want = ConfigHasKey("aiob_online_players") ? GetConfigValueInt("aiob_online_players") : 2;
        bool lobby = wait_cfg;

        if(MPHost()) {
            BattleLobbySend();
            lobby = !battle_lobby_start && !(wait_cfg && mp_players >= want);
        }

        if(lobby) {
            battle_phase_start = the_time;
        } else if(the_time - battle_phase_start > 1.0f) {
            BattlePrepare();
        }
    } else if(battle_phase == kBattlePlace) {
        BattleUpdatePlacement();
    } else if(battle_phase == kBattleSpawn) {
        BattleSpawnBatch();
    } else if(battle_phase == kBattleCountdown) {
        BattleSetupNewFighters();

        if(the_time >= battle_next_tick) {
            battle_next_tick = the_time + 0.25f;
            BattleTick();  // counts only: the fight rules wait for the start
        }

        int left = int(ceil(3.0f - (the_time - battle_phase_start)));

        if(left < battle_last_go) {
            battle_last_go = left;
        }

        if(the_time - battle_phase_start >= 3.0f) {
            BattleGo();
        }
    } else if(battle_phase == kBattleFight || battle_phase == kBattleOver) {
        BattleSetupNewFighters();

        if(the_time >= battle_next_tick) {
            battle_next_tick = the_time + 0.25f;
            BattleTick();
        }
    }

    if(battle_phase == kBattleOver && !battle_panel_open && GetInputPressed(0, "skip_dialogue")) {
        battle_panel_open = true;
    }

    BattleUpdatePlayer();
    BattleUpdateCamera(is_paused != 0);
}

// ---- preparing ----------------------------------------------------------------

void BattleRestart() {
    for(uint i = 0; i < battle_fighters.length(); ++i) {
        if(MovementObjectExists(battle_fighters[i].id)) {
            QueueDeleteObjectID(battle_fighters[i].id);
        }
    }

    for(uint i = 0; i < battle_items.length(); ++i) {
        if(ObjectExists(battle_items[i])) {
            QueueDeleteObjectID(battle_items[i]);
        }
    }

    battle_fighters.resize(0);
    battle_items.resize(0);
    battle_spawn_queue.resize(0);
    battle_panel_open = false;
    battle_winner = -1;
    battle_player_ko_time = -1.0f;
    BattleSetPhase(kBattleWait);
}

void BattlePrepare() {
    battle_player_id = -1;
    vec3 sum;
    int counted = 0;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.controlled) {
            battle_player_id = char.GetID();
        }
    }

    // Online every player's character is "controlled" here: ours is the one the engine names
    if(MPOnline() && mp_me != -1 && MovementObjectExists(mp_me)) {
        battle_player_id = mp_me;
    }

    // Clear the level's own NPCs (not ours: the list is empty on a fresh start)
    for(int i = GetNumCharacters() - 1; i >= 0; --i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.controlled || BattleIsOurs(char.GetID())) {
            continue;
        }

        sum += char.position;
        ++counted;

        if(!battle.keep_level_npcs) {
            QueueDeleteObjectID(char.GetID());
        }
    }

    battle_query_id = battle_player_id;

    if(!battle_center_set) {
        battle_center_set = true;

        if(battle_player_id != -1) {
            battle_center = ReadCharacterID(battle_player_id).position;
        } else if(counted > 0) {
            battle_center = sum / float(counted);
        }

        vec3 snapped;

        if(BattleGround(battle_center, battle_center, snapped)) {
            battle_center = snapped;
        } else {
            battle_center_floating = true;  // e.g. the player starts on a pillar: settle on the bases' height
        }
    }

    if(battle.manual_place) {
        BattleBeginPlacement();
        return;
    }

    BattleFinishPrepare();
}

// Bases, spawn spots, the player, the spawn order
void BattleFinishPrepare() {
    BattlePlan();

    if(battle_center_floating && battle_teams.length() > 0) {
        battle_center_floating = false;
        float y = 0.0f;

        for(uint t = 0; t < battle_teams.length(); ++t) {
            y += battle_teams[t].base.y;
        }

        battle_center.y = y / float(battle_teams.length());
    }

    BattleSetUpPlayer();
    BattleSetUpRemotePlayers();
    BattleSendConfig();

    // Spawn order: round-robin over the teams so no team gets a head start
    battle_spawn_queue.resize(0);
    int most = 0;

    for(int t = 0; t < battle.num_teams; ++t) {
        most = max(most, battle.teams[t].count);
    }

    for(int k = 0; k < most; ++k) {
        for(int t = 0; t < battle.num_teams; ++t) {
            if(k < battle.teams[t].count) {
                battle_spawn_queue.insertLast(t);
            }
        }
    }

    BattleLog("prepared center=" + BattleVec(battle_center) + " teams=" + battle.num_teams + " fighters=" + battle_spawn_queue.length() +
        " query=" + battle_query_id);
    BattleSetPhase(kBattleSpawn);
}

bool BattleIsOurs(int id) {
    for(uint i = 0; i < battle_fighters.length(); ++i) {
        if(battle_fighters[i].id == id) {
            return true;
        }
    }

    return false;
}

string BattleVec(const vec3 &in v) {
    return "(" + int(v.x) + "," + int(v.y) + "," + int(v.z) + ")";
}

// Navmesh point near `p` (through a character's script: level scripts have no
// navmesh access)
bool BattleNavSnap(const vec3 &in p, vec3 &out q) {
    q = p;

    if(battle_query_id == -1 || !MovementObjectExists(battle_query_id) || p.x != p.x || p.y != p.y || p.z != p.z) {
        return false;
    }

    MovementObject@ qc = ReadCharacterID(battle_query_id);
    qc.Execute("AIOBattleNavPoint(" + p.x + ", " + p.y + ", " + p.z + ");");

    if(!qc.GetBoolVar("aiob_q_ok")) {
        return false;
    }

    q = vec3(qc.GetFloatVar("aiob_q_x"), qc.GetFloatVar("aiob_q_y"), qc.GetFloatVar("aiob_q_z"));
    return true;
}

bool BattleReachable(const vec3 &in a, const vec3 &in b) {
    if(battle_query_id == -1 || !MovementObjectExists(battle_query_id)) {
        return true;
    }

    MovementObject@ qc = ReadCharacterID(battle_query_id);
    qc.Execute("AIOBattleReachable(" + a.x + ", " + a.y + ", " + a.z + ", " + b.x + ", " + b.y + ", " + b.z + ");");
    return qc.GetBoolVar("aiob_q_ok");
}

// Ground under (x, z) near the height of `near`, snapped to the navmesh
bool BattleGround(const vec3 &in p, const vec3 &in near, vec3 &out q) {
    // From well above: a start inside a hill would find its far side
    vec3 top = vec3(p.x, near.y + 100.0f, p.z);
    vec3 bottom = vec3(p.x, near.y - 100.0f, p.z);
    vec3 hit = col.GetRayCollision(top, bottom);
    q = p;

    if(hit == bottom) {
        return false;
    }

    // The top surface may be a canopy or a roof: only walkable ground right there counts
    return BattleNavSnap(hit + vec3(0.0f, 0.3f, 0.0f), q) && distance(q, hit) < 2.5f;
}

float battle_base_angle = 0.0f;

// Automatic base for team t (ring, scattered or two fronts)
vec3 BattleAutoBase(int t, vec3 &out dir) {
    int n = battle.num_teams;
    float angle = battle_base_angle + t * 6.2832f / n;
    float lateral = 0.0f;

    if(battle.layout == 2) {
        // Two fronts: even teams on one side, odd on the other, side by side
        int per_side = (n + 1) / 2;
        angle = battle_base_angle + (t % 2) * 3.1416f;
        lateral = ((t / 2) - (per_side - 1) * 0.5f) * 9.0f;
    }

    dir = vec3(cos(angle), 0.0f, sin(angle));

    if(battle.layout != 1) {
        // Reachable ground: the planned direction first, then turned up to 90
        // degrees either way, then closer in
        const array<float> turns = {0.0f, 0.52f, -0.52f, 1.05f, -1.05f, 1.57f, -1.57f};

        for(float f = 1.0f; f > 0.2f; f -= 0.15f) {
            for(uint k = 0; k < turns.length(); ++k) {
                float a = angle + turns[k];
                vec3 d = vec3(cos(a), 0.0f, sin(a));
                vec3 side = vec3(-d.z, 0.0f, d.x);
                vec3 q;

                if(BattleGround(battle_center + d * battle.spread * f + side * lateral, battle_center, q) &&
                        BattleReachable(battle_center, q)) {
                    dir = d;
                    return q;
                }
            }
        }

        // Nothing checked out: still apart from the other teams, never on the centre
        BattleLog("team " + t + ": no checked ground, placed unchecked");
        return battle_center + dir * min(battle.spread, 8.0f);
    }

    return battle_center;
}

void BattlePlan() {
    battle_teams.resize(0);
    battle_base_angle = RangedRandomFloat(0.0f, 6.2832f);
    int n = battle.num_teams;
    bool manual = battle.manual_place && battle_place_ok.length() >= uint(n);

    for(int t = 0; t < n; ++t) {
        BattleTeamState@ state = BattleTeamState();
        vec3 dir;

        if(manual && battle_place_ok[t]) {
            state.base = battle_place_pos[t];
            dir = AIOBFlatDir(state.base - battle_center, vec3(0.0f, 0.0f, 1.0f));
        } else {
            state.base = BattleAutoBase(t, dir);
        }

        state.facing = AIOBFlatDir(battle_center - state.base, dir * -1.0f);
        battle_teams.insertLast(state);
    }

    for(int t = 0; t < n; ++t) {
        BattleTeamState@ state = battle_teams[t];

        if(manual) {
            // Placed by hand: face the other teams
            vec3 others;

            for(int o = 0; o < n; ++o) {
                if(o != t) {
                    others += battle_teams[o].base / float(n - 1);
                }
            }

            state.facing = AIOBFlatDir(others - state.base, state.facing);
        }

        BattleMakeSlots(state, battle.teams[t].count);
        BattleLog("team " + t + " base=" + BattleVec(state.base) + " slots=" + state.slots.length() + (manual && battle_place_ok[t] ? " placed" : ""));
    }
}

vec3 AIOBFlatDir(const vec3 &in v, const vec3 &in fallback) {
    vec3 flat = vec3(v.x, 0.0f, v.z);
    return length_squared(flat) > 0.01f ? normalize(flat) : fallback;
}

void BattleMakeSlots(BattleTeamState@ state, int count) {
    state.slots.resize(0);
    vec3 side = vec3(-state.facing.z, 0.0f, state.facing.x);

    for(int k = 0; k < count; ++k) {
        vec3 want;

        if(battle.layout == 1 && !battle.manual_place) {
            // Scattered: anywhere within the spread of the centre
            float a = RangedRandomFloat(0.0f, 6.2832f);
            float r = sqrt(RangedRandomFloat(0.04f, 1.0f)) * battle.spread;
            want = battle_center + vec3(cos(a) * r, 0.0f, sin(a) * r);
        } else if(battle.formation == 1) {
            // Lines of 8 across the front, rows behind
            int row = k / 8;
            int in_row = min(8, count - row * 8);
            float x = (k % 8 - (in_row - 1) * 0.5f) * 1.8f;
            want = state.base + side * x - state.facing * (row * 2.0f);
        } else {
            // Blob: sunflower spiral around the base
            float r = 1.3f * sqrt(float(k));
            float a = k * 2.39996f;
            want = state.base + vec3(cos(a) * r, 0.0f, sin(a) * r);
        }

        vec3 q;

        if(!BattleGround(want, state.base, q)) {
            q = state.base + vec3(RangedRandomFloat(-1.5f, 1.5f), 0.0f, RangedRandomFloat(-1.5f, 1.5f));
        }

        state.slots.insertLast(q);
    }
}


// ---- manual placement ----------------------------------------------------------

// Config-safe name of the battle map: placements are remembered per map
string BattleMapKey() {
    string name = BattleLevelKey(battle.level_path);
    string key;

    for(uint i = 0; i < name.length(); ++i) {
        uint8 c = name[i];

        if((c >= 97 && c <= 122) || (c >= 48 && c <= 57)) {
            key += name.substr(i, 1);
        }
    }

    return key;
}

void BattleLoadPlacement() {
    battle_place_pos.resize(kMaxTeams);
    battle_place_ok.resize(kMaxTeams);
    string prefix = "aiob_place_" + BattleMapKey() + "_";
    int loaded = 0;

    for(int t = 0; t < kMaxTeams; ++t) {
        battle_place_ok[t] = false;

        if(!ConfigHasKey(prefix + t)) {
            continue;
        }

        array<string>@ parts = GetConfigValueString(prefix + t).split(" ");

        if(parts.length() == 3) {
            battle_place_pos[t] = vec3(atof(parts[0]), atof(parts[1]), atof(parts[2]));
            battle_place_ok[t] = true;
            ++loaded;
        }
    }

    if(loaded > 0) {
        BattlePlaceMessage("Your last placement on this map is loaded: press Start, or click to move teams.");
    }
}

void BattleSavePlacement() {
    string prefix = "aiob_place_" + BattleMapKey() + "_";

    for(int t = 0; t < battle.num_teams; ++t) {
        vec3 p = battle_place_pos[t];
        SetConfigValueString(prefix + t, battle_place_ok[t] ? "" + p.x + " " + p.y + " " + p.z : "");
    }

    SaveConfig();
}

void BattlePlaceMessage(const string &in text) {
    battle_place_msg = text;
    battle_place_msg_until = the_time + 5.0f;
}

void BattleBeginPlacement() {
    if(!battle_place_loaded) {
        battle_place_loaded = true;
        BattleLoadPlacement();
    }

    // The player waits out of the way while the teams are placed
    if(battle_player_id != -1 && MovementObjectExists(battle_player_id)) {
        MovementObject@ player = ReadCharacterID(battle_player_id);
        player.ReceiveScriptMessage("set_dialogue_control true");
        vec3 park = battle_center + vec3(0.0f, 150.0f, 0.0f);
        player.Execute("dialogue_position = vec3(" + park.x + ", " + park.y + ", " + park.z + ");");
    }

    // Camera high above the middle, looking down
    battle_cam_pos = BattleCameraSpot(battle_center + vec3(0.0f, 35.0f, 30.0f));
    battle_cam_yaw = 0.0f;
    battle_cam_pitch = -50.0f;
    battle_cam_last = the_time;
    battle_follow = -1;
    battle_place_team = 0;

    for(int t = 0; t < battle.num_teams; ++t) {
        if(!battle_place_ok[t]) {
            battle_place_team = t;
            break;
        }
    }

    BattleLog("placement");
    BattleSetPhase(kBattlePlace);
}

void BattleStartFromPlacement() {
    BattleSavePlacement();
    BattleFinishPrepare();
}

void BattleAutoPlaceRest() {
    battle_base_angle = RangedRandomFloat(0.0f, 6.2832f);

    for(int t = 0; t < battle.num_teams; ++t) {
        if(!battle_place_ok[t]) {
            vec3 dir;
            battle_place_pos[t] = BattleAutoBase(t, dir);
            battle_place_ok[t] = true;
        }
    }
}

// Ground under the mouse cursor, snapped to where fighters can stand
bool BattleCursorGround(vec3 &out q, bool &out walkable) {
    vec3 start = camera.GetPos();
    vec3 end = start + camera.GetMouseRay() * 1000.0f;
    vec3 hit = col.GetRayCollision(start, end);
    q = hit;
    walkable = false;

    if(hit == end) {
        return false;
    }

    if(battle_query_id == -1) {
        walkable = true;  // no character to ask about the navmesh: trust the ground
        return true;
    }

    vec3 snapped;
    walkable = BattleNavSnap(hit + vec3(0.0f, 0.3f, 0.0f), snapped) && distance(snapped, hit) < 2.5f;

    if(walkable) {
        q = snapped;
    }

    return true;
}

void BattlePlaceAtCursor() {
    vec3 q;
    bool walkable;

    if(!BattleCursorGround(q, walkable)) {
        BattlePlaceMessage("Point at the ground.");
        return;
    }

    if(!walkable) {
        BattlePlaceMessage("Fighters cannot stand there: pick open, walkable ground.");
        return;
    }

    int t = battle_place_team;
    battle_place_pos[t] = q;
    battle_place_ok[t] = true;
    BattleLog("placed team " + t + " at " + BattleVec(q));

    // On to the next team still to place
    for(int k = 1; k <= battle.num_teams; ++k) {
        int next = (t + k) % battle.num_teams;

        if(!battle_place_ok[next]) {
            battle_place_team = next;
            break;
        }
    }
}

float BattleMarkerRadius(int t) {
    return 1.3f * sqrt(float(battle.teams[t].count)) + 0.8f;
}

void BattleUpdatePlacement() {
    battle_place_looking = GetInputDown(0, "grab");

    // Team markers: a ring the size of the team and a post with its name
    for(int t = 0; t < battle.num_teams; ++t) {
        if(!battle_place_ok[t]) {
            continue;
        }

        vec3 base = battle_place_pos[t];
        vec3 color = battle.teams[t].color;
        DebugDrawWireCylinder(base, BattleMarkerRadius(t), 0.25f, color, _delete_on_update);
        DebugDrawLine(base, base + vec3(0.0f, 5.0f, 0.0f), color, _delete_on_update);
        DebugDrawText(base + vec3(0.0f, 5.6f, 0.0f), battle.teams[t].name, 1.0f, false, _delete_on_update);

        if(t == battle_place_team) {
            DebugDrawWireCylinder(base, BattleMarkerRadius(t) + 0.4f, 0.25f, vec3(1.0f), _delete_on_update);
        }
    }

    // Where a click would put the selected team
    if(!battle_place_looking) {
        vec3 q;
        bool walkable;

        if(BattleCursorGround(q, walkable)) {
            vec3 color = walkable ? battle.teams[battle_place_team].color : vec3(0.4f);
            DebugDrawWireCylinder(q, BattleMarkerRadius(battle_place_team), 0.15f, color, _delete_on_update);
        }
    }
}

void BattleDrawPlacement() {
    ImGui_SetNextWindowPos(vec2(20.0f, 20.0f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(380.0f, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Place the teams", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui_TextWrapped("Left-click the ground to place the selected team. Hold the right mouse button to look around; " +
        "WASD to move, Space / Crouch up and down, hold Walk to go faster.");
    ImGui_Separator();

    for(int t = 0; t < battle.num_teams; ++t) {
        ImGui_PushStyleColor(ImGuiCol_Text, BattleTextColor(battle.teams[t].color));
        string status = battle_place_ok[t] ? "placed" : "not placed yet";

        if(ImGui_Selectable(battle.teams[t].name + "  (" + battle.teams[t].count + ")   -   " + status + "##place" + t,
                t == battle_place_team)) {
            battle_place_team = t;
        }

        ImGui_PopStyleColor();
    }

    ImGui_Separator();

    if(ImGui_Button("Auto-place the rest", vec2(170.0f, 0.0f))) {
        BattleAutoPlaceRest();
    }

    ImGui_SameLine();

    if(ImGui_Button("Clear all", vec2(170.0f, 0.0f))) {
        for(int t = 0; t < kMaxTeams; ++t) {
            battle_place_ok[t] = false;
        }

        battle_place_team = 0;
    }

    int missing = 0;

    for(int t = 0; t < battle.num_teams; ++t) {
        if(!battle_place_ok[t]) {
            ++missing;
        }
    }

    ImGui_Spacing();

    if(ImGui_Button(missing == 0 ? "START BATTLE" : "Start (auto-place " + missing + " team" + (missing > 1 ? "s" : "") + ")",
            vec2(346.0f, 36.0f))) {
        BattleStartFromPlacement();
    }

    if(the_time < battle_place_msg_until) {
        ImGui_TextColored(vec4(1.0f, 0.8f, 0.4f, 1.0f), battle_place_msg);
    }

    ImGui_End();

    // A click on the map (not on this window) places the selected team
    if(!battle_place_looking && ImGui_IsMouseClicked(0) && !ImGui_IsMouseHoveringAnyWindow()) {
        BattlePlaceAtCursor();
    }
}

// ---- spawning ---------------------------------------------------------------

// Characters with cloth palette channels only (guard2, dog_soldier and wolf
// have no palette at all, so they could not wear team colours)
const array<array<string>> kBattleVariants = {
    {"guard", "male_rabbit_1", "male_rabbit_2", "male_rabbit_3", "female_rabbit_1", "female_rabbit_2", "female_rabbit_3", "raider_rabbit"},
    {"male_cat", "female_cat", "striped_cat", "fancy_striped_cat"},
    {"lt_dog_big", "lt_dog_female", "lt_dog_male_1", "lt_dog_male_2"},
    {"rat", "female_rat", "hooded_rat"},
    {"male_wolf"}
};

int BattlePickSpecies(int setting) {
    if(setting >= 0 && setting <= 4) {
        return setting;
    }

    float r = RangedRandomFloat(0.0f, 1.0f);
    return r < 0.35f ? 0 : (r < 0.55f ? 1 : (r < 0.75f ? 2 : (r < 0.9f ? 3 : 4)));
}

string BattleWeaponPath(int weapon, int species) {
    if(weapon == 8) {
        weapon = rand() % 8;  // mixed: unarmed included
    }

    switch(weapon) {
        case 1: return (species == 2) ? "Data/Items/DogWeapons/DogKnife.xml" : "Data/Items/rabbit_weapons/rabbit_knife.xml";
        case 2: return "Data/Items/DogWeapons/DogSword.xml";
        case 3: return "Data/Items/DogWeapons/DogSpear.xml";
        case 4: return "Data/Items/staffbasic.xml";
        case 5: return "Data/Items/DogWeapons/DogBroadSword.xml";
        case 6: return "Data/Items/DogWeapons/DogHammer.xml";
        case 7: return "Data/Items/Rapier.xml";
    }

    return "";
}

void BattleSpawnBatch() {
    BattleSetupNewFighters();

    // The first fighter of each kind loads its assets: give that a moment
    // (characters created during a cold asset load can go NaN)
    if(the_time - battle_phase_start < 0.3f) {
        return;
    }

    int per_frame = battle_fighters.length() < uint(battle.num_teams) ? 1 : 2;

    for(int i = 0; i < per_frame && battle_spawn_queue.length() > 0; ++i) {
        int team = battle_spawn_queue[0];
        battle_spawn_queue.removeAt(0);
        BattleTeamState@ state = battle_teams[team];
        vec3 pos = state.slots[state.next_slot % state.slots.length()];
        ++state.next_slot;
        BattleSpawnFighter(team, pos);

        if(battle_fighters.length() == uint(battle.num_teams)) {
            battle_phase_start = the_time;  // pause after the first of each team
            return;
        }
    }

    if(battle_spawn_queue.length() == 0 && the_time - battle_phase_start > 0.3f) {
        battle_last_go = 4;
        BattleSetPhase(kBattleCountdown);
    }
}

int BattleSpawnFighter(int team, const vec3 &in pos) {
    BattleTeam@ t = battle.teams[team];
    BattleTeamState@ state = battle_teams[team];
    int species = BattlePickSpecies(t.species);
    uint variant = uint(rand()) % kBattleVariants[species].length();
    string actor = "Data/Objects/overhaul/battle/" + kBattleVariants[species][variant] + ".xml";
    int id = CreateObject(actor, true);
    Object@ obj = ReadObjectFromID(id);
    obj.SetTranslation(pos + vec3(0.0f, 0.6f, 0.0f));
    ScriptParams@ params = obj.GetScriptParams();
    params.SetString("Teams", "aiob_team_" + team);

    // AI
    params.SetInt("AI - Overhaul", t.ai == 2 ? 0 : 1);
    params.SetInt("AI - Team Tactics", t.ai == 0 ? 1 : 0);

    if(!battle.throwing) {
        params.SetFloat("AI - Weapon Throwing", 0.0f);
    }

    // Skill, after the stock arena's difficulty table
    float s = t.skill;
    params.SetFloat("Block Follow-up", mix(RangedRandomFloat(0.05f, 0.25f), RangedRandomFloat(0.75f, 1.0f), s));
    params.SetFloat("Block Skill", mix(RangedRandomFloat(0.05f, 0.25f), RangedRandomFloat(0.55f, 0.8f), s));
    params.SetFloat("Attack Speed", mix(RangedRandomFloat(0.85f, 0.95f), RangedRandomFloat(1.0f, 1.1f), s));
    float damage = mix(RangedRandomFloat(0.5f, 0.6f), RangedRandomFloat(0.95f, 1.05f), s);
    params.SetFloat("Attack Damage", damage);
    params.SetFloat("Attack Knockback", damage);
    params.SetFloat("Movement Speed", mix(RangedRandomFloat(0.9f, 0.95f), RangedRandomFloat(1.0f, 1.05f), s));
    params.SetFloat("Aggression", mix(RangedRandomFloat(0.3f, 0.6f), RangedRandomFloat(0.6f, 0.9f), s));
    params.SetFloat("Ground Aggression", s);
    params.SetFloat("Damage Resistance", mix(0.75f, 1.0f, s) * t.toughness);
    params.SetFloat("Character Scale", t.size * RangedRandomFloat(0.95f, 1.05f));
    params.SetInt("Wearing Metal Armor", t.armor ? 1 : 0);
    params.SetInt("Left handed", rand() % 6 == 0 ? 1 : 0);

    if(battle.never_flee) {
        params.SetInt("Fear - Never Afraid On Sight", 1);
        params.SetInt("Fear - Always Afraid On Sight", 0);
        params.SetFloat("Fear - Afraid At Health Level", 0.0f);
    }

    obj.UpdateScriptParams();

    BattleFighter@ fighter = BattleFighter();
    fighter.id = id;
    fighter.team = team;
    fighter.spawn_time = the_time;

    // Weapons (wolves have no hands)
    if(species != 4) {
        if(battle.melee_weapons) {
            string weapon = BattleWeaponPath(t.weapon, species);

            if(weapon != "") {
                BattleAttach(obj, fighter, weapon, pos, _at_grip);
            }
        }

        if(battle.throwing && t.spare_knives) {
            BattleAttach(obj, fighter, "Data/Items/rabbit_weapons/rabbit_throwing_knife.xml", pos, _at_sheathe);
            BattleAttach(obj, fighter, "Data/Items/rabbit_weapons/rabbit_throwing_knife.xml", pos, _at_sheathe);
        }
    }

    battle_fighters.insertLast(fighter);
    ++state.spawned;
    return id;
}

void BattleAttach(Object@ obj, BattleFighter@ fighter, const string &in path, const vec3 &in pos, AttachmentType how) {
    int item_id = CreateObject(path, true);
    Object@ item = ReadObjectFromID(item_id);
    item.SetTranslation(pos);
    obj.AttachItem(item, how, false);
    fighter.items.insertLast(item_id);
    battle_items.insertLast(item_id);
}

// A few frames after spawning the character script runs: team colours, face
// the enemy, hold fire until the countdown ends
void BattleSetupNewFighters() {
    for(uint i = 0; i < battle_fighters.length(); ++i) {
        BattleFighter@ f = battle_fighters[i];

        if(f.set_up || the_time - f.spawn_time < 0.15f || !MovementObjectExists(f.id)) {
            continue;
        }

        f.set_up = true;
        MovementObject@ char = ReadCharacterID(f.id);
        vec3 c = battle.teams[f.team].color * RangedRandomFloat(0.85f, 1.1f);
        vec3 face = battle_teams[f.team].facing;
        // Wolves wear no cloth: their coat is tinted toward the team colour instead
        vec3 coat = mix(vec3(0.42f, 0.4f, 0.37f), battle.teams[f.team].color, 0.5f);
        char.Execute("bool wolf = species == _wolf; for(int i = 0; i < 4; ++i) { string ch = character_getter.GetChannel(i); " +
            "if(ch == \"cloth\") { ReadObjectFromID(this_mo.GetID()).SetPaletteColor(i, vec3(" + c.x + ", " + c.y + ", " + c.z + ")); } " +
            "else if(wolf && ch == \"fur\") { ReadObjectFromID(this_mo.GetID()).SetPaletteColor(i, vec3(" + coat.x + ", " + coat.y + ", " + coat.z + ")); } } " +
            "this_mo.SetRotationFromFacing(vec3(" + face.x + ", 0.0f, " + face.z + "));");

        if(battle_phase < kBattleFight) {
            char.ReceiveScriptMessage("set_combat_allowed false");
        } else {
            BattleNoticeNearest(f, 3);  // reinforcement: straight into the fight
        }
    }
}

// ---- the fight ----------------------------------------------------------------

void BattleGo() {
    BattleSetPhase(kBattleFight);
    battle_fight_start = the_time;
    battle_next_hunt = the_time + (battle.aware ? 3.0f : 20.0f);

    for(uint i = 0; i < battle_fighters.length(); ++i) {
        if(MovementObjectExists(battle_fighters[i].id)) {
            ReadCharacterID(battle_fighters[i].id).ReceiveScriptMessage("set_combat_allowed true");
        }
    }

    if(battle.aware) {
        for(uint i = 0; i < battle_fighters.length(); ++i) {
            BattleNoticeNearest(battle_fighters[i], 4);
        }
    }

    BattleLog("go fighters=" + battle_fighters.length());
}

// Tell a fighter about its `count` nearest enemies (it will pick its target)
void BattleNoticeNearest(BattleFighter@ f, int count) {
    if(!MovementObjectExists(f.id)) {
        return;
    }

    MovementObject@ me = ReadCharacterID(f.id);
    array<int> picked;

    for(int n = 0; n < count; ++n) {
        int best = -1;
        float best_d = 1.0e9f;

        for(uint j = 0; j < battle_fighters.length(); ++j) {
            BattleFighter@ o = battle_fighters[j];

            if(o.team == f.team || o.ko_time >= 0.0f || picked.find(o.id) >= 0 || !MovementObjectExists(o.id)) {
                continue;
            }

            float d = distance_squared(me.position, ReadCharacterID(o.id).position);

            if(d < best_d) {
                best_d = d;
                best = o.id;
            }
        }

        if(best == -1) {
            break;
        }

        picked.insertLast(best);
        me.ReceiveScriptMessage("notice " + best);
    }

    // A player fighting on a team is an enemy too
    if(battle.player_mode == 1 && !battle_player_out && battle_player_id != -1 && battle.player_team != f.team &&
            MovementObjectExists(battle_player_id) && picked.length() < uint(count)) {
        me.ReceiveScriptMessage("notice " + battle_player_id);
    }
}

void BattleTick() {
    for(int t = 0; t < battle.num_teams; ++t) {
        battle_teams[t].alive = 0;
    }

    bool any_ko = false;

    for(int i = int(battle_fighters.length()) - 1; i >= 0; --i) {
        BattleFighter@ f = battle_fighters[i];

        if(!MovementObjectExists(f.id)) {
            battle_fighters.removeAt(i);
            continue;
        }

        MovementObject@ char = ReadCharacterID(f.id);
        bool awake = char.GetIntVar("knocked_out") == _awake;

        if(awake) {
            ++battle_teams[f.team].alive;
            continue;
        }

        if(f.ko_time < 0.0f) {
            f.ko_time = the_time;
            ++battle_teams[f.team].fallen;
            any_ko = true;
            BattleTeam@ t = battle.teams[f.team];
            BattleTeamState@ state = battle_teams[f.team];

            // A replacement, if the team has any left
            if(battle_phase == kBattleFight &&
                    state.spawned + int(state.reinforce_due.length()) < t.count * battle.waves) {
                state.reinforce_due.insertLast(the_time + battle.wave_delay);
            }
        }

        // Clear old bodies (and the weapons nobody picked up)
        if(battle.cleanup > 0 && the_time - f.ko_time > battle.cleanup) {
            QueueDeleteObjectID(f.id);

            for(uint k = 0; k < f.items.length(); ++k) {
                if(ObjectExists(f.items[k]) && !ReadItemID(f.items[k]).IsHeld() && ReadItemID(f.items[k]).StuckInWhom() == -1) {
                    QueueDeleteObjectID(f.items[k]);
                }
            }

            battle_fighters.removeAt(i);
        }
    }

    if(battle.player_mode == 1 && !battle_player_out && battle_player_id != -1 && MovementObjectExists(battle_player_id) &&
            ReadCharacterID(battle_player_id).GetIntVar("knocked_out") == _awake) {
        ++battle_teams[battle.player_team].alive;
    }

    for(uint i = 0; i < battle_remote_ids.length(); ++i) {
        if(MovementObjectExists(battle_remote_ids[i]) && ReadCharacterID(battle_remote_ids[i]).GetIntVar("knocked_out") == _awake) {
            ++battle_teams[battle_remote_teams[i]].alive;
        }
    }

    if(battle_phase != kBattleFight) {
        return;
    }

    // Reinforcements arrive at the team's base
    for(int t = 0; t < battle.num_teams; ++t) {
        BattleTeamState@ state = battle_teams[t];

        for(int k = int(state.reinforce_due.length()) - 1; k >= 0; --k) {
            if(the_time >= state.reinforce_due[k]) {
                state.reinforce_due.removeAt(k);
                BattleSpawnFighter(t, state.slots[state.next_slot % state.slots.length()]);
                ++state.next_slot;
            }
        }
    }

    // Stalemate breaking: idle fighters are pointed at the nearest enemy
    if(the_time >= battle_next_hunt) {
        battle_next_hunt = the_time + 3.0f;

        for(uint i = 0; i < battle_fighters.length(); ++i) {
            BattleFighter@ f = battle_fighters[i];

            if(f.ko_time < 0.0f && f.set_up && MovementObjectExists(f.id) &&
                    ReadCharacterID(f.id).QueryIntFunction("int IsAggro()") != 1) {
                BattleNoticeNearest(f, 1);
            }
        }
    }

    // Who is still in it
    int standing = 0;
    int last = -1;

    for(int t = 0; t < battle.num_teams; ++t) {
        if(battle_teams[t].alive > 0 || battle_teams[t].reinforce_due.length() > 0) {
            ++standing;
            last = t;
        }
    }

    bool timed_out = battle.time_limit > 0 && the_time - battle_fight_start > battle.time_limit * 60.0f;

    if(standing <= 1 || timed_out) {
        if(timed_out && standing > 1) {
            int most = -1;
            last = -2;

            for(int t = 0; t < battle.num_teams; ++t) {
                if(battle_teams[t].alive > most) {
                    most = battle_teams[t].alive;
                    last = t;
                } else if(battle_teams[t].alive == most) {
                    last = -2;
                }
            }
        }

        battle_winner = standing == 0 ? -2 : last;
        battle_fight_end = the_time;
        battle_panel_open = true;
        BattleSetPhase(kBattleOver);
        BattleLog("over winner=" + battle_winner + " time=" + (battle_fight_end - battle_fight_start));

        if(battle.slowmo_finish && any_ko && !timed_out) {
            TimedSlowMotion(0.25f, 1.6f, 0.0f);
        }
    }
}

// ---- the player ---------------------------------------------------------------

string BattleAllTeams() {
    string all;

    for(int t = 0; t < battle.num_teams; ++t) {
        all += (t > 0 ? "," : "") + "aiob_team_" + t;
    }

    return all;
}

void BattleSetUpPlayer() {
    battle_spectating = battle.player_mode == 0 || battle_player_id == -1;

    if(battle_player_id == -1) {
        BattleStartCamera();
        return;
    }

    MovementObject@ player = ReadCharacterID(battle_player_id);
    Object@ obj = ReadObjectFromID(battle_player_id);
    ScriptParams@ params = obj.GetScriptParams();

    if(battle_spectating) {
        // On every team: nobody fights it; frozen in dialogue control, the
        // camera is ours
        params.SetString("Teams", BattleAllTeams());
        obj.UpdateScriptParams();
        player.ReceiveScriptMessage("set_dialogue_control true");
        // Dialogue control pins the body to dialogue_position: park it high
        // above the battle, out of sight and out of everyone's way
        vec3 park = battle_center + vec3(0.0f, 150.0f, 0.0f);
        player.Execute("dialogue_position = vec3(" + park.x + ", " + park.y + ", " + park.z + ");");
        BattleStartCamera();
        return;
    }

    // Dialogue control on revives and stands the body up, off gives control back
    player.ReceiveScriptMessage("set_dialogue_control true");
    player.ReceiveScriptMessage("set_dialogue_control false");
    battle_player_out = false;
    int team = max(0, min(battle.num_teams - 1, battle.player_team));
    battle.player_team = team;
    params.SetString("Teams", "aiob_team_" + team);
    obj.UpdateScriptParams();
    vec3 front = battle_teams[team].base + battle_teams[team].facing * 2.5f;
    vec3 face = battle_teams[team].facing;
    vec3 c = battle.teams[team].color;
    player.Execute("this_mo.position = vec3(" + front.x + ", " + (front.y + 0.6f) + ", " + front.z + "); this_mo.velocity = vec3(0.0f); " +
        "this_mo.SetRotationFromFacing(vec3(" + face.x + ", 0.0f, " + face.z + ")); " +
        "for(int i = 0; i < 4; ++i) { if(character_getter.GetChannel(i) == \"cloth\") { " +
        "ReadObjectFromID(this_mo.GetID()).SetPaletteColor(i, vec3(" + c.x + ", " + c.y + ", " + c.z + ")); } }");
}

// Fighting on a team and knocked out: watch the rest with the spectator camera
void BattleUpdatePlayer() {
    if(battle.player_mode != 1 || battle_spectating || battle_player_out || battle_player_id == -1 ||
            !MovementObjectExists(battle_player_id)) {
        return;
    }

    MovementObject@ player = ReadCharacterID(battle_player_id);

    if(player.GetIntVar("knocked_out") == _awake) {
        battle_player_ko_time = -1.0f;
        return;
    }

    if(battle_player_ko_time < 0.0f) {
        battle_player_ko_time = the_time;
    } else if(the_time - battle_player_ko_time > 2.5f && battle_phase == kBattleFight) {
        battle_player_out = true;
        battle_spectating = true;
        battle_cam_pos = camera.GetPos();
        battle_cam_yaw = camera.GetYRotation();
        battle_cam_pitch = camera.GetXRotation();
        battle_cam_last = the_time;
        battle_follow = -1;
        player.ReceiveScriptMessage("set_dialogue_control true");
        vec3 park = battle_center + vec3(0.0f, 150.0f, 0.0f);
        player.Execute("dialogue_position = vec3(" + park.x + ", " + park.y + ", " + park.z + ");");
        BattleLog("player out: spectating");
    }
}

// ---- spectator camera -----------------------------------------------------------

// A camera spot on the way from the map centre to `want`, in front of any wall
// or ceiling in between (caves, enclosed arenas)
vec3 BattleCameraSpot(const vec3 &in want) {
    vec3 from = battle_center + vec3(0.0f, 1.5f, 0.0f);
    vec3 hit = col.GetRayCollision(from, want);

    if(hit == want) {
        return want;
    }

    vec3 dir = want - from;
    float len = length(dir);
    return len > 0.01f ? hit - dir / len * 1.5f : want;
}

void BattleStartCamera() {
    vec3 back = battle_teams.length() > 0 ? AIOBFlatDir(battle_teams[0].base - battle_center, vec3(0.0f, 0.0f, 1.0f)) : vec3(0.0f, 0.0f, 1.0f);
    vec3 side = vec3(-back.z, 0.0f, back.x);
    battle_cam_pos = BattleCameraSpot(battle_center + side * (battle.spread + 12.0f) + vec3(0.0f, 14.0f, 0.0f));
    vec3 look = battle_center - battle_cam_pos;
    look = length_squared(look) > 0.01f ? normalize(look) : vec3(0.0f, -0.5f, -0.86f);
    battle_cam_yaw = atan2(-look.x, -look.z) * 57.2958f;
    battle_cam_pitch = asin(look.y) * 57.2958f;
    battle_cam_last = the_time;
    battle_follow = -1;
}

int BattleNextFollow(int after) {
    int count = int(battle_fighters.length());

    if(count == 0) {
        return -1;
    }

    int start = 0;

    for(int i = 0; i < count; ++i) {
        if(battle_fighters[i].id == after) {
            start = i + 1;
        }
    }

    for(int k = 0; k < count; ++k) {
        BattleFighter@ f = battle_fighters[(start + k) % count];

        if(f.ko_time < 0.0f && MovementObjectExists(f.id)) {
            return f.id;
        }
    }

    return -1;
}

void BattleUpdateCamera(bool is_paused) {
    bool placing = battle_phase == kBattlePlace;

    if(!battle_spectating && !placing) {
        return;
    }

    float dt = max(0.0f, min(0.1f, the_time - battle_cam_last));
    battle_cam_last = the_time;
    bool ui = AIOBattleHasFocus() || menu_open;
    bool look = placing ? battle_place_looking && !menu_open : !ui;

    if(!is_paused && look) {
        SetGrabMouse(true);
        battle_cam_yaw -= GetLookXAxis(0);
        battle_cam_pitch = max(-89.0f, min(89.0f, battle_cam_pitch - GetLookYAxis(0)));
    }

    if(!is_paused && !ui && !placing) {
        if(GetInputPressed(0, "attack")) {
            battle_follow = BattleNextFollow(battle_follow);
        }

        if(GetInputPressed(0, "grab")) {
            battle_follow = -1;
        }
    }

    float yaw = battle_cam_yaw * 0.0174533f;
    float pitch = battle_cam_pitch * 0.0174533f;
    vec3 facing = vec3(-sin(yaw) * cos(pitch), sin(pitch), -cos(yaw) * cos(pitch));
    vec3 right = normalize(cross(facing, vec3(0.0f, 1.0f, 0.0f)));

    if(battle_follow != -1 && (!MovementObjectExists(battle_follow) ||
            ReadCharacterID(battle_follow).GetIntVar("knocked_out") != _awake)) {
        battle_follow = BattleNextFollow(battle_follow);  // it fell: watch the next one
    }

    if(battle_follow != -1) {
        vec3 target = ReadCharacterID(battle_follow).position + vec3(0.0f, 1.0f, 0.0f);
        battle_cam_pos = target - facing * 5.5f;
    } else if(!is_paused && (placing ? !menu_open : !ui)) {
        vec3 move;

        if(GetInputDown(0, "up")) move += facing;
        if(GetInputDown(0, "down")) move -= facing;
        if(GetInputDown(0, "right")) move += right;
        if(GetInputDown(0, "left")) move -= right;
        if(GetInputDown(0, "jump")) move.y += 1.0f;
        if(GetInputDown(0, "crouch")) move.y -= 1.0f;

        float speed = GetInputDown(0, "walk") ? 30.0f : 12.0f;
        battle_cam_pos += move * speed * dt;
    }

    camera.SetFlags(kEditorCamera);
    camera.SetXRotation(battle_cam_pitch);
    camera.SetYRotation(battle_cam_yaw);
    camera.SetZRotation(0.0f);
    camera.SetPos(battle_cam_pos);
    camera.SetDistance(0.0f);
    camera.SetFOV(75.0f);
    UpdateListener(battle_cam_pos, vec3(0.0f), camera.GetFacing(), camera.GetUpVector());
}

// ---- HUD ------------------------------------------------------------------------

vec4 BattleTextColor(const vec3 &in c) {
    return vec4(min(1.0f, c.x * 1.15f + 0.1f), min(1.0f, c.y * 1.15f + 0.1f), min(1.0f, c.z * 1.15f + 0.1f), 1.0f);
}

string BattleClock(float seconds) {
    int s = int(seconds);
    string sec = "" + (s % 60);
    return (s / 60) + ":" + (sec.length() < 2 ? "0" + sec : sec);
}

void BattleBigText(const string &in id, const string &in text, const vec4 &in color, float scale, float y) {
    ImGui_SetNextWindowPos(vec2(0.0f, y), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(float(GetScreenWidth()), 40.0f + 30.0f * scale), ImGuiSetCond_Always);
    ImGui_PushStyleColor(ImGuiCol_WindowBg, vec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui_Begin(id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(scale);
    float w = ImGui_CalcTextSize(text).x;
    ImGui_SetCursorPosX(max(0.0f, (float(GetScreenWidth()) - w) * 0.5f));
    ImGui_TextColored(color, text);
    ImGui_End();
    ImGui_PopStyleColor();
}

void AIOBattleDrawGUI() {
    float screen_h = float(GetScreenHeight());

    if(BattleLobbyShown()) {
        if(battle_lobby_open) {
            BattleDrawLobby();
        } else {
            BattleBigText("aiob_lobby_hint", "Battle lobby: press " + GetStringDescriptionForBinding("key", "skip_dialogue") +
                " to pick a team", vec4(1.0f, 0.9f, 0.6f, 0.9f), 1.0f, screen_h - 50.0f);
        }

        return;
    }

    if(!BattleActive()) {
        return;
    }

    if(battle_phase == kBattlePlace) {
        BattleDrawPlacement();
        return;
    }

    if(battle_mirror && (battle_phase == kBattleWait || battle_phase == kBattleSpawn)) {
        BattleBigText("aiob_prep", "The host is preparing the battle...", vec4(1.0f, 0.9f, 0.6f, 1.0f), 2.0f, screen_h * 0.4f);
        return;
    }

    if(battle_phase == kBattleWait || battle_phase == kBattleSpawn) {
        int total = battle_fighters.length() + battle_spawn_queue.length();
        BattleBigText("aiob_prep", "Preparing the battle...  " + battle_fighters.length() + " / " + total,
            vec4(1.0f, 0.9f, 0.6f, 1.0f), 2.0f, screen_h * 0.4f);
        return;
    }

    if(battle_phase == kBattleCountdown) {
        int left = max(1, battle_last_go);
        BattleBigText("aiob_count", "" + left, vec4(1.0f, 0.85f, 0.4f, 1.0f), 5.0f, screen_h * 0.3f);
    } else if(battle_phase == kBattleFight && the_time - battle_fight_start < 1.2f) {
        BattleBigText("aiob_count", "FIGHT!", vec4(1.0f, 0.4f, 0.3f, 1.0f), 5.0f, screen_h * 0.3f);
    }

    if(!menu_open) {
        BattleDrawScoreboard();  // it would sit on the Overhaul window's title bar
    }

    if(battle_spectating && battle_phase != kBattleOver) {
        string help = battle_follow != -1 ? "Following a fighter  |  LMB next fighter  |  RMB free camera  |  mouse to look around"
            : "WASD fly  |  Space / Crouch up and down  |  hold Walk for speed  |  LMB follow a fighter";
        BattleBigText("aiob_help", help, vec4(0.85f, 0.85f, 0.85f, 0.8f), 1.0f, screen_h - 50.0f);
    }

    if(battle_phase == kBattleOver) {
        BattleDrawResult();
    }
}

void BattleDrawScoreboard() {
    float screen_w = float(GetScreenWidth());
    float width = min(screen_w - 20.0f, 150.0f * battle.num_teams + 140.0f);
    ImGui_SetNextWindowPos(vec2((screen_w - width) * 0.5f, 8.0f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(width, 62.0f), ImGuiSetCond_Always);
    ImGui_PushStyleColor(ImGuiCol_WindowBg, vec4(0.0f, 0.0f, 0.0f, 0.45f));
    ImGui_Begin("aiob_score", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(1.25f);
    float clock = battle_phase == kBattleOver ? battle_fight_end - battle_fight_start :
        (battle_phase == kBattleFight ? the_time - battle_fight_start : 0.0f);
    ImGui_Columns(battle.num_teams + 1, "aiob_cols", false);
    ImGui_TextColored(vec4(1.0f, 1.0f, 1.0f, 1.0f), BattleClock(clock));

    if(battle.time_limit > 0) {
        ImGui_TextColored(vec4(0.7f, 0.7f, 0.7f, 1.0f), "of " + battle.time_limit + ":00");
    }

    for(int t = 0; t < battle.num_teams; ++t) {
        ImGui_NextColumn();
        BattleTeam@ team = battle.teams[t];
        BattleTeamState@ state = battle_teams[t];
        vec4 color = BattleTextColor(team.color);
        ImGui_TextColored(color, team.name + (battle.player_mode == 1 && battle.player_team == t ? " (you)" : ""));
        int reserve = battle_mirror ? (t < int(battle_net_reserve.length()) ? battle_net_reserve[t] + int(state.reinforce_due.length()) : 0) :
            team.count * battle.waves - state.spawned + int(state.reinforce_due.length());
        string line = state.alive + " standing";

        if(battle.waves > 1) {
            line += "  +" + max(0, reserve - int(state.reinforce_due.length())) + "";
        }

        ImGui_TextColored(state.alive > 0 ? vec4(0.9f, 0.9f, 0.9f, 1.0f) : vec4(0.5f, 0.5f, 0.5f, 1.0f), line);
    }

    ImGui_Columns(1);
    ImGui_End();
    ImGui_PopStyleColor();
}

void BattleDrawResult() {
    string title = battle_winner == -2 ? "Draw" : battle.teams[battle_winner].name + " wins!";
    vec4 color = battle_winner == -2 ? vec4(0.9f, 0.9f, 0.9f, 1.0f) :
        BattleTextColor(battle.teams[battle_winner].color);

    if(!battle_panel_open) {
        BattleBigText("aiob_result", title, color, 3.0f, 80.0f);
        BattleBigText("aiob_result_hint", "Press " + GetStringDescriptionForBinding("key", "skip_dialogue") + " for the results",
            vec4(0.8f, 0.8f, 0.8f, 0.9f), 1.0f, 170.0f);
        return;
    }

    float screen_w = float(GetScreenWidth());
    float screen_h = float(GetScreenHeight());
    ImGui_SetNextWindowPos(vec2((screen_w - 460.0f) * 0.5f, screen_h * 0.22f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(460.0f, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Battle over", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(2.0f);
    ImGui_TextColored(color, title);
    ImGui_SetWindowFontScale(1.0f);
    ImGui_Text("Battle time " + BattleClock(battle_fight_end - battle_fight_start));
    ImGui_Separator();

    for(int t = 0; t < battle.num_teams; ++t) {
        BattleTeam@ team = battle.teams[t];
        ImGui_TextColored(BattleTextColor(team.color), team.name);
        ImGui_SameLine(150.0f);
        ImGui_Text(battle_teams[t].alive + " standing,  " + battle_teams[t].fallen + " fell  (" + kAINames[team.ai] + ")");
    }

    ImGui_Separator();

    if(!battle_mirror) {
        if(ImGui_Button("Rematch", vec2(140.0f, 34.0f))) {
            BattleRestart();
        }

        ImGui_SameLine();
    }

    if(ImGui_Button("Keep watching", vec2(140.0f, 34.0f))) {
        battle_panel_open = false;
    }

    ImGui_SameLine();

    if(ImGui_Button("Main menu", vec2(140.0f, 34.0f))) {
        LoadLevel("back_to_menu");
    }

    ImGui_End();
}

// ---- online (Phase 5): the host runs it, clients mirror it ------------------------

// Host: the other players' characters get teams and spots; their own machines move them
void BattleSetUpRemotePlayers() {
    battle_remote_ids.resize(0);
    battle_remote_teams.resize(0);

    if(!MPHost()) {
        return;
    }

    // Players per team so far (the host's own), and spots taken
    array<int> players(battle.num_teams, 0);
    array<int> spots(battle.num_teams, 0);

    if(battle.player_mode == 1) {
        ++players[battle.player_team];
    }

    // Their picks first, then the rest onto the emptiest teams
    array<int> ids;
    array<int> wish;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);
        int id = char.GetID();
        bool player = mp_player_ids.length() > 0 ? MPIsPlayer(id) : char.controlled;

        if(!player || id == battle_player_id || BattleIsOurs(id)) {
            continue;
        }

        int pick = battle_pick_ids.find(id);
        int team = pick >= 0 ? battle_pick_teams[pick] : -1;
        ids.insertLast(id);
        wish.insertLast(team >= 0 && team < battle.num_teams ? team : -1);
    }

    for(int pass = 0; pass < 2; ++pass) {
        for(uint i = 0; i < ids.length(); ++i) {
            if((pass == 0) != (wish[i] >= 0)) {
                continue;
            }

            int team = wish[i];

            if(team < 0) {
                team = 0;

                for(int t = 1; t < battle.num_teams; ++t) {
                    if(players[t] < players[team]) {
                        team = t;
                    }
                }
            }

            ++players[team];
            BattlePlaceRemote(ids[i], team, spots[team]++);
        }
    }
}

// Host: one other player onto a team, at its base
void BattlePlaceRemote(int id, int team, int k) {
    battle_remote_ids.insertLast(id);
    battle_remote_teams.insertLast(team);

    Object@ obj = ReadObjectFromID(id);
    obj.GetScriptParams().SetString("Teams", "aiob_team_" + team);  // reaches every machine
    BattleTeamState@ state = battle_teams[team];
    vec3 right(-state.facing.z, 0.0f, state.facing.x);
    vec3 spot = state.base + state.facing * 2.5f + right * (1.4f * (k + 1) * ((k % 2) == 0 ? 1.0f : -1.0f));
    vec3 c = battle.teams[team].color;
    MPToClients("aiob player " + MPRef(id) + " " + team + " " + spot.x + " " + (spot.y + 0.6f) + " " + spot.z + " " +
        state.facing.x + " " + state.facing.z + " " + c.x + " " + c.y + " " + c.z);
    BattleLog("online player " + id + " team " + team);
}

// ---- the lobby ------------------------------------------------------------------

// Up while the host waits to start a battle (host), or while its lobby
// messages come in and its battle hasn't begun here (client)
bool BattleLobbyShown() {
    if(MPHost()) {
        return BattleActive() && battle_phase == kBattleWait && !battle_lobby_start;
    }

    return MPClient() && !battle_mirror && the_time - battle_lobby_seen < 3.0f;
}

// Host, every second: "aiob lobby <teams> {name r g b}... {#player team}..."
void BattleLobbySend() {
    if(the_time < battle_lobby_next) {
        return;
    }

    battle_lobby_next = the_time + 1.0f;
    battle_lobby_ids.resize(0);
    battle_lobby_teams.resize(0);

    for(uint i = 0; i < mp_player_ids.length(); ++i) {
        int id = mp_player_ids[i];
        int pick = battle_pick_ids.find(id);
        battle_lobby_ids.insertLast(id);
        battle_lobby_teams.insertLast(id == mp_me ? (battle.player_mode == 1 ? battle.player_team : -1) :
            (pick >= 0 ? battle_pick_teams[pick] : -1));
    }

    string msg = "aiob lobby " + battle.num_teams;

    for(int t = 0; t < battle.num_teams; ++t) {
        vec3 c = battle.teams[t].color;
        msg += " " + MPName(battle.teams[t].name, true) + " " + c.x + " " + c.y + " " + c.z;
    }

    for(uint i = 0; i < battle_lobby_ids.length(); ++i) {
        msg += " " + MPRef(battle_lobby_ids[i]) + " " + battle_lobby_teams[i];
    }

    MPToClients(msg);
}

// Host: "ohr <player> aiob pick #<character> <team>" (-1 any team)
void BattleLobbyPick(const array<string> &in t) {
    if(t.length() < 6 || battle_phase != kBattleWait) {
        return;
    }

    int id = MPId(t[4]);
    int team = atoi(t[5]);

    if(id == -1 || !MPIsPlayer(id) || id == mp_me || team < -1 || team >= battle.num_teams) {
        return;
    }

    int at = battle_pick_ids.find(id);

    if(at < 0) {
        battle_pick_ids.insertLast(id);
        battle_pick_teams.insertLast(team);
    } else {
        battle_pick_teams[at] = team;
    }

    battle_lobby_next = 0.0f;  // everyone sees it now
    BattleLog("lobby: player " + id + " picked team " + team);
}

// Client: the host's lobby
void BattleLobbyReceive(const array<string> &in t) {
    int n = atoi(t[3]);

    if(n < 2 || n > 8 || int(t.length()) < 4 + n * 4) {
        return;
    }

    battle_lobby_seen = the_time;
    battle_lobby_names.resize(n);
    battle_lobby_colors.resize(n);

    for(int i = 0; i < n; ++i) {
        battle_lobby_names[i] = MPName(t[4 + i * 4], false);
        battle_lobby_colors[i] = vec3(atof(t[5 + i * 4]), atof(t[6 + i * 4]), atof(t[7 + i * 4]));
    }

    battle_lobby_ids.resize(0);
    battle_lobby_teams.resize(0);

    for(uint k = 4 + n * 4; k + 1 < t.length(); k += 2) {
        int id = MPId(t[k]);

        if(id != -1) {
            battle_lobby_ids.insertLast(id);
            battle_lobby_teams.insertLast(atoi(t[k + 1]));
        }
    }
}

string BattleLobbyName(int id) {
    if(id == mp_me) {
        return "You";
    }

    string name = MPPlayerName(id);
    return name != "" ? name : "Player";
}

void BattleDrawLobby() {
    int n = MPHost() ? battle.num_teams : int(battle_lobby_names.length());
    float w = 420.0f;
    ImGui_SetNextWindowPos(vec2((GetScreenWidth() - w) * 0.5f, GetScreenHeight() * 0.22f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(w, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Battle lobby", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui_TextWrapped(MPHost() ? "Players pick their teams. Start when everyone is ready." :
        "The host is setting up a battle. Pick your team:");
    ImGui_Separator();

    for(int t = 0; t < n; ++t) {
        string name = MPHost() ? battle.teams[t].name : battle_lobby_names[t];
        vec3 c = MPHost() ? battle.teams[t].color : battle_lobby_colors[t];
        string who = "";

        for(uint i = 0; i < battle_lobby_ids.length(); ++i) {
            if(battle_lobby_teams[i] == t) {
                who += (who == "" ? "" : ", ") + BattleLobbyName(battle_lobby_ids[i]);
            }
        }

        ImGui_TextColored(BattleTextColor(c), name);
        ImGui_SameLine();
        ImGui_Text(who == "" ? "-" : who);
        ImGui_SameLine();
        ImGui_SetCursorPosX(w - 90.0f);

        if(ImGui_Button("Join##aiob_join" + t)) {
            BattleLobbyChoose(t);
        }
    }

    string any = "";

    for(uint i = 0; i < battle_lobby_ids.length(); ++i) {
        if(battle_lobby_teams[i] < 0) {
            any += (any == "" ? "" : ", ") + BattleLobbyName(battle_lobby_ids[i]);
        }
    }

    ImGui_TextColored(vec4(0.7f, 0.7f, 0.7f, 1.0f), "Any team");
    ImGui_SameLine();
    ImGui_Text(any == "" ? "-" : any);

    if(MPClient()) {
        ImGui_SameLine();
        ImGui_SetCursorPosX(w - 90.0f);

        if(ImGui_Button("Any##aiob_any")) {
            BattleLobbyChoose(-1);
        }
    }

    ImGui_Separator();

    if(MPHost() && ImGui_Button("Start the battle")) {
        battle_lobby_start = true;
        BattleLog("lobby: the host starts it with " + mp_players + " players");
    }

    if(MPHost()) {
        ImGui_SameLine();
    }

    if(ImGui_Button("Hide")) {
        battle_lobby_open = false;
    }

    ImGui_End();
}

void BattleLobbyChoose(int team) {
    if(MPHost()) {
        if(team >= 0) {
            battle.player_mode = 1;
            battle.player_team = team;
        }

        battle_lobby_next = 0.0f;
    } else {
        MPToHost("aiob pick " + MPRef(mp_me) + " " + team);
    }
}

void BattleSendConfig() {
    if(!MPHost()) {
        return;
    }

    string msg = "aiob cfg " + battle.num_teams + " " + battle.waves + " " + battle.time_limit;

    for(int t = 0; t < battle.num_teams; ++t) {
        BattleTeam@ team = battle.teams[t];
        msg += " " + team.count + " " + team.ai + " " + team.color.x + " " + team.color.y + " " + team.color.z + " " +
            MPName(team.name, true);
    }

    MPToClients(msg);
}

void BattleSendState() {
    float clock = battle_phase == kBattleOver ? battle_fight_end - battle_fight_start :
        (battle_phase == kBattleFight ? the_time - battle_fight_start : 0.0f);
    string msg = "aiob state " + int(battle_phase) + " " + battle_winner + " " + int(clock * 100.0f) + " " + battle_last_go;

    for(int t = 0; t < battle.num_teams; ++t) {
        BattleTeamState@ state = battle_teams[t];
        int reserve = battle.teams[t].count * battle.waves - state.spawned;
        msg += " " + state.alive + " " + state.fallen + " " + reserve;
    }

    MPToClients(msg);
}

// Client: the host's battle
void BattleMirrorReceive(const array<string> &in t) {
    if(t.length() < 3 || !MPClient()) {
        return;
    }

    if(t[2] == "lobby" && t.length() >= 4) {
        if(!battle_mirror) {
            BattleLobbyReceive(t);
        }

        return;
    }

    if(t[2] == "cfg" && t.length() >= 6) {
        int n = atoi(t[3]);

        if(n < 2 || int(t.length()) < 6 + n * 6) {
            return;
        }

        battle.num_teams = n;
        battle.waves = atoi(t[4]);
        battle.time_limit = atoi(t[5]);
        battle.teams.resize(n);

        for(int i = 0; i < n; ++i) {
            int b = 6 + i * 6;
            battle.teams[i].count = atoi(t[b]);
            battle.teams[i].ai = atoi(t[b + 1]);
            battle.teams[i].color = vec3(atof(t[b + 2]), atof(t[b + 3]), atof(t[b + 4]));
            battle.teams[i].name = MPName(t[b + 5], false);
        }

        while(int(battle_teams.length()) < n) {
            battle_teams.insertLast(BattleTeamState());
        }

        battle_net_reserve.resize(n);

        if(!battle_mirror) {
            battle_mirror = true;
            battle_player_id = mp_me;
            battle.player_mode = 1;
            BattleSetPhase(kBattleSpawn);
            BattleLog("mirroring the host's battle: " + n + " teams");
        }
    } else if(t[2] == "state" && battle_mirror && t.length() >= 7) {
        BattlePhase phase = BattlePhase(atoi(t[3]));

        if(phase != battle_phase) {
            BattleSetPhase(phase);

            if(phase != kBattleOver) {
                battle_panel_open = false;
            }
        }

        battle_winner = atoi(t[4]);
        float clock = atoi(t[5]) / 100.0f;
        battle_last_go = atoi(t[6]);

        if(phase == kBattleOver) {
            battle_fight_end = the_time;
            battle_fight_start = the_time - clock;
        } else if(phase == kBattleFight) {
            battle_fight_start = the_time - clock;
        }

        for(int i = 0; i < battle.num_teams && 7 + i * 3 + 2 < int(t.length()); ++i) {
            battle_teams[i].alive = atoi(t[7 + i * 3]);
            battle_teams[i].fallen = atoi(t[8 + i * 3]);
            battle_net_reserve[i] = atoi(t[9 + i * 3]);
        }
    } else if(t[2] == "player" && t.length() >= 13) {
        int id = MPId(t[3]);

        if(id == -1 || id != mp_me || !MovementObjectExists(id)) {
            return;  // another machine's player
        }

        int team = atoi(t[4]);
        vec3 p(atof(t[5]), atof(t[6]), atof(t[7]));
        vec3 f(atof(t[8]), 0.0f, atof(t[9]));
        vec3 c(atof(t[10]), atof(t[11]), atof(t[12]));
        MovementObject@ me = ReadCharacterID(id);
        ReadObjectFromID(id).UpdateScriptParams();  // the host set our team
        // Dialogue control on revives and stands the body up, off gives control back
        me.ReceiveScriptMessage("set_dialogue_control true");
        me.ReceiveScriptMessage("set_dialogue_control false");
        me.Execute("this_mo.position = vec3(" + p.x + ", " + p.y + ", " + p.z + "); this_mo.velocity = vec3(0.0f); " +
            "this_mo.SetRotationFromFacing(vec3(" + f.x + ", 0.0f, " + f.z + ")); " +
            "for(int i = 0; i < 4; ++i) { if(character_getter.GetChannel(i) == \"cloth\") { " +
            "ReadObjectFromID(this_mo.GetID()).SetPaletteColor(i, vec3(" + c.x + ", " + c.y + ", " + c.z + ")); } }");
        battle_player_id = id;
        battle.player_team = team;
        battle.player_mode = 1;
        BattleLog("placed by the host: team " + team);
    }
}

// ---- for the HUD ------------------------------------------------------------------

// The battle team of a character (its "aiob_team_<n>" Teams parameter), or -1
int BattleTeamOf(int id) {
    if(!BattleActive() || !ObjectExists(id)) {
        return -1;
    }

    if(id == battle_player_id && battle.player_mode == 1 && !battle_spectating) {
        return battle.player_team;
    }

    string teams = ReadObjectFromID(id).GetScriptParams().GetString("Teams");

    if(teams.findFirst(",") >= 0 || teams.findFirst("aiob_team_") != 0) {
        return -1;  // not a fighter of ours (the spectator is on every team)
    }

    int t = atoi(teams.substr(10));
    return t >= 0 && t < battle.num_teams ? t : -1;
}

string BattleTeamName(int id) {
    int t = BattleTeamOf(id);
    return t >= 0 ? battle.teams[t].name : "";
}

bool BattleColorOf(int id, vec3 &out color) {
    int t = BattleTeamOf(id);

    if(t < 0) {
        return false;
    }

    color = battle.teams[t].color;
    return true;
}

bool BattleSpectating() {
    return BattleActive() && battle_spectating;
}
