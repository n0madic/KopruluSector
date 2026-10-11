# Function atlas index by research domain

This index maps each domain of the [completion ledger](../completion-ledger.md) to the atlas documents that specify its code. Package documents list every function in address order. Use this page to find the subsystem; use the package page for exact behavior. The [atlas README](README.md) defines entry format, statuses and evidence level.

Package scope summaries:

| Package | Subject |
|---|---|
| [P01](p01.md) | PKWARE DCL compression, crash reporter, hand-written terrain/GRP blitters, ApproxDistance |
| [P02](p02.md) | GRP blitters, DAT requirement gates, cost/supply checks, weapon explosions and Bullet hit machinery |
| [P03](p03.md) | Bullet pool, terrain scroll cache, Flingy movement, profiles, cheats, image effect callbacks |
| [P04](p04.md) | Image records, IScript interpreter (69 opcodes), image pools and persistence, placement grids |
| [P05](p05.md) | Save compression, order pool, unit counters, credits/epilog markup, sprites, fog ghosts |
| [P06](p06.md) | Thingy pools, tooltip, unit spatial index, construction/repair/training orders, hangars |
| [P07](p07.md) | Cloak, player colors, target acquisition, attack legality, flee, attack/patrol/hold orders, CTF |
| [P08](p08.md) | Spatial queries, harvesting orders, CUnit pool and creation |
| [P09](p09.md) | Unit save/load, ownership transfer, reference cleanup, critter/doodad/trap/powerup orders, order queue |
| [P10](p10.md) | Movement state machine (36 states), collision escape, Protoss build/warp/shield/merge orders |
| [P11](p11.md) | Psi power, spell effects, hallucinations, unit save, target selection, spell AI, cloaking, addons |
| [P12](p12.md) | Order dispatchers, per-frame unit step, transports, status effects, research/upgrade, Zerg morphs |
| [P13](p13.md) | Zerg orders and spells, cursor, accelerators, window procedure, message pump |
| [P14](p14.md) | Nearest-unit sweeps, forces and alliances, game start (seeding) and teardown, DAT loader, music choice |
| [P15](p15.md) | Game loop and frame driver, tileset loading, game-screen layer, unit sound feedback, victory templates |
| [P16](p16.md) | In-game input procedures, accelerator commands and bindings, melee AI start |
| [P17](p17.md) | Briefing action table and portraits; Battle.net/lobby (excluded) |
| [P18](p18.md) | Campaign selection menu, Create Game / Play Custom; lobby/connection (excluded) |
| [P19](p19.md) | Glue state machine (26 states), message boxes, expansion menu, load-game screen |
| [P20](p20.md) | Character selection, main menu, CD checks, map list, game creation |
| [P21](p21.md) | Campaign ready rooms (briefing start), score screen, popups |
| [P22](p22.md) | Program start-up and main loop, archives, spawn check, control groups, GRP/health bars, remaps, vision templates |
| [P23](p23.md) | CHK loading and section tables, map archives, on-screen text lines, fog overlay |
| [P24](p24.md) | Fog/vision callbacks, minimap, modeless dialogs, music streaming |
| [P25](p25.md) | Game-command execution handlers and command senders, selection commands |
| [P26](p26.md) | Turn and command pipeline, Storm session glue, game RNG, replay recorder, score result, alliances |
| [P27](p27.md) | Player-slot setup, sync records, elapsed clock and pause, in-game menus, command-card actions, building placement, placement search |
| [P28](p28.md) | Right-click orders, cinematics, replays, flyer repulsion, region accessors, AI banker |
| [P29](p29.md) | AI towns, worker/building AI, spending consumers |
| [P30](p30.md) | Local path geometry, terrain contours, pathing save block |
| [P31](p31.md) | Computer-AI orders, path records, region generation |
| [P32](p32.md) | Unit path construction, local A* search, AIScript engine (113 opcodes, launch whitelist) |
| [P33](p33.md) | AI placement filters, resource areas, expansion and attack-route choice, saved games |
| [P34](p34.md) | Save-game files, scrolling, parallax stars, group moves, targeted orders, DirectSound setup |
| [P35](p35.md) | Sound engine, options, command-card button sets (250), console panels |
| [P36](p36.md) | Status panels, leaderboard display, portraits and transmissions, resources |
| [P37](p37.md) | Resource panel, wireframes, AI unit strength, regional military AI |
| [P38](p38.md) | Computer-AI regions (captains), attack planning, reactions to attacks |
| [P39](p39.md) | Guard AI, attack staging search, in-game text box, receding creep, tips, title |
| [P40](p40.md) | Trigger actions (60) and conditions (24), selectors, unit-count caches, leaderboard ranking |
| [P41](p41.md) | Campaign progression, selection and targeting rules, trigger engine, end-of-game dialogs |
| [P42](p42.md) | Creep engine, CHK chunk reader, TBL loaders, dialog/control framework |
| [P43](p43.md) | Dialog painting and presentation, UI timers, list/combo/edit boxes, buttons |
| [P44](p44.md) | Scroll bars and sliders, blend tables, screen layers and refresh, fatal errors, palette fades |
| [P45](p45.md) | Bitmap-font text engine, exit list, registry options, DirectDraw start-up |
| [S01](s01.md) | Storm.dll imports: MPQ, memory, strings, PCX, SDraw, SDlg, SCode blitter compiler, SVid, registry, SNet local provider |
| [C01](c01.md) | Visual C++ runtime as used by the game (sorting, rounding, `rand`) |
| [A01](a01.md) | Functions found by a late pass over code reached only through pointers |
| [E01](e01.md) | Functions that had no entry of their own: those the emulation oracle executed (spell-target callbacks, transport and nuke orders, region-state handlers, UI procedures) and, from the final pass, status-panel rows, spell callbacks and exit callbacks such as the registry saver `0x4be880` |
| [V01](v01.md) | Verification of disputed claims |
| [F01](f01-corrections.md) | Cross-package corrections |
| [F02](f02-native-reconciliation.md) | Reconciliation of earlier specifications |
| [F03](f03-final-fixes.md) | Final fix pass: all follow-up items closed, gap entries, central glossary |

Domain map:

| Ledger domain | Primary atlas documents |
|---|---|
| R01 Version, archives, locale, missing inputs | P22, P20, S01 (archive priorities, locale matching), [resource manifest](../evidence/resource-manifest.json) |
| R02 DAT/TBL schemas, defaults, overrides | [DAT schemas](../dat-schemas.md), P14 (loader), P42 (TBL loaders), P23 (CHK overrides), P09/P05 (save-version remaps) |
| R03 CHK sections, ownership, forces, normalization | P23, P42, P14 (FORC), P27 (slot setup) |
| R04 Tileset and terrain flags | P15 (tileset loading), P03 (terrain cache), P23, P01 (terrain blitters) |
| R05 GRP/LO geometry and draw order | P04, P05, P22, P02 |
| R06 Palettes, remaps, draw functions, effects | P44 (blend tables, fades), P22 (remaps), P03 (image callbacks), S01 (SCode), P01/P02 (blitters) |
| R07 Fonts, text, UI resources, portraits, WAV/SMK | P45, P42, P43, P36, P17, P35, S01 (SVid, PCX) |
| R08 Frame phases and command application | P15 (game loop), P26 (turns, RNG enable), P25 (commands), P12 (unit step) |
| R09 RNG lifecycle and consumers | P26, P14 (seeding), C01 (CRT `rand` used only for presentation) |
| R10 Pools and lifecycles | P03, P04, P05, P06, P08, P09, P29 |
| R11 IScript | P04 (interpreter), P02 (weapon explosions) |
| R12 Flingy movement | P03, P10 |
| R13 Regions, paths, collision | P10, P28 (repulsion), P30, P31, P32, P33 |
| R14 Fog, LOS, detection | P23, P24, P05 (fog ghosts), P07 (cloak) |
| R15 Orders and command rules | P06–P13, P25, P27, P34, P41 |
| R16 Targeting and retaliation | P07, P11, P38 |
| R17 Attack timing and projectiles | P02, P03, P07 |
| R18 Damage and scoring | P02, P07, P26 (score result), P21 (score screen) |
| R19 Abilities and status effects | P11, P12, P13 |
| R20 Carriers, Reavers, mines, nukes, transports, Bunker, Nydus | P06, P12, P11 |
| R21 Harvesting | P08, P29 |
| R22 Construction, repair, addons, lift/land, placement | P06, P10, P11, P12, P27, P33 |
| R23 Training, supply, requirements, research | P02, P06, P12, P05 |
| R24 Larva, morph, creep, warp, power | P13, P12, P11, P42, P39 |
| R25 AIScript VM | P32 |
| R26 Towns and resource areas | P29, P33 |
| R27 AI spending and production | P28 (banker), P29, P38 |
| R28 AI regions, strength, attacks | P37, P38, P39, P33 |
| R29 Guards and AI reactions | P39, P38, P11 |
| R30 Triggers | P40, P41 |
| R31 Briefings | P17, P21 |
| R32 Campaign progression and outcomes | P41, P26, P21, P19 |
| R33 Doors, traps, rescue, powerups, campaign-only state | P09, P40, P12 |
| R34 Buttons, selection, control groups, hotkeys | P35, P41, P22, P16, P25 |
| R35 Sound and music | P35, P24, P15 |
| R36 Mutable state and save/load | P34, P33, P05, P09, P03, P04, P30, P31, P37, P41 |
| R37 Campaign dependency closure | All packages: every function has a status; excluded code is network/Battle.net/telemetry/crash-only |
