"""Phase 4 (combat), part 6: sounds. The host broadcast every sound its
scripts played; clients played theirs only for themselves. With ownership a
client runs its own character (footsteps, whooshes, voice) and, as the
defender, the hit sounds of attacks on it. Now a client shares the
positional sounds and voices played by scripts of characters it runs (a
proxy's script stands still, so that is nearly all of them); the host relays
them to everyone else. 2D sounds stay local. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/online.h", [
("""    vector<std::pair<ObjectID, ObjectID> > pending_drop_orders;""", """    vector<std::pair<ObjectID, ObjectID> > pending_drop_orders;

    // Sounds: scripts of characters this machine runs are playing (main thread)
    int owned_script_depth = 0;
    bool ShouldShareSound() const { return IsHosting() || (IsClient() && owned_script_depth > 0); }"""),
])

patch("Online/online.cpp", [
# Host: pass a client's sounds on to everyone else
("""        for (auto& it : online_session->incoming_messages) {
            message_handler.Execute(it.message, it.from);
        }""", """        for (auto& it : online_session->incoming_messages) {
            message_handler.Execute(it.message, it.from);

            // Overhaul Online: a client's character sounds go to the other clients too
            if (IsHosting()) {
                OnlineMessageType type = message_handler.GetMessageType(it.message);
                if (type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundLocationMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlayGroupPriorityMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupGainMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupVoiceMessage>()) {
                    RelayFromPeer(it.message, it.from);
                }
            }
        }"""),
("""    if (reaches || m->type == "attackblocked") {
        result = target->ResolveRemoteHit(""", """    if (reaches || m->type == "attackblocked") {
        ++owned_script_depth;  // the defender's sounds are everyone's
        result = target->ResolveRemoteHit("""),
("""m->type, m->attack_path, m->dir, m->pos, attacker_id, m->damage_mult, m->knockback_mult);
    }

    ++hits_resolved;""", """m->type, m->attack_path, m->dir, m->pos, attacker_id, m->damage_mult, m->knockback_mult);
        --owned_script_depth;
    }

    ++hits_resolved;"""),
("""    if (IsLocalAvatar(char_id)) {
        item->SetLinearVelocity(m->velocity);  // its script weighs the blow by the item's speed
        mo->HitByItem(item_id, m->point, m->material, m->type);""", """    if (IsLocalAvatar(char_id)) {
        item->SetLinearVelocity(m->velocity);  // its script weighs the blow by the item's speed
        ++owned_script_depth;
        mo->HitByItem(item_id, m->point, m->material, m->type);
        --owned_script_depth;"""),
])

patch("Objects/movementobject.cpp", [
("""    SetNetworkProxy(online->IsActive() && !online->IsLocalAvatar(GetID()));""",
 """    SetNetworkProxy(online->IsActive() && !online->IsLocalAvatar(GetID()));

    // Overhaul Online: sounds our characters' scripts play are shared
    struct OwnedScope {
        bool on;
        explicit OwnedScope(bool on) : on(on) {
            if (on) ++Online::Instance()->owned_script_depth;
        }
        ~OwnedScope() {
            if (on) --Online::Instance()->owned_script_depth;
        }
    } owned_scope(online->IsActive() && !network_proxy);"""),
("""    if (online->IsHosting()) {
        online->Send<OnlineMessages::AudioPlaySoundGroupVoiceMessage>(path, GetID(), delay);
    }""", """    if (online->ShouldShareSound()) {
        online->Send<OnlineMessages::AudioPlaySoundGroupVoiceMessage>(path, GetID(), delay);
    }"""),
])

s = open(S + "Scripting/angelscript/asfuncs.cpp", encoding="utf-8").read()
for msg in ("AudioPlaySoundLocationMessage>(path, location)", "AudioPlaySoundGroupMessage>(path, location)",
            "AudioPlayGroupPriorityMessage>(path, location, priority)", "AudioPlaySoundGroupGainMessage>(path, location, gain)"):
    send = "        online->Send<OnlineMessages::" + msg + ";"
    i = s.index(send)
    head = s.rfind("    if (online->IsHosting()) {\n", 0, i)
    assert head != -1 and i - head < 80, msg
    s = s[:head] + "    if (online->ShouldShareSound()) {  // Overhaul Online: clients share their characters' sounds\n" + s[head + len("    if (online->IsHosting()) {\n"):]
open(S + "Scripting/angelscript/asfuncs.cpp", "w", encoding="utf-8", newline="").write(s)
print("ok")
