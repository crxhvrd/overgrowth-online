//-----------------------------------------------------------------------------
//           Name: coop_revive.as
//    Script Type: Level (included by level.as)
//    Description: Overgrowth Online: reviving a downed teammate. Drag their
//                 body (crouch + hold grab) for kReviveSeconds and they get
//                 up. Only another player's character, on a team of yours
//                 (story co-op: everyone; the arenas: nobody).
//
//                 The reviver's machine counts (its own character drags, the
//                 body is its for the drag) and tells the others:
//                   ohl coop_revive #<reviver> #<downed> <percent>   (-1: stopped)
//                   ohl coop_revived #<reviver> #<downed>
//                 A guest says it to the host ("ohr ..."), which passes it on.
//                 When done the reviver lets go; the body goes back to its
//                 machine, and the host has that machine stand it up
//                 (Online_PlaceAvatar waits for the body to come back).
//        License: Apache-2.0
//-----------------------------------------------------------------------------

const float kReviveSeconds = 3.0f;
const int kReviveDragBody = 3;  // aschar.as _TETHERED_DRAGBODY

int revive_target = -1;       // the downed teammate we drag (ours)
float revive_time = 0.0f;
float revive_next_share = 0.0f;
float revive_lost_at = -1.0f;  // the drag slipped at (-1 it holds)

// What the others are doing (from messages)
int revive_seen_reviver = -1;
int revive_seen_target = -1;
float revive_seen_pct = 0.0f;
float revive_seen_time = -10.0f;

string revive_flash = "";     // "You revived ..." / "... revived you"
float revive_flash_until = 0.0f;

int revive_me = -1;           // our player's character (the engine's "mp_role ... #<me>")
int revive_block_target = -1; // just revived: a hand still on the key doesn't take it again
float revive_block_until = 0.0f;

// Not "the character we run": a body we drag is ours for the drag too
int ReviveOwnCharacter() {
    if(revive_me != -1 && MovementObjectExists(revive_me)) {
        return revive_me;
    }
    int num = GetNumCharacters();
    for(int i = 0; i < num; ++i) {
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled && !char.remote) {
            return char.GetID();
        }
    }
    return -1;
}

bool ReviveIsPlayer(int id) {
    array<PlayerState>@ states = GetPlayerStates();
    for(uint k = 0; k < states.length(); ++k) {
        if(states[k].avatar_id == id) {
            return true;
        }
    }
    return false;
}

string ReviveName(int id) {
    array<PlayerState>@ states = GetPlayerStates();
    for(uint k = 0; k < states.length(); ++k) {
        if(states[k].avatar_id == id && states[k].playername != "") {
            return states[k].playername;
        }
    }
    return "Your teammate";
}

bool ReviveSameTeam(int a, int b) {
    string ta = ReadObjectFromID(a).GetScriptParams().GetString("Teams");
    string tb = ReadObjectFromID(b).GetScriptParams().GetString("Teams");
    array<string> la = ta.split(",");
    array<string> lb = tb.split(",");
    for(uint i = 0; i < la.length(); ++i) {
        string x = la[i];
        x = join(x.split(" "), "");
        if(x == "") {
            continue;
        }
        for(uint j = 0; j < lb.length(); ++j) {
            string y = lb[j];
            y = join(y.split(" "), "");
            if(x == y) {
                return true;
            }
        }
    }
    return false;
}

// Says it to everyone: the host directly, a guest through the host
void ReviveShare(const string &in what) {
    if(Online_IsHosting()) {
        level.SendMessage("ohl " + what);
    } else {
        level.SendMessage("ohr " + what);
    }
}

// level.as Update
void CoopReviveUpdate() {
    if(!Online_IsActive()) {
        revive_target = -1;
        return;
    }

    int me = ReviveOwnCharacter();
    int target = -1;
    if(me != -1 && the_time < revive_block_until) {
        MovementObject@ mo = ReadCharacterID(me);
        if(mo.GetIntVar("tethered") == kReviveDragBody && mo.GetIntVar("tether_id") == revive_block_target) {
            mo.Execute("UnTether();");
        }
    }
    if(me != -1) {
        MovementObject@ mo = ReadCharacterID(me);
        if(mo.GetIntVar("knocked_out") == _awake && mo.GetIntVar("tethered") == kReviveDragBody) {
            int t = mo.GetIntVar("tether_id");
            if(t != -1 && t != me && MovementObjectExists(t) && ReviveIsPlayer(t) && ReviveSameTeam(me, t) &&
                    ReadCharacterID(t).GetIntVar("knocked_out") != _awake) {
                target = t;
            }
        }
        // Started with a drag, it goes on while the hand stays on the body: a
        // drag of another player's body slips (its machine hands it over)
        if(target == -1 && revive_target != -1 && MovementObjectExists(revive_target) &&
                mo.GetIntVar("knocked_out") == _awake && GetInputDown(mo.controller_id, "grab") &&
                ReadCharacterID(revive_target).GetIntVar("knocked_out") != _awake &&
                distance(mo.position, ReadCharacterID(revive_target).position) < 1.5f) {
            target = revive_target;
        }
    }

    // A drag that slips for a moment (it does, as it starts) keeps its progress
    if(target == -1 && revive_target != -1) {
        if(revive_lost_at < 0.0f) {
            revive_lost_at = the_time;
        }
        if(the_time - revive_lost_at < 0.5f) {
            return;
        }
    }
    if(target != revive_target) {
        if(me != -1) {  // (why it starts or stops, for logs)
            MovementObject@ mo = ReadCharacterID(me);
            int t = mo.GetIntVar("tether_id");
            Log(info, "[coop_revive] me " + me + " tethered " + mo.GetIntVar("tethered") + " tether_id " + t +
                (t != -1 && MovementObjectExists(t) ? " body ko " + ReadCharacterID(t).GetIntVar("knocked_out") +
                 " state " + ReadCharacterID(t).GetIntVar("state") : "") +
                (revive_target != -1 && MovementObjectExists(revive_target) ? " (was: state " + ReadCharacterID(revive_target).GetIntVar("state") +
                 " at " + distance(mo.position, ReadCharacterID(revive_target).position) + " m)" : "") +
                ": target " + revive_target + " -> " + target + " after " + revive_time + " s");
        }
        if(revive_target != -1 && me != -1) {
            ReviveShare("coop_revive #" + me + " #" + revive_target + " -1");  // stopped
        }
        revive_target = target;
        revive_time = 0.0f;
        revive_next_share = 0.0f;
    }
    revive_lost_at = -1.0f;
    if(target == -1) {
        return;
    }

    revive_time += time_step;
    if(the_time >= revive_next_share) {
        revive_next_share = the_time + 0.25f;
        ReviveShare("coop_revive #" + me + " #" + target + " " + int(100.0f * min(1.0f, revive_time / kReviveSeconds)));
    }

    if(revive_time >= kReviveSeconds) {
        // Let go: the body goes back to its machine (a second once settled),
        // which stands it up itself (its own script and physics agree there;
        // stood up here, it came back unable to move). Don't take it again meanwhile
        ReadCharacterID(me).Execute("UnTether();");
        Online_ReleaseTakeOver(target);  // now (not once settled): it gets up there
        revive_block_target = target;
        revive_block_until = the_time + 3.0f;
        if(Online_IsHosting()) {
            ReviveDo(me, target);
        } else {
            ReviveShare("coop_revive_done #" + me + " #" + target);
        }
        revive_flash = "You revived " + ReviveName(target) + "!";
        revive_flash_until = the_time + 2.5f;
        revive_target = -1;
        revive_time = 0.0f;
    }
}

// Host: stand the downed one up where it lies, tell everyone
void ReviveDo(int reviver, int target) {
    if(!MovementObjectExists(target) || ReadCharacterID(target).GetIntVar("knocked_out") == _awake) {
        return;
    }
    // A guest's word: only a player's character, by another player who
    // stands beside it (not oneself from afar, not an enemy back up)
    if(!Online_IsPlayerCharacter(target) || reviver == target || !MovementObjectExists(reviver) ||
       !Online_IsPlayerCharacter(reviver) || ReadCharacterID(reviver).GetIntVar("knocked_out") != _awake ||
       distance(ReadCharacterID(reviver).position, ReadCharacterID(target).position) > 3.0f) {
        return;
    }
    // Where it lies, on safe ground; else beside the reviver (water, a ledge)
    MovementObject@ body = ReadCharacterID(target);
    vec3 spot;
    const vec3 any_way(0.0f, 0.0f, 1.0f);  // (which side it tries first)
    if(!Online_FindSafeSpot(body.position, any_way, 0, spot) &&
       !(MovementObjectExists(reviver) && Online_FindSafeSpot(ReadCharacterID(reviver).position, any_way, 1, spot))){
        spot = body.position + vec3(0.0f, 0.3f, 0.0f);
    }
    Online_PlaceAvatar(target, spot, vec3(0.0f));
    level.SendMessage("ohl coop_revived #" + reviver + " #" + target);
}

int ReviveId(const string &in tok) {
    if(tok.length() < 2 || tok.substr(0, 1) != "#") {
        return -1;
    }
    return atoi(tok.substr(1));
}

// level.as ReceiveMessage: true if it was ours
bool CoopReviveMessage(const string &in msg) {
    if(msg.length() > 9 && msg.substr(0, 8) == "mp_role ") {  // "mp_role host|client #<me> ..." (theirs too)
        TokenIterator r;
        r.Init();
        if(r.FindNextToken(msg) && r.FindNextToken(msg)) {
            string role = r.GetToken(msg);
            revive_me = (role != "none" && r.FindNextToken(msg)) ? ReviveId(r.GetToken(msg)) : -1;
        }
        return false;
    }
    if(msg.length() < 5 || (msg.substr(0, 4) != "ohl " && msg.substr(0, 4) != "ohr ") || msg.findFirst("coop_reviv") < 0) {
        return false;
    }
    TokenIterator it;
    it.Init();
    array<string> t;
    while(it.FindNextToken(msg) && t.length() < 7) {
        t.insertLast(it.GetToken(msg));
    }
    if(t.length() < 2) {
        return false;
    }

    // Host: a guest's word, passed on ("ohr <peer> coop_revive ...")
    if(t[0] == "ohr" && t.length() >= 5 && Online_IsHosting()) {
        if(t[2] == "coop_revive" && t.length() >= 6) {
            level.SendMessage("ohl coop_revive " + t[3] + " " + t[4] + " " + t[5]);
            return true;
        }
        if(t[2] == "coop_revive_done") {
            ReviveDo(ReviveId(t[3]), ReviveId(t[4]));
            return true;
        }
        if(t[2] == "coop_revived") {
            level.SendMessage("ohl coop_revived " + t[3] + " " + t[4]);
            return true;
        }
        return false;
    }

    if(t[0] != "ohl" || t.length() < 4) {
        return false;
    }
    int me = ReviveOwnCharacter();
    if(t[1] == "coop_revive" && t.length() >= 5) {
        int reviver = ReviveId(t[2]);
        int target = ReviveId(t[3]);
        int pct = atoi(t[4]);
        if(reviver == me) {
            return true;  // (we draw our own from our count)
        }
        if(pct < 0) {
            if(revive_seen_target == target) {
                revive_seen_target = -1;
            }
        } else {
            revive_seen_reviver = reviver;
            revive_seen_target = target;
            revive_seen_pct = pct;
            revive_seen_time = the_time;
        }
        return true;
    }
    if(t[1] == "coop_revived") {
        int reviver = ReviveId(t[2]);
        int target = ReviveId(t[3]);
        if(target == me && me != -1) {
            revive_flash = ReviveName(reviver) + " revived you!";
            revive_flash_until = the_time + 2.5f;
        }
        if(revive_seen_target == target) {
            revive_seen_target = -1;
        }
        return true;
    }
    return false;
}

// ImGui 1.85 divides by zero on a rounded corner of a radius just over 0 (the
// bar barely started): round only rects big enough for it
void ReviveRectFilled(const vec2 &in a, const vec2 &in b, uint col, float rounding = 0.0f) {
    float side = min(max(b.x - a.x, a.x - b.x), max(b.y - a.y, a.y - b.y));
    ImDrawList_AddRectFilled(a, b, col, side >= rounding * 2.0f + 4.0f ? rounding : 0.0f);
}

uint ReviveCol(const vec3 &in c, float a) {
    uint r = uint(max(0.0f, min(1.0f, c.x)) * 255.0f);
    uint g = uint(max(0.0f, min(1.0f, c.y)) * 255.0f);
    uint b = uint(max(0.0f, min(1.0f, c.z)) * 255.0f);
    uint al = uint(max(0.0f, min(1.0f, a)) * 255.0f);
    return (al << 24) | (b << 16) | (g << 8) | r;
}

// level.as DrawGUI: a bar under the middle of the screen
void CoopReviveDraw() {
    if(!Online_IsActive()) {
        return;
    }
    int me = ReviveOwnCharacter();
    string label = "";
    float fraction = -1.0f;

    if(revive_target != -1) {
        label = "Reviving " + ReviveName(revive_target) + "...";
        fraction = min(1.0f, revive_time / kReviveSeconds);
    } else if(revive_seen_target != -1 && revive_seen_target == me && the_time - revive_seen_time < 0.75f) {
        label = ReviveName(revive_seen_reviver) + " is reviving you...";
        fraction = revive_seen_pct / 100.0f;
    } else if(revive_seen_target != -1 && the_time - revive_seen_time < 0.75f) {
        label = ReviveName(revive_seen_reviver) + " is reviving " + ReviveName(revive_seen_target) + "...";
        fraction = revive_seen_pct / 100.0f;
    } else if(the_time < revive_flash_until) {
        label = revive_flash;
    }
    if(label == "") {
        return;
    }

    float w = float(GetScreenWidth());
    float h = float(GetScreenHeight());
    ImGui_SetNextWindowPos(vec2(0.0f, 0.0f), ImGuiSetCond_Always);
    ImGui_SetNextWindowSize(vec2(w, h), ImGuiSetCond_Always);
    ImGui_PushStyleColor(ImGuiCol_WindowBg, vec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui_Begin("coop_revive", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui_SetWindowFontScale(1.6f);

    float bar_w = min(420.0f, w * 0.4f);
    float bar_h = 14.0f;
    vec2 at(w * 0.5f - bar_w * 0.5f, h * 0.68f);
    vec2 size = ImGui_CalcTextSize(label);
    vec2 text_at(w * 0.5f - size.x * 0.5f, at.y - size.y - 8.0f);
    ImDrawList_AddText(text_at + vec2(2.0f, 2.0f), ReviveCol(vec3(0.0f), 0.8f), label);
    ImDrawList_AddText(text_at, ReviveCol(vec3(1.0f), 1.0f), label);
    if(fraction >= 0.0f) {
        ReviveRectFilled(at - vec2(2.0f, 2.0f), at + vec2(bar_w + 2.0f, bar_h + 2.0f), ReviveCol(vec3(0.0f), 0.7f), 3.0f);
        ReviveRectFilled(at, at + vec2(bar_w * fraction, bar_h), ReviveCol(vec3(0.35f, 0.85f, 0.4f), 0.95f), 3.0f);
    }

    ImGui_End();
    ImGui_PopStyleColor();
}
