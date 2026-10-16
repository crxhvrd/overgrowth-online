"""Phase 5, part 5: one game speed online. The host sends its speed (the
game's own global_time_scale_mult) every 2 s; clients run at it without
touching their saved setting, and go back to their own when they leave.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Main/engine.h", [
("""    void SetGameSpeed(float val, bool hard);
""", """    void SetGameSpeed(float val, bool hard);
    // Overhaul Online: run at the host's speed (our saved setting stays); the
    // speed this machine runs at now
    void FollowGameSpeed(float val);
    float GameSpeed() const { return current_global_scale_mult; }
"""),
])

patch("Main/engine.cpp", [
("""void Engine::SetGameSpeed(float val, bool hard) {""", """void Engine::FollowGameSpeed(float val) {
    if (val >= 0.1f && val != current_global_scale_mult) {
        game_timer.target_time_scale /= current_global_scale_mult;
        current_global_scale_mult = val;
        game_timer.target_time_scale *= val;
    }
}

void Engine::SetGameSpeed(float val, bool hard) {"""),
])

patch("Online/online.cpp", [
("""            scenegraph->level->Message(role);""", """            scenegraph->level->Message(role);

            if (IsHosting()) {
                Send<OnlineMessages::SendLevelMessage>("ohg speed " + to_string(Engine::Instance()->GameSpeed()));
            }"""),
])

patch("Online/Message/send_level_message.cpp", [
("""        } else {
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
        }""", """        } else if (lm->msg.compare(0, 10, "ohg speed ") == 0) {
            Engine::Instance()->FollowGameSpeed((float)atof(lm->msg.c_str() + 10));  // the host's game speed
        } else {
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
        }"""),
])
print("ok")
