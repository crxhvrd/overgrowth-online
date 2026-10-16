//-----------------------------------------------------------------------------
//           Name: overhaul_finisher.as
//    Description: Finisher camera trigger, run by the level hook. Every
//                 knockout and death sends a level message (aschar.as
//                 SetKnockedOut); when it is yours to watch (you finished an
//                 enemy, or you went down) the player's own camera script
//                 plays a short shot (aschar.as OcamFinisher*). Settings:
//                 Camera tab, ofin_* keys.
//-----------------------------------------------------------------------------

const array<string> kFinisherModes = {"Off", "Last enemy only", "Sometimes", "Every knockout"};

const int kSeqDeath = 6;    // aschar.as kOfinSequences

// The shot starts on the next update, never from inside the knockout call
int fin_pending = -1;
int fin_pending_attacker = -1;
bool fin_pending_last = false;
float fin_next_ok = 0.0f;        // ui_time: one shot at a time, a breather after it
array<int> fin_seen;             // each victim once (a knockout can turn into a death)
array<float> fin_seen_time;

void FinisherReceive(const string &in message) {
    if(message.findFirst("character_knocked_out ") != 0 && message.findFirst("character_died ") != 0) {
        return;
    }

    array<string>@ parts = message.split(" ");

    if(parts.length() >= 2) {
        FinisherConsider(atoi(parts[1]));
    }
}

bool FinisherSeen(int id) {
    for(int i = int(fin_seen.length()) - 1; i >= 0; --i) {
        if(ui_time - fin_seen_time[i] > 10.0f) {
            fin_seen.removeAt(i);
            fin_seen_time.removeAt(i);
        }
    }

    if(fin_seen.find(id) >= 0) {
        return true;
    }

    fin_seen.insertLast(id);
    fin_seen_time.insertLast(ui_time);
    return false;
}

void FinisherConsider(int victim_id) {
    if(FinisherSeen(victim_id) || !MovementObjectExists(victim_id) || fin_pending != -1 || ui_time < fin_next_ok) {
        return;
    }

    if(!CfgBool("ocam_enabled", true) || EditorModeActive() || level.DialogueCameraControl() || BattleSpectating()) {
        return;
    }

    int player_id = HudPlayerId();

    if(player_id == -1) {
        return;
    }

    MovementObject@ player = ReadCharacterID(player_id);
    MovementObject@ victim = ReadCharacterID(victim_id);
    int attacker = victim.GetIntVar("attacked_by_id");
    bool recent = victim.GetFloatVar("time") - victim.GetFloatVar("last_damaged_time") < 1.5f;

    if(victim_id == player_id) {
        if(CfgBool("ofin_death", true)) {
            fin_pending_attacker = (recent && attacker != player_id && MovementObjectExists(attacker)) ? attacker : -1;
            fin_pending_last = false;
            fin_pending = victim_id;
        }

        return;
    }

    int mode = CfgInt("ofin_mode", 2);

    if(mode == 0 || attacker != player_id || !recent || player.OnSameTeam(victim) || player.GetIntVar("knocked_out") != _awake) {
        return;
    }

    // Who is still standing against you
    int others = 0;
    bool close = false;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == victim_id || char.controlled || char.static_char || player.OnSameTeam(char) ||
                char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        float dist = distance(char.position, player.position);

        if(dist < 25.0f) {
            ++others;
        }

        if(dist < 5.0f) {
            close = true;  // never take the camera away while someone can hit you
        }
    }

    bool last = others == 0;
    bool go = last || (!close && (mode == 3 || (mode == 2 && RangedRandomFloat(0.0f, 1.0f) < CfgFloat("ofin_chance", 0.35f))));

    if(!go) {
        return;
    }

    fin_pending_attacker = player_id;
    fin_pending_last = last;
    fin_pending = victim_id;
}

// ---- first-person key ------------------------------------------------------------------

const array<string> kFirstPersonKeys = {"None", "v", "h", "c", "f6"};

void FirstPersonKey() {
    int key = CfgInt("ocam_fp_key", 1);

    if(menu_open || key <= 0 || key >= int(kFirstPersonKeys.length()) || !GetInputPressed(0, kFirstPersonKeys[key])) {
        return;
    }

    SetConfigValueBool("ocam_fp", !CfgBool("ocam_fp", false));
    int player_id = HudPlayerId();

    if(player_id != -1) {
        ReadCharacterID(player_id).Execute("ocam_read_countdown = 0;");  // now, not in 15 updates
    }
}

void FinisherUpdate() {
    FirstPersonKey();

    if(fin_pending == -1) {
        return;
    }

    int victim_id = fin_pending;
    fin_pending = -1;
    int player_id = HudPlayerId();

    if(player_id == -1 || !MovementObjectExists(victim_id)) {
        return;
    }

    // One update after the knockout the body has its launch: pick the shots by it
    MovementObject@ victim = ReadCharacterID(victim_id);
    bool fast = length(victim.velocity) > 6.0f;
    float r = RangedRandomFloat(0.0f, 1.0f);
    int seq;

    if(victim_id == player_id) {
        seq = kSeqDeath;
    } else if(fin_pending_last) {
        seq = fast ? 4 : (r < 0.5f ? 3 : 5);  // two shots for the last one
    } else if(fast) {
        seq = r < 0.55f ? 2 : 4;
    } else {
        seq = r < 0.4f ? 0 : (r < 0.75f ? 1 : 3);
    }

    float shot_len = max(1.0f, min(4.0f, CfgFloat("ofin_length", 2.2f)));

    if(seq >= 3 && seq <= 5) {
        shot_len *= 1.25f;  // two shots
    }

    if(seq == kSeqDeath) {
        shot_len *= 1.4f;
    }

    ReadCharacterID(player_id).Execute("OcamFinisherStart(" + victim_id + ", " + fin_pending_attacker + ", " + seq + ", " + shot_len + ");");

    // Speed ramp: hit-stop, deep slow motion, ease back (the engine smooths between).
    // Not online: the host's would slow everyone's game, ours our own character.
    float s = max(0.0f, min(1.0f, CfgFloat("ofin_slowmo_amt", 0.85f)));

    if(s > 0.01f && !MPOnline()) {
        if(seq == kSeqDeath) {
            TimedSlowMotion(mix(1.0f, 0.2f, s), shot_len * 0.8f, 0.0f);
        } else {
            TimedSlowMotion(mix(1.0f, 0.03f, s), 0.12f, 0.0f);
            TimedSlowMotion(mix(1.0f, 0.12f, s), shot_len * 0.45f, 0.0f);
            TimedSlowMotion(mix(1.0f, 0.4f, s), shot_len * 0.9f, 0.0f);
        }
    }

    fin_next_ok = ui_time + shot_len + 1.5f;
}
