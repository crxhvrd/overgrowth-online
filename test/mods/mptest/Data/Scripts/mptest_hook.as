// Test-only level hook (Phase 0 baseline of the stock online code).
// config mpt_role = host | client, mpt_ip = address to connect to.
// Logs [MPT] lines: session state, players, every character's position.

int frames = 0;
bool started = false;

void Init(string level_name) {
    Log(info, "[MPT] init level=" + level_name + " role=" + GetConfigValueString("mpt_role"));
}

void Update(int is_paused) {
    ++frames;
    string role = GetConfigValueString("mpt_role");

    if(!started && frames > 240 && !Online_IsActive()) {
        started = true;

        if(role == "host") {
            Online_Host();
            Log(info, "[MPT] hosting");
        } else if(role == "client") {
            Online_Connect(GetConfigValueString("mpt_ip"));
            Log(info, "[MPT] connecting to " + GetConfigValueString("mpt_ip"));
        }
    }

    if(frames % 240 == 0) {
        Report();
    }
}

void Report() {
    Log(info, "[MPT] frame=" + frames + " active=" + Online_IsActive() + " hosting=" + Online_IsHosting() +
        " client=" + Online_IsClient() + " chars=" + GetNumCharacters());
    array<PlayerState>@ players = GetPlayerStates();

    for(uint i = 0; i < players.length(); ++i) {
        Log(info, "[MPT] player name=" + players[i].playername + " avatar=" + players[i].avatar_id + " ping=" + players[i].ping);
    }

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);
        vec3 p = char.position;
        Log(info, "[MPT] char id=" + char.GetID() + " controlled=" + char.controlled + " remote=" + char.remote +
            " pos=" + p.x + "," + p.y + "," + p.z);
    }
}

void ReceiveMessage(string message) {
}

void DrawGUI() {
}

bool HasFocus() {
    return false;
}
