"""Phase 4 (combat), part 1, script side (Data/Scripts/aschar.as): proxies
stand still, attacks on characters other machines run become hit requests,
and the defender's answer arrives in OnHitResult. Run from the repo root."""
P = "Data/Scripts/aschar.as"
s = open(P, encoding="utf-8").read()


def rep(old, new):
    global s
    assert s.count(old) == 1, (old[:90], s.count(old))
    s = s.replace(old, new)


if "Modified By: Overhaul Online" not in s:
    rep("//      Developer: Wolfire Games LLC\n", "//      Developer: Wolfire Games LLC\n//    Modified By: Overhaul Online (crxhvrd)\n")

# A proxy's script stands still: its owner runs it, the engine writes the
# owner's game state into these variables (MovementObject::ApplyNetworkVitals)
rep("""    if(frozen && !this_mo.controlled) {
        if(knocked_out != _dead) {""", """    // Overhaul Online: another machine runs this character; we show its stream
    // and the engine keeps its game state here up to date
    if(this_mo.network_proxy) {
        Timestep ts(time_step, num_frames);
        time += ts.step();
        return;
    }

    if(frozen && !this_mo.controlled) {
        if(knocked_out != _dead) {""")

# The attacker's two calls into the defender
rep("""            int return_val = ReadCharacterID(target_id).WasHit(
                event, attack_getter.GetPath(), dir, world_pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);""",
    """            int return_val = NetWasHit(target_id, event, attack_getter.GetPath(), dir, world_pos,
                (_attack_range + range_extend) * this_mo.rigged_object().GetCharScale());""")

rep("""                hit = ReadCharacterID(target_id).WasHit(
                    "grabbed", attack_getter.GetPath(), direction, this_mo.position, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);""",
    """                hit = NetWasHit(target_id, "grabbed", attack_getter.GetPath(), direction, this_mo.position, GetCloseAttackRange() + 1.0f);""")

rep("""int WasHit(string type, string attack_path, vec3 dir, vec3 pos, int attacker_id, float attack_damage_mult, float attack_knockback_mult) {""",
    """// Overhaul Online: hitting a character another machine runs. Its owner decides
// the outcome on its own state (block, dodge, damage: what its player saw is
// what counts); here the attack goes on with a best guess and corrects itself
// when the answer comes back (OnHitResult)
int NetWasHit(int target, const string &in type, const string &in attack_path, vec3 dir, vec3 pos, float max_range) {
    MovementObject@ char = ReadCharacterID(target);

    if(!char.network_proxy || this_mo.network_proxy) {
        return char.WasHit(type, attack_path, dir, pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult);
    }

    Online_SendHitRequest(target, type, attack_path, dir, pos, this_mo.getID(), p_attack_damage_mult, p_attack_knockback_mult, max_range);

    if(type == "attackimpact") {
        return char.GetIntVar("knocked_out") == _awake ? _hit : _miss;
    } else if(type == "attackblocked") {
        return _block_impact;  // we switched to the blocked attack because it said it would block
    }

    return _miss;  // blockprepare: no block until it says so; grabbed: throws across machines come later
}

void OnHitResult(int target, const string &in type, int result) {
    if(type == "blockprepare") {
        if(state != _attack_state || target != target_id) {
            return;  // that attack is over
        }

        if(result == _going_to_block && attack_getter.GetAsLayer() != 1 && knife_layer_id == -1 &&
                this_mo.rigged_object().anim_client().GetTimeUntilEvent("attackimpact") > 0.0f) {
            HandleAIEvent(_attack_failed);
            SwitchToBlockedAnim();
        } else if(result == _going_to_dodge) {
            HandleAIEvent(_attack_failed);
            block_stunned = species == _wolf ? 1.0f : 0.5f;
            block_stunned_by_id = target;
        }
    } else if(result == _block_impact) {
        block_stunned = 0.5f;
        block_stunned_by_id = target;
    } else if(result == _going_to_dodge) {
        HandleAIEvent(_attack_failed);
        block_stunned = species == _wolf ? 1.0f : 0.5f;
        block_stunned_by_id = target;
    }
}

int WasHit(string type, string attack_path, vec3 dir, vec3 pos, int attacker_id, float attack_damage_mult, float attack_knockback_mult) {""")

open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")
