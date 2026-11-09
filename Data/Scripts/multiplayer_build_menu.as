//-----------------------------------------------------------------------------
//           Name: multiplayer_build_menu.as
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Build a level (the Multiplayer menu): pick a level and host it in
//                 build mode, the editor open, everyone who joins allowed to build
//                 (docs_mp_editing_plan.md). An empty level, your own saved levels and
//                 the autosaves, the mods' levels, the campaigns' levels. A level of
//                 your own goes to the players who join as a file.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;
array<LevelInfo@> all_levels;    // every level offered
array<LevelInfo@> build_levels;  // what's shown (a search narrows it)
BuildSearch search;

const int item_per_screen = 4;
const int rows_per_screen = 3;
const string kThumbnail = "Textures/ui/menus/main/custom_level_thumbnail.jpg";

bool HasFocus() {
    return false;
}

void Initialize() {
    @imGUI = CreateIMGUI();
    PlaySong("overgrowth_main");
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);
    imGUI.setup();
    ResetLevelList();
    BuildUI();
    search.SetCollection(build_levels);
    setBackGround();
}

// "Data/Levels/autosave/red_hills_autosave.xml" -> "red_hills_autosave"
string LevelFileTitle(const string &in path) {
    int slash = path.findLast("/");
    string name = slash >= 0 ? path.substr(slash + 1) : path;
    int dot = name.findLast(".");
    return dot > 0 ? name.substr(0, dot) : name;
}

// Overhaul's Large Battles maps (overhaul_battles/): copies of the campaign maps without
// their story, made for the battle mode; the originals are in the list already
bool BattleCopy(const string &in path) {
    return path.findFirst("overhaul_battles/") >= 0;
}

bool BattleCampaign(Campaign &in camp) {
    return camp.GetMenuScript().findFirst("aio_battle") >= 0;  // (multiplayer_menu.as: CoopCampaign)
}

LevelInfo@ Offer(const string &in file, const string &in title, const string &in thumbnail) {
    LevelInfo@ li = LevelInfo(file, title, thumbnail, false, true, false);
    li.hide_stars = true;
    return li;
}

void ResetLevelList() {
    all_levels.resize(0);
    all_levels.insertLast(Offer("Data/Levels/nothing.xml", "Empty level", kThumbnail));

    // Yours: saved in your write folder (the host's autosaves while building too)
    array<string>@ saved = Online_SavedLevels();
    for(uint i = 0; i < saved.length(); ++i) {
        string title = LevelFileTitle(saved[i]);
        if(saved[i].findFirst("/autosave/") >= 0) {
            title += " (autosave)";
        } else {
            title += " (yours)";
        }
        all_levels.insertLast(Offer(saved[i], title, kThumbnail));
    }

    uint skipped = 0;  // (Large Battles copies)

    // The mods' own levels
    array<ModID>@ active_sids = GetActiveModSids();
    for(uint i = 0; i < active_sids.length(); i++) {
        array<ModLevel>@ levels = ModGetSingleLevels(active_sids[i]);
        for(uint k = 0; k < levels.length(); k++) {
            if(BattleCopy(levels[k].GetPath())) {
                ++skipped;
                continue;
            }
            all_levels.insertLast(Offer(levels[k].GetPath(), levels[k].GetTitle(), levels[k].GetThumbnail()));
        }
    }

    // The campaigns' levels (not Large Battles: its maps are copies of these)
    array<Campaign>@ campaigns = GetCampaigns();
    for(uint i = 0; i < campaigns.length(); i++) {
        if(BattleCampaign(campaigns[i])) {
            skipped += campaigns[i].GetLevels().length();
            continue;
        }
        array<ModLevel>@ levels = campaigns[i].GetLevels();
        for(uint k = 0; k < levels.length(); k++) {
            if(BattleCopy(levels[k].GetPath())) {
                ++skipped;
                continue;
            }
            all_levels.insertLast(Offer(levels[k].GetPath(), campaigns[i].GetTitle() + ": " + levels[k].GetTitle(), levels[k].GetThumbnail()));
        }
    }
    build_levels = all_levels;
    Log(info, "Build a level: " + all_levels.length() + " levels, " + skipped + " Large Battles copies left out");
}

void BuildUI() {
    ClearControllerItems();
    IMDivider mainDiv("mainDiv", DOHorizontal);
    int initial_offset = 0;
    if(StorageHasInt32("multiplayer_build-shift_offset")) {
        initial_offset = StorageGetInt32("multiplayer_build-shift_offset");
    }
    while(initial_offset >= int(build_levels.length())) {
        initial_offset -= item_per_screen;
        if(initial_offset < 0) {
            initial_offset = 0;
            break;
        }
    }
    if(build_levels.size() == 0) {
        AddNoResults(mainDiv, true);
    } else {
        CreateMenu(mainDiv, build_levels, "multiplayer_build", initial_offset, item_per_screen, rows_per_screen, false, false, menu_width, menu_height, false, false, false, true);
    }
    imGUI.getMain().setElement(mainDiv);

    IMDivider header_divider("header_div", DOHorizontal);
    AddTitleHeader("Build a level", header_divider);
    AddSearchbar(header_divider, @search);
    imGUI.getHeader().setElement(header_divider);

    AddBackButton(true, true);
    search.ShowSearchResults();
}

void Dispose() {
    imGUI.clear();
}

bool CanGoBack() {
    return true;
}

void Update() {
    if(!search.active) {
        UpdateController();
    }
    search.Update();
    UpdateKeyboardMouse();
    while(imGUI.getMessageQueueSize() > 0) {
        IMMessage@ message = imGUI.getNextMessage();

        if(message.name == "Back") {
            this_ui.SendCallback("back");
        } else if(message.name == "run_file") {
            // Hosted, in build mode once it has loaded (the engine: Online::BuildOnLoad)
            Online_BuildOnLoad();
            Online_Host();
            this_ui.SendCallback(message.getString(0));
        } else if(message.name == "shift_menu") {
            StorageSetInt32("multiplayer_build-shift_offset", ShiftMenu(message.getInt(0)));
            SetControllerItemBeforeShift();
            BuildUI();
            SetControllerItemAfterShift(message.getInt(0));
        } else if(message.name == "refresh_menu_by_name") {
            string current_controller_item_name = GetCurrentControllerItemName();
            BuildUI();
            SetCurrentControllerItem(current_controller_item_name);
        } else if(message.name == "refresh_menu_by_id") {
            int index = GetCurrentControllerItemIndex();
            BuildUI();
            SetCurrentControllerItem(index);
        } else if(message.name == "activate_search") {
            search.Activate();
        } else if(message.name == "clear_search_results") {
            build_levels = all_levels;
            search.ResetSearch();
            SetCurrentControllerItem(0);
            imGUI.receiveMessage(IMMessage("refresh_menu_by_id"));
        }
    }
    imGUI.update();
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
}

void Draw() {
}

void Init(string str) {
}

class BuildSearch : Search {
    array<LevelInfo@>@ collection;
    BuildSearch() {
    }
    void SetCollection(array<LevelInfo@>@ _collection) {
        @collection = @_collection;
    }
    void GetSearchResults(string query) {
        array<LevelInfo@> results;
        for(uint i = 0; i < all_levels.length(); i++) {
            if(ToLowerCase(all_levels[i].name).findFirst(query) != -1) {
                results.insertLast(all_levels[i]);
            }
        }
        collection = results;
    }
}
