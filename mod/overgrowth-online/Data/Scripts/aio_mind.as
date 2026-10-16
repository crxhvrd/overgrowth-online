//-----------------------------------------------------------------------------
//           Name: aio_mind.as
//    Description: Overhaul AI: what makes each NPC its own fighter and hard
//                 to read. Included by enemycontrol.as next to aio_brain.as.
//                 - personality: a fighting style per NPC (kicker, boxer,
//                   trickster, brawler, skirmisher, balanced) that scales the
//                   brain's behaviours, picked by species or the character's
//                   "AI - Style" parameter
//                 - reading the opponent: what got blocked or reversed
//                   against whom; adapts attack choice, feints and throws
//                 - rhythm: pauses and bursts instead of a steady cadence
//                 - breathers: a hurt fighter backs off while its temporary
//                   damage heals (0.05 per second in aschar.as)
//                 - no repeats: the same special move is rarely used twice
//                   in a row
//-----------------------------------------------------------------------------

// ---- settings (Overhaul AI tab) ---------------------------------------------

bool aio_personality = true;
bool aio_adapt = true;
bool aio_tempo = true;
bool aio_breath = true;

void AIOMindRefreshSettings() {
    aio_personality = AIOCfgBool("aio_personality", true);
    aio_adapt = AIOCfgBool("aio_adapt", true);
    aio_tempo = AIOCfgBool("aio_tempo", true);
    aio_breath = AIOCfgBool("aio_breath", true);
    aio_momentum = AIOCfgBool("aio_momentum", true);
    aio_footwork = AIOCfgBool("aio_footwork", true);
    aio_combat = AIOCfgBool("aio_combat_sense", true);
}

// ---- personality ------------------------------------------------------------

enum AIOStyle {
    kStyleBalanced,
    kStyleKicker,
    kStyleBoxer,
    kStyleTrickster,
    kStyleBrawler,
    kStyleSkirmisher,
    kStyleCount
}

const array<string> kStyleNames = {"balanced", "kicker", "boxer", "trickster", "brawler", "skirmisher"};

int aio_style = -1;

// Multipliers on the brain's behaviours (1 = as tuned)
float aio_tr_jk = 1.0f;       // jump kicks
float aio_tr_air = 1.0f;      // air combat: dodging, intercepts, anti-air
float aio_tr_feint = 1.0f;
float aio_tr_throw = 1.0f;    // weapon throwing
float aio_tr_bait = 1.0f;     // fake retreats
float aio_tr_hops = 1.0f;     // bunny-hop chasing
float aio_tr_rolls = 1.0f;
float aio_tr_breath = 1.0f;   // backing off to recover
float aio_tr_tempo = 1.0f;    // how often the rhythm changes
float aio_tr_grab = 1.0f;     // throwing an opponent who keeps blocking
float aio_tr_aggr = 0.0f;     // added to the stock aggression roll
float aio_tr_patience = 1.0f; // scales the wait after a blocked attack

void AIOSetTraits(float jk, float air, float feint, float throw_, float bait, float hops, float rolls, float breath,
        float tempo, float grab, float aggr, float patience) {
    aio_tr_jk = jk;
    aio_tr_air = air;
    aio_tr_feint = feint;
    aio_tr_throw = throw_;
    aio_tr_bait = bait;
    aio_tr_hops = hops;
    aio_tr_rolls = rolls;
    aio_tr_breath = breath;
    aio_tr_tempo = tempo;
    aio_tr_grab = grab;
    aio_tr_aggr = aggr;
    aio_tr_patience = patience;
}

void AIOApplyStyle(int style) {
    aio_style = style;

    // jk, air, feint, throw, bait, hops, rolls, breath, tempo, grab, aggr, patience
    switch(style) {
        case kStyleKicker:     AIOSetTraits(1.8f, 1.4f, 0.7f, 0.8f, 1.3f, 1.3f, 1.0f, 0.8f, 1.0f, 0.7f, 0.05f, 0.9f); break;
        case kStyleBoxer:      AIOSetTraits(0.4f, 0.8f, 1.4f, 0.6f, 0.6f, 0.6f, 0.8f, 0.7f, 1.3f, 1.8f, 0.10f, 0.6f); break;
        case kStyleTrickster:  AIOSetTraits(1.0f, 1.1f, 2.2f, 1.2f, 2.0f, 1.1f, 1.6f, 1.0f, 1.8f, 1.2f, -0.05f, 1.2f); break;
        case kStyleBrawler:    AIOSetTraits(0.9f, 0.8f, 0.4f, 0.7f, 0.3f, 1.0f, 0.6f, 0.2f, 0.7f, 1.3f, 0.25f, 0.4f); break;
        case kStyleSkirmisher: AIOSetTraits(1.2f, 1.2f, 1.0f, 2.0f, 1.4f, 1.8f, 1.3f, 1.8f, 1.2f, 0.6f, -0.10f, 1.3f); break;
        default:               AIOSetTraits(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f); break;
    }
}

// Weighted by species: dogs box and brawl, cats and rats trick and skirmish,
// rabbits kick, wolves brawl
array<float> AIOWeights(float a, float b, float c, float d, float e, float f) {
    array<float> w = {a, b, c, d, e, f};
    return w;
}

int AIORollStyle() {
    array<float> w;

    // balanced, kicker, boxer, trickster, brawler, skirmisher
    switch(species) {
        case _cat:  w = AIOWeights(1.0f, 2.0f, 1.0f, 3.0f, 0.5f, 2.0f); break;
        case _dog:  w = AIOWeights(2.0f, 0.3f, 3.0f, 0.5f, 3.0f, 0.7f); break;
        case _rat:  w = AIOWeights(1.0f, 1.0f, 0.5f, 3.0f, 1.0f, 3.0f); break;
        case _wolf: w = AIOWeights(1.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f); break;
        default:    w = AIOWeights(2.0f, 3.0f, 1.5f, 1.5f, 1.5f, 1.5f); break;
    }

    float total = 0.0f;

    for(int i = 0; i < kStyleCount; ++i) {
        total += w[i];
    }

    float r = RangedRandomFloat(0.0f, total);

    for(int i = 0; i < kStyleCount; ++i) {
        r -= w[i];

        if(r <= 0.0f) {
            return i;
        }
    }

    return kStyleBalanced;
}

// Once per character: the "AI - Style" parameter, or a roll by species
void AIOPickStyle() {
    if(aio_style != -1) {
        return;
    }

    int style = -1;
    string wanted = p_aio_style;

    for(uint i = 0; i < wanted.length(); ++i) {
        if(wanted[i] >= 65 && wanted[i] <= 90) {
            wanted[i] = wanted[i] + 32;  // the engine has ToUpper only
        }
    }

    for(int i = 0; i < kStyleCount; ++i) {
        if(wanted == kStyleNames[i]) {
            style = i;
        }
    }

    AIOApplyStyle(style == -1 ? AIORollStyle() : style);
    AIOLog("style " + kStyleNames[aio_style] + (style == -1 ? " (rolled)" : " (set)"));
}

// The style's multipliers on the menu's scales (all 1 when personalities are off)
void AIOApplyTraitScales() {
    if(!aio_personality) {
        return;
    }

    AIOPickStyle();
    aio_jk *= aio_tr_jk;
    aio_air *= aio_tr_air;
    aio_feint *= aio_tr_feint;
    aio_throw *= aio_tr_throw;
}

float AIOTrait(float trait) {
    return aio_personality ? trait : 1.0f;
}

float AIOAggressionBonus() {
    return (aio_on && aio_personality) ? aio_tr_aggr : 0.0f;
}

// ---- reading the opponent -------------------------------------------------------

class AIOFoe {
    int id = -1;
    float attempts = 0.0f;    // our attacks on it
    float blocked = 0.0f;     // ...that it blocked
    float reversed = 0.0f;    // ...that it reversed (threw us)
    float rev_stationary = 0.0f;
    float rev_moving = 0.0f;
    float rev_low = 0.0f;
    float caught = 0.0f;      // our thrown weapons it caught
    float seen = 0.0f;
}

array<AIOFoe@> aio_foes;
float aio_last_attack_time = -10.0f;
int aio_last_attack_target = -1;
string aio_last_attack_kind = "";
float aio_foes_decay_next = 0.0f;

AIOFoe@ AIOFoeFor(int id) {
    for(uint i = 0; i < aio_foes.length(); ++i) {
        if(aio_foes[i].id == id) {
            aio_foes[i].seen = time;
            return aio_foes[i];
        }
    }

    if(aio_foes.length() >= 6) {
        uint oldest = 0;

        for(uint i = 1; i < aio_foes.length(); ++i) {
            if(aio_foes[i].seen < aio_foes[oldest].seen) {
                oldest = i;
            }
        }

        aio_foes.removeAt(oldest);
    }

    AIOFoe@ foe = AIOFoe();
    foe.id = id;
    foe.seen = time;
    aio_foes.insertLast(foe);
    return foe;
}

// Habits fade: what worked a minute ago may not now
void AIODecayFoes() {
    if(time < aio_foes_decay_next) {
        return;
    }

    aio_foes_decay_next = time + 10.0f;

    for(uint i = 0; i < aio_foes.length(); ++i) {
        AIOFoe@ f = aio_foes[i];
        f.attempts *= 0.85f;
        f.blocked *= 0.85f;
        f.reversed *= 0.85f;
        f.rev_stationary *= 0.85f;
        f.rev_moving *= 0.85f;
        f.rev_low *= 0.85f;
        f.caught *= 0.85f;
    }
}

float AIOBlockRate(int id) {
    if(!aio_adapt || id == -1) {
        return 0.0f;
    }

    AIOFoe@ f = AIOFoeFor(id);
    return f.blocked / max(3.0f, f.attempts);  // a few lucky blocks prove nothing
}

void AIOMindOnAttackStart() {
    if(chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return;
    }

    aio_last_attack_time = time;
    aio_last_attack_target = chase_target_id;
    aio_last_attack_kind = aio_prev_attack;
    AIOFoeFor(chase_target_id).attempts += 1.0f;
}

// Our attack was blocked (stock _attack_failed)
void AIOMindOnAttackFailed() {
    if(!aio_on) {
        return;
    }

    if(time - aio_last_attack_time < 1.5f && aio_last_attack_target != -1) {
        AIOFoeFor(aio_last_attack_target).blocked += 1.0f;
    }

    // Patient styles wait longer after a block, brawlers press on
    dont_attack_before = time + RangedRandomFloat(0.0f, 1.4f) * AIOTrait(aio_tr_patience);
}

// We were thrown; mid-attack that means the attack was reversed
void AIOMindOnThrown() {
    if(!aio_on || time - aio_last_attack_time > 1.5f || aio_last_attack_target == -1) {
        return;
    }

    AIOFoe@ f = AIOFoeFor(aio_last_attack_target);
    f.reversed += 1.0f;

    if(aio_last_attack_kind == "stationary") {
        f.rev_stationary += 1.0f;
    } else if(aio_last_attack_kind == "moving") {
        f.rev_moving += 1.0f;
    } else if(aio_last_attack_kind == "low") {
        f.rev_low += 1.0f;
    }

    AIOLog("reversed_by=" + aio_last_attack_target + " attack=" + aio_last_attack_kind + " times=" + f.reversed);
}

// Ground attack choice against this opponent: away from what it reverses,
// low or varied against a wall of blocks
void AIOAdaptAttack(string &inout attack_str) {
    if(!aio_on || !aio_adapt || chase_target_id == -1) {
        return;
    }

    AIOFoe@ f = AIOFoeFor(chase_target_id);
    float rev = attack_str == "stationary" ? f.rev_stationary : (attack_str == "moving" ? f.rev_moving : f.rev_low);

    if(rev >= 1.5f && RangedRandomFloat(0.0f, 1.0f) < 0.7f) {
        // Least reversed of the other two
        string a = attack_str == "stationary" ? "moving" : "stationary";
        string b = attack_str == "low" ? "moving" : "low";
        float ra = a == "stationary" ? f.rev_stationary : (a == "moving" ? f.rev_moving : f.rev_low);
        float rb = b == "stationary" ? f.rev_stationary : (b == "moving" ? f.rev_moving : f.rev_low);
        string was = attack_str;
        attack_str = ra <= rb ? a : b;
        AIOLog("adapt_attack avoid=" + was + " use=" + attack_str);
        return;
    }

    if(AIOBlockRate(chase_target_id) > 0.5f && attack_str != "low" && RangedRandomFloat(0.0f, 1.0f) < 0.35f) {
        attack_str = "low";
        AIOLog("adapt_attack blocker_low");
    }
}

// More feints against a good blocker or a reverser
float AIOAdaptFeint() {
    if(!aio_on || !aio_adapt || chase_target_id == -1) {
        return 1.0f;
    }

    AIOFoe@ f = AIOFoeFor(chase_target_id);
    return 1.0f + AIOBlockRate(chase_target_id) * 1.5f + min(f.reversed, 3.0f) * 0.3f;
}

// Up close to an opponent that keeps blocking: throw it instead of striking
float aio_grab_next = 0.0f;
float aio_grab_until = 0.0f;  // the body asks every frame during a throw: keep saying yes

bool AIOWantsThrowBlocker() {
    if(time < aio_grab_until) {
        return true;
    }

    if(!aio_on || (!aio_adapt && !aio_combat) || chase_target_id == -1 || !MovementObjectExists(chase_target_id) || time < aio_grab_next) {
        return false;
    }

    MovementObject@ target = ReadCharacterID(chase_target_id);

    // A fresh guard squarely in our face: a grab goes through what a strike won't
    bool fresh_guard = aio_combat && aio_guard_up && target.GetFloatVar("block_health") > 0.6f;
    bool blocker = aio_adapt && AIOBlockRate(chase_target_id) >= 0.45f;

    if(distance(target.position, this_mo.position) > (fresh_guard && !blocker ? 1.5f : 1.8f) || (!blocker && !fresh_guard)) {
        return false;
    }

    aio_grab_next = time + RangedRandomFloat(2.0f, 4.0f);

    if(RangedRandomFloat(0.0f, 1.0f) > (blocker ? 0.5f : 0.3f) * AIOTrait(aio_tr_grab)) {
        return false;
    }

    aio_grab_until = time + 1.0f;
    AIOLog(blocker ? "throw_blocker rate=" + AIOBlockRate(chase_target_id) : "throw_guard");
    return true;
}

// ---- rhythm: pauses and bursts -------------------------------------------------

float aio_tempo_next = 0.0f;
float aio_tempo_pause_until = 0.0f;
float aio_tempo_burst_until = 0.0f;

void AIOUpdateTempo(MovementObject@ target, float dist) {
    if(!aio_tempo) {
        aio_tempo_pause_until = 0.0f;
        aio_tempo_burst_until = 0.0f;
        return;
    }

    if(time >= aio_tempo_next && dist < 5.0f && dist > 1.0f && state == _movement_state && !AIOChasingRetreat(target)) {
        aio_tempo_next = time + RangedRandomFloat(3.0f, 7.0f) / max(0.3f, AIOTrait(aio_tr_tempo));

        if(RangedRandomFloat(0.0f, 1.0f) < 0.5f) {
            // A beat of stillness that invites a move, then all at once
            aio_tempo_pause_until = time + RangedRandomFloat(0.4f, 0.9f);
            aio_tempo_burst_until = aio_tempo_pause_until + RangedRandomFloat(0.8f, 1.4f);
            AIOLog("tempo_pause");
        }
    }

    bool pressed = target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") == this_mo.GetID();

    if(time < aio_tempo_pause_until) {
        if(pressed) {
            aio_tempo_pause_until = time;  // it took the bait: answer at once
        } else {
            target_attack_range = max(target_attack_range, 2.2f);
            ai_attacking = false;
        }
    } else if(time < aio_tempo_burst_until) {
        target_attack_range = 0.0f;
        ai_attacking = true;
        dont_attack_before = 0.0f;
    }
}

// ---- breathers ------------------------------------------------------------------

float aio_breath_until = 0.0f;
float aio_breath_next = 0.0f;

void AIOUpdateBreath(MovementObject@ target, float dist) {
    if(!aio_breath) {
        aio_breath_until = 0.0f;
        return;
    }

    bool pressed = target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") == this_mo.GetID();

    if(time < aio_breath_until) {
        if(pressed || dist < 2.0f || temp_health >= permanent_health - 0.05f) {
            aio_breath_until = 0.0f;  // caught up with us, or healed: back to it
        } else {
            target_attack_range = 5.5f;
            ai_attacking = false;
        }

        return;
    }

    // Only worth it when a good share of the damage is temporary
    if(time < aio_breath_next || pressed || dist < 2.5f || temp_health > 0.55f || permanent_health - temp_health < 0.2f) {
        return;
    }

    aio_breath_next = time + RangedRandomFloat(8.0f, 12.0f);

    if(RangedRandomFloat(0.0f, 1.0f) < 0.5f * AIOTrait(aio_tr_breath)) {
        aio_breath_until = time + RangedRandomFloat(2.0f, 3.5f);
        AIOLog("catch_breath temp=" + temp_health + " perm=" + permanent_health);
    }
}

// ---- no repeats ------------------------------------------------------------------

string aio_last_special = "";
float aio_last_special_time = -10.0f;

// The same special move again within a few seconds: usually not
bool AIORepeatVeto(const string &in kind) {
    if(!aio_tempo) {
        return false;
    }

    if(kind == aio_last_special && time - aio_last_special_time < 5.0f && RangedRandomFloat(0.0f, 1.0f) < 0.45f) {
        return true;
    }

    aio_last_special = kind;
    aio_last_special_time = time;
    return false;
}


// ---- armed and unarmed: who holds a weapon changes the whole fight ----------
// A bare hand can't block a blade (aschar.as: no passive block against sharp
// damage without a blocking weapon), so trading blows with a sword is how an
// unarmed fighter dies. Each tick, with "Weapon sense" on:
//   unarmed vs blade   stay just outside its reach; strike only at openings
//                      (it just whiffed at us, it is busy with someone else,
//                      its back or side is turned, it is staggered or down);
//                      after a long stand-off take one calculated chance;
//                      never roll, hop or kick into it up close
//   blade vs unarmed   fight at the tip so it can't reach back; fewer flying
//                      kicks (they only risk the weapon)
//   blade vs blade     longer reach keeps its distance, shorter reach rushes in
// (The stock AI already runs for any weapon within reach when unarmed.)

int aio_match = 0;            // 0 even, 1 our blade vs bare hands, -1 bare hands vs a blade, 2 both armed
float aio_my_reach = 1.6f;
float aio_their_reach = 1.6f;
bool aio_target_was_swinging = false;
float aio_blade_opening_until = 0.0f;
float aio_blade_hold_since = -1.0f;
float aio_blade_patience = 5.0f;
string aio_match_logged = "";

float AIOReach(int weap) {
    if(weap == -1) {
        return 1.6f;
    }

    ItemObject@ io = ReadItemID(weap);
    string label = io.GetLabel();

    if(label == "knife") return 1.8f;
    if(label == "sword" || label == "rapier") return 2.3f;
    if(label == "big_sword") return 2.5f;
    if(label == "staff") return 2.8f;
    if(label == "spear") return 3.0f;
    return io.GetMass() < 1.5f ? 1.9f : 2.4f;  // unlabelled: machete, hammer...
}

bool AIOIsBlade(int weap) {
    return weap != -1 && ReadItemID(weap).GetType() == _weapon && ReadItemID(weap).GetLabel() != "scabbard";
}

void AIOUpdateMatchup(MovementObject@ target, float dist) {
    if(!aio_weapon_sense || species == _wolf) {
        aio_match = 0;
        return;
    }

    int mine = weapon_slots[primary_weapon_slot];
    int theirs = GetCharPrimaryWeapon(target);
    bool i_armed = AIOIsBlade(mine);
    bool they_armed = AIOIsBlade(theirs);
    aio_my_reach = AIOReach(i_armed ? mine : -1);
    aio_their_reach = AIOReach(they_armed ? theirs : -1);
    aio_match = (i_armed && they_armed) ? 2 : (i_armed ? 1 : (they_armed ? -1 : 0));
    string name = aio_match == 2 ? "blade_vs_blade" : (aio_match == 1 ? "blade_vs_unarmed" : (aio_match == -1 ? "unarmed_vs_blade" : "unarmed"));

    if(name != aio_match_logged) {
        aio_match_logged = name;
        AIOLog("matchup " + name + " reach=" + aio_my_reach + "/" + aio_their_reach);
    }

    int my_id = this_mo.GetID();
    bool swinging_at_me = target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") == my_id;

    // It swung at us and missed: the recovery is the opening
    if(aio_target_was_swinging && !swinging_at_me && state == _movement_state) {
        aio_blade_opening_until = time + 0.8f;
    }

    aio_target_was_swinging = swinging_at_me;

    if(aio_match == -1) {
        vec3 to_me = AIOFlat(this_mo.position - target.position);
        float d = length(to_me);
        bool facing_me = d > 0.1f && dot(AIOFlat(target.GetFacing()), to_me / d) > 0.3f;
        int their_state = target.GetIntVar("state");
        string why = "";

        if(time < aio_blade_opening_until) {
            why = "whiff";
        } else if(their_state == _attack_state && target.GetIntVar("target_id") != my_id) {
            why = "busy";
        } else if(their_state == _hit_reaction_state || their_state == _ragdoll_state) {
            why = "staggered";
        } else if(!facing_me) {
            why = "back";
        } else if(aio_blade_hold_since >= 0.0f && time - aio_blade_hold_since > aio_blade_patience) {
            why = "patience";  // a stand-off forever is no plan either: one calculated chance
            aio_blade_opening_until = time + 1.0f;
        }

        if(why != "") {
            if(aio_blade_hold_since >= 0.0f) {
                AIOLog("blade_opening why=" + why);
            }

            aio_blade_hold_since = -1.0f;
            target_attack_range = 0.0f;
            ai_attacking = true;
            dont_attack_before = 0.0f;
        } else {
            if(aio_blade_hold_since < 0.0f) {
                aio_blade_hold_since = time;
                aio_blade_patience = RangedRandomFloat(4.0f, 6.0f) * AIOTrait(aio_tr_patience);
            }

            target_attack_range = max(target_attack_range, aio_their_reach + 0.8f);
            ai_attacking = false;
        }

        return;
    }

    aio_blade_hold_since = -1.0f;

    if(aio_match == 1 || (aio_match == 2 && aio_my_reach > aio_their_reach + 0.4f)) {
        // At the tip of our reach: we hit, they can't hit back
        if(target_attack_range < aio_my_reach - 0.5f) {
            target_attack_range = aio_my_reach - 0.5f;
        }
    } else if(aio_match == 2 && aio_my_reach < aio_their_reach - 0.4f && !swinging_at_me) {
        // Shorter reach: inside the long weapon, where it is clumsy
        target_attack_range = 0.0f;
    }
}

// Flying and close kicks that would only feed a blade
bool AIOMatchupVeto(MovementObject@ target, const string &in kind) {
    if(!aio_weapon_sense) {
        return false;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(aio_match == -1 && time >= aio_blade_opening_until && dist < aio_their_reach + 1.2f) {
        AIOLog("blade_veto kind=" + kind);
        return true;  // into a blade from close range, with no opening
    }

    if(aio_match == 1 && RangedRandomFloat(0.0f, 1.0f) < 0.5f) {
        return true;  // the blade does more, a kick only risks losing it
    }

    return false;
}

// A roll toward someone holding a blade and facing us ends on the blade
bool AIORollIntoBlade(const string &in kind) {
    return aio_weapon_sense && aio_match == -1 && kind == "roll_in" && time >= aio_blade_opening_until;
}


// ---- knockback and momentum -------------------------------------------------
// In Overgrowth an attack's damage is fixed (aschar.as: attack damage x the
// "Attack Damage" parameter; a leg cannon also ignores passive block and
// strips 2 knockout shield). Speed works through the knockback: a character
// knocked into ragdoll keeps its own momentum plus the attack's push (which
// grows as its health drops), then takes impact damage on landing that
// grows with the impact, and loses knockout shield for every half second in
// the air. So with "Knockback and momentum" on:
//   - a kick is worth more when the target moves away from it (launched
//     further), has a wall or a drop right behind it (slammed or dropped), is
//     hurt (flies further), or is one leg cannon from a knockout
//   - positioning favours spots that put a wall behind the target
//   - with an attack about to land on it, it sidesteps instead of backing
//     straight away (its own momentum away from the hit would add to its
//     flight)

bool aio_momentum = true;
float aio_kb_bonus = 1.0f;       // last kick-value multiplier, for the launch log
string aio_kb_why = "";
float aio_kb_time = -10.0f;
float aio_sidestep_until = 0.0f;
float aio_sidestep_side = 1.0f;

// A wall within reach behind the target, along the kick
bool AIOWallBehindTarget(MovementObject@ t, const vec3 &in kick_dir) {
    vec3 from = t.position + vec3(0.0f, 0.4f, 0.0f);
    vec3 to = from + kick_dir * 2.5f;
    return col.GetRayCollision(from, to) != to;
}

bool AIODropBehindTarget(MovementObject@ t, const vec3 &in kick_dir) {
    vec3 nav = GetNavPointPos(t.position);
    return nav != vec3(0.0f) && AIODropBeyond(nav, kick_dir, 2.5f);
}

float AIOKnockbackBonus(MovementObject@ t) {
    if(!aio_on || !aio_momentum) {
        return 1.0f;
    }

    vec3 kick_dir = AIOFlat(t.position - this_mo.position);

    if(length_squared(kick_dir) < 0.01f) {
        return 1.0f;
    }

    kick_dir = normalize(kick_dir);
    float bonus = 1.0f;
    string why = "";
    float away_speed = dot(AIOFlat(t.velocity), kick_dir);

    if(away_speed > 2.0f) {
        bonus *= 1.0f + min(away_speed, 8.0f) * 0.08f;  // its momentum adds to the launch
        why += " fleeing";
    }

    if(AIODropBehindTarget(t, kick_dir)) {
        bonus *= 1.6f;  // over the edge
        why += " drop";
    } else if(AIOWallBehindTarget(t, kick_dir)) {
        bonus *= 1.4f;  // into the wall
        why += " wall";
    }

    float hp = max(0.0f, min(1.0f, t.GetFloatVar("temp_health")));

    if(hp < 0.6f) {
        bonus *= 1.0f + (1.0f - hp) * 0.8f;  // hurt bodies fly further
        why += " hurt";
    }

    if(t.GetIntVar("ko_shield") <= 2 && t.GetIntVar("max_ko_shield") > 0) {
        bonus *= 1.3f;  // the kick's 2 shield points finish it
        why += " finisher";
    }

    aio_kb_bonus = bonus;
    aio_kb_why = why;
    aio_kb_time = time;
    return bonus;
}

// Called when a flying kick launches: say why it was worth it
void AIOLogKnockbackKick(const string &in kind) {
    if(aio_momentum && time - aio_kb_time < 1.0f && aio_kb_bonus > 1.3f) {
        AIOLog("knockback_kick kind=" + kind + " bonus=" + aio_kb_bonus + aio_kb_why);
    }
}

// An attack on us is about to land: backing straight away only adds our own
// momentum to the flight, so turn that into a sidestep
vec3 AIONoBackpedal(const vec3 &in move) {
    if(!aio_on || !aio_momentum || !on_ground || state != _movement_state || flip_info.IsRolling() ||
            chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return move;
    }

    MovementObject@ t = ReadCharacterID(chase_target_id);

    if(t.GetIntVar("state") != _attack_state || t.GetIntVar("target_id") != this_mo.GetID() ||
            t.rigged_object().anim_client().GetTimeUntilEvent("attackimpact") < 0.0f) {
        return move;
    }

    vec3 away = AIOFlat(this_mo.position - t.position);
    float dist = length(away);
    vec3 flat = AIOFlat(move);
    float speed = length(flat);

    if(dist > 3.0f || dist < 0.1f || speed < 0.1f || dot(flat / speed, away / dist) < 0.35f) {
        return move;
    }

    if(time >= aio_sidestep_until) {
        aio_sidestep_side = rand() % 2 == 0 ? 1.0f : -1.0f;
        AIOLog("sidestep");
    }

    aio_sidestep_until = time + 0.6f;
    away /= dist;
    return vec3(-away.z, 0.0f, away.x) * aio_sidestep_side * speed;
}


// ---- footwork: no straight lines to time ------------------------------------
// The stock approach is a straight run at constant speed, easy to time a kick
// or a throw against. With "Unpredictable footwork" on, a fighter switches
// between movement patterns, picked by distance, situation and personality:
//   on the way in (4.5-14 m)
//     zigzag      weave side to side (against blades and throwers above all)
//     stutter     stop-go, breaking the timing of anti-air and counters
//     angle       curve in toward the flank instead of down the middle
//     walk_burst  stroll in, then sprint the last metres
//   at fighting range (2-4.5 m)
//     circle      circle the opponent, reversing direction at random
//     dash        a fake entry to draw a swing, then out: punish the whiff
//     open_guard  stand still as if inviting a hit, then answer it
// Any swing at us that misses opens a short punish window. Movement comes
// from GetAttackMovement's plain branch only: special moves, dodges, team
// holds and the like steer on their own.

bool aio_footwork = true;

enum AIOFoot {
    kFootStraight,
    kFootZigzag,
    kFootStutter,
    kFootAngle,
    kFootWalkBurst,
    kFootCircle,
    kFootDash,
    kFootOpenGuard,
    kFootCount
}

const array<string> kFootNames = {"straight", "zigzag", "stutter", "angle", "walk_burst", "circle", "dash", "open_guard"};

int aio_foot = kFootStraight;
float aio_foot_start = 0.0f;
float aio_foot_until = 0.0f;
float aio_foot_side = 1.0f;
float aio_foot_flip_at = 0.0f;
bool aio_foot_far = true;           // the band the pattern was picked for
bool aio_foe_was_swinging = false;
float aio_whiff_until = 0.0f;

float AIOFootStyleScale(int foot) {
    if(!aio_personality || foot == kFootStraight) {
        return (aio_personality && aio_style == kStyleBrawler && foot == kFootStraight) ? 4.0f : 1.0f;
    }

    switch(aio_style) {
        case kStyleBrawler:    return 0.35f;
        case kStyleTrickster:  return (foot == kFootOpenGuard || foot == kFootDash || foot == kFootZigzag) ? 2.2f : 1.5f;
        case kStyleSkirmisher: return (foot == kFootZigzag || foot == kFootAngle || foot == kFootCircle) ? 2.0f : 1.3f;
        case kStyleBoxer:      return (foot == kFootDash || foot == kFootStutter || foot == kFootCircle) ? 2.0f : 0.8f;
        case kStyleKicker:     return (foot == kFootAngle || foot == kFootWalkBurst) ? 1.6f : 1.0f;
    }

    return 1.0f;
}

void AIOPickFootwork(MovementObject@ t, float dist) {
    array<float> w(kFootCount);
    vec3 to_me = AIOFlat(this_mo.position - t.position);
    float d = length(to_me);
    bool facing_me = d > 0.1f && dot(AIOFlat(t.GetFacing()), to_me / d) > 0.5f;
    bool still = length(AIOFlat(t.velocity)) < 1.0f;
    bool blade = aio_match == -1;
    bool thrower = AIOThrowThreat(t) > 0.0f;
    aio_foot_far = dist > 4.5f;

    if(aio_foot_far) {
        w[kFootStraight] = 1.5f;
        w[kFootZigzag] = (thrower || blade) ? 4.0f : 1.0f;
        w[kFootStutter] = (still && facing_me) ? 2.0f : 1.0f;
        w[kFootAngle] = (still && facing_me) ? 2.0f : 1.0f;
        w[kFootWalkBurst] = blade ? 0.0f : ((still && facing_me) ? 1.5f : 0.7f);
    } else {
        w[kFootStraight] = 1.5f;
        w[kFootCircle] = 1.5f;
        w[kFootDash] = blade ? 0.0f : 1.0f;  // no fake entries into a blade
        w[kFootOpenGuard] = blade ? 0.0f : 0.6f;
    }

    float total = 0.0f;

    for(int i = 0; i < kFootCount; ++i) {
        w[i] *= AIOFootStyleScale(i);
        total += w[i];
    }

    float r = RangedRandomFloat(0.0f, total);
    int pick = kFootStraight;

    for(int i = 0; i < kFootCount; ++i) {
        r -= w[i];

        if(r <= 0.0f && w[i] > 0.0f) {
            pick = i;
            break;
        }
    }

    aio_foot = pick;
    aio_foot_start = time;
    aio_foot_side = rand() % 2 == 0 ? 1.0f : -1.0f;
    aio_foot_flip_at = time + RangedRandomFloat(0.35f, 0.7f);

    switch(pick) {
        case kFootDash:      aio_foot_until = time + 0.9f; break;
        case kFootOpenGuard: aio_foot_until = time + RangedRandomFloat(0.6f, 1.0f); break;
        case kFootWalkBurst: aio_foot_until = time + RangedRandomFloat(1.8f, 2.6f); break;
        default:             aio_foot_until = time + RangedRandomFloat(1.5f, 3.0f); break;
    }

    if(pick != kFootStraight) {
        AIOLog("footwork " + kFootNames[pick] + " dist=" + int(dist));
    }
}

// Per tick, while fighting: pick patterns, the punish window, and the
// patterns that work through range and attack timing
void AIOUpdateFootwork(MovementObject@ t, float dist) {
    if(!aio_footwork) {
        aio_foot = kFootStraight;
        return;
    }

    int my_id = this_mo.GetID();
    bool swinging_at_me = t.GetIntVar("state") == _attack_state && t.GetIntVar("target_id") == my_id;

    // A swing at us that missed: its recovery is ours (blades: see matchups)
    if(aio_foe_was_swinging && !swinging_at_me && state == _movement_state && aio_match != -1) {
        aio_whiff_until = time + 0.7f;
        AIOLog("punish_whiff");
    }

    aio_foe_was_swinging = swinging_at_me;

    if(time < aio_whiff_until) {
        target_attack_range = 0.0f;
        ai_attacking = true;
        dont_attack_before = 0.0f;
        aio_foot = kFootStraight;
        return;
    }

    vec3 away = AIOFlat(t.position - this_mo.position);
    bool retreating = length_squared(away) > 0.01f && dot(AIOFlat(t.velocity), normalize(away)) > 2.0f;

    if(swinging_at_me || dist < 1.8f || dist > 14.0f || retreating || time < aio_pursuit_until) {
        aio_foot = kFootStraight;  // defend, fight, just get there, or chase it down
        return;
    }

    if(time >= aio_foot_until || (dist > 4.5f) != aio_foot_far) {
        AIOPickFootwork(t, dist);
    }

    float t_in = time - aio_foot_start;

    if(aio_foot == kFootDash) {
        if(t_in < 0.35f) {
            target_attack_range = 1.2f;  // in, as if to strike...
            ai_attacking = false;
        } else {
            target_attack_range = 4.0f;  // ...and out: a swing now meets air
            ai_attacking = false;
        }
    } else if(aio_foot == kFootOpenGuard) {
        ai_attacking = false;  // invite it; blocks and counters answer
    } else if(aio_foot == kFootWalkBurst && t_in > aio_foot_until - aio_foot_start - 0.8f) {
        dont_attack_before = 0.0f;  // the burst arrives swinging
    }
}

// Shapes the plain attack movement by the current pattern
vec3 AIOFootwork(const vec3 &in move) {
    if(!aio_on || !aio_footwork || aio_foot == kFootStraight || chase_target_id == -1 ||
            !MovementObjectExists(chase_target_id) || !on_ground) {
        return move;
    }

    vec3 flat = AIOFlat(move);
    float speed = length(flat);
    vec3 to_target = AIOFlat(ReadCharacterID(chase_target_id).position - this_mo.position);

    if(length_squared(to_target) < 0.01f) {
        return move;
    }

    vec3 fwd = normalize(to_target);
    vec3 side = vec3(-fwd.z, 0.0f, fwd.x);
    float t_in = time - aio_foot_start;

    if(time >= aio_foot_flip_at && (aio_foot == kFootZigzag || aio_foot == kFootCircle)) {
        aio_foot_side = -aio_foot_side;
        aio_foot_flip_at = time + (aio_foot == kFootZigzag ? RangedRandomFloat(0.35f, 0.7f) : RangedRandomFloat(0.4f, 1.2f));
    }

    switch(aio_foot) {
        case kFootZigzag:
            return normalize(fwd + side * aio_foot_side * 0.9f) * max(speed, 0.8f);
        case kFootStutter:
            // go ~0.7 s, stop ~0.25 s
            return (t_in % 0.95f) < 0.7f ? move : move * 0.05f;
        case kFootAngle:
            return normalize(fwd + side * aio_foot_side * 0.7f) * max(speed, 0.8f);
        case kFootWalkBurst:
            return t_in < aio_foot_until - aio_foot_start - 0.8f ? move * 0.35f : normalize(fwd) * 1.0f;
        case kFootCircle: {
            // keep the stock in/out to hold the range, add the sideways part
            vec3 radial = fwd * dot(flat, fwd);
            return radial + side * aio_foot_side * 0.75f;
        }
        case kFootOpenGuard:
            return move * 0.0f;
    }

    return move;
}

// ---- per tick, while fighting ----------------------------------------------------

bool AIOMindHolding() {
    return time < aio_tempo_pause_until || time < aio_breath_until;
}

bool aio_mind_was_holding = false;

void AIOUpdateMind(MovementObject@ target) {
    AIODecayFoes();
    float dist = xz_distance(this_mo.position, target.position);
    AIOUpdateMatchup(target, dist);
    AIOUpdateFootwork(target, dist);
    AIOUpdateBreath(target, dist);

    if(time >= aio_breath_until && aio_match != -1) {
        AIOUpdateTempo(target, dist);  // no bursts into a blade: openings time those
    }

    if(AIOMindHolding()) {
        aio_mind_was_holding = true;
    } else if(aio_mind_was_holding) {
        aio_mind_was_holding = false;
        instant_range_change = true;  // pick a normal fighting range again
    }
}
