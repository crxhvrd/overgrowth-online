//-----------------------------------------------------------------------------
//           Name: overhaul_online.as
//    Description: What this machine is in an Overgrowth Online session (part of
//                 the level hook). The online engine tells the level scripts
//                 every 2 s: "mp_role host|client #<our character> <players>
//                 #<id> <name> ..." (every player's character and name).
//                 The stock game never does, so everything online stays off
//                 there. Messages to the clients go out as level messages
//                 starting "ohl " (the host's), requests to the host as "ohr "
//                 (it receives "ohr <player> ..."); character ids travel as
//                 #<id> and arrive translated to each machine's own.
//-----------------------------------------------------------------------------

int mp_role = 0;      // 0 offline, 1 host, 2 client
int mp_me = -1;       // our character
int mp_players = 1;
array<int> mp_player_ids;      // every player's character
array<string> mp_player_names;

bool MPHost() {
    return mp_role == 1;
}

bool MPClient() {
    return mp_role == 2;
}

bool MPOnline() {
    return mp_role != 0;
}

array<string> MPTokens(const string &in msg) {
    array<string> res;
    TokenIterator it;
    it.Init();

    while(it.FindNextToken(msg)) {
        res.insertLast(it.GetToken(msg));
    }

    return res;
}

// "#123" -> 123 (-1 if it isn't one)
int MPId(const string &in token) {
    if(token.length() < 2 || token.substr(0, 1) != "#") {
        return -1;
    }

    return atoi(token.substr(1));
}

string MPRef(int id) {
    return "#" + id;
}

// Host: to every client's level scripts
void MPToClients(const string &in body) {
    if(MPHost()) {
        level.SendMessage("ohl " + body);
    }
}

// Client: to the host's level scripts
void MPToHost(const string &in body) {
    if(MPClient()) {
        level.SendMessage("ohr " + body);
    }
}

// Spaces travel as underscores in names
string MPName(const string &in s, bool encode) {
    string res = s;

    for(uint i = 0; i < res.length(); ++i) {
        if(encode && res.substr(i, 1) == " ") {
            res = res.substr(0, i) + "_" + res.substr(i + 1);
        } else if(!encode && res.substr(i, 1) == "_") {
            res = res.substr(0, i) + " " + res.substr(i + 1);
        }
    }

    return res;
}

bool MPIsPlayer(int id) {
    return mp_player_ids.find(id) >= 0;
}

string MPPlayerName(int id) {
    int i = mp_player_ids.find(id);
    return i < 0 ? "" : mp_player_names[i];
}

// Level hook ReceiveMessage: true if it was ours
bool OverhaulOnlineReceive(const string &in message) {
    if(message.length() < 8 || message.substr(0, 8) != "mp_role ") {
        return false;
    }

    array<string> t = MPTokens(message);

    if(t.length() >= 2) {
        int was = mp_role;
        mp_role = t[1] == "host" ? 1 : (t[1] == "client" ? 2 : 0);
        mp_me = t.length() >= 3 ? MPId(t[2]) : -1;
        mp_players = t.length() >= 4 ? atoi(t[3]) : 1;
        mp_player_ids.resize(0);
        mp_player_names.resize(0);

        for(uint k = 4; k + 1 < t.length(); k += 2) {
            int id = MPId(t[k]);

            if(id != -1) {
                mp_player_ids.insertLast(id);
                mp_player_names.insertLast(MPName(t[k + 1], false));
            }
        }

        if(was != mp_role) {
            Log(info, "[Overhaul] online role " + t[1] + " me=" + mp_me + " players=" + mp_players);
        }
    }

    return true;
}
