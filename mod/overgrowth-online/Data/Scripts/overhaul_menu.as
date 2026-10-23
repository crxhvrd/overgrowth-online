//-----------------------------------------------------------------------------
//           Name: overhaul_menu.as
//    Description: The Overhaul window. A level hook (mod.xml <LevelHookFile>),
//                 so it runs in every level. F9 opens and closes it (General
//                 tab: menu key). One tab per part of the mod; each tab lives
//                 in its own overhaul_tab_*.as file and is registered in
//                 RegisterTabs(). Settings are config keys saved to
//                 config.txt; NPC brains re-read theirs live.
//                 Also runs the "Large Battles" mode (aio_battle.as) in the
//                 level the battle setup menu loaded.
//-----------------------------------------------------------------------------

#include "overhaul_online.as"
#include "aio_battle.as"
#include "overhaul_tab_ai.as"
#include "overhaul_tab_general.as"
#include "overhaul_hud.as"
#include "overhaul_tab_gui.as"
#include "overhaul_tab_camera.as"
#include "overhaul_finisher.as"
#include "overhaul_trainer.as"
#include "overhaul_tab_trainer.as"
#include "overhaul_weather.as"
#include "overhaul_tab_weather.as"

const string kVersion = "0.40.0";

bool menu_open = false;
bool paused_by_menu = false;
bool settings_changed = false;
float notify_after = 0.0f;

bool CfgBool(const string &in key, bool fallback) {
    return ConfigHasKey(key) ? GetConfigValueBool(key) : fallback;
}

float CfgFloat(const string &in key, float fallback) {
    return ConfigHasKey(key) ? GetConfigValueFloat(key) : fallback;
}

int CfgInt(const string &in key, int fallback) {
    return ConfigHasKey(key) ? GetConfigValueInt(key) : fallback;
}

string MenuKey() {
    return ConfigHasKey("aio_menu_key") ? GetConfigValueString("aio_menu_key") : "f9";
}

void Init(string level_name) {
    AIOBattleInit(level_name);

    if(CfgBool("aio_test_menu_open", false)) {
        SetOpen(true);
    }
}

// Tell every brain to re-read the settings now instead of within half a second
void NotifyBrains() {
    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        ReadCharacter(i).ReceiveScriptMessage("aio_settings_changed");
    }

    settings_changed = false;
}

void SetOpen(bool open) {
    if(open == menu_open) {
        return;
    }

    menu_open = open;

    if(open) {
        if(CfgBool("aio_menu_pause", true) && !EditorModeActive()) {
            SetPaused(true);
            paused_by_menu = true;
        }
    } else {
        if(paused_by_menu) {
            SetPaused(false);
            paused_by_menu = false;
        }

        SaveConfig();
        NotifyBrains();
    }
}

void Update(int is_paused) {
    if(GetInputPressed(0, MenuKey())) {
        SetOpen(!menu_open);
    }

    if(settings_changed && the_time >= notify_after) {
        NotifyBrains();
    }

    AIOBattleUpdate(is_paused);
    TrainerUpdate();
    FinisherUpdate();
    WeatherUpdate(is_paused);
}

// While open the level has focus: the cursor is free and the player's
// character ignores attack and camera input (stock level.as and aschar.as)
bool HasFocus() {
    return menu_open || AIOBattleHasFocus();
}

void ReceiveMessage(string message) {
    if(OverhaulOnlineReceive(message)) {
        return;
    }

    AIOBattleReceive(message);
    HudFeedReceive(message);
    TrainerReceive(message);
    FinisherReceive(message);
    WeatherReceive(message);
}

// ---- widgets shared by the tabs -----------------------------------------------

const vec4 kHeaderColor = vec4(0.95f, 0.78f, 0.35f, 1.0f);
const vec4 kNoteColor = vec4(0.65f, 0.65f, 0.65f, 1.0f);
const vec4 kTabActive = vec4(0.78f, 0.52f, 0.16f, 1.0f);
const vec4 kTabIdle = vec4(0.22f, 0.22f, 0.26f, 1.0f);
const vec4 kTabHover = vec4(0.36f, 0.33f, 0.30f, 1.0f);

void Changed() {
    settings_changed = true;
    notify_after = the_time + 0.1f;
}

void Tip(const string &in text) {
    if(text != "" && ImGui_IsItemHovered()) {
        ImGui_SetTooltip(text);
    }
}

void Header(const string &in text) {
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, text);
    ImGui_Separator();
}

void Note(const string &in text) {
    ImGui_TextColored(kNoteColor, text);
}

void Toggle(const string &in label, const string &in key, bool fallback, const string &in tip) {
    bool value = CfgBool(key, fallback);

    if(ImGui_Checkbox(label, value)) {
        SetConfigValueBool(key, value);
        Changed();
    }

    Tip(tip);
}

// 0 = off, 1 = as tuned per character, 2 = double
void Scale(const string &in label, const string &in key, const string &in tip) {
    float value = CfgFloat(key, 1.0f);

    if(ImGui_SliderFloat(label, value, 0.0f, 2.0f, value < 0.05f ? "Off" : "%.1fx")) {
        SetConfigValueFloat(key, value < 0.05f ? 0.0f : value);
        Changed();
    }

    Tip(tip);
}

// ---- tabs -------------------------------------------------------------------------

funcdef void OverhaulTabFn();

class OverhaulTab {
    string name;
    string tip;
    OverhaulTabFn@ draw;
    OverhaulTabFn@ reset;   // null: nothing to reset
}

array<OverhaulTab@> overhaul_tabs;
int overhaul_tab = -1;

void AddTab(const string &in name, const string &in tip, OverhaulTabFn@ draw, OverhaulTabFn@ reset) {
    OverhaulTab@ tab = OverhaulTab();
    tab.name = name;
    tab.tip = tip;
    @tab.draw = draw;
    @tab.reset = reset;
    overhaul_tabs.insertLast(tab);
}

// One line per tab, in the order they appear
void RegisterTabs() {
    AddTab("AI", "Smarter enemies: jump kicks, air combat, movement, weapons, group and team tactics",
        @DrawAITab, @ResetAITab);
    AddTab("GUI", "The HUD: health panel, minimap, names and health above characters, attacker arrows",
        @DrawGUITab, @ResetGUITab);
    AddTab("Camera", "The third-person camera: distance, height, shoulder, field of view, smoothing, look limits, " +
        "fight framing", @DrawCameraTab, @ResetCameraTab);
    AddTab("Weather", "Time of day (the sun, or a moon at night) and weather: clouds, rain, storms, snow",
        @DrawWeatherTab, @ResetWeatherTab);
    AddTab("Trainer", "Cheats: god mode, strength and speed, weapons, teleport, enemies, spawning, game speed, gravity",
        @DrawTrainerTab, @ResetTrainerTab);
    AddTab("General", "The Overhaul window itself, and what it contains", @DrawGeneralTab, @ResetGeneralTab);
}

// The tabs share the window's width; too narrow for one row, they wrap into
// even rows instead of running off the edge
void DrawTabBar() {
    const float kGap = 6.0f;
    const float kMinTab = 96.0f;
    int count = int(overhaul_tabs.length());
    float avail = ImGui_GetContentRegionAvailWidth();
    int per_row = max(1, min(count, int((avail + kGap) / (kMinTab + kGap))));
    int rows = (count + per_row - 1) / per_row;
    per_row = (count + rows - 1) / rows;
    float width = floor((avail - kGap * float(per_row - 1)) / float(per_row));
    ImGui_PushStyleVar(ImGuiStyleVar_ItemSpacing, vec2(kGap, kGap));

    for(int i = 0; i < count; ++i) {
        if(i % per_row != 0) {
            ImGui_SameLine();
        }

        bool active = i == overhaul_tab;
        ImGui_PushStyleColor(ImGuiCol_Button, active ? kTabActive : kTabIdle);
        ImGui_PushStyleColor(ImGuiCol_ButtonHovered, active ? kTabActive : kTabHover);
        ImGui_PushStyleColor(ImGuiCol_ButtonActive, kTabActive);

        if(ImGui_Button(overhaul_tabs[i].name, vec2(width, 28.0f)) && !active) {
            overhaul_tab = i;
            SetConfigValueInt("overhaul_menu_tab", overhaul_tab);
        }

        ImGui_PopStyleColor(3);
        Tip(overhaul_tabs[i].tip);
    }

    ImGui_PopStyleVar();
    ImGui_Separator();
}

// ---- window ----------------------------------------------------------------

void DrawGUI() {
    OverhaulHudDraw();
    AIOBattleDrawGUI();

    if(!menu_open) {
        return;
    }

    if(overhaul_tabs.length() == 0) {
        RegisterTabs();
    }

    if(overhaul_tab < 0 || overhaul_tab >= int(overhaul_tabs.length())) {
        overhaul_tab = max(0, min(int(overhaul_tabs.length()) - 1, CfgInt("overhaul_menu_tab", 0)));
    }

    // Top-centre, as tall as the screen allows; the tab's page scrolls
    float screen_w = float(GetScreenWidth());
    float screen_h = float(GetScreenHeight());
    float width = 600.0f;
    float height = min(780.0f, screen_h - 60.0f);
    ImGui_SetNextWindowSize(vec2(width, height), ImGuiSetCond_Appearing);
    ImGui_SetNextWindowPos(vec2(max(10.0f, (screen_w - width) * 0.5f), 30.0f), ImGuiSetCond_Appearing);
    bool keep_open = true;

    if(ImGui_Begin("Overhaul " + kVersion + "   [" + ToUpper(MenuKey()) + " to close]", keep_open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        DrawTabBar();
        OverhaulTab@ tab = overhaul_tabs[overhaul_tab];
        ImGui_BeginChild("overhaul_page", vec2(0.0f, -40.0f), false);
        ImGui_PushItemWidth(240.0f);
        tab.draw();
        ImGui_PopItemWidth();
        ImGui_EndChild();
        ImGui_Separator();

        if(tab.reset !is null) {
            if(ImGui_Button("Reset " + tab.name)) {
                tab.reset();
                Changed();
            }

            Tip("Back to the defaults for this tab only");
            ImGui_SameLine();
        }

        if(ImGui_Button("Close")) {
            keep_open = false;
        }

        ImGui_SameLine();
        Note("Changes apply at once and are saved.");
    }

    ImGui_End();

    if(!keep_open) {
        SetOpen(false);
    }
}
