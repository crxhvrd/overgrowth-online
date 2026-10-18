//-----------------------------------------------------------------------------
//           Name: overhaul_trainer.as
//    Description: Trainer runtime, run by the level hook (overhaul_menu.as):
//                 world (gravity; game speed is the engine's own key),
//                 peaceful enemies, knock out / kill / revive / heal,
//                 spawning, weapons, teleport, and the HUD reminder line.
//                 The player's own cheats (god mode, damage, speed, jump...)
//                 live in aschar.as (OtrTick), read from the same otr_* keys.
//                 Online the world is the host's (gravity, game speed,
//                 peaceful, spawning, knock out / revive, weapons); players
//                 get their own cheats only if the host allows it.
//-----------------------------------------------------------------------------

int trainer_countdown = 0;
float trainer_gravity = 1.0f;
bool trainer_gravity_set = false;
bool trainer_peaceful = false;
array<int> trainer_pacified;
string trainer_summary = "";

string trainer_status = "";

// ---- online ----------------------------------------------------------------------
// The host sends "ohl trainer <gravity> <players allowed 0|1>" every 2 s.

bool trainer_net_allowed = false;   // client: the host lets us use the Trainer
float trainer_net_gravity = 1.0f;   // client: the host's gravity
float trainer_net_next = 0.0f;
int trainer_net_char = -1;          // client: our character, last told whether it may cheat
bool trainer_net_told = false;
float trainer_net_tell_next = 0.0f;

// The world: offline or on the host
bool TrainerWorldAllowed() {
    return !MPClient();
}

// Our own cheats: offline, on the host, or if the host allows them
bool TrainerPersonalAllowed() {
    return !MPClient() || trainer_net_allowed;
}

void TrainerNetSync() {
    if(MPHost() && the_time >= trainer_net_next) {
        trainer_net_next = the_time + 2.0f;
        MPToClients("trainer " + trainer_gravity + " " + (CfgBool("otr_mp_allow", false) ? 1 : 0));
    }

    // Our character's own cheats (aschar.as OtrTick): blocked unless allowed
    int me = MPClient() ? mp_me : -1;
    bool block = me != -1 && !trainer_net_allowed;

    if(trainer_net_char != -1 && trainer_net_char != me && MovementObjectExists(trainer_net_char)) {
        TrainerTellBlocked(trainer_net_char, false);  // offline again, or another character
    }

    if(me != -1 && MovementObjectExists(me) &&
            (me != trainer_net_char || block != trainer_net_told || the_time >= trainer_net_tell_next)) {
        TrainerTellBlocked(me, block);
        trainer_net_tell_next = the_time + 2.0f;  // a level reset gives it a fresh script
    }

    trainer_net_char = me != -1 && MovementObjectExists(me) ? me : -1;
    trainer_net_told = block;
}

void TrainerTellBlocked(int id, bool block) {
    MovementObject@ char = ReadCharacterID(id);

    if(char.HasVar("otr_net_blocked")) {
        char.Execute("otr_net_blocked = " + (block ? "true" : "false") + "; otr_read_countdown = 0;");
    }
}

// Client: the host's world and rules
void TrainerReceive(const string &in message) {
    if(!MPClient() || message.length() < 12 || message.substr(0, 12) != "ohl trainer ") {
        return;
    }

    array<string> t = MPTokens(message);

    if(t.length() >= 4) {
        trainer_net_gravity = max(0.0f, min(3.0f, atof(t[2])));
        trainer_net_allowed = atoi(t[3]) != 0;
    }
}

void TrainerSay(const string &in text) {
    trainer_status = text;
}

// The player, or null while spectating a battle or without one
MovementObject@ TrainerPlayer() {
    int id = HudPlayerId();

    if(id == -1 || BattleSpectating()) {
        return null;
    }

    return ReadCharacterID(id);
}

bool TrainerIsEnemy(MovementObject@ char, MovementObject@ player) {
    if(char.controlled || char.static_char) {
        return false;
    }

    return player is null || !player.OnSameTeam(char);
}

// ---- every update ----------------------------------------------------------------

void TrainerUpdate() {
    if(--trainer_countdown <= 0) {
        trainer_countdown = 15;
        trainer_gravity = MPClient() ? trainer_net_gravity : max(0.0f, min(3.0f, CfgFloat("otr_gravity", 1.0f)));
        trainer_peaceful = TrainerWorldAllowed() && CfgBool("otr_peaceful", false);
        TrainerPeace();
        TrainerBuildSummary();
        TrainerNetSync();
    }

    // Gravity: characters at once; ragdolls and items from the next level load
    if(trainer_gravity != 1.0f) {
        physics.gravity_vector = vec3(0.0f, -9.8f * trainer_gravity, 0.0f);
        trainer_gravity_set = true;
    } else if(trainer_gravity_set) {
        physics.gravity_vector = vec3(0.0f, -9.8f, 0.0f);
        trainer_gravity_set = false;
    }

    TrainerSpawnQueue();

    if(TrainerPersonalAllowed()) {
        TrainerKeepSkin();
    }
}

// Enemies may not attack while "Peaceful" is on; given back when it goes off
void TrainerPeace() {
    if(trainer_peaceful) {
        MovementObject@ player = TrainerPlayer();

        for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
            MovementObject@ char = ReadCharacter(i);

            if(TrainerIsEnemy(char, player) && trainer_pacified.find(char.GetID()) < 0) {
                char.ReceiveScriptMessage("set_combat_allowed false");
                trainer_pacified.insertLast(char.GetID());
            }
        }
    } else if(trainer_pacified.length() > 0) {
        for(uint i = 0; i < trainer_pacified.length(); ++i) {
            if(MovementObjectExists(trainer_pacified[i]) && !ReadCharacterID(trainer_pacified[i]).controlled) {
                // Not the message: it would also send them straight at you
                ReadCharacterID(trainer_pacified[i]).Execute("combat_allowed = true;");
            }
        }

        trainer_pacified.resize(0);
    }
}

// What is on, for the HUD reminder
void TrainerBuildSummary() {
    array<string> on;
    bool own = TrainerPersonalAllowed();

    if(own && CfgBool("otr_god", false)) on.insertLast("God mode");
    if(own && CfgFloat("otr_regen", 0.0f) > 0.0f) on.insertLast("Regen");
    if(own && CfgBool("otr_no_bleed", false)) on.insertLast("No bleeding");
    if(own && CfgBool("otr_block", false)) on.insertLast("Unbreakable block");
    if(own && CfgBool("otr_no_fall", false)) on.insertLast("No fall damage");
    if(own && CfgBool("otr_no_disarm", false)) on.insertLast("No disarm");
    if(own) {
        TrainerSummaryScale(@on, "Damage taken", "otr_damage_taken");
        TrainerSummaryScale(@on, "Damage", "otr_attack_damage");
        TrainerSummaryScale(@on, "Attack speed", "otr_attack_speed");
        TrainerSummaryScale(@on, "Knockback", "otr_knockback");
        TrainerSummaryScale(@on, "Speed", "otr_speed");
        TrainerSummaryScale(@on, "Jump", "otr_jump");
    }
    if(trainer_peaceful) on.insertLast("Peaceful enemies");

    if(MPClient()) {
        if(abs(trainer_net_gravity - 1.0f) > 0.001f) on.insertLast("Gravity " + TrainerX(trainer_net_gravity) + " (host)");
    } else {
        TrainerSummaryScale(@on, "Game speed", "global_time_scale_mult");
        TrainerSummaryScale(@on, "Gravity", "otr_gravity");
    }
    trainer_summary = join(on, "  |  ");
}

void TrainerSummaryScale(array<string>@ on, const string &in label, const string &in key) {
    float value = CfgFloat(key, 1.0f);

    if(abs(value - 1.0f) > 0.001f) {
        on.insertLast(label + " " + TrainerX(value));
    }
}

string TrainerX(float value) {
    int tenths = int(value * 10.0f + 0.5f);
    return "" + (tenths / 10) + "." + (tenths % 10) + "x";
}

void TrainerHudLine() {
    if(trainer_summary == "" || !CfgBool("otr_hud_line", true)) {
        return;
    }

    string text = "Trainer: " + trainer_summary;
    vec2 at(22.0f, hud_h - 70.0f * hud_scale - 62.0f);
    ImDrawList_AddText(at + vec2(1.0f, 1.0f), HudCol(kHudDark, 0.7f), text);
    ImDrawList_AddText(at, HudCol(vec3(0.6f, 0.85f, 1.0f), 0.85f), text);
}

// ---- you -------------------------------------------------------------------------

void TrainerHealMe() {
    MovementObject@ player = TrainerPlayer();

    if(player is null) {
        TrainerSay("No player character here.");
        return;
    }

    player.Execute("Recover();");
    TrainerSay("Healed.");
}

// ---- everyone ----------------------------------------------------------------------

void TrainerKnockOutEnemies(bool kill) {
    MovementObject@ player = TrainerPlayer();
    int n = 0;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);
        int ko = char.GetIntVar("knocked_out");

        if(!TrainerIsEnemy(char, player) || ko == _dead || (!kill && ko != _awake)) {
            continue;
        }

        // The stock scripts' own calls (aschar TakeDamage, drikafall drowning)
        char.Execute(kill ? "SetKnockedOut(_dead); Ragdoll(_RGDL_INJURED);" : "SetKnockedOut(_unconscious); Ragdoll(_RGDL_LIMP);");
        ++n;
    }

    TrainerSay((kill ? "Killed " : "Knocked out ") + n + (n == 1 ? " enemy." : " enemies."));
}

void TrainerReviveAll() {
    int n = 0;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.static_char || (BattleSpectating() && char.controlled)) {
            continue;
        }

        if(char.GetIntVar("knocked_out") != _awake) {
            ++n;
        }

        char.Execute("Recover();");  // stock: tutorial.as revive_all
    }

    TrainerSay("Everyone healed; " + n + " back on their feet.");
}

// ---- weapons -----------------------------------------------------------------------

const array<string> kTrainerWeaponNames = {"Knife", "Throwing knife", "Sword", "Rapier", "Spear", "Staff", "Big sword",
    "Hammer", "Machete", "Glaive", "Bastard sword", "Main gauche"};
const array<string> kTrainerWeaponPaths = {"Data/Items/rabbit_weapons/rabbit_knife.xml",
    "Data/Items/rabbit_weapons/rabbit_throwing_knife.xml", "Data/Items/DogWeapons/DogSword.xml", "Data/Items/Rapier.xml",
    "Data/Items/DogWeapons/DogSpear.xml", "Data/Items/staffbasic.xml", "Data/Items/DogWeapons/DogBroadSword.xml",
    "Data/Items/DogWeapons/DogHammer.xml", "Data/Items/rat_weapons/rat_machete.xml", "Data/Items/DogWeapons/DogGlaive.xml",
    "Data/Items/Bastard.xml", "Data/Items/MainGauche.xml"};

void TrainerGiveWeapon(int index) {
    MovementObject@ player = TrainerPlayer();

    if(player is null || index < 0 || index >= int(kTrainerWeaponPaths.length())) {
        TrainerSay("No player character here.");
        return;
    }

    int id = CreateObject(kTrainerWeaponPaths[index], true);
    Object@ item = ReadObjectFromID(id);
    vec3 flat = camera.GetFlatFacing();

    // species 1 = wolf (aschar enum, not visible here): no hands
    if(HudWeaponOf(player) == -1 && player.GetIntVar("knocked_out") == _awake && player.GetIntVar("species") != 1) {
        item.SetTranslation(player.position);
        ReadObjectFromID(player.GetID()).AttachItem(item, _at_grip, false);
        TrainerSay(kTrainerWeaponNames[index] + " in your hand.");
    } else {
        item.SetTranslation(player.position + flat * 0.8f + vec3(0.0f, 0.5f, 0.0f));
        TrainerSay(kTrainerWeaponNames[index] + " dropped in front of you (hands full).");
    }
}

// ---- teleport -------------------------------------------------------------------------

array<vec3> trainer_saved_pos = {vec3(0.0f), vec3(0.0f), vec3(0.0f)};
array<bool> trainer_saved = {false, false, false};

void TrainerMovePlayer(MovementObject@ player, const vec3 &in to) {
    player.Execute("this_mo.position = vec3(" + to.x + ", " + to.y + ", " + to.z + "); this_mo.velocity = vec3(0.0f);");
}

bool TrainerCanMove(MovementObject@ player) {
    if(player is null) {
        TrainerSay("No player character here.");
        return false;
    }

    if(player.GetIntVar("knocked_out") != _awake) {
        TrainerSay("Not while you're down (Heal first).");
        return false;
    }

    return true;
}

void TrainerTeleportLook() {
    MovementObject@ player = TrainerPlayer();

    if(!TrainerCanMove(player)) {
        return;
    }

    vec3 from = camera.GetPos();
    vec3 dir = camera.GetFacing();
    vec3 end = from + dir * 300.0f;
    vec3 hit = col.GetRayCollision(from, end);

    if(distance(hit, end) < 0.01f) {
        TrainerSay("Nothing there to stand on.");
        return;
    }

    int dist = int(distance(player.position, hit));
    TrainerMovePlayer(player, hit - dir * 0.6f + vec3(0.0f, 1.0f, 0.0f));
    TrainerSay("Teleported " + dist + " m.");
}

void TrainerSavePos(int slot) {
    MovementObject@ player = TrainerPlayer();

    if(!TrainerCanMove(player)) {
        return;
    }

    trainer_saved_pos[slot] = player.position;
    trainer_saved[slot] = true;
    TrainerSay("Position " + (slot + 1) + " saved (this level only).");
}

void TrainerLoadPos(int slot) {
    MovementObject@ player = TrainerPlayer();

    if(!trainer_saved[slot]) {
        TrainerSay("Position " + (slot + 1) + " is empty.");
        return;
    }

    if(TrainerCanMove(player)) {
        TrainerMovePlayer(player, trainer_saved_pos[slot] + vec3(0.0f, 0.1f, 0.0f));
        TrainerSay("Back at position " + (slot + 1) + ".");
    }
}

// ---- spawning -------------------------------------------------------------------------

class TrainerSpawn {
    vec3 pos;
    int species;
    bool enemy;
    int ai;
    int weapon;
}

class TrainerSpawned {
    int id;
    bool enemy;
    int frames = 0;
    bool set_up = false;
    array<int> items;
}

array<TrainerSpawn@> trainer_queue;
array<TrainerSpawned@> trainer_spawned;
int trainer_queue_wait = 0;

const array<string> kTrainerSpecies = {"Random", "Rabbit", "Cat", "Dog", "Rat", "Wolf"};
const array<string> kTrainerSides = {"Enemy", "Ally"};
const array<string> kTrainerAIs = {"Overhaul AI", "Vanilla AI"};
const array<string> kTrainerSpawnWeapons = {"None", "Knife", "Sword", "Spear", "Staff", "Big sword", "Hammer", "Rapier", "Mixed"};

void TrainerSpawnGroup() {
    MovementObject@ player = TrainerPlayer();
    int species = CfgInt("otr_sp_species", 0);
    bool enemy = CfgInt("otr_sp_side", 0) == 0;
    int ai = CfgInt("otr_sp_ai", 0);
    int weapon = CfgInt("otr_sp_weapon", 0);
    int count = max(1, min(10, CfgInt("otr_sp_count", 1)));
    vec3 flat = camera.GetFlatFacing();
    vec3 right(-flat.z, 0.0f, flat.x);
    vec3 base = (player !is null ? player.position : camera.GetPos()) + flat * 5.0f;

    if(!enemy && player !is null) {
        TrainerJoinAllies(player);
    }

    for(int i = 0; i < count; ++i) {
        vec3 pos = base + right * (float(i) - float(count - 1) * 0.5f) * 1.6f;
        vec3 ground = col.GetRayCollision(pos + vec3(0.0f, 4.0f, 0.0f), pos - vec3(0.0f, 30.0f, 0.0f));

        if(distance(ground, pos - vec3(0.0f, 30.0f, 0.0f)) > 0.01f) {
            pos = ground;
        }

        TrainerSpawn@ s = TrainerSpawn();
        s.pos = pos + vec3(0.0f, 0.6f, 0.0f);
        s.species = BattlePickSpecies(species - 1);
        s.enemy = enemy;
        s.ai = ai;
        s.weapon = weapon;
        trainer_queue.insertLast(s);
    }

    TrainerSay("Spawning " + count + (enemy ? " enemies." : " allies."));
}

// Allies need a team to share: an empty player team becomes "otr_allies"
string trainer_ally_team = "otr_allies";

void TrainerJoinAllies(MovementObject@ player) {
    Object@ obj = ReadObjectFromID(player.GetID());
    ScriptParams@ params = obj.GetScriptParams();
    string teams = params.HasParam("Teams") ? params.GetString("Teams") : "";

    if(teams == "") {
        params.SetString("Teams", "otr_allies");
        obj.UpdateScriptParams();
        teams = "otr_allies";
    }

    trainer_ally_team = teams;
}

// One at a time, a pause after the first: characters created during a cold
// asset load can go NaN (Large Battles does the same)
void TrainerSpawnQueue() {
    if(trainer_queue_wait > 0) {
        --trainer_queue_wait;
    } else if(trainer_queue.length() > 0) {
        TrainerSpawnOne(trainer_queue[0]);
        trainer_queue.removeAt(0);
        trainer_queue_wait = trainer_spawned.length() == 1 ? 40 : 4;
    }

    MovementObject@ player = TrainerPlayer();

    for(int i = int(trainer_spawned.length()) - 1; i >= 0; --i) {
        TrainerSpawned@ s = trainer_spawned[i];

        if(!MovementObjectExists(s.id)) {
            trainer_spawned.removeAt(i);
            continue;
        }

        if(s.set_up || ++s.frames < 20) {
            continue;
        }

        s.set_up = true;

        if(s.enemy && player !is null) {
            ReadCharacterID(s.id).ReceiveScriptMessage("notice " + player.GetID());
        }
    }
}

void TrainerSpawnOne(TrainerSpawn@ s) {
    uint variant = uint(rand()) % kBattleVariants[s.species].length();
    int id = CreateObject("Data/Objects/overhaul/battle/" + kBattleVariants[s.species][variant] + ".xml", true);
    Object@ obj = ReadObjectFromID(id);
    obj.SetTranslation(s.pos);
    ScriptParams@ params = obj.GetScriptParams();
    params.SetString("Teams", s.enemy ? "otr_enemies" : trainer_ally_team);
    params.SetInt("AI - Overhaul", s.ai == 1 ? 0 : 1);
    obj.UpdateScriptParams();

    TrainerSpawned@ spawned = TrainerSpawned();
    spawned.id = id;
    spawned.enemy = s.enemy;

    if(s.species != 4 && s.weapon > 0) {
        string path = BattleWeaponPath(s.weapon, s.species);

        if(path != "") {
            int item_id = CreateObject(path, true);
            Object@ item = ReadObjectFromID(item_id);
            item.SetTranslation(s.pos);
            obj.AttachItem(item, _at_grip, false);
            spawned.items.insertLast(item_id);
        }
    }

    trainer_spawned.insertLast(spawned);
}

void TrainerRemoveSpawned() {
    int n = 0;

    for(uint i = 0; i < trainer_spawned.length(); ++i) {
        TrainerSpawned@ s = trainer_spawned[i];

        for(uint j = 0; j < s.items.length(); ++j) {
            if(ObjectExists(s.items[j])) {
                QueueDeleteObjectID(s.items[j]);
            }
        }

        if(ObjectExists(s.id)) {
            QueueDeleteObjectID(s.id);
            ++n;
        }
    }

    trainer_spawned.resize(0);
    trainer_queue.resize(0);
    TrainerSay("Removed " + n + " spawned character" + (n == 1 ? "." : "s."));
}

// ---- skins ---------------------------------------------------------------------------

// Stock characters by species (Data/Characters), without the wounded and
// story-pose variants; name|file
const array<string> kSkinSpecies = {"Rabbit", "Cat", "Dog", "Rat", "Wolf"};
const array<array<string>> kSkins = {
    {"Turner|turner", "Pale Turner|pale_turner", "Rabbit 1|male_rabbit_1", "Rabbit 2|male_rabbit_2", "Rabbit 3|male_rabbit_3",
        "Female rabbit 1|female_rabbit_1", "Female rabbit 2|female_rabbit_2", "Female rabbit 3|female_rabbit_3",
        "Raider|raider_rabbit", "Guard|guard", "Guard 2|guard2", "Base guard|base_guard", "Heavy|heavy", "Heavy 2|heavy2",
        "Commander|commander", "Mook 1|mook1", "Mook 2|mook2", "Mook 3|mook3", "Mook 4|mook4", "Mook 5|mook5",
        "Mook 1 (winter)|mook1winter", "Mook 3 (winter)|mook3winter", "Civilian|civ", "Pale civilian|pale_rabbit_civ",
        "Ancient|ancient", "Ancient (female)|femaleancient", "Bijou|bijou", "Bijou (fine)|bijou_fine", "Jairo|jairo",
        "Donatas|donatas", "Gunk|gunk", "Reaper|reaper", "Beetle|beetle", "Ghost|ghost", "Ghost (armour)|ghostheavyarmor",
        "Rabbot|rabbot"},
    {"Cat|cat", "Male cat|male_cat", "Female cat|female_cat", "Striped cat|striped_cat", "Fancy striped cat|fancy_striped_cat",
        "Cobus|cobus", "Drika|drika", "Ferdi|ferdi", "Ouranian hand|ouranian_hand"},
    {"Dog soldier|dog_soldier", "Big dog|lt_dog_big", "Female dog|lt_dog_female", "Dog 1|lt_dog_male_1", "Dog 2|lt_dog_male_2"},
    {"Rat|rat", "Female rat|female_rat", "Hooded rat|hooded_rat"},
    {"Wolf|wolf", "Male wolf|male_wolf"}
};

string SkinName(int species, int index) {
    return kSkins[species][index].split("|")[0];
}

string SkinPath(int species, int index) {
    return "Data/Characters/" + kSkins[species][index].split("|")[1] + ".xml";
}

array<string>@ SkinNames(int species) {
    array<string> names;

    for(uint i = 0; i < kSkins[species].length(); ++i) {
        names.insertLast(SkinName(species, i));
    }

    return names;
}

void TrainerApplySkin(const string &in path) {
    MovementObject@ player = TrainerPlayer();

    if(player is null) {
        TrainerSay("No player character here.");
        return;
    }

    player.Execute("SwitchCharacter(\"" + path + "\");");  // stock: fixedattribute.as
    trainer_palette_char = "";
    TrainerSay("You are now " + path.substr(16, path.length() - 20) + ".");
}

// Palette slots of the player's character, labelled by channel
string trainer_palette_char = "";
array<int> trainer_palette_kinds = {0, 0, 0, 0};

void TrainerReadPalette(MovementObject@ player) {
    string key = player.GetID() + " " + player.char_path;

    if(key == trainer_palette_char) {
        return;
    }

    trainer_palette_char = key;
    player.Execute("OtrQueryChannels();");

    for(int i = 0; i < 4; ++i) {
        trainer_palette_kinds[i] = player.GetArrayIntVar("otr_channel_kinds", i);
    }
}

void TrainerSaveColors(Object@ obj) {
    string saved = "";

    for(int i = 0, len = min(4, obj.GetNumPaletteColors()); i < len; ++i) {
        vec3 c = obj.GetPaletteColor(i);
        saved += (i > 0 ? ";" : "") + c.x + " " + c.y + " " + c.z;
    }

    SetConfigValueString("otr_skin_colors", saved);
}

void TrainerLoadColors(Object@ obj) {
    if(!ConfigHasKey("otr_skin_colors") || GetConfigValueString("otr_skin_colors") == "") {
        return;
    }

    array<string>@ colors = GetConfigValueString("otr_skin_colors").split(";");

    for(int i = 0, len = min(int(colors.length()), obj.GetNumPaletteColors()); i < len; ++i) {
        array<string>@ parts = colors[i].split(" ");

        if(parts.length() == 3) {
            obj.SetPaletteColor(i, vec3(atof(parts[0]), atof(parts[1]), atof(parts[2])));
        }
    }
}

// "Keep in every level": once the level's player is up, switch it over
bool trainer_skin_checked = false;
float trainer_skin_wait = 0.0f;

void TrainerKeepSkin() {
    if(trainer_skin_checked || !CfgBool("otr_skin_keep", false) || BattleActive() || EditorModeActive()) {
        return;
    }

    MovementObject@ player = TrainerPlayer();

    if(player is null) {
        return;
    }

    trainer_skin_wait += time_step;

    if(trainer_skin_wait < 0.5f) {
        return;  // let the level's own setup finish
    }

    trainer_skin_checked = true;
    string path = ConfigHasKey("otr_skin_path") ? GetConfigValueString("otr_skin_path") : "";

    if(path == "") {
        return;
    }

    if(player.char_path != path) {
        player.Execute("SwitchCharacter(\"" + path + "\");");
    }

    TrainerLoadColors(ReadObjectFromID(player.GetID()));
    trainer_palette_char = "";
}
