"""Phase 6, part 8: hotspots/overgrowth_level.as (the Overgrowth story
controller) for online co-op. (Split out of phase6_coop_scripts.py, which
does threatcheck.as, the checkpoint and level.as.)

- Goals are the host's: clients don't evaluate them; the host sends its
  progress ("ohl og_progress N" at each step and every 5 s) and clients
  follow, so a restart puts everyone at the same spawn.
- Goals and the level win stop only when every player is down (they used to
  stop while the host's character was down).
- A step reached gets downed players up (engine: "coop_checkpoint").
- The defeat-goal heal reaches every player ("ohl og_heal").
- A fall-death checkpoint kills the character that fell, on its machine.
- Respawn: every player at the spawn point; the others on safe spots beside
  it (Online_SafeSpotNear: not the water by the first level's start).
- The death prompt explains the co-op rules; the debug key K only with
  debug keys on (it changed progress on any machine).
Run from the repo root."""
P = "Data/Scripts/hotspots/overgrowth_level.as"
s = open(P, encoding="utf-8").read()


def rep(o, n):
    global s
    assert s.count(o) == 1, (o[:90], s.count(o))
    s = s.replace(o, n)


rep("""int progress = 0;
""", """int progress = 0;
float progress_sync_next = 0.0f;  // online host: the progress again now and then (late joiners)
""")

rep("""void PossibleWinEvent(const string &in event, int val, int goal_check, int recursion = 0){
    if(ko_time != -1.0){
        return;
    }""", """void PossibleWinEvent(const string &in event, int val, int goal_check, int recursion = 0){
    // Online the host decides goals (clients follow its progress), and they
    // stop only when every player is down
    if(Online_IsActive() ? (Online_IsClient() || AllPlayersDown()) : ko_time != -1.0){
        return;
    }""")

rep("""    Log(info, "IncrementProgress: "+progress+" to "+(progress+1));
    TriggerGoalPost(_all_but_sting);
    ++progress;
    win_time = -1.0;
""", """    Log(info, "IncrementProgress: "+progress+" to "+(progress+1));
    TriggerGoalPost(_all_but_sting);
    ++progress;
    win_time = -1.0;

    if(Online_IsHosting()){
        level.SendMessage("ohl og_progress "+progress);  // clients follow
        level.SendMessage("coop_checkpoint "+progress);  // the engine gets downed players up
    }
""")

rep("""                    if(success){
                        int player_id = GetPlayerCharacterID();
                        if(player_id != -1){
                            EnterTelemetryZone("Restore player health");
                            MovementObject@ mo = ReadCharacter(player_id);
                            mo.QueueScriptMessage("restore_health");
                            LeaveTelemetryZone();
                        }""", """                    if(success){
                        int player_id = GetPlayerCharacterID();
                        if(player_id != -1){
                            EnterTelemetryZone("Restore player health");
                            MovementObject@ mo = ReadCharacter(player_id);
                            mo.QueueScriptMessage("restore_health");
                            LeaveTelemetryZone();
                        }
                        if(Online_IsHosting()){
                            level.SendMessage("ohl og_heal");  // the other players heal themselves
                        }""")

rep("""        } else if(sub_msg == "set_camera_dof"){""", """        } else if(sub_msg == "ohl" && Online_IsClient() && token_iter.FindNextToken(msg)){
            string what = token_iter.GetToken(msg);
            if(what == "og_progress" && token_iter.FindNextToken(msg)){
                progress = atoi(token_iter.GetToken(msg));
            } else if(what == "og_heal"){
                int player_id = GetPlayerCharacterID();
                if(player_id != -1){
                    ReadCharacter(player_id).QueueScriptMessage("restore_health");
                }
            }
        } else if(sub_msg == "set_camera_dof"){""")

rep("""            if(progress >= checkpoint_id){
                int player_id = GetPlayerCharacterID();
                if(player_id != -1 && ObjectExists(player_id)){
                    MovementObject@ char = ReadCharacter(player_id);
                    char.ReceiveMessage("fall_death");
                }
            }""", """            if(progress >= checkpoint_id){
                // The character that fell (online: on the machine that runs it)
                int fell = token_iter.FindNextToken(msg) ? atoi(token_iter.GetToken(msg)) : -1;
                if(fell != -1 && MovementObjectExists(fell)){
                    MovementObject@ char = ReadCharacterID(fell);
                    if(!char.remote){
                        char.ReceiveMessage("fall_death");
                    }
                } else {
                    int player_id = GetPlayerCharacterID();
                    if(player_id != -1 && ObjectExists(player_id)){
                        MovementObject@ char = ReadCharacter(player_id);
                        char.ReceiveMessage("fall_death");
                    }
                }
            }""")

rep("""    // Place player character at correct spawn point
    array<int> player_ids = GetControlledCharacterIDs();
    for(uint i = 0; i < player_ids.length(); i++) {
        MovementObject@ mo = ReadCharacter(player_ids[i]);
        if(progress == 0){ // Spawn at actual initial player spawn point
            Object@ obj = ReadObjectFromID(mo.GetID());
            mo.position = obj.GetTranslation();
            mo.SetRotationFromFacing(obj.GetRotation() * vec3(0,0,1));
        } else { // Spawn at a custom spawn point
            string param_str = params.GetString("player_spawn");
            token_iter.Init();
            for(int j=0; j<progress; ++j){
                token_iter.FindNextToken(param_str);
            }
            int id = atoi(token_iter.GetToken(param_str));
            if(ObjectExists(id)){
                Object@ obj = ReadObjectFromID(id);
                if(obj.GetType() == _placeholder_object){
                    mo.position = obj.GetTranslation();
                    mo.SetRotationFromFacing(obj.GetRotation() * vec3(0,0,1));
                    mo.Execute("SetCameraFromFacing(); SetOnGround(true); FixDiscontinuity();");
                }
            }
        }
    }""", """    // Place player character at correct spawn point. Co-op: the first player
    // on it, the others on safe spots beside it (not in the water)
    array<int> player_ids = GetControlledCharacterIDs();
    for(uint i = 0; i < player_ids.length(); i++) {
        MovementObject@ mo = ReadCharacter(player_ids[i]);
        Object@ spawn = null;
        if(progress == 0){ // Spawn at actual initial player spawn point (the level's player)
            @spawn = ReadObjectFromID(ReadCharacter(player_ids[0]).GetID());
        } else { // Spawn at a custom spawn point
            string param_str = params.GetString("player_spawn");
            token_iter.Init();
            for(int j=0; j<progress; ++j){
                token_iter.FindNextToken(param_str);
            }
            int id = atoi(token_iter.GetToken(param_str));
            if(ObjectExists(id) && ReadObjectFromID(id).GetType() == _placeholder_object){
                @spawn = ReadObjectFromID(id);
            }
        }
        if(spawn !is null){
            vec3 facing = spawn.GetRotation() * vec3(0,0,1);
            mo.position = i == 0 ? spawn.GetTranslation() : Online_SafeSpotNear(spawn.GetTranslation(), facing, int(i));
            mo.SetRotationFromFacing(facing);
            if(progress != 0){
                mo.Execute("SetCameraFromFacing(); SetOnGround(true); FixDiscontinuity();");
            }
        }
    }""")

rep("""    if(win_time != -1.0 && the_time > win_time && ko_time == -1.0){""",
    """    if(win_time != -1.0 && the_time > win_time && (Online_IsActive() ? (Online_IsHosting() && !AllPlayersDown()) : ko_time == -1.0)){""")

rep("""            string respawn = params.GetString("death_message");""", """            string respawn = params.GetString("death_message");
            if(Online_IsActive()){
                respawn = "\\nYou are down. You get up at the next checkpoint; if everyone is down, the level starts over.";
            }""")

rep("""void Update() {
    EnterTelemetryZone("Overgrowth Level Update");

    if(GetInputPressed(0, "k")){""", """void Update() {
    EnterTelemetryZone("Overgrowth Level Update");

    if(Online_IsHosting() && the_time >= progress_sync_next){
        progress_sync_next = the_time + 5.0f;
        level.SendMessage("ohl og_progress "+progress);
    }

    if(DebugKeysEnabled() && GetInputPressed(0, "k")){  // (a debug key: it changed progress on any machine)""")

open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")
