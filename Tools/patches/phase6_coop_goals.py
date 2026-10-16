"""Phase 6, part 11: guests follow the story's goal steps; talks answered.

The "?" characters (start_dialogue hotspots with "Start Disabled") are
switched on by a goal step ("activate <hotspot>" in goal_N_pre/post). A
guest took the host's goal number ("ohl og_progress") without running the
steps it skipped, so on guests those talkers stayed off: no "?", no talk.
- overgrowth_level.as: a guest runs the goal steps between its number and
  the host's (CoopFollowProgress). Dialogues in them stay the host's (level.as
  refuses them on guests); enable/disable is the engine's anyway; this brings
  "activate" and the music layers.
- start_dialogue.as / level.as: the host hands a guest's talk to the hotspot
  as "coop_request": played already (a late joiner, or the host got there
  first) -> it tells the guests "coop_talked" again, so their "?" goes.
Run from the repo root, after phase6_coop_talk.py."""
D = "Data/Scripts/"


def patch(p, pairs):
    s = open(D + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(D + p, "w", encoding="utf-8", newline="").write(s)


patch("hotspots/overgrowth_level.as", [
("""            if(what == "og_progress" && token_iter.FindNextToken(msg)){
                progress = atoi(token_iter.GetToken(msg));""",
 """            if(what == "og_progress" && token_iter.FindNextToken(msg)){
                CoopFollowProgress(atoi(token_iter.GetToken(msg)));"""),
("""void SetEnabledCharacterAndItems(int id, bool enabled){""",
 """// Online, a guest: the goal steps up to the host's goal, as IncrementProgress
// runs them (they switch on the "?" talkers)
void CoopFollowProgress(int to){
    while(progress < to){
        TriggerGoalPost(_all_but_sting);
        ++progress;
        win_time = -1.0;
        TriggerGoalPre();
    }
    progress = to;  // (lower: the host started over)
}

void SetEnabledCharacterAndItems(int id, bool enabled){"""),
])

patch("hotspots/start_dialogue.as", [
("""    if(msg == "coop_played"){  // online: the host played it
        played = true;
    }""", """    if(msg == "coop_played"){  // online: the host played it
        played = true;
    }
    if(msg == "coop_request"){  // online, the host: a guest's talk
        if(played){
            level.SendMessage("ohl coop_talked "+hotspot.GetID());  // its "?" goes
        } else {
            TryToPlayDialogue();
        }
    }"""),
])

patch("level.as", [
("""                ReadObjectFromID(id).QueueScriptMessage("player_pressed_attack");""",
 """                ReadObjectFromID(id).QueueScriptMessage("coop_request");"""),
])
print("ok")
