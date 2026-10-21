//-----------------------------------------------------------------------------
//           Name: multiplayer_menu.as
//      Developer: Wolfire Games LLC
//    Script Type:
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------

#include "save/general.as"
#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

const int item_per_screen = 4;
const int rows_per_screen = 3;

IMGUI@ imGUI;
array<LevelInfo@> play_menu = {};

FontSetup invalid_name_font("edosz", 50 , HexColor("#ff5555"), true);

bool HasFocus() {
    return false;
}

void Initialize() {
    if (Online_IsHosting()) {
        Online_Close();
    }

    @imGUI = CreateIMGUI();
    // Start playing some music
    PlaySong("overgrowth_main");
    play_menu.resize(0);

    // We're going to want a 100 'gui space' pixel header/footer
	imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);

	imGUI.setFooterPanels(200.0f, 1400.0f);
    // Actually setup the GUI -- must do this before we do anything
    imGUI.setup();
    SetPlayMenuList();
    BuildUI();
	setBackGround();
}

// Overgrowth Online: the story campaigns in co-op, the linear ones the co-op
// rules know (Online::CoopCampaign), and the Arena, player against player
// (online_arena.as). The 1.4 data marks none as online, so this doesn't ask;
// Large Battles and Versus stay out.
bool CoopCampaign(Campaign &in camp) {
    return camp.GetMainScript().findFirst("linear_campaign") >= 0 && camp.GetMenuScript().findFirst("arena") < 0 &&
        camp.GetMenuScript().findFirst("aio_battle") < 0;  // (Versus runs on linear_campaign too, with an arena menu)
}

// Arena bots (online_arena.as reads the same key): fill up to this many
// fighters, 0 none; 2 if never set
int ArenaFighters() {
    return ConfigHasKey("oo_arena_bots") ? GetConfigValueInt("oo_arena_bots") : 2;
}

string ArenaBotsLabel() {
    int fighters = ArenaFighters();
    return fighters <= 0 ? "Bots: off" : "Bots: up to " + fighters;
}

// Your games: public (the server browser lists them; read by the engine
// as it opens the lobby) or friends only. Public unless set off
bool HostingPublic() {
    return !ConfigHasKey("oo_public_game") || GetConfigValueBool("oo_public_game");
}

string HostingLabel() {
    return HostingPublic() ? "Hosting: public" : "Hosting: friends";
}

bool ArenaRandom() {
    return ConfigHasKey("oo_arena_random") && GetConfigValueBool("oo_arena_random");
}

string ArenaRandomLabel() {
    return ArenaRandom() ? "Randomizer: on" : "Randomizer: off";
}

bool ArenaCampaign(Campaign &in camp) {
    return camp.GetID() == "com-wolfire-arena";
}

void SetPlayMenuList() {
    array<Campaign>@ campaigns = GetCampaigns();
    for( uint i = 0; i < campaigns.length(); i++ ) {
        Campaign camp = campaigns[i];
        if (CoopCampaign(camp) || ArenaCampaign(camp)) {
            string camp_thumbnail_path = camp.GetThumbnail();
            play_menu.insertLast(
                LevelInfo(  camp.GetMenuScript(),
                    ArenaCampaign(camp) ? camp.GetTitle() + " (PvP)" : camp.GetTitle(),
                    camp_thumbnail_path,
                    camp.GetID(),
                    0,
                    GetLastLevelPlayed(camp.GetID()).length() > 0,
                    true,
                    IsLastCampaignPlayed(camp),
                    -1,
                    -1
                )
            );
        }
    }
}

TextBox text_box_ip;
TextBox text_box_playername;

// The column's buttons (wide enough for "Randomizer: off"), and the campaign
// cards beside it, three to a row
const float kButtonWidth = 660.0f;
const uint kCardsPerRow = 3;
const float kCardWidth = 480.0f;
const float kCardHeight = 360.0f;
FontSetup section_font("edosz", 48, HexColor("#9A9A9A"), true);

// How many public games are open now, beside "Browse games" (the browser's
// list: Online_PublicGames; asked again every 20 s, read every second)
FontSetup count_font("edosz", 40, HexColor("#9A9A9A"), true);
IMText@ public_count_text;
float public_ask_timer = 0.0f;
float public_read_timer = 0.0f;

string PublicCountLabel() {
    array<string>@ lines = Online_PublicGames().split("
");
    string state = lines.length() > 0 ? lines[0] : "failed";
    int open = 0;
    for(uint i = 1; i < lines.length(); ++i) {
        if(lines[i].split("	").length() >= 7) {
            ++open;
        }
    }
    if(open > 0) {
        return open + " open";
    }
    return state == "busy" ? "looking..." : state == "ready" ? "none open" : "";  // (failed, or no Steam: nothing)
}

void UpdatePublicCount() {
    public_ask_timer -= time_step;
    if(public_ask_timer <= 0.0f) {
        public_ask_timer = 20.0f;
        Online_RequestPublicGames();
    }
    public_read_timer -= time_step;
    if(public_read_timer <= 0.0f && public_count_text !is null) {
        public_read_timer = 1.0f;
        public_count_text.setText(PublicCountLabel());
    }
}

void AddWideButton(const string &in text, IMDivider@ parent, const string &in icon) {
    AddButton(text, parent, icon, button_background_diamond, true, kButtonWidth);
}

// A group's name above its buttons
void AddSection(IMDivider@ parent, const string &in title) {
    parent.append(IMSpacer(DOVertical, 18));
    IMDivider row(DOHorizontal);
    row.append(IMSpacer(DOHorizontal, 20));
    row.append(IMText(title, section_font));
    parent.append(row);
    parent.append(IMSpacer(DOVertical, 10));
}

void BuildUI(){
    // The title, as the Play menu's
    IMDivider header_divider("header_div", DOHorizontal);
    header_divider.setAlignment(CACenter, CACenter);
    AddTitleHeader("Multiplayer", header_divider);
    imGUI.getHeader().setElement(header_divider);

    IMDivider mainDiv("mainDiv", DOHorizontal);
    mainDiv.setAlignment(CACenter, CATop);

    // Left: what you can do, in groups, every button the same width
    IMDivider left_panel("left_panel", DOVertical);
    left_panel.setAlignment(CALeft, CATop);
    mainDiv.append(left_panel);
    mainDiv.append(IMSpacer(DOHorizontal, 40));

    AddSection(left_panel, "Play online");
    IMDivider browse_row("browse_row", DOHorizontal);             // public games anywhere (multiplayer_browser.as),
    browse_row.setAlignment(CALeft, CACenter);                    // and how many are open
    AddWideButton("Browse games", browse_row, play_icon);
    browse_row.append(IMSpacer(DOHorizontal, 20));
    @public_count_text = IMText(PublicCountLabel(), count_font);
    browse_row.append(public_count_text);
    left_panel.append(browse_row);
    AddWideButton("Join a friend", left_panel, play_icon);        // Steam friends hosting (multiplayer_join_friend.as)
    AddWideButton("Your character", left_panel, settings_icon);   // character, colours and name: their own screen

    // The games you host: listed in the browser or for friends only; the
    // arena's bots (a player who joins takes one's place) and randomizer
    AddSection(left_panel, "Hosting");
    AddWideButton(HostingLabel(), left_panel, settings_icon);
    AddWideButton(ArenaBotsLabel(), left_panel, settings_icon);
    AddWideButton(ArenaRandomLabel(), left_panel, settings_icon);

    AddSection(left_panel, "Direct connection");
    text_box_ip.on_enter_message = "Connect";
    text_box_ip.on_value_changed_message = "update_last_ip";
    text_box_ip.input_text = GetConfigValueString("last_ip");
    text_box_ip.default_text = "Enter IP...";
    IMDivider ip_row("ip_row", DOHorizontal);
    ip_row.setAlignment(CALeft, CACenter);
    AddTextInput(ip_row, text_box_ip, "activate_ip_text_input");
    AddButton("Connect", ip_row, "null", button_background_diamond, true, 380.0f);
    left_panel.append(ip_row);

    // Right: the campaigns to host, in rows (no page arrows: they all fit)
    IMDivider cards("cards", DOVertical);
    cards.setAlignment(CALeft, CATop);
    IMDivider@ row = null;
    for(uint i = 0; i < play_menu.length(); ++i) {
        if(i % kCardsPerRow == 0) {
            IMDivider new_row("cards_row" + i, DOHorizontal);
            @row = @new_row;
            cards.append(new_row);
        }
        CreateMenuItem(row, play_menu[i], true, play_menu[i].last_played, int(i) + 1, kCardWidth, kCardHeight, false, false, false, true);
    }
    mainDiv.append(cards);
    imGUI.getMain().setElement( @mainDiv );

    AddBackButton(true, true);
}

void AddCustomLevelsMenuItem() {
	if(NrCustomLevels() != 0) {
        LevelInfo li("custom_levels.as", "Custom Levels", "Textures/ui/menus/main/custom_level_thumbnail.jpg", true, false);
        li.hide_stars = true;
		play_menu.insertLast(li);
	}
}

void Dispose() {
    imGUI.clear();
}

bool CanGoBack() {
    return true;
}

void Update() {
    if(!text_box_ip.active && !text_box_playername.active) {
        UpdateController();
    }
    text_box_ip.Update();
    text_box_playername.Update();

	UpdateKeyboardMouse();
    UpdatePublicCount();
    // process any messages produced from the update
    while( imGUI.getMessageQueueSize() > 0 ) {
        IMMessage@ message = imGUI.getNextMessage();

        if( message.name == "run_file" )
        {
			string campaign_id = play_menu[message.getInt(0)].campaign_id;
			if(campaign_id != "")  {
                SetCampaignID(campaign_id);
            }

            // We assume that running a file in this menu means we start hosting
            // In reality, mods can and will place their own menu before a map loads.
            Online_Host();

            this_ui.SendCallback(message.getString(0));
        }
        else if (message.name == "Join a friend")
        {
            this_ui.SendCallback("multiplayer_join_friend.as");
        }
        else if (message.name == "Your character")
        {
            this_ui.SendCallback("multiplayer_character_menu.as");
        }
        else if (message.name == ArenaRandomLabel())
        {
            SetConfigValueBool("oo_arena_random", !ArenaRandom());
            SaveConfig();
            imGUI.clear();
            Initialize();
        }
        else if (message.name == ArenaBotsLabel())
        {
            // off, then up to 2, 3, 4, 6, 8 fighters
            array<int> steps = {0, 2, 3, 4, 6, 8};
            int at = steps.find(ArenaFighters());
            SetConfigValueInt("oo_arena_bots", steps[(at + 1) % steps.length()]);
            SaveConfig();
            imGUI.clear();
            Initialize();
        }
        else if (message.name == "Browse games")
        {
            this_ui.SendCallback("multiplayer_browser.as");
        }
        else if (message.name == HostingLabel())
        {
            // public: the server browser lists your games; else friends only
            SetConfigValueBool("oo_public_game", !HostingPublic());
            SaveConfig();
            imGUI.clear();
            Initialize();
        }
        else if (message.name == "Connect")
        {
            this_ui.SendCallback("multiplayer_menu_connecting.as");
            Online_Connect(text_box_ip.GetText());
        }
		else if( message.name == "Custom Levels" )
        {
			this_ui.SendCallback( "custom_levels.as" );
        }
        else if( message.name == "Back" )
        {
            this_ui.SendCallback( "back" );
        }
        else if( message.name == "change_difficulty" )
        {
            this_ui.SendCallback( "difficulty_menu.as" );
        }
        else if (message.name == "activate_ip_text_input")
        {
            text_box_playername.Deactivate();
            text_box_ip.Activate();
        }
        else if (message.name == "activate_playername_text_input")
        {
            text_box_ip.Deactivate();
            text_box_playername.Activate();
        }
        else if (message.name == "update_playername")
        {
            if(Online_IsValidPlayerName(text_box_playername.input_text)) {
                text_box_playername.text_font = button_font_small;
                SetConfigValueString("playername", text_box_playername.input_text);
            } else {
                text_box_playername.text_font = invalid_name_font;
            }
            text_box_playername.UpdateText();
        }
        else if (message.name == "update_last_ip")
        {
            SetConfigValueString("last_ip", text_box_ip.input_text);
        }
    }

	// Do the general GUI updating
    imGUI.update();
}

void Resize() {
    imGUI.doScreenResize(); // This must be called first
	setBackGround();
}

void ScriptReloaded() {
    // Clear the old GUI
    imGUI.clear();
    // Rebuild it
    Initialize();
}

void DrawGUI() {
    imGUI.render();
}

void Draw() {

}

void Init(string str) {

}
