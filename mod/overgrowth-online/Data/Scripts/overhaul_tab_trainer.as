//-----------------------------------------------------------------------------
//           Name: overhaul_tab_trainer.as
//    Description: "Trainer" tab of the Overhaul window (otr_* config keys;
//                 game speed is the engine's global_time_scale_mult).
//                 Runtime: overhaul_trainer.as and aschar.as OtrTick.
//-----------------------------------------------------------------------------

void TrSlider(const string &in label, const string &in key, float fallback, float lo, float hi, const string &in format,
        const string &in tip) {
    float value = CfgFloat(key, fallback);

    if(ImGui_SliderFloat(label, value, lo, hi, format)) {
        SetConfigValueFloat(key, value);
        trainer_countdown = 0;
    }

    Tip(tip);
}

void TrCombo(const string &in label, const string &in key, const array<string> &in names) {
    int value = CfgInt(key, 0);

    if(ImGui_Combo(label, value, names)) {
        SetConfigValueInt(key, value);
    }
}

int trainer_weapon_pick = 0;

void DrawTrainerTab() {
    if(trainer_status != "") {
        ImGui_TextColored(vec4(0.6f, 0.85f, 1.0f, 1.0f), trainer_status);
    } else {
        Note("Options stay on until you turn them off (Reset Trainer turns everything off).");
    }

    if(MPHost()) {
        Header("Online");
        Toggle("Let players use the Trainer", "otr_mp_allow", false,
            "Their own options: god mode, strength, speed, teleport, skin. The world (gravity, game speed, enemies, spawning, weapons) stays yours.");
    } else if(MPClient()) {
        Header("Online");
        Note(trainer_net_allowed ? "The host lets you use your own options. The world is the host's." :
            "The host hasn't allowed the Trainer for players. The world is the host's.");
    }

    if(TrainerPersonalAllowed()) {
        DrawTrainerYou();
        DrawTrainerSkin();
        DrawTrainerStrength();
    }

    if(TrainerWorldAllowed()) {
        DrawTrainerWeapons();  // a new item: the world's
    }

    if(TrainerPersonalAllowed()) {
        DrawTrainerTeleport();
    }

    if(TrainerWorldAllowed()) {
        DrawTrainerEnemies();
        DrawTrainerSpawn();
        DrawTrainerWorld();
    }

    DrawTrainerReminder();
}

void DrawTrainerYou() {
    Header("You");
    Toggle("God mode", "otr_god", false, "No damage at all: hits, cuts, falls, fire.");
    Toggle("No bleeding", "otr_no_bleed", false, "Cuts never bleed you out.");
    Toggle("Unbreakable block", "otr_block", false, "Your block never wears down.");
    Toggle("No fall damage", "otr_no_fall", false, "");
    Toggle("Can't be disarmed", "otr_no_disarm", false, "");
    TrSlider("Health regen", "otr_regen", 0.0f, 0.0f, 0.5f, "%.2f / s", "Health back per second, up to full. 0 = off.");
    TrSlider("Damage taken", "otr_damage_taken", 1.0f, 0.0f, 3.0f, "%.2fx", "0 = none. 1x = normal.");

    if(ImGui_Button("Heal me")) {
        TrainerHealMe();
    }

    Tip("Full health, no bleeding, back on your feet, fire out.");
}

void DrawTrainerSkin() {
    Header("Skin");
    TrCombo("Species##skin", "otr_skin_species", kSkinSpecies);
    int skin_species = max(0, min(int(kSkinSpecies.length()) - 1, CfgInt("otr_skin_species", 0)));
    int skin = max(0, min(int(kSkins[skin_species].length()) - 1, CfgInt("otr_skin_index", 0)));

    if(ImGui_Combo("Skin", skin, SkinNames(skin_species))) {
        SetConfigValueInt("otr_skin_index", skin);
    }

    if(ImGui_Button("Become this")) {
        string path = SkinPath(skin_species, skin);
        SetConfigValueString("otr_skin_path", path);
        SetConfigValueString("otr_skin_colors", "");
        TrainerApplySkin(path);
    }

    Tip("Your weapons drop. Wolves fight without weapons; each species keeps its own moves.");
    ImGui_SameLine();
    MovementObject@ skin_player = TrainerPlayer();

    if(ImGui_Button("Random colours") && skin_player !is null) {
        skin_player.Execute("RandomizeColors();");
        TrainerSaveColors(ReadObjectFromID(skin_player.GetID()));
    }

    if(skin_player !is null) {
        TrainerReadPalette(skin_player);
        Object@ skin_obj = ReadObjectFromID(skin_player.GetID());
        array<string> kind_names = {"", "Fur", "Cloth", "Detail"};

        for(int i = 0, len = min(4, skin_obj.GetNumPaletteColors()); i < len; ++i) {
            int kind = trainer_palette_kinds[i];

            if(kind <= 0) {
                continue;
            }

            vec3 color = skin_obj.GetPaletteColor(i);

            if(ImGui_ColorEdit3(kind_names[kind] + " " + (i + 1), color)) {
                skin_obj.SetPaletteColor(i, color);
                TrainerSaveColors(skin_obj);
            }
        }
    }

    Toggle("Keep in every level", "otr_skin_keep", false,
        "Every level you load starts you as this skin, in these colours (not in Large Battles: teams have their colours).");
}

void DrawTrainerStrength() {
    Header("Strength and speed");
    TrSlider("Attack damage", "otr_attack_damage", 1.0f, 0.5f, 10.0f, "%.1fx", "10x knocks most enemies out in one hit.");
    TrSlider("Attack speed", "otr_attack_speed", 1.0f, 0.5f, 2.0f, "%.2fx", "");
    TrSlider("Knockback", "otr_knockback", 1.0f, 0.0f, 5.0f, "%.1fx", "How far your hits send them flying.");
    TrSlider("Movement speed", "otr_speed", 1.0f, 0.5f, 3.0f, "%.2fx", "");
    TrSlider("Jump power", "otr_jump", 1.0f, 0.5f, 3.0f, "%.2fx",
        "Take-off speed (height grows with its square). Pair with No fall damage.");
}

void DrawTrainerWeapons() {
    Header("Weapons");
    ImGui_Combo("##trainer_weapon", trainer_weapon_pick, kTrainerWeaponNames);
    ImGui_SameLine();

    if(ImGui_Button("Give")) {
        TrainerGiveWeapon(trainer_weapon_pick);
    }

    Tip("Into your hand if it's empty, else at your feet.");
}

void DrawTrainerTeleport() {
    Header("Teleport");

    if(ImGui_Button("Teleport where the camera points")) {
        TrainerTeleportLook();
    }

    Tip("Onto the surface at the centre of the screen, up to 300 m away.");

    for(int slot = 0; slot < 3; ++slot) {
        if(ImGui_Button("Save " + (slot + 1))) {
            TrainerSavePos(slot);
        }

        ImGui_SameLine();

        if(ImGui_Button("Load " + (slot + 1))) {
            TrainerLoadPos(slot);
        }

        if(slot < 2) {
            ImGui_SameLine();
            ImGui_Text("  ");
            ImGui_SameLine();
        }
    }
}

void DrawTrainerEnemies() {
    Header("Enemies");
    Toggle("Peaceful", "otr_peaceful", false, "Enemies don't attack (they may still follow you around).");

    if(ImGui_Button("Knock out all")) {
        TrainerKnockOutEnemies(false);
    }

    ImGui_SameLine();

    if(ImGui_Button("Kill all")) {
        TrainerKnockOutEnemies(true);
    }

    ImGui_SameLine();

    if(ImGui_Button("Revive and heal everyone")) {
        TrainerReviveAll();
    }

    Tip("Everyone, you and allies included.");
    Note("Enemies = everyone not on your team; while spectating a battle, everyone.");
}

void DrawTrainerSpawn() {
    Header("Spawn");
    TrCombo("Species", "otr_sp_species", kTrainerSpecies);
    TrCombo("Side", "otr_sp_side", kTrainerSides);
    TrCombo("AI", "otr_sp_ai", kTrainerAIs);
    TrCombo("Weapon", "otr_sp_weapon", kTrainerSpawnWeapons);
    int count = CfgInt("otr_sp_count", 1);

    if(ImGui_SliderInt("How many", count, 1, 10)) {
        SetConfigValueInt("otr_sp_count", count);
    }

    if(ImGui_Button("Spawn")) {
        TrainerSpawnGroup();
    }

    Tip("5 m ahead of you, side by side. Enemies come for you; allies join your team.");
    ImGui_SameLine();

    if(ImGui_Button("Remove spawned")) {
        TrainerRemoveSpawned();
    }
}

void DrawTrainerWorld() {
    Header("World");
    TrSlider("Game speed", "global_time_scale_mult", 1.0f, 0.1f, 2.0f, "%.2fx",
        "The game's own speed setting: below 1 is slow motion. Kept until you change it back.");

    if(ImGui_Button("0.25x")) {
        SetConfigValueFloat("global_time_scale_mult", 0.25f);
        trainer_countdown = 0;
    }

    ImGui_SameLine();

    if(ImGui_Button("0.5x")) {
        SetConfigValueFloat("global_time_scale_mult", 0.5f);
        trainer_countdown = 0;
    }

    ImGui_SameLine();

    if(ImGui_Button("Normal")) {
        SetConfigValueFloat("global_time_scale_mult", 1.0f);
        trainer_countdown = 0;
    }

    TrSlider("Gravity", "otr_gravity", 1.0f, 0.1f, 2.0f, "%.2fx",
        "Running, jumping and falling characters at once; ragdolls and dropped items follow after the next level load.");
}

void DrawTrainerReminder() {
    Header("Reminder");
    Toggle("Show what's on in the HUD", "otr_hud_line", true, "A line above your health panel while any trainer option is on.");
}

// Everything off; the spawn choices stay
void ResetTrainerTab() {
    SetConfigValueBool("otr_god", false);
    SetConfigValueBool("otr_no_bleed", false);
    SetConfigValueBool("otr_block", false);
    SetConfigValueBool("otr_no_fall", false);
    SetConfigValueBool("otr_no_disarm", false);
    SetConfigValueFloat("otr_regen", 0.0f);
    SetConfigValueFloat("otr_damage_taken", 1.0f);
    SetConfigValueFloat("otr_attack_damage", 1.0f);
    SetConfigValueFloat("otr_attack_speed", 1.0f);
    SetConfigValueFloat("otr_knockback", 1.0f);
    SetConfigValueFloat("otr_speed", 1.0f);
    SetConfigValueFloat("otr_jump", 1.0f);
    SetConfigValueBool("otr_peaceful", false);
    SetConfigValueFloat("global_time_scale_mult", 1.0f);
    SetConfigValueFloat("otr_gravity", 1.0f);
    SetConfigValueBool("otr_hud_line", true);
    trainer_countdown = 0;
}
