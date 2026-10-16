"""Phase 5, part 1: the online logic moves from aschar.as into the engine, so
any character script works online, the Overhaul mod's 1.4-based one included
(the script loader has no #if: a mod script can't name online-only
functions and still run on the stock 1.4 game).

- WasHit on a character another machine runs, called by a script of a
  character this machine runs, becomes a hit request (a grab takes the
  victim over and runs here); the attacker gets the usual guess back.
- Execute on such a character that ties a tether (rear choke, body drag)
  takes it over first.
- A proxy's script update is skipped (its time still advances).
- The owner's answer and the resume after a takeover call the script's own
  handlers if it has them, else stock-symbol code.
- Data/Scripts/aschar.as goes back to the repository's version.
Run from the repo root."""
import subprocess

S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Objects/movementobject.cpp", [
# Proxies: no script update, the clock still runs
("""    --update_script_counter;
    if (update_script_counter <= 0) {
        ASArglist args;
        args.Add(update_script_period);
        {
            PROFILER_ZONE(g_profiler_ctx, "Angelscript Update()");

            as_context->CallScriptFunction(as_funcs.update, &args);
            angle_script_ready = true;
        }

        update_script_counter += update_script_period;
    }""", """    --update_script_counter;
    if (update_script_counter <= 0) {
        ASArglist args;
        args.Add(update_script_period);
        if (network_proxy) {
            // Overhaul Online: its owner runs it; the engine keeps its game
            // state here (ApplyNetworkVitals), only the script's clock moves
            if (ASHasVar("time")) {
                ASSetFloatVar("time", ASGetFloatVar("time") + timestep * update_script_period);
            }
        } else {
            PROFILER_ZONE(g_profiler_ctx, "Angelscript Update()");

            as_context->CallScriptFunction(as_funcs.update, &args);
            angle_script_ready = true;
        }

        update_script_counter += update_script_period;
    }"""),
# Hits on proxies from our scripts become requests
("""int MovementObject::ASWasHit(std::string type, std::string attack_path, vec3 dir, vec3 pos, int attacker_id, float attack_damage_mult, float attack_knockback_mult) {
    ASArglist args;""", """int MovementObject::ASWasHit(std::string type, std::string attack_path, vec3 dir, vec3 pos, int attacker_id, float attack_damage_mult, float attack_knockback_mult) {
    // Overhaul Online: another machine runs this character and a script of a
    // character ours is hitting it: its owner decides (a grab takes it over)
    Online* online = Online::Instance();
    if (network_proxy && online->IsActive() && online->owned_script_depth > 0) {
        if (type == "grabbed") {
            if (!online->TakeOver(GetID(), 5.0f)) {
                return 0;  // _miss
            }
        } else {
            return online->NetworkWasHit(this, type, attack_path, dir, pos, attacker_id, attack_damage_mult, attack_knockback_mult);
        }
    }

    ASArglist args;"""),
# Tying a tether to a proxy (rear choke, body drag) takes it over first
("""void MovementObject::Execute(std::string string) {
    as_context->Execute(string);
}""", """void MovementObject::Execute(std::string string) {
    // Overhaul Online: a choke or drag grabs a character another machine runs:
    // it plays out here, so take the character for it first
    Online* online = Online::Instance();
    if (network_proxy && online->IsActive() && online->owned_script_depth > 0 && string.find("SetTethered(") != std::string::npos) {
        online->TakeOver(GetID(), 60.0f);
    }
    as_context->Execute(string);
}"""),
# Resume: the script's handler, or stock-symbol code
("""    if (!proxy && HasFunction("void OnNetworkResume()")) {
        Execute("OnNetworkResume();");
    }""", """    if (!proxy) {
        if (HasFunction("void OnNetworkResume()")) {
            Execute("OnNetworkResume();");
        } else if (NetworkVitalsSupported() && HasFunction("void Ragdoll(int type)")) {
            // aschar.as (1.4 and later): a ragdoll stays a ragdoll
            as_context->Execute(
                "if(state == _ragdoll_state || knocked_out != _awake) { state = _movement_state; "
                "Ragdoll(knocked_out != _awake ? _RGDL_LIMP : _RGDL_FALL); }");
        }
    }"""),
])

patch("Online/online.h", [
("""    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);""",
 """    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);
    int NetworkWasHit(MovementObject* target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                      ObjectID attacker, float damage_mult, float knockback_mult);"""),
])

patch("Online/online.cpp", [
("""// We run the defender: decide the hit on its own state, as its player saw it""", """// A script of ours hit a character another machine runs: ask its owner, and
// give the attack the usual guess meanwhile (aschar.as values: _miss 0,
// _hit 2, _block_impact 3). The reach the defender checks against is ours.
int Online::NetworkWasHit(MovementObject* target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                          ObjectID attacker, float damage_mult, float knockback_mult) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* a = sg != nullptr ? sg->GetObjectFromID(attacker) : nullptr;
    float reach = a != nullptr ? distance(a->GetTranslation(), target->position) : 0.0f;

    SendHitRequest(target->GetID(), type, attack_path, dir, pos, attacker, damage_mult, knockback_mult, reach);

    if (type == "attackimpact") {
        return target->NetworkVitalsSupported() && target->ASGetIntVar("knocked_out") != 0 ? 0 : 2;
    } else if (type == "attackblocked") {
        return 3;  // we switched to the blocked attack because it said it would block
    }
    return 0;  // blockprepare: no block until it says so
}

// We run the defender: decide the hit on its own state, as its player saw it"""),
# The answer: the script's handler, or stock-symbol code
("""    if (attacker->HasFunction("void OnHitResult(int target, const string &in type, int result)")) {
        attacker->Execute("OnHitResult(" + to_string(target_id) + ", \\"" + m->type + "\\", " + to_string(m->result) + ");");
    }""", """    string t = to_string(target_id), r = to_string(m->result);

    if (attacker->HasFunction("void OnHitResult(int target, const string &in type, int result)")) {
        attacker->Execute("OnHitResult(" + t + ", \\"" + m->type + "\\", " + r + ");");
    } else if (m->type == "blockprepare") {
        // aschar.as (1.4 and later): a block switches the attack to its
        // blocked animation while the impact is still ahead; a dodge staggers
        attacker->Execute(
            "if(state == _attack_state && target_id == " + t + ") {"
            " if(" + r + " == _going_to_block && attack_getter.GetAsLayer() != 1 && knife_layer_id == -1 &&"
            " this_mo.rigged_object().anim_client().GetTimeUntilEvent(\\"attackimpact\\") > 0.0f) {"
            " HandleAIEvent(_attack_failed); SwitchToBlockedAnim(); }"
            " else if(" + r + " == _going_to_dodge) { HandleAIEvent(_attack_failed); block_stunned = 0.5f; block_stunned_by_id = " + t + "; } }");
    } else {
        attacker->Execute(
            "if(" + r + " == _block_impact) { block_stunned = 0.5f; block_stunned_by_id = " + t + "; }"
            " else if(" + r + " == _going_to_dodge) { HandleAIEvent(_attack_failed); block_stunned = 0.5f; block_stunned_by_id = " + t + "; }");
    }"""),
])

# The repository's character script again (Phase 4 had put this logic there)
orig = subprocess.run(["git", "show", "245fe48:Data/Scripts/aschar.as"], capture_output=True).stdout
open("Data/Scripts/aschar.as", "wb").write(orig)
print("ok")
