//-----------------------------------------------------------------------------
//           Name: aio_battle_common.as
//    Description: Overhaul "Large Battles" settings, shared by the setup
//                 menu (aio_battle_menu.as) and the in-level battle
//                 (aio_battle.as). Everything lives in aiob_* config keys, so
//                 the last setup is remembered.
//-----------------------------------------------------------------------------

const int kMaxTeams = 8;
const int kMaxPerTeam = 60;

const array<string> kSpeciesNames = {"Rabbits", "Cats", "Dogs", "Rats", "Wolves", "Mixed"};
const array<string> kAINames = {"Overgrowth Online AI", "Overgrowth Online AI, no team tactics", "Vanilla Overgrowth AI"};
const array<string> kWeaponNames = {"Unarmed", "Knives", "Swords", "Spears", "Staffs", "Big swords", "Hammers",
    "Rapiers", "Mixed weapons"};
const array<string> kPlayerModes = {"Spectate (free camera)", "Fight on a team"};
const array<string> kLayoutNames = {"Ring: teams around the map centre", "Scattered: everyone mixed up", "Two fronts: teams split into two sides"};
const array<string> kFormationNames = {"Blob", "Lines"};

const array<string> kTeamDefaultNames = {"Red", "Blue", "Green", "Gold", "Purple", "Orange", "Cyan", "White"};
const array<vec3> kTeamDefaultColors = {
    vec3(0.85f, 0.08f, 0.06f), vec3(0.08f, 0.24f, 0.95f), vec3(0.10f, 0.70f, 0.15f), vec3(0.95f, 0.78f, 0.10f),
    vec3(0.55f, 0.15f, 0.80f), vec3(0.98f, 0.45f, 0.05f), vec3(0.10f, 0.80f, 0.85f), vec3(0.92f, 0.92f, 0.92f)
};

class BattleTeam {
    string name;
    vec3 color;
    int count = 6;
    int species = 0;
    int ai = 0;
    float skill = 0.6f;
    int weapon = 0;
    bool spare_knives = false;
    float toughness = 1.0f;
    float size = 1.0f;
    bool armor = false;
}

class BattleSettings {
    string level_path = "overhaul_battles/forgotten_plains.xml";
    string level_title = "Forgotten Plains";
    int num_teams = 2;
    array<BattleTeam> teams;

    int player_mode = 0;       // 0 spectate, 1 fight
    int player_team = 0;
    int layout = 0;
    int formation = 0;
    float spread = 22.0f;      // metres from the centre to each team
    bool melee_weapons = true;
    bool throwing = true;
    int waves = 1;             // each team fields `count` at once, `count * waves` in all
    float wave_delay = 8.0f;   // seconds before a fallen fighter's replacement arrives
    int time_limit = 0;        // minutes, 0 = none
    int cleanup = 20;          // seconds before a knocked-out body is removed, 0 = never
    bool aware = true;         // everyone knows where the enemy is from the start
    bool never_flee = true;
    bool slowmo_finish = true;
    bool keep_level_npcs = false;
    bool manual_place = false;  // place each team on the map yourself
    bool show_all_maps = false; // also list maps that keep their own scenario

    BattleSettings() {
        teams.resize(kMaxTeams);

        for(int i = 0; i < kMaxTeams; ++i) {
            teams[i].name = kTeamDefaultNames[i];
            teams[i].color = kTeamDefaultColors[i];
        }
    }

    int TotalOnField() {
        int total = 0;

        for(int i = 0; i < num_teams; ++i) {
            total += teams[i].count;
        }

        return total;
    }

    void Load() {
        level_path = BCfgString("level", level_path);
        level_title = BCfgString("level_title", level_title);
        num_teams = max(2, min(kMaxTeams, BCfgInt("teams", num_teams)));
        player_mode = BCfgInt("player_mode", player_mode);
        player_team = BCfgInt("player_team", player_team);
        layout = BCfgInt("layout", layout);
        formation = BCfgInt("formation", formation);
        spread = BCfgFloat("spread", spread);
        melee_weapons = BCfgBool("melee_weapons", melee_weapons);
        throwing = BCfgBool("throwing", throwing);
        waves = max(1, BCfgInt("waves", waves));
        wave_delay = BCfgFloat("wave_delay", wave_delay);
        time_limit = BCfgInt("time_limit", time_limit);
        cleanup = BCfgInt("cleanup", cleanup);
        aware = BCfgBool("aware", aware);
        never_flee = BCfgBool("never_flee", never_flee);
        slowmo_finish = BCfgBool("slowmo_finish", slowmo_finish);
        keep_level_npcs = BCfgBool("keep_level_npcs", keep_level_npcs);
        manual_place = BCfgBool("manual_place", manual_place);
        show_all_maps = BCfgBool("show_all_maps", show_all_maps);

        for(int i = 0; i < kMaxTeams; ++i) {
            string t = "t" + i + "_";
            BattleTeam@ team = teams[i];
            team.name = BCfgString(t + "name", team.name);
            team.color = vec3(BCfgFloat(t + "r", team.color.x), BCfgFloat(t + "g", team.color.y), BCfgFloat(t + "b", team.color.z));
            team.count = max(1, min(kMaxPerTeam, BCfgInt(t + "count", team.count)));
            team.species = BCfgInt(t + "species", team.species);
            team.ai = BCfgInt(t + "ai", team.ai);
            team.skill = BCfgFloat(t + "skill", team.skill);
            team.weapon = BCfgInt(t + "weapon", team.weapon);
            team.spare_knives = BCfgBool(t + "spare_knives", team.spare_knives);
            team.toughness = BCfgFloat(t + "toughness", team.toughness);
            team.size = BCfgFloat(t + "size", team.size);
            team.armor = BCfgBool(t + "armor", team.armor);
        }
    }

    void Save() {
        SetConfigValueString("aiob_level", level_path);
        SetConfigValueString("aiob_level_title", level_title);
        SetConfigValueInt("aiob_teams", num_teams);
        SetConfigValueInt("aiob_player_mode", player_mode);
        SetConfigValueInt("aiob_player_team", player_team);
        SetConfigValueInt("aiob_layout", layout);
        SetConfigValueInt("aiob_formation", formation);
        SetConfigValueFloat("aiob_spread", spread);
        SetConfigValueBool("aiob_melee_weapons", melee_weapons);
        SetConfigValueBool("aiob_throwing", throwing);
        SetConfigValueInt("aiob_waves", waves);
        SetConfigValueFloat("aiob_wave_delay", wave_delay);
        SetConfigValueInt("aiob_time_limit", time_limit);
        SetConfigValueInt("aiob_cleanup", cleanup);
        SetConfigValueBool("aiob_aware", aware);
        SetConfigValueBool("aiob_never_flee", never_flee);
        SetConfigValueBool("aiob_slowmo_finish", slowmo_finish);
        SetConfigValueBool("aiob_keep_level_npcs", keep_level_npcs);
        SetConfigValueBool("aiob_manual_place", manual_place);
        SetConfigValueBool("aiob_show_all_maps", show_all_maps);

        for(int i = 0; i < kMaxTeams; ++i) {
            string t = "aiob_t" + i + "_";
            BattleTeam@ team = teams[i];
            SetConfigValueString(t + "name", team.name);
            SetConfigValueFloat(t + "r", team.color.x);
            SetConfigValueFloat(t + "g", team.color.y);
            SetConfigValueFloat(t + "b", team.color.z);
            SetConfigValueInt(t + "count", team.count);
            SetConfigValueInt(t + "species", team.species);
            SetConfigValueInt(t + "ai", team.ai);
            SetConfigValueFloat(t + "skill", team.skill);
            SetConfigValueInt(t + "weapon", team.weapon);
            SetConfigValueBool(t + "spare_knives", team.spare_knives);
            SetConfigValueFloat(t + "toughness", team.toughness);
            SetConfigValueFloat(t + "size", team.size);
            SetConfigValueBool(t + "armor", team.armor);
        }

        SaveConfig();
    }
}

string BCfgString(const string &in key, const string &in fallback) {
    return ConfigHasKey("aiob_" + key) ? GetConfigValueString("aiob_" + key) : fallback;
}

int BCfgInt(const string &in key, int fallback) {
    return ConfigHasKey("aiob_" + key) ? GetConfigValueInt("aiob_" + key) : fallback;
}

float BCfgFloat(const string &in key, float fallback) {
    return ConfigHasKey("aiob_" + key) ? GetConfigValueFloat("aiob_" + key) : fallback;
}

bool BCfgBool(const string &in key, bool fallback) {
    return ConfigHasKey("aiob_" + key) ? GetConfigValueBool("aiob_" + key) : fallback;
}

// Lower-case file name of a level path, for matching paths written different ways
string BattleLevelKey(const string &in path) {
    string p = path;

    for(uint i = 0; i < p.length(); ++i) {
        if(p[i] >= 65 && p[i] <= 90) {
            p[i] = p[i] + 32;  // A-Z to a-z (the engine has ToUpper only)
        }
    }

    int slash = max(p.findLast("/"), p.findLast("\\"));
    return slash >= 0 ? p.substr(slash + 1) : p;
}
