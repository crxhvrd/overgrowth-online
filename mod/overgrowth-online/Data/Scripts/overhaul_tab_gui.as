//-----------------------------------------------------------------------------
//           Name: overhaul_tab_gui.as
//    Description: "GUI" tab of the Overhaul window: the HUD drawn by
//                 overhaul_hud.as (ohud_* config keys).
//-----------------------------------------------------------------------------

void HudSlider(const string &in label, const string &in key, float fallback, float lo, float hi, const string &in format,
        const string &in tip) {
    float value = CfgFloat(key, fallback);

    if(ImGui_SliderFloat(label, value, lo, hi, format)) {
        SetConfigValueFloat(key, value);
        hud_settings_next = 0.0f;
    }

    Tip(tip);
}

void DrawGUITab() {
    Toggle("Show the Overgrowth Online HUD", "ohud_enabled", true, "Everything below. Hidden in the editor and during cutscenes.");

    Header("You");
    Toggle("Health panel", "ohud_health", true,
        "Bottom left: health (the dark part won't heal back), bleeding, block strength, weapon in hand, knockout shield.");

    Header("Objectives");
    Toggle("Campaign objectives", "ohud_objectives", true,
        "Top left: what the level wants next. A gold marker on the checkpoint to reach, gold pointers over the " +
        "enemies to defeat (dimmer: optional ones), an arrow at the edge when they are out of view, and both on " +
        "the minimap. Story levels and Lugaru levels, alone or online.");

    Header("Minimap");
    Toggle("Minimap", "ohud_minimap", true,
        "Top right radar: enemies red, allies green (team colours in Large Battles), knocked out grey, weapons lying " +
        "about gold, you the white arrow.");
    Toggle("Turn with the camera", "ohud_map_rotate", true, "Off: fixed, world forward is up.");
    Toggle("Show weapons lying about", "ohud_map_items", true, "");
    HudSlider("Range", "ohud_map_range", 40.0f, 15.0f, 100.0f, "%.0f m", "How far the minimap reaches.");
    HudSlider("Size", "ohud_map_size", 170.0f, 100.0f, 320.0f, "%.0f px", "");

    Header("Names and health above characters");
    Toggle("Name tags", "ohud_tags", true,
        "Health above characters close enough and in plain sight, with an icon for anyone but a player (players have " +
        "their nicknames): a red diamond for an enemy, a green shield for a teammate (team colours in Large Battles). " +
        "Blue pips under the bar: its knockout shield, the hits it takes before it can be knocked out.");
    Toggle("On allies too", "ohud_tag_allies", true, "");
    Toggle("Show fighting style", "ohud_tag_style", false, "The Overgrowth Online AI personality: kicker, boxer, trickster...");
    HudSlider("Tag range", "ohud_tag_range", 14.0f, 4.0f, 40.0f, "%.0f m", "");

    Header("Damage and the combat log");
    Toggle("Damage numbers", "ohud_dmg", true, "Numbers rising above an enemy you hit; KO when you knock it out.");
    Toggle("For everyone's hits", "ohud_dmg_all", false, "Numbers for every hit, not just yours (good for spectating).");
    Toggle("Combat log", "ohud_feed", true, "Top right: who knocked out, killed or revived whom, who fell. Players by " +
        "name, anyone else by icon and species; lines with a player in them are framed.");
    Toggle("Only players' events", "ohud_feed_players", false, "Leave out lines without a player (big battles).");

    Header("Warnings");
    Toggle("Attackers out of view", "ohud_threats", true,
        "Red arrows at the screen edge for enemies attacking you or closing in from where you can't see them.");

    Header("Look");
    HudSlider("Scale", "ohud_scale", 1.0f, 0.6f, 2.0f, "%.1fx", "Size of bars, tags and arrows.");
    HudSlider("Opacity", "ohud_opacity", 0.9f, 0.2f, 1.0f, "%.2f", "");
}

void ResetGUITab() {
    SetConfigValueBool("ohud_enabled", true);
    SetConfigValueBool("ohud_health", true);
    SetConfigValueBool("ohud_minimap", true);
    SetConfigValueBool("ohud_map_rotate", true);
    SetConfigValueBool("ohud_map_items", true);
    SetConfigValueBool("ohud_tags", true);
    SetConfigValueBool("ohud_tag_allies", true);
    SetConfigValueBool("ohud_tag_style", false);
    SetConfigValueBool("ohud_threats", true);
    SetConfigValueFloat("ohud_map_range", 40.0f);
    SetConfigValueFloat("ohud_map_size", 170.0f);
    SetConfigValueFloat("ohud_tag_range", 14.0f);
    SetConfigValueFloat("ohud_scale", 1.0f);
    SetConfigValueFloat("ohud_opacity", 0.9f);
    SetConfigValueBool("ohud_dmg", true);
    SetConfigValueBool("ohud_dmg_all", false);
    SetConfigValueBool("ohud_feed", true);
    SetConfigValueBool("ohud_feed_players", false);
    SetConfigValueBool("ohud_objectives", true);
    hud_settings_next = 0.0f;
}
