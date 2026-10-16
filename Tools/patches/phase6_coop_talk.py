"""Phase 6, part 9: talking to story characters (the "?" ones) in co-op.

A guest pressing attack by a talker ran the dialogue hotspot on its own
machine: the dialogue was refused there (only the host starts dialogues),
and its copy of the hotspot counted as used, so the "?" was gone for the
guest and nothing happened.
- start_dialogue.as: on a guest, a talk (or walking into an automatic one)
  is a request to the host: "ohr coop_talk <hotspot>". When the host's
  hotspot plays, it tells the guests: "ohl coop_talked <hotspot>", and their
  copies count as used too.
- level.as: hands those to the hotspot (host: as if its player pressed
  attack there; guest: "coop_played").
Run from the repo root."""
D = "Data/Scripts/"


def patch(p, pairs):
    s = open(D + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(D + p, "w", encoding="utf-8", newline="").write(s)


patch("hotspots/start_dialogue.as", [
("""    if(msg == "reset"){
        Reset();
    }""", """    if(msg == "reset"){
        Reset();
    }
    if(msg == "coop_played"){  // online: the host played it
        played = true;
    }"""),
("""void TryToPlayDialogue() {
    if(!played){""", """void TryToPlayDialogue() {
    // Online a guest asks the host: dialogues are the host's, and its start reaches everyone
    if(!played && Online_IsClient()){
        level.SendMessage("ohr coop_talk "+hotspot.GetID());
        return;
    }
    if(!played){"""),
("""            }
            played = true;
        }
    }
}""", """            }
            played = true;
            if(Online_IsHosting()){
                level.SendMessage("ohl coop_talked "+hotspot.GetID());  // the guests' copies are used too
            }
        }
    }
}"""),
])

patch("level.as", [
("""    string token = token_iter.GetToken(msg);
    if(token == "cleartext"){
        has_display_text = false;""", """    string token = token_iter.GetToken(msg);

    // Online co-op: talking to story characters (start_dialogue.as)
    if(token == "ohr" && Online_IsHosting()){
        // "ohr <player> coop_talk <hotspot>": a guest pressed attack there
        if(token_iter.FindNextToken(msg) && token_iter.FindNextToken(msg) && token_iter.GetToken(msg) == "coop_talk" &&
                token_iter.FindNextToken(msg)){
            int id = atoi(token_iter.GetToken(msg));
            if(ObjectExists(id) && ReadObjectFromID(id).GetType() == _hotspot_object &&
                    ReadObjectFromID(id).GetScriptParams().HasParam("Dialogue")){  // (a dialogue hotspot, nothing else)
                ReadObjectFromID(id).QueueScriptMessage("player_pressed_attack");
            }
        }
        return;
    }
    if(token == "ohl" && Online_IsClient()){
        if(token_iter.FindNextToken(msg) && token_iter.GetToken(msg) == "coop_talked" && token_iter.FindNextToken(msg)){
            int id = atoi(token_iter.GetToken(msg));
            if(ObjectExists(id) && ReadObjectFromID(id).GetType() == _hotspot_object){
                ReadObjectFromID(id).QueueScriptMessage("coop_played");
            }
        }
        return;
    }

    if(token == "cleartext"){
        has_display_text = false;"""),
])
print("ok")
