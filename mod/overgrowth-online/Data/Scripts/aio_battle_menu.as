//-----------------------------------------------------------------------------
//           Name: aio_battle_menu.as
//    Description: Overhaul "Large Battles" setup screen (Play menu ->
//                 Large Battles). Pick a map, the teams (size, species, AI,
//                 skill, weapons...), the rules and your role, then Start:
//                 the settings are saved to aiob_* config keys and the map
//                 loads; aio_battle.as takes over inside the level.
//-----------------------------------------------------------------------------

#include "menu_common.as"
#include "music_load.as"
#include "aio_battle_common.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;

class BattleMap {
    string title;
    string path;
    string thumb;
    string source;
}

BattleSettings setup;
array<BattleMap@> maps;
int selected_map = -1;
string map_filter;
TextureAssetRef map_texture;
string map_texture_path;
bool setup_dirty = false;
uint clean_map_count = 0;    // the first ones in the list are the clean battle maps

const vec4 kTitleColor = vec4(0.95f, 0.78f, 0.35f, 1.0f);
const vec4 kDimColor = vec4(0.65f, 0.65f, 0.65f, 1.0f);
const vec4 kWarnColor = vec4(1.0f, 0.55f, 0.3f, 1.0f);

bool HasFocus() {
    return false;
}

void Initialize() {
    @imGUI = CreateIMGUI();
    PlaySong("overgrowth_main");
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);
    imGUI.setup();
    setBackGround();
    setup.Load();
    BuildMapList();
}

void Dispose() {
    imGUI.clear();
}

bool CanGoBack() {
    return true;
}

void Update() {
    UpdateKeyboardMouse();

    while(imGUI.getMessageQueueSize() > 0) {
        IMMessage@ message = imGUI.getNextMessage();

        if(message.name == "Back") {
            this_ui.SendCallback("back");
        }
    }

    imGUI.update();
    UpdateController();
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
    DrawSetup();
}

void Draw() {
}

void Init(string str) {
}

// ---- maps ---------------------------------------------------------------------

string LowerPath(const string &in path) {
    string p = path;

    for(uint i = 0; i < p.length(); ++i) {
        if(p[i] >= 65 && p[i] <= 90) {
            p[i] = p[i] + 32;
        } else if(p[i] == 92) {
            p[i] = 47;  // backslash to slash
        }
    }

    return p;
}

void AddMap(const string &in title, const string &in path, const string &in thumb, const string &in source) {
    string key = LowerPath(path);

    for(uint i = 0; i < maps.length(); ++i) {
        if(LowerPath(maps[i].path) == key) {
            return;
        }
    }

    BattleMap@ map = BattleMap();
    map.title = title;
    map.path = path;
    map.thumb = thumb;
    map.source = source;
    maps.insertLast(map);
}

void AddCampaignMaps(Campaign campaign) {
    array<ModLevel>@ levels = campaign.GetLevels();

    for(uint k = 0; k < levels.length(); ++k) {
        AddMap(levels[k].GetTitle(), levels[k].GetPath(), levels[k].GetThumbnail(), campaign.GetTitle());
    }
}

void BuildMapList() {
    maps.resize(0);

    // Ours: clean copies of open maps, every scenario element removed
    // (tools/make_battle_maps.py); the rest only when asked for
    string ours = GetCurrCampaignID();
    array<Campaign>@ campaigns = GetCampaigns();

    for(uint i = 0; i < campaigns.length(); ++i) {
        if(campaigns[i].GetID() == ours) {
            AddCampaignMaps(campaigns[i]);
        }
    }

    clean_map_count = maps.length();

    if(!setup.show_all_maps) {
        SelectSavedMap();
        return;
    }

    for(uint i = 0; i < campaigns.length(); ++i) {
        if(campaigns[i].GetID() != ours) {
            AddCampaignMaps(campaigns[i]);
        }
    }

    array<ModID>@ sids = GetActiveModSids();

    for(uint i = 0; i < sids.length(); ++i) {
        array<ModLevel>@ levels = ModGetSingleLevels(sids[i]);

        for(uint k = 0; k < levels.length(); ++k) {
            AddMap(levels[k].GetTitle(), levels[k].GetPath(), levels[k].GetThumbnail(), ModGetName(sids[i]));
        }
    }

    SelectSavedMap();
}

void SelectSavedMap() {
    selected_map = maps.length() > 0 ? 0 : -1;

    for(uint i = 0; i < maps.length(); ++i) {
        if(LowerPath(maps[i].path) == LowerPath(setup.level_path)) {
            selected_map = int(i);
        }
    }
}

// Section of a clean battle map, from its folder in overhaul_battles/
string MapGroup(const string &in path) {
    string p = LowerPath(path);
    string prefix = "overhaul_battles/";

    if(p.findFirst(prefix) != 0) {
        return "Other maps (keep their own scenario)";
    }

    string rest = p.substr(prefix.length());
    int slash = rest.findFirst("/");

    if(slash < 0) {
        return "Open maps and arenas";
    }

    string folder = rest.substr(0, slash);

    if(folder == "overgrowth") return "Overgrowth story";
    if(folder == "lugaru") return "Lugaru story";
    if(folder == "drika") return "Drika's Story (needs that mod enabled)";
    if(folder == "therium") return "Therium-2 (needs that mod enabled)";
    return folder;
}

bool MapMatches(BattleMap@ map) {
    if(map_filter.length() == 0) {
        return true;
    }

    string f = LowerPath(map_filter);
    return LowerPath(map.title).findFirst(f) >= 0 || LowerPath(map.source).findFirst(f) >= 0 ||
        LowerPath(MapGroup(map.path)).findFirst(f) >= 0;
}

// ---- setup screen -----------------------------------------------------------------

void Tip(const string &in text) {
    if(ImGui_IsItemHovered()) {
        ImGui_SetTooltip(text);
    }
}

void Title(const string &in text) {
    ImGui_TextColored(kTitleColor, text);
    ImGui_Separator();
}

void DrawSetup() {
    float sw = float(GetScreenWidth());
    float sh = float(GetScreenHeight());
    ImGui_SetNextWindowPos(vec2(sw * 0.04f, sh * 0.06f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(sw * 0.92f, sh * 0.88f), ImGuiSetCond_Always);
    ImGui_Begin("Large Battles  -  Overgrowth Online", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(sh >= 1000.0f ? 1.2f : 1.0f);

    float body_h = sh * 0.88f - 120.0f;
    ImGui_Columns(3, "aiob_setup_cols", true);

    ImGui_BeginChild("aiob_maps", vec2(0.0f, body_h), false);
    DrawMapColumn();
    ImGui_EndChild();
    ImGui_NextColumn();

    ImGui_BeginChild("aiob_teams", vec2(0.0f, body_h), false);
    DrawTeamsColumn();
    ImGui_EndChild();
    ImGui_NextColumn();

    ImGui_BeginChild("aiob_rules", vec2(0.0f, body_h), false);
    DrawRulesColumn();
    ImGui_EndChild();

    ImGui_Columns(1);
    ImGui_Separator();
    DrawButtons();
    ImGui_End();
}

void DrawMapColumn() {
    Title("Map");
    bool all = setup.show_all_maps;

    if(ImGui_Checkbox("Also show every installed map", all)) {
        setup.show_all_maps = all;
        BuildMapList();
    }

    Tip("Off: only the clean battle maps (no characters, cutscenes, objectives or arena rules).
" +
        "On: also every other map, which keeps its own scenario and events.");
    ImGui_PushItemWidth(-1.0f);
    ImGui_InputText("##aiob_filter", map_filter, 64);
    Tip("Filter the maps by name");
    ImGui_PopItemWidth();
    vec2 avail = ImGui_GetContentRegionAvail();
    float thumb_h = avail.x * 9.0f / 16.0f;
    ImGui_BeginChild("aiob_map_list", vec2(0.0f, max(120.0f, avail.y - thumb_h - 60.0f)), true);

    string group = "-";
    bool group_open = true;
    bool searching = map_filter.length() > 0;

    for(uint i = 0; i < maps.length(); ++i) {
        if(!MapMatches(maps[i])) {
            continue;
        }

        // One collapsible section per campaign; a search shows every match
        string this_group = i < clean_map_count ? MapGroup(maps[i].path) : "Other maps (keep their own scenario)";

        if(this_group != group) {
            group = this_group;

            if(searching) {
                ImGui_TextColored(kTitleColor, group);
                group_open = true;
            } else {
                ImGui_SetNextTreeNodeOpen(i == 0, ImGuiSetCond_Once);
                group_open = ImGui_CollapsingHeader(group + "###group_" + group);
            }
        }

        if(!group_open) {
            continue;
        }

        if(ImGui_Selectable(maps[i].title + "##map" + i, int(i) == selected_map)) {
            selected_map = int(i);
        }

        if(ImGui_IsItemHovered()) {
            ImGui_SetTooltip(maps[i].source + "\n" + maps[i].path);
        }
    }

    ImGui_EndChild();

    if(selected_map >= 0) {
        BattleMap@ map = maps[selected_map];
        string thumb = "Data/" + map.thumb;

        if(map_texture_path != thumb) {
            map_texture_path = thumb;

            if(map.thumb != "" && FileExists(thumb)) {
                map_texture = LoadTexture(thumb, TextureLoadFlags_NoMipmap | TextureLoadFlags_NoReduce);
            } else {
                map_texture_path = "";
            }
        }

        if(map_texture_path != "") {
            ImGui_Image(map_texture, vec2(avail.x, thumb_h));
        }

        ImGui_TextColored(kTitleColor, map.title);
        ImGui_TextColored(kDimColor, map.source);
    }
}

void DrawTeamsColumn() {
    Title("Teams");
    ImGui_PushItemWidth(-140.0f);
    int teams = setup.num_teams;

    if(ImGui_SliderInt("Number of teams", teams, 2, kMaxTeams)) {
        setup.num_teams = teams;
    }

    ImGui_PopItemWidth();

    if(ImGui_Button("Same as team 1 for all")) {
        for(int t = 1; t < setup.num_teams; ++t) {
            BattleTeam@ src = setup.teams[0];
            BattleTeam@ dst = setup.teams[t];
            dst.count = src.count;
            dst.species = src.species;
            dst.ai = src.ai;
            dst.skill = src.skill;
            dst.weapon = src.weapon;
            dst.spare_knives = src.spare_knives;
            dst.toughness = src.toughness;
            dst.size = src.size;
            dst.armor = src.armor;
        }
    }

    Tip("Copy the first team's settings to every team (names and colours stay)");
    ImGui_SameLine();

    if(ImGui_Button("Default colours")) {
        for(int t = 0; t < kMaxTeams; ++t) {
            setup.teams[t].name = kTeamDefaultNames[t];
            setup.teams[t].color = kTeamDefaultColors[t];
        }
    }

    ImGui_Spacing();

    for(int t = 0; t < setup.num_teams; ++t) {
        DrawTeam(t);
    }
}

void DrawTeam(int t) {
    BattleTeam@ team = setup.teams[t];
    string id = "##t" + t;
    ImGui_SetNextTreeNodeOpen(t < 2, ImGuiSetCond_Once);
    bool open = ImGui_CollapsingHeader(team.name + "  -  " + team.count + " " + kSpeciesNames[team.species] + ", " +
        kAINames[team.ai] + "###team" + t);

    if(!open) {
        return;
    }

    ImGui_PushItemWidth(-140.0f);
    vec3 color = team.color;

    if(ImGui_ColorEdit3("Colour" + id, color)) {
        team.color = color;
    }

    string name = team.name;

    if(ImGui_InputText("Name" + id, name, 32)) {
        team.name = name;
    }

    int count = team.count;

    if(ImGui_SliderInt("Fighters" + id, count, 1, kMaxPerTeam)) {
        team.count = count;
    }

    int species = team.species;

    if(ImGui_Combo("Species" + id, species, kSpeciesNames)) {
        team.species = species;
    }

    Tip("Mixed: rabbits, cats, dogs, rats and a few wolves. Wolves fight without weapons.");
    int ai = team.ai;

    if(ImGui_Combo("AI" + id, ai, kAINames)) {
        team.ai = ai;
    }

    Tip("Overgrowth Online AI: everything, team tactics included.\nNo team tactics: its fighting without the battle read, target choice and morale.\nVanilla: the game's own AI.");
    float skill = team.skill;

    if(ImGui_SliderFloat("Skill" + id, skill, 0.0f, 1.0f, "%.2f")) {
        team.skill = skill;
    }

    Tip("Blocking, follow-ups, attack speed and damage, aggression");
    int weapon = team.weapon;

    if(ImGui_Combo("Weapons" + id, weapon, kWeaponNames)) {
        team.weapon = weapon;
    }

    float toughness = team.toughness;

    if(ImGui_SliderFloat("Toughness" + id, toughness, 0.4f, 2.5f, "%.1fx")) {
        team.toughness = toughness;
    }

    Tip("Damage resistance: how many hits it takes to knock them out");
    float size = team.size;

    if(ImGui_SliderFloat("Size" + id, size, 0.75f, 1.35f, "%.2fx")) {
        team.size = size;
    }

    bool armor = team.armor;

    if(ImGui_Checkbox("Metal armour" + id, armor)) {
        team.armor = armor;
    }

    Tip("Blades glance off: only blunt hits and unarmed fighting work well");
    ImGui_SameLine();
    bool knives = team.spare_knives;

    if(ImGui_Checkbox("Throwing knives" + id, knives)) {
        team.spare_knives = knives;
    }

    Tip("Two spare throwing knives each (needs Throwing on)");
    ImGui_PopItemWidth();
    ImGui_Spacing();
}

void DrawRulesColumn() {
    Title("You");
    ImGui_PushItemWidth(-140.0f);
    int mode = setup.player_mode;

    if(ImGui_Combo("Your role", mode, kPlayerModes)) {
        setup.player_mode = mode;
    }

    if(setup.player_mode == 1) {
        array<string> names;

        for(int t = 0; t < setup.num_teams; ++t) {
            names.insertLast(setup.teams[t].name);
        }

        int team = min(setup.player_team, setup.num_teams - 1);

        if(ImGui_Combo("Your team", team, names)) {
            setup.player_team = team;
        }

        setup.player_team = team;
        ImGui_TextColored(kDimColor, "Knocked out: you keep watching with the free camera.");
    } else {
        ImGui_TextColored(kDimColor, "WASD + mouse to fly, left click to follow a fighter.");
    }

    ImGui_Spacing();
    Title("Battle");
    bool manual = setup.manual_place;

    if(ImGui_Checkbox("Place the teams myself", manual)) {
        setup.manual_place = manual;
    }

    Tip("The map loads first: click the ground to put each team where you want it, then start.
Placements are remembered per map.");
    int layout = setup.layout;

    if(ImGui_Combo(setup.manual_place ? "Teams you skip" : "Start", layout, kLayoutNames)) {
        setup.layout = layout;
    }

    if(setup.manual_place && ImGui_IsItemHovered()) {
        ImGui_SetTooltip("How teams you leave unplaced are put on the map");
    }

    int formation = setup.formation;

    if(setup.layout != 1 && ImGui_Combo("Formation", formation, kFormationNames)) {
        setup.formation = formation;
    }

    float spread = setup.spread;

    if(ImGui_SliderFloat("Distance", spread, 8.0f, 70.0f, "%.0f m")) {
        setup.spread = spread;
    }

    Tip("How far each team starts from the middle of the map (moved closer where the ground ends)");
    bool aware = setup.aware;

    if(ImGui_Checkbox("Charge at once", aware)) {
        setup.aware = aware;
    }

    Tip("On: everyone knows where the enemy is and charges.\nOff: they must find each other first (idle ones are sent in after 20 s).");
    bool flee = setup.never_flee;

    if(ImGui_Checkbox("Nobody flees", flee)) {
        setup.never_flee = flee;
    }

    Tip("Off: fighters can panic at wolves or when badly hurt, as in the story");
    bool slowmo = setup.slowmo_finish;

    if(ImGui_Checkbox("Slow motion on the final blow", slowmo)) {
        setup.slowmo_finish = slowmo;
    }

    bool keep = setup.keep_level_npcs;

    if(ImGui_Checkbox("Keep the map's own characters", keep)) {
        setup.keep_level_npcs = keep;
    }

    Tip("Off (default): the map's NPCs are removed before the battle");

    ImGui_Spacing();
    Title("Weapons");
    bool melee = setup.melee_weapons;

    if(ImGui_Checkbox("Melee weapons", melee)) {
        setup.melee_weapons = melee;
    }

    Tip("Off: everyone fights unarmed, whatever their team's weapons");
    ImGui_SameLine();
    bool throwing = setup.throwing;

    if(ImGui_Checkbox("Throwing", throwing)) {
        setup.throwing = throwing;
    }

    Tip("Off: nobody throws weapons");

    ImGui_Spacing();
    Title("Reinforcements and limits");
    int waves = setup.waves;

    if(ImGui_SliderInt("Waves", waves, 1, 10)) {
        setup.waves = waves;
    }

    Tip("Each team brings this many times its fighters: a fallen fighter is replaced at the team's start until the reserve runs out");

    if(setup.waves > 1) {
        float delay = setup.wave_delay;

        if(ImGui_SliderFloat("Replacement delay", delay, 2.0f, 30.0f, "%.0f s")) {
            setup.wave_delay = delay;
        }
    }

    int limit = setup.time_limit;

    if(ImGui_SliderInt("Time limit", limit, 0, 20, limit == 0 ? "None" : "%.0f min")) {
        setup.time_limit = limit;
    }

    Tip("When time runs out the team with the most fighters standing wins");
    int cleanup = setup.cleanup;

    if(ImGui_SliderInt("Remove bodies after", cleanup, 0, 120, cleanup == 0 ? "Never" : "%.0f s")) {
        setup.cleanup = cleanup;
    }

    Tip("Keeps big battles running smoothly");
    ImGui_PopItemWidth();

    ImGui_Spacing();
    int total = setup.TotalOnField();
    ImGui_Text("Fighters on the field: " + total);

    if(total > 70) {
        ImGui_TextColored(kWarnColor, "Very large battles can run slowly.");
    }
}

void DrawButtons() {
    bool can_start = selected_map >= 0;

    if(ImGui_Button(can_start ? "START BATTLE" : "Pick a map", vec2(220.0f, 40.0f)) && can_start) {
        StartBattle();
    }

    ImGui_SameLine();

    if(ImGui_Button("Back", vec2(120.0f, 40.0f))) {
        setup.Save();
        this_ui.SendCallback("back");
    }

    ImGui_SameLine();

    if(ImGui_Button("Reset setup", vec2(120.0f, 40.0f))) {
        string level = setup.level_path;
        string title = setup.level_title;
        setup = BattleSettings();
        setup.level_path = level;
        setup.level_title = title;
    }

    if(selected_map >= 0) {
        ImGui_SameLine();
        ImGui_TextColored(kDimColor, "   " + maps[selected_map].title + ",  " + setup.num_teams + " teams,  " +
            setup.TotalOnField() + " fighters" + (setup.waves > 1 ? " (x" + setup.waves + " waves)" : ""));
    }
}

void StartBattle() {
    BattleMap@ map = maps[selected_map];
    setup.level_path = map.path;
    setup.level_title = map.title;
    setup.Save();
    SetConfigValueBool("aiob_pending", true);

    // Large Battles stays offline for now (Overgrowth Online is campaign co-op):
    // no "host the next map" left over from an earlier version
    if(ConfigHasKey("ohl_host_on_load")) {
        SetConfigValueBool("ohl_host_on_load", false);
    }

    SaveConfig();
    this_ui.SendCallback(map.path);
}
