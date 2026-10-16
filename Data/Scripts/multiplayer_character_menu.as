//-----------------------------------------------------------------------------
//           Name: multiplayer_character_menu.as
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Your character for online co-op: name, character and
//                 colours. Saved in the config (playername, mp_character,
//                 mp_fur, mp_cloth); in every online level the engine puts it
//                 on your character (Online::ApplyOwnCharacter) and the other
//                 players see it.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;

const array<string> kSpecies = {"Rabbit", "Cat", "Dog", "Rat", "Wolf"};
const array<array<string>> kCharacters = {
    {"Turner|turner", "Pale Turner|pale_turner", "Rabbit 1|male_rabbit_1", "Rabbit 2|male_rabbit_2", "Rabbit 3|male_rabbit_3",
        "Female rabbit 1|female_rabbit_1", "Female rabbit 2|female_rabbit_2", "Female rabbit 3|female_rabbit_3",
        "Raider|raider_rabbit", "Guard|guard", "Guard 2|guard2", "Base guard|base_guard", "Heavy|heavy", "Heavy 2|heavy2",
        "Commander|commander", "Civilian|civ", "Pale civilian|pale_rabbit_civ", "Ancient|ancient",
        "Ancient (female)|femaleancient", "Bijou|bijou", "Jairo|jairo", "Donatas|donatas", "Reaper|reaper", "Ghost|ghost"},
    {"Cat|cat", "Male cat|male_cat", "Female cat|female_cat", "Striped cat|striped_cat", "Fancy striped cat|fancy_striped_cat",
        "Cobus|cobus", "Drika|drika", "Ferdi|ferdi"},
    {"Dog soldier|dog_soldier", "Big dog|lt_dog_big", "Female dog|lt_dog_female", "Dog 1|lt_dog_male_1", "Dog 2|lt_dog_male_2"},
    {"Rat|rat", "Female rat|female_rat", "Hooded rat|hooded_rat"},
    {"Wolf|wolf", "Male wolf|male_wolf"}
};

const vec4 kTitleColor = vec4(0.95f, 0.78f, 0.35f, 1.0f);
const vec4 kDimColor = vec4(0.65f, 0.65f, 0.65f, 1.0f);

int species = 0;
int pick = -1;       // -1: the default (the story's character for the host, Turner for the others)
bool own_fur = false;
bool own_cloth = false;
vec3 fur_color(0.55f, 0.42f, 0.30f);
vec3 cloth_color(0.40f, 0.12f, 0.10f);
string name_buffer;
bool name_rejected = false;

bool HasFocus() {
    return false;
}

string CharacterPath(int s, int i) {
    return "Data/Characters/" + kCharacters[s][i].split("|")[1] + ".xml";
}

string CharacterName(int s, int i) {
    return kCharacters[s][i].split("|")[0];
}

array<string>@ CharacterNames(int s) {
    array<string> names;

    for(uint i = 0; i < kCharacters[s].length(); ++i) {
        names.insertLast(CharacterName(s, i));
    }

    return names;
}

bool ReadColor(const string &in key, vec3 &out c) {
    if(!ConfigHasKey(key) || GetConfigValueString(key) == "") {
        return false;
    }

    array<string>@ parts = GetConfigValueString(key).split(" ");

    if(parts.length() != 3) {
        return false;
    }

    c = vec3(atof(parts[0]), atof(parts[1]), atof(parts[2]));
    return true;
}

string ColorString(const vec3 &in c) {
    return "" + c.x + " " + c.y + " " + c.z;
}

void Load() {
    name_buffer = ConfigHasKey("playername") ? GetConfigValueString("playername") : "";
    string path = ConfigHasKey("mp_character") ? GetConfigValueString("mp_character") : "";
    pick = -1;

    for(uint s = 0; s < kCharacters.length(); ++s) {
        for(uint i = 0; i < kCharacters[s].length(); ++i) {
            if(CharacterPath(s, i) == path) {
                species = s;
                pick = i;
            }
        }
    }

    own_fur = ReadColor("mp_fur", fur_color);
    own_cloth = ReadColor("mp_cloth", cloth_color);
}

void Save() {
    SetConfigValueString("playername", name_buffer);
    SetConfigValueString("mp_character", pick >= 0 ? CharacterPath(species, pick) : "");
    SetConfigValueString("mp_fur", own_fur ? ColorString(fur_color) : "");
    SetConfigValueString("mp_cloth", own_cloth ? ColorString(cloth_color) : "");
    SaveConfig();
}

void Initialize() {
    @imGUI = CreateIMGUI();
    PlaySong("overgrowth_main");
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);
    imGUI.setup();
    setBackGround();
    Load();
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
    DrawCharacter();
}

void Draw() {
}

void Init(string str) {
}

void DrawCharacter() {
    float sw = float(GetScreenWidth());
    float sh = float(GetScreenHeight());
    float w = min(760.0f, sw * 0.9f);
    ImGui_SetNextWindowPos(vec2((sw - w) * 0.5f, sh * 0.12f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(w, 0.0f), ImGuiSetCond_Always);
    ImGui_Begin("Your co-op character", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFontScale(sh >= 1000.0f ? 1.25f : 1.05f);

    ImGui_TextColored(kTitleColor, "Name");
    ImGui_InputText("##mp_name", name_buffer, 32);

    if(name_rejected) {
        ImGui_TextColored(vec4(1.0f, 0.45f, 0.35f, 1.0f), "Use 3 or more Latin letters, digits or spaces.");
    } else {
        ImGui_TextColored(kDimColor, "Empty: your Steam name (if it is 3+ Latin letters or digits).");
    }
    ImGui_Spacing();

    ImGui_TextColored(kTitleColor, "Character");
    bool default_pick = pick < 0;

    if(ImGui_Checkbox("The level's own character", default_pick)) {
        pick = default_pick ? -1 : 0;
    }

    ImGui_TextColored(kDimColor, "Host: the story's hero. Players who join: Turner.");

    if(pick >= 0) {
        if(ImGui_Combo("Species", species, kSpecies)) {
            pick = 0;
        }

        int now = pick;

        if(ImGui_ListBox("##mp_character", now, CharacterNames(species), 8)) {
            pick = now;
        }

        if(species == 4) {
            ImGui_TextColored(kDimColor, "Wolves fight without weapons.");
        }
    }

    ImGui_Spacing();
    ImGui_TextColored(kTitleColor, "Colours");
    ImGui_Checkbox("My own fur colour", own_fur);

    if(own_fur) {
        ImGui_SameLine();
        ImGui_ColorEdit3("##mp_fur", fur_color);
    }

    ImGui_Checkbox("My own clothes colour", own_cloth);

    if(own_cloth) {
        ImGui_SameLine();
        ImGui_ColorEdit3("##mp_cloth", cloth_color);
    }

    ImGui_TextColored(kDimColor, "Otherwise the character's own (or random) colours.");
    ImGui_Spacing();
    ImGui_Separator();

    if(ImGui_Button("Save", vec2(160.0f, 36.0f))) {
        name_rejected = name_buffer != "" && !Online_IsValidPlayerName(name_buffer);

        if(!name_rejected) {
            Save();
            this_ui.SendCallback("back");
        }
    }

    ImGui_SameLine();

    if(ImGui_Button("Cancel", vec2(160.0f, 36.0f))) {
        this_ui.SendCallback("back");
    }

    ImGui_End();
}
