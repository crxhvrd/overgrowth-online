//-----------------------------------------------------------------------------
//           Name: overhaul_tab_general.as
//    Description: "General" tab of the Overhaul window: how the window
//                 behaves, and what the mod contains.
//-----------------------------------------------------------------------------

const array<string> kMenuKeys = {"f9", "f8", "f10", "f11", "f12"};

void DrawGeneralTab() {
    Header("This window");
    Toggle("Pause the game while it is open", "aio_menu_pause", true, "");

    array<string> labels;
    int current = 0;

    for(uint i = 0; i < kMenuKeys.length(); ++i) {
        labels.insertLast(ToUpper(kMenuKeys[i]) + (i == 0 ? " (default)" : ""));

        if(kMenuKeys[i] == MenuKey()) {
            current = int(i);
        }
    }

    if(ImGui_Combo("Key that opens it", current, labels)) {
        SetConfigValueString("aio_menu_key", kMenuKeys[current]);
    }

    // The host's arena matches (online_arena.as in the Overgrowth Online build reads the key)
    if(MPHost()) {
        Header("Online arena (you host)");
        array<string> bot_labels = {"Off", "Up to 2 fighters", "Up to 3 fighters", "Up to 4 fighters", "Up to 6 fighters", "Up to 8 fighters"};
        array<int> bot_steps = {0, 2, 3, 4, 6, 8};
        int at = max(0, bot_steps.find(CfgInt("oo_arena_bots", 2)));

        if(ImGui_Combo("Arena bots", at, bot_labels)) {
            SetConfigValueInt("oo_arena_bots", bot_steps[at]);
        }

        Tip("AI fighters fill your arena matches up to this many fighters. A player who joins takes a bot's place. " +
            "Changes apply from the next round.");

        Toggle("Randomizer", "oo_arena_random", false, "Every round each fighter, players and bots, becomes a random species " +
            "and look with random weapons. Off again: the players get their own characters back. From the next round.");
    }

    Header("About Overgrowth Online " + kVersion);
    ImGui_TextWrapped("Overgrowth Online grows into a whole ecosystem: online play and improvements to the whole game. Each part has its own tab here.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Overgrowth Online AI");
    ImGui_TextWrapped("Smarter enemies built around the jump kick: air combat, movement, weapons, group and team tactics. " +
        "Settings in the AI tab.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Large Battles");
    ImGui_TextWrapped("A game mode for big fights: up to 8 teams on any map, each with its own size, species, AI, " +
        "skill and weapons. Play menu > Large Battles.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "GUI");
    ImGui_TextWrapped("Health panel, minimap, campaign objectives (the next checkpoint or the enemies to defeat), names " +
        "and health above nearby characters, damage numbers, a combat log and arrows for attackers out of view.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Camera");
    ImGui_TextWrapped("Tune the third-person camera, a dynamic cinematic camera, first person (V by default) and " +
        "finisher shots with slow motion.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Weather");
    ImGui_TextWrapped("Any time of day, with the sun moving or still and a moon at night, and clouds, rain, storms, " +
        "snow or a blizzard. Online the host's.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Trainer");
    ImGui_TextWrapped("God mode, strength and speed, any character as your skin, weapons, teleport, enemies, spawning, " +
        "game speed and gravity. Reset Trainer turns it all off.");
    ImGui_Spacing();
    ImGui_TextColored(kHeaderColor, "Background fix");
    ImGui_TextWrapped("Optional, installed in the game folder (GLU32.dll): caps the frame rate while Overgrowth is " +
        "in the background or minimized, so it cannot freeze the PC. Settings in OverhaulFix.ini there.");
}

void ResetGeneralTab() {
    SetConfigValueBool("aio_menu_pause", true);
    SetConfigValueString("aio_menu_key", "f9");
}
