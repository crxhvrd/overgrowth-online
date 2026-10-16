"""Phase 5, part 4: the level scripts learn who the players are. The role
message lists every player's character and name:
    mp_role host|client #<ours> <count> #<id> <name> #<id> <name> ...
(names with spaces travel with underscores). Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/online.cpp", [
("""            int mine = GetOwnPlayerState().object_id;
            scenegraph->level->Message(string("mp_role ") + (IsHosting() ? "host" : "client") + " #" +
                                       to_string(mine) + " " + to_string(GetPlayerCount()));""",
 """            int mine = GetOwnPlayerState().object_id;
            string role = string("mp_role ") + (IsHosting() ? "host" : "client") + " #" + to_string(mine) + " " +
                          to_string(GetPlayerCount());
            for (const PlayerState& player : GetPlayerStates()) {
                string name = player.playername.empty() ? "Player" : player.playername;
                for (char& c : name) {
                    if (c == ' ' || c == '\\t' || c == '#') {
                        c = '_';
                    }
                }
                role += " #" + to_string((int)player.object_id) + " " + name;
            }
            scenegraph->level->Message(role);"""),
])
print("ok")
