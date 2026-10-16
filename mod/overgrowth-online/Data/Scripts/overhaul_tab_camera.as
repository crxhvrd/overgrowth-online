//-----------------------------------------------------------------------------
//           Name: overhaul_tab_camera.as
//    Description: "Camera" tab of the Overhaul window: the third-person camera
//                 in aschar.as ApplyCameraControls (ocam_* config keys, read
//                 there every few frames, also while paused). Field of view,
//                 mouse sensitivity and invert are the game's own keys.
//-----------------------------------------------------------------------------

void CamSlider(const string &in label, const string &in key, float fallback, float lo, float hi, const string &in format,
        const string &in tip) {
    float value = CfgFloat(key, fallback);

    if(ImGui_SliderFloat(label, value, lo, hi, format)) {
        SetConfigValueFloat(key, value);
    }

    Tip(tip);
}

// The game's own options: written the way its settings menu does
void CamEngineToggle(const string &in label, const string &in key, const string &in tip) {
    bool value = GetConfigValueBool(key);

    if(ImGui_Checkbox(label, value)) {
        SetConfigValueBool(key, value);
        ReloadStaticValues();
    }

    Tip(tip);
}

class CamPreset {
    string name;
    string tip;
    array<string> keys;
    array<float> values;
}

array<CamPreset@> cam_presets;

void AddCamPreset(const string &in name, const string &in tip, const string &in values) {
    CamPreset@ preset = CamPreset();
    preset.name = name;
    preset.tip = tip;
    array<string>@ pairs = values.split(",");

    for(uint i = 0; i < pairs.length(); ++i) {
        array<string>@ kv = pairs[i].split("=");

        if(kv.length() != 2) {
            continue;  // "Stock" has no values
        }

        preset.keys.insertLast(kv[0]);
        preset.values.insertLast(atof(kv[1]));
    }

    cam_presets.insertLast(preset);
}

void SetUpCamPresets() {
    if(cam_presets.length() > 0) {
        return;
    }

    AddCamPreset("Stock", "The game's own camera", "");
    AddCamPreset("Close", "Tight over the right shoulder",
        "ocam_distance=1.5,ocam_height=0.05,ocam_shoulder=0.35,chase_camera_fov=80");
    AddCamPreset("Action", "Pulls back with speed and in fights, wider at speed",
        "ocam_distance=2.4,ocam_speed_pull=0.6,ocam_fight_pull=0.6,ocam_speed_fov=10,chase_camera_fov=95");
    AddCamPreset("Cinematic", "Far, low field of view, slow and smooth, swings behind the run",
        "ocam_distance=3.2,ocam_height=0.15,ocam_shoulder=0.3,ocam_rot_smooth=0.85,ocam_follow_smooth=0.9," +
        "ocam_swing=0.5,ocam_shake=0.5,chase_camera_fov=70");
    AddCamPreset("Tactical", "High and far: the whole fight in view",
        "ocam_distance=4.5,ocam_height=0.8,ocam_fight_pull=0.8,ocam_lock=0.4,chase_camera_fov=90");
}

void ApplyCamPreset(CamPreset@ preset) {
    ResetCameraTab();

    for(uint i = 0; i < preset.keys.length(); ++i) {
        SetConfigValueFloat(preset.keys[i], preset.values[i]);
    }
}

void DrawCameraTab() {
    SetUpCamPresets();
    Toggle("Overgrowth Online camera", "ocam_enabled", true,
        "Off: the stock camera whatever is set below (field of view and mouse settings stay, they are the game's own).");
    Note("Changes show live behind this window.");

    Header("Presets");

    for(uint i = 0; i < cam_presets.length(); ++i) {
        if(i > 0) {
            ImGui_SameLine();
        }

        if(ImGui_Button(cam_presets[i].name)) {
            ApplyCamPreset(cam_presets[i]);
        }

        Tip(cam_presets[i].tip);
    }

    Header("Framing");
    CamSlider("Distance", "ocam_distance", 2.0f, 0.8f, 8.0f, "%.1f m", "How far behind you. Stock 2.0 m.");
    CamSlider("Height", "ocam_height", 0.0f, -0.3f, 1.5f, "%+.2f m", "Raise or lower the point the camera looks at. Stock 0.");
    CamSlider("Shoulder", "ocam_shoulder", 0.0f, -1.0f, 1.0f, "%+.2f m",
        "Over the shoulder: positive right, negative left. Stays clear of walls. Stock 0 (centred).");
    CamSlider("Field of view", "chase_camera_fov", 90.0f, 40.0f, 140.0f, "%.0f deg",
        "The game's own field of view setting (horizontal). Stock 90.");
    CamSlider("Throw aiming distance", "ocam_aim_dist", 0.25f, 0.1f, 1.0f, "%.2fx",
        "How close the camera comes while you aim a throw. Stock 0.25x.");

    Header("Dynamic");
    CamSlider("Pull back at speed", "ocam_speed_pull", 0.0f, 0.0f, 1.0f, "%.2f",
        "Running, jumping and flying: the camera falls back (up to +60% at 1.0). Stock 0.");
    CamSlider("Pull back in fights", "ocam_fight_pull", 0.0f, 0.0f, 1.0f, "%.2f",
        "More enemies within 7 m, more distance (up to +50% with three at 1.0). Stock 0.");
    CamSlider("Wider view at speed", "ocam_speed_fov", 0.0f, 0.0f, 25.0f, "+%.0f deg",
        "Field of view grows with speed, for a sense of pace. Stock 0.");

    Header("Feel");
    CamSlider("Turn smoothing", "ocam_rot_smooth", 0.5f, 0.0f, 0.95f, "%.2f",
        "0 = turns exactly with the mouse; higher = heavier, smoother turns. Stock 0.50.");
    CamSlider("Follow smoothing", "ocam_follow_smooth", 0.8f, 0.0f, 0.95f, "%.2f",
        "How much the camera trails your movement. Stock 0.80.");
    CamSlider("Zoom-out smoothing", "ocam_zoom_smooth", 0.95f, 0.8f, 0.995f, "%.3f",
        "How slowly it eases back out after a wall pushed it in. Stock 0.950.");
    CamSlider("Shake on hits", "ocam_shake", 1.0f, 0.0f, 2.0f, "%.1fx", "0 turns the shake off. Stock 1x.");

    Header("Look");
    CamSlider("Mouse sensitivity", "mouse_sensitivity", 0.5f, 0.05f, 2.5f, "%.2f", "The game's own setting.");
    CamSlider("Vertical speed", "ocam_look_y", 1.0f, 0.3f, 2.0f, "%.2fx", "Up/down look speed relative to left/right. Stock 1x.");
    CamEngineToggle("Invert vertical", "invert_y_mouse_look", "The game's own setting.");
    CamEngineToggle("Invert horizontal", "invert_x_mouse_look", "The game's own setting.");
    CamSlider("Look up limit", "ocam_pitch_max", 50.0f, 0.0f, 85.0f, "%.0f deg", "Stock 50.");
    CamSlider("Look down limit", "ocam_pitch_min", -90.0f, -90.0f, -10.0f, "%.0f deg", "Stock -90 (straight down).");

    Header("Assist");
    CamSlider("Face the fight", "ocam_lock", 0.0f, 0.0f, 1.0f, "%.2f",
        "Once you leave the mouse alone for a moment, the camera turns toward the nearest enemy (within 7 m) " +
        "if it is more than 20 degrees off. Stock 0.");
    CamSlider("Swing behind when running", "ocam_swing", 0.0f, 0.0f, 1.0f, "%.2f",
        "Once you leave the mouse alone, the camera drifts behind the way you run. Stock 0.");
    Note("Both stay off while the game's own auto camera (game settings) is on.");

    Header("First person");
    Toggle("First person", "ocam_fp", false,
        "Through your character's eyes. Back to third person while you're knocked down, and for finisher shots.");
    int fp_key = CfgInt("ocam_fp_key", 1);

    if(ImGui_Combo("First-person key", fp_key, kFirstPersonKeys)) {
        SetConfigValueInt("ocam_fp_key", fp_key);
    }

    Tip("Switches first person on and off in play. V is also the game's debug scream key (debug keys only).");
    CamSlider("First-person field of view", "ocam_fp_fov", 95.0f, 60.0f, 120.0f, "%.0f deg", "");
    CamSlider("Head motion", "ocam_fp_bob", 0.5f, 0.0f, 1.0f, "%.2f",
        "0 = a steady eye; 1 = every movement of the head (runs, rolls, hits).");

    Header("Dynamic camera");
    Toggle("Dynamic camera", "ocam_dynamic", false,
        "A heavy, operated camera: it eases and settles instead of snapping, leaves room ahead of you when you move " +
        "across the screen, banks softly into turns, sinks on landings, breathes, and takes hits with a weighty, " +
        "low shake. Standing still a while, the lens slowly pushes in.");
    CamSlider("Weight", "ocam_dyn_stiff", 1.0f, 0.4f, 2.5f, "%.2fx", "Lower = heavier and slower to settle; higher = tighter.");
    CamSlider("Lead room", "ocam_dyn_lead", 1.0f, 0.0f, 2.5f, "%.2fx", "Space left ahead of you when you move across the screen.");
    CamSlider("Give", "ocam_dyn_sway", 1.0f, 0.0f, 2.5f, "%.2fx", "How much speeding up and stopping move the camera.");
    CamSlider("Lean into turns", "ocam_dyn_lean", 1.0f, 0.0f, 2.5f, "%.2fx", "A soft bank when you turn or swing the mouse.");
    CamSlider("Landing", "ocam_dyn_land", 1.0f, 0.0f, 2.5f, "%.2fx", "The sink and nod when you land, by how hard.");
    CamSlider("Stride float", "ocam_dyn_bob", 0.5f, 0.0f, 2.5f, "%.2fx", "A faint rise and fall with your steps.");
    CamSlider("Breathing", "ocam_dyn_hand", 0.6f, 0.0f, 2.0f, "%.2fx", "A slow drift, like a camera on an operator's shoulder.");
    CamSlider("Shake", "ocam_dyn_shake", 1.0f, 0.0f, 2.5f, "%.2fx", "Hits taken and dealt, hard landings: a weighty jolt and a low shake.");
    CamSlider("Idle push-in", "ocam_dyn_push", 1.0f, 0.0f, 2.5f, "%.2fx", "Standing still, the lens slowly tightens (4 degrees at 1x).");

    Header("Finisher camera");
    int fin_mode = CfgInt("ofin_mode", 2);

    if(ImGui_Combo("Finisher shots", fin_mode, kFinisherModes)) {
        SetConfigValueInt("ofin_mode", fin_mode);
    }

    Tip("A short cinematic shot when you knock an enemy out or kill it. Last enemy only: when the game's own slow " +
        "motion plays. Sometimes: that, plus a chance on other knockouts. Never while another enemy is close to you.");
    CamSlider("How often", "ofin_chance", 0.35f, 0.05f, 1.0f, "%.2f", "Sometimes: the chance per knockout (the last enemy always).");
    CamSlider("Shot length", "ofin_length", 2.2f, 1.0f, 4.0f, "%.1f s",
        "Real seconds. Two-shot sequences (last enemy, flying bodies) get 1.25x, your own fall 1.4x.");
    CamSlider("Slow motion", "ofin_slowmo_amt", 0.85f, 0.0f, 1.0f, "%.2f",
        "Strength of the speed ramp: a near-freeze at the impact, deep slow motion, then easing back. 0 = none.");
    Toggle("Cinematic look", "ofin_grade", true, "Focus on the victim, a heavier vignette, a warm grade (cold when you fall), " +
        "the impact flash.");
    Toggle("Letterbox", "ofin_bars", true, "Black bars and the HUD hidden while a shot plays (damage numbers stay).");
    Toggle("When you go down", "ofin_death", true, "The camera rises and circles over you when you're knocked out or killed.");
    Note("Move the mouse, attack or jump to skip a shot.");

    Header("Collision");
    CamSlider("Wall clearance", "ocam_radius", 0.15f, 0.05f, 0.4f, "%.2f m",
        "How far the camera keeps from walls and ceilings. Stock 0.15 m.");
}

// Back to the stock camera; mouse sensitivity and invert are left alone
void ResetCameraTab() {
    SetConfigValueBool("ocam_enabled", true);
    SetConfigValueFloat("ocam_distance", 2.0f);
    SetConfigValueFloat("ocam_height", 0.0f);
    SetConfigValueFloat("ocam_shoulder", 0.0f);
    SetConfigValueFloat("chase_camera_fov", 90.0f);
    SetConfigValueFloat("ocam_aim_dist", 0.25f);
    SetConfigValueFloat("ocam_speed_pull", 0.0f);
    SetConfigValueFloat("ocam_fight_pull", 0.0f);
    SetConfigValueFloat("ocam_speed_fov", 0.0f);
    SetConfigValueFloat("ocam_rot_smooth", 0.5f);
    SetConfigValueFloat("ocam_follow_smooth", 0.8f);
    SetConfigValueFloat("ocam_zoom_smooth", 0.95f);
    SetConfigValueFloat("ocam_shake", 1.0f);
    SetConfigValueFloat("ocam_look_y", 1.0f);
    SetConfigValueFloat("ocam_pitch_max", 50.0f);
    SetConfigValueFloat("ocam_pitch_min", -90.0f);
    SetConfigValueFloat("ocam_lock", 0.0f);
    SetConfigValueFloat("ocam_swing", 0.0f);
    SetConfigValueFloat("ocam_radius", 0.15f);
    SetConfigValueBool("ocam_fp", false);
    SetConfigValueInt("ocam_fp_key", 1);
    SetConfigValueFloat("ocam_fp_fov", 95.0f);
    SetConfigValueFloat("ocam_fp_bob", 0.5f);
    SetConfigValueBool("ocam_dynamic", false);
    SetConfigValueFloat("ocam_dyn_stiff", 1.0f);
    SetConfigValueFloat("ocam_dyn_sway", 1.0f);
    SetConfigValueFloat("ocam_dyn_lean", 1.0f);
    SetConfigValueFloat("ocam_dyn_land", 1.0f);
    SetConfigValueFloat("ocam_dyn_bob", 0.5f);
    SetConfigValueFloat("ocam_dyn_hand", 0.6f);
    SetConfigValueFloat("ocam_dyn_shake", 1.0f);
    SetConfigValueFloat("ocam_dyn_lead", 1.0f);
    SetConfigValueFloat("ocam_dyn_push", 1.0f);
    SetConfigValueInt("ofin_mode", 2);
    SetConfigValueFloat("ofin_chance", 0.35f);
    SetConfigValueFloat("ofin_length", 2.2f);
    SetConfigValueFloat("ofin_slowmo_amt", 0.85f);
    SetConfigValueBool("ofin_grade", true);
    SetConfigValueBool("ofin_bars", true);
    SetConfigValueBool("ofin_death", true);
}
