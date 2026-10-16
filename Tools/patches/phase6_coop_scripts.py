"""Phase 6, part 6: the Overgrowth story level scripts for online co-op
(found by a read-only audit of the story scripts, 2026-10-15).

Online every player's character is "controlled"; ours is the one that is
not "remote". Level and hotspot scripts run on every machine.

- threatcheck.as: GetPlayerCharacterID() = our own character (offline the
  player, as before); AllPlayersDown().
- overgrowth_level.as:
  - goals are the host's: clients don't evaluate them; the host sends its
    progress ("ohl og_progress N", at each step and every 5 s) and clients
    follow, so a level reset puts everyone at the same spawn;
  - goals and the level win stop only when every player is down (they used
    to stop while the host's character was down);
  - a step reached revives downed players (engine: "coop_checkpoint");
  - the defeat-goal heal reaches every player ("ohl og_heal");
  - a fall-death checkpoint kills the character that fell, on its machine
    (it used to kill the host's);
  - respawn spots are spread out per player;
  - the death prompt explains the co-op rules.
- overgrowth_level_checkpoint.as: says who fell.
- level.as: clients don't start dialogues themselves (the host's start
  reaches them; a client's own got stuck waiting for the host), and a
  client's character stands still while a dialogue plays.
Run from the repo root."""
D = "Data/Scripts/"


def patch(p, pairs):
    s = open(D + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(D + p, "w", encoding="utf-8", newline="").write(s)


patch("threatcheck.as", [
("""int GetPlayerCharacterID() {
    int num = GetNumCharacters();
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            return i;
        }
    }
    return -1;
}""", """// Our own character: online every player's is "controlled", ours is not "remote"
int GetPlayerCharacterID() {
    int num = GetNumCharacters();
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled && !char.remote){
            return i;
        }
    }
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            return i;
        }
    }
    return -1;
}

// Every player's character is down (co-op: the level stops only then)
bool AllPlayersDown() {
    int num = GetNumCharacters();
    bool any = false;
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            any = true;
            if(char.GetIntVar("knocked_out") == _awake){
                return false;
            }
        }
    }
    return any;
}"""),
])

# hotspots/overgrowth_level.as: see phase6_coop_level.py

patch("hotspots/overgrowth_level_checkpoint.as", [
("""                obj.ReceiveScriptMessage("player_entered_checkpoint_fall_death "+checkpoint_id);""",
 """                obj.ReceiveScriptMessage("player_entered_checkpoint_fall_death "+checkpoint_id+" "+mo.GetID());  // (who fell)"""),
])

patch("level.as", [
("""    } else if(token == "start_dialogue"){
        token_iter.FindNextToken(msg);
        dialogue_queue.push_back(token_iter.GetToken(msg));
    } else if(token == "start_dialogue_fade"){
        token_iter.FindNextToken(msg);
        dialogue_queue.push_back(token_iter.GetToken(msg));""", """    } else if(token == "start_dialogue"){
        token_iter.FindNextToken(msg);
        if(!Online_IsClient()){  // online the host starts it everywhere (a client's own waited for the host forever)
            dialogue_queue.push_back(token_iter.GetToken(msg));
        }
    } else if(token == "start_dialogue_fade"){
        token_iter.FindNextToken(msg);
        if(!Online_IsClient()){
            dialogue_queue.push_back(token_iter.GetToken(msg));
        }"""),
])
print("ok")
