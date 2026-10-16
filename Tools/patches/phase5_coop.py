"""Phase 5, part 7: co-op rules for story campaigns.

- A client's own scripts never reset the level (its death prompt, a hotspot):
  only the host's resets count, and they reach everyone.
- The host's reset is held while the host is down and another player is still
  up (its death prompt); a reset while the host stands (Retry, a script) goes.
- In a linear (story) campaign, when every player has been down for 5 s the
  host resets the level for everyone.
- A checkpoint the host's scripts report (player_entered_checkpoint) brings
  every downed player back up next to a player who is still standing. Each
  avatar is revived by the machine that runs it ("ohg revive", queued to
  LateUpdate: scripts must not run inside message handling).
- A hot-joining player's character appears beside the host's, not inside it.
- (Wolfire bug) campaign_id = -1 on a std::string made it "\\xff".
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/online.h", [
("""    vector<std::pair<ObjectID, ObjectID> > pending_drop_orders;
""", """    vector<std::pair<ObjectID, ObjectID> > pending_drop_orders;

    // Co-op rules (story campaigns)
    bool delivering_network_level_message = false;  // Level::Message is passing on the host's message
    bool AllowScriptReset();                        // a level script here asked for "reset"
    bool CoopCampaign();                            // the level belongs to a linear (story) campaign
    vector<ObjectID> PlayerAvatarIDs();             // every player's character here
    void CoopUpdate(SceneGraph* sg);                // host: everyone down for 5 s -> reset
    void CoopCheckpoint() { coop_revive_pending = true; }  // host: a checkpoint was reached
    void CoopReviveRequest(const string& msg);      // "ohg revive <common id> x y z" from the host
    void ReviveAvatar(MovementObject* mo, const vec3& pos);
    double coop_all_down_ms = -1.0;
    bool coop_revive_pending = false;
    vector<std::pair<ObjectID, vec3> > pending_revives;
    string coop_campaign_for;
    bool coop_campaign = false;
"""),
])

patch("Online/online.cpp", [
# The Wolfire bug
("""            } else {
                campaign_id = -1;
            }""", """            } else {
                campaign_id = "";
            }"""),
# Hot-join: beside the host
("""        team = mymo->GetScriptParams()->GetStringVal("Teams");
        position = mymo->GetTranslation();""", """        team = mymo->GetScriptParams()->GetStringVal("Teams");
        // Overhaul Online: beside the host's character, alternating sides, not inside it
        int n = (int)online_session->player_states.size();
        vec3 facing = mymo->GetFacing();
        vec3 right = normalize(vec3(-facing.z(), 0.0f, facing.x()) + vec3(0.0001f, 0.0f, 0.0f));
        position = mymo->position + right * (1.3f * (1 + n / 2) * (n % 2 == 0 ? 1.0f : -1.0f)) - facing * 0.6f;"""),
# The co-op update runs with the rest of LateUpdate
("""        for (const auto& order : pending_drop_orders) {""", """        CoopUpdate(scenegraph);

        for (const auto& order : pending_drop_orders) {"""),
# Implementations, before SendLevelRequest
("""// Client: a level script's request to the host ("ohr ...")""", """// ---- Co-op rules (story campaigns) -------------------------------------------

vector<ObjectID> Online::PlayerAvatarIDs() {
    vector<ObjectID> ids;
    for (const PlayerState& player : GetPlayerStates()) {
        if (player.object_id >= 0) {
            ids.push_back(player.object_id);
        }
    }
    return ids;
}

static bool AvatarUp(SceneGraph* sg, ObjectID id) {
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    return o != nullptr && o->GetType() == _movement_object &&
           static_cast<MovementObject*>(o)->ASGetIntVar("knocked_out") == 0;  // aschar.as _awake
}

bool Online::AllowScriptReset() {
    if (!IsActive() || !host_started_level) {
        return true;
    }
    if (!IsHosting()) {
        LOGI << "Overhaul Online: a client does not reset the level, the host does" << std::endl;
        return false;
    }

    // The host's death prompt: not while someone else is still fighting
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    vector<ObjectID> mine = GetLocalAvatarIDs();
    if (!mine.empty() && AvatarUp(sg, mine[0])) {
        return true;  // the host stands: Retry or a script, on purpose
    }
    for (ObjectID id : PlayerAvatarIDs()) {
        if (!IsLocalAvatar(id) && AvatarUp(sg, id)) {
            LOGI << "Overhaul Online: reset held, player character " << id << " is still up" << std::endl;
            return false;
        }
    }
    return true;
}

bool Online::CoopCampaign() {
    if (coop_campaign_for != level_name + "|" + campaign_id) {
        coop_campaign_for = level_name + "|" + campaign_id;
        coop_campaign = false;
        for (ModInstance* mod : ModLoading::Instance().GetAllMods()) {
            if (!mod->IsActive()) {
                continue;
            }
            for (const ModInstance::Campaign& c : mod->campaigns) {
                if (!campaign_id.empty() && campaign_id == c.id.str()) {
                    coop_campaign = string(c.main_script.str()).find("linear_campaign") != string::npos;
                }
            }
        }
    }
    return coop_campaign;
}

void Online::CoopUpdate(SceneGraph* sg) {
    // Revives: a client's own character, as the host asked
    for (const auto& r : pending_revives) {
        Object* o = sg != nullptr ? sg->GetObjectFromID(r.first) : nullptr;
        if (o != nullptr && o->GetType() == _movement_object && IsLocalAvatar(r.first)) {
            ReviveAvatar(static_cast<MovementObject*>(o), r.second);
        }
    }
    pending_revives.clear();

    if (!IsHosting() || !host_started_level || sg == nullptr || sg->level == nullptr) {
        coop_all_down_ms = -1.0;
        coop_revive_pending = false;
        return;
    }

    vector<ObjectID> players = PlayerAvatarIDs();

    // A checkpoint: everyone who is down gets up beside someone who isn't
    if (coop_revive_pending) {
        coop_revive_pending = false;
        MovementObject* anchor = nullptr;
        vector<ObjectID> mine = GetLocalAvatarIDs();
        if (!mine.empty() && AvatarUp(sg, mine[0])) {
            anchor = static_cast<MovementObject*>(sg->GetObjectFromID(mine[0]));
        }
        for (ObjectID id : players) {
            if (anchor == nullptr && AvatarUp(sg, id)) {
                anchor = static_cast<MovementObject*>(sg->GetObjectFromID(id));
            }
        }
        int k = 0;
        for (ObjectID id : players) {
            Object* o = sg->GetObjectFromID(id);
            if (anchor == nullptr || o == nullptr || o->GetType() != _movement_object || AvatarUp(sg, id)) {
                continue;
            }
            vec3 facing = anchor->GetFacing();
            vec3 right = normalize(vec3(-facing.z(), 0.0f, facing.x()) + vec3(0.0001f, 0.0f, 0.0f));
            vec3 pos = anchor->position + right * (1.3f * (1 + k / 2) * (k % 2 == 0 ? 1.0f : -1.0f)) - facing * 0.6f;
            ++k;
            LOGI << "Overhaul Online: checkpoint, player character " << id << " back up" << std::endl;
            if (IsLocalAvatar(id)) {
                ReviveAvatar(static_cast<MovementObject*>(o), pos);
            } else {
                Send<OnlineMessages::SendLevelMessage>("ohg revive " + to_string((int)GetOriginalID(id)) + " " +
                                                       to_string(pos.x()) + " " + to_string(pos.y()) + " " + to_string(pos.z()));
            }
        }
    }

    // Everyone down for 5 s in a story level: the level starts over for all
    if (players.size() < 2 || !CoopCampaign()) {
        coop_all_down_ms = -1.0;
        return;
    }
    for (ObjectID id : players) {
        if (AvatarUp(sg, id)) {
            coop_all_down_ms = -1.0;
            return;
        }
    }
    if (coop_all_down_ms < 0.0) {
        coop_all_down_ms = OnlineClockMs();
    } else if (OnlineClockMs() - coop_all_down_ms > 5000.0) {
        coop_all_down_ms = -1.0;
        LOGI << "Overhaul Online: every player is down, the level starts over" << std::endl;
        sg->level->Message("reset");
        sg->level->Message("skip_dialogue");
    }
}

// Client: "ohg revive <common id> x y z"; runs in LateUpdate
void Online::CoopReviveRequest(const string& msg) {
    int cid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (sscanf(msg.c_str(), "ohg revive %d %f %f %f", &cid, &x, &y, &z) == 4) {
        pending_revives.push_back(std::make_pair(GetObjectID(cid), vec3(x, y, z)));
    }
}

void Online::ReviveAvatar(MovementObject* mo, const vec3& pos) {
    // Stock calls: full health (tutorial.as revive_all), dialogue control on
    // and off stands the body up, then the spot
    mo->Execute("Recover();");
    mo->ReceiveScriptMessage("set_dialogue_control true");
    mo->ReceiveScriptMessage("set_dialogue_control false");
    mo->Execute("this_mo.position = vec3(" + to_string(pos.x()) + ", " + to_string(pos.y()) + ", " + to_string(pos.z()) +
                "); this_mo.velocity = vec3(0.0f);");
}

// Client: a level script's request to the host ("ohr ...")"""),
])

patch("Online/Message/send_level_message.cpp", [
("""            if (lm->msg.compare(0, 4, "ohr ") == 0) {
                sg->level->Message("ohr " + std::to_string((int)peer) + " " + online->TranslateIDs(lm->msg.substr(4), false));
            }""", """            if (lm->msg.compare(0, 4, "ohr ") == 0) {
                online->delivering_network_level_message = true;
                sg->level->Message("ohr " + std::to_string((int)peer) + " " + online->TranslateIDs(lm->msg.substr(4), false));
                online->delivering_network_level_message = false;
            }"""),
("""        } else if (lm->msg.compare(0, 10, "ohg speed ") == 0) {
            Engine::Instance()->FollowGameSpeed((float)atof(lm->msg.c_str() + 10));  // the host's game speed
        } else {
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
        }""", """        } else if (lm->msg.compare(0, 10, "ohg speed ") == 0) {
            Engine::Instance()->FollowGameSpeed((float)atof(lm->msg.c_str() + 10));  // the host's game speed
        } else if (lm->msg.compare(0, 11, "ohg revive ") == 0) {
            online->CoopReviveRequest(lm->msg);  // a checkpoint: our character back up
        } else {
            online->delivering_network_level_message = true;  // (the host's reset counts here)
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
            online->delivering_network_level_message = false;
        }"""),
])

patch("Game/level.cpp", [
("""void Level::Message(const std::string& msg) {
    Online* online = Online::Instance();
    ASArglist args;
    args.AddObject((void*)&msg);
""", """void Level::Message(const std::string& msg) {
    Online* online = Online::Instance();
    ASArglist args;
    args.AddObject((void*)&msg);

    // Overhaul Online: co-op rules. Only the host resets the level, and not
    // from its death prompt while another player still fights; a checkpoint
    // brings downed players back
    if (online->IsActive() && !online->delivering_network_level_message) {
        if (msg == "reset" && !online->AllowScriptReset()) {
            return;
        }
        if (online->IsHosting() && msg.compare(0, 25, "player_entered_checkpoint") == 0) {
            online->CoopCheckpoint();
        }
    }
"""),
])
print("ok")
