//-----------------------------------------------------------------------------
//           Name: overhaul_hud.as
//    Description: Overhaul HUD, drawn by the level hook (overhaul_menu.as)
//                 in every level: the player's health panel, a radar
//                 minimap, name and health tags above nearby characters,
//                 and arrows at the screen edge for attackers out of view.
//                 Settings: GUI tab, ohud_* config keys.
//-----------------------------------------------------------------------------

// ---- settings ------------------------------------------------------------------

bool hud_on = true;
bool hud_health = true;
bool hud_map = true;
bool hud_map_rotate = true;
bool hud_map_items = true;
bool hud_tags = true;
bool hud_tag_allies = true;
bool hud_tag_style = false;
bool hud_threats = true;
float hud_map_range = 40.0f;
float hud_map_size = 170.0f;
float hud_tag_range = 14.0f;
float hud_scale = 1.0f;
float hud_opacity = 0.9f;
float hud_settings_next = 0.0f;

bool HudCfgBool(const string &in key, bool fallback) {
    return ConfigHasKey(key) ? GetConfigValueBool(key) : fallback;
}

float HudCfgFloat(const string &in key, float fallback) {
    return ConfigHasKey(key) ? GetConfigValueFloat(key) : fallback;
}

void HudRefreshSettings() {
    hud_settings_next = the_time + 0.5f;
    hud_on = HudCfgBool("ohud_enabled", true);
    hud_health = HudCfgBool("ohud_health", true);
    hud_map = HudCfgBool("ohud_minimap", true);
    hud_map_rotate = HudCfgBool("ohud_map_rotate", true);
    hud_map_items = HudCfgBool("ohud_map_items", true);
    hud_tags = HudCfgBool("ohud_tags", true);
    hud_tag_allies = HudCfgBool("ohud_tag_allies", true);
    hud_tag_style = HudCfgBool("ohud_tag_style", false);
    hud_threats = HudCfgBool("ohud_threats", true);
    hud_map_range = HudCfgFloat("ohud_map_range", 40.0f);
    hud_map_size = HudCfgFloat("ohud_map_size", 170.0f);
    hud_tag_range = HudCfgFloat("ohud_tag_range", 14.0f);
    hud_scale = HudCfgFloat("ohud_scale", 1.0f);
    hud_opacity = HudCfgFloat("ohud_opacity", 0.9f);
    hud_dmg = HudCfgBool("ohud_dmg", true);
    hud_dmg_all = HudCfgBool("ohud_dmg_all", false);
    hud_feed = HudCfgBool("ohud_feed", true);
    hud_feed_players = HudCfgBool("ohud_feed_players", false);
    hud_bars = HudCfgBool("ofin_bars", true);
    hud_objectives = HudCfgBool("ohud_objectives", true);
}

// ---- finisher letterbox ---------------------------------------------------------------

bool hud_bars = true;

// How far a finisher shot is blended in on the player's camera (aschar.as ofin_weight)
float HudCinematic(MovementObject@ player) {
    if(player is null || !hud_bars || !player.HasVar("ofin_weight")) {
        return 0.0f;
    }

    float w = player.GetFloatVar("ofin_weight");
    return w * w * (3.0f - 2.0f * w);
}

void HudDrawLetterbox(float cine) {
    float bar = hud_h * 0.11f * cine;
    HudRectFilled(vec2(0.0f, 0.0f), vec2(hud_w, bar), 0xFF000000, 0.0f);
    HudRectFilled(vec2(0.0f, hud_h - bar), vec2(hud_w, hud_h), 0xFF000000, 0.0f);
}

// ---- helpers ---------------------------------------------------------------------

// The game's ImGui (1.85) divides by zero on a rounded corner whose radius
// ends up just over 0 (a rect barely bigger than twice its rounding: a health
// bar running out, a small pip; a crash on 2026-10-16). Round only rects big
// enough for it (a NaN corner rounds nothing either)
float HudSafeRounding(const vec2 &in a, const vec2 &in b, float rounding) {
    float side = min(max(b.x - a.x, a.x - b.x), max(b.y - a.y, a.y - b.y));
    return side >= rounding * 2.0f + 4.0f ? rounding : 0.0f;
}

void HudRectFilled(const vec2 &in a, const vec2 &in b, uint col, float rounding = 0.0f) {
    ImDrawList_AddRectFilled(a, b, col, HudSafeRounding(a, b, rounding));
}

void HudRect(const vec2 &in a, const vec2 &in b, uint col, float rounding = 0.0f) {
    ImDrawList_AddRect(a, b, col, HudSafeRounding(a, b, rounding));
}

uint HudCol(const vec3 &in c, float a) {
    uint r = uint(max(0.0f, min(1.0f, c.x)) * 255.0f);
    uint g = uint(max(0.0f, min(1.0f, c.y)) * 255.0f);
    uint b = uint(max(0.0f, min(1.0f, c.z)) * 255.0f);
    uint al = uint(max(0.0f, min(1.0f, a * hud_opacity)) * 255.0f);
    return (al << 24) | (b << 16) | (g << 8) | r;
}

const int kHudAttackState = 2;  // aschar.as _attack_state (character-script constant)

const vec3 kHudRed = vec3(0.86f, 0.16f, 0.12f);
const vec3 kHudGreen = vec3(0.25f, 0.82f, 0.32f);
const vec3 kHudWhite = vec3(1.0f, 1.0f, 1.0f);
const vec3 kHudDark = vec3(0.05f, 0.05f, 0.06f);
const vec3 kHudGrey = vec3(0.55f, 0.55f, 0.55f);
const vec3 kHudGold = vec3(0.95f, 0.78f, 0.35f);

const array<string> kHudSpecies = {"Rabbit", "Wolf", "Dog", "Rat", "Cat"};
const array<string> kHudStyles = {"Balanced", "Kicker", "Boxer", "Trickster", "Brawler", "Skirmisher"};

vec3 HudHealthColor(float h) {
    return h > 0.6f ? kHudGreen : (h > 0.3f ? vec3(0.95f, 0.75f, 0.2f) : kHudRed);
}

// World point to screen pixels. Camera::GetFOV is the horizontal FOV; the
// engine draws with vertical = max(2 atan(tan(h/2) / aspect), h / (4/3))
vec3 hud_cam_pos;
vec3 hud_cam_fwd;
vec3 hud_cam_right;
vec3 hud_cam_up;
float hud_tan_v = 1.0f;
float hud_aspect = 1.0f;
float hud_w = 1.0f;
float hud_h = 1.0f;

void HudSetUpCamera() {
    hud_w = float(GetScreenWidth());
    hud_h = float(GetScreenHeight());
    hud_aspect = hud_w / max(1.0f, hud_h);
    hud_cam_pos = camera.GetPos();
    hud_cam_fwd = normalize(camera.GetFacing());
    hud_cam_right = normalize(cross(hud_cam_fwd, camera.GetUpVector()));
    hud_cam_up = cross(hud_cam_right, hud_cam_fwd);
    float h_rad = camera.GetFOV() * 0.0174533f;
    float v_rad = max(2.0f * atan(tan(h_rad * 0.5f) / hud_aspect), h_rad / (4.0f / 3.0f));
    hud_tan_v = tan(v_rad * 0.5f);
}

bool HudProject(const vec3 &in p, vec2 &out s) {
    vec3 d = p - hud_cam_pos;
    float z = dot(d, hud_cam_fwd);

    if(z < 0.2f) {
        return false;
    }

    float x = dot(d, hud_cam_right) / (z * hud_tan_v * hud_aspect);
    float y = dot(d, hud_cam_up) / (z * hud_tan_v);
    s = vec2((x + 1.0f) * 0.5f * hud_w, (1.0f - y) * 0.5f * hud_h);
    return s.x > -50.0f && s.x < hud_w + 50.0f && s.y > -50.0f && s.y < hud_h + 50.0f;
}

int HudPlayerId() {
    if(MPOnline()) {
        // online other players' characters are controlled too
        return mp_me != -1 && MovementObjectExists(mp_me) ? mp_me : -1;
    }

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        if(ReadCharacter(i).controlled) {
            return ReadCharacter(i).GetID();
        }
    }

    return -1;
}

int HudWeaponOf(MovementObject@ char) {
    return char.GetArrayIntVar("weapon_slots", char.GetIntVar("primary_weapon_slot"));
}

// Team colour in a Large Battle, else enemy red / ally green
vec3 HudColorOf(MovementObject@ char, MovementObject@ player) {
    vec3 c;

    if(BattleColorOf(char.GetID(), c)) {
        return c;
    }

    if(player is null) {
        return kHudGrey;
    }

    return player.OnSameTeam(char) ? kHudGreen : kHudRed;
}

void HudBar(const vec2 &in at, float w, float h, float value, float cap, const vec3 &in color) {
    HudRectFilled(at, at + vec2(w, h), HudCol(kHudDark, 0.75f), 2.0f);

    if(cap < 1.0f) {
        // the part that won't heal back, dimmed
        HudRectFilled(at + vec2(w * cap, 0.0f), at + vec2(w, h), HudCol(vec3(0.25f, 0.05f, 0.05f), 0.8f), 2.0f);
    }

    HudRectFilled(at, at + vec2(w * max(0.0f, min(1.0f, value)), h), HudCol(color, 0.95f), 2.0f);
    HudRect(at, at + vec2(w, h), HudCol(kHudWhite, 0.25f), 2.0f);
}


// ---- names, damage numbers and the combat log ---------------------------------
// Every frame each character's health is compared with the last frame:
// temp_health drops on every hit (TakeDamage), blood_health drops on cuts
// (and slowly while bleeding); attacked_by_id says who hit it last. A drop
// is a hit by that attacker; a knockout goes to whoever hit it within 5 s.
// Records are dropped the frame a character is gone: Large Battles reuses
// ids for reinforcements, and a new fighter must not inherit a name.

bool hud_dmg = true;
bool hud_dmg_all = false;
bool hud_feed = true;
bool hud_feed_players = false;  // the feed: only lines with a player in them

class HudChar {
    int id = -1;
    float temp = 1.0f;
    float blood = 1.0f;
    float bleed = 0.0f;       // slow bleeding, summed until it is worth a number
    int ko = 0;
    int hitter = -1;
    float hit_time = -10.0f;
    bool seen = false;
}

class HudNumber {
    int victim = -1;
    vec3 pos;
    float value = 0.0f;
    string text;
    vec3 color;
    float t0 = 0.0f;
}

// One side of a feed line: "You", a player's nickname, or an icon and species
class HudFeedWho {
    bool none = true;         // (no attacker: it fell, or nobody hit it lately)
    string text;
    int icon = 0;             // 0 none, 1 enemy, 2 teammate
    vec3 color;
    bool player = false;
}

const int kGlyphKO = 0;       // fist: knocked out
const int kGlyphKill = 1;     // skull: killed
const int kGlyphBlade = 2;    // blade: killed with a weapon, a cut throat
const int kGlyphFall = 3;     // arrow down: lava, water, a fall
const int kGlyphRevive = 4;   // plus: got up again (a teammate's revive)

class HudFeedLine {
    HudFeedWho@ a;
    int glyph = 0;
    HudFeedWho@ b;
    float t0 = 0.0f;
    float life = 4.5f;
    bool mine = false;        // you or another player is in it: framed, kept longer
    int count = 1;            // the same line again soon after: "x2"
}

array<HudChar@> hud_chars;
array<HudNumber@> hud_numbers;
array<HudFeedLine@> hud_feed_lines;
float hud_dealt = 0.0f;
float hud_taken = 0.0f;
int hud_kos = 0;

HudChar@ HudCharFor(MovementObject@ char) {
    int id = char.GetID();

    for(uint i = 0; i < hud_chars.length(); ++i) {
        if(hud_chars[i].id == id) {
            return hud_chars[i];
        }
    }

    HudChar@ rec = HudChar();
    rec.id = id;
    rec.temp = char.GetFloatVar("temp_health");
    rec.blood = char.GetFloatVar("blood_health");
    rec.ko = char.GetIntVar("knocked_out");
    hud_chars.insertLast(rec);
    return rec;
}

// Who it is in the feed, seen from our player: "You", a player's nickname, or
// an icon (enemy or teammate) and species for anyone else
HudFeedWho@ HudWho(int id, MovementObject@ player, int player_id) {
    HudFeedWho@ w = HudFeedWho();

    if(id == -1 || !MovementObjectExists(id)) {
        return w;
    }

    w.none = false;
    w.color = HudCharColor(id, player, player_id);

    if(id == player_id) {
        w.text = "You";
        w.player = true;
    } else if(MPIsPlayer(id)) {
        string nick = MPPlayerName(id);
        w.text = nick != "" ? nick : "Player";
        w.player = true;
    } else {
        MovementObject@ char = ReadCharacterID(id);
        int species = char.GetIntVar("species");
        w.text = species >= 0 && species < int(kHudSpecies.length()) ? kHudSpecies[species] : "Fighter";
        w.icon = player !is null && player.OnSameTeam(char) ? 2 : 1;
    }

    return w;
}

// The icon for a character that isn't a player: a diamond for an enemy, a
// shield for a teammate (`c` its middle, `r` its half width)
void HudShieldPath(const vec2 &in c, float r) {
    ImDrawList_PathClear();
    ImDrawList_PathLineTo(c + vec2(-r, -r));
    ImDrawList_PathLineTo(c + vec2(r, -r));
    ImDrawList_PathLineTo(c + vec2(r, r * 0.2f));
    ImDrawList_PathLineTo(c + vec2(0.0f, r * 1.2f));
    ImDrawList_PathLineTo(c + vec2(-r, r * 0.2f));
}

void HudDrawIcon(const vec2 &in c, float r, bool ally, const vec3 &in color, float a) {
    if(ally) {
        HudShieldPath(c + vec2(1.0f, 1.0f), r);
        ImDrawList_PathFillConvex(HudCol(kHudDark, 0.6f * a));
        HudShieldPath(c, r);
        ImDrawList_PathFillConvex(HudCol(color, a));
        HudShieldPath(c, r);
        ImDrawList_PathStroke(HudCol(kHudWhite, 0.55f * a), true, 1.0f);
    } else {
        vec2 t(0.0f, -r * 1.2f), e(r, 0.0f), b(0.0f, r * 1.2f), w(-r, 0.0f);
        vec2 o(1.0f, 1.0f);
        ImDrawList_AddQuadFilled(c + t + o, c + e + o, c + b + o, c + w + o, HudCol(kHudDark, 0.6f * a));
        ImDrawList_AddQuadFilled(c + t, c + e, c + b, c + w, HudCol(color, a));
        ImDrawList_AddQuad(c + t, c + e, c + b, c + w, HudCol(kHudWhite, 0.55f * a), 1.0f);
    }
}

vec3 HudCharColor(int id, MovementObject@ player, int player_id) {
    if(id == player_id) {
        return kHudGold;
    }

    if(id == -1 || !MovementObjectExists(id)) {
        return kHudGrey;
    }

    return HudColorOf(ReadCharacterID(id), player) * 0.7f + vec3(0.3f);
}

void HudAddNumber(MovementObject@ victim, float value, const string &in text, const vec3 &in color) {
    int id = victim.GetID();
    vec3 head = victim.rigged_object().GetAvgIKChainPos("head") + vec3(0.0f, 0.6f, 0.0f);

    // A quick string of hits reads as one number
    for(uint i = 0; i < hud_numbers.length(); ++i) {
        HudNumber@ n = hud_numbers[i];

        if(n.victim == id && n.text == "" && text == "" && the_time - n.t0 < 0.35f) {
            n.value += value;
            n.pos = head;
            n.t0 = the_time;
            n.color = color;
            return;
        }
    }

    HudNumber@ n = HudNumber();
    n.victim = id;
    n.pos = head;
    n.value = value;
    n.text = text;
    n.color = color;
    n.t0 = the_time;
    hud_numbers.insertLast(n);
}

bool HudSameWho(HudFeedWho@ x, HudFeedWho@ y) {
    return x.none == y.none && x.text == y.text && x.icon == y.icon;
}

void HudAddEvent(int attacker, int glyph, int victim, MovementObject@ player, int player_id) {
    HudFeedLine@ line = HudFeedLine();
    @line.a = HudWho(attacker, player, player_id);
    line.glyph = glyph;
    @line.b = HudWho(victim, player, player_id);
    line.mine = line.a.player || line.b.player;
    line.life = line.mine ? 8.0f : 4.5f;
    line.t0 = the_time;

    if(!line.mine && hud_feed_players) {
        return;
    }

    // The same thing again soon after (a Large Battle): one line, counted
    if(!line.mine) {
        for(uint i = 0; i < hud_feed_lines.length(); ++i) {
            HudFeedLine@ l = hud_feed_lines[i];

            if(!l.mine && l.glyph == glyph && HudSameWho(l.a, line.a) && HudSameWho(l.b, line.b) && the_time - l.t0 < 3.0f) {
                ++l.count;
                l.t0 = the_time;
                return;
            }
        }
    }

    hud_feed_lines.insertLast(line);

    while(hud_feed_lines.length() > 5) {
        hud_feed_lines.removeAt(0);
    }
}

// How it went down: a hazard, a blade (a weapon, a cut throat), a beating
int HudDownGlyph(MovementObject@ char, int ko, int killer) {
    if(char.GetIntVar("zone_killed") != 0) {
        return kGlyphFall;
    }

    if(ko != _dead) {
        return kGlyphKO;
    }

    bool armed = killer != -1 && MovementObjectExists(killer) && HudWeaponOf(ReadCharacterID(killer)) != -1;
    return armed || char.GetBoolVar("cut_throat") ? kGlyphBlade : kGlyphKill;
}

// A teammate's revive (coop_revive.as, online): "ohl coop_revived #<reviver> #<revived>"
void HudFeedReceive(const string &in message) {
    if(!hud_feed || message.length() < 18 || message.substr(0, 17) != "ohl coop_revived ") {
        return;
    }

    array<string> t = MPTokens(message);

    if(t.length() < 4) {
        return;
    }

    int player_id = HudPlayerId();
    MovementObject@ player = null;

    if(player_id != -1 && MovementObjectExists(player_id)) {
        @player = ReadCharacterID(player_id);
    }

    HudAddEvent(MPId(t[2]), kGlyphRevive, MPId(t[3]), player, player_id);
}

void HudTrack(MovementObject@ player, int player_id) {
    for(uint i = 0; i < hud_chars.length(); ++i) {
        hud_chars[i].seen = false;
    }

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.static_char) {
            continue;
        }

        HudChar@ rec = HudCharFor(char);
        rec.seen = true;
        float temp = char.GetFloatVar("temp_health");
        float blood = char.GetFloatVar("blood_health");
        int ko = char.GetIntVar("knocked_out");
        float hit = max(0.0f, rec.temp - temp);
        float drop = max(0.0f, rec.blood - blood);
        bool bled = false;

        if(drop > 0.015f) {
            hit = max(hit, drop);  // a cut (it can pull temp_health down with it)
        } else {
            rec.bleed += drop;  // bleeding: summed up

            if(rec.bleed > 0.05f) {
                hit += rec.bleed;
                rec.bleed = 0.0f;
                bled = true;
            }
        }

        rec.temp = temp;
        rec.blood = blood;
        int attacker = char.GetIntVar("attacked_by_id");

        if(hit * 100.0f >= 0.5f) {
            float value = hit * 100.0f;
            rec.hitter = attacker;
            rec.hit_time = the_time;

            if(attacker == player_id && player_id != -1) {
                hud_dealt += value;
            }

            if(char.GetID() == player_id) {
                hud_taken += value;
            }

            if(hud_dmg && char.GetID() != player_id && (hud_dmg_all || (attacker == player_id && player_id != -1))) {
                HudAddNumber(char, value, "", bled ? vec3(0.75f, 0.12f, 0.1f) :
                    (value >= 30.0f ? vec3(1.0f, 0.55f, 0.15f) : vec3(1.0f, 0.95f, 0.75f)));
            }
        }

        if(rec.ko == _awake && ko != _awake) {
            // Down: whoever hit it last, if that was recent
            int killer = the_time - rec.hit_time < 5.0f ? rec.hitter : -1;

            if(killer == player_id && player_id != -1) {
                ++hud_kos;
            }

            if(hud_feed) {
                HudAddEvent(killer != char.GetID() ? killer : -1, HudDownGlyph(char, ko, killer), char.GetID(), player, player_id);
            }

            if(hud_dmg && char.GetID() != player_id && (hud_dmg_all || killer == player_id)) {
                HudAddNumber(char, 0.0f, ko == _dead ? "KILL" : "KO", kHudRed);
            }
        }

        rec.ko = ko;
    }

    HudForgetGone();
}

// Gone characters: forget them
void HudForgetGone() {
    for(int i = int(hud_chars.length()) - 1; i >= 0; --i) {
        if(!hud_chars[i].seen) {
            hud_chars.removeAt(i);
        }
    }
}

// ---- online: one set of names ------------------------------------------------------
void HudDrawNumbers() {
    for(int i = int(hud_numbers.length()) - 1; i >= 0; --i) {
        HudNumber@ n = hud_numbers[i];
        float age = the_time - n.t0;

        if(age > 1.3f) {
            hud_numbers.removeAt(i);
            continue;
        }

        vec2 s;

        if(!HudProject(n.pos + vec3(0.0f, age * 0.8f, 0.0f), s)) {
            continue;
        }

        string text = n.text != "" ? n.text : "" + int(n.value + 0.5f);
        float a = age < 0.9f ? 1.0f : 1.0f - (age - 0.9f) / 0.4f;
        vec2 size = ImGui_CalcTextSize(text);
        ImDrawList_AddText(vec2(s.x - size.x * 0.5f + 1.0f, s.y + 1.0f), HudCol(kHudDark, a * 0.8f), text);
        ImDrawList_AddText(vec2(s.x - size.x * 0.5f, s.y), HudCol(n.color, a), text);
    }
}

// The feed's parts, measured and drawn left to right (x: where it starts)
float HudWhoWidth(HudFeedWho@ w) {
    if(w.none) {
        return 0.0f;
    }

    return (w.icon != 0 ? 14.0f : 0.0f) + ImGui_CalcTextSize(w.text).x;
}

float HudDrawWho(float x, float y, HudFeedWho@ w, float a) {
    if(w.none) {
        return x;
    }

    if(w.icon != 0) {
        HudDrawIcon(vec2(x + 6.0f, y + 7.0f), 5.0f, w.icon == 2, w.color, a);
        x += 14.0f;
    }

    ImDrawList_AddText(vec2(x + 1.0f, y + 1.0f), HudCol(kHudDark, 0.7f * a), w.text);
    ImDrawList_AddText(vec2(x, y), HudCol(w.player ? w.color * 0.5f + vec3(0.5f) : w.color, a), w.text);
    return x + ImGui_CalcTextSize(w.text).x;
}

// How: a small drawing in a 16 px cell (c: its middle)
void HudDrawGlyph(const vec2 &in c, int glyph, float a) {
    uint dark = HudCol(kHudDark, 0.9f * a);

    if(glyph == kGlyphKO) {
        uint col = HudCol(vec3(0.95f, 0.85f, 0.6f), a);
        HudRectFilled(c + vec2(-5.0f, -1.5f), c + vec2(5.0f, 5.5f), col, 2.0f);
        for(int k = 0; k < 4; ++k) {
            ImDrawList_AddCircleFilled(c + vec2(-3.6f + 2.4f * k, -2.5f), 1.6f, col, 8);
        }
        ImDrawList_AddLine(c + vec2(-4.0f, 1.5f), c + vec2(1.0f, 1.5f), dark, 1.0f);
    } else if(glyph == kGlyphKill) {
        uint col = HudCol(vec3(0.92f), a);
        ImDrawList_AddCircleFilled(c + vec2(0.0f, -1.5f), 5.2f, col, 16);
        HudRectFilled(c + vec2(-3.0f, 1.5f), c + vec2(3.0f, 6.0f), col, 1.0f);
        ImDrawList_AddCircleFilled(c + vec2(-2.1f, -1.2f), 1.5f, dark, 8);
        ImDrawList_AddCircleFilled(c + vec2(2.1f, -1.2f), 1.5f, dark, 8);
        ImDrawList_AddLine(c + vec2(0.0f, 4.0f), c + vec2(0.0f, 6.0f), dark, 1.0f);
    } else if(glyph == kGlyphBlade) {
        uint col = HudCol(vec3(0.85f, 0.9f, 1.0f), a);
        ImDrawList_AddLine(c + vec2(-5.0f, 5.0f), c + vec2(5.5f, -5.5f), col, 2.0f);
        ImDrawList_AddLine(c + vec2(-5.5f, 1.0f), c + vec2(-1.0f, 5.5f), col, 2.0f);
        ImDrawList_AddCircleFilled(c + vec2(-5.5f, 5.5f), 1.3f, col, 6);
    } else if(glyph == kGlyphFall) {
        uint col = HudCol(vec3(1.0f, 0.6f, 0.2f), a);
        HudRectFilled(c + vec2(-1.5f, -6.0f), c + vec2(1.5f, -1.0f), col);
        ImDrawList_AddTriangleFilled(c + vec2(-5.0f, -1.0f), c + vec2(5.0f, -1.0f), c + vec2(0.0f, 6.0f), col);
    } else if(glyph == kGlyphRevive) {
        uint col = HudCol(vec3(0.35f, 0.9f, 0.45f), a);
        HudRectFilled(c + vec2(-1.8f, -6.0f), c + vec2(1.8f, 6.0f), col, 1.0f);
        HudRectFilled(c + vec2(-6.0f, -1.8f), c + vec2(6.0f, 1.8f), col, 1.0f);
    }
}

void HudDrawFeed() {
    for(int i = int(hud_feed_lines.length()) - 1; i >= 0; --i) {
        if(the_time - hud_feed_lines[i].t0 > hud_feed_lines[i].life) {
            hud_feed_lines.removeAt(i);
        }
    }

    // Top right, under the minimap
    float right = hud_w - 18.0f;
    float y = hud_map ? hud_map_size * hud_scale + 34.0f : 20.0f;

    for(int i = int(hud_feed_lines.length()) - 1; i >= 0; --i) {  // newest first
        HudFeedLine@ l = hud_feed_lines[i];
        float age = the_time - l.t0;
        float a = age < l.life - 1.2f ? 1.0f : max(0.0f, (l.life - age) / 1.2f);
        a *= l.mine ? 1.0f : 0.85f;
        string tail = l.count > 1 ? "  x" + l.count : "";
        float gap = 6.0f;
        float w = HudWhoWidth(l.a) + (l.a.none ? 0.0f : gap) + 16.0f + gap + HudWhoWidth(l.b) + ImGui_CalcTextSize(tail).x;
        float x = right - w;

        HudRectFilled(vec2(x - 7.0f, y - 3.0f), vec2(right + 7.0f, y + 17.0f), HudCol(kHudDark, (l.mine ? 0.6f : 0.4f) * a), 3.0f);

        if(l.mine) {
            HudRect(vec2(x - 7.0f, y - 3.0f), vec2(right + 7.0f, y + 17.0f), HudCol(kHudGold, 0.55f * a), 3.0f);
        }

        x = HudDrawWho(x, y, l.a, a);

        if(!l.a.none) {
            x += gap;
        }

        HudDrawGlyph(vec2(x + 8.0f, y + 7.0f), l.glyph, a);
        x += 16.0f + gap;
        x = HudDrawWho(x, y, l.b, a);

        if(tail != "") {
            ImDrawList_AddText(vec2(x, y), HudCol(vec3(1.0f, 0.9f, 0.6f), a), tail);
        }

        y += 24.0f;
    }
}

// ---- tags above characters -------------------------------------------------------

void HudDrawTags(MovementObject@ player, int player_id) {
    vec3 from = player !is null ? player.position : hud_cam_pos;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == player_id || char.static_char) {
            continue;
        }

        bool ally = player !is null && player.OnSameTeam(char);

        if(ally && !hud_tag_allies) {
            continue;
        }

        float dist = distance(char.position, from);

        if(dist > hud_tag_range) {
            continue;
        }

        bool awake = char.GetIntVar("knocked_out") == _awake;
        vec3 head = char.rigged_object().GetAvgIKChainPos("head") + vec3(0.0f, 0.45f, 0.0f);
        vec2 s;

        if(!HudProject(head, s)) {
            continue;
        }

        // Hidden behind something: no tag
        vec3 hit = col.GetRayCollision(hud_cam_pos, head);

        if(distance(hit, head) > 0.6f) {
            continue;
        }

        float fade = dist > hud_tag_range * 0.7f ? 1.0f - (dist - hud_tag_range * 0.7f) / (hud_tag_range * 0.3f) : 1.0f;
        fade *= awake ? 1.0f : 0.5f;
        vec3 color = HudColorOf(char, player);
        bool is_player = MPIsPlayer(char.GetID());
        string style_text = "";

        if(hud_tag_style && char.HasVar("aio_style")) {
            int style = char.GetIntVar("aio_style");

            if(style >= 0 && style < int(kHudStyles.length())) {
                style_text = kHudStyles[style];
            }
        }

        float bar_w = 64.0f * hud_scale;

        if(is_player) {
            // A player: the game shows the nickname above it already; just the health
        } else {
            // Anyone else: an icon (enemy or teammate), dimmed when down
            vec2 c(s.x, s.y - 12.0f * hud_scale);
            HudDrawIcon(c, 6.0f * hud_scale, ally, color * 0.75f + vec3(0.25f), fade);

            if(style_text != "") {
                ImDrawList_AddText(c + vec2(10.0f * hud_scale + 1.0f, -7.0f + 1.0f), HudCol(kHudDark, 0.8f * fade), style_text);
                ImDrawList_AddText(c + vec2(10.0f * hud_scale, -7.0f), HudCol(color * 0.6f + vec3(0.4f), fade), style_text);
            }
        }

        if(awake) {
            float temp = char.GetFloatVar("temp_health");
            float perm = char.GetFloatVar("permanent_health");
            vec2 at(s.x - bar_w * 0.5f, s.y - 3.0f * hud_scale);
            HudRectFilled(at, at + vec2(bar_w, 5.0f * hud_scale), HudCol(kHudDark, 0.7f * fade), 1.5f);
            HudRectFilled(at + vec2(bar_w * max(0.0f, perm), 0.0f), at + vec2(bar_w, 5.0f * hud_scale),
                HudCol(vec3(0.25f, 0.05f, 0.05f), 0.8f * fade), 1.5f);
            HudRectFilled(at, at + vec2(bar_w * max(0.0f, min(1.0f, temp)), 5.0f * hud_scale),
                HudCol(HudHealthColor(temp), 0.95f * fade), 1.5f);
            HudDrawShieldPips(char, vec2(s.x, at.y + 7.0f * hud_scale), bar_w, fade);
        }
    }
}

// The knockout shield under the health bar: a pip a point (aschar.as ko_shield,
// up to "Knockout Shield"); while one is left it can't be knocked out. Broken
// ones stay as outlines. Nothing for a character without a shield
void HudDrawShieldPips(MovementObject@ char, const vec2 &in top_middle, float bar_w, float fade) {
    int most = char.GetIntVar("max_ko_shield");

    if(most <= 0) {
        return;
    }

    int left = max(0, min(most, char.GetIntVar("ko_shield")));
    float gap = 2.0f * hud_scale;
    float w = min(8.0f * hud_scale, (bar_w - gap * (most - 1)) / most);
    float h = 3.0f * hud_scale;
    float x = top_middle.x - (w * most + gap * (most - 1)) * 0.5f;

    for(int k = 0; k < most; ++k) {
        vec2 a(x + k * (w + gap), top_middle.y);

        if(k < left) {
            HudRectFilled(a + vec2(1.0f, 1.0f), a + vec2(w + 1.0f, h + 1.0f), HudCol(kHudDark, 0.7f * fade), 1.0f);
            HudRectFilled(a, a + vec2(w, h), HudCol(vec3(0.55f, 0.75f, 1.0f), 0.95f * fade), 1.0f);
        } else {
            HudRect(a, a + vec2(w, h), HudCol(vec3(0.55f, 0.75f, 1.0f), 0.35f * fade), 1.0f);
        }
    }
}

// ---- attackers out of view -------------------------------------------------------

void HudDrawThreats(MovementObject@ player, int player_id) {
    if(player is null) {
        return;
    }

    vec2 center(hud_w * 0.5f, hud_h * 0.5f);
    float radius = min(hud_w, hud_h) * 0.42f;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == player_id || char.static_char || player.OnSameTeam(char) || char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        vec3 d = char.position - player.position;
        float dist = length(d);

        if(dist > 10.0f) {
            continue;
        }

        bool attacking = char.GetIntVar("state") == kHudAttackState && char.GetIntVar("target_id") == player_id;
        bool coming = dot(char.velocity, normalize(d) * -1.0f) > 3.0f && dist < 7.0f;

        if(!attacking && !coming) {
            continue;
        }

        vec2 s;

        if(HudProject(char.position, s) && s.x > 0.0f && s.x < hud_w && s.y > 0.0f && s.y < hud_h) {
            continue;  // in view: no arrow needed
        }

        vec3 rel = char.position - hud_cam_pos;
        vec2 dir(dot(rel, hud_cam_right), -dot(rel, hud_cam_fwd));

        if(length(dir) < 0.01f) {
            continue;
        }

        dir = normalize(dir);
        vec2 tip = center + dir * radius;
        vec2 side(-dir.y, dir.x);
        float size = (attacking ? 16.0f : 11.0f) * hud_scale;
        float a = attacking ? (0.6f + 0.4f * sin(the_time * 12.0f)) : 0.7f;
        ImDrawList_AddTriangleFilled(tip + dir * size, tip + side * size * 0.6f, tip - side * size * 0.6f, HudCol(kHudRed, a));
    }
}

// ---- objectives ---------------------------------------------------------------------
// The campaign's next goal, offline and online alike. Story levels: the level
// hotspot (overgrowth_level.as) holds goal_<n> ("reach <checkpoint>",
// "reach_skippable <checkpoint>", "defeat <ids>", "spawn_defeat [no_delay] <ids>",
// "defeat_optional <ids>") and "progress", the goal now (a guest's follows the
// host's). An optional defeat is passed by doing the next goal, so that one leads
// and the optional enemies get a dimmer mark. Lugaru levels (lugaru_level.as) are
// won when no enemy is awake. A line top left, a mark on the target, an arrow at
// the edge when it is out of view, and the minimap.

bool hud_objectives = true;
int hud_obj_hotspot = -1;   // the level hotspot (object id)
int hud_obj_index = -1;     // ... its ReadHotspot index
bool hud_obj_lugaru = false;
float hud_obj_scan_next = 0.0f;
int hud_obj_progress = -1;  // the goal the checkpoint below is for
int hud_obj_checkpoint = -1;
float hud_obj_point_next = 0.0f;
bool hud_obj_point_found = false;
vec3 hud_obj_found_point;

// This frame's objective
string hud_obj_line = "";
string hud_obj_sub = "";
bool hud_obj_has_point = false;
vec3 hud_obj_point;
array<int> hud_obj_targets;
array<int> hud_obj_optional;

void HudObjScan() {
    hud_obj_hotspot = -1;
    hud_obj_index = -1;
    hud_obj_lugaru = false;
    hud_obj_progress = -1;

    for(int i = 0, len = GetNumHotspots(); i < len; ++i) {
        Hotspot@ h = ReadHotspot(i);
        ScriptParams@ p = ReadObjectFromID(h.GetID()).GetScriptParams();

        if(p.HasParam("goal_0") && h.HasVar("progress")) {
            hud_obj_hotspot = h.GetID();
            hud_obj_index = i;
            hud_obj_lugaru = false;
            return;
        }

        if(p.HasParam("press_attack_to_restart") && p.HasParam("death_message")) {
            hud_obj_hotspot = h.GetID();
            hud_obj_index = i;
            hud_obj_lugaru = true;
        }
    }
}

bool HudObjValid() {
    return hud_obj_hotspot != -1 && ObjectExists(hud_obj_hotspot) && hud_obj_index < GetNumHotspots() &&
        ReadHotspot(hud_obj_index).GetID() == hud_obj_hotspot;
}

// goal_<n>: its type ("" when there is none) and the ids in it
string HudObjGoal(ScriptParams@ p, int n, array<int>@ ids) {
    ids.resize(0);

    if(n < 0 || !p.HasParam("goal_" + n)) {
        return "";
    }

    string s = p.GetString("goal_" + n);
    TokenIterator it;
    it.Init();

    if(!it.FindNextToken(s)) {
        return "";
    }

    string type = it.GetToken(s);

    while(it.FindNextToken(s)) {
        string t = it.GetToken(s);

        if(t != "" && t != "no_delay") {
            ids.insertLast(atoi(t));
        }
    }

    return type;
}

// The ones still standing (and in the level: not spawned yet = disabled)
void HudObjAwake(array<int>@ ids, array<int>@ awake) {
    awake.resize(0);

    for(uint i = 0; i < ids.length(); ++i) {
        int id = ids[i];

        if(ObjectExists(id) && ReadObjectFromID(id).GetType() == _movement_object && ReadObjectFromID(id).GetEnabled() &&
                ReadCharacterID(id).GetIntVar("knocked_out") == _awake) {
            awake.insertLast(id);
        }
    }
}

// The checkpoint hotspot "reach <checkpoint>" means (fall-death ones aside),
// the nearest if there are several
bool HudObjCheckpoint(int checkpoint, const vec3 &in from, vec3 &out pos) {
    bool found = false;
    float best = 1e9f;

    for(int i = 0, len = GetNumHotspots(); i < len; ++i) {
        Object@ obj = ReadObjectFromID(ReadHotspot(i).GetID());
        ScriptParams@ p = obj.GetScriptParams();

        if(!p.HasParam("checkpoint_id") || p.GetInt("checkpoint_id") != checkpoint || p.HasParam("fall_death") ||
                (p.HasParam("level_hotspot_id") && p.GetInt("level_hotspot_id") != hud_obj_hotspot)) {
            continue;
        }

        float d = distance(obj.GetTranslation(), from);

        if(d < best) {
            best = d;
            pos = obj.GetTranslation();
            found = true;
        }
    }

    return found;
}

string HudObjCount(int n, const string &in one, const string &in many) {
    return n == 1 ? one : many;
}

void HudObjUpdate(MovementObject@ player, int player_id) {
    hud_obj_line = "";
    hud_obj_sub = "";
    hud_obj_has_point = false;
    hud_obj_targets.resize(0);
    hud_obj_optional.resize(0);

    if(the_time >= hud_obj_scan_next) {
        hud_obj_scan_next = the_time + 2.0f;

        if(!HudObjValid()) {
            HudObjScan();
        }
    }

    if(!HudObjValid()) {
        return;
    }

    vec3 from = player !is null ? player.position : hud_cam_pos;

    if(hud_obj_lugaru) {
        if(player is null) {
            return;
        }

        for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
            MovementObject@ char = ReadCharacter(i);

            if(char.GetID() != player_id && !char.static_char && !player.OnSameTeam(char) &&
                    char.GetIntVar("knocked_out") == _awake && ReadObjectFromID(char.GetID()).GetEnabled()) {
                hud_obj_targets.insertLast(char.GetID());
            }
        }

        if(hud_obj_targets.length() > 0) {
            hud_obj_line = HudObjCount(hud_obj_targets.length(), "Defeat the last enemy", "Defeat every enemy");
            hud_obj_sub = hud_obj_targets.length() + " left";
        }

        return;
    }

    int progress = ReadHotspot(hud_obj_index).GetIntVar("progress");
    ScriptParams@ p = ReadObjectFromID(hud_obj_hotspot).GetScriptParams();
    array<int> ids;
    int n = progress;
    string type = HudObjGoal(p, n, ids);

    // Optional fights (passed by doing a later goal): their enemies marked,
    // the first goal after them leads
    for(int k = 0; k < 8 && type == "defeat_optional"; ++k) {
        array<int> awake;
        HudObjAwake(ids, awake);

        for(uint i = 0; i < awake.length(); ++i) {
            hud_obj_optional.insertLast(awake[i]);
        }

        ++n;
        type = HudObjGoal(p, n, ids);
    }

    if((type == "reach" || type == "reach_skippable") && ids.length() > 0) {
        // (a look through every hotspot: when the goal changes, and each second for the nearest)
        if(n != hud_obj_progress || hud_obj_checkpoint != ids[0] || the_time >= hud_obj_point_next) {
            hud_obj_progress = n;
            hud_obj_checkpoint = ids[0];
            hud_obj_point_next = the_time + 1.0f;
            hud_obj_point_found = HudObjCheckpoint(ids[0], from, hud_obj_found_point);
        }

        hud_obj_has_point = hud_obj_point_found;
        hud_obj_point = hud_obj_found_point;

        if(hud_obj_has_point) {
            hud_obj_line = "Go to the marker";
            hud_obj_sub = int(distance(from, hud_obj_point) + 0.5f) + " m";
        }
    } else if(type == "defeat" || type == "spawn_defeat" || type == "defeat_optional") {
        HudObjAwake(ids, hud_obj_targets);

        if(hud_obj_targets.length() > 0) {
            hud_obj_line = HudObjCount(hud_obj_targets.length(), "Defeat the enemy", "Defeat the enemies");
            hud_obj_sub = hud_obj_targets.length() + " left";
        }
    }

    if(hud_obj_line == "" && hud_obj_optional.length() > 0) {
        hud_obj_line = HudObjCount(hud_obj_optional.length(), "Defeat the enemy", "Defeat the enemies");
        hud_obj_sub = hud_obj_optional.length() + " left (optional)";
    }
}

// A gold diamond with a dark rim
void HudObjDiamond(const vec2 &in c, float r, float a) {
    vec2 t(0.0f, -r);
    vec2 e(r * 0.7f, 0.0f);
    vec2 o(1.5f, 1.5f);
    ImDrawList_AddQuadFilled(c + t + o, c + e + o, c - t + o, c - e + o, HudCol(kHudDark, 0.6f * a));
    ImDrawList_AddQuadFilled(c + t, c + e, c - t, c - e, HudCol(kHudGold, a));
    ImDrawList_AddQuad(c + t, c + e, c - t, c - e, HudCol(kHudWhite, 0.6f * a), 1.0f);
}

// A gold pointer above a head
void HudObjPointer(const vec2 &in c, float r, float a) {
    vec2 o(1.5f, 1.5f);
    ImDrawList_AddTriangleFilled(c + vec2(-r, -r) + o, c + vec2(r, -r) + o, c + vec2(0.0f, r * 0.4f) + o, HudCol(kHudDark, 0.6f * a));
    ImDrawList_AddTriangleFilled(c + vec2(-r, -r), c + vec2(r, -r), c + vec2(0.0f, r * 0.4f), HudCol(kHudGold, a));
}

void HudObjText(const vec2 &in at, const string &in text, const vec3 &in color, float a) {
    ImDrawList_AddText(at + vec2(1.0f, 1.0f), HudCol(kHudDark, 0.8f * a), text);
    ImDrawList_AddText(at, HudCol(color, a), text);
}

// Off screen: a gold arrow at the edge of a circle round the centre, toward it
void HudObjEdgeArrow(const vec3 &in target, const string &in label) {
    vec3 rel = target - hud_cam_pos;
    vec2 dir(dot(rel, hud_cam_right), -dot(rel, hud_cam_fwd));

    if(length(dir) < 0.01f) {
        return;
    }

    dir = normalize(dir);
    vec2 center(hud_w * 0.5f, hud_h * 0.5f);
    vec2 tip = center + dir * min(hud_w, hud_h) * 0.45f;
    vec2 side(-dir.y, dir.x);
    float size = 14.0f * hud_scale;
    vec2 o(1.5f, 1.5f);
    ImDrawList_AddTriangleFilled(tip + dir * size + o, tip + side * size * 0.65f + o, tip - side * size * 0.65f + o,
        HudCol(kHudDark, 0.6f));
    ImDrawList_AddTriangleFilled(tip + dir * size, tip + side * size * 0.65f, tip - side * size * 0.65f, HudCol(kHudGold, 0.95f));

    if(label != "") {
        vec2 ts = ImGui_CalcTextSize(label);
        HudObjText(tip - dir * (size + 4.0f) - ts * 0.5f, label, kHudGold, 0.9f);
    }
}

bool HudObjOnScreen(const vec3 &in p, vec2 &out s) {
    return HudProject(p, s) && s.x > 0.0f && s.x < hud_w && s.y > 0.0f && s.y < hud_h;
}

vec3 HudObjHead(int id) {
    return ReadCharacterID(id).rigged_object().GetAvgIKChainPos("head") + vec3(0.0f, 0.95f, 0.0f);
}

string hud_obj_logged = "";

void HudDrawObjectives(MovementObject@ player) {
    string now = hud_obj_line == "" ? "" : hud_obj_line + (hud_obj_has_point ? " (marker)" : "") +
        (hud_obj_targets.length() > 0 ? " " + hud_obj_targets.length() + " to defeat" : "") +
        (hud_obj_optional.length() > 0 ? ", " + hud_obj_optional.length() + " optional" : "");

    if(now != hud_obj_logged) {
        hud_obj_logged = now;
        Log(info, "[Overhaul] objective: " + (now == "" ? "none" : now));
    }

    if(hud_obj_line == "") {
        return;
    }

    vec3 from = player !is null ? player.position : hud_cam_pos;
    float bob = sin(the_time * 3.0f) * 3.0f * hud_scale;

    // The line, top left
    vec2 at(22.0f, 20.0f);
    string head = "OBJECTIVE";
    float w = max(ImGui_CalcTextSize(head).x, ImGui_CalcTextSize(hud_obj_line + "   " + hud_obj_sub).x);
    HudRectFilled(at - vec2(10.0f, 6.0f), at + vec2(w + 10.0f, 38.0f), HudCol(kHudDark, 0.4f), 6.0f);
    HudObjText(at, head, kHudGold, 0.85f);
    HudObjText(at + vec2(0.0f, 17.0f), hud_obj_line, kHudWhite, 0.95f);
    HudObjText(at + vec2(ImGui_CalcTextSize(hud_obj_line + "   ").x, 17.0f), hud_obj_sub, kHudGold, 0.9f);

    // The checkpoint
    if(hud_obj_has_point) {
        vec2 s;
        string dist = int(distance(from, hud_obj_point) + 0.5f) + " m";

        if(HudObjOnScreen(hud_obj_point, s)) {
            HudObjDiamond(s + vec2(0.0f, bob), 11.0f * hud_scale, 0.95f);
            vec2 ts = ImGui_CalcTextSize(dist);
            HudObjText(s + vec2(-ts.x * 0.5f, 14.0f * hud_scale + bob), dist, kHudGold, 0.9f);
        } else {
            HudObjEdgeArrow(hud_obj_point, dist);
        }
    }

    // The enemies: a pointer on each in view, the nearest's arrow if none is
    int nearest = -1;
    float nearest_d = 1e9f;
    bool any_seen = false;

    for(uint i = 0; i < hud_obj_targets.length(); ++i) {
        vec3 p = HudObjHead(hud_obj_targets[i]);
        vec2 s;
        float d = distance(from, p);

        if(d < nearest_d) {
            nearest_d = d;
            nearest = hud_obj_targets[i];
        }

        if(HudObjOnScreen(p, s)) {
            HudObjPointer(s + vec2(0.0f, bob), 8.0f * hud_scale, 0.95f);
            any_seen = true;
        }
    }

    if(nearest != -1 && !any_seen) {
        HudObjEdgeArrow(HudObjHead(nearest), int(nearest_d + 0.5f) + " m");
    }

    for(uint i = 0; i < hud_obj_optional.length(); ++i) {
        vec2 s;

        if(HudObjOnScreen(HudObjHead(hud_obj_optional[i]), s)) {
            HudObjPointer(s + vec2(0.0f, bob), 6.0f * hud_scale, 0.45f);
        }
    }
}

// ---- minimap ------------------------------------------------------------------------

void HudDrawMinimap(MovementObject@ player, int player_id) {
    float r = hud_map_size * 0.5f * hud_scale;
    vec2 c(hud_w - r - 18.0f, r + 18.0f);
    vec3 origin = player !is null ? player.position : hud_cam_pos;
    vec3 fwd = hud_map_rotate ? camera.GetFlatFacing() : vec3(0.0f, 0.0f, -1.0f);

    if(length_squared(fwd) < 0.01f) {
        fwd = vec3(0.0f, 0.0f, -1.0f);
    }

    fwd = normalize(fwd);
    vec3 right(-fwd.z, 0.0f, fwd.x);
    float scale = r / hud_map_range;

    ImDrawList_AddCircleFilled(c, r, HudCol(kHudDark, 0.55f), 40);
    ImDrawList_AddCircle(c, r, HudCol(kHudWhite, 0.35f), 40, 1.5f);
    ImDrawList_AddCircle(c, r * 0.5f, HudCol(kHudWhite, 0.12f), 32, 1.0f);

    // Weapons lying about
    if(hud_map_items) {
        for(int i = 0, len = GetNumItems(); i < len; ++i) {
            ItemObject@ item = ReadItem(i);

            if(item.GetType() != _weapon || item.IsHeld() || item.StuckInWhom() != -1) {
                continue;
            }

            vec3 rel = item.GetPhysicsPosition() - origin;
            vec2 m(dot(rel, right), -dot(rel, fwd));

            if(length(m) < hud_map_range) {
                HudRectFilled(c + m * scale - vec2(1.5f), c + m * scale + vec2(1.5f), HudCol(kHudGold, 0.8f));
            }
        }
    }

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == player_id || char.static_char) {
            continue;
        }

        vec3 rel = char.position - origin;
        vec2 m(dot(rel, right), -dot(rel, fwd));

        if(length(m) > hud_map_range) {
            continue;
        }

        vec2 p = c + m * scale;
        bool awake = char.GetIntVar("knocked_out") == _awake;
        vec3 color = HudColorOf(char, player);

        if(awake) {
            ImDrawList_AddCircleFilled(p, 3.5f * hud_scale, HudCol(color, 0.95f), 10);
        } else {
            ImDrawList_AddCircleFilled(p, 2.0f * hud_scale, HudCol(kHudGrey, 0.6f), 8);
        }
    }

    // The objective: the checkpoint (on the rim when farther), rings on the targets
    if(hud_objectives && hud_obj_line != "") {
        if(hud_obj_has_point) {
            vec3 rel = hud_obj_point - origin;
            vec2 m(dot(rel, right), -dot(rel, fwd));

            if(length(m) > hud_map_range * 0.92f) {
                m = normalize(m) * hud_map_range * 0.92f;
            }

            HudObjDiamond(c + m * scale, 6.0f * hud_scale, 1.0f);
        }

        for(uint i = 0; i < hud_obj_targets.length(); ++i) {
            vec3 rel = ReadCharacterID(hud_obj_targets[i]).position - origin;
            vec2 m(dot(rel, right), -dot(rel, fwd));

            if(length(m) < hud_map_range) {
                ImDrawList_AddCircle(c + m * scale, 6.0f * hud_scale, HudCol(kHudGold, 0.95f), 12, 1.5f);
            }
        }
    }

    // Us, at the centre, pointing where we face
    // Level scripts can't read a character's facing: where it moves, else the camera
    vec3 face = camera.GetFlatFacing();

    if(player !is null && length_squared(vec3(player.velocity.x, 0.0f, player.velocity.z)) > 0.5f) {
        face = vec3(player.velocity.x, 0.0f, player.velocity.z);
    }

    vec2 f(dot(face, right), -dot(face, fwd));
    f = length(f) > 0.01f ? normalize(f) : vec2(0.0f, -1.0f);
    vec2 side(-f.y, f.x);
    float s = 7.0f * hud_scale;
    ImDrawList_AddTriangleFilled(c + f * s, c - f * s * 0.6f + side * s * 0.6f, c - f * s * 0.6f - side * s * 0.6f, HudCol(kHudWhite, 1.0f));
}

// ---- the player's panel -------------------------------------------------------------

void HudDrawPlayer(MovementObject@ player) {
    float w = 240.0f * hud_scale;
    vec2 at(22.0f, hud_h - 70.0f * hud_scale);
    float temp = player.GetFloatVar("temp_health");
    float perm = player.GetFloatVar("permanent_health");
    float blood = player.GetFloatVar("blood_health");
    float block = player.GetFloatVar("block_health");

    HudRectFilled(at - vec2(10.0f, 10.0f), at + vec2(w + 10.0f, 52.0f * hud_scale), HudCol(kHudDark, 0.35f), 6.0f);
    HudBar(at, w, 12.0f * hud_scale, temp, perm, HudHealthColor(temp));

    if(blood < 0.99f) {
        HudBar(at + vec2(0.0f, 16.0f * hud_scale), w, 4.0f * hud_scale, blood, 1.0f, vec3(0.7f, 0.05f, 0.05f));  // bleeding
    }

    HudBar(at + vec2(0.0f, 23.0f * hud_scale), w * 0.5f, 4.0f * hud_scale, block, 1.0f, vec3(0.35f, 0.6f, 0.95f));

    string line = "";
    int weap = HudWeaponOf(player);

    if(weap != -1 && ObjectExists(weap)) {
        string label = ReadItemID(weap).GetLabel();
        line = label == "" ? "weapon" : label;
    }

    int shield = player.GetIntVar("ko_shield");

    if(player.GetIntVar("max_ko_shield") > 0) {
        line += (line == "" ? "" : "   ") + "shield " + shield;
    }

    if(line != "") {
        ImDrawList_AddText(at + vec2(0.0f, 30.0f * hud_scale), HudCol(kHudWhite, 0.8f), line);
    }

    if(hud_dealt > 0.0f || hud_taken > 0.0f || hud_kos > 0) {
        // This level's tally, just above the panel
        string tally = "dealt " + int(hud_dealt + 0.5f) + "   taken " + int(hud_taken + 0.5f) + "   KOs " + hud_kos;
        vec2 t = at - vec2(0.0f, 34.0f);
        HudRectFilled(t - vec2(10.0f, 4.0f), t + vec2(ImGui_CalcTextSize(tally).x + 10.0f, 18.0f),
            HudCol(kHudDark, 0.35f), 6.0f);
        ImDrawList_AddText(t, HudCol(kHudGold, 0.9f), tally);
    }
}

// ---- entry point (DrawGUI) ------------------------------------------------------------

void OverhaulHudDraw() {
    if(the_time >= hud_settings_next) {
        HudRefreshSettings();
    }

    if(EditorModeActive() || level.DialogueCameraControl()) {
        return;
    }

    HudSetUpCamera();
    int player_id = HudPlayerId();
    MovementObject@ player = null;

    if(player_id != -1) {
        @player = ReadCharacterID(player_id);
    }
    bool spectating = BattleSpectating();

    if(spectating) {
        @player = null;  // parked out of sight: the camera is the point of view
    }

    float cine = HudCinematic(player);
    bool shown = hud_on && cine < 0.02f;  // a finisher shot hides the HUD

    if(!hud_on && cine <= 0.0f) {
        return;
    }

    ImGui_SetNextWindowPos(vec2(0.0f, 0.0f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(hud_w, hud_h), ImGuiSetCond_Always);
    ImGui_PushStyleColor(ImGuiCol_WindowBg, vec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui_Begin("overhaul_hud", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if(hud_on) {
        HudTrack(player, spectating ? -1 : player_id);
    }

    if(hud_on && hud_objectives && !spectating) {
        HudObjUpdate(player, player_id);
    } else {
        hud_obj_line = "";
    }

    if(shown && hud_objectives) {
        HudDrawObjectives(player);
    }

    if(shown && hud_tags) {
        HudDrawTags(player, spectating ? -1 : player_id);
    }

    if(hud_on && hud_dmg) {
        HudDrawNumbers();
    }

    if(shown && hud_feed) {
        HudDrawFeed();
    }

    if(shown) {
        TrainerHudLine();
    }

    if(shown && hud_threats) {
        HudDrawThreats(player, player_id);
    }

    if(shown && hud_map) {
        HudDrawMinimap(player, spectating ? -1 : player_id);
    }

    if(shown && hud_health && player !is null && player.GetIntVar("knocked_out") == _awake) {
        HudDrawPlayer(player);
    }

    if(cine > 0.0f) {
        HudDrawLetterbox(cine);
    }

    ImGui_End();
    ImGui_PopStyleColor();
}
