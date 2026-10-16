"""Phase 5, part 2: a channel for level scripts (the Overhaul mod's hooks),
through APIs the stock 1.4 game has too, so the same scripts run offline:

- level.SendMessage("ohl ...") on the host reaches every client's level
  scripts; level.SendMessage("ohr ...") on a client reaches the host's as
  "ohr <player id> ...". Character and item ids written as #<id> are
  translated between the host's and each machine's own ids.
- Every 2 s while online, the level scripts here get
  "mp_role host|client <our character id> <players>", and "mp_role none"
  when the session ends.
- An object a host script creates (CreateObject) appears on every client
  (deleting one already did).
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/online.h", [
("""    void SendLevelMessage(const string& msg);""", """    void SendLevelMessage(const string& msg);
    // Overhaul Online: level-script channel ("ohl" host -> clients, "ohr" client -> host)
    string TranslateIDs(const string& msg, bool to_common) const;
    bool SendLevelRequest(const string& msg);  // client: true if it went to the host
    double last_role_message_ms = -1e9;
    bool sent_role = false;"""),
])

patch("Online/online.cpp", [
("""void Online::SendLevelMessage(const string& msg) {
    // we currently only want start_dialogue
    if (msg.find("reset") == string::npos)
        return;  // tutorial is very spammy

    Send<OnlineMessages::SendLevelMessage>(msg);
}""", """void Online::SendLevelMessage(const string& msg) {
    // Overhaul Online: level scripts' own channel to the clients ("ohl ...")
    if (msg.compare(0, 4, "ohl ") == 0) {
        Send<OnlineMessages::SendLevelMessage>(msg);  // the host's ids are the common ones
        return;
    }

    // we currently only want start_dialogue
    if (msg.find("reset") == string::npos)
        return;  // tutorial is very spammy

    Send<OnlineMessages::SendLevelMessage>(msg);
}

// "#<id>" tokens: our ids to the host's (to_common) or the host's to ours
string Online::TranslateIDs(const string& msg, bool to_common) const {
    string out;
    size_t i = 0;

    while (i < msg.size()) {
        if (msg[i] == '#' && i + 1 < msg.size() && (isdigit((unsigned char)msg[i + 1]) || msg[i + 1] == '-')) {
            size_t j = i + 1;
            if (msg[j] == '-') {
                ++j;
            }
            while (j < msg.size() && isdigit((unsigned char)msg[j])) {
                ++j;
            }
            int id = atoi(msg.substr(i + 1, j - i - 1).c_str());
            if (id >= 0) {
                id = to_common ? GetOriginalID(id) : GetObjectID(id);
            }
            out += "#" + to_string(id);
            i = j;
        } else {
            out += msg[i++];
        }
    }

    return out;
}

// Client: a level script's request to the host ("ohr ...")
bool Online::SendLevelRequest(const string& msg) {
    if (!IsClient() || msg.compare(0, 4, "ohr ") != 0) {
        return false;
    }
    Send<OnlineMessages::SendLevelMessage>(TranslateIDs(msg, true));
    return true;
}"""),
# The role, every 2 s while online
("""        UpdateTakeOvers(scenegraph);
""", """        UpdateTakeOvers(scenegraph);

        // Overhaul Online: level scripts learn whether they run on the host or a client
        if (scenegraph != nullptr && scenegraph->level != nullptr && host_started_level &&
            OnlineClockMs() - last_role_message_ms > 2000.0) {
            last_role_message_ms = OnlineClockMs();
            sent_role = true;
            vector<ObjectID> mine = GetLocalAvatarIDs();
            scenegraph->level->Message(string("mp_role ") + (IsHosting() ? "host" : "client") + " #" +
                                       to_string(mine.empty() ? -1 : (int)mine[0]) + " " + to_string(GetPlayerCount()));
        }
"""),
])

patch("Online/Message/send_level_message.cpp", [
("""    if (sg != nullptr) {
        sg->level->Message(lm->msg);
    } else {""", """    if (sg != nullptr) {
        Online* online = Online::Instance();
        if (online->IsHosting()) {
            // Overhaul Online: from a client only its level scripts' requests, tagged with who asked
            if (lm->msg.compare(0, 4, "ohr ") == 0) {
                sg->level->Message("ohr " + std::to_string((int)peer) + " " + online->TranslateIDs(lm->msg.substr(4), false));
            }
        } else {
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
        }
    } else {"""),
])

# Client: an "ohr" request goes to the host instead of our own level scripts
patch("Game/level.cpp", [
("""    if (online->IsHosting()) {
        online->SendLevelMessage(msg);
    }
""", """    if (online->IsHosting()) {
        online->SendLevelMessage(msg);
    } else if (online->SendLevelRequest(msg)) {
        return;  // Overhaul Online: a request for the host
    }
"""),
])

# A host script's new object appears everywhere
patch("Scripting/angelscript/asfuncs.cpp", [
("""        if (obj) {
            obj->exclude_from_undo = exclude_from_save;
            obj->exclude_from_save = exclude_from_save;
            obj->permission_flags = 0;
            id = obj->GetID();""", """        if (obj) {
            obj->exclude_from_undo = exclude_from_save;
            obj->exclude_from_save = exclude_from_save;
            obj->permission_flags = 0;
            id = obj->GetID();

            // Overhaul Online: what a host script creates, every client creates too
            Online* online = Online::Instance();
            if (online->IsHosting() && online->host_started_level &&
                (obj->GetType() == _movement_object || obj->GetType() == _item_object)) {
                online->Send<OnlineMessages::CreateEntity>(path, obj->GetTranslation(), id);
            }"""),
])
print("ok")
