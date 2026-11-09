// Test-only level hook for the Overgrowth Online script API (Tools/ogmp/online_api_suite.py).
// It listens, and says everything it hears as level messages the test bridge records:
//   apitest got <me> <from> <length> <text>    a message on "apitest:msg"
//   apitest joined <me> <player> <name>        "online:joined"
//   apitest left <me> <player>                 "online:left"
// "apitest:ping" is answered to its sender only, on "apitest:msg" ("pong <text>").

void Init(string level_name) {
    Online_Listen("apitest:msg", "OnMsg");
    Online_Listen("apitest:ping", "void OnPing(int from, const string &in text)");
    Online_Listen("online:joined", "OnJoined");
    Online_Listen("online:left", "OnLeft");
}

void OnMsg(int from, const string &in text) {
    level.SendMessage("apitest got " + Online_GetOwnPlayerID() + " " + from + " " + text.length() + " " + text);
}

void OnPing(int from, const string &in text) {
    Online_SendToPlayer(from, "apitest:msg", "pong " + text);
}

void OnJoined(int player, const string &in name) {
    level.SendMessage("apitest joined " + Online_GetOwnPlayerID() + " " + player + " " + name);
}

void OnLeft(int player, const string &in name) {
    level.SendMessage("apitest left " + Online_GetOwnPlayerID() + " " + player);
}
