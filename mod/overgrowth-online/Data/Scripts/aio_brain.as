//-----------------------------------------------------------------------------
//           Name: aio_brain.as
//    Description: Overhaul additions to enemycontrol.as: jump kicks,
//                 weapon throwing, feints, group flanking with attack turns,
//                 breaking out of a surround, attack variety.
//                 Included by enemycontrol.as, so it uses the body
//                 (aschar.as) and brain globals directly. Every feature is
//                 off when the character's "AI - Overhaul" box is unticked.
//-----------------------------------------------------------------------------

// ---- shared helpers --------------------------------------------------------

bool aio_log = false;
bool aio_test_aware = false;  // test runs only: NPCs see everyone (config key absent for players)

// ---- settings: Overhaul window (overhaul_menu.as, F9: Overhaul AI tab) x the character's "AI -" params
// The menu writes aio_* config keys; every brain re-reads them twice a second
// (or at once on "aio_settings_changed"). Bug fixes are not switchable.

bool aio_on = true;           // master switch and the character's "AI - Overhaul" box
float aio_jk = 0.5f;          // jump kick drive
float aio_air = 0.5f;         // air combat and kick reading
float aio_throw = 0.5f;       // weapon throwing
float aio_feint = 0.3f;       // feints
bool aio_group = true;        // attack turns, flanking
bool aio_standing = true;     // standing jump kicks up close
bool aio_bait_on = true;      // fake retreats
bool aio_hops = true;         // bunny-hop chasing and fleeing
bool aio_rolls = true;        // dodge, throw-dodge, roll-in, roll-out rolls
bool aio_fast_getup = true;   // quick get-up rolls away from enemies
bool aio_escape_on = true;    // escape jumps, breaking out of surrounds
bool aio_ally_cries = true;   // hearing allies go down
bool aio_terrain = true;      // high ground and fighting position
bool aio_crouch = true;       // duck under high attacks
bool aio_walls = true;        // wall kicks and wall flips
bool aio_ledges = true;       // ledge grabs and climbing
bool aio_team = true;         // team tactics in battles
bool aio_morale_on = true;    // fall back and regroup when morale breaks
bool aio_cohesion = true;     // teammates stick together (part of team tactics)
float aio_settings_next = 0.0f;

bool AIOCfgBool(const string &in key, bool fallback) {
    return ConfigHasKey(key) ? GetConfigValueBool(key) : fallback;
}

float AIOCfgFloat(const string &in key, float fallback) {
    return ConfigHasKey(key) ? max(0.0f, GetConfigValueFloat(key)) : fallback;
}

void AIORefreshSettings() {
    aio_settings_next = time + 0.5f;
    aio_log = AIOCfgBool("aio_debug_log", false);
    aio_test_aware = AIOCfgBool("aio_test_aware", false);
    aio_on = p_aio_enabled && AIOCfgBool("aio_master", true);
    aio_jk = p_aio_jump_kicks * AIOCfgFloat("aio_jump_kicks", 1.0f);
    aio_air = p_aio_air_combat * AIOCfgFloat("aio_air_combat", 1.0f);
    aio_throw = p_aio_throwing * AIOCfgFloat("aio_throwing", 1.0f);
    aio_feint = p_aio_feints * AIOCfgFloat("aio_feints", 1.0f);
    aio_group = p_aio_group_tactics && AIOCfgBool("aio_group_tactics", true);
    aio_standing = AIOCfgBool("aio_standing_kicks", true);
    aio_bait_on = AIOCfgBool("aio_bait", true);
    aio_hops = AIOCfgBool("aio_hop_pursuit", true);
    aio_rolls = AIOCfgBool("aio_rolls", true);
    aio_fast_getup = AIOCfgBool("aio_fast_getup", true);
    aio_escape_on = AIOCfgBool("aio_escape_jumps", true);
    aio_chaser_kick = AIOCfgBool("aio_chaser_kick", true);
    aio_ally_cries = AIOCfgBool("aio_ally_cries", true);
    aio_terrain = AIOCfgBool("aio_terrain", true);
    aio_crouch = AIOCfgBool("aio_crouch", true);
    aio_walls = AIOCfgBool("aio_wall_moves", true);
    aio_ledges = AIOCfgBool("aio_ledges", true);
    aio_team = p_aio_team_tactics && AIOCfgBool("aio_team", true);
    aio_morale_on = AIOCfgBool("aio_morale", true);
    aio_cohesion = aio_team && AIOCfgBool("aio_cohesion", true);
    aio_weapon_sense = AIOCfgBool("aio_weapon_sense", true);
    AIOMindRefreshSettings();
    AIOApplyTraitScales();  // the personality on top of the menu's scales
}

void AIOReadConfig() {
    if(time >= aio_settings_next) {
        AIORefreshSettings();
    }
}

void AIOLog(const string &in msg) {
    if(aio_log) {
        Log(info, "[AIO] " + msg + " id=" + this_mo.GetID());
    }
}

vec3 AIOFlat(vec3 v) {
    v.y = 0.0f;
    return v;
}

vec3 AIORotateY(const vec3 &in v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x * c - v.z * s, 0.0f, v.x * s + v.z * c);
}

// Clear line at chest height (static geometry only)
bool AIOClearLine(const vec3 &in a, const vec3 &in b) {
    vec3 from = a + vec3(0.0f, 0.6f, 0.0f);
    vec3 to = b + vec3(0.0f, 0.6f, 0.0f);
    return col.GetRayCollision(from, to) == to;
}

// Another NPC running this brain, so its aio_* vars can be read
bool AIOIsAIOBrain(MovementObject@ char) {
    return !char.controlled && char.HasVar("aio_attack_slot_target");
}

bool AIOReachable(const vec3 &in pos) {
    NavPath nav_path = GetPath(this_mo.position, pos, inclusive_flags, exclusive_flags);

    if(!nav_path.success || nav_path.NumPoints() == 0) {
        return false;
    }

    vec3 end = nav_path.GetPoint(nav_path.NumPoints() - 1);
    return xz_distance(end, pos) < 1.5f && abs(end.y - pos.y) < 2.0f;
}

// ---- per-tick entry point (called at the end of UpdateBrain) ---------------

int aio_attack_slot_target = -1;  // enemy this brain is committed to striking (read by teammates)
int aio_flanking_target = -1;     // enemy this brain is circling (read by teammates)
int aio_last_leg_cannon_hits = 0;
int aio_engaged_id = -1;          // current fight target, and when it was picked
float aio_engage_time = 0.0f;

void AIOUpdate(const Timestep &in ts) {
    AIOReadConfig();

    if(aio_test_aware && !omniscient && hostile) {
        omniscient = true;
    }

    aio_npc_air_attacks =aio_on && AIOCanJumpKick() && hostile && combat_allowed;

    if(goal != _get_weapon) {
        aio_weapon_claim = -1;  // teammates read it
    }

    if(!aio_on) {
        aio_attack_slot_target = -1;
        aio_flanking_target = -1;
        aio_throw_target_id = -1;
        return;
    }

    if(species == _cat || species == _rat) {
        inclusive_flags = POLYFLAGS_ALL;  // agile species use jump links too (vanilla: rabbits only)
    }

    AIOTrackThrownItem();
    AIOUpdateHopState();

    if(on_ground) {
        aio_last_ground_y = this_mo.position.y;
    }
    aio_jk_lessons = max(0.0f, aio_jk_lessons - 0.033f * ts.step());  // forget a quiet kicker in ~3 min

    if(got_hit_by_leg_cannon_count > aio_last_leg_cannon_hits) {
        AIOLog("jumpkick_hit_taken");
        AIOJumpKickLesson();
    }

    aio_last_leg_cannon_hits = got_hit_by_leg_cannon_count;

    bool fighting = goal == _attack && chase_target_id != -1 && knocked_out == _awake && MovementObjectExists(chase_target_id);
    aio_attack_slot_target = (fighting && (sub_goal == _rush_and_attack || sub_goal == _wait_and_attack || sub_goal == _knock_off_ledge)) ? chase_target_id : -1;

    if(!fighting) {
        aio_flanking_target = -1;
        aio_want_throw = false;
        aio_throw_target_id = -1;

        if(goal == _flee && knocked_out == _awake) {
            if(!AIOTryFleeChaserKick()) {  // running after us? kick it (aio_combat.as)
                AIOUpdateFleeJumps();
                AIOUpdateFleeHops();
            }
            return;
        }

        if(on_ground && aio_jk_active) {
            aio_jk_active = false;
            has_jump_target = false;
        }

        aio_land_until = 0.0f;
        aio_counter_until = 0.0f;
        return;
    }

    if(chase_target_id != aio_engaged_id) {
        aio_engaged_id = chase_target_id;
        aio_engage_time = time;
    }

    MovementObject@ target = ReadCharacterID(chase_target_id);
    AIOUpdateAir(target);
    AIOUpdateThrow(target);
    AIOUpdateFlanking(target);
    AIOUpdatePosition(target);
    AIOUpdateWalls(target);
    AIOUpdateClimb(target);
    AIOUpdateDuckCounter();
    AIOUpdateMind(target);
    AIOUpdateCombat(target);
    AIOUpdateTeam(target);
    AIOUnstick(target);
    AIOCheckSurrounded();
}

// ---- air combat: jump kicks, jump-dodges, anti-air -------------------------

// One airborne action at a time. Kinds: jumpkick, counter (after a dodge),
// evade (hop back out of a hit), evade_over (hop over a low attack and kick
// down), intercept (meet a jumping target in the air)
bool aio_jk_active = false;  // airborne on purpose; read by teammates
string aio_air_kind = "";
bool aio_jk_struck = false;
float aio_jk_launch_time = 0.0f;
vec3 aio_evade_dir;
float aio_jk_next_check = 0.0f;
float aio_jk_cooldown_until = 0.0f;
float aio_jk_setup_until = 0.0f;
float aio_jk_next_setup = 0.0f;
bool aio_jk_was_setting_up = false;

int aio_evade_considered_attacker = -1;
float aio_evade_considered_until = 0.0f;
float aio_evade_cooldown_until = 0.0f;
int aio_counter_target = -1;   // after a jump-dodge: punish this attacker on landing
float aio_counter_until = 0.0f;

int aio_tgt_tracked = -1;
bool aio_tgt_was_on_ground = true;
float aio_antiair_cooldown_until = 0.0f;
vec3 aio_land_spot;
float aio_land_set_time = 0.0f;
float aio_land_until = 0.0f;   // run to aio_land_spot and hit the landing

float aio_jk_lessons = 0.0f;   // jump kicks seen landing on this NPC or nearby allies

bool AIOCanJumpKick() {
    return species == _rabbit || species == _cat || species == _rat;
}

bool AIOCanLeaveGround() {
    return combat_allowed && !startled && state == _movement_state && on_ground && !pre_jump &&
        tethered == _TETHERED_FREE && !flip_info.IsFlipping() && !has_jump_target &&
        !aio_hop_request && !aio_hop_in_air && aio_wall_state == 0 &&
        trying_to_climb == _nothing && path_find_type == _pft_nav_mesh;
}

// One flying kick at a target at a time
bool AIOTeammateJumpKicking(int enemy_id) {
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() != my_id && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                char.GetBoolVar("aio_jk_active") && char.GetIntVar("chase_target_id") == enemy_id) {
            return true;
        }
    }

    return false;
}

// Air attacks while an air action is under way, whatever the sub-goal says
bool AIOWantsAirAttack() {
    return aio_on && (aio_jk_active || (aio_hop_in_air && goal == _attack)) && !on_ground && !startled && combat_allowed;
}

// Steering while airborne: an evading hop keeps going away
bool AIOAirSteer(vec3 &out dir) {
    if(aio_on && aio_jk_active && !on_ground && aio_air_kind == "evade") {
        dir = aio_evade_dir;
        return true;
    }

    return false;
}

// Launch velocity that reaches `to` from `from` in `t` seconds
vec3 AIOBallisticVel(const vec3 &in from, const vec3 &in to, float t) {
    return (to - from) / t - physics.gravity_vector * (0.5f * t);
}

// NPC jumps never beat the player's: a rabbit holding jump peaks near 3.8 m
// (about 8.6 m/s straight up) and runs 8 m/s plus a little air control;
// other species launch at 60% vertical with a fifth of the jump boost.
float AIOMaxJumpVert() {
    return species == _rabbit ? 8.0f : 4.0f;
}

float AIOMaxJumpHorz() {
    return run_speed * 1.2f;
}

vec3 AIOClampJump(vec3 vel) {
    vec3 flat = AIOFlat(vel);
    float horz = length(flat);

    if(horz > AIOMaxJumpHorz()) {
        flat *= AIOMaxJumpHorz() / horz;
    }

    return vec3(flat.x, min(vel.y, AIOMaxJumpVert()), flat.z);
}

bool AIOVelFeasible(const vec3 &in vel) {
    return vel.y > 1.0f && vel.y <= AIOMaxJumpVert() && length(AIOFlat(vel)) <= AIOMaxJumpHorz();
}

// The arc must not hit a wall well short of where it is meant to go
// ...and must come down on walkable ground, never more than 3 m below takeoff
bool AIOArcClear(const vec3 &in vel, float min_travel) {
    JumpTestEq(this_mo.position, vel, jump_info.jump_path);

    if(jump_info.jump_path.size() == 0) {
        return false;
    }

    vec3 land = jump_info.jump_path[jump_info.jump_path.size() - 1];
    jump_info.jump_path.resize(0);
    return xz_distance(land, this_mo.position) >= min_travel && AIOSafeLanding(land);
}

// Stock pathing drops off a ledge after a target below without checking the
// drop: only drop onto walkable ground, well under the height that hurts
// (landing hurts above 30 m/s divided by the fall damage multiplier)
bool AIOSafeDrop(const vec3 &in edge, const vec3 &in bottom) {
    float g = -physics.gravity_vector.y;
    float hurt_speed = 30.0f / fall_damage_mult;
    float safe_height = hurt_speed * hurt_speed / (2.0f * g) * 0.5f;
    float drop = edge.y - bottom.y;

    if(drop < 0.5f) {
        return false;  // the stock check mistook raised ground for a drop
    }

    vec3 nav = GetNavPointPos(bottom);
    bool safe = drop < safe_height && nav != vec3(0.0f) && xz_distance(nav, bottom) < 2.0f && abs(nav.y - bottom.y) < 2.0f;

    if(!safe) {
        AIOLog("drop_refused drop=" + drop);
    }

    return safe;
}

bool AIOSafeLanding(const vec3 &in land) {
    vec3 nav = GetNavPointPos(land);
    return nav != vec3(0.0f) && xz_distance(nav, land) < 1.5f && land.y > this_mo.position.y - 3.0f &&
        !AIOLandingCrowded(land);
}

// Another standing enemy (not the one we go for) next to the landing: it swats
// us out of the air or hits the landing, the usual way jumpers get knocked out
bool aio_crowd_logged = false;

bool AIOLandingCrowded(const vec3 &in land) {
    array<int> around;
    GetCharactersInSphere(land, 2.2f, around);

    for(uint i = 0; i < around.size(); ++i) {
        if(around[i] == this_mo.GetID() || around[i] == chase_target_id) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(around[i]);

        if(!this_mo.OnSameTeam(char) && !char.static_char && char.GetIntVar("knocked_out") == _awake &&
                char.GetIntVar("state") != _ragdoll_state) {
            if(!aio_crowd_logged) {
                aio_crowd_logged = true;
                AIOLog("landing_crowded by=" + around[i]);
            }

            return true;
        }
    }

    aio_crowd_logged = false;
    return false;
}

// `vel` is the velocity the character should leave the ground with
void AIOLaunchAir(const vec3 &in vel, const string &in kind) {
    has_jump_target = true;
    jump_target_vel = vel;

    if(species != _rabbit) {
        jump_target_vel.y /= 0.6f;  // StartJump scales non-rabbit jumps down to 60%
    }

    aio_jk_setup_until = 0.0f;
    aio_jk_active = true;
    aio_air_kind = kind;
    aio_jk_struck = false;
    aio_jk_launch_time = time;
    AIOLog(kind + "_launch");
}

// A deliberate jump is queued but not yet off the ground: a ground attack
// started now would cancel it (the body checks attacks before jumps)
bool AIOTakeoffPending() {
    return aio_on && aio_jk_active && on_ground && has_jump_target;
}

// Flying kick at a grounded target, leading its movement along the navmesh.
// A fast kick uses a flat, quick arc that leaves little time to react.
bool AIOLaunchJumpKickAt(MovementObject@ target, const string &in kind, bool fast = false) {
    if(AIORepeatVeto(kind)) {
        return false;  // just did that one: something else
    }

    if(AIORespectThrower(target, kind)) {
        return false;  // a blade in hand, facing us: in the air we would be a free hit
    }

    if(AIOMatchupVeto(target, kind)) {
        return false;  // armed and unarmed: no kick that only feeds a blade
    }

    AIOLogKnockbackKick(kind);

    const float takeoff = 0.08f;  // pre-jump crouch plus up to a brain tick
    float dist = xz_distance(this_mo.position, target.position);
    float arc = fast ? 0.1f : 0.3f;
    float flight = 0.5f;
    vec3 aim = AIOLeadTarget(target, 0.4f + takeoff);
    vec3 vel = GetVelocityForTarget(this_mo.position, aim, AIOMaxJumpHorz(), AIOMaxJumpVert(), arc, flight);

    if(vel.y == 0.0f) {
        return false;
    }

    aim = AIOLeadTarget(target, min(flight, 0.8f) + takeoff);
    vel = GetVelocityForTarget(this_mo.position, aim, AIOMaxJumpHorz(), AIOMaxJumpVert(), arc, flight);

    if(vel.y == 0.0f || !AIOArcClear(vel, dist * 0.6f)) {
        return false;
    }

    AIOLaunchAir(vel, kind);
    return true;
}

// Where and when an airborne character comes down: its arc simulated against
// the world (platforms, slopes), then checked against the navmesh
vec3 AIOPredictLanding(MovementObject@ target, float &out t_land) {
    JumpTestEq(target.position, target.velocity, jump_info.jump_path);
    uint n = jump_info.jump_path.size();

    if(n == 0) {
        t_land = -1.0f;
        return target.position;
    }

    vec3 land = jump_info.jump_path[n - 1];
    jump_info.jump_path.resize(0);
    t_land = max(0.05f, (float(n) - 1.5f) * time_step * 20.0f);  // JumpTestEq steps 20 frames
    vec3 nav = GetNavPointPos(land);

    if(nav == vec3(0.0f) || xz_distance(nav, land) > 2.0f || abs(nav.y - land.y) > 2.0f) {
        t_land = -1.0f;
    }

    return land;
}

void AIOUpdateAir(MovementObject@ target) {
    // Track the target leaving the ground (every tick, before any early out)
    bool tgt_on_ground = target.GetBoolVar("on_ground");
    bool just_jumped = false;

    if(aio_tgt_tracked != chase_target_id) {
        aio_tgt_tracked = chase_target_id;
    } else {
        just_jumped = aio_tgt_was_on_ground && !tgt_on_ground && target.velocity.y > 1.0f &&
            target.GetIntVar("state") != _ragdoll_state;
    }

    if(aio_tgt_tracked == chase_target_id && !aio_tgt_was_on_ground && tgt_on_ground) {
        aio_tgt_land_time = time;
        aio_tgt_last_apex = aio_tgt_apex_now;  // how high its last jump went
    }

    if(just_jumped) {
        AIONoteTargetJump();
        aio_tgt_takeoff_y = target.position.y;
        aio_tgt_apex_now = 0.0f;
    }

    if(!tgt_on_ground) {
        aio_tgt_apex_now = max(aio_tgt_apex_now, target.position.y - aio_tgt_takeoff_y);
    }

    // Holding jump keeps pushing a jumper up for up to half a second: its arc
    // is not ballistic yet, so predictions would aim too low
    float vy_dt = time - aio_tgt_vy_time;

    if(!tgt_on_ground && vy_dt > 0.001f && vy_dt < 0.2f) {
        float accel = (target.velocity.y - aio_tgt_last_vy) / vy_dt;
        aio_tgt_boosting = target.velocity.y > 0.5f && accel > physics.gravity_vector.y + 3.0f;
    } else {
        aio_tgt_boosting = false;
    }

    aio_tgt_last_vy = target.velocity.y;
    aio_tgt_vy_time = time;
    aio_tgt_was_on_ground = tgt_on_ground;

    if(aio_jk_active) {
        // attack_getter is empty until the first attack is chosen; curr_attack is safe to read
        if(!aio_jk_struck && state == _attack_state && attack_animation_set && curr_attack == "air") {
            aio_jk_struck = true;
            AIOLog(aio_air_kind + "_strike");
        }

        bool landed = on_ground && !has_jump_target && time - aio_jk_launch_time > 0.3f;
        bool stale = has_jump_target && time - aio_jk_launch_time > 0.6f;  // the jump never fired

        if(landed || stale || time - aio_jk_launch_time > 3.0f) {
            if(stale) {
                has_jump_target = false;
                AIOLog(aio_air_kind + "_aborted");
            } else if(!aio_jk_struck && aio_air_kind != "evade" && aio_air_kind != "escape_jump") {
                AIOLog(aio_air_kind + "_miss");
            }

            if(landed && (aio_air_kind == "evade" || (aio_air_kind == "evade_over" && !aio_jk_struck))) {
                aio_counter_until = time + 1.2f;  // punish the attacker we dodged
            }

            aio_jk_active = false;
            aio_air_kind = "";
        }

        return;
    }

    AIOUpdateLandingPunish(target);

    if(AIOTryReadIncomingKick() || AIOTryCloseAntiAir(target, just_jumped) || AIOTryDodgeThrow() || AIOTryDuck() || AIOTryCounter() ||
            AIOTryEvade() || AIOTryAirborneTarget(target) || AIOTryAntiAir(target, just_jumped)) {
        return;
    }

    AIOUpdatePursuit(target);

    if(AIOUpdateBait(target) || AIOTryEscapeJump(target) || AIOTryCloseKick(target) || AIOTryRollIn(target)) {
        return;
    }

    AIOUpdateKickLane(target);
    AIOTryJumpKick(target);
}

// ---- standing jump kicks: the close-range killer ---------------------------
// A jump kick needs no run-up: pop straight up next to someone and the leg
// cannon fires at once. NPCs use it in close fights and read it from others.

float aio_close_kick_next = 0.0f;
float aio_close_kick_pending_until = 0.0f;  // just blocked an attack: punish with a standing kick

// Pop up, drifting in just enough to stay in reach; the air kick fires on its own
bool AIOLaunchCloseKick(MovementObject@ target, const string &in kind) {
    if(!AIOCanJumpKick() || !AIOCanLeaveGround()) {
        return false;
    }

    vec3 to_target = AIOFlat(target.position - this_mo.position);
    float dist = length(to_target);
    vec3 dir = dist > 0.01f ? to_target / dist : this_mo.GetFacing();
    vec3 vel = AIOClampJump(dir * min(3.0f, dist * 1.5f) + vec3(0.0f, _jump_vel * 0.85f, 0.0f));

    if(!AIOArcClear(vel, 0.0f)) {
        return false;
    }

    AIOLaunchAir(vel, kind);
    return true;
}

// HandleAIEvent(_activeblocked)
void AIOOnActiveBlocked() {
    if(aio_on && aio_standing && AIOCanJumpKick() && aio_jk > 0.0f &&
            RangedRandomFloat(0.0f, 1.0f) < aio_jk * mix(0.15f, 0.5f, game_difficulty)) {
        aio_close_kick_pending_until = time + 0.8f;
    }
}

bool AIOTryCloseKick(MovementObject@ target) {
    if(!aio_standing || aio_jk <= 0.0f || !AIOCanJumpKick() || !AIOCanLeaveGround() ||
            target.GetIntVar("knocked_out") != _awake || target.GetIntVar("state") == _ragdoll_state ||
            !target.GetBoolVar("on_ground")) {
        return false;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(dist < 0.8f || dist > 2.8f || abs(target.position.y - this_mo.position.y) > 1.0f ||
            AIOTeammateJumpKicking(chase_target_id)) {
        return false;
    }

    // Right after blocking its attack
    if(time < aio_close_kick_pending_until) {
        aio_close_kick_pending_until = 0.0f;
        aio_close_kick_next = time + RangedRandomFloat(2.0f, 4.0f);
        return AIOLaunchCloseKick(target, "block_kick");
    }

    if(time < aio_close_kick_next) {
        return false;
    }

    aio_close_kick_next = time + RangedRandomFloat(0.4f, 0.7f);  // re-check soon
    bool fast;
    float chance = aio_jk * mix(0.06f, 0.25f, game_difficulty) * AIOKickOpportunity(target, fast) * 2.0f;

    if(RangedRandomFloat(0.0f, 1.0f) > chance || !AIOLaunchCloseKick(target, "close_kick")) {
        return false;
    }

    aio_close_kick_next = time + RangedRandomFloat(mix(6.0f, 2.5f, game_difficulty), mix(10.0f, 5.0f, game_difficulty));
    return true;
}

// The target popped up next to us: a standing kick is coming
bool AIOTryCloseAntiAir(MovementObject@ target, bool just_jumped) {
    if(!just_jumped || aio_air <= 0.0f || xz_distance(this_mo.position, target.position) > 3.2f) {
        return false;
    }

    AIOObserveKick(chase_target_id);
    float chance = mix(0.25f, 0.8f, game_difficulty) * (0.5f + aio_air) * (0.7f + 0.1f * aio_jk_lessons);

    if(RangedRandomFloat(0.0f, 1.0f) > chance) {
        return false;  // misread; the stock block timing may still catch it
    }

    float pick = RangedRandomFloat(0.0f, 1.0f);

    if(pick < 0.3f && aio_standing && !AIOTeammateJumpKicking(chase_target_id) && AIOLaunchCloseKick(target, "close_counter")) {
        return true;  // kick it first
    }

    if(pick < 0.75f && aio_rolls && state == _movement_state && on_ground && !flip_info.IsFlipping() && tethered == _TETHERED_FREE) {
        // Roll back out of reach, then come back in to hit the landing
        vec3 away = AIOFlat(this_mo.position - target.position);
        away = length_squared(away) > 0.01f ? normalize(away) : this_mo.GetFacing() * -1.0f;
        aio_dodge_dir = AIORotateY(away, rand() % 2 == 0 ? 0.4f : -0.4f);
        aio_dodge_until = time + 0.45f;
        wants_to_roll = true;
        ai_attacking = false;

        float t_land;
        AIOPredictLanding(target, t_land);
        aio_land_spot = target.position;
        aio_land_set_time = time;
        aio_land_until = time + max(0.4f, t_land) + 0.8f;
        AIOLog("close_kick_read_roll");
        return true;
    }

    AIOLog("close_kick_read_block");
    return false;  // hold ground: the stock active block handles leg cannons
}

// Right after a dodge: punish the attacker while it recovers
bool AIOTryCounter() {
    if(time >= aio_counter_until || aio_counter_target == -1 || !MovementObjectExists(aio_counter_target) ||
            state != _movement_state || !on_ground || flip_info.IsFlipping()) {
        return false;
    }

    MovementObject@ attacker = ReadCharacterID(aio_counter_target);

    if(attacker.GetIntVar("knocked_out") != _awake || attacker.GetIntVar("state") == _ragdoll_state) {
        aio_counter_until = 0.0f;
        return false;
    }

    float dist = xz_distance(this_mo.position, attacker.position);

    if(dist < (AIOCanJumpKick() ? 2.2f : 3.5f) && combat_allowed) {
        SetSubGoal(_rush_and_attack);
        sub_goal_pick_time = time + 1.0f;
        ai_attacking = true;
        dont_attack_before = 0.0f;
        aio_counter_until = 0.0f;
        AIOLog("counter_rush");
        return true;
    }

    if(dist <= 6.5f && AIOCanJumpKick() && AIOCanLeaveGround() && AIOClearLine(this_mo.position, attacker.position) &&
            AIOLaunchJumpKickAt(attacker, "counter")) {
        aio_counter_until = 0.0f;
        return true;
    }

    return false;
}

// Get out of an attack that is about to land: jump over sweeps, roll or hop
// out of the rest (non-rabbits mostly roll)
bool AIOTryEvade() {
    bool can_jump = AIOCanJumpKick() && AIOCanLeaveGround();
    bool can_roll = AIOCanRoll();

    if(aio_air <= 0.0f || time < aio_evade_cooldown_until || time < aio_duck_until || active_blocking || (!can_jump && !can_roll)) {
        return false;
    }

    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 3.5f, nearby);
    int my_id = this_mo.GetID();

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == my_id) {
            continue;
        }

        MovementObject@ attacker = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(attacker) || attacker.GetIntVar("knocked_out") != _awake ||
                attacker.GetIntVar("state") != _attack_state || attacker.GetIntVar("target_id") != my_id ||
                !attacker.GetBoolVar("on_ground")) {
            continue;
        }

        float t_imp = attacker.rigged_object().anim_client().GetTimeUntilEvent("attackimpact");

        if(t_imp < 0.18f || t_imp > 0.42f) {
            continue;
        }

        // Decide once per attack
        if(aio_evade_considered_attacker == nearby[i] && time < aio_evade_considered_until) {
            continue;
        }

        aio_evade_considered_attacker = nearby[i];
        aio_evade_considered_until = time + t_imp + 0.3f;

        bool low = attacker.GetFloatVar("duck_amount") > 0.8f;  // sweeps and low kicks
        float chance = (0.5f + aio_air) * mix(0.15f, 0.6f, game_difficulty) * AIOTrait(aio_tr_rolls);

        if(low) {
            chance *= 1.6f;  // airborne characters can't be hit low
        }

        if(weapon_slots[primary_weapon_slot] == -1 && AIOHasBlade(attacker)) {
            chance *= 1.5f;  // bare hands against a blade: don't stand there
        }

        if(RangedRandomFloat(0.0f, 1.0f) > chance) {
            continue;
        }

        vec3 to_attacker = AIOFlat(attacker.position - this_mo.position);
        vec3 toward = length_squared(to_attacker) > 0.01f ? normalize(to_attacker) : this_mo.GetFacing();

        if(low && can_jump) {
            // Hop over the sweep; the attacker is in reach, so the leg cannon fires on the way down
            AIOLaunchAir(AIOClampJump(toward * 1.5f + vec3(0.0f, _jump_vel, 0.0f)), "evade_over");
        } else if(can_roll && (!can_jump || RangedRandomFloat(0.0f, 1.0f) < (species == _rabbit ? 0.55f : 0.85f))) {
            if(!AIOStartRoll(AIORotateY(toward * -1.0f, rand() % 2 == 0 ? 0.9f : -0.9f), "dodge_roll")) {
                continue;
            }

            aio_counter_until = time + 1.3f;  // counter once the roll ends
        } else if(can_jump) {
            vec3 dir = AIORotateY(toward * -1.0f, rand() % 2 == 0 ? 0.6f : -0.6f);
            vec3 vel = AIOClampJump(dir * min(run_speed * 1.2f, 6.0f) + vec3(0.0f, _jump_vel * 0.9f, 0.0f));

            if(!AIOArcClear(vel, 1.5f)) {
                continue;
            }

            aio_evade_dir = dir;
            AIOLaunchAir(vel, "evade");
        } else {
            continue;
        }

        aio_counter_target = nearby[i];
        aio_evade_cooldown_until = time + RangedRandomFloat(mix(3.0f, 1.0f, game_difficulty), mix(5.0f, 2.0f, game_difficulty));
        return true;
    }

    return false;
}


// ---- airborne targets: meet them in the air ---------------------------------
// The whole flight is watched, not just the takeoff: every tick the target is
// in the air within reach, look for a fair meeting point; once one exists,
// decide once for this jump. A target that keeps jumping is a hopper: meet
// it far more often (and hop after it, see AIOUpdatePursuit).

array<float> aio_tgt_jump_times;
bool aio_air_decided = false;  // this jump of the target already got a decision

void AIONoteTargetJump() {
    aio_tgt_jump_times.insertLast(time);

    if(aio_tgt_jump_times.length() > 4) {
        aio_tgt_jump_times.removeAt(0);
    }

    aio_air_decided = false;
}

// Two or more jumps in the last 4 seconds
bool AIOTargetHopper() {
    int recent = 0;

    for(uint i = 0; i < aio_tgt_jump_times.length(); ++i) {
        if(time - aio_tgt_jump_times[i] < 4.0f) {
            ++recent;
        }
    }

    return recent >= 2;
}

bool AIOTryAirborneTarget(MovementObject@ target) {
    if(aio_air <= 0.0f || aio_air_decided || time < aio_antiair_cooldown_until || !AIOCanJumpKick() ||
            target.GetBoolVar("on_ground") || target.GetIntVar("state") == _ragdoll_state ||
            target.GetIntVar("knocked_out") != _awake || xz_distance(this_mo.position, target.position) > 9.0f ||
            !AIOCanLeaveGround() || AIOTeammateJumpKicking(chase_target_id)) {
        return false;
    }

    if(aio_tgt_boosting && target.velocity.y > 1.5f) {
        return false;  // still holding jump: wait until its arc is predictable
    }

    vec3 vel;

    if(!AIOBestIntercept(target, 0.2f, 0.7f, vel)) {
        return false;  // nothing reachable yet; look again next tick
    }

    aio_air_decided = true;
    bool hopper = AIOTargetHopper();
    float chance = aio_air * mix(0.25f, 0.75f, game_difficulty) * (1.0f + 0.25f * min(aio_jk_lessons, 4.0f));

    if(hopper) {
        chance = max(chance, mix(0.5f, 0.95f, game_difficulty) * min(1.0f, aio_air * 2.0f));
    }

    if(target.GetIntVar("state") == _attack_state) {
        chance *= 1.3f;  // a flying kick is coming
    }

    if(RangedRandomFloat(0.0f, 1.0f) > chance) {
        return false;
    }

    aio_antiair_cooldown_until = time + (hopper ? RangedRandomFloat(0.6f, 1.0f) : RangedRandomFloat(1.5f, 3.0f));
    AIOLaunchAir(vel, hopper ? "hopper_intercept" : "air_intercept");
    return true;
}

// The target just jumped: meet it in the air, or be where it lands
bool AIOTryAntiAir(MovementObject@ target, bool just_jumped) {
    if(!just_jumped || aio_air <= 0.0f || time < aio_antiair_cooldown_until) {
        return false;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(dist > 9.0f || dist <= 3.2f) {
        return false;  // close jumps are standing kicks: AIOTryCloseAntiAir
    }

    vec3 to_me = AIOFlat(this_mo.position - target.position);

    if(dist > 4.0f && dot(AIOFlat(target.velocity), to_me / dist) < -3.0f) {
        return false;  // hopping away from us: the chase handles it
    }

    float chance = aio_air * mix(0.15f, 0.6f, game_difficulty) * (1.0f + 0.25f * min(aio_jk_lessons, 4.0f));

    if(target.GetIntVar("state") == _attack_state) {
        chance *= 1.3f;  // a flying kick is coming
    }

    if(RangedRandomFloat(0.0f, 1.0f) > chance) {
        return false;
    }

    aio_antiair_cooldown_until = time + RangedRandomFloat(2.0f, 4.0f);

    if(AIOCanJumpKick() && AIOCanLeaveGround() && dist < 6.5f && !AIOTeammateJumpKicking(chase_target_id)) {
        vec3 vel;

        if(AIOBestIntercept(target, 0.25f, 0.65f, vel)) {
            AIOLaunchAir(vel, "intercept");
            return true;
        }

        AIOLog("intercept_infeasible");
    } else {
        AIOLog("intercept_skipped can_jump=" + AIOCanJumpKick() + " can_leave=" + AIOCanLeaveGround() + " dist=" + dist + " state=" + state);
    }

    float t_land;
    vec3 land = AIOPredictLanding(target, t_land);

    if(t_land > 0.1f) {
        vec3 offset = AIOFlat(this_mo.position - land);
        aio_land_spot = land + (length_squared(offset) > 0.01f ? normalize(offset) : vec3(0.0f)) * 1.0f;
        aio_land_set_time = time;
        aio_land_until = time + t_land + 0.6f;
        AIOLog("antiair_landing t=" + t_land);
        return true;
    }

    return false;
}

// Waiting at the landing spot: strike as the target touches down
void AIOUpdateLandingPunish(MovementObject@ target) {
    if(time >= aio_land_until) {
        return;
    }

    if(target.GetBoolVar("on_ground") && time - aio_land_set_time > 0.15f) {
        aio_land_until = 0.0f;

        if(xz_distance(this_mo.position, target.position) < 3.0f && combat_allowed) {
            SetSubGoal(_rush_and_attack);
            sub_goal_pick_time = time + 1.0f;
            ai_attacking = true;
            dont_attack_before = 0.0f;
            AIOLog("landing_punish");
        }
    }
}

bool AIOLandingMovement(vec3 &out move) {
    if(aio_on && time < aio_land_until) {
        move = GetMovementToPoint(aio_land_spot, 0.3f);
        return true;
    }

    return false;
}

// Jump kicks are shared knowledge: every hit teaches nearby allies to read them
void AIOJumpKickLesson() {
    aio_jk_lessons = min(6.0f, aio_jk_lessons + 1.0f);
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() != my_id && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                distance_squared(char.position, this_mo.position) < 15.0f * 15.0f) {
            char.ReceiveScriptMessage("aio_jk_lesson");
        }
    }
}

// Offensive flying kicks, with a step back to make room when too close
void AIOTryJumpKick(MovementObject@ target) {
    bool setting_up = time < aio_jk_setup_until;

    if(setting_up) {
        target_attack_range = 4.6f;
        ai_attacking = false;
        aio_jk_was_setting_up = true;
    } else if(aio_jk_was_setting_up) {
        aio_jk_was_setting_up = false;
        instant_range_change = true;  // pick a normal fighting range again
    }

    if(time < aio_jk_next_check) {
        return;
    }

    aio_jk_next_check = time + RangedRandomFloat(0.3f, 0.6f);

    if(time < aio_jk_cooldown_until || aio_jk <= 0.0f || !AIOCanJumpKick() || !AIOCanLeaveGround() || jump_delay > 0.0f) {
        return;
    }

    if(target.GetIntVar("knocked_out") != _awake || target.GetIntVar("state") == _ragdoll_state || !target.GetBoolVar("on_ground")) {
        return;
    }

    vec3 to_target = target.position - this_mo.position;
    float dy = to_target.y;
    float dist = length(AIOFlat(to_target));

    // Level ground, or a drop kick from high ground onto a target up to 4 m below
    bool drop_kick = dy < -1.2f;

    if(dy > 1.2f || dy < -4.0f || dist > 7.5f) {
        return;
    }

    if(dist < 2.8f && !drop_kick) {
        // Too close to jump: now and then step back to make room for one
        if(!setting_up && time > aio_jk_next_setup && sub_goal != _surround_target && sub_goal != _escape_surround &&
                RangedRandomFloat(0.0f, 1.0f) < aio_jk * mix(0.08f, 0.3f, game_difficulty)) {
            aio_jk_setup_until = time + RangedRandomFloat(1.2f, 2.2f);
            aio_jk_next_setup = aio_jk_setup_until + RangedRandomFloat(3.0f, 6.0f);
            AIOLog("jumpkick_setup dist=" + dist);
        }

        return;
    }

    bool fast;
    float chance = aio_jk * mix(0.25f, 0.8f, game_difficulty) * AIOKickOpportunity(target, fast) * 2.0f;

    if(setting_up) {
        chance *= 4.0f;  // backed off on purpose
    }

    if(drop_kick) {
        chance *= 1.8f;  // high ground: the kick comes down on them
    }

    if(sub_goal == _defend || sub_goal == _surround_target || sub_goal == _escape_surround) {
        chance *= 0.4f;
    }

    if(RangedRandomFloat(0.0f, 1.0f) > chance || !AIOClearLine(this_mo.position, target.position) ||
            AIOTeammateJumpKicking(chase_target_id)) {
        return;
    }

    if(AIOLaunchJumpKickAt(target, drop_kick ? "drop_kick" : (fast ? "fast_kick" : "jumpkick"), fast)) {
        aio_jk_cooldown_until = time + RangedRandomFloat(mix(9.0f, 4.0f, game_difficulty), mix(15.0f, 8.0f, game_difficulty));
    }
}

// ---- kick craft: respecting and exploiting the jump kick -------------------
// The leg cannon is the deadliest move in the game. NPCs keep out of an
// enemy's kick lane, read incoming kicks, remember who kicks, kick into
// openings, bait chases, and jump away when they are losing.

float aio_tgt_land_time = -10.0f;  // when the chase target last touched down
float aio_tgt_takeoff_y = 0.0f;    // the target's jump profile
float aio_tgt_apex_now = 0.0f;     // height reached in its current jump
float aio_tgt_last_apex = 0.0f;    // how high its last jump went
float aio_tgt_last_vy = 0.0f;
float aio_tgt_vy_time = 0.0f;
bool aio_tgt_boosting = false;     // holding jump: still accelerating upward

int aio_seen_kick_id = -1;         // last airborne enemy counted as a kick attempt
float aio_seen_kick_time = -10.0f;
int aio_kick_read_id = -1;         // last incoming kick a response was decided for
float aio_kick_read_time = -10.0f;

vec3 aio_dodge_dir;
float aio_dodge_until = 0.0f;      // rolling out of a kick's line

vec3 aio_lane_dir;
float aio_lane_until = 0.0f;       // moving out of an enemy's kick lane
float aio_lane_next_check = 0.0f;

float aio_bait_until = 0.0f;       // fake retreat; turn and kick if chased
float aio_bait_spring_until = 0.0f;
float aio_bait_next = 0.0f;

float aio_escape_jump_next = 0.0f;

bool AIOCanSpeciesJumpKick(MovementObject@ char) {
    int s = char.GetIntVar("species");
    return s == _rabbit || s == _cat || s == _rat;
}

// An airborne enemy coming at us counts as a kick attempt; the group learns
void AIOObserveKick(int enemy_id) {
    if(enemy_id == aio_seen_kick_id && time - aio_seen_kick_time < 1.5f) {
        return;
    }

    aio_seen_kick_id = enemy_id;
    aio_seen_kick_time = time;
    aio_jk_lessons = min(6.0f, aio_jk_lessons + 0.5f);
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() != my_id && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                distance_squared(char.position, this_mo.position) < 15.0f * 15.0f) {
            char.ReceiveScriptMessage("aio_jk_seen");
        }
    }

    AIOLog("kick_seen lessons=" + aio_jk_lessons);
}

// How exposed we are to `enemy` launching a flying kick at us right now
float AIOKickThreat(MovementObject@ enemy) {
    int enemy_state = enemy.GetIntVar("state");

    if(enemy.GetIntVar("knocked_out") != _awake || enemy_state != _movement_state ||
            !enemy.GetBoolVar("on_ground") || !AIOCanSpeciesJumpKick(enemy)) {
        return 0.0f;
    }

    vec3 rel = AIOFlat(this_mo.position - enemy.position);
    float dist = length(rel);

    if(dist < 2.5f || dist > 7.5f) {
        return 0.0f;
    }

    vec3 enemy_front = AIOFlat(enemy.GetFacing());

    if(length_squared(enemy_front) < 0.01f) {
        return 0.0f;
    }

    float facing = dot(normalize(enemy_front), rel / dist);

    if(facing < 0.5f || !AIOClearLine(this_mo.position, enemy.position)) {
        return 0.0f;
    }

    return facing * (0.5f + 0.15f * min(aio_jk_lessons, 6.0f));
}

// How good a moment it is to kick `t` (0.25 = neutral: it can still react)
float AIOKickOpportunity(MovementObject@ t, bool &out fast) {
    fast = false;
    float score = 0.25f;
    int t_state = t.GetIntVar("state");
    vec3 to_me = normalize(AIOFlat(this_mo.position - t.position));

    if(t_state == _attack_state) {
        if(t.rigged_object().anim_client().GetTimeUntilEvent("attackimpact") < 0.0f) {
            score = 1.0f;  // recovering from a swing
            fast = true;
        } else if(t.GetIntVar("target_id") != this_mo.GetID()) {
            score = 0.9f;  // swinging at someone else
        } else {
            score = 0.4f;
        }
    } else if(t_state == _hit_reaction_state) {
        score = 0.9f;  // staggered
        fast = true;
    }

    if(time - aio_tgt_land_time < 0.4f) {
        score = max(score, 0.8f);  // just landed
        fast = true;
    }

    vec3 t_front = AIOFlat(t.GetFacing());

    if(length_squared(t_front) > 0.01f && dot(normalize(t_front), to_me) < -0.2f) {
        score = max(score, 0.85f);  // back turned
    }

    if(dot(t.velocity, to_me) > 3.0f) {
        score = max(score, 0.7f);  // running onto the kick
        fast = true;
    }

    if(t.GetBoolVar("active_blocking")) {
        score *= 0.3f;
    }

    return score * AIOKnockbackBonus(t);  // how hard it lands: fleeing, wall or drop behind, hurt, finisher
}

// An enemy is flying at us: roll out of its line or meet it in the air
bool AIOTryReadIncomingKick() {
    if(time < aio_dodge_until) {
        ai_attacking = false;
        return true;
    }

    if(state != _movement_state || !on_ground || tethered != _TETHERED_FREE || flip_info.IsFlipping()) {
        return false;
    }

    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 9.0f, nearby);
    int my_id = this_mo.GetID();

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == my_id) {
            continue;
        }

        MovementObject@ enemy = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(enemy) || enemy.GetIntVar("knocked_out") != _awake ||
                enemy.GetIntVar("state") == _ragdoll_state || enemy.GetBoolVar("on_ground")) {
            continue;
        }

        // Closest pass of its flight to us, gravity included
        vec3 rel = AIOFlat(this_mo.position - enemy.position);
        vec3 flat_vel = AIOFlat(enemy.velocity);
        float tca;

        if(dot(flat_vel, flat_vel) < 4.0f || AIOClosestPass(enemy, tca) > 1.8f || tca < 0.1f || tca > 0.7f) {
            continue;
        }

        AIOObserveKick(nearby[i]);

        if(nearby[i] == aio_kick_read_id && time - aio_kick_read_time < 1.5f) {
            continue;  // already decided for this jump
        }

        aio_kick_read_id = nearby[i];
        aio_kick_read_time = time;
        float chance = mix(0.2f, 0.75f, game_difficulty) * (0.5f + aio_air) * (0.7f + 0.1f * aio_jk_lessons);

        if(RangedRandomFloat(0.0f, 1.0f) > chance) {
            continue;  // missed the read; vanilla avoidance may still react
        }

        if(tca > 0.3f && AIOCanJumpKick() && AIOCanLeaveGround() && RangedRandomFloat(0.0f, 1.0f) < 0.35f) {
            vec3 vel;

            if(AIOBestIntercept(enemy, 0.2f, max(0.2f, tca * 0.8f), vel)) {
                AIOLaunchAir(vel, "kick_read_intercept");
                return true;
            }
        }

        // Roll out of its line, to the side we are already on, then punish the landing
        if(!aio_rolls) {
            continue;
        }

        vec3 perp = normalize(vec3(-flat_vel.z, 0.0f, flat_vel.x));

        if(dot(perp, rel) < 0.0f) {
            perp *= -1.0f;
        }

        aio_dodge_dir = perp;
        aio_dodge_until = time + 0.45f;
        wants_to_roll = true;
        ai_attacking = false;

        if(nearby[i] == chase_target_id) {
            float t_land;
            vec3 land = AIOPredictLanding(enemy, t_land);

            if(t_land > 0.1f) {
                aio_land_spot = land;
                aio_land_set_time = time;
                aio_land_until = time + t_land + 0.8f;
            }
        }

        AIOLog("kick_read_roll tca=" + tca);
        return true;
    }

    return false;
}

// Standing in an enemy's kick lane: sidestep, crowd it, or back out
void AIOUpdateKickLane(MovementObject@ target) {
    if(time < aio_lane_until) {
        ai_attacking = false;
        return;
    }

    if(time < aio_lane_next_check || aio_air <= 0.0f || state != _movement_state) {
        return;
    }

    aio_lane_next_check = time + RangedRandomFloat(0.4f, 0.7f);

    if(sub_goal != _wait_and_attack && sub_goal != _defend && sub_goal != _provoke_attack && sub_goal != _surround_target) {
        return;
    }

    float threat = AIOKickThreat(target);

    if(threat < 0.6f || RangedRandomFloat(0.0f, 1.0f) > aio_air * mix(0.15f, 0.6f, game_difficulty) * threat) {
        return;
    }

    vec3 from_enemy = normalize(AIOFlat(this_mo.position - target.position));
    float pick = RangedRandomFloat(0.0f, 1.0f);

    if(pick < 0.55f) {
        // Sidestep toward the side with more walkable room
        vec3 side = vec3(-from_enemy.z, 0.0f, from_enemy.x);
        vec3 nav = GetNavPointPos(this_mo.position);

        if(nav != vec3(0.0f)) {
            float room_a = distance(nav, NavRaycast(nav, nav + side * 3.0f));
            float room_b = distance(nav, NavRaycast(nav, nav - side * 3.0f));

            if(room_b > room_a) {
                side *= -1.0f;
            }
        } else if(aio_rolls && rand() % 2 == 0) {
            side *= -1.0f;
        }

        if(RangedRandomFloat(0.0f, 1.0f) < 0.4f && AIOStartRoll(side, "kick_lane_roll")) {
            return;
        }

        aio_lane_dir = side;
        aio_lane_until = time + RangedRandomFloat(0.6f, 1.1f);
        ai_attacking = false;
        AIOLog("kick_lane_strafe threat=" + threat);
    } else if(pick < 0.85f && combat_allowed) {
        SetSubGoal(_rush_and_attack);  // inside 2 m a flying kick has no room
        sub_goal_pick_time = time + RangedRandomFloat(0.8f, 1.4f);
        ai_attacking = true;
        AIOLog("kick_lane_close threat=" + threat);
    } else {
        aio_lane_dir = from_enemy;
        aio_lane_until = time + RangedRandomFloat(0.6f, 1.0f);
        ai_attacking = false;
        AIOLog("kick_lane_back threat=" + threat);
    }
}

// Fake retreat: back off, and if the target gives chase, turn and kick it
bool AIOUpdateBait(MovementObject@ target) {
    if(!aio_bait_on && time >= aio_bait_spring_until) {
        return false;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(time < aio_bait_spring_until) {
        vec3 to_me = normalize(AIOFlat(this_mo.position - target.position));
        float closing = dot(target.velocity, to_me);

        if(closing < -1.5f) {
            // It runs instead of following: a fake retreat only grows the gap
            aio_bait_until = 0.0f;
            aio_bait_spring_until = 0.0f;
            AIOLog("bait_cancel");
            return false;
        }

        if(dist > 2.8f && dist < 6.5f && closing > 2.0f && AIOCanLeaveGround() && target.GetBoolVar("on_ground") &&
                AIOClearLine(this_mo.position, target.position) && !AIOTeammateJumpKicking(chase_target_id) &&
                AIOLaunchJumpKickAt(target, "bait_kick", true)) {
            aio_bait_until = 0.0f;
            aio_bait_spring_until = 0.0f;
            aio_jk_cooldown_until = time + RangedRandomFloat(4.0f, 7.0f);
            return true;
        }

        if(time < aio_bait_until) {
            ai_attacking = false;
            return true;
        }

        return false;
    }

    if(time < aio_bait_next || aio_jk <= 0.0f || !AIOCanJumpKick() || !combat_allowed ||
            sub_goal == _surround_target || sub_goal == _escape_surround || state != _movement_state ||
            dist < 1.5f || dist > 4.5f || AIOChasingRetreat(target)) {
        return false;
    }

    aio_bait_next = time + 1.0f;  // consider it about once a second

    if(RangedRandomFloat(0.0f, 1.0f) > aio_jk * mix(0.03f, 0.18f, game_difficulty) * AIOTrait(aio_tr_bait)) {
        return false;
    }

    aio_bait_until = time + RangedRandomFloat(1.0f, 1.8f);
    aio_bait_spring_until = aio_bait_until + 0.6f;
    aio_bait_next = aio_bait_spring_until + RangedRandomFloat(8.0f, 15.0f);
    ai_attacking = false;
    AIOLog("bait_start dist=" + dist);
    return true;
}

vec3 AIORetreatMovement() {
    MovementObject@ target = ReadCharacterID(chase_target_id);
    vec3 away = AIOFlat(this_mo.position - target.position);
    away = length_squared(away) > 0.01f ? normalize(away) : this_mo.GetFacing() * -1.0f;
    vec3 goal_pos = this_mo.position + away * 4.0f;
    vec3 from = GetNavPointPos(this_mo.position);

    if(from != vec3(0.0f)) {
        vec3 slid = NavRaycastSlide(from, from + away * 4.0f, 2);

        if(slid != vec3(0.0f)) {
            goal_pos = slid;
        }
    }

    return GetMovementToPoint(goal_pos, 0.0f);
}

// A big jump away from `threat` to a safe landing spot (rabbits only: the
// other species can't jump high)
bool AIOLaunchEscapeJump(const vec3 &in threat, const string &in reason) {
    if(!aio_escape_on || species != _rabbit || !AIOCanLeaveGround()) {
        return false;
    }

    vec3 away = AIOFlat(this_mo.position - threat);
    away = length_squared(away) > 0.01f ? normalize(away) : this_mo.GetFacing() * -1.0f;
    array<float> angles = {0.0f, 0.7f, -0.7f, 1.4f, -1.4f};

    for(uint i = 0; i < angles.size(); ++i) {
        vec3 dir = AIORotateY(away, angles[i]);
        vec3 vel = AIOClampJump(dir * AIOMaxJumpHorz() * 0.85f + vec3(0.0f, AIOMaxJumpVert() * 0.85f, 0.0f));
        JumpTestEq(this_mo.position, vel, jump_info.jump_path);

        if(jump_info.jump_path.size() == 0) {
            continue;
        }

        vec3 land = jump_info.jump_path[jump_info.jump_path.size() - 1];
        jump_info.jump_path.resize(0);
        vec3 nav = GetNavPointPos(land);

        if(xz_distance(land, this_mo.position) < 4.0f || land.y < this_mo.position.y - 3.0f ||
                nav == vec3(0.0f) || distance(nav, land) > 1.5f) {
            continue;  // blocked, a hurting drop, or nowhere to stand
        }

        AIOLaunchAir(vel, "escape_jump");
        AIOLog("escape_jump reason=" + reason);
        aio_escape_jump_next = time + RangedRandomFloat(4.0f, 8.0f);
        return true;
    }

    return false;
}

// Hurt and pressed: leap or roll away instead of trading blows
bool AIOTryEscapeJump(MovementObject@ target) {
    if(!aio_escape_on || time < aio_escape_jump_next || temp_health >= 0.35f || species == _wolf ||
            target.GetIntVar("knocked_out") != _awake || xz_distance(this_mo.position, target.position) > 3.0f) {
        return false;
    }

    vec3 to_me = AIOFlat(this_mo.position - target.position);
    to_me = length_squared(to_me) > 0.01f ? normalize(to_me) : this_mo.GetFacing() * -1.0f;

    if(target.GetIntVar("state") != _attack_state && dot(target.velocity, to_me) < 1.0f) {
        return false;  // not pressing us
    }

    aio_escape_jump_next = time + 3.0f;

    float escape_chance = mix(0.2f, 0.7f, game_difficulty) * (0.5f + aio_air) * ((aio_team && aio_team_state <= -1) ? 1.5f : 1.0f);

    if(RangedRandomFloat(0.0f, 1.0f) > escape_chance) {
        return false;
    }

    if(species == _rabbit && RangedRandomFloat(0.0f, 1.0f) < 0.6f && AIOLaunchEscapeJump(target.position, "hurt")) {
        return true;
    }

    if(AIOStartRoll(AIORotateY(to_me, rand() % 2 == 0 ? 0.6f : -0.6f), "roll_out")) {
        aio_escape_jump_next = time + RangedRandomFloat(3.0f, 6.0f);
        return true;
    }

    return false;
}

// Fleeing (fear or low health): hop away from the pursuer to break contact
void AIOUpdateFleeJumps() {
    if(aio_jk_active) {
        bool landed = on_ground && !has_jump_target && time - aio_jk_launch_time > 0.3f;
        bool stale = has_jump_target && time - aio_jk_launch_time > 0.6f;

        if(landed || stale || time - aio_jk_launch_time > 3.0f) {
            if(stale) {
                has_jump_target = false;
            }

            aio_jk_active = false;
            aio_air_kind = "";
        }

        return;
    }

    if(time < aio_escape_jump_next || chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return;
    }

    MovementObject@ pursuer = ReadCharacterID(chase_target_id);

    if(xz_distance(this_mo.position, pursuer.position) < 4.0f &&
            AIOLaunchEscapeJump(pursuer.position, "flee")) {
        aio_escape_jump_next = time + RangedRandomFloat(2.5f, 4.0f);
    }
}

// All movement overrides for the attack goal, most urgent first
bool AIOMovementOverride(vec3 &out move) {
    if(!aio_on) {
        return false;
    }

    if(state == _ragdoll_state && AIORecoverDir(move)) {
        return true;  // the get-up roll (and ragdoll drift) goes away from the nearest enemy
    }

    if(path_find_type != _pft_nav_mesh || trying_to_climb != _nothing) {
        return false;  // the stock climb / drop routine steers
    }

    if(AIOFallBackMovement(move)) {
        return true;
    }

    if(AIOCohesionMovement(move)) {
        return true;
    }

    if(AIOWallMovement(move)) {
        return true;
    }

    if(AIOAirSteer(move)) {
        return true;
    }

    if(time < aio_dodge_until) {
        move = aio_dodge_dir;
        return true;
    }

    if(AIOLandingMovement(move)) {
        return true;
    }

    if(time < aio_bait_until && chase_target_id != -1 && MovementObjectExists(chase_target_id)) {
        move = AIORetreatMovement();
        return true;
    }

    if(time < aio_lane_until) {
        move = GetMovementToPoint(this_mo.position + aio_lane_dir * 3.0f, 0.0f);
        return true;
    }

    if(AIOTerrainMovement(move)) {
        return true;
    }

    return false;
}

// ---- pursuit: bunny-hop chasing --------------------------------------------
// A jump launches at run speed and air control keeps adding speed with no
// drag, so chained hops outrun running. NPCs hop exactly like a player does
// (the body's normal jump, air control and jump hold): they hop after a
// fleeing target, kick when a hop brings them in reach, and hop away when
// they are the ones fleeing.

bool aio_hop_request = false;    // ask the body for a normal jump
float aio_hop_request_time = 0.0f;
bool aio_hop_boost = false;      // hold jump while rising: a longer hop
float aio_hop_boost_height = 0.0f;  // ...until the hop will peak this high above takeoff
bool aio_hop_in_air = false;     // airborne from one of our hops
bool aio_hop_struck = false;
float aio_hop_takeoff = 0.0f;
vec3 aio_hop_takeoff_pos;

float aio_pursuit_until = 0.0f;  // hop-chasing until then (refreshed while the chase lasts)
float aio_pursuit_next_check = 0.0f;
bool aio_pursuit_hops = false;   // this chase uses hops (rolled per chase, by difficulty)
float aio_chase_kick_next = 0.0f;

bool AIOWantsHop() {
    return aio_on && aio_hop_request;
}

// Hold jump only until the hop will peak at the wanted height (released
// jumps keep rising for v^2 / 2g)
bool AIOWantsHopBoost() {
    if(!aio_on || !aio_hop_in_air || !aio_hop_boost || this_mo.velocity.y <= 0.0f) {
        return false;
    }

    float g = -physics.gravity_vector.y;
    float rise_left = this_mo.velocity.y * this_mo.velocity.y / (2.0f * g);
    return (this_mo.position.y - aio_hop_takeoff_pos.y) + rise_left < aio_hop_boost_height;
}

// HandleAIEvent(_jumped)
void AIOOnJumped() {
    if(aio_hop_request) {
        aio_hop_request = false;
        aio_hop_in_air = true;
        aio_hop_struck = false;
        aio_hop_takeoff = time;
        aio_hop_takeoff_pos = this_mo.position;
    }
}

void AIORequestHop(bool boost) {
    aio_hop_request = true;
    aio_hop_request_time = time;
    aio_hop_boost = boost;
}

// Hop bookkeeping, whatever the goal
void AIOUpdateHopState() {
    if(aio_hop_request && time - aio_hop_request_time > 0.5f) {
        aio_hop_request = false;  // the body never took it (attacking, hit, ...)
    }

    if(aio_hop_in_air) {
        if(!aio_hop_struck && state == _attack_state && attack_animation_set && curr_attack == "air") {
            aio_hop_struck = true;
            AIOLog("hop_kick_strike");
        }

        if(on_ground && time - aio_hop_takeoff > 0.2f) {
            aio_hop_in_air = false;
        }
    }
}

// Direction the brain is currently moving (flat), zero if standing
vec3 AIOMoveDir() {
    vec3 v = AIOFlat(GetTargetVelocity());
    return length_squared(v) > 0.01f ? normalize(v) : vec3(0.0f);
}

// A normal hop in `dir` lands on the navmesh, without a hurting drop or a wall in the face
// Simulates the hop as it will really fly: air control adds speed on the way
// and a held jump flies higher and longer
bool AIOHopSafe(const vec3 &in dir, bool boost) {
    float vy = species == _rabbit ? _jump_vel : _jump_vel * 0.6f;

    if(boost) {
        vy = AIOMaxJumpVert() * 0.95f;
    }

    JumpTestEq(this_mo.position, dir * (run_speed + 2.0f) + vec3(0.0f, vy, 0.0f), jump_info.jump_path);

    if(jump_info.jump_path.size() == 0) {
        return false;
    }

    vec3 land = jump_info.jump_path[jump_info.jump_path.size() - 1];
    jump_info.jump_path.resize(0);

    if(xz_distance(land, this_mo.position) < 3.0f || !AIOSafeLanding(land)) {
        return false;
    }

    // Leave room to stop: no hop that lands near a drop-off or wall in its path
    vec3 nav_land = GetNavPointPos(land);
    return distance(nav_land, NavRaycast(nav_land, nav_land + dir * 3.0f)) > 2.9f;
}

// Mid-hop: if the landing we are heading for is off walkable ground, steer
// back toward the nearest safe ground (or where we took off)
bool AIOHopEdgeSteer(vec3 &out dir) {
    if(!aio_on || !aio_hop_in_air || on_ground) {
        return false;
    }

    float g = -physics.gravity_vector.y;
    float vy = this_mo.velocity.y;
    float above = this_mo.position.y - aio_hop_takeoff_pos.y;
    float t_land = (vy + sqrt(max(0.0f, vy * vy + 2.0f * g * above))) / g;
    vec3 land = this_mo.position + AIOFlat(this_mo.velocity) * t_land;
    land.y = aio_hop_takeoff_pos.y;
    vec3 nav = GetNavPointPos(land);

    if(nav != vec3(0.0f) && xz_distance(nav, land) < 1.0f && nav.y > land.y - 2.5f) {
        return false;  // coming down on safe ground
    }

    vec3 back = (nav != vec3(0.0f) && nav.y > land.y - 2.5f) ? AIOFlat(nav - this_mo.position) : AIOFlat(aio_hop_takeoff_pos - this_mo.position);

    if(length_squared(back) < 0.01f) {
        return false;
    }

    dir = normalize(back);
    return true;
}

bool AIOReadyToHop() {
    return on_ground && state == _movement_state && !pre_jump && !has_jump_target && !aio_jk_active &&
        !aio_hop_request && !aio_hop_in_air && !flip_info.IsFlipping() && tethered == _TETHERED_FREE && !startled;
}

// Chasing a target that is getting away: no move may turn us away from it
// (a fake retreat, a walk to high ground, a pause, running back to the group)
bool AIOChasingRetreat(MovementObject@ target) {
    if(time < aio_pursuit_until) {
        return true;
    }

    vec3 away = AIOFlat(target.position - this_mo.position);
    return length_squared(away) > 0.01f && dot(AIOFlat(target.velocity), normalize(away)) > 1.5f;
}

float aio_retreat_since = -1.0f;   // the target has kept moving away since then
float aio_pursuit_reroll = 0.0f;   // a chase on foot reconsiders hopping then

// Hop the chase? Likelier the faster and longer the target runs; every style
// hops a clear retreat now and then; never at a blade-holder facing us
float AIOPursuitHopChance(MovementObject@ target, float flee_speed, bool target_hopping) {
    float p = mix(0.5f, 1.0f, game_difficulty) * max(0.6f, AIOTrait(aio_tr_hops));

    if(flee_speed > 4.0f || target_hopping) {
        p *= 1.4f;
    }

    if(aio_retreat_since >= 0.0f && time - aio_retreat_since > 1.5f) {
        p *= 1.3f;
    }

    return min(1.0f, p * (1.0f - 0.8f * AIOThrowThreat(target)));
}

// Chasing a target that runs or hops away
void AIOUpdatePursuit(MovementObject@ target) {
    if(!AIOCanJumpKick()) {
        return;
    }

    vec3 to_target = AIOFlat(target.position - this_mo.position);
    float dist = length(to_target);
    vec3 away = dist > 0.01f ? to_target / dist : vec3(0.0f);
    float flee_speed = dot(AIOFlat(target.velocity), away);  // how fast it moves away from us
    bool target_hopping = !target.GetBoolVar("on_ground") && flee_speed > 3.0f;

    // A clear retreat: still moving away after half a second
    if(flee_speed > 2.0f) {
        if(aio_retreat_since < 0.0f) {
            aio_retreat_since = time;
        }
    } else {
        aio_retreat_since = -1.0f;
    }

    bool clear_retreat = aio_retreat_since >= 0.0f && time - aio_retreat_since > 0.5f;

    if(time > aio_pursuit_next_check) {
        aio_pursuit_next_check = time + 0.3f;

        bool chasing = time < aio_pursuit_until;
        bool hopper = AIOTargetHopper() && dist > 2.0f && dist < 9.0f;
        bool starts = (dist > 5.0f && (flee_speed > 4.0f || target_hopping || dist > 9.0f)) || hopper ||
            (clear_retreat && dist > 3.0f);
        bool keeps_running = chasing && dist > 1.2f && (flee_speed > 2.5f || target_hopping || hopper || clear_retreat);

        if(starts || keeps_running) {
            if(!chasing) {
                aio_pursuit_hops = RangedRandomFloat(0.0f, 1.0f) < AIOPursuitHopChance(target, flee_speed, target_hopping);
                aio_pursuit_reroll = time + 1.0f;
                AIOLog("chase_start dist=" + dist + " flee_speed=" + flee_speed + " hops=" + aio_pursuit_hops);
            } else if(!aio_pursuit_hops && time > aio_pursuit_reroll) {
                // Still running after it on foot: reconsider hopping
                aio_pursuit_reroll = time + 1.0f;
                aio_pursuit_hops = RangedRandomFloat(0.0f, 1.0f) < AIOPursuitHopChance(target, flee_speed, target_hopping);

                if(aio_pursuit_hops) {
                    AIOLog("chase_hops dist=" + dist + " flee_speed=" + flee_speed);
                }
            }

            aio_pursuit_until = time + 1.5f;
        }
    }

    if(time > aio_pursuit_until || dist < 1.2f) {
        return;
    }

    // Close behind a fleeing back: flying kick at where it will be. A jump is
    // barely faster than a run, so only from close, and only if it can close
    // the gap before landing (a fair arc, high enough to swing before touchdown)
    float closing_room = AIOMaxJumpHorz() - flee_speed;

    if(!aio_pursuit_hops && dist > 2.5f && dist < 5.0f && closing_room > 1.5f && time > aio_chase_kick_next && aio_jk > 0.0f &&
            target.GetBoolVar("on_ground") && AIOCanLeaveGround() && !aio_hop_in_air &&
            AIOClearLine(this_mo.position, target.position) && !AIOTeammateJumpKicking(chase_target_id)) {
        aio_chase_kick_next = time + RangedRandomFloat(2.0f, 3.5f);

        if(RangedRandomFloat(0.0f, 1.0f) < aio_jk * mix(0.5f, 1.0f, game_difficulty) &&
                AIOLaunchJumpKickAt(target, "chase_kick", false)) {
            return;
        }
    }

    // Keep hopping all the way in: air control homes the last hop onto the
    // target and the air kick fires as soon as it is in reach
    if(!aio_hops || !aio_pursuit_hops || dist < 1.5f || !AIOReadyToHop()) {
        return;
    }

    vec3 dir = AIOMoveDir();

    if(dir == vec3(0.0f) || dot(dir, away) < 0.5f || !AIOHopSafe(dir, dist > 10.0f)) {
        return;  // standing, the path bends away, or the hop would land badly
    }

    // Long hops when far; against a hopper, jump as high as it does
    float hop_height = 0.0f;

    if(dist > 10.0f) {
        hop_height = 3.8f;
    } else if(AIOTargetHopper() && aio_tgt_last_apex > 1.6f) {
        hop_height = min(3.8f, aio_tgt_last_apex);
    }

    AIORequestHop(hop_height > 0.0f);
    aio_hop_boost_height = hop_height;
    AIOLog((dist < 5.0f ? "hop_attack" : "hop_chase") + " dist=" + dist + " height=" + hop_height);
}

// Fleeing: hop away along the escape route; leap if the pursuer is close
void AIOUpdateFleeHops() {
    if(!aio_hops || chase_target_id == -1 || !MovementObjectExists(chase_target_id) || !AIOCanJumpKick() || !AIOReadyToHop()) {
        return;
    }

    float dist = xz_distance(this_mo.position, ReadCharacterID(chase_target_id).position);

    if(dist > 14.0f) {
        return;
    }

    vec3 dir = AIOMoveDir();

    if(dir != vec3(0.0f) && AIOHopSafe(dir, dist < 6.0f)) {
        AIORequestHop(dist < 6.0f);
        AIOLog("hop_flee dist=" + dist);
    }
}

// ---- prediction helpers ----------------------------------------------------

// Where a character will be in `t` seconds: a flier follows its arc until it
// lands, a runner slides along the navmesh instead of through walls
vec3 AIOLeadTarget(MovementObject@ target, float t) {
    if(!target.GetBoolVar("on_ground")) {
        float t_land;
        vec3 land = AIOPredictLanding(target, t_land);

        if(t_land >= 0.0f && t >= t_land) {
            return land;
        }

        return target.position + target.velocity * t + physics.gravity_vector * (0.5f * t * t);
    }

    vec3 flat_vel = AIOFlat(target.velocity);
    vec3 nav = GetNavPointPos(target.position);

    if(nav == vec3(0.0f) || length_squared(flat_vel) < 0.01f) {
        return target.position + flat_vel * t;
    }

    vec3 slid = NavRaycastSlide(nav, nav + flat_vel * t, 2);

    if(slid == vec3(0.0f)) {
        return target.position + flat_vel * t;
    }

    return vec3(slid.x, target.position.y + (slid.y - nav.y), slid.z);
}

// Closest 3D pass of a flying character to us over the next 0.8 s (gravity included)
float AIOClosestPass(MovementObject@ flier, float &out t_best) {
    float best = 1.0e6f;
    t_best = -1.0f;

    for(float t = 0.05f; t <= 0.8f; t += 0.05f) {
        vec3 p = flier.position + flier.velocity * t + physics.gravity_vector * (0.5f * t * t);
        float d = distance(p, this_mo.position);

        if(d < best) {
            best = d;
            t_best = t;
        }
    }

    return best;
}

// Cheapest fair jump that meets a flying target in the air (meeting times t_min..t_max)
bool AIOBestIntercept(MovementObject@ target, float t_min, float t_max, vec3 &out best_vel) {
    const float takeoff = 0.08f;  // pre-jump crouch plus up to a brain tick
    float t_land;
    AIOPredictLanding(target, t_land);
    float best = 1.0e6f;
    bool found = false;

    for(float t = t_min; t <= t_max + 0.001f; t += 0.05f) {
        float lead = t + takeoff;

        if(t_land >= 0.0f && lead > t_land) {
            break;  // it is down before we get there
        }

        vec3 meet = target.position + target.velocity * lead + physics.gravity_vector * (0.5f * lead * lead);
        vec3 vel = AIOBallisticVel(this_mo.position, meet, t);

        if(AIOVelFeasible(vel) && length(vel) < best) {
            best = length(vel);
            best_vel = vel;
            found = true;
        }
    }

    return found && AIOArcClear(best_vel, 0.0f);
}

// ---- rolls -------------------------------------------------------------------
// A roll covers 4-5 m in half a second and keeps the body low, so high
// attacks miss. NPCs roll to get up fast, out of attacks and thrown blades,
// to close a gap, and out of a beating.

int aio_throw_dodge_id = -1;
float aio_throw_dodge_time = -10.0f;
float aio_roll_in_next = 0.0f;
float aio_recover_side = 1.0f;  // which way to angle the get-up roll (picked per knockdown)

bool AIOCanRoll() {
    return aio_rolls && species != _wolf && state == _movement_state && on_ground && !flip_info.IsFlipping() &&
        tethered == _TETHERED_FREE && !startled && !pre_jump && !has_jump_target && !aio_hop_request;
}

// 4 m of walkable ground in that direction (no rolling off ledges or into walls)
bool AIORollRoom(const vec3 &in dir) {
    vec3 nav = GetNavPointPos(this_mo.position);

    if(nav == vec3(0.0f)) {
        return false;
    }

    return distance(nav, NavRaycast(nav, nav + dir * 4.0f)) > 3.5f;
}

bool AIOStartRoll(vec3 dir, const string &in kind) {
    if(AIORollIntoBlade(kind)) {
        return false;  // rolling into a blade
    }

    if(aio_personality && aio_tr_rolls < 1.0f && RangedRandomFloat(0.0f, 1.0f) > aio_tr_rolls) {
        return false;  // not a roller
    }

    if(!AIOCanRoll()) {
        return false;
    }

    if(!AIORollRoom(dir)) {
        vec3 alt_a = AIORotateY(dir, 1.2f);
        vec3 alt_b = AIORotateY(dir, -1.2f);

        if(AIORollRoom(alt_a)) {
            dir = alt_a;
        } else if(AIORollRoom(alt_b)) {
            dir = alt_b;
        } else {
            return false;
        }
    }

    aio_dodge_dir = dir;
    aio_dodge_until = time + 0.45f;
    wants_to_roll = true;
    ai_attacking = false;
    AIOLog(kind);
    return true;
}

bool AIOHasBlade(MovementObject@ char) {
    int weap = GetCharPrimaryWeapon(char);
    return weap != -1 && ReadItemID(weap).GetLabel() != "staff";
}

int AIONearestEnemy(float range) {
    array<int> nearby;
    GetCharactersInSphere(this_mo.position, range, nearby);
    int best = -1;
    float best_dist = range * range;

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == this_mo.GetID()) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(char) || char.static_char || char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        float d = distance_squared(char.position, this_mo.position);

        if(d < best_dist) {
            best_dist = d;
            best = nearby[i];
        }
    }

    return best;
}

// HandleAIEvent(_ragdolled): get up much faster than stock (0.1-2 s)
void AIOOnRagdolled() {
    if(aio_on && aio_fast_getup) {
        roll_after_ragdoll_delay = RangedRandomFloat(mix(0.3f, 0.05f, game_difficulty), mix(1.2f, 0.35f, game_difficulty));
        aio_air_recover_delay = mix(0.45f, 0.2f, game_difficulty) + RangedRandomFloat(0.0f, 0.15f);
        aio_recover_side = rand() % 2 == 0 ? 1.0f : -1.0f;
        aio_rollup_logged = false;
    }
}

bool aio_rollup_logged = false;
float aio_air_recover_delay = 0.3f;

bool AIORollUpNow(const string &in why) {
    if(!aio_rollup_logged) {
        aio_rollup_logged = true;
        AIOLog("rollup " + why + " after=" + ragdoll_time);
    }

    return true;
}

// Roll up out of ragdoll: after the delay, or at once if someone is about to
// hit us on the ground; also when not attacking but an enemy is near
bool AIOWantsRollUp() {
    if(!aio_on || !aio_fast_getup || !combat_allowed || startled) {
        return false;
    }

    int enemy_id = AIONearestEnemy(6.0f);

    if(enemy_id != -1 && ragdoll_time > 0.05f) {
        MovementObject@ enemy = ReadCharacterID(enemy_id);

        if(distance(enemy.position, this_mo.position) < 2.5f && enemy.GetIntVar("state") == _attack_state &&
                enemy.GetIntVar("target_id") == this_mo.GetID()) {
            return AIORollUpNow("ground_punish");
        }
    }

    if(ragdoll_time > roll_after_ragdoll_delay && (goal == _attack || enemy_id != -1)) {
        return AIORollUpNow("ready");
    }

    // Knocked out of the air: flip upright and land on the feet, as players do.
    // A ragdoll that hits the ground loses KO protection for its air time and
    // takes the impact (no ground under us = CanRoll false = the body flips)
    if(ragdoll_time > aio_air_recover_delay && !CanRoll() && this_mo.rigged_object().GetAvgVelocity().y < 2.0f) {
        return AIORollUpNow("air_recover");
    }

    return false;
}

// Get-up roll direction: away from the nearest enemy, at an angle, where there is room
bool AIORecoverDir(vec3 &out dir) {
    if(!aio_fast_getup) {
        return false;
    }

    int enemy_id = AIONearestEnemy(8.0f);

    if(enemy_id == -1) {
        return false;
    }

    vec3 away = AIOFlat(this_mo.position - ReadCharacterID(enemy_id).position);

    if(length_squared(away) < 0.01f) {
        return false;
    }

    away = normalize(away);
    vec3 d = AIORotateY(away, 0.8f * aio_recover_side);

    if(!AIORollRoom(d)) {
        d = AIORotateY(away, -0.8f * aio_recover_side);

        if(!AIORollRoom(d)) {
            d = away;

            if(!AIORollRoom(d)) {
                return false;
            }
        }
    }

    dir = d;
    return true;
}

// Someone is winding up a throw at us: roll off the line
bool AIOTryDodgeThrow() {
    if(!AIOCanRoll()) {
        return false;
    }

    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 16.0f, nearby);
    int my_id = this_mo.GetID();

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == my_id) {
            continue;
        }

        MovementObject@ enemy = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(enemy) || enemy.GetIntVar("knocked_out") != _awake || enemy.GetIntVar("target_id") != my_id) {
            continue;
        }

        bool throwing = enemy.GetIntVar("throw_knife_layer_id") != -1 || enemy.GetBoolVar("going_to_throw_item") ||
            (enemy.GetIntVar("state") == _attack_state && enemy.GetBoolVar("throw_anim"));

        if(!throwing || (nearby[i] == aio_throw_dodge_id && time - aio_throw_dodge_time < 1.5f)) {
            continue;
        }

        aio_throw_dodge_id = nearby[i];
        aio_throw_dodge_time = time;
        float chance = mix(0.25f, 0.85f, game_difficulty) * (0.5f + aio_air);

        if(species == _cat) {
            chance *= 0.3f;  // cats catch thrown blades
        }

        if(RangedRandomFloat(0.0f, 1.0f) > chance) {
            continue;
        }

        vec3 line = AIOFlat(this_mo.position - enemy.position);

        if(length_squared(line) < 0.01f) {
            continue;
        }

        line = normalize(line);
        vec3 side = vec3(-line.z, 0.0f, line.x);

        if(rand() % 2 == 0) {
            side *= -1.0f;
        }

        if(AIOStartRoll(side, "dodge_throw_roll")) {
            return true;
        }
    }

    return false;
}

// Close a 3-5.5 m gap with a roll (under high strikes), then attack
bool AIOTryRollIn(MovementObject@ target) {
    if(time < aio_roll_in_next || !combat_allowed || !AIOCanRoll()) {
        return false;
    }

    aio_roll_in_next = time + 0.5f;

    if(sub_goal != _rush_and_attack && sub_goal != _wait_and_attack) {
        return false;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(dist < 3.0f || dist > 5.5f || target.GetIntVar("state") == _attack_state || !target.GetBoolVar("on_ground") ||
            RangedRandomFloat(0.0f, 1.0f) > mix(0.08f, 0.3f, game_difficulty)) {
        return false;
    }

    vec3 toward = normalize(AIOFlat(target.position - this_mo.position));

    if(!AIOStartRoll(AIORotateY(toward, RangedRandomFloat(-0.3f, 0.3f)), "roll_in")) {
        return false;
    }

    aio_roll_in_next = time + RangedRandomFloat(3.0f, 6.0f);
    SetSubGoal(_rush_and_attack);
    sub_goal_pick_time = time + 1.2f;
    dont_attack_before = 0.0f;
    return true;
}

// ---- terrain: high ground and the best place to fight -----------------------
// Every couple of seconds a fighting NPC scores walkable spots around its
// target: height above it (it has to jump up into our anti-air; we can drop
// kick it), a clear 3-6.5 m kick lane, no drop-off behind us, a drop-off
// behind the target, room from teammates, and the walk to get there. It moves
// to a clearly better spot and holds it for a few seconds.

vec3 aio_post_pos;
float aio_post_until = 0.0f;   // walking to the chosen spot
float aio_hold_until = 0.0f;   // holding it
float aio_post_next = 0.0f;

// Walkable ground ends within `reach` in `dir` and what lies past it is a
// drop of 2 m or more (a wall is not a drop)
bool AIODropBeyond(const vec3 &in nav_from, const vec3 &in dir, float reach) {
    vec3 edge = NavRaycast(nav_from, nav_from + dir * reach);

    if(edge == vec3(0.0f) || distance(edge, nav_from) > reach - 0.2f) {
        return false;  // ground all the way
    }

    vec3 beyond = edge + dir * 0.8f;
    vec3 bottom = beyond - vec3(0.0f, 8.0f, 0.0f);
    vec3 hit = col.GetRayCollision(beyond + vec3(0.0f, 1.0f, 0.0f), bottom);
    return hit == bottom || hit.y < nav_from.y - 2.0f;
}

float AIOSpotScore(const vec3 &in spot, const vec3 &in target_ground, MovementObject@ target) {
    float dy = spot.y - target_ground.y;
    float dist = xz_distance(spot, target_ground);
    float score = max(-2.0f, min(3.0f, dy));  // above the target is better, below is worse

    if(dy > 3.5f) {
        score -= (dy - 3.5f) * 1.5f;  // too high to drop-kick from
    }

    if(dist >= 3.0f && dist <= 6.5f && AIOClearLine(spot + vec3(0.0f, 0.45f, 0.0f), target.position)) {
        score += 1.5f;  // a clear kick lane
    } else if(dist < 3.0f) {
        score += 0.5f;
    } else {
        score -= (dist - 6.5f) * 0.3f;
    }

    vec3 away = AIOFlat(spot - target_ground);

    if(length_squared(away) > 0.01f) {
        away = normalize(away);

        if(AIODropBeyond(spot, away, 2.5f)) {
            score -= 2.0f;  // one kick and we go over the edge
        }

        if(AIODropBeyond(target_ground, away * -1.0f, 2.0f)) {
            score += 1.0f;  // one kick and they go over the edge
        } else if(aio_momentum) {
            vec3 from = target_ground + vec3(0.0f, 1.0f, 0.0f);
            vec3 to = from - away * 2.5f;

            if(col.GetRayCollision(from, to) != to) {
                score += 0.6f;  // one kick and they hit the wall
            }
        }

        if(aio_team && aio_team_state <= -1 && AIOWallBehindSpot(spot, away)) {
            score += 1.0f;  // outnumbered: nobody gets behind us here
        }
    }

    // Leave the spot to one teammate
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() != my_id && this_mo.OnSameTeam(char) && char.GetIntVar("knocked_out") == _awake &&
                xz_distance(char.position, spot) < 2.0f) {
            score -= 1.0f;
        }
    }

    return score;
}

void AIOUpdatePosition(MovementObject@ target) {
    if(!aio_terrain || time < aio_post_next) {
        return;
    }

    aio_post_next = time + RangedRandomFloat(1.5f, 2.5f);

    if(state != _movement_state || !on_ground || aio_jk_active || aio_hop_in_air || time < aio_bait_until ||
            time < aio_post_until || sub_goal == _surround_target || sub_goal == _escape_surround) {
        return;
    }

    float dist = xz_distance(this_mo.position, target.position);

    if(dist < 3.0f || dist > 14.0f) {
        return;
    }

    // Never walk off while it is pressing us: that is a free hit in the back
    vec3 to_me = normalize(AIOFlat(this_mo.position - target.position));

    if(target.GetIntVar("state") == _attack_state || dot(target.velocity, to_me) > 2.0f || AIOChasingRetreat(target)) {
        return;
    }

    vec3 target_ground = GetNavPointPos(target.position);
    vec3 my_ground = GetNavPointPos(this_mo.position);

    if(target_ground == vec3(0.0f) || my_ground == vec3(0.0f)) {
        return;
    }

    float here = AIOSpotScore(my_ground, target_ground, target);
    float best = here + 1.0f;  // only move for a clearly better spot
    float best_walk = 0.0f;
    vec3 best_spot;
    bool found = false;

    for(int ring = 0; ring < 2; ++ring) {
        float radius = ring == 0 ? 3.5f : 5.5f;

        for(int k = 0; k < 12; ++k) {
            float angle = k * 0.5236f + ring * 0.26f;
            vec3 around = target_ground + vec3(cos(angle) * radius, 0.0f, sin(angle) * radius);

            // Sample at ground height and 3 m up, to find raised ground too
            for(int level = 0; level < 2; ++level) {
                vec3 spot = GetNavPointPos(around + vec3(0.0f, level * 3.0f, 0.0f));

                if(spot == vec3(0.0f) || xz_distance(spot, around) > 1.5f) {
                    continue;
                }

                float score = AIOSpotScore(spot, target_ground, target);

                if(score <= best) {
                    continue;
                }

                NavPath nav_path = GetPath(this_mo.position, spot, inclusive_flags, exclusive_flags);

                if(!nav_path.success || nav_path.NumPoints() == 0) {
                    continue;
                }

                float walk = 0.0f;

                for(int p = 0, n = nav_path.NumPoints() - 1; p < n; ++p) {
                    walk += distance(nav_path.GetPoint(p), nav_path.GetPoint(p + 1));
                }

                score -= walk * 0.15f;

                if(walk < 10.0f && score > best) {
                    best = score;
                    best_spot = spot;
                    best_walk = walk;
                    found = true;
                }
            }
        }
    }

    if(!found) {
        return;
    }

    aio_post_pos = best_spot;
    aio_post_until = time + min(5.0f, best_walk / 6.0f + 1.5f);
    aio_hold_until = 0.0f;
    AIOLog("terrain_move dy=" + (best_spot.y - target_ground.y) + " gain=" + (best - here) + " walk=" + best_walk);
}

// Walk to the chosen spot; hold it only if it is real high ground, briefly,
// side-stepping on the spot, and only while holding is still worth it
float aio_hold_side = 1.0f;
float aio_hold_switch = 0.0f;

bool AIOHoldWorthIt(MovementObject@ target) {
    float dist = xz_distance(this_mo.position, target.position);
    return dist >= 3.0f && dist <= 7.0f && target.GetIntVar("state") != _attack_state &&
        target.GetBoolVar("on_ground") && this_mo.position.y - target.position.y >= 0.6f;
}

bool AIOTerrainMovement(vec3 &out move) {
    if(!aio_terrain || chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return false;
    }

    MovementObject@ target = ReadCharacterID(chase_target_id);

    if(time < aio_post_until) {
        if(xz_distance(this_mo.position, aio_post_pos) > 0.8f) {
            move = GetMovementToPoint(aio_post_pos, 0.3f);
            return true;
        }

        aio_post_until = 0.0f;
        aio_post_next = time + RangedRandomFloat(5.0f, 8.0f);  // no back-to-back repositioning
        vec3 target_ground = GetNavPointPos(target.position);

        if(target_ground != vec3(0.0f) && aio_post_pos.y - target_ground.y >= 0.8f) {
            aio_hold_until = time + RangedRandomFloat(1.2f, 2.5f);
            aio_hold_side = rand() % 2 == 0 ? 1.0f : -1.0f;
            aio_hold_switch = time + 0.7f;
            AIOLog("terrain_hold");
        }
    }

    if(time < aio_hold_until) {
        if(!AIOHoldWorthIt(target)) {
            aio_hold_until = 0.0f;
            AIOLog("terrain_hold_end");
            return false;
        }

        // Side-step on the spot (staying on walkable ground) instead of freezing
        if(time > aio_hold_switch) {
            aio_hold_side *= -1.0f;
            aio_hold_switch = time + RangedRandomFloat(0.5f, 0.9f);
        }

        vec3 to_target = normalize(AIOFlat(target.position - this_mo.position));
        vec3 side = vec3(-to_target.z, 0.0f, to_target.x) * aio_hold_side;
        vec3 post_nav = GetNavPointPos(aio_post_pos);
        vec3 step_to = post_nav != vec3(0.0f) ? NavRaycast(post_nav, post_nav + side * 0.8f) : aio_post_pos;
        move = GetMovementToPoint(step_to != vec3(0.0f) ? step_to : aio_post_pos, 0.3f) * 0.5f;
        return true;
    }

    return false;
}

// ---- stuck watchdog ----------------------------------------------------------
// A fighting NPC that stands nearly still for 1.5 s with its target more than
// 3.2 m away, and is not doing something on purpose, drops every movement
// override and goes in (and logs which ones were active)

float aio_still_since = -1.0f;

void AIOUnstick(MovementObject@ target) {
    bool deliberate = time < aio_hold_until || time < aio_duck_until || time < aio_jk_setup_until ||
        time < aio_throw_setup_until || time < aio_bait_until || time < aio_land_until || time < aio_lane_until ||
        time < aio_wait_allies_until || time < aio_hitrun_until || time < aio_fallback_until ||
        time < aio_regroup_until || time < aio_advance_until || AIOMindHolding() ||
        state != _movement_state || !on_ground || startled || !combat_allowed || flip_info.IsFlipping() ||
        aio_jk_active || aio_hop_in_air || tethered != _TETHERED_FREE;
    float dist = xz_distance(this_mo.position, target.position);

    if(deliberate || dist < 3.2f || length_squared(AIOFlat(this_mo.velocity)) > 0.25f ||
            target.GetIntVar("knocked_out") != _awake) {
        aio_still_since = -1.0f;
        return;
    }

    if(aio_still_since < 0.0f) {
        aio_still_since = time;
        return;
    }

    if(time - aio_still_since < 1.5f) {
        return;
    }

    AIOLog("unstick dist=" + dist + " sub_goal=" + int(sub_goal) + " post=" + (time < aio_post_until) +
        " escape=" + (time < aio_escape_until) + " wall=" + aio_wall_state + " pft=" + int(path_find_type) +
        " range=" + target_attack_range + " attacking=" + ai_attacking);
    aio_post_until = 0.0f;
    aio_hold_until = 0.0f;
    aio_lane_until = 0.0f;
    aio_bait_until = 0.0f;
    aio_bait_spring_until = 0.0f;
    aio_jk_setup_until = 0.0f;
    aio_throw_setup_until = 0.0f;
    aio_land_until = 0.0f;
    aio_escape_until = 0.0f;
    aio_dodge_until = 0.0f;
    aio_wall_state = 0;
    SetSubGoal(_rush_and_attack);
    sub_goal_pick_time = time + 1.5f;
    ai_attacking = true;
    aio_still_since = -1.0f;
}

// ---- parkour: crouching, walls, ledges, climbing --------------------------
// The game's own movement systems, driven by the brain: duck under high
// attacks and sweep back, kick or flip off a wall when pinned against it,
// catch ledges when falling, and climb after a target on unreachable high
// ground (the stock run-up / wall-run / grab / climb routine).

// -- crouching under high attacks --
array<string> aio_high_anims;     // animations of stock high attacks
bool aio_high_anims_built = false;
float aio_duck_until = 0.0f;
float aio_duck_next = 0.0f;
int aio_duck_attacker = -1;
float aio_duck_considered_until = 0.0f;
bool aio_force_low = false;       // next ground attack is a sweep

void AIOBuildHighAttackTable() {
    aio_high_anims_built = true;
    // Full literal paths: the level loader preloads path-like string literals
    // in character scripts, and a bare folder prefix is a fatal error there
    array<string> files = {"Data/Attacks/haymaker.xml", "Data/Attacks/jab.xml", "Data/Attacks/kickup.xml",
                           "Data/Attacks/roundhouse.xml", "Data/Attacks/spinkick.xml", "Data/Attacks/spinkick_wolf.xml",
                           "Data/Attacks/thrustkick.xml", "Data/Attacks/scabbardslashleft.xml"};
    AttackScriptGetter getter;

    for(uint i = 0; i < files.length(); ++i) {
        getter.Load(files[i]);

        if(getter.GetHeight() == _high) {
            aio_high_anims.insertLast(getter.GetUnblockedAnimPath());
            aio_high_anims.insertLast(getter.GetBlockedAnimPath());
        }
    }

    if(aio_high_anims.length() > 0) {
        AIOLog("duck_table n=" + aio_high_anims.length() + " first=" + aio_high_anims[0]);
    }
}


bool AIOIsHighAttack(MovementObject@ attacker) {
    if(!aio_high_anims_built) {
        AIOBuildHighAttackTable();
    }

    return aio_high_anims.find(attacker.rigged_object().anim_client().GetCurrAnim()) >= 0;
}

// WantsToCrouch()
bool AIOWantsDuck() {
    return aio_on && time < aio_duck_until;
}

// ChooseAttack(): sweep after ducking under a high attack
bool AIOConsumeForceLow() {
    if(aio_force_low) {
        aio_force_low = false;
        return true;
    }

    return false;
}

bool AIOTryDuck() {
    if(!aio_crouch || time < aio_duck_next || state != _movement_state || !on_ground || tethered != _TETHERED_FREE) {
        return false;
    }

    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 3.5f, nearby);
    int my_id = this_mo.GetID();

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == my_id) {
            continue;
        }

        MovementObject@ attacker = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(attacker) || attacker.GetIntVar("knocked_out") != _awake ||
                attacker.GetIntVar("state") != _attack_state || attacker.GetIntVar("target_id") != my_id) {
            continue;
        }

        float t_imp = attacker.rigged_object().anim_client().GetTimeUntilEvent("attackimpact");

        if(t_imp < 0.1f || t_imp > 0.4f || (nearby[i] == aio_duck_attacker && time < aio_duck_considered_until) ||
                !AIOIsHighAttack(attacker)) {
            continue;
        }

        aio_duck_attacker = nearby[i];
        aio_duck_considered_until = time + t_imp + 0.3f;

        if(RangedRandomFloat(0.0f, 1.0f) > mix(0.2f, 0.7f, game_difficulty)) {
            continue;
        }

        aio_duck_until = time + t_imp + 0.25f;
        aio_duck_next = time + RangedRandomFloat(mix(3.0f, 1.0f, game_difficulty), mix(5.0f, 2.0f, game_difficulty));
        ai_attacking = false;
        AIOLog("duck_high t=" + t_imp);
        return true;
    }

    return false;
}

// Coming up from the duck: sweep the attacker while it recovers
void AIOUpdateDuckCounter() {
    if(aio_duck_attacker == -1 || time < aio_duck_until || time > aio_duck_until + 0.5f) {
        return;
    }

    if(MovementObjectExists(aio_duck_attacker) && combat_allowed &&
            xz_distance(this_mo.position, ReadCharacterID(aio_duck_attacker).position) < 2.5f) {
        aio_force_low = true;
        SetSubGoal(_rush_and_attack);
        sub_goal_pick_time = time + 1.0f;
        ai_attacking = true;
        dont_attack_before = 0.0f;
        AIOLog("duck_counter");
    }

    aio_duck_attacker = -1;
}

// -- walls: kick or flip off a wall when pinned against it --
int aio_wall_state = 0;           // 0 none, 1 hopping onto the wall, 2 on it, 3 pushed off
float aio_wall_state_time = 0.0f;
vec3 aio_wall_dir;                // toward the wall
vec3 aio_wall_side;               // where a flip curves to
bool aio_wall_flip = false;       // flip away (escape) rather than kick off
float aio_wall_next = 0.0f;

void AIOSetWallState(int new_state) {
    aio_wall_state = new_state;
    aio_wall_state_time = time;
}

// A wall tall enough to run on, within 2.2 m behind us (away from the enemy)
bool AIOFindWallBehind(MovementObject@ target, vec3 &out dir_to_wall) {
    vec3 away = AIOFlat(this_mo.position - target.position);

    if(length_squared(away) < 0.01f) {
        return false;
    }

    away = normalize(away);
    array<float> angles = {0.0f, 0.5f, -0.5f};

    for(uint i = 0; i < angles.size(); ++i) {
        vec3 dir = AIORotateY(away, angles[i]);
        vec3 low = this_mo.position + vec3(0.0f, 0.3f, 0.0f);
        vec3 high = this_mo.position + vec3(0.0f, 1.5f, 0.0f);

        if(col.GetRayCollision(low, low + dir * 2.2f) != low + dir * 2.2f &&
                col.GetRayCollision(high, high + dir * 2.6f) != high + dir * 2.6f) {
            dir_to_wall = dir;
            return true;
        }
    }

    return false;
}

void AIOUpdateWalls(MovementObject@ target) {
    if(aio_wall_state != 0) {
        float in_state = time - aio_wall_state_time;

        if(aio_wall_state == 1) {
            if(!on_ground && jump_info.hit_wall) {
                AIOSetWallState(2);
            } else if(in_state > 0.9f) {
                AIOLog("wall_missed");
                AIOSetWallState(0);
            }
        } else if(aio_wall_state == 2) {
            if(on_ground || (!jump_info.hit_wall && in_state > 0.3f)) {
                AIOSetWallState(0);
            }
        } else if(on_ground && in_state > 0.2f) {
            AIOSetWallState(0);
        }

        return;
    }

    if(!aio_walls || time < aio_wall_next || !combat_allowed || !AIOCanJumpKick() || !AIOReadyToHop()) {
        return;
    }

    aio_wall_next = time + 0.35f;
    vec3 to_me = AIOFlat(this_mo.position - target.position);
    float dist = length(to_me);

    if(dist < 1.0f || dist > 3.5f) {
        return;
    }

    to_me /= dist;
    bool pressed = dist < 2.2f || target.GetIntVar("state") == _attack_state || dot(target.velocity, to_me) > 1.0f;
    vec3 wall_dir;

    if(!pressed || !AIOFindWallBehind(target, wall_dir) ||
            RangedRandomFloat(0.0f, 1.0f) > mix(0.25f, 0.8f, game_difficulty) * (0.5f + aio_air)) {
        return;
    }

    // Hurt or ganged up on: flip out sideways; otherwise kick off the wall at it
    aio_wall_flip = temp_health < 0.5f || AIONearbyEnemies(3.5f) >= 2 || RangedRandomFloat(0.0f, 1.0f) < 0.3f;
    aio_wall_dir = wall_dir;
    aio_wall_side = vec3(-wall_dir.z, 0.0f, wall_dir.x) * (rand() % 2 == 0 ? 1.0f : -1.0f);
    aio_wall_next = time + RangedRandomFloat(4.0f, 8.0f);
    AIOSetWallState(1);
    AIORequestHop(false);  // a normal hop, so air control keeps us on the wall and steers after
    AIOLog(aio_wall_flip ? "wall_flip_start" : "wall_kick_start");
}

int AIONearbyEnemies(float range) {
    array<int> nearby;
    GetCharactersInSphere(this_mo.position, range, nearby);
    int count = 0;

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == this_mo.GetID()) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(nearby[i]);

        if(!this_mo.OnSameTeam(char) && !char.static_char && char.GetIntVar("knocked_out") == _awake) {
            ++count;
        }
    }

    return count;
}

// WantsToJumpOffWall() / WantsToFlipOffWall(): asked every frame on a wall
bool AIOWantsWallJump() {
    if(aio_on && aio_wall_state == 2 && !aio_wall_flip && time - aio_wall_state_time > 0.05f) {
        AIOSetWallState(3);
        AIOLog("wall_kick");
        return true;
    }

    return false;
}

bool AIOWantsWallFlip() {
    if(aio_on && aio_wall_state == 2 && aio_wall_flip && time - aio_wall_state_time > 0.05f) {
        AIOSetWallState(3);
        AIOLog("wall_flip");
        return true;
    }

    return false;
}

bool AIOWallMovement(vec3 &out move) {
    if(aio_wall_state == 1 || aio_wall_state == 2) {
        move = aio_wall_dir;  // onto the wall, and keep pressing into it
        return true;
    }

    if(aio_wall_state == 3 && aio_wall_flip) {
        move = aio_wall_side;  // curve the flip around the enemy
        return true;
    }

    return false;  // a wall kick steers at the target like any hop
}

// -- ledges: catch them when falling --
// WantsToGrabLedge(): falling in normal movement (knocked or stepped off an
// edge, a hop that went long): catch the edge; hanging NPCs climb up by themselves
bool AIOWantsLedgeGrab() {
    if(!aio_on || !aio_ledges || state != _movement_state) {
        return false;
    }

    if(ledge_info.on_ledge) {
        return true;
    }

    // Only when dropping below the ground we last stood on, not on a planned drop
    return !on_ground && this_mo.velocity.y < -2.0f && aio_wall_state == 0 && path_find_type != _pft_drop &&
        this_mo.position.y < aio_last_ground_y - 0.6f;
}

float aio_last_ground_y = 0.0f;

// -- climbing after a target on high ground there is no walkway to --
float aio_climb_next = 0.0f;

// Where to start climbing toward a target on high ground: the end of walkable
// ground on the way there, with a wall right past it and the top within reach
// (a rabbit's wall-run plus ledge grab tops out near 3.8 m)
bool AIOFindClimbSpot(MovementObject@ target, vec3 &out base, vec3 &out into_wall) {
    vec3 my_ground = GetNavPointPos(this_mo.position);
    vec3 top = GetNavPointPos(target.position);

    if(my_ground == vec3(0.0f) || top == vec3(0.0f)) {
        return false;
    }

    float height = top.y - my_ground.y;
    vec3 dir = AIOFlat(top - my_ground);
    float dist = length(dir);

    if(height < 1.0f || height > 3.8f || dist < 0.5f) {
        return false;
    }

    dir /= dist;
    vec3 edge = NavRaycast(my_ground, vec3(top.x, my_ground.y, top.z));

    if(edge == vec3(0.0f) || xz_distance(edge, my_ground) > 12.0f) {
        return false;
    }

    vec3 probe = edge + vec3(0.0f, 1.0f, 0.0f);

    if(col.GetRayCollision(probe, probe + dir * 2.0f) == probe + dir * 2.0f) {
        return false;  // the walkable ground ends at a drop, not a wall
    }

    base = edge;
    into_wall = dir;
    return true;
}

void AIOUpdateClimb(MovementObject@ target) {
    if(!aio_ledges || time < aio_climb_next || path_find_type != _pft_nav_mesh || trying_to_climb != _nothing ||
            !on_ground || state != _movement_state || species != _rabbit) {
        return;
    }

    aio_climb_next = time + 1.0f;
    float dy = target.position.y - this_mo.position.y;

    if(dy < 1.3f || dy > 4.5f || xz_distance(this_mo.position, target.position) > 12.0f || AIOReachable(target.position)) {
        return;
    }

    vec3 base, into_wall;

    if(!AIOFindClimbSpot(target, base, into_wall)) {
        AIOLog("climb_none dy=" + dy);
        aio_climb_next = time + 3.0f;
        return;
    }

    // Hand over to the stock routine: run to the wall, jump, wall-run, grab, climb
    path_find_type = _pft_climb;
    path_find_point = base;
    climb_dir = into_wall;
    path_find_give_up_time = 2.0f + xz_distance(this_mo.position, base) / 5.0f;
    aio_climb_next = time + 4.0f;
    AIOLog("climb_start dy=" + dy);
}

// HandleAIEvent: climbing milestones for the log
void AIOOnParkourEvent(AIEvent event) {
    if(event == _grabbed_ledge) {
        AIOLog("ledge_grab");
    } else if(event == _climbed_up) {
        AIOLog("climbed_up");
    }
}

// ---- team tactics: large battles -------------------------------------------
// About once a second a fighting NPC reads the battle within 15 m: its side's
// strength against the other side's (health, weapons, species; players count
// extra), and keeps a morale value. Then:
//   majority     - spread over the enemies, finish the weak, up to 3 strikers,
//                  even encirclement, hurt strikers tag out, arrive together
//   outnumbered  - call for help, pick off isolated enemies, hit and run,
//                  fight with a wall at the back, break surrounds early
//   broken morale (outnumbered and hurt) - fall back to the nearest allies
// Targets are chosen by score instead of "closest": rescue allies being
// beaten, finish hurt enemies, pick off isolated ones, don't overcrowd one.

int aio_team_state = 0;        // -2 badly outnumbered, -1 outnumbered, 0 even, 1 majority
float aio_team_ratio = 1.0f;   // our strength / theirs
float aio_team_next = 0.0f;
int aio_team_pending = 0;      // reading waiting to be confirmed by the next one
float aio_morale = -1.0f;      // 0..1, set on the first read
int aio_allies_near = 0;
int aio_enemies_near = 0;
int aio_seen_ko_allies = 0;
int aio_seen_ko_enemies = 0;
float aio_target_next = 0.0f;
float aio_help_next = 0.0f;
float aio_wait_allies_until = 0.0f;
float aio_wait_allies_next = 0.0f;
bool aio_hitrun_pending = false;
bool aio_team_holding = false;  // waiting / backing off: restore the fighting range after
float aio_hitrun_until = 0.0f;
float aio_fallback_until = 0.0f;
float aio_fallback_next = 0.0f;
vec3 aio_fallback_pos;

// On our team and on the enemy's too (a spectator, an observer): not in this fight
bool AIOIsNeutral(MovementObject@ char) {
    if(chase_target_id == -1 || char.GetID() == chase_target_id || !MovementObjectExists(chase_target_id)) {
        return false;
    }

    return this_mo.OnSameTeam(char) && char.OnSameTeam(ReadCharacterID(chase_target_id));
}

float AIOFighterPower(MovementObject@ char) {
    if(char.GetIntVar("knocked_out") != _awake) {
        return 0.0f;
    }

    int char_species = char.GetIntVar("species");
    float power = char_species == _wolf ? 3.0f : (char_species == _dog ? 1.5f : (char_species == _rat ? 0.8f : 1.0f));

    if(GetCharPrimaryWeapon(char) != -1) {
        power *= 1.4f;
    }

    power *= mix(0.6f, 1.0f, max(0.0f, min(1.0f, char.GetFloatVar("temp_health"))));

    if(char.controlled) {
        power *= 1.5f;  // a player is worth more than one guard
    }

    return power;
}

string AIOTeamStateName(int team_state) {
    if(team_state >= 1) return "majority";
    if(team_state == 0) return "even";
    if(team_state == -1) return "outnumbered";
    return "badly_outnumbered";
}

void AIOReadBattle() {
    if(time < aio_team_next) {
        return;
    }

    aio_team_next = time + RangedRandomFloat(0.8f, 1.2f);

    if(aio_morale < 0.0f) {
        aio_morale = 0.55f + p_aggression * 0.3f;
    }

    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 15.0f, nearby);
    float ours = AIOFighterPower(this_mo);
    float theirs = 0.0f;
    int allies = 0;
    int enemies = 0;
    int ko_allies = 0;
    int ko_enemies = 0;
    int my_id = this_mo.GetID();
    string ally_ids;

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == my_id) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(nearby[i]);

        if(char.static_char || AIOIsNeutral(char)) {
            continue;
        }

        bool awake = char.GetIntVar("knocked_out") == _awake;

        if(this_mo.OnSameTeam(char)) {
            if(awake) {
                ours += AIOFighterPower(char);
                ++allies;
                ally_ids += " " + nearby[i] + (char.controlled ? "p" : "") + "@" + int(distance(char.position, this_mo.position));
            } else {
                ++ko_allies;
            }
        } else if(awake) {
            theirs += AIOFighterPower(char);
            ++enemies;
        } else {
            ++ko_enemies;
        }
    }

    aio_allies_near = allies;
    aio_enemies_near = enemies;
    aio_team_ratio = theirs > 0.01f ? ours / theirs : 9.0f;
    // Bodies count first; power alone decides only lopsided fights (a rabbit facing a wolf)
    int bodies_ours = allies + 1;
    int new_state = 0;

    if((bodies_ours > enemies && aio_team_ratio >= 1.5f) || aio_team_ratio >= 2.5f) {
        new_state = 1;
    } else if(aio_team_ratio <= 0.4f) {
        new_state = -2;
    } else if(bodies_ours < enemies && aio_team_ratio <= 0.67f) {
        new_state = -1;
    }

    // Allies dropping shakes morale, enemies dropping lifts it; company steadies,
    // being hurt and outnumbered wears it down
    if(ko_allies > aio_seen_ko_allies) {
        aio_morale -= 0.15f * (ko_allies - aio_seen_ko_allies);
    }

    if(ko_enemies > aio_seen_ko_enemies) {
        aio_morale += 0.12f * (ko_enemies - aio_seen_ko_enemies);
    }

    aio_seen_ko_allies = ko_allies;
    aio_seen_ko_enemies = ko_enemies;
    aio_morale += (new_state >= 0 ? 0.03f : -0.02f) + (allies > 0 ? 0.01f : 0.0f) - (temp_health < 0.4f ? 0.03f : 0.0f);
    aio_morale = max(0.0f, min(1.0f, aio_morale));

    if(species == _wolf || species == _dog) {
        aio_morale = max(aio_morale, 0.5f);  // they never break
    }

    // Fighters step in and out of range all the time: act on a reading only once it repeats
    bool confirmed = new_state == aio_team_pending;
    aio_team_pending = new_state;

    if(new_state != aio_team_state && confirmed) {
        aio_team_state = new_state;
        AIOLog("team_state " + AIOTeamStateName(new_state) + " ratio=" + aio_team_ratio + " allies=" + allies +
            " enemies=" + enemies + " morale=" + aio_morale + " ally_ids=" + ally_ids);
    }
}

// Awake enemies of a character within 4 m of it that are after it
int AIOAttackersOn(int victim_id) {
    MovementObject@ victim = ReadCharacterID(victim_id);
    array<int> around;
    GetCharactersInSphere(victim.position, 4.0f, around);
    int count = 0;

    for(uint i = 0; i < around.size(); ++i) {
        if(around[i] == victim_id) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(around[i]);

        if(!victim.OnSameTeam(char) && char.GetIntVar("knocked_out") == _awake && char.GetIntVar("target_id") == victim_id) {
            ++count;
        }
    }

    return count;
}

// Who to fight: score the known enemies instead of taking the closest
int AIOChooseTarget(int closest) {
    if(!aio_on || !aio_team || closest == -1) {
        return closest;  // -1: no threats left, the stock victory path runs
    }

    bool current_valid = chase_target_id != -1 && MovementObjectExists(chase_target_id) &&
        ReadCharacterID(chase_target_id).GetIntVar("knocked_out") == _awake && situation.KnowsAbout(chase_target_id);

    if(time < aio_target_next && current_valid) {
        return chase_target_id;
    }

    aio_target_next = time + RangedRandomFloat(1.5f, 2.5f);
    int my_id = this_mo.GetID();
    int best = -1;
    float best_score = -1.0e6f;
    string best_why;

    for(uint i = 0; i < situation.known_chars.size(); ++i) {
        int id = situation.known_chars[i].id;

        if(situation.known_chars[i].friendly || situation.known_chars[i].knocked_out != _awake || !MovementObjectExists(id)) {
            continue;
        }

        MovementObject@ enemy = ReadCharacterID(id);

        if(enemy.GetIntVar("knocked_out") != _awake || enemy.static_char) {
            continue;
        }

        vec3 seen_at = situation.known_chars[i].last_seen_time > time - 3.0f ? enemy.position : situation.known_chars[i].last_known_position;
        float dist = distance(this_mo.position, seen_at);
        float score = -dist * 0.15f;
        string why = "near";

        // Finish the hurt
        score += (1.0f - max(0.0f, min(1.0f, enemy.GetFloatVar("temp_health")))) * 1.0f;

        // Pick off the isolated: how many of its own friends stand by it
        array<int> around;
        GetCharactersInSphere(enemy.position, 5.0f, around);
        int its_friends = 0;

        for(uint j = 0; j < around.size(); ++j) {
            if(around[j] != id && around[j] != my_id) {
                MovementObject@ other = ReadCharacterID(around[j]);

                if(!this_mo.OnSameTeam(other) && other.GetIntVar("knocked_out") == _awake) {
                    ++its_friends;
                }
            }
        }

        if(its_friends == 0) {
            score += 0.8f;
            why = "isolated";
        } else {
            score -= its_friends * 0.25f;
        }

        // It is coming for us
        if(enemy.GetIntVar("target_id") == my_id && dist < 4.0f) {
            score += 1.2f;
            why = "threat";
        }

        // Rescue: it is beating an ally who is down, hurt, or fighting more than one
        int victim = enemy.GetIntVar("target_id");

        if(victim != -1 && victim != my_id && MovementObjectExists(victim)) {
            MovementObject@ ally = ReadCharacterID(victim);

            if(this_mo.OnSameTeam(ally) && !AIOIsNeutral(ally) && ally.GetIntVar("knocked_out") == _awake &&
                    distance(enemy.position, ally.position) < 4.0f &&
                    (ally.GetIntVar("state") == _ragdoll_state ||
                     (ally.GetFloatVar("temp_health") < 0.5f && enemy.GetIntVar("state") == _attack_state) ||
                     AIOAttackersOn(victim) >= 2)) {
                score += 1.5f;
                why = "rescue";
            } else if(aio_cohesion && this_mo.OnSameTeam(ally) && !AIOIsNeutral(ally) &&
                    ally.GetIntVar("knocked_out") == _awake && AIOBehindAlly(enemy, ally)) {
                score += 1.2f;
                why = "cover_back";
            }
        }

        // Fight near the group, not out on our own
        if(aio_cohesion && aio_group_size > 0) {
            score -= max(0.0f, xz_distance(seen_at, aio_group_center) - 8.0f) * 0.12f;
        }

        // Spread out: allies already on it beyond a fair share
        int on_it = 0;

        for(int j = 0, len = GetNumCharacters(); j < len; ++j) {
            MovementObject@ char = ReadCharacter(j);

            if(char.GetID() != my_id && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                    char.GetIntVar("chase_target_id") == id && char.GetIntVar("knocked_out") == _awake) {
                ++on_it;
            }
        }

        // Local superiority: two on one wins exchanges (a fighter blocks one
        // direction at a time); past the striker cap the extras only circle
        int cap = (game_difficulty >= 0.5f ? 2 : 1) + (aio_team_state >= 1 ? 1 : 0);

        if(on_it >= 1 && on_it < cap) {
            score += 0.5f;

            if(why == "near") {
                why = "gang";
            }
        } else if(on_it >= cap + 1) {
            score -= 0.6f * (on_it - cap);
        }

        score += AIOCombatTargetBonus(enemy, dist, why);  // grips on allies, downed, backs, broken guards, blades

        if(id == chase_target_id) {
            score += dist < 3.0f ? 1.2f : 0.8f;  // don't flip-flop, least of all mid-exchange
        }

        if(score > best_score) {
            best_score = score;
            best = id;
            best_why = why;
        }
    }

    if(best == -1) {
        return closest;
    }

    if(best != chase_target_id && chase_target_id != -1) {
        AIOLog("target_switch why=" + best_why + " to=" + best);
    }

    return best;
}

// Per-tick team behaviour while fighting (after the sub-goal is picked)
void AIOUpdateTeam(MovementObject@ target) {
    if(!aio_team) {
        return;
    }

    AIOReadBattle();
    AIOUpdateCohesion(target);
    float dist = xz_distance(this_mo.position, target.position);
    bool pressed = target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") == this_mo.GetID();

    // Outnumbered: shout for allies who are not fighting yet
    if(aio_team_state <= -1 && time > aio_help_next && chase_target_id != -1 && MovementObjectExists(chase_target_id)) {
        aio_help_next = time + RangedRandomFloat(8.0f, 12.0f);
        int called = 0;

        for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
            MovementObject@ char = ReadCharacter(i);

            if(char.GetID() != this_mo.GetID() && !char.controlled && !char.static_char && this_mo.OnSameTeam(char) &&
                    char.GetIntVar("knocked_out") == _awake && distance(char.position, this_mo.position) < 18.0f &&
                    char.QueryIntFunction("int IsAggro()") != 1) {
                char.ReceiveScriptMessage("notice " + chase_target_id);
                ++called;
            }
        }

        if(called > 0) {
            this_mo.PlaySoundGroupVoice("engage", 0.0f);
            AIOLog("call_help allies=" + called);
        }
    }

    // Majority: the first to arrive waits briefly at kicking range so the group hits together
    if(!aio_cohesion && aio_team_state >= 1 && time > aio_wait_allies_next && dist > 4.0f && dist < 9.0f && !pressed && time >= aio_wait_allies_until) {
        aio_wait_allies_next = time + 1.0f;
        int behind = 0;

        for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
            MovementObject@ char = ReadCharacter(i);

            if(char.GetID() != this_mo.GetID() && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                    char.GetIntVar("chase_target_id") == chase_target_id && char.GetIntVar("knocked_out") == _awake &&
                    xz_distance(char.position, target.position) > dist + 3.0f) {
                ++behind;
            }
        }

        if(behind > 0 && RangedRandomFloat(0.0f, 1.0f) < 0.6f) {
            aio_wait_allies_until = time + RangedRandomFloat(1.2f, 2.0f);
            aio_wait_allies_next = aio_wait_allies_until + RangedRandomFloat(5.0f, 8.0f);
            AIOLog("wait_allies behind=" + behind);
        }
    }

    if(time < aio_wait_allies_until) {
        if(dist < 4.0f || pressed) {
            aio_wait_allies_until = 0.0f;  // it came to us: fight
        } else {
            target_attack_range = 6.0f;
            ai_attacking = false;
        }
    }

    // Outnumbered: strike, then back off and reset
    if(aio_hitrun_pending && state == _movement_state) {
        aio_hitrun_pending = false;
        aio_hitrun_until = time + RangedRandomFloat(1.2f, 2.2f);
        AIOLog("hit_and_run");
    }

    if(time < aio_hitrun_until) {
        target_attack_range = 5.0f;
        ai_attacking = false;
    }

    if(time < aio_wait_allies_until || time < aio_hitrun_until || time < aio_advance_until) {
        aio_team_holding = true;
    } else if(aio_team_holding) {
        aio_team_holding = false;
        instant_range_change = true;
    }

    // Broken: outnumbered, hurt and shaken - fall back to the nearest allies
    if(aio_morale_on && time > aio_fallback_next && time >= aio_fallback_until && aio_morale < 0.25f &&
            aio_team_state <= -1 && temp_health < 0.5f && species != _wolf && species != _dog) {
        aio_fallback_next = time + 2.0f;
        AIOStartFallBack(target);
    }

    if(time >= aio_fallback_until && aio_fallback_until > 0.0f) {
        aio_fallback_until = 0.0f;
        aio_morale = min(1.0f, aio_morale + 0.25f);  // regrouped
        AIOLog("regrouped morale=" + aio_morale);
    }
}

void AIOStartFallBack(MovementObject@ target) {
    // Behind the group when there is one to stand behind
    AIOReadGroup();

    if(aio_cohesion && aio_group_size > 0 && xz_distance(aio_group_center, target.position) > 4.0f) {
        vec3 away = AIOFlat(aio_group_center - target.position);
        away = length_squared(away) > 0.01f ? normalize(away) : this_mo.GetFacing() * -1.0f;
        aio_fallback_pos = aio_group_center + away * 2.5f;
        AIOLog("fall_back to_group=" + aio_group_size + " morale=" + aio_morale);
        aio_fallback_until = time + RangedRandomFloat(3.0f, 5.0f);
        aio_fallback_next = aio_fallback_until + 10.0f;
        ai_attacking = false;
        return;
    }

    // The nearest ally 6-30 m away who is not in the thick of it, else simply away
    int best = -1;
    float best_dist = 30.0f;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == this_mo.GetID() || !this_mo.OnSameTeam(char) || AIOIsNeutral(char) || char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        float d = distance(char.position, this_mo.position);

        if(d > 6.0f && d < best_dist && distance(char.position, target.position) > 5.0f) {
            best_dist = d;
            best = char.GetID();
        }
    }

    if(best == -1) {
        return;  // nobody to regroup with: running only shows them your back
    }

    aio_fallback_pos = ReadCharacterID(best).position;
    AIOLog("fall_back to_ally=" + best + " morale=" + aio_morale);

    aio_fallback_until = time + RangedRandomFloat(3.0f, 5.0f);
    aio_fallback_next = aio_fallback_until + 10.0f;
    ai_attacking = false;
}

bool AIOFallBackMovement(vec3 &out move) {
    if(time < aio_fallback_until) {
        ai_attacking = false;
        move = GetMovementToPoint(aio_fallback_pos, 1.5f);
        return true;
    }

    return false;
}

// Team-aware sub-goal shaping (inside AIOAdjustAttackSubGoal)
AISubGoal AIOTeamSubGoal(AISubGoal picked, int strikers) {
    if(!aio_team) {
        return picked;
    }

    bool attacking = picked == _rush_and_attack || picked == _wait_and_attack;

    // Majority: a hurt striker steps out and lets a fresh ally take the turn
    if(aio_team_state >= 1 && attacking && temp_health < 0.4f && strikers > 0) {
        AIOLog("tag_out");
        return _surround_target;
    }

    // Morale: shaken fighters hang back, confident ones press
    if(aio_morale < 0.35f && attacking && RangedRandomFloat(0.0f, 1.0f) < 0.5f) {
        return _defend;
    }

    if(aio_morale > 0.8f && aio_team_state >= 1 && (picked == _defend || picked == _provoke_attack) &&
            RangedRandomFloat(0.0f, 1.0f) < 0.3f) {
        return _rush_and_attack;
    }

    return picked;
}

// Wall at the back of a spot (in the direction away from the target)
bool AIOWallBehindSpot(const vec3 &in spot, const vec3 &in away) {
    vec3 from = spot + vec3(0.0f, 1.0f, 0.0f);
    return col.GetRayCollision(from, from + away * 1.5f) != from + away * 1.5f;
}

// ---- team cohesion: stick together ------------------------------------------
// A team that keeps together survives: nobody gets picked off alone or caught
// with an enemy behind them. Each fighter reads where its group is (awake
// teammates within 25 m; a player on the team counts double, so NPC allies
// keep with the player) and:
//   regroup        cut off from the team and not fighting: rejoin it
//   leash          a chase pulls it far from the group: let the enemy go
//   advance        far ahead of the group on the way in: wait for it
//   close ranks    outnumbered: pull in tight, back to back
//   shelter        badly hurt: step behind the group rather than run alone
// Target choice also prefers enemies near the group and enemies attacking an
// ally from behind (see AIOChooseTarget).

vec3 aio_group_center;
int aio_group_size = 0;          // teammates counted, not us
float aio_nearest_ally = 1.0e6f;
float aio_group_next = 0.0f;
float aio_regroup_until = 0.0f;  // moving to aio_regroup_point until then
float aio_regroup_next = 0.0f;
vec3 aio_regroup_point;
string aio_regroup_why;
float aio_advance_until = 0.0f;  // holding for the group until then
float aio_advance_next = 0.0f;

void AIOReadGroup() {
    if(time < aio_group_next) {
        return;
    }

    aio_group_next = time + 0.5f;
    vec3 sum;
    float weight = 0.0f;
    aio_group_size = 0;
    aio_nearest_ally = 1.0e6f;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() == this_mo.GetID() || char.static_char || !this_mo.OnSameTeam(char) || AIOIsNeutral(char) ||
                char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        float d = distance(char.position, this_mo.position);

        if(d > 25.0f) {
            continue;
        }

        float w = char.controlled ? 2.0f : 1.0f;
        sum += char.position * w;
        weight += w;
        ++aio_group_size;
        aio_nearest_ally = min(aio_nearest_ally, d);
    }

    aio_group_center = weight > 0.0f ? sum / weight : this_mo.position;
}

// Busy trading blows: cohesion never pulls a fighter out of an exchange
bool AIOEngaged(MovementObject@ target, float dist) {
    return dist < 4.0f || state == _attack_state ||
        (target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") == this_mo.GetID());
}

void AIOStartRegroup(const vec3 &in point, const string &in why, float min_s, float max_s) {
    aio_regroup_point = point;
    aio_regroup_why = why;
    aio_regroup_until = time + RangedRandomFloat(min_s, max_s);
    aio_regroup_next = aio_regroup_until + RangedRandomFloat(2.0f, 3.5f);
    ai_attacking = false;
    AIOLog(why + " group=" + aio_group_size + " ally_dist=" + int(aio_nearest_ally) + " center_dist=" +
        int(xz_distance(this_mo.position, aio_group_center)));
}

void AIOUpdateCohesion(MovementObject@ target) {
    if(!aio_cohesion) {
        aio_regroup_until = 0.0f;
        aio_advance_until = 0.0f;
        return;
    }

    AIOReadGroup();

    if(aio_group_size == 0) {
        return;  // alone: nothing to stick to
    }

    float dist = xz_distance(this_mo.position, target.position);
    bool engaged = AIOEngaged(target, dist);
    float to_center = xz_distance(this_mo.position, aio_group_center);
    float target_to_center = xz_distance(target.position, aio_group_center);

    if(engaged) {
        aio_regroup_until = 0.0f;  // it came to us: fight
        aio_advance_until = 0.0f;
    }

    // An ongoing regroup ends once back with the others
    if(time < aio_regroup_until && xz_distance(this_mo.position, aio_regroup_point) < 2.5f) {
        aio_regroup_until = 0.0f;
    }

    bool chasing = AIOChasingRetreat(target);

    if(chasing && to_center > 14.0f && target_to_center > to_center && dist > 5.0f && time >= aio_advance_until) {
        // The chase drags us away from everyone: let it go, stop here (no run back)
        aio_advance_until = time + RangedRandomFloat(1.5f, 2.5f);
        aio_advance_next = aio_advance_until + 1.0f;
        AIOLog("leash center_dist=" + int(to_center));
    }

    if(!engaged && !chasing && time >= aio_regroup_until && time > aio_regroup_next) {
        vec3 away = AIOFlat(aio_group_center - target.position);
        away = length_squared(away) > 0.01f ? normalize(away) : this_mo.GetFacing() * -1.0f;

        if(temp_health < 0.35f && dist < 8.0f && target_to_center < 10.0f && species != _wolf) {
            // Badly hurt: behind the group, away from the enemy
            AIOStartRegroup(aio_group_center + away * 2.5f, "shelter", 1.5f, 2.5f);
        } else if(aio_team_state <= -1 && to_center > 4.0f && dist > 3.0f) {
            // Outnumbered: tight together so nobody gets in behind
            AIOStartRegroup(aio_group_center, "close_ranks", 1.0f, 2.0f);
        } else if(aio_nearest_ally > 10.0f && dist > 6.0f && dist > target_to_center - 2.0f) {
            // Cut off to the side or behind: rejoin before taking anyone on alone
            // (ahead of the group on the way in is "advance together": hold)
            AIOStartRegroup(aio_group_center, "regroup", 1.5f, 3.0f);
        }
    }

    // Advance together: well ahead of the group on the way in, hold for it
    if(!engaged && time >= aio_regroup_until && time > aio_advance_next && dist > 5.0f && dist < 16.0f &&
            dist < target_to_center - 5.0f && time >= aio_advance_until) {
        aio_advance_next = time + 1.0f;
        aio_advance_until = time + RangedRandomFloat(1.0f, 1.6f);
        AIOLog("advance_wait ahead=" + int(target_to_center - dist));
    }

    if(time < aio_advance_until) {
        target_attack_range = max(target_attack_range, dist);  // hold the line where we are
        ai_attacking = false;
    }
}

bool AIOCohesionMovement(vec3 &out move) {
    if(time < aio_regroup_until) {
        ai_attacking = false;
        move = GetMovementToPoint(aio_regroup_point, 1.5f);
        return true;
    }

    return false;
}

// Behind a teammate whose back an enemy has got to: worth covering
bool AIOBehindAlly(MovementObject@ enemy, MovementObject@ ally) {
    vec3 to_enemy = AIOFlat(enemy.position - ally.position);
    float d = length(to_enemy);
    return d > 0.1f && d < 3.0f && dot(to_enemy / d, AIOFlat(ally.GetFacing())) < -0.3f;
}

// ---- weapon throwing -------------------------------------------------------

bool aio_want_throw = false;
float aio_throw_decided_time = -10.0f;
float aio_throw_next_check = 0.0f;
float aio_throw_cooldown_until = 0.0f;
int aio_thrown_weapon_id = -1;

bool AIOHasSpareWeapon() {
    for(int slot = _sheathed_left; slot <= _sheathed_right; ++slot) {
        int id = weapon_slots[slot];

        if(id != -1 && ReadItemID(id).GetType() == _weapon && ReadItemID(id).GetLabel() != "scabbard") {
            return true;
        }
    }

    int secondary = weapon_slots[secondary_weapon_slot];
    return secondary != -1 && ReadItemID(secondary).GetType() == _weapon;
}

// One-shot answer for WantsToThrowItem(); the body then asks GetThrowTarget()
bool AIOConsumeThrow() {
    if(aio_want_throw) {
        aio_want_throw = false;
        return true;
    }

    return false;
}

// Follow a released weapon for the log: hit, caught or missed
int aio_flying_item = -1;
int aio_flying_target = -1;
float aio_flying_since = 0.0f;

void AIOTrackThrownItem() {
    if(aio_flying_item == -1) {
        return;
    }

    if(!ObjectExists(aio_flying_item)) {
        aio_flying_item = -1;
        return;
    }

    ItemObject@ item = ReadItemID(aio_flying_item);
    int stuck = item.StuckInWhom();
    int holder = item.IsHeld() ? item.HeldByWhom() : -1;
    string outcome = "";

    if(stuck == aio_flying_target) {
        outcome = "throw_hit";
    } else if(holder == aio_flying_target) {
        outcome = "throw_caught";
    } else if(stuck != -1) {
        outcome = "throw_hit_other";
    } else if(time - aio_flying_since > 2.5f) {
        outcome = "throw_miss";
    }

    if(outcome != "") {
        AIOLog(outcome);

        if(outcome == "throw_caught" && aio_flying_target != -1) {
            AIOFoeFor(aio_flying_target).caught += 1.0f;  // it catches: throw at it less
        } else if(outcome == "throw_hit" && aio_flying_target == chase_target_id && aio_weapon_sense) {
            aio_throw_followup_until = time + 1.5f;  // it is staggered: punish it now
            AIOLog("throw_followup");
        }

        aio_flying_item = -1;
    }
}

// ---- weapon sense: keep or throw, throw when they can't react, respect throwers
// A thrown weapon flies at 20 m/s divided by its mass (long weapons thrown with
// the throw animation count a quarter of their mass) after a ~0.25 s wind-up,
// and the body leads a moving target in a straight line. Nobody in the air can
// dodge, block or sidestep it: a target that will still be airborne when the
// blade arrives is a near-sure hit. The other way round, jumping at someone who
// holds a blade and faces us hands them that same shot.

bool aio_weapon_sense = true;
float aio_throw_followup_until = 0.0f;
float aio_respect_thrower_until = 0.0f;

// What a weapon is worth thrown, against keeping it to hit with (0 = keep)
float AIOThrowValue(int item_id, int owner_species) {
    ItemObject@ io = ReadItemID(item_id);
    string label = io.GetLabel();

    if(label == "knife") return 1.0f;
    if(label == "spear") return 0.9f;
    if(label == "sword" || label == "rapier") return 0.5f;
    if(label == "big_sword") return owner_species == _dog ? 0.45f : 0.3f;
    if(label == "staff") return 0.25f;
    if(label == "") return io.GetMass() < 1.5f ? 0.9f : 0.3f;  // unlabelled: light ones throw like knives
    return 0.0f;
}

float AIOThrowSpeed(int item_id) {
    ItemObject@ io = ReadItemID(item_id);
    string label = io.GetLabel();
    float mass = io.GetMass();

    if(label == "big_sword" || label == "spear" || label == "staff") {
        mass *= 0.25f;
    }

    return 20.0f / max(1.0f, mass);
}

// Seconds before an airborne character is back on its feet
float AIOAirTimeLeft(MovementObject@ char) {
    if(char.GetBoolVar("on_ground")) {
        return 0.0f;
    }

    vec3 p = char.position;
    vec3 below = col.GetRayCollision(p, p - vec3(0.0f, 30.0f, 0.0f));
    float h = max(0.0f, p.y - below.y - 0.9f);  // hips above the feet
    float vy = char.velocity.y;
    const float g = 9.81f;
    return (vy + sqrt(max(0.0f, vy * vy + 2.0f * g * h))) / g;
}

bool AIOBackTurned(MovementObject@ char) {
    vec3 to_me = AIOFlat(this_mo.position - char.position);
    float d = length(to_me);
    return d > 0.1f && dot(AIOFlat(char.GetFacing()), to_me / d) < -0.2f;
}

// The target holds something worth throwing and faces us at throwing range:
// how dangerous it is to be in the air in front of it (0..1)
float AIOThrowThreat(MovementObject@ target) {
    if(!aio_on || !aio_weapon_sense) {
        return 0.0f;
    }

    int weap = GetCharPrimaryWeapon(target);

    if(weap == -1) {
        return 0.0f;
    }

    float value = AIOThrowValue(weap, target.GetIntVar("species"));

    if(value < 0.4f) {
        return 0.0f;
    }

    vec3 to_me = AIOFlat(this_mo.position - target.position);
    float d = length(to_me);

    if(d < 2.5f || d > 11.0f || dot(AIOFlat(target.GetFacing()), to_me / d) < 0.5f) {
        return 0.0f;
    }

    return value;
}

// Before a flying kick: not into a blade (a short memory, so a veto isn't re-rolled every tick)
bool AIORespectThrower(MovementObject@ target, const string &in kind) {
    if(time < aio_respect_thrower_until) {
        return true;
    }

    float threat = AIOThrowThreat(target);

    if(threat > 0.0f && RangedRandomFloat(0.0f, 1.0f) < 0.7f * threat) {
        aio_respect_thrower_until = time + RangedRandomFloat(1.2f, 2.0f);
        AIOLog("respect_thrower kind=" + kind);
        return true;
    }

    return false;
}

bool aio_throw_was_setting_up = false;
float aio_throw_setup_until = 0.0f;
float aio_throw_next_setup = 0.0f;

void AIOUpdateThrow(MovementObject@ target) {
    if(aio_thrown_weapon_id != -1) {
        if(weapon_slots[primary_weapon_slot] != aio_thrown_weapon_id) {
            AIOLog("throw_release");
            aio_flying_item = aio_thrown_weapon_id;
            aio_flying_target = aio_throw_target_id;
            aio_flying_since = time;
            aio_thrown_weapon_id = -1;
        } else if(time - aio_throw_decided_time > 1.5f) {
            aio_thrown_weapon_id = -1;
        }
    }

    if(time < aio_throw_followup_until) {
        target_attack_range = 0.0f;
        ai_attacking = true;
        dont_attack_before = 0.0f;
    }

    // Setting up: back off to throwing range and hold attacks
    bool setting_up = time < aio_throw_setup_until;

    if(setting_up) {
        target_attack_range = 5.5f;
        ai_attacking = false;
        aio_throw_was_setting_up = true;
    } else if(aio_throw_was_setting_up) {
        aio_throw_was_setting_up = false;
        instant_range_change = true;
    }

    if(aio_want_throw) {
        if(time - aio_throw_decided_time > 1.0f) {
            aio_want_throw = false;
        }

        return;
    }

    if(time < aio_throw_next_check) {
        return;
    }

    aio_throw_next_check = time + RangedRandomFloat(0.3f, 0.5f);

    if(time < aio_throw_cooldown_until || aio_throw <= 0.0f || species == _wolf) {
        return;
    }

    int weap = weapon_slots[primary_weapon_slot];

    if(weap == -1 || !combat_allowed || startled || state != _movement_state || !on_ground ||
            tethered != _TETHERED_FREE || throw_knife_layer_id != -1 || going_to_throw_item) {
        return;
    }

    string label = ReadItemID(weap).GetLabel();
    float min_dist = 4.0f;
    float max_dist = 9.5f;
    float throw_value = 1.0f;

    if(aio_weapon_sense) {
        throw_value = AIOThrowValue(weap, species);

        if(throw_value <= 0.0f) {
            return;
        }
    } else if(label != "knife" && label != "spear" && !(label == "big_sword" && species == _dog)) {
        return;
    }

    if(label == "spear") {
        min_dist = 5.0f;
        max_dist = 18.0f;
    } else if(label == "big_sword" && species == _dog) {
        min_dist = 7.0f;
    }

    if(target.GetIntVar("knocked_out") != _awake || target.GetIntVar("state") == _ragdoll_state) {
        return;
    }

    float dist = distance(this_mo.position, target.position);
    bool spare = AIOHasSpareWeapon();

    // Still in the air when the blade arrives: nothing it can do about it
    bool sure_air = aio_weapon_sense && !target.GetBoolVar("on_ground") &&
        AIOAirTimeLeft(target) > dist / AIOThrowSpeed(weap) + 0.3f;

    if(sure_air && dist >= 2.5f) {
        min_dist = 2.5f;
    }

    if(dist < min_dist) {
        // Too close to throw: a knife thrower with a spare now and then backs off for a throw
        if(!setting_up && spare && label == "knife" && time > aio_throw_next_setup &&
                sub_goal != _surround_target && sub_goal != _escape_surround &&
                RangedRandomFloat(0.0f, 1.0f) < aio_throw * mix(0.05f, 0.2f, game_difficulty)) {
            aio_throw_setup_until = time + RangedRandomFloat(1.2f, 2.0f);
            aio_throw_next_setup = aio_throw_setup_until + RangedRandomFloat(5.0f, 9.0f);
            AIOLog("throw_setup dist=" + dist);
        }

        return;
    }

    if(dist > max_dist || time - target_history.LastUpdated() > 0.4f ||
            !AIOClearLine(this_mo.position, target.position)) {
        return;
    }

    vec3 away = normalize(AIOFlat(target.position - this_mo.position));
    bool fleeing = dot(target.velocity, away) > 2.0f;
    bool airborne = !target.GetBoolVar("on_ground");
    bool unreachable = !AIOReachable(target.position);

    float chance = aio_throw * mix(0.3f, 1.0f, game_difficulty) * 0.35f;

    if(spare) {
        chance *= 2.0f;
    }

    if(fleeing) {
        chance *= 2.5f;
    }

    if(sure_air) {
        chance *= 5.0f;  // can't dodge, block or sidestep in the air
    } else if(airborne) {
        chance *= 1.5f;
    }

    if(aio_weapon_sense) {
        chance *= throw_value;

        if(AIOBackTurned(target)) {
            chance *= 2.0f;  // can't see it coming
        }

        if(target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") != this_mo.GetID()) {
            chance *= 2.0f;  // committed to a swing at someone else
        }

        AIOFoe@ foe = AIOFoeFor(chase_target_id);
        chance *= max(0.2f, 1.0f - 0.35f * foe.caught);  // it catches what we throw

        if(target.GetIntVar("species") == _cat) {
            chance *= 0.7f;
        }
    }

    if(unreachable) {
        chance *= 4.0f;
    }

    if(setting_up) {
        chance *= 4.0f;  // backed off on purpose
    }

    if(time - aio_engage_time < 4.0f) {
        chance *= 2.5f;  // open the fight with a throw while closing in
    }

    if(aio_team && aio_team_state <= -1) {
        chance *= 1.5f;  // outnumbered: thin them out from range
    }

    if(!spare && !fleeing && !unreachable && !sure_air && (label == "knife" || dist < 9.0f)) {
        // Don't toss the only weapon at someone who is coming anyway, least of all a big one
        chance *= (!aio_weapon_sense || label == "knife") ? 0.25f : 0.1f;
    }

    if(RangedRandomFloat(0.0f, 1.0f) > chance) {
        return;
    }

    aio_want_throw = true;
    aio_throw_setup_until = 0.0f;
    aio_throw_target_id = chase_target_id;
    aio_throw_decided_time = time;
    aio_thrown_weapon_id = weap;
    aio_throw_cooldown_until = time + RangedRandomFloat(2.5f, 5.0f);
    AIOLog("throw_decide weapon=" + label + " dist=" + dist + " spare=" + spare + " fleeing=" + fleeing + " unreachable=" + unreachable +
        " sure_air=" + sure_air);
}

// ---- feints ----------------------------------------------------------------

bool aio_feint_this_attack = false;
float aio_attack_start_time = 0.0f;

// HandleAIEvent(_attacking)
void AIOOnAttackStart() {
    aio_attack_start_time = time;
    aio_feint_this_attack = false;
    aio_hitrun_pending = aio_on && aio_team && aio_team_state <= -1 && RangedRandomFloat(0.0f, 1.0f) < 0.6f;

    if(!aio_hitrun_pending && aio_on && aio_escape_on && AIONearbyEnemies(4.0f) >= 2) {
        aio_hitrun_pending = RangedRandomFloat(0.0f, 1.0f) < 0.4f;  // among several of them: strike, then jump out
    }

    if(!aio_on || aio_feint <= 0.0f || chase_target_id == -1 || !on_ground) {
        return;
    }

    int weap = weapon_slots[primary_weapon_slot];

    if(weap != -1 && ReadItemID(weap).GetLabel() == "knife") {
        return;  // knife slashes are animation layers and can't be feinted
    }

    float chance = aio_feint * mix(0.1f, 0.45f, game_difficulty) * AIOAdaptFeint();

    if(ReadCharacterID(chase_target_id).controlled) {
        chance *= 1.5f;  // players read wind-ups; bots time blocks off the animation
    }

    aio_feint_this_attack = RangedRandomFloat(0.0f, 1.0f) < chance;
}

bool AIOWantsToFeint() {
    if(!aio_on || !aio_feint_this_attack || chase_target_id == -1 || !MovementObjectExists(chase_target_id)) {
        return false;
    }

    // Wait for the target to commit to a block, or a short beat into the wind-up
    bool target_blocking = ReadCharacterID(chase_target_id).GetBoolVar("active_blocking");

    if(target_blocking || time - aio_attack_start_time > 0.12f) {
        aio_feint_this_attack = false;
        dont_attack_before = 0.0f;
        AIOLog("feint target_blocking=" + target_blocking);
        return true;
    }

    return false;
}

// ---- attack variety --------------------------------------------------------

string aio_prev_attack = "";
string aio_prev_attack2 = "";

// Never throw the same ground attack three times running
void AIOVaryAttack(string &inout attack_str) {
    if(aio_on && attack_str == aio_prev_attack && attack_str == aio_prev_attack2) {
        if(attack_str == "stationary") {
            attack_str = (rand() % 2 == 0) ? "moving" : "low";
        } else if(attack_str == "moving") {
            attack_str = (rand() % 2 == 0) ? "stationary" : "low";
        } else {
            attack_str = (rand() % 2 == 0) ? "stationary" : "moving";
        }
    }

    aio_prev_attack2 = aio_prev_attack;
    aio_prev_attack = attack_str;
}

// ---- group tactics: attack turns, flanking, opportunism --------------------

int aio_flank_slot = 0;
int aio_flank_total = 0;          // teammates circling the same enemy, us included

int AIOCountFlankersOn(int enemy_id) {
    int count = 1;

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);

        if(char.GetID() != this_mo.GetID() && AIOIsAIOBrain(char) && this_mo.OnSameTeam(char) &&
                char.GetIntVar("aio_flanking_target") == enemy_id) {
            ++count;
        }
    }

    return count;
}
float aio_next_flank_slot_update = 0.0f;
bool aio_was_flanking = false;
float aio_next_flank_strike = 0.0f;

// Teammates fighting the same enemy: how many, and how many are striking
void AIOCountTeammatesOn(int enemy_id, int &out engaged, int &out strikers, int &out flank_slot) {
    engaged = 0;
    strikers = 0;
    flank_slot = 0;
    MovementObject@ enemy = ReadCharacterID(enemy_id);
    int my_id = this_mo.GetID();

    for(int i = 0, len = GetNumCharacters(); i < len; ++i) {
        MovementObject@ char = ReadCharacter(i);
        int id = char.GetID();

        if(id == my_id || !AIOIsAIOBrain(char) || !this_mo.OnSameTeam(char) ||
                char.GetIntVar("knocked_out") != _awake ||
                char.GetIntVar("chase_target_id") != enemy_id ||
                distance_squared(char.position, enemy.position) > 100.0f) {
            continue;
        }

        if(char.QueryIntFunction("int IsAggro()") != 1) {
            continue;
        }

        ++engaged;

        if(char.GetIntVar("aio_attack_slot_target") == enemy_id) {
            ++strikers;
        }

        if(char.GetIntVar("aio_flanking_target") == enemy_id && id < my_id) {
            ++flank_slot;
        }
    }
}

// Applied to every attack sub-goal pick
AISubGoal AIOAdjustAttackSubGoal(AISubGoal picked) {
    if(!aio_on || chase_target_id == -1 || !MovementObjectExists(chase_target_id) ||
            state == _ragdoll_state || state == _hit_reaction_state) {
        return picked;
    }

    if(time < aio_escape_until) {
        return _escape_surround;
    }

    MovementObject@ target = ReadCharacterID(chase_target_id);
    float dist = distance(this_mo.position, target.position);

    // The target is swinging at someone else: punish it
    if(combat_allowed && dont_attack_before <= time && dist < 6.0f &&
            target.GetIntVar("state") == _attack_state && target.GetIntVar("target_id") != this_mo.GetID()) {
        return _rush_and_attack;
    }

    // It faces us with its guard up and a teammate stands behind it: hold its
    // eyes, the one behind lands the hit (aio_combat.as)
    if(AIOPinning(target, dist)) {
        return _provoke_attack;
    }

    if(!aio_group || !combat_allowed) {
        return picked;
    }

    int engaged, strikers, flank_slot;
    AIOCountTeammatesOn(chase_target_id, engaged, strikers, flank_slot);

    if(engaged == 0) {
        return picked;
    }

    int max_strikers = (game_difficulty >= 0.5f ? 2 : 1) + (aio_team && aio_team_state >= 1 ? 1 : 0);
    picked = AIOTeamSubGoal(picked, strikers);

    if(picked == _rush_and_attack || picked == _wait_and_attack) {
        return strikers < max_strikers ? picked : _surround_target;
    }

    if(picked == _defend || picked == _provoke_attack) {
        return strikers > 0 ? _surround_target : picked;
    }

    return picked;
}

void AIOUpdateFlanking(MovementObject@ target) {
    if(sub_goal != _surround_target) {
        aio_flanking_target = -1;
        aio_was_flanking = false;
        return;
    }

    aio_flanking_target = chase_target_id;

    if(!aio_was_flanking || time > aio_next_flank_slot_update) {
        aio_next_flank_slot_update = time + 0.5f;
        int engaged, strikers;
        AIOCountTeammatesOn(chase_target_id, engaged, strikers, aio_flank_slot);

        aio_flank_total = AIOCountFlankersOn(chase_target_id);

        if(!aio_was_flanking) {
            AIOLog("surround slot=" + aio_flank_slot + " strikers=" + strikers);
        }
    }

    aio_was_flanking = true;

    // Strike when the target turns its back or is busy
    float dist = xz_distance(this_mo.position, target.position);
    vec3 to_me = normalize(AIOFlat(this_mo.position - target.position));
    vec3 target_front = AIOFlat(target.GetFacing());

    if(length_squared(target_front) > 0.01f) {
        target_front = normalize(target_front);
    }

    int target_state = target.GetIntVar("state");
    bool behind = dot(target_front, to_me) < -0.1f;
    bool busy = target_state == _attack_state || target_state == _hit_reaction_state;

    if(dist < 3.4f && (behind || busy) && dont_attack_before <= time && time > aio_next_flank_strike) {
        aio_next_flank_strike = time + 0.5f;

        if(RangedRandomFloat(0.0f, 1.0f) < mix(0.3f, 0.9f, game_difficulty)) {
            SetSubGoal(_rush_and_attack);
            sub_goal_pick_time = time + RangedRandomFloat(0.8f, 1.4f);
            ai_attacking = true;
            aio_flanking_target = -1;
            AIOLog("flank_strike behind=" + behind + " busy=" + busy);
        }
    }
}

// Circle to a flank slot around the target instead of queueing in front
vec3 AIOSurroundMovement(const vec3 &in target_pos) {
    MovementObject@ target = ReadCharacterID(chase_target_id);
    vec3 front = AIOFlat(target.GetFacing());
    front = length_squared(front) > 0.01f ? normalize(front) : vec3(0.0f, 0.0f, 1.0f);

    float deg = 105.0f;

    if(aio_team && aio_flank_total >= 2) {
        deg = 100.0f + (aio_flank_slot % aio_flank_total) * (160.0f / (aio_flank_total - 1));
    } else {
        switch(aio_flank_slot % 5) {
            case 0: deg = 105.0f; break;
            case 1: deg = -105.0f; break;
            case 2: deg = 180.0f; break;
            case 3: deg = 145.0f; break;
            case 4: deg = -145.0f; break;
        }
    }

    vec3 want_dir = AIORotateY(front, deg * 3.14159f / 180.0f);
    vec3 rel = AIOFlat(this_mo.position - target_pos);
    float r = length(rel);
    vec3 cur_dir = r > 0.05f ? rel / r : want_dir;

    // Walk around the target, never through it
    float angle = atan2(cur_dir.x * want_dir.z - cur_dir.z * want_dir.x, dot(cur_dir, want_dir));
    angle = max(-0.8f, min(0.8f, angle));
    vec3 goal_pos = target_pos + AIORotateY(cur_dir, angle) * 2.7f;

    vec3 nav_from = GetNavPointPos(target_pos);

    if(nav_from != vec3(0.0f)) {
        vec3 clipped = NavRaycast(nav_from, vec3(goal_pos.x, nav_from.y, goal_pos.z));

        if(clipped != vec3(0.0f)) {
            goal_pos = clipped;
        }
    }

    return GetMovementToPoint(goal_pos, 0.5f);
}

// ---- breaking out of a surround --------------------------------------------

float aio_escape_until = 0.0f;
float aio_next_surround_check = 0.0f;
vec3 aio_escape_dir;

void AIOCheckSurrounded() {
    if(!aio_escape_on || time < aio_escape_until || time < aio_next_surround_check || state != _movement_state) {
        return;
    }

    aio_next_surround_check = time + 0.5f;
    array<int> nearby;
    GetCharactersInSphere(this_mo.position, 4.0f, nearby);
    array<vec3> dirs;
    vec3 sum;

    for(uint i = 0; i < nearby.size(); ++i) {
        if(nearby[i] == this_mo.GetID()) {
            continue;
        }

        MovementObject@ char = ReadCharacterID(nearby[i]);

        if(this_mo.OnSameTeam(char) || char.static_char || char.GetIntVar("knocked_out") != _awake) {
            continue;
        }

        vec3 dir = AIOFlat(char.position - this_mo.position);

        if(length_squared(dir) < 0.01f) {
            continue;
        }

        dir = normalize(dir);
        dirs.push_back(dir);
        sum += dir;
    }

    if(dirs.size() < 2) {
        return;
    }

    bool flanked = false;

    for(uint i = 0; i < dirs.size() && !flanked; ++i) {
        for(uint j = i + 1; j < dirs.size(); ++j) {
            if(dot(dirs[i], dirs[j]) < -0.35f) {
                flanked = true;
                break;
            }
        }
    }

    float escape_chance = (aio_team && aio_team_state <= -1) ? mix(0.6f, 1.0f, game_difficulty) : mix(0.3f, 0.8f, game_difficulty);

    if(!flanked || RangedRandomFloat(0.0f, 1.0f) > escape_chance) {
        return;
    }

    // Step out so every enemy ends up on one side
    vec3 out_dir = length_squared(sum) > 0.01f ? normalize(sum) * -1.0f : AIORotateY(dirs[0], 1.5708f);
    aio_escape_dir = out_dir;
    aio_escape_until = time + RangedRandomFloat(1.0f, 1.8f);
    SetSubGoal(_escape_surround);
    sub_goal_pick_time = aio_escape_until;
    ai_attacking = false;

    if(RangedRandomFloat(0.0f, 1.0f) < 0.5f && AIOLaunchEscapeJump(this_mo.position - out_dir, "surrounded")) {
        // leaping out
    } else if(aio_rolls && rand() % 2 == 0) {
        wants_to_roll = true;
    }

    AIOLog("escape_surround enemies=" + dirs.size());
}

vec3 AIOEscapeMovement() {
    vec3 goal_pos = this_mo.position + aio_escape_dir * 4.0f;
    vec3 from = GetNavPointPos(this_mo.position);

    if(from != vec3(0.0f)) {
        vec3 slid = NavRaycastSlide(from, from + aio_escape_dir * 4.0f, 2);

        if(slid != vec3(0.0f)) {
            goal_pos = slid;
        }
    }

    return GetMovementToPoint(goal_pos, 0.0f);
}

// ---- searching ------------------------------------------------------------

// Where a lost target probably went: last seen spot plus its heading, on the navmesh
vec3 AIOPredictLostTarget() {
    float since = min(2.0f, max(0.0f, time - target_history.LastUpdated()));
    vec3 last_pos = target_history.GetPos(target_history.LastUpdated());
    vec3 last_vel = AIOFlat(target_history.GetVel(target_history.LastUpdated()));
    vec3 from = GetNavPointPos(last_pos);

    if(from == vec3(0.0f) || length_squared(last_vel) < 0.25f) {
        return last_pos;
    }

    vec3 slid = NavRaycastSlide(from, from + last_vel * since, 2);
    return slid != vec3(0.0f) ? slid : last_pos;
}
