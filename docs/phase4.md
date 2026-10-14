# Phase 4 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 4 (combat and abilities). Started 2026-10-14, after the user approved the design (inline design document, 2026-10-14):
- the state of combat lives in the native game `KpGame` (fixed arrays in the original's record numbering, no allocation in a step); the save format stays version 1 and grows with it;
- the step stays strictly sequential in the original's order (unit timers, orders, images that fire, the bullet step, thingies);
- every sub-stage is checked against its oracles before the next one: per-step emulator scenarios (level C) and original-code probes (level A) for the branches a scenario cannot reach;
- unported branches keep failing with their addresses; M1 (`terran/terran01`) needs come first.

**Design correction (2026-10-14).** The design expected enemies "of a computer player without an AI script, which keeps only guard records". A computer player always brings the regional AI: its AI region records, the regional scheduler, the reaction of its units to attacks and their AI orders (Guard, AIGuard, ComputerAI, ComputerReturn). A second human slot is disabled in single player and a neutral slot is allied with everyone, so neutral targets (player 11) never fight back. The scriptless computer's AI is therefore part of item 4.2; the AI scripts, towns, workers, military groups and production stay in plan phase 7.

## Work items

| # | Item | Status | Where |
|---|---|---|---|
| 4.0 | Oracle: the Bullet pool and the computer AI in the state model, scenario units with hit points, shields and energy, the damage table of original code | done: the dump categories (`bullets`, `bullet`, `ai_region`, `ai_reach`, `ai_player`, the guard records' death counts), the spec fields, the damage table (`WeaponDamage 0x41dfc0` of 130 weapons at 228 unit types and two upgrade levels on the real DAT, equal in all 456 rows) | `tools/native_state.py`, `native/kp_core/src/core/kp_dump.c`, `converter/scenarios.py`, `tools/research/sc_emulator/state.py`, `tools/research/probe_damage_table_117.py`, `tests/lua/test_damage_table.lua` |
| 4.1 | Weapons and bullets: IScript attack hooks, weapon choice, readiness, cooldown with its jitter, the Bullet pool with its states, the miss test, the damage pipeline, splash and bounce | in progress: ranged attacks of behaviours 0–2 (fly to a point or a target, appear on the target) and 3–5 at creation, melee, the normal, radial, enemy and air splash hits, the weapon damage pipeline, the shield hit image (`0x415370` at the shield LO point of the hit direction), the target range test of `trgtrangecondjmp`, the forward offsets of `attkshiftproj` (a live DAT write the save keeps); bounce, damage over time, the Lurker line, spell effects and matrix still fail with their addresses | `kp_attack.c`, `kp_bullets.c`, `kp_combat.c` |
| 4.2 | AttackUnit, acquisition, retaliation; the Human and the Computer paths | in progress: AttackUnit (`0x41e5c0`), StayInRange (21), TurretAttack (22), the AttackMove and Patrol reactions, PlayerGuard's attacks, Hold Position's attack in place (`0x420bd0`), TowerGuard and TowerAttack (Photon Cannons wait for Protoss power, `0x430670`), the best and the random acquisition (`0x431c40`, `0x431b70`) with target ranks, the reactions of a damaged unit and its allies (`0x4203d0`); the scriptless computer: AI regions with the scheduler and state 0, the enemy strength maps, the military reachability cache, AIGuard (160), ComputerAI (156), the AI target choice (`0x41f6c0`), the reaction to attacks (`0x4aebc0`) with the help of nearby units, guard return home, the AI removal of a unit | `kp_attack.c`, `kp_combat.c`, `kp_ai.c`, `kp_order_dispatch.c` |
| 4.3 | Death: the Death order and animation, remnants, supply, kill and death counters, score, burning buildings | in progress: hit point damage (`0x41e150`), death statistics, kill counts, the Death animation of a visible unit and its sprite while it plays, the teardown of a Terran building, burning Terran buildings, hit point regeneration | `kp_combat.c`, `kp_remove.c`, `kp_order_dispatch.c`, `kp_timers.c` |
| 4.4 | Timers and statuses: energy, the status timers and overlays, cloak upkeep | not started | |
| 4.5 | Mechanics (Medic Heal and Spider Mines first) | in progress: Medic Heal (orders 175–179, the patrolling Medic, the nearest-unit sweep `0x442d90`), Burrow and Unburrow (commands 0x2c, 0x2d, orders 116 and 118, `RequestUnburrow 0x43c560`, the burrowed sprite `0x43d940`/`0x43da00` with `ChangeSpriteID 0x415f90`, the burrowed cloak and `Decloak 0x41d360`), Stim Packs (command 0x36, `0x438af0`, the computer's use), Siege Mode (commands 0x26, 0x25, orders 98, 99, WatchTarget 100, the tank's death over a building `0x435a70`) over `ChangeUnitType 0x424ad0` (with `0x424bd0`, `0x424a00`, `0x426750`), Spider Mines (PlaceMine 132, the mine's order 20 with its target search `0x431e90` and intercept course `0x40c3f0`); a unit pushed out from under another (`FindLegalMovePosition 0x42ce60` with `0x42d2a0`, movement state 13); a computer's Medic, transports, Bunkers, Carriers, Reavers, the Nuke and the rest are next | `kp_medic.c`, `kp_burrow.c`, `kp_siege.c`, `kp_morph.c`, `kp_mines.c`, `kp_collision.c`, `kp_movement.c`, `kp_spatial.c`, `kp_combat.c`, `kp_detection.c`, `kp_commands.c` |
| 4.6 | SC and BW spells by campaign frequency | in progress: OrderCastSpell (`0x434460` with `0x435020`, `0x435530`), the target legality (`0x434130`, `0x435380`, `0x435320`, `0x434e70`), IScript `castspell` (`0x41f4a0`), Ensnare (`0x43dc90`, `0x43e0e0`, `0x43dee0`), Parasite, Spawn Broodlings (`0x43e120`, ported, not yet observed); status overlays (`0x422ae0`, `0x422e70`) | `kp_spells.c`, `kp_status.c`, `kp_bullets.c` |
| 4.7 | Heroes and campaign units | not started | |
| 4.8 | Acceptance: the damage table, every scenario at every step, `bench_combat` on the Fold7, combat in the viewer, ASan/UBSan | not started | |

## Oracle

**Bullets in the state model.** The original's 100 Bullet records (`0x53db90`, `0x70` bytes each) appear in two categories of their own, so the goldens of the earlier categories stay valid: `bullets` (the allocated count `0x53db8c`, the active and free lists with their tails, `0x540750`/`0x540758`) and `bullet` (each active record, in list order). Koprulu keeps a bullet's flingy part in the unit field arrays at record `KP_UNITS + b` (owner, state at `+0x4d`, signal, order point and target included), so the flingy code moves units and bullets alike, and the rest of the record in `KpBullets`.

**The computer AI in the state model.** The emulator dumps each player's AI region array (`0x6b1a74[p]`, the blobs `ai_regions<p>`); the categories are `ai_region` (the records that differ from their start), `ai_reach` (`0x6b1aa0` with its time and source) and `ai_player` (the PlayerAi fields at `0x67d068` that Koprulu models). In a campaign game `SetupVictoryConditions 0x448530` marks every player's AI as campaign AI (flag `0x20`).

**The damage table.** `tools/research/probe_damage_table_117.py` maps the executable into Unicorn, replaces its writable data section with a state dump's (the DAT columns and upgrade levels as the game loaded them) and runs `WeaponDamage 0x41dfc0` for each weapon's base damage at a target CUnit of each type (player 0, incomplete, hit points 0x40000000, full shields), with the owner's upgrades at levels 0 and 3; the shield hit image is skipped. `tests/lua/test_damage_table.lua` retypes one unit of a native game the same way.

**Fields left out.** From the third step on, the unit's minimap blink counter (`+0x8a`, set to 20 when the local player's unit is attacked) is left out on both sides: the minimap's drawing counts it down (`DrawLocalMinimapUnits 0x46e490`), as the drawing refreshes the image bounds.

**Order dispatch and commands.** The dispatcher switches on the unit's current order at each stage (`0x439420`): an every-step handler that ends its order (MedicHealToIdle) lets the next order's periodic handler run in the same step. The game RNG runs while the turn's commands execute (`ExecuteGameTurn 0x4768a0`): a command that stops an attack plays an animation whose `waitrand` draws from it.

**Emulator runs.** A scenario run deletes the state dumps of an earlier, longer run before it dumps its own steps.

**Local selections in the emulator's log.** `ui.jsonl` records the interface's selections at `0x49cab0`, but not the game's own removals of a unit that dies or leaves sight (`0x4268d0`, `0x4a7b80` through `0x49cf80`): Koprulu makes those itself, and replaying them again would reselect twice.

**Scenario units.** A spec unit may give `"hp"`, `"shields"` and `"energy"` in percent (the CHK UNIT record's valid properties 0x2, 0x4, 0x8). `"researched": {"slot": [tech, ...]}` makes techs available and researched for a slot (the CHK sections PTEC and PTEx).

**Query frames.** The original's rectangle queries stack their results (`0x5d88f8`) so that a query nested in the walk of another keeps its results; the AI's reactions nest several. `kp_query_rect` keeps the same frames in the game (`KpSpatial.frames`) instead of a unit array on the C stack per level.

## Scenarios

| Scenario | What it runs | Status |
|---|---|---|
| `marine_depot` | a Marine attacks a neutral Supply Depot at 10% hit points: Gauss Rifle bullets, the burning building, its destruction and its rubble | equal at all 261 steps |
| `wraith_duel` | a Wraith attacks a neutral Wraith: Gemini Missiles flying to their point | equal at all 241 steps |
| `marines_zerglings` | three Marines attack neutral Zerglings (neutral units never turn on their attacker) | equal at all 301 steps |
| `goliath_wraith` | a Goliath's turret fires Hellfire Missiles in pairs with alternating spin at a neutral Wraith; the body keeps it in range (StayInRange) | equal at all 261 steps |
| `firebat_lings` | a Firebat's Flame Thrower splashes a cluster of neutral Zerglings | equal at all 241 steps |
| `vulture_marines` | a Vulture's Fragmentation Grenades against neutral Marines | equal at all 261 steps |
| `zealot_marine` | a Zealot's Psi Blades in melee | equal at all 241 steps |
| `hydra_ling` | a Hydralisk's Needle Spines | equal at all 221 steps |
| `tank_marines` | a Siege Tank's turret (TurretAttack) with the Arclite Cannon | equal at all 261 steps |
| `zealots_marines` | two Zealots against three Marines of a computer player: Gauss Rifle shots hit the Zealots' shields (the shield hit image), Psi Blades in melee, PlayerGuard's attack on the remembered attacker | equal at all 301 steps |
| `hold_turret` | three Marines and a Wraith hold position next to a computer's Missile Turret and two Zerglings: TowerGuard and TowerAttack, the attack in place, the Wraith laser's `trgtrangecondjmp`, the local selection after a selected Marine dies | equal at all 261 steps |
| `medic_heal` | a Medic heals two Marines at 40% hit points on her own: MedicIdle, MedicHeal, MedicHealToIdle, the heal target search | equal at all 201 steps |
| `right_click_attack` | two Marines and a Goliath right-click a computer's Supply Depot: the attack orders of the units and of the Goliath's turret (`0x47f7b3`), the computer's Zerglings come to help | equal at all 241 steps |
| `burrow_zerg` | a Zergling, a Hydralisk and a Drone burrow and unburrow (U): the burrow commands and orders, the burrowed sprite with its selection circles, the burrowed cloak and decloak | equal at all 221 steps |
| `stim_marines` | three Marines and a Firebat stim (T) and attack a computer's Marines that also have Stim Packs | equal at all 261 steps |
| `siege_tank` | a Siege Tank sieges (O), fires at a computer's Zerglings and unsieges (O): the siege orders and the type change of the tank and its turret | equal at all 301 steps |
| `spider_mines` | a Vulture lays a mine, attacks a computer's Zerglings and retreats over it: the mine arms, burrows, rises, chases and bursts; the Vulture is pushed out from under the new mine | equal at all 401 steps |
| `queen_spells` | a Queen casts Ensnare and Parasite at a computer's Marines | equal at all 301 steps |
| `computer_zerglings` | three Marines against three Zerglings of a computer player without a script: the AI regions, the computer's guards (AIGuard, ComputerAI), their reaction to the attack, both sides' deaths | equal at all 301 steps |

All scenarios of phase 3 were dumped again with the new blobs; with the new categories every one of the 35 equals the original at every step, and the 64 initial states too.
