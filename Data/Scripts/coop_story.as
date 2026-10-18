//-----------------------------------------------------------------------------
//           Name: coop_story.as
//    Script Type: Level (included by level.as)
//        License: Apache-2.0
//    Description: Overgrowth Online co-op, the host's side. The weapons the
//                 host's character starts a level with: every guest gets the
//                 same ones (copies, in the same hands and sheaths) a moment
//                 after their character appears, and again after the level
//                 starts over if theirs are gone.
//-----------------------------------------------------------------------------

const float kCoopArmDelay = 3.0f;  // (a guest's own character choice drops weapons when it applies)
const float kCoopStartRead = 2.0f;  // the level's first seconds: what the host's character holds

int coop_host_char = -1;            // the host's character (the level's player)
float coop_level_start = -1.0f;
array<string> coop_start_files;     // its starting weapons: file
array<int> coop_start_slots;        //   and aschar.as weapon slot (_held_left .. _sheathed_right)

array<int> coop_seen_ids;           // guests' characters: first seen at
array<float> coop_seen_times;
array<int> coop_armed_ids;          // given their copies

array<int> coop_copy_owner;         // the copies made: whose, which slot, the item
array<int> coop_copy_slot;
array<int> coop_copy_item;

void CoopStoryUpdate() {
    if(!Online_IsHosting()) {
        return;
    }
    if(coop_level_start < 0.0f) {
        coop_level_start = the_time;
        coop_host_char = GetPlayerCharacterID() == -1 ? -1 : ReadCharacter(GetPlayerCharacterID()).GetID();
    }
    if(coop_host_char == -1 || !MovementObjectExists(coop_host_char)) {
        return;
    }

    // What the host's character holds as the level starts
    if(the_time - coop_level_start < kCoopStartRead) {
        MovementObject@ host = ReadCharacterID(coop_host_char);
        for(int slot = 0; slot < 4; ++slot) {
            int item = host.GetArrayIntVar("weapon_slots", slot);
            if(item != -1 && ObjectExists(item) && coop_start_slots.find(slot) < 0) {
                string file = Online_ObjectFile(item);
                if(file != "") {
                    coop_start_files.insertLast(file);
                    coop_start_slots.insertLast(slot);
                    Log(info, "Overgrowth Online: the host starts with " + file + " (slot " + slot + ")");
                }
            }
        }
        return;
    }
    if(coop_start_files.length() == 0) {
        return;
    }

    // Each guest, a moment after their character appears
    array<PlayerState>@ states = GetPlayerStates();
    for(uint k = 0; k < states.length(); ++k) {
        int id = states[k].avatar_id;
        if(id < 0 || id == coop_host_char || !MovementObjectExists(id) || coop_armed_ids.find(id) >= 0) {
            continue;
        }
        int seen = coop_seen_ids.find(id);
        if(seen < 0) {
            coop_seen_ids.insertLast(id);
            coop_seen_times.insertLast(the_time);
            continue;
        }
        if(the_time - coop_seen_times[seen] < kCoopArmDelay) {
            continue;
        }
        coop_armed_ids.insertLast(id);
        CoopArm(id);
    }
}

// The host's starting weapons for a guest, in the slots that are empty
void CoopArm(int id) {
    MovementObject@ mo = ReadCharacterID(id);
    if(mo.GetIntVar("species") == 1) {
        return;  // (aschar.as _wolf: no hands)
    }
    for(uint w = 0; w < coop_start_files.length(); ++w) {
        int slot = coop_start_slots[w];
        if(mo.GetArrayIntVar("weapon_slots", slot) != -1) {
            continue;
        }

        // This guest's copy for the slot again, if nobody holds it; else a new one
        int item = -1;
        for(uint c = 0; c < coop_copy_item.length(); ++c) {
            if(coop_copy_owner[c] == id && coop_copy_slot[c] == slot && ObjectExists(coop_copy_item[c]) &&
               !ReadItemID(coop_copy_item[c]).IsHeld()) {
                item = coop_copy_item[c];
            }
        }
        if(item == -1) {
            item = CreateObject(coop_start_files[w], true);
            ReadObjectFromID(item).GetScriptParams().AddIntCheckbox("No Save", true);
            coop_copy_owner.insertLast(id);
            coop_copy_slot.insertLast(slot);
            coop_copy_item.insertLast(item);
        }
        Object@ obj = ReadObjectFromID(item);
        obj.SetTranslation(mo.position);
        // aschar.as HandleEditorAttachment: grip mirrored = left hand, sheathe mirrored = right side
        bool grip = slot < 2;
        bool mirror = grip ? slot == 0 : slot == 3;
        ReadObjectFromID(id).AttachItem(obj, grip ? _at_grip : _at_sheathe, mirror);
        Log(info, "Overgrowth Online: guest character " + id + " gets " + coop_start_files[w] + " (slot " + slot + ")");
    }
}

// The level starts over: the guests get theirs again where they lost them
void CoopStoryMessage(const string &in token) {
    if(token == "reset" && Online_IsHosting()) {
        coop_seen_ids.resize(0);
        coop_seen_times.resize(0);
        coop_armed_ids.resize(0);
    }
}
