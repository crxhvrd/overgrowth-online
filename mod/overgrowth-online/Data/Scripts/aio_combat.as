// Overhaul: close-combat sense (0.31.0), included by enemycontrol.as
//
// What makes a hit land (aschar.as, being hit): a fighter passively blocks
// whatever it faces unless it is attacking itself, was startled, has worn its
// block out (block_health: 0.3-0.8 a blocked hit, recovers slowly), or the
// hit is a blade and it holds nothing that blocks a blade (a knife blocks
// nothing, and nothing blocks a knife). NPCs block nothing from their rear
// half. Every unblocked bare-handed hit knocks the defender down; the
// attack's force only sets how hard (and how hard it lands). High attacks
// miss a crouched fighter, low ones an airborne one. Damage is the same for
// every bare-handed strike.
//
// So: strike at openings, pick the move that lands, kick the fallen while
// they get up, let the fighter in front hold the target's eyes while one
// behind strikes, and fetch the best weapon in reach rather than the nearest.

bool aio_combat = true;

// ---- openings ------------------------------------------------------------------

const int kOpenBack = 1;       // we are in its rear half: it can't block us
const int kOpenBusy = 2;       // swinging at someone else, or reeling from a hit
const int kOpenGuard = 4;      // its block is worn out
const int kOpenStartled = 8;
const int kOpenBlade = 16;     // our blade, and it holds nothing that blocks one
const int kOpenDown = 32;      // on the ground but awake

int aio_open = 0;
bool aio_guard_up = false;     // facing us, guard intact, not busy
float aio_open_next = 0.0f;
float aio_pin_log_next = 0.0f;

// Would a hit from this side get through? (players: by difficulty, as the engine)
bool AIOCantBlockFrom(MovementObject@ t, const vec3 &in from) {
    vec3 front = AIOFlat(t.GetFacing());
    vec3 to_us = AIOFlat(from - t.position);

    if(length_squared(front) < 0.01f || length_squared(to_us) < 0.0001f) {
        return false;
    }

    float tolerance = t.controlled ? mix(1.1f, 0.0f, game_difficulty) : 0.0f;
    return dot(normalize(front), normalize(to_us)) < -tolerance - 0.05f;
}

bool AIOHoldsBlockingWeapon(MovementObject@ t) {
    int weap = GetCharPrimaryWeapon(t);
    return ItemExists(weap) && ReadItemID(weap).GetLabel() != "knife";
}

// Our blade against someone who can't block it
bool AIOBladeOpen(MovementObject@ t) {
    int weap = weapon_slots[primary_weapon_slot];

    if(!AIOIsBlade(weap)) {
        return false;
    }

    return ReadItemID(weap).GetLabel() == "knife" || !AIOHoldsBlockingWeapon(t);
}

string AIOOpenNames() {
    string s;

    if((aio_open & kOpenBack) != 0) s += "back,";
    if((aio_open & kOpenBusy) != 0) s += "busy,";
    if((aio_open & kOpenGuard) != 0) s += "guard_broken,";
    if((aio_open & kOpenStartled) != 0) s += "startled,";
    if((aio_open & kOpenBlade) != 0) s += "blade,";
    if((aio_open & kOpenDown) != 0) s += "down,";

    return s;
}

void AIOReadOpenings(MovementObject@ t) {
    aio_open = 0;
    aio_guard_up = false;

    if(!aio_on || !aio_combat || t.GetIntVar("knocked_out") != _awake) {
        return;
    }

    int t_state = t.GetIntVar("state");

    if(t_state == _ragdoll_state) {
        aio_open |= kOpenDown;
        return;
    }

    if(AIOCantBlockFrom(t, this_mo.position)) {
        aio_open |= kOpenBack;
    }

    if((t_state == _attack_state && t.GetIntVar("target_id") != this_mo.GetID()) || t_state == _hit_reaction_state) {
        aio_open |= kOpenBusy;
    }

    if(t.GetFloatVar("block_health") < 0.3f && t.GetIntVar("species") != _wolf) {
        aio_open |= kOpenGuard;  // (a wolf's guard never quite wears out)
    }

    if(!t.controlled && t.GetBoolVar("startled")) {
        aio_open |= kOpenStartled;
    }

    if(AIOBladeOpen(t)) {
        aio_open |= kOpenBlade;
    }

    vec3 front = AIOFlat(t.GetFacing());
    vec3 to_us = AIOFlat(this_mo.position - t.position);

    if((aio_open & ~kOpenBlade) == 0 && t_state != _attack_state && length_squared(front) > 0.01f &&
            length_squared(to_us) > 0.0001f && dot(normalize(front), normalize(to_us)) > 0.3f) {
        aio_guard_up = true;
    }
}

// Strike now: an opening is a hit that lands
bool AIOTryOpeningStrike(MovementObject@ t, float dist) {
    const int kStrikeOpenings = kOpenBack | kOpenBusy | kOpenGuard | kOpenStartled;

    if((aio_open & kStrikeOpenings) == 0 || !combat_allowed || startled || dont_attack_before > time ||
            time < aio_open_next || time < aio_breath_until) {
        return false;
    }

    if(state != _movement_state || sub_goal == _escape_surround) {
        return false;
    }

    // Bare hands against a blade: the matchup times it, only back and busy count
    if(aio_match == -1 && (aio_open & (kOpenBack | kOpenBusy)) == 0) {
        return false;
    }

    float reach = AIOReach(weapon_slots[primary_weapon_slot]) + 0.4f;

    if(dist > reach + 1.0f) {
        return false;
    }

    aio_open_next = time + 0.35f;

    if(RangedRandomFloat(0.0f, 1.0f) > mix(0.4f, 0.9f, game_difficulty) + aio_tr_aggr * 0.5f) {
        return false;
    }

    SetSubGoal(_rush_and_attack);
    sub_goal_pick_time = time + RangedRandomFloat(0.6f, 1.0f);
    ai_attacking = true;
    AIOLog("open_strike " + AIOOpenNames() + " d=" + dist);
    return true;
}

// The target faces us with its guard up while a teammate stands behind it:
// hold its attention (close in, threaten, don't commit) so the one behind
// lands the hit
bool AIOPinning(MovementObject@ t, float dist) {
    if(!aio_on || !aio_combat || !aio_group || !combat_allowed || !aio_guard_up || dist > 4.0f) {
        return false;
    }

    int t_id = t.GetID();
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ ally = ReadCharacter(i);
        int id = ally.GetID();

        if(id == my_id || id == t_id || !this_mo.OnSameTeam(ally) || AIOIsNeutral(ally) ||
                ally.GetIntVar("knocked_out") != _awake || ally.GetIntVar("state") == _ragdoll_state ||
                distance(ally.position, t.position) > 3.2f) {
            continue;
        }

        if(!ally.controlled && (!AIOIsAIOBrain(ally) || ally.GetIntVar("chase_target_id") != t_id)) {
            continue;
        }

        if(AIOCantBlockFrom(t, ally.position)) {
            if(time > aio_pin_log_next) {
                aio_pin_log_next = time + 2.0f;
                AIOLog("pin for=" + id);
            }

            return true;
        }
    }

    return false;
}

void AIOUpdateCombat(MovementObject@ t) {
    AIOReadOpenings(t);

    if(aio_on && aio_combat) {
        AIOTryOpeningStrike(t, xz_distance(this_mo.position, t.position));
    }

    AIOUpdateDisengage(t);
}

// ---- getting away: hit and jump, and kicking a chaser --------------------------
// A hop launches at run speed and air control keeps adding speed with no drag,
// so hops out-pace a runner, and in the air nothing low lands and nobody grabs
// you. So when we break off (hit and run, a breather, falling back, regrouping)
// we leave by leaping and hopping, not by walking backwards. And someone who
// runs after us is running onto a kick: turn and fast-kick it.

bool aio_chaser_kick = true;
float aio_chaser_kick_next = 0.0f;
float aio_disengage_hop_next = 0.0f;
bool aio_was_disengaging = false;

bool AIODisengaging() {
    return time < aio_hitrun_until || time < aio_breath_until || time < aio_fallback_until || time < aio_regroup_until;
}

// It runs at us from kicking distance: meet it with a flat, fast flying kick
bool AIOTryChaserKick(MovementObject@ t, const string &in kind) {
    if(!aio_on || !aio_chaser_kick || aio_jk <= 0.0f || !AIOCanJumpKick() || time < aio_chaser_kick_next ||
            aio_jk_active || jump_delay > 0.0f || !AIOCanLeaveGround()) {
        return false;
    }

    if(t.GetIntVar("knocked_out") != _awake || t.GetIntVar("state") == _ragdoll_state || !t.GetBoolVar("on_ground") ||
            t.GetIntVar("species") == _wolf) {
        return false;  // (kicking a wolf just feeds it)
    }

    vec3 to_me = AIOFlat(this_mo.position - t.position);
    float dist = length(to_me);

    if(dist < 2.4f || dist > 5.5f || abs(t.position.y - this_mo.position.y) > 1.2f) {
        return false;
    }

    float closing = dot(t.velocity, to_me / dist);

    if(closing < 3.0f) {
        return false;  // walking after us is not running onto a kick
    }

    aio_chaser_kick_next = time + 0.5f;  // decide about twice a second while it closes in
    float chance = aio_jk * mix(0.35f, 0.85f, game_difficulty) * AIOTrait(aio_tr_jk);

    if(RangedRandomFloat(0.0f, 1.0f) > chance || !AIOClearLine(this_mo.position, t.position) ||
            AIOTeammateJumpKicking(t.GetID())) {
        return false;
    }

    if(!AIOLaunchJumpKickAt(t, kind, true)) {
        return false;
    }

    aio_chaser_kick_next = time + RangedRandomFloat(3.0f, 5.0f);
    AIOLog(kind + " dist=" + dist + " closing=" + closing);
    return true;
}

// Fleeing (goal _flee): the chase target is the pursuer
bool AIOTryFleeChaserKick() {
    if(chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return false;
    }

    return AIOTryChaserKick(ReadCharacterID(chase_target_id), "flee_kick");
}

// Every tick while fighting
void AIOUpdateDisengage(MovementObject@ t) {
    bool now = AIODisengaging();
    bool started = now && !aio_was_disengaging;
    aio_was_disengaging = now;

    vec3 to_me = AIOFlat(this_mo.position - t.position);
    float dist = length(to_me);
    to_me = dist > 0.01f ? to_me / dist : this_mo.GetFacing() * -1.0f;
    bool moving_away = dot(AIOFlat(this_mo.velocity), to_me) > 1.5f;

    // Backing off (or just moving away) and it comes running: kick it
    if((now || moving_away) && AIOTryChaserKick(t, "chaser_kick")) {
        return;
    }

    if(!now || !aio_escape_on || !AIOCanJumpKick() || !AIOCanLeaveGround()) {
        return;
    }

    // The moment we break off from up close: leap out rather than back away on foot
    if(started && dist < 3.5f && species == _rabbit && time >= aio_escape_jump_next &&
            AIOLaunchEscapeJump(t.position, "hit_and_jump")) {
        return;
    }

    // Still backing off with it following: hop the rest of the way
    if(time > aio_disengage_hop_next && dist < 10.0f && dot(t.velocity, to_me) > -0.5f && AIOReadyToHop()) {
        vec3 dir = AIOMoveDir();

        if(dir != vec3(0.0f) && dot(dir, to_me) > 0.3f && AIOHopSafe(dir, dist < 6.0f)) {
            AIORequestHop(dist < 6.0f);
            aio_disengage_hop_next = time + 0.4f;
            AIOLog("hop_away dist=" + dist);
        }
    }
}


// ---- which attack --------------------------------------------------------------

// The moves this character makes right now (held weapon included: the engine's
// attack lookup returns the weapon's overrides)
class AIOAtk {
    int height = _medium;
    float force = 10000.0f;
    float impact = 0.4f;   // seconds to the hit
}

array<AIOAtk@> aio_atk_table;
int aio_atk_weapon = -2;
const array<string> kAIOAtkKeys = {"stationary", "stationary_close", "moving", "moving_close", "low"};

void AIOEnsureAttackTable() {
    int weap = weapon_slots[primary_weapon_slot];

    if(weap == aio_atk_weapon && aio_atk_table.length() == kAIOAtkKeys.length()) {
        return;
    }

    aio_atk_weapon = weap;
    aio_atk_table.resize(0);
    AttackScriptGetter getter;

    for(uint i = 0; i < kAIOAtkKeys.length(); ++i) {
        AIOAtk@ a = AIOAtk();
        string path = character_getter.GetAttackPath(kAIOAtkKeys[i]);

        if(path != "") {
            getter.Load(path);
            a.height = getter.GetHeight();
            a.force = getter.GetForce();
            float t = GetAnimationEventTime(getter.GetUnblockedAnimPath(), "attackimpact");

            if(t > 0.05f && t < 2.0f) {
                a.impact = t;
            }
        }

        aio_atk_table.insertLast(a);
    }
}

AIOAtk@ AIOAtkFor(const string &in cls, bool close) {
    AIOEnsureAttackTable();

    if(cls == "low") {
        return aio_atk_table[4];
    } else if(cls == "moving") {
        return aio_atk_table[close ? 3 : 2];
    }

    return aio_atk_table[close ? 1 : 0];
}

// ChooseAttack(), before the opponent-reading tweaks: score the three classes
// by what lands and what it does
void AIOSmartAttack(string &inout attack_str) {
    if(!aio_on || !aio_combat || chase_target_id == -1 || !MovementObjectExists(chase_target_id) ||
            RangedRandomFloat(0.0f, 1.0f) > mix(0.55f, 0.9f, game_difficulty)) {
        return;  // some of the stock randomness stays
    }

    MovementObject@ t = ReadCharacterID(chase_target_id);
    bool close = distance(this_mo.position, t.position) < GetCloseAttackRange();
    bool ducking = t.GetFloatVar("duck_amount") >= 0.5f;
    bool down = t.GetIntVar("state") == _ragdoll_state;
    bool airborne = !down && !t.GetBoolVar("on_ground");
    bool open = (aio_open & (kOpenBack | kOpenBusy | kOpenGuard | kOpenStartled)) != 0;
    bool finish = t.GetFloatVar("temp_health") < 0.35f;
    bool incoming = t.GetIntVar("state") == _attack_state && t.GetIntVar("target_id") == this_mo.GetID();
    array<string> classes = {"stationary", "moving", "low"};
    string best = attack_str;
    float best_score = -1.0e6f;
    string why;

    for(uint i = 0; i < classes.length(); ++i) {
        AIOAtk@ a = AIOAtkFor(classes[i], close);
        float s = RangedRandomFloat(0.0f, mix(0.9f, 0.35f, game_difficulty));  // keep them guessing

        if(a.height == _high && ducking) {
            s -= 10.0f;  // sails over a crouch
        }

        if(a.height == _low && airborne) {
            s -= 10.0f;  // under a jump
        }

        if(down) {
            // "moving" at a body on the ground is the kick down at it
            s += classes[i] == "moving" ? 2.0f : (classes[i] == "low" ? -1.0f : -0.3f);
        }

        if(open || finish) {
            s += a.force / 20000.0f;  // it will land: throw it down hard
        }

        if(incoming) {
            s += (0.45f - a.impact) * 4.0f;  // trading blows: the quicker one lands first
        } else if(aio_guard_up && !open) {
            s += (0.45f - a.impact) * 2.0f;  // into a guard: short, cheap to recover from
        }

        if(s > best_score) {
            best_score = s;
            best = classes[i];
        }
    }

    why = ducking ? "ducking" : (airborne ? "airborne" : (down ? "down" : (incoming ? "trade" :
          (open ? "open" : (finish ? "finish" : (aio_guard_up ? "guard" : "even"))))));

    if(best != attack_str || why != "even") {
        AIOLog("smart_attack " + best + " why=" + why);
    }

    attack_str = best;
}

// After the variety tweaks: never a high attack at a crouch or a low one at a jump
void AIOAttackVeto(string &inout attack_str) {
    if(!aio_on || !aio_combat || chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return;
    }

    MovementObject@ t = ReadCharacterID(chase_target_id);
    bool close = distance(this_mo.position, t.position) < GetCloseAttackRange();
    bool ducking = t.GetFloatVar("duck_amount") >= 0.5f;
    bool airborne = t.GetIntVar("state") != _ragdoll_state && !t.GetBoolVar("on_ground");
    AIOAtk@ a = AIOAtkFor(attack_str, close);

    if(!((a.height == _high && ducking) || (a.height == _low && airborne))) {
        return;
    }

    array<string> classes = {"stationary", "moving", "low"};

    for(uint i = 0; i < classes.length(); ++i) {
        AIOAtk@ b = AIOAtkFor(classes[i], close);

        if(!((b.height == _high && ducking) || (b.height == _low && airborne))) {
            AIOLog("attack_veto " + attack_str + " -> " + classes[i]);
            attack_str = classes[i];
            return;
        }
    }
}

// ---- the ground ----------------------------------------------------------------

// PickAttackSubGoal(): kick it while it gets up? A fallen fighter can't block
float AIOGroundPunishChance() {
    float stock = mix(0.0f, p_ground_aggression, game_difficulty);

    if(!aio_on || !aio_combat || chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return stock;
    }

    MovementObject@ t = ReadCharacterID(chase_target_id);
    float chance = max(stock, mix(0.35f, 0.85f, game_difficulty));

    if(t.GetFloatVar("temp_health") < 0.5f) {
        chance += 0.15f;
    }

    // Not with its friends standing over it: that's how you get jumped
    if(AIONearbyEnemies(4.0f) >= 2) {
        chance *= 0.5f;
    }

    return min(1.0f, chance);
}

// ---- weapons -------------------------------------------------------------------

int aio_weapon_claim = -1;  // the weapon we are going for (teammates read it)
int aio_weapon_logged = -1;

float AIOWeaponValue(ItemObject@ io) {
    string label = io.GetLabel();

    if(label == "knife") {
        return 1.0f;   // nothing blocks it
    } else if(label == "sword") {
        return 0.9f;
    } else if(label == "rapier" || label == "spear") {
        return 0.85f;
    } else if(label == "big_sword") {
        return species == _dog ? 1.0f : 0.75f;
    } else if(label == "staff") {
        return 0.6f;
    }

    return 0.5f;
}

// CheckForNearbyWeapons(): the best weapon worth the walk, not just the nearest
int AIOPickWeapon(float range) {
    // Being swung at right now: fight, don't turn your back to fetch
    if(chase_target_id != -1 && MovementObjectExists(chase_target_id)) {
        MovementObject@ t = ReadCharacterID(chase_target_id);

        if(t.GetIntVar("knocked_out") == _awake && distance(t.position, this_mo.position) < 2.2f &&
                t.GetIntVar("state") == _attack_state && t.GetIntVar("target_id") == this_mo.GetID()) {
            return -1;
        }
    }

    int my_id = this_mo.GetID();
    int best = -1;
    float best_score = 0.0f;

    for(uint i = 0; i < situation.known_items.size(); ++i) {
        int id = situation.known_items[i].id;

        if(!ItemExists(id)) {  // (ids are reused)
            continue;
        }

        ItemObject@ io = ReadItemID(id);

        if(io.IsHeld() || io.GetType() != _weapon || io.GetLabel() == "scabbard") {
            continue;
        }

        int stuck = io.StuckInWhom();

        if(stuck != -1 && ReadCharacterID(stuck).GetIntVar("knocked_out") == _awake) {
            continue;
        }

        vec3 pos = io.GetPhysicsPosition();
        float d = distance(pos, this_mo.position);

        if(d > range || !IsOnNavMesh(io, pos)) {
            continue;
        }

        float score = AIOWeaponValue(io) * 10.0f - d * 0.7f;
        bool taken = false;

        for(int j = 0, len = GetNumCharacters(); j < len && !taken; ++j) {
            MovementObject@ other = ReadCharacter(j);

            if(other.GetID() == my_id || other.GetIntVar("knocked_out") != _awake) {
                continue;
            }

            float od = distance(other.position, pos);

            if(this_mo.OnSameTeam(other)) {
                // A teammate closer to it already went for it
                taken = AIOIsAIOBrain(other) && other.GetIntVar("aio_weapon_claim") == id && od < d;
            } else if(!AIOIsNeutral(other) && od < 3.0f) {
                score -= 3.0f;  // an enemy stands over it
            }
        }

        if(!taken && score > best_score) {
            best_score = score;
            best = id;
        }
    }

    aio_weapon_claim = best;

    if(best != -1 && best != aio_weapon_logged) {
        aio_weapon_logged = best;
        AIOLog("weapon_pick item=" + best + " label=" + ReadItemID(best).GetLabel() + " score=" + best_score);
    }

    return best;
}

// ---- targets (AIOChooseTarget) -------------------------------------------------

float AIOCombatTargetBonus(MovementObject@ enemy, float dist, string &inout why) {
    if(!aio_combat) {
        return 0.0f;
    }

    float bonus = 0.0f;
    int my_id = this_mo.GetID();

    // An ally in its grip (a choke, a throw): break it up
    if(enemy.GetIntVar("tethered") != _TETHERED_FREE) {
        int held = enemy.GetIntVar("tether_id");

        if(held != -1 && held != my_id && MovementObjectExists(held) && this_mo.OnSameTeam(ReadCharacterID(held))) {
            bonus += 2.0f;
            why = "free_ally";
        }
    }

    if(dist > 6.0f) {
        return bonus;
    }

    // Down but awake: it can't block while it gets up
    if(enemy.GetIntVar("state") == _ragdoll_state) {
        bonus += enemy.GetFloatVar("temp_health") < 0.5f ? 0.9f : 0.6f;

        if(why == "near" || why == "isolated") {
            why = "downed";
        }
    } else if(AIOCantBlockFrom(enemy, this_mo.position)) {
        bonus += 0.4f;

        if(why == "near") {
            why = "its_back";
        }
    }

    if(enemy.GetFloatVar("block_health") < 0.3f && enemy.GetIntVar("species") != _wolf) {
        bonus += 0.4f;

        if(why == "near") {
            why = "guard_broken";
        }
    }

    if(AIOBladeOpen(enemy)) {
        bonus += 0.5f;

        if(why == "near") {
            why = "blade_open";
        }
    }

    return bonus;
}
