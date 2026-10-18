//-----------------------------------------------------------------------------
//           Name: overhaul_tab_ai.as
//    Description: "Overhaul AI" tab of the Overhaul window: the settings the
//                 NPC brains (aio_brain.as) read from aio_* config keys.
//-----------------------------------------------------------------------------

void DrawAITab() {
    if(MPClient()) {
        Note("Online the host runs every NPC: its AI settings apply, not these.");
    }

    Toggle("Overhaul AI", "aio_master", true,
        "Off: every NPC uses the stock AI (bug fixes stay on).");

    if(!CfgBool("aio_master", true)) {
        Note("The Overhaul AI is off: the settings below have no effect.");
    }

    Header("Jump kicks");
    Scale("Jump kicks", "aio_jump_kicks",
        "Flying kicks: into your openings, fast flat kicks, chase kicks, kicks out of a hop.");
    Toggle("Standing kicks up close", "aio_standing_kicks", true,
        "Pop up and leg-cannon from 0.8-2.8 m, and punish your attacks with one after blocking.");
    Toggle("Knockback and momentum", "aio_momentum", true,
        "A hit's damage is fixed, but the knockback flight hurts: kick when the target flees (launched further), has a " +
        "wall or a drop behind it, is hurt, or is one kick from a knockout; take spots that pin it against a wall; " +
        "sidestep instead of backing straight away from a hit about to land.");
    Toggle("Fake retreats", "aio_bait", true,
        "Back off as if retreating; if you give chase, turn and fast-kick you.");
    Toggle("Kick a chaser", "aio_chaser_kick", true,
        "Whenever they back off or flee and you run after them, they turn and meet you with a fast flying kick.");

    Header("Air combat");
    Scale("Air combat", "aio_air_combat",
        "Jump-dodging attacks, meeting your jumps in the air, punishing landings, reading your kicks, leaving your kick lane.");

    Header("Movement");
    Toggle("Bunny-hop chasing and fleeing", "aio_hop_pursuit", true,
        "Chain hops like a player to run you down (last hop kicks) or to get away.");
    Toggle("Rolls", "aio_rolls", true,
        "Dodge rolls, rolling off thrown weapons, rolling in to close a gap, rolling out of a beating.");
    Toggle("Fast get-up rolls", "aio_fast_getup", true,
        "Roll up out of a knockdown quickly, away from the nearest enemy; flip upright when knocked out of the air.");
    Toggle("Escape jumps", "aio_escape_jumps", true,
        "Leap away when hurt, surrounded or fleeing; break out of surrounds. Hit and jump: when they break off " +
        "(hit and run, a breather, falling back, regrouping) they leap and hop away instead of backing off on foot, " +
        "and strike then jump out when several of you stand around them.");
    Toggle("Crouch under high attacks", "aio_crouch", true,
        "Duck under spin kicks, haymakers and other high strikes, then sweep you.");
    Toggle("Wall kicks and wall flips", "aio_wall_moves", true,
        "Pinned against a wall: run up it and kick off at you, or flip away around you.");
    Toggle("Ledge grabs and climbing", "aio_ledges", true,
        "Catch ledges when falling, and climb walls to reach you on high ground with no way up.");
    Toggle("High ground and terrain", "aio_terrain", true,
        "Move to the best spot to fight: above you, in a clear kick lane, never with a drop behind them; drop-kick from above.");

    Header("Weapons and tactics");
    Scale("Weapon throwing", "aio_throwing",
        "Throwing knives and spears: openers, at fleeing or unreachable targets.");
    Toggle("Close-combat sense", "aio_combat_sense", true,
        "Strike at real openings: your back, mid-swing at someone else, a worn-out guard, a startled moment. " +
        "Pick the move that lands: nothing high at a crouch, nothing low at a jump, kick you when you're down, " +
        "hit hardest to finish, quickest to trade blows; grab a fresh guard. Kick fallen enemies while they get up. " +
        "In a group the fighter in front holds your attention while one behind strikes; allies break up chokes " +
        "and throws, and go for the downed, the guard-broken and those who can't block a blade.");
    Toggle("Weapon sense", "aio_weapon_sense", true,
        "Fight by who holds a weapon. Bare hands against a blade: stay outside its reach, strike only at openings " +
        "(a whiff, its back, busy with someone else), never roll or kick into it. Blade against bare hands: fight at " +
        "the tip. Blade against blade: long reach keeps distance, short reach rushes in. Keep or throw by what the " +
        "weapon is worth; throw when the target can't react (in the air, back turned, mid-swing); stop throwing at " +
        "someone who catches; rush in after a hit; don't leap at someone holding a blade and facing them. " +
        "Fetch the best weapon in reach, not the nearest; leave ones an enemy stands over or a teammate went for.");
    Scale("Feints", "aio_feints",
        "Fake an attack to bait your block.");
    Toggle("Group tactics", "aio_group_tactics", true,
        "Take turns attacking, flank, strike your back when you are busy.");
    Toggle("Team tactics in battles", "aio_team", true,
        "Read who is winning: as the majority spread out, finish the weak, encircle, arrive together; outnumbered call for help, pick off stragglers, hit and run, back to a wall. Targets: rescue beaten allies, finish hurt enemies.");
    Toggle("Stick together", "aio_cohesion", true,
        "Teammates fight as a group: rejoin the team when cut off, don't chase enemies away from it, advance together, " +
        "close ranks when outnumbered, cover each other's backs, hurt fighters step behind the group. " +
        "NPC allies keep with you when you fight on their team. Needs team tactics.");
    Toggle("Morale: fall back and regroup", "aio_morale", true,
        "Outnumbered, hurt and shaken fighters retreat to their allies and come back steadier.");
    Toggle("Hear allies fall", "aio_ally_cries", true,
        "An ally's death cry brings nearby NPCs to investigate.");

    Header("Personality and reading");
    Toggle("Fighting personalities", "aio_personality", true,
        "Each NPC gets a style: kicker, boxer, trickster, brawler, skirmisher or balanced (by species, or the character's " +
        "\"AI - Style\" parameter). It scales how often they kick, feint, bait, roll, hop, throw and press.");
    Toggle("Read the opponent", "aio_adapt", true,
        "Remember what you block and reverse: avoid the attacks you reverse, sweep and feint against a wall of blocks, " +
        "throw you when you keep blocking up close.");
    Toggle("Unpredictable footwork", "aio_footwork", true,
        "No straight run to time: zigzag in (above all against blades and throwers), stutter-step, angle in to the " +
        "flank, stroll then sprint; at range circle with sudden reversals, dash in and out to draw a swing, or stand " +
        "open to invite one. Any swing that misses them gets punished. Personality picks the mix.");
    Toggle("Change the rhythm", "aio_tempo", true,
        "No steady beat to time against: sudden stillness, then a burst; the same special move rarely twice in a row.");
    Toggle("Catch their breath", "aio_breath", true,
        "A hurt fighter not under pressure backs off for a few seconds while its temporary damage heals.");

    Header("Debug");
    Toggle("Show AI state above NPCs", "debug_show_ai_state", false,
        "Stock debug text: state, goal and sub-goal of every NPC.");
    Toggle("Show stealth icons", "debug_draw_stealth_debug", false,
        "Stock debug icons: what each NPC heard or saw.");
    Toggle("Log AI events", "aio_debug_log", false,
        "Writes [AIO] lines (jump kicks, rolls, dodges...) to logfile.txt.");

    ImGui_Spacing();
    Note("Sliders scale each character's own \"AI -\" editor values.");
}

void ResetAITab() {
    SetConfigValueBool("aio_master", true);
    SetConfigValueFloat("aio_jump_kicks", 1.0f);
    SetConfigValueBool("aio_standing_kicks", true);
    SetConfigValueBool("aio_bait", true);
    SetConfigValueFloat("aio_air_combat", 1.0f);
    SetConfigValueBool("aio_hop_pursuit", true);
    SetConfigValueBool("aio_rolls", true);
    SetConfigValueBool("aio_fast_getup", true);
    SetConfigValueBool("aio_escape_jumps", true);
    SetConfigValueBool("aio_chaser_kick", true);
    SetConfigValueFloat("aio_throwing", 1.0f);
    SetConfigValueFloat("aio_feints", 1.0f);
    SetConfigValueBool("aio_group_tactics", true);
    SetConfigValueBool("aio_ally_cries", true);
    SetConfigValueBool("aio_terrain", true);
    SetConfigValueBool("aio_crouch", true);
    SetConfigValueBool("aio_wall_moves", true);
    SetConfigValueBool("aio_ledges", true);
    SetConfigValueBool("aio_team", true);
    SetConfigValueBool("aio_morale", true);
    SetConfigValueBool("aio_cohesion", true);
    SetConfigValueBool("aio_personality", true);
    SetConfigValueBool("aio_weapon_sense", true);
    SetConfigValueBool("aio_combat_sense", true);
    SetConfigValueBool("aio_momentum", true);
    SetConfigValueBool("aio_footwork", true);
    SetConfigValueBool("aio_adapt", true);
    SetConfigValueBool("aio_tempo", true);
    SetConfigValueBool("aio_breath", true);
    SetConfigValueBool("debug_show_ai_state", false);
    SetConfigValueBool("debug_draw_stealth_debug", false);
    SetConfigValueBool("aio_debug_log", false);
}
