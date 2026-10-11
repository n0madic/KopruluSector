# Full-game emulation oracle (StarCraft: Brood War 1.17.0.1)

This oracle runs the complete original game: StarCraft.exe (SHA-256 `94dea448…6ed0`) and storm.dll execute as x86 code in Unicorn 2.1.4. Only the Windows API boundary is emulated in Python. It boots the program through its CRT entry point to the main menu. It starts campaign missions through the game's own start path, runs thousands of frames with no player input, and records per-frame state digests, sound requests, rendered frames and function coverage.

Code: `tools/research/sc_emulator/` (Python package, run from `tools/research`). Outputs: `~/koprulu-private/emu/` (private). Summary: [evidence/full-game-emulation.json](evidence/full-game-emulation.json).

## Results

| Milestone | Result |
|---|---|
| 1 Boot data | Reached. The CRT entry point runs WinMain: archives, spawn check, `local.dll`, window, DirectDraw, sound, title screen and image loading. The glue loop enters the main menu (`0x45a8b0`) at 9.4 s of virtual time, and the run stops after 3,000 empty message polls (13.0 s virtual, about 20 s wall). 496 atlas functions executed. Storm served 1,074 file requests: `font.gid/clh/ccd`, encrypted fonts, `network.tbl`, `mapdata.dat/tbl`, `sfxdata.dat/tbl`, `images.dat/tbl`, `iscript.bin`, all 876 image GRPs and the LO* overlays, templates, glue PCX/BIN files and SMK animations. `units.dat`, `flingy.dat`, `sprites.dat`, `weapons.dat`, `upgrades.dat`, `techdata.dat`, `orders.dat`, `portdata.dat`, `stat_txt.tbl`, `aiscript.bin`, the tileset files and the CHK load at game start, through the same code, and appear in every mission's `files.txt`. |
| 2 Start a mission | Reached for every campaign mission (path below). `terran1` loads `campaign\terran\terran01\staredit\scenario.chk` ("T1) Wasteland") through the game's CHK loader, creates 46 units, runs the briefing and starts the triggers and AI scripts. The tutorial (`terran0`) works the same way. |
| 3 Run frames | Reached: 3,000 frames per mission with no input. Each frame's digest goes to `trace.jsonl` and coverage is recorded. Two independent runs of Wasteland, one with extra observer hooks, gave byte-identical 3,000-frame traces and sound logs (SHA-1 `e6d82714…`). |
| 4 All campaigns | Reached: all 64 playable rows of the six campaign tables, 3,000 frames each (192,000 frames). There were no faults, no guest exceptions, no message boxes and no unimplemented APIs, and no mission ended within 3,000 frames. Each mission took 500–700 s wall with 11 in parallel (≈0.18 s per frame). |
| 5 Presentation | Reached. PNGs of the game bitmap and of the presented 32-bit surface are saved at frames 1, 500, 1000, 2000 and 3000 of every mission, and all show the original renderer's output (terrain, units, console, minimap, resources). 9,427 sound and music requests are logged across the missions, including briefing and trigger speech (`PlayStreamedSoundFile` paths) and music track changes. |

### Second pass (H02): mission endings, input, long runs, sound

| Item | Result |
|---|---|
| Mission end and campaign progression | Achieved for every campaign mission (all 64 rows), through the game's own victory path (details below). Six chains, one per campaign, played each campaign from its first mission to its end in a single process each: 57 missions in total. The seven hidden rows that no chain reaches (45, 53–57, 63) were each started individually and ended the same way. |
| Defeat path | Achieved. In 1.17, the cheat "game over man" (bit `0x40`) makes the local player lose. Contrary to the follow-up's premise it is a defeat cheat, as the run shows (details below). |
| Synthetic mouse input | Achieved. A drag box and a right-click go through the game's own selection and right-click order code (details below). The third pass extends this to the command card, placement, hotkeys and targeted orders. |
| Long runs | Achieved. Four missions (original Terran and Protoss, Brood War Zerg and Protoss) ran 20,000 frames twice each. All eight runs ended with no faults, and every pair is byte-identical in trace and sound log. |
| Sound positions | Achieved. Buffers now advance on the virtual clock (details below). |

**How a mission ends.** After 300 frames with no input, the driver types the cheat into the game's own message line: `VK_RETURN`, the text as `WM_CHAR`, `VK_RETURN`.
- Enter reaches `ToggleTextbox` `0x4b3bd0` through the window procedure. `ProcessCheatText` recognises "there is no cow level" (bit `0x80`).
- The trigger engine commits victory (`0x5349e0[local]` = 3). The frame is recorded as `result_frame`.
- `ShowVictoryDialog` `0x4bcff0` opens `wmission.bin`. Its button hotkey V leads to `RequestGameExit(6)`.
- The score screen (glue 14–19, `0x461940`) is left with its Ok hotkey O.
- `AdvanceCampaignAfterGame` `0x4bb790`(1) then picks the next row:
  - a cinematic (game mode 10, skipped with Escape, the player's own skip key);
  - the next ready room;
  - or the end of the campaign: campaign menu glue 6/22, or epilog mode 9 after the last original Protoss and expansion Zerg missions.

Every profile save is captured at `SaveCharacterFile` `0x40d830`. Progress is six u32 at record `+0x28` (highest mission reached: original Zerg, Terran, Protoss, then expansion Zerg, Terran, Protoss). The character is "Anonymous", network.tbl's default name, because the warp start bypasses the ID screen. `CreateCharacterFile` writes it into the overlay.

| Chain | Start | Missions played (mapdata index) | Victory frames | End | Final saved progress | Wall s |
|---|---|---|---|---|---|---:|
| orig-terran | `terran0` | 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 | 312–343 | campaign end (mode 4, glue 6) | [0, 12, 0, 0, 0, 0] | 1008 |
| orig-zerg | `zerg1` | 12 → 13 → 14 → 15 → 16 → 17 → 18 → 19 → 20 → 21 | 312–343 | campaign end (mode 4, glue 6) | [22, 0, 0, 0, 0, 0] | 786 |
| orig-protoss | `protoss1` | 22 → 23 → 24 → 25 → 26 → 27 → 28 → 29 → 30 → 31 | 312–343 | campaign end (mode 9, glue 19) | [0, 0, 32, 0, 0, 0] | 803 |
| exp-protoss | `xprotoss1` | 32 → 33 → 34 → 35 → 36 → 37 → 38 → 39 | 312–340 | campaign end (mode 4, glue 22) | [0, 0, 0, 0, 0, 40] | 551 |
| exp-terran | `xterran1` | 40 → 41 → 42 → 43 → 44 → 46 → 47 → 48 | 312–340 | campaign end (mode 4, glue 22) | [0, 0, 0, 0, 49, 0] | 648 |
| exp-zerg | `xzerg1` | 49 → 50 → 51 → 52 → 58 → 59 → 60 → 61 → 62 → 64 | 312–343 | campaign end (mode 9, glue 15) | [0, 0, 0, 65, 0, 0] | 687 |

- The victory frame is the frame at which the local player's committed trigger result became 3; it falls 12–43 frames after the cheat line is submitted.
- The expansion Terran and Zerg chains follow the maps' own `SetNextScenario` choices: `Terran05a.scx`, `Terran06.scx`, `Zerg04a.scx`, `Zerg05.scx` and `Zerg10.scx`, resolved by `0x4bba20`. They therefore skip the hidden branches 45, 53–57 and 63, as a player's game would.

Single missions (start, end, next row chosen by the game; "result" 3 = victory, 2 = defeat):

| Run | Start | Mission | Cheat | Result | Result frame | Path | Next mission | SetNextScenario |
|---|---|---|---|---:|---:|---|---|---|
| defeat-terran02 | `terran1` | 2 | `game over man` | 2 | 339 | defeat_dialog, game_exit(7), score_screen, advance_campaign(0) | None | — |
| single-m45 | `xterran5b` | 45 | `there is no cow level` | 3 | 343 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 46 | Terran06.scx |
| single-m53 | `xzerg4b` | 53 | `there is no cow level` | 3 | 312 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 58 | Zerg05.scx |
| single-m54 | `xzerg4c` | 54 | `there is no cow level` | 3 | 312 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 58 | Zerg05.scx |
| single-m55 | `xzerg4d` | 55 | `there is no cow level` | 3 | 312 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 58 | Zerg05.scx |
| single-m56 | `xzerg4e` | 56 | `there is no cow level` | 3 | 312 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 58 | Zerg05.scx |
| single-m57 | `xzerg4f` | 57 | `there is no cow level` | 3 | 312 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 58 | Zerg05.scx |
| single-m63 | `xzerg9b` | 63 | `there is no cow level` | 3 | 325 | victory_dialog, game_exit(6), score_screen, advance_campaign(1) | 64 | Zerg10.scx |

**Defeat path.** In the defeat run, "game over man" commits result 2 at frame 339.
- `ShowDefeatDialog` `0x4bcf80` opens `lmission.bin`, which closes itself after 20 s and calls `RequestGameExit(7)`.
- The defeat score screen follows (glue 16), then `AdvanceCampaignAfterGame`(0), which sets mode 5.
- `RunMainLoop` restarts the same mission directly, without a ready room; game mode 3 is seen again.

**Mouse input** (`sc_emulator/input_demo.py`, `emu/input_demo/input_demo.json`, Wasteland):
- At frame 123, a left-button drag from (30, 60) to (230, 190), sent as WM_MOUSEMOVE/WM_LBUTTONDOWN/moves/WM_LBUTTONUP, selected 10 units in the selection array `0x6a6070`: 5 SCVs (type 7) and 5 Marines (type 0); an earlier version of this text called the SCVs Vultures.
- A right-click at (420, 250) gave every selected unit order 6 (Move), each with its own group-move target. Four frames later, for example: unit 1695 at (56, 117) → target (335, 238); unit 1693 → (362, 272); unit 1696 → (367, 225).
- 200 frames later all had arrived and returned to order 3, for example unit 1695 at (335, 238).
- `unit_rows` records order, position and move target of the selected units every 4 frames.

**Long runs** (20,000 frames, `emu/long/`):

| Mission | Warp | Frames (run a / b) | Byte-identical | Trace SHA-1 | Units at frame 20000 | Wall s (a / b) |
|---|---|---|---|---|---:|---|
| protoss-m25 | `protoss4` | 20000 / 20000 | yes | `6206b8acbe24` | 200 | 2918 / 2492 |
| terran-m10 | `terran9` | 20000 / 20000 | yes | `dd5cabe40007` | 248 | 3100 / 3099 |
| xprotoss-m33 | `xprotoss2` | 20000 / 20000 | yes | `6444dae4606e` | 169 | 3206 / 2772 |
| xzerg-m50 | `xzerg2` | 20000 / 20000 | yes | `f67a088a9154` | 218 | 3307 / 3319 |

- No guest exception, fault or unimplemented API occurred, and no mission ended within 20,000 frames.
- Up to frame 3000, three of these traces equal the first-pass 3,000-frame traces field by field, except for the virtual-time field `t`.
- The fourth, `xzerg2`, differs from frame 1 in the RNG and the unit hash. The seed is CRT `time()` at game start, and here that call landed one virtual second later (`946684822` against `946684821`). The cause is the changed event sequence before the game: the Start key is now sent without an extra WM_CHAR, and sound cursors move.
- Traces are therefore reproducible for a given emulator configuration. Changing the configuration can move the seed, as in a real game started a second later.

**Sound.** Buffer play cursors now advance on the virtual clock.
- Position = start position + elapsed virtual time × `nAvgBytesPerSec`. Looping buffers wrap; one-shot buffers stop at their end and then report "not playing". The write cursor leads by 10 ms.
- The original code that reads these values:
  - Storm's DDA stream pump (S01 `SFileDdaBeginEx`), which refills when 0x4000 bytes are free;
  - `AcquireSoundChannel` `0x49db60`, `PlayUiClickSound` `0x49e460`, `PlaySoundInternal` `0x49e4e0` (channel exclusivity) and `StartCachedSound` `0x49e620`, which all call `GetStatus`.
- All of these manage sound channels, buffers and the sound cache only. Wasteland's 600-frame digest is identical field by field with and without moving cursors, apart from `t`.

### Third pass (H03): scripted play through window messages

All of the following go through synthetic window messages only: `sc_emulator/inputs.py`, `play.py` and `scenarios.py`; results in `emu/h03/<scenario>/result.json` and the evidence file.
- Mouse actions and keystrokes are posted to the game window. The game's window procedure, input procedures, selection code, command card, accelerators, placement cursor and order code act on them.
- No game memory is written. Cheats are used only where stated (H03b's helper cheats); the H07 victories use none.
- Positions come from reading the unit pool, the view origin `0x543218`/`0x54321c`, the minimap geometry (`0x671a8c`, `0x671fb8`…) and the command-card dialog `0x6aaae8`, whose children 1–9 are the buttons.

Three fixes to the Windows boundary made this possible:
- `LoadAcceleratorsA` reads the real `RT_ACCELERATOR` resources, and `TranslateAcceleratorA` matches them and sends `WM_COMMAND`. Control groups, location hotkeys and the in-game menus are accelerator commands in `Local.dll`.
- Accelerator handles are unique; an earlier counter reused freed handles.
- `GetKeyState` follows the key messages retrieved from the queue.

| Item | Result |
|---|---|
| 1 Command card | Achieved, by mouse and by hotkey. Details below. |
| 2 Building placement | Achieved for all three races and for an add-on. Details below. |
| 3 Hotkeys and control groups | Achieved (Wasteland). Details below. |
| 4 Targeted orders | Achieved. Details below. |
| 5 Real (non-cheat) victory and defeat | All three races achieved with no cheat at all: Terran Wasteland (H03), Zerg Z1 and Protoss P1 First Strike (H07, below). The maps' own triggers fired in every case. H03b's earlier Zerg (Z1) and Protoss (Dark Origin) victories used non-ending helper cheats. Real defeats occurred in four H03 runs and in the H07 P6 attempts. |
| 6 PrintScreen | Achieved. Details below. |

**1. Command card.**
- *Training.* Command Center: SCV by button 1 (mouse), then by hotkey S. Each time the queue gained type 7 and minerals dropped by 50 (56→6, 54→12, the second including income). Barracks: Marine by button 1, then hotkey M ×4; the queue filled to `[0,0,0,0,0]` and minerals fell 50 per Marine (286→44).
- *Orders on a Marine in mission T8* (Big Push base).
  - Stop (button 2): order 3.
  - Hold Position (button 5): order 107.
  - Patrol (P + map click): order 152, target 120 px east.
  - Stop (S): order 3.
- *Upgrades and research.*
  - Engineering Bay button 1 started an upgrade: order 76, −100 minerals / −92 gas net of income.
  - Machine Shop button 1 also started an upgrade (order 76, −100/−100).
  - Machine Shop button 2 started a research (order 75, −100/−100).
- *Spell.* Science Vessel Defensive Matrix (hotkey D, then a click on the Marine). The vessel took order 141 targeting the Marine; energy went 200 → 100.

**2. Placement.**
- *Terran Barracks* (Wasteland).
  - Build menu B, then Barracks B, puts the cursor into placement mode (`0x6751f4` = 1, button set 230).
  - Over the Command Center the validator's cell array (`0x675188`, 96 bytes) has nonzero (invalid) cells, and a click there does nothing. Over open ground every cell is 0.
  - A click there gave the SCV order 30 (PlaceBuilding) at the tile. The building was paid for when construction began and completed at frame 3743.
  - An earlier attempt in the same mission lost the unfinished Barracks to a Zerg attack. Defending the site with an attack-move fixed it.
- *Protoss Pylon* (Into the Flames).
  - The probe's build menu hotkey B works; the Pylon was chosen by command-card button 2, because its hotkey E did not start placement in this build.
  - The first spot was valid; the probe took order 31, and the Pylon appeared at frame 189 and completed at frame 700.
- *Zerg Spawning Pool* (Among the Ruins).
  - The spot east of the Hatchery was invalid (nonzero cells; the click did nothing). The spot west was valid.
  - The drone took order 70 and became the Pool (type 142, order 45) at frame 323; it completed at frame 1488.
- *Add-on: Machine Shop* (T8).
  - Factory hotkey C entered placement mode, and a click east of the Factory started it (factory order 74, −50/−50). It completed at frame 1688 at (1184, 2144).
  - The cell array was 0 for both probe points, so add-on placement feedback was not distinguishable this way.

**3. Hotkeys and selection** (Wasteland).
- A drag box selected 10 units; Ctrl+1 assigned them.
- Click then Shift+click gave the selection [1689] → [1689, 1690].
- Key 1 recalled the 10 units. A double tap of 1 centred the view on the group: (672, 1248) → (0, 0).
- Shift+F2 stored the view, a minimap click moved it, and F2 restored it: (0, 0) → (1152, 64) → (0, 0).
- Ctrl+click on a Marine selected the 5 Marines on screen.
- Tab and the other F-keys were not exercised; 1.17 has no subgroup Tab.

**4. Targeted orders.**
- *Attack-move:* A + minimap click on Raynor's position gave all 10 units order 14 with individual targets near (1014, 1031). Raynor joined at frame 481.
- *Attack a specific unit:* A + click on a visible Supply Depot gave the Marine order 10 with the depot as target unit. A target hidden by fog cannot be selected; clicking it becomes an attack-move (order 14), which is how the first two attempts ended.
- *Rally points:* right-click with a building selected stored the point at CUnit `+0xf8/+0xfa`. Command Center → (1248, 1872), the mineral field (with the unit at `+0xfc`); Barracks → (1064, 1624).
- *Minimap:* a left click moved the view (1824, 2432) → (1664, 1280); a right click gave the Marine order 6 to (2368, 2624).
- *Spell:* as in item 1.

**5. Real victory and defeat.** The Wasteland script:
1. The 10 starting units, attack-moving by the minimap, reach Raynor; he joins at frame 481.
2. SCVs are sent to the minerals (orders 85), the CC trains SCVs, and the Barracks is placed and defended.
3. The Barracks trains five Marines.

The map's own victory trigger then fired at **frame 5586**: the local player commands at least 10 Marines, a Barracks and Raynor, and trigger result 3 opens the victory dialog.

In H03, without helper cheats, no Zerg or Protoss victory was achieved; H03b used helper cheats, and H07 (below) won Z1 and P1 without any cheat. The objectives were read from the maps' TRIG sections:
- Most Zerg and Protoss missions require destroying a defended base, delivering a carried object through defended terrain, or setting several switches.
- The "bring a hero" missions that were tried failed:
  - Zerg Z5 *The Amerigo*: Kerrigan to (256–352, 2080–2176). She stalled at (735–960, 860–1012), then died at frame 2601 → defeat at frame 2630.
  - Protoss mission 63 *Dark Origin*: cloaked Zeratul to COP04. He was killed on the way at frame 1804 → defeat at frame 1820.
  - Protoss P6, find Zeratul: Tassadar with his escort by attack-move died at frame 2417 → defeat at 2420; Tassadar alone by move died at frame 1121 → defeat at 1156.
  - Protoss P5: Tassadar and Zealots to the installation entrance. The group stalled at (1229–1284, ≈1690) for 6,000 frames; the run was stopped and left no result file.
- These four defeats are real ones: the maps' own hero-death defeat triggers (result 2). Mission T8 also ended in a real defeat at frame 5787 when the idle base was overrun.

**H03b: Zerg and Protoss victories with helper cheats.** Helper cheats that do not end a mission were typed into the message line (Enter, text, Enter); each is recorded with the cheat word `0x5432fc` before and after. Victories still had to come from the maps' own triggers, met by real unit actions.

- *Zerg, Z1 Among the Ruins* (`zerg1`):
  - Helper cheats: "power overwhelming" (cheat word `0x200` → `0x204`), "show me the money", "operation cwal" (→ `0x206`).
  - A Drone built the Spawning Pool (complete at frame 255). Another built the Hydralisk Den, found by its action value 135 in button set 238 at position 7 and placed north-west of the Hatchery (complete at frame 756).
  - The 12 Zerglings (Ctrl+click, Ctrl+1) attack-moved through the minimap to each remaining building of player slot 5 in turn; they started with 14 buildings.
  - The victory trigger fired at **frame 5035** (result 3). Its owners are all players, and its conditions are: "Command: player 6 (slot 5) commands at most 0 buildings", "Command: current player commands at least 1 Spawning Pool (142)", "Command: current player commands at least 1 Hydralisk Den (135)".
- *Protoss, BW secret mission 63 Dark Origin* (pending-mission start):
  - Helper cheat: "power overwhelming" (`0x0` → `0x4`).
  - Zeratul was clicked and sent by a minimap right-click (order 6) to COP04 (2592–2688, 2816–2880).
  - The victory trigger fired at **frame 2011** (result 3). Its owners are all players, and its condition is "Bring: current player brings at least 1 Zeratul (75) to COP04". Without the cheat, he had been killed on the way (frame 1804).
- *Not achieved: Protoss P6* (find Zeratul with invincible Tassadar). No trigger fired within the 12,000-frame budget. H07 gives the reason from the track: Tassadar stood inside the closed "door10" from frame 2,779.

**H04: the remaining input paths** (`scenarios.terran_misc` in T9 New Gettysburg, `scenarios.zerg_burrow` in Z1; results in `emu/h03/terran-misc` and `emu/h03/zerg-burrow`).

- *Tab:* with 6 Marines selected, Tab left the selection unchanged. 1.17 has no subgroup cycling.
- *F10 and Escape:* F10 opened the game menu. The pause flag `0x67517c` became 1, and no frame advanced in 1,000 ms of virtual time. Escape closed the menu: flag 0, and the game resumed.
- *F3 and F4:* each jumps to a saved screen position. F3 moved the view (832, 608) → (736, 512). F4 left it at (736, 512): every saved position starts as the map's start position (`0x445810`).
- *Alt-click:* the 6 Marines were selected, then the Command Center. A WM_SYSKEYDOWN Alt plus a click on one Marine then selected the automatic recent group holding all 6 (`SelectRecentGroupOfUnit` `0x464030`).
- *Training cancel:* two Marines were queued (250 → 150 minerals).
  - Cancel button 9 removed the last one: queue `[0,0]` → `[0]`, minerals 150 → 200.
  - Escape with the Barracks selected removed the other: queue empty, minerals 200 → 250. Both are full refunds.
- *Construction cancel:* an SCV began a Supply Depot.
  - Escape while the SCV was still selected did nothing.
  - With the incomplete depot selected (button set 231), Cancel button 9 removed it and refunded 75 of its 100 minerals: 150 → 225.
- *Transport:*
  - A Marine right-clicked on the Dropship took order 92 and was loaded (status bit `0x40`) at frame 444.
  - The Dropship was put in group 3, the view centred on another Marine, and group 3 recalled. Load button 8 (button set 229 = targeting) then a click on the Marine gave the Dropship order 94; that Marine was loaded at frame 930.
  - Unload hotkey U plus a click on a point gave order 112 (unload at the point). Both Marines were set down there by frame 1081.
  - Pitfalls met on the way: a minimap left-click while a targeting order is pending issues the order at that point, and a target under the hovering Dropship picks the Dropship.
- *Burrow:* with helper cheats "show me the money" and "operation cwal", Hatchery button 3 started Burrowing research (−100/−100), which finished by frame 2344. U then burrowed all 8 selected Zerglings (status bit `0x10`), and U again unburrowed them.

**H05: the last unexercised inputs** (`scenarios.protoss_shuttle` in P5, `scenarios.zerg_h05` in Z6; Z1 does not offer the Lair).

- *Shuttle:* a Zealot right-clicked on the Shuttle took order 92 and was aboard (status `0x40`) at frame 94. Load button 8 (targeting set 229) and a click on a second Zealot gave the Shuttle order 94; that Zealot was aboard at frame 215. Unload All (button 9) at a point gave order 112, and both Zealots were set down there by frame 339.
- *Overlord:*
  - Helper cheats "show me the money" and "operation cwal". After the Spawning Pool, Hatchery button 7 started the Lair (queue `[132]`, order 45); it completed at frame 1024.
  - Lair button 4 started Ventral Sacs: −200/−200.
  - Then the same transport cycle as the Shuttle: a Drone loaded by right-click (frame 4122), a Drone by Load button 8 (order 94; loaded at frame 4317), and Unload All at a point (order 112; both set down by frame 4560).
- *Select Larva:* hotkey S with the Hatchery selected, and Hatchery button 1, each selected its Larva (type 35; button set 35).
- *Morph cancel* (explained in H05b with a memory-write hook on the unit record and code hooks; `emu/h03/h05b/writes.json`):
  - *Egg cancel.* Larva → Zergling (hotkey Z) became an egg: type 36 written by `InitializeUnitType` `0x425a70` at frame 67, −50 minerals. Egg button 9 then ran `CancelConstruction` `0x41a340` at frame 83, with ECX = the egg; minerals rose by 50, a full refund.
    - The egg then died. At frame 84, `RemoveUnitBody` → `MoveUnitToRecycleList` unlinked it from the active list into the pending-destruction list. At frame 85, `RecycleUnit` `0x4256b0` put it at the head of the free list.
    - No Larva was created or restored: the Hatchery's Larva count was 1 before the morph and 0 after the cancel.
    - The freed record was reused at frame 125. A Reaver of player slot 1 trained a Scarab (`SecondaryTrainFighter` → `TryCreateQueuedUnit` → `CreateUnit` → `InitializeUnit`), and `InitializeUnitType` wrote type 85 (Scarab), owner 1, into it. That is the "type 85" seen afterwards.
    - This confirms the atlas: P06 `0x41a340` refunds the egg's queued type at full cost and then kills the egg.
  - *Building cancel.* A Drone morphing into an Evolution Chamber (button set 233) was cancelled by button 9. `CancelConstruction` → `CancelZergBuilding` `0x43b380` → `RefundThreeQuartersUnitCost` `0x408490` ran at frame 875 and the Drone came back (type 41, order 3). Minerals rose by 56 = floor(3·75/4), the atlas's three-quarter refund; the earlier "expected 75" was wrong.
- *F1:* opened the help menu; the game paused (`0x67517c` = 1, no frames advanced). Escape closed it and the game resumed.

**H06: speed keys, screen-position keys, music and sound toggles** (`scenarios.speed_keys`, Wasteland; with a no-input baseline run of the same mission).

- *Game speed:* `+` and `-` are ASCII accelerators in Local.dll table 101 (WM_CHAR, commands 40056/40058, offline only).
  - `+` raised the speed index `0x6d3130` 4 → 5, a second `+` 5 → 6, and `-` 6 → 5, each taking effect within 6 frames through command `0x0f`.
  - The ms per frame passed in ECX to the trigger scheduler `0x4be500` at `0x444f71` followed it: 56 → 48 → 42 → 48.
  - Compared with the baseline, every frame digest is identical apart from the virtual time `t`, except frames 245–259. In those, player 1's trigger-wait field shows an active wait (remaining 0xFFFFFFFF) that the baseline does not have. So the speed changes the per-frame milliseconds the trigger timers count down, and here one wait's timing with it. Units, RNG, resources and results are unchanged over the 900 frames.
- *Screen positions:* Shift+F2/F3/F4 stored the current view in the slots at `0x526550`/`0x526554`/`0x526558`: (64, 160), (864, 352), (256, 1248). Ctrl+F2…F4 have no binding in the accelerator tables and changed nothing. F2, F3 and F4 recalled the three views exactly.
- *Music and sound toggles:*
  - Ctrl+M (command 40038, `ToggleMusicMute` `0x49ec00`) set the music volume `0x6d313c` from `0xffffffff` to 0, and a second Ctrl+M restored it.
  - Ctrl+S (40039, `ToggleSfxMute` `0x49ec40`) executed but left `0x6d3140` unchanged. Its first test found the DirectSound pointer `[0x6aa954]` = 0, because scripted-play runs are configured without a sound device, so it took the early exit.
  - H06b repeated Ctrl+S twice with a recording device (`scenarios.sfx_toggle`, `[0x6aa954]` = `0x3012fe60`).
    - The first press called `StopAllSounds` `0x49eca0`, saved the effects volume in `0x6aaa50` (50), set `0x6d3140` 50 → 0, and set the UI sound volume through `0x4c3760` to −4000.
    - The second press restored `0x6d3140` = 50 from `0x6aaa50` and set the UI volume to −602.

**Which runs had a sound device.**
- With the recording DirectSound device (`sound_device=True`): the H01 boot and 64 three-thousand-frame missions, the H02 20,000-frame runs (`mission.run_mission`), and H06b.
- Without a device (`DirectSoundCreate` fails, `[0x6aa954]` = 0, effects volume `0x6d3140` = `0xffffffff`): the H02 campaign chains, single-mission endings, defeat run and input demo (`chain.py` and `input_demo.py` use the default `Config`), and every H03–H06 scripted-play run (`scenarios.start` defaults to no device).
- In H03–H06 only the Ctrl+S observation depends on the device, and H06b covers it.
- The game's sound-request functions still run without a device; they return early, as the atlas specifies. The device cannot change simulation results; this was checked statically. None of the functions that read the device pointer `0x6aa954` calls the game RNG helpers (`0x4781d0`, `0x478200`, `0x478240`, `0x478270`, the only users of the state `0x67393c`) or CRT `rand` `0x4d14d4`. The CRT `rand` consumers that sound choice and creep share (the Yes/What choosers `0x447130`/`0x447190` and their callers `0x446120`/`0x446c30`) contain no device test, so the main thread's `rand` sequence is the same with and without a device. The sound loader thread runs only with a device, and it has its own per-thread `rand` state. Separately, the 600-frame Wasteland digest with advancing and with static device cursors was identical (H02).

**H07: victories with no cheat at all.** No cheat was typed in these runs, not even a helper cheat; the only cheat-word bit set is Ophelia (`0x200`) from the warp start. Each run posts window messages only and reads game state to decide what to click.

- *Zerg, Z1 Among the Ruins* (`scenarios.zerg_no_cheats`, `emu/h03/zerg-z1-nocheat`): **victory at frame 21,785** (result 3; the victory dialog opened at frame 21,831).
  - The trigger is the same as in H03b. Its owners are all players, and its conditions are "player 6 (slot 5) commands at most 0 buildings", "current player commands at least 1 Spawning Pool (142)" and "at least 1 Hydralisk Den (135)".
  - Economy:
    - Idle Drones are right-clicked onto the nearest mineral fields.
    - Larvae morph Overlords (when the supply margin drops below 3), Drones (up to 14), then Zerglings, and Hydralisks once there is gas.
    - Buildings: the Spawning Pool started at frame 4,166, the Extractor at 4,671 (three Drones were sent to gas), the Hydralisk Den at 6,993 and a second Hatchery at 7,701.
    - Placement clicks candidate points on rings around the Hatchery and keeps the first one the game accepts. Placement mode ending (`0x6751f4` = 0) is the acceptance signal; on a rejected click, placement mode stays on.
  - Waves:
    - Both Hatcheries rally (right-click) to a staging point 30% of the way to the enemy base, and idle units elsewhere are sent back to it.
    - When 24 idle units are staged (12 after the first wave), up to six Ctrl+clicks each select one unit type on the screen. Each is followed by A and a minimap click on the enemy building nearest the staging point.
    - There were 5 waves; the first left at frame 10,801, when slot 5 still had 15 buildings. Near the end the army was 50 Zerglings and 18 Hydralisks.
  - Input totals: 1,574 events (1,301 clicks, 145 right-clicks, 128 keys). No guest exception was raised and no unimplemented API was called.
  - Once the result was set, the game left the map for the end screens. The script's next minimap action then failed, and this is recorded as `script_stopped_after_result` rather than as an error.
  - Earlier versions of the scenario:
    - The first reached 40,221 frames without a victory: the Hydralisk Den needs 50 gas, which it had no Extractor for, and its waves were too small.
    - A drag-select version won twice at frame 31,674 with identical frame digests.
- *Protoss, P1 First Strike* (`scenarios.protoss_p1_nocheat`, `emu/h03/protoss-p1-nocheat`): **victory at frame 21,614** (result 3; the victory dialog opened at frame 21,615).
  - The trigger is owned by force 1 and has one condition: "Command: player 1 (slot 0) commands at most 0 buildings (231)".
  - This mission was chosen because its objective is the cheapest without cheats. The Zerg base of slot 0 starts with 10 buildings, 30 Zerglings, 15 Hydralisks and 8 Mutalisks. The other original Protoss missions need a hero brought through Zerg-held ground (P4, P5, P6), switches set by the map (P7, P9), or other targets destroyed (P2 a Cerebrate, P3 every enemy building, P8, P10). Dark Origin needs a cloaked hero carried past seven Missile Turrets and three Science Vessels.
  - Rescue:
    - The six Zealots and two Dragoons attack-move by minimap to Fenix (1271, 1853) and then to the base of slot 1 at (480, 2224).
    - Fenix joined by frame 1,430. The Nexus, two Pylons, Assimilator, Gateway, Forge and three Dragoons joined at frame 1,540.
  - Economy:
    - The rescued Gateway stands outside every Pylon's field: the console shows "Unpowered" and its train key does nothing. A Pylon placed next to it (frame 2,084) powered it.
    - The Nexus trains Probes up to 20, and idle Probes are sent to minerals.
    - Probes build Pylons when supply runs short, three Photon Cannons, and Gateways up to five. Placement uses the same accept-or-retry clicks as the Zerg run.
    - The mission does not offer the Cybernetics Core (the V menu has no such button), so only Zealots were trained: 43 of them, each Gateway keeping two queued.
  - Combat:
    - The Gateways rally on a staging point 10% of the way toward the Zerg base.
    - Idle units attack-move onto raiders near any building (8 times).
    - Waves leave at 20 staged units (10 after the first), selected by Ctrl+click as in the Zerg run, which leaves out Fenix, whose death loses the mission. Idle units within 800 px of an enemy building attack-move to it.
    - Wave 1 left at frame 12,238; by frame 15,064 slot 0 was down from 10 buildings to 6. Wave 2 left at 18,877, and the last building, the Extractor, fell at frame 21,614.
  - Input totals: 1,598 events (1,377 clicks, 104 right-clicks, 117 keys). No guest exception was raised and no unimplemented API was called.
  - A second run with the staging point at 30% (`emu/h03/protoss-p1-nocheat-b`) also won, at frame 25,885.
- *Protoss P6 (Into the Darkness): why H03's attempt failed, and the no-cheat attempts* (`scenarios.protoss_p6_nocheat`, `emu/h03/p6-nocheat-*`; the track of the H03 move is `emu/h03/p6-track`).
  - The victory trigger is owned by player 8, the local player (slot 7). Its single condition is "current player commands at least 1 Zeratul (75)", and its actions end with Victory.
  - Zeratul starts as player 3's (slot 2) with order 161 (RescuePassive) at (3073, 3820), so he joins the player when one of the player's units comes near.
  - The CHK triggers (TRIG/MRGN) gate him behind two doors.
    - Bringing any unit ("Men") to "open sesame!" (512–608, 2080–2144) removes the door units 205/206 at "door10" (320–512, 3232–3392) and sets switch 1.
    - Bringing one to "Free me!" (3680–3776, 3328–3392) removes them at "door15" (2560–2752, 3520–3648), which encloses Zeratul's cell, and sets switch 5.
    - Doors 1–3, 5–7 and 11 open when a unit reaches the door location itself.
  - *Exact reason for the H03 failure.* Invincible Tassadar was given a single move (order 6) to Zeratul's position. The track shows (987, 581) → (1626, 927) → (2226, 1388) → (3006, 1506) → (3300, 2094) → (2447, 2520) → (1574, 2902) → (863, 3022), and from frame 2,779 to 9,087 he stood at (452, 3281) with order 6 still set. That point is inside "door10". The door was still closed because no unit had entered "open sesame!", and the move never leads there. Even with door10 open, door15 needs "Free me!". The tracking run (same start, same move, with "power overwhelming") ended at frame 9,087 without any trigger firing.
  - *No-cheat attempts.* The route goes through the trigger locations: "marines 2" (rescues four Marines), the eastern loop the game's path takes (through "ambush trigger" (3456–3712, 1696–1920), which spawns an Infested Terran and three Hydralisks), "open sesame!", door10, the south-west rescue group, door11, then "Free me!", door15 and Zeratul.
    - Tassadar (80 hit points, 300 shields, 250 energy) attack-moves with the escort or follows it. He casts Psionic Storm (T + click) on visible clusters and, in run f, on the ground of known burrowed groups.
    - No run reached "open sesame!". Runs a and c lost the whole escort, and the script stopped them there; every other run ended in the map's Tassadar-death defeat (result 2):

    | Run | End | Position | Nearest enemy group (mission-start table) |
    |---|---|---|---|
    | a, escort without Tassadar | Zealots dead, frame 1,416 | last seen at (1490, 1280) | 3 burrowed Hydralisks (slot 6) at (1452–1515, 1263–1306) |
    | b | defeat, frame 4,859 | (1144, 1979) | Infested Terran (slot 3) at (1058, 1920). The four Marines were rescued at frame 2,376, and the escort died at the ambush junction. |
    | c, via the teleporter | escort dead, frame 1,586 (Tassadar alive) | (1132, 1002) | 3 burrowed Hydralisks at (930–1000, 1219–1261) |
    | d, via the teleporter | defeat, frame 1,404 | (2469, 1458) | An Infested Terran detonated on Tassadar |
    | e | defeat, frame 6,139 | (841, 2338) | 3 burrowed Hydralisks at (817–941, 2356–2415) and 2 Hydralisks (slot 3) at (749–860, 2186–2241), beside "open sesame!" |
    | f, pre-emptive storms | defeat, frame 1,986 | (1042, 1339) | 3 burrowed Hydralisks at (930–1000, 1219–1261) |
    | g | defeat, frame 3,666 | (2207, 1387) | 3 burrowed Zerglings at (2366–2430, 1368–1402) |
    | h, Tassadar following | defeat, frame 1,273 | (1590, 1030) | 3 burrowed Hydralisks at (1452–1515, 1263–1306) |

  - The mission-start unit table explains the outcome. Slot 6 has 99 burrowed units (order 117) in 16 groups, and slot 3 has 60 guards in 10 groups, including 7 Infested Terrans; the player has no detector.
    - An Infested Terran's 500 damage exceeds Tassadar's 380 hit points plus shields.
    - Tassadar has three storms at the start and regains one about every 2,300 frames (run b: 116 → 140 energy over 743 frames).
  - A cheat-free P6 victory would need hand-made micro for each ambush, which the scripted recipes do not provide. P6 stays not completed.

**6. PrintScreen.** VK_SNAPSHOT went through the window procedure (`SaveScreenshot`) to Storm's `SDrawCaptureScreen` (ordinal 342). The PNG encoder wrote `C:\Users\Player\Documents\Starcraft\SCScrnShot_010100_000154.png` (350,862 bytes, named from the virtual date) into the overlay. A copy is in `emu/h03/terran-wasteland/printscreen.png`.

No scripted run raised a guest exception or called an unimplemented API.

### Missions

Units are the active-list counts at frames 1 and 3000; RNG is the game RNG state at frame 3000. The complete checkpoint digests (frames 1, 1000, 2000, 3000) are in the evidence file. The mission's mapdata index is in its name (`-mNN`). The third column is the buffer `0x527120`, which `CreateCampaignMissionGame` copies into the game header as the map name. It usually holds the scenario title, but for some maps it shows "Untitled Scenario" or even a briefing WAV path (Z3). This is an observation about that buffer, not a harness error, and it was not investigated further.

| Mission | Warp | String at `0x527120` | Units f1 → f3000 | RNG f3000 | Sound requests | Wall s |
|---|---|---|---|---|---:|---:|
| terran-m01 | `terran0` | Terran Tutorial | 44 → 44 | 0x6b5df9c0 | 22 | 565 |
| terran-m02 | `terran1` | T1) Wasteland | 46 → 46 | 0x1b6c6b23 | 19 | 540 |
| terran-m03 | `terran2` | T2) Backwater Station | 94 → 94 | 0x0f1c02de | 20 | 641 |
| terran-m04 | `terran3` | T3) Desperate Alliance | 197 → 202 | 0x89d02d80 | 47 | 657 |
| terran-m05 | `terran4` | Untitled Scenario | 120 → 120 | 0x8bf085c8 | 19 | 502 |
| terran-m06 | `terran5` | T5) The Antigan Revolt | 168 → 144 | 0x81919e91 | 19 | 541 |
| terran-m07 | `terran6` | T6) New Year's Day | 218 → 221 | 0xaa8d2783 | 18 | 643 |
| terran-m08 | `terran7` | T8) The Trump Card | 210 → 197 | 0x4ab2d50f | 633 | 596 |
| terran-m09 | `terran8` | The Big Push | 276 → 277 | 0xfb2ce49f | 19 | 502 |
| terran-m10 | `terran9` | T11) New Gettysburg | 252 → 257 | 0x3ff097c8 | 46 | 636 |
| terran-m11 | `terran10` | T12) The Hammer Falls | 327 → 343 | 0x5c786ede | 21 | 538 |
| zerg-m12 | `zerg1` | Z1) Among the Ruins | 100 → 102 | 0xebb37409 | 20 | 665 |
| zerg-m13 | `zerg2` | Z2) Egression | 145 → 154 | 0x2da5c123 | 113 | 565 |
| zerg-m14 | `zerg3` | staredit\wav\Z3B00zad.wav | 130 → 133 | 0x8f45c669 | 17 | 770 |
| zerg-m15 | `zerg4` | Z4) Agent of the Swarm | 193 → 205 | 0xcac39fab | 438 | 869 |
| zerg-m16 | `zerg5` | Assimilation | 172 → 172 | 0x877398aa | 22 | 706 |
| zerg-m17 | `zerg6` | Z6) The Dark Templar | 186 → 197 | 0xef16ae27 | 26 | 776 |
| zerg-m18 | `zerg7` | Z7) The Culling | 236 → 246 | 0xc691603d | 17 | 767 |
| zerg-m19 | `zerg8` | Z8) The Dark Templar | 197 → 208 | 0x72b1441d | 30 | 672 |
| zerg-m20 | `zerg9` | Z9) The Invasion of Aiur | 372 → 380 | 0x91947812 | 17 | 822 |
| zerg-m21 | `zerg10` | Untitled Scenario | 444 → 467 | 0xbc1ae2bf | 23 | 831 |
| protoss-m22 | `protoss1` | P1) First Strike | 123 → 129 | 0x97e32e1f | 59 | 684 |
| protoss-m23 | `protoss2` | P2) Into the Flames | 142 → 149 | 0x495a5935 | 80 | 622 |
| protoss-m24 | `protoss3` | P3) Higher Ground | 199 → 213 | 0xdc012865 | 109 | 717 |
| protoss-m25 | `protoss4` | P4) The Hunt for Tassadar | 197 → 200 | 0xe82a98d0 | 17 | 581 |
| protoss-m26 | `protoss5` | P5) Choosing Sides | 255 → 284 | 0x40c940f1 | 19 | 570 |
| protoss-m27 | `protoss6` | Protoss 06 | 231 → 231 | 0xb002e52a | 19 | 469 |
| protoss-m28 | `protoss7` | P7) Homeland | 357 → 358 | 0x9c32c29f | 693 | 650 |
| protoss-m29 | `protoss8` | P8) The Trial of Tassadar | 256 → 275 | 0x40fbdf0e | 244 | 604 |
| protoss-m30 | `protoss9` | P9) Shadow Hunters | 350 → 400 | 0xe3995919 | 45 | 654 |
| protoss-m31 | `protoss10` | Untitled Scenario | 411 → 473 | 0x65169e0d | 23 | 579 |
| xprotoss-m32 | `xprotoss1` | Escape From Aiur | 288 → 297 | 0x68134c89 | 67 | 553 |
| xprotoss-m33 | `xprotoss2` | The Dunes of Shakuras | 165 → 169 | 0xe05c5e8e | 17 | 605 |
| xprotoss-m34 | `xprotoss3` | Legacy of the Xel'Naga | 172 → 179 | 0x3ec9cb12 | 114 | 529 |
| xprotoss-m35 | `xprotoss4` | Quest for Uraj | 253 → 184 | 0x89fc4941 | 169 | 517 |
| xprotoss-m36 | `xprotoss5` | The Battle of Braxis | 400 → 309 | 0xc363ff04 | 19 | 452 |
| xprotoss-m37 | `xprotoss6` | Return to Char | 607 → 628 | 0x41e77786 | 44 | 617 |
| xprotoss-m38 | `xprotoss7` | The Insurgent | 349 → 353 | 0x3690427e | 49 | 563 |
| xprotoss-m39 | `xprotoss8` | Countdown | 759 → 814 | 0xabe7120c | 20 | 603 |
| xterran-m40 | `xterran1` | First Strike | 292 → 275 | 0x90486acc | 32 | 518 |
| xterran-m41 | `xterran2` | Raid on the Dylarian Shipyards | 119 → 107 | 0xefb0a7b9 | 27 | 435 |
| xterran-m42 | `xterran3` | Ruins of Tarsonis | 357 → 369 | 0x954d2225 | 41 | 599 |
| xterran-m43 | `xterran4` | Assault on Korhal | 453 → 398 | 0x6fd78c64 | 65 | 516 |
| xterran-m44 | `xterran5` | Emperor's Fall (Ground Zero) | 666 → 433 | 0x82b90b85 | 262 | 579 |
| xterran-m45 | `xterran5b` | Emperor's Fall (Birds of War) | 462 → 466 | 0xa5eb6240 | 1248 | 581 |
| xterran-m46 | `xterran6` | Emperor's Flight | 494 → 490 | 0xbf18f0dc | 3034 | 494 |
| xterran-m47 | `xterran7` | Patriot's Blood | 336 → 329 | 0xbf58c4c0 | 152 | 491 |
| xterran-m48 | `xterran8` | To Chain the Beast | 705 → 741 | 0x86cba3a1 | 128 | 477 |
| xzerg-m49 | `xzerg1` | Untitled Scenario | 242 → 241 | 0x38a129fa | 29 | 468 |
| xzerg-m50 | `xzerg2` | Untitled Scenario | 273 → 218 | 0x2e3ccac3 | 221 | 482 |
| xzerg-m51 | `xzerg3` | The Kel-Morian Combine | 545 → 552 | 0xc548c042 | 21 | 517 |
| xzerg-m52 | `xzerg4` | The Liberation of Korhal | 526 → 483 | 0xab5472ed | 186 | 532 |
| xzerg-m53 | `xzerg4b` | The Liberation of Korhal | 528 → 486 | 0x48357dea | 42 | 533 |
| xzerg-m54 | `xzerg4c` | The Liberation of Korhal | 530 → 485 | 0x96f6b68a | 46 | 536 |
| xzerg-m55 | `xzerg4d` | The Liberation of Korhal | 532 → 489 | 0xb2990414 | 26 | 540 |
| xzerg-m56 | `xzerg4e` | The Liberation of Korhal | 534 → 493 | 0x83ecdb55 | 26 | 533 |
| xzerg-m57 | `xzerg4f` | The Liberation of Korhal | 536 → 494 | 0x6d936cde | 28 | 534 |
| xzerg-m58 | `xzerg5` | True Colors | 504 → 457 | 0x5aa8d79f | 78 | 496 |
| xzerg-m59 | `xzerg6` | Fury of the Swarm | 454 → 420 | 0xd9156907 | 33 | 563 |
| xzerg-m60 | `xzerg7` | The Drawing of the Web | 353 → 359 | 0x83c7b7c4 | 95 | 469 |
| xzerg-m61 | `xzerg8` | To Slay the Beast | 538 → 526 | 0x4768b2d3 | 125 | 542 |
| xzerg-m62 | `xzerg9` | The Reckoning | 279 → 269 | 0x1d88e3d0 | 17 | 432 |
| xzerg-m63 | `xzerg9b` | Untitled Scenario | 166 → 161 | 0x77d13d7b | 35 | 413 |
| xzerg-m64 | `xzerg10` | Omega | 673 → 704 | 0x0a436fbd | 17 | 452 |

### Coverage compared with the atlas

The aggregation was rerun on 2026-10-11 after the H07 no-cheat runs, against the atlas as it was then. It covers the boot, the 64 three-thousand-frame missions, the campaign chains and single-mission endings, the defeat run, the input demonstration, the 20,000-frame runs, and the H03–H07 scenarios. Together they executed **3475** of the 6081 function entries known to the atlas index:

| Status | Executed | Atlas total |
|---|---:|---:|
| specified | 2284 | 3590 |
| covered | 872 | 1019 |
| library | 295 | 854 |
| excluded | 24 | 618 |
| no atlas entry | 0 | — |

- **New in the third pass (H03).** The scripted play (H03–H07) executed 311 functions that no earlier run reached: 272 specified, 37 covered and 2 library. None of them is `excluded`, and none lacks an atlas entry.
- **Executed only by the H07 no-cheat runs (25).** These are 22 specified and 3 covered functions, none `excluded` and none without an atlas entry. They are listed in the evidence under `coverage.executed_only_by_no_cheat_runs_h07`. Among them:
  - Psionic Storm (`UseTechCasePsionicStorm` `0x47c8c0`, `BulletStateDamageOverTime` `0x409a30`);
  - placement previews and validity (`CreatePlacementPreviewImages` `0x40ebd0`, `CanPlaceCurrent` `0x47db60`, `AddGeyserGhosts` `0x47d190`);
  - selection removal (`ExecSelectRemove` `0x473170`, `RemoveUnitFromSelectionRow` `0x49c870`);
  - power loss (`UnlinkPowerProvider` `0x430610`).
- **New in the second pass.** Mission endings, input and long runs executed 394 functions that the boot and 3,000-frame missions had not reached: 241 specified, 67 covered and 86 library. None of them is `excluded`, and none lacks an atlas entry.
- **Executed functions whose atlas status is `excluded` (24):** `0x4021a0` DynInit_MainThreadId, `0x4021f0` DynInit_OfferedFileList, `0x402210` DynInit_ActiveTransferList, `0x402270` DynInit_TextContextList, `0x402290` DynInit_NetProviderList, `0x4022b0` DynInit_JoinGameList, `0x402330` DynInit_ModemDeviceList, `0x402350` DynInit_ModemPhoneBook, `0x402390` DynInit_LobbyNameList, `0x4023b0` DynInit_ChatCommandQueue, `0x4023d0` DynInit_LeagueConnectedEvent, `0x402400` DynInit_EnumGameList, `0x4025b0` DynInit_ErrorLogLock, `0x402ab0` SetCrashCallback, `0x402c90` CrashInitThunk, `0x402ca0` SetCrashAppDirectory, `0x404120` NameMinidumpThread, `0x404870` MinidumpThreadProc, `0x4049e0` InitCrashSubsystem, `0x4175e0` GetSpriteVisibilityChecksum, `0x44fbf0` GetProfileKeyList, `0x467570` RestrictProcessAccess, `0x470150` SendQueuedServerChatCommand, `0x4cc1a0` InitCrashReporter.
  - Most of these run at start-up regardless of mode: the CRT's C++ static initializers, crash-reporter set-up and its watchdog thread, and the process-hardening step, whose security calls fail under HLE.
  - The in-game ones run every frame or turn in single player. The atlas says this for `0x4175e0` and `0x470150` and argues they have no offline effect.
  - `0x45e380` SetupModemProvider, listed here by the first aggregation, is no longer `excluded` in the current atlas.
- **Executed functions without any atlas entry or status:** 0. The first aggregation had found 45; the current atlas has entries for all of them.
- **Executed code outside every atlas function body.** 83 groups of executed blocks start beyond the Ghidra size of their containing entry.
  - Almost all are continuation blocks of that function: epilogues, fall-through after a call or branch, short-jump targets.
  - None is reported as a likely unlisted function. Starts that the atlas text already describes, such as `0x4e14a9`, the tail of `_alloc_osfhnd` attached to funclet `0x4e14a0` (C01, F03), are now marked `documented_in_atlas_text` instead. `0x4e14a9` is not a missing function.
  - The full list is in `emu/coverage_union.json`.

Executed storm.dll code is recorded too (block maps). The atlas covers only Storm's exports, in S01.

## Architecture

| Module | Role |
|---|---|
| `pe.py` | Maps PE images at their preferred base, applies base relocations, binds imports. |
| `machine.py` | The process: Unicorn CPU, memory layout, GDT/TEB/PEB, handles, cooperative threads, the HLE stub dispatcher, guest calls by continuation, deterministic slicing. |
| `seh.py` | Structured exception dispatch for `RaiseException` (and, on request, CPU faults); `RtlUnwind`. |
| `hle/` | Windows API implementations (`kernel32`, `user32`, `gdi32`, `misc` for ADVAPI32/VERSION/SHELL32/IMM32/WINMM/WINTRUST/CRYPT32/PSAPI/TAPI32, `ddraw`, `dsound`). |
| `vfs.py` | Guest file system: read-only game directory plus an in-memory write overlay. |
| `coverage.py`, `native/coverage.c` | Native (C) Unicorn block hook: coverage maps and deterministic time slices. |
| `process.py` | Builds the process (configuration, images, PEB, main thread, storm.dll `DllMain`). |
| `session.py`, `mission.py`, `campaigns.py`, `batch.py`, `aggregate.py` | Boot run, one-mission driver, campaign enumeration, parallel batch, evidence aggregation. |
| `game.py`, `render.py`, `debug.py` | Game addresses and the frame digest, PNG capture, call tracing. |

### What is native

All of the following run as original x86 code. HLE never implements StarCraft or Storm logic.

- StarCraft.exe, including its statically linked Visual C++ 12 runtime (CRT start-up, heap wrappers, `rand`, locale tables).
- storm.dll at `0x15000000`: SFile/MPQ (archive opening, hash lookup, decryption, PKWARE/Huffman/ADPCM decompression, its background I/O thread), SMem, SStr, SBmp, SDraw (8-bit back buffer converted to the 32-bit primary surface), SCode (runtime-generated blitters, executed from guest heap), SDlg, SVid, SNet with the hidden local provider id 0 from `standard.snp`, SReg and SErr.
- Dynamically loaded game DLLs, loaded from the game directory by `LoadLibraryA`: `local.dll` (localization resources), `standard.snp` (network providers; single player uses its null transport) and `smackw32.dll` (Smacker decoder for glue animations, portraits and videos). Each is relocated if needed, its imports are bound, and its `DllMain` runs.

Imports of StarCraft.exe from Storm (143 by ordinal, 2 by mangled C++ name) resolve to storm.dll exports. Imports from Windows DLLs, and `GetProcAddress` lookups in them, resolve to 16-byte stubs in the HLE stub region. Each stub is `jmp $`, so an accidental execution is harmless. A code hook limited to that region runs the Python implementation, pops the stdcall arguments and returns to the caller.

### Memory layout and CPU state

| Range | Contents |
|---|---|
| `0x00400000` | StarCraft.exe (preferred base) |
| `0x02000000`, `0x10000000`, `0x15000000`, `0x1d000000` | local.dll, smackw32.dll, storm.dll, standard.snp |
| `0x05000000` | thread stacks (64 KiB guard below each) |
| `0x20000000` | `VirtualAlloc` and file mappings (64 KiB granularity) |
| `0x30000000` | heap arena (`HeapAlloc`, `LocalAlloc`) |
| `0x7E000000` | HLE stubs |
| `0x7E100000` | GDT, PEB, TEBs |

- A flat GDT gives 32-bit code and data descriptors (DS, ES, SS and GS = `0x10`). Without a GDT, Unicorn falls back to 16-bit stack addressing.
- Every thread has its own FS descriptor whose base is its TEB. Each TEB holds SEH chain `−1`, stack base and limit, self pointer, client ids, PEB pointer, `LastErrorValue` (`+0x34`) and 64 TLS slots (`+0xE10`). The PEB holds the image base, process heap and OS version 5.1.2600.
- New threads start with x87 control word `0x27F` and MXCSR `0x1F80`, as on Windows. Unicorn's reset value 0 unmasks every FPU exception. Before this was fixed, the CRT math library raised `STATUS_FLOAT_INEXACT_RESULT` (`0xC000008F`) inside a running mission.
- Allocators are deterministic. The heap uses 16-byte-aligned bump allocation with exact-size LIFO reuse and remembers block sizes for `HeapSize`/`HeapReAlloc`. Regions and handles come from counters.

### Threads, waits and time (determinism)

- Threads are cooperative. `CreateThread` makes a real guest thread with its own context, stack and TEB.
- A thread runs until it blocks in an HLE wait: `WaitForSingleObject`/`WaitForMultipleObjects`, `Sleep`, `GetMessageA`, `MsgWaitForMultipleObjects`, or a contended critical section. The next ready thread in creation order then runs, and `Sleep(0)` yields.
- When every thread is blocked, the virtual clock jumps to the earliest timeout or timer. When no timeout or timer exists, the run reports a deadlock.
- Threads seen in a mission:
  - the game's crash-watchdog thread (`0x404870`, waits forever);
  - Storm's SFile I/O thread (`0x15017970`), which serves the asynchronous MPQ reads of every archive opened with flag 2;
  - Storm's SNet pump thread (`0x15022780`), created by each `SNetInitializeProvider`. It waits with no timeout because there are no remote players;
  - the game's sound loader thread (`0x49e090` through the CRT wrapper `0x4d376f`, when a sound device exists).

  Thread attach/detach notifications are not sent to DLLs.
- The virtual clock starts at 2000‑01‑01 00:00:00 UTC (`GetSystemTimeAsFileTime`, CRT `time()`), with `GetTickCount` = 3,600,000 ms. It advances only in four ways:
  - `Sleep`/wait timeouts;
  - 1 ms for every `PeekMessageA` that finds no message;
  - 50 µs for every `GetTickCount`/`timeGetTime`/`QueryPerformanceCounter` call, so loops that spin on the clock (palette fades) terminate;
  - jumps to the next due timer when everything is idle.

  Virtual time therefore depends only on the guest's own call sequence.
- Host control returns at deterministic points: the native block hook stops emulation after a fixed number of executed translation blocks (4 million), and at thread switches.
  - Unicorn's wall-clock timeout was abandoned. A timeout that lands while a stub hook rewrites EIP can resume at the stub and run the API twice. In practice this re-ran `ReadFile` on Storm's I/O thread and corrupted decompression.
  - Python code hooks that stop the run are not refired on resume (`Machine.add_hook`).
- Two runs of the same mission produce byte-identical traces and sound logs: checked on 300-frame and 3,000-frame Wasteland runs.

### Structured exceptions

`RaiseException` builds an `EXCEPTION_RECORD` and a `CONTEXT` on the guest stack and calls the FS:[0] handlers by continuation. A handler result of 0 reloads the registers from the CONTEXT; 1 continues the search. `RtlUnwind` calls the intervening handlers with `EXCEPTION_UNWINDING`, unlinks them and discards HLE frames inside the unwound stack range. The VC++ language handlers jump into the `__except` block themselves.

CPU faults (unmapped memory, interrupts) are reported as crashes and stop the run rather than being dispatched. The CRT's top-level `__except` would otherwise turn every crash into a silent exit. `dispatch_hardware_fault` exists for experiments. No mission raised a software exception after the FPU fix; see "Exceptions" in the evidence.

### Windows boundary, devices and configuration

- **Files.** `C:\StarCraft\` is the game directory `~/koprulu-private/full/app`, matched case-insensitively and read-only. Writes go to an in-memory overlay that starts empty for every run: character files, saved data, `CSV`/error logs and the user directory `C:\Users\Player\Documents\Starcraft`. Overlapped reads complete synchronously and signal their event.
- **CD archive.** `FindCdArchive` tries `BroodWar.mpq`, then `StarCraft.mpq`, in the install directory. `rez\glucmpgn.bin` and the original campaign maps exist only in `StarCraft.mpq`. Original-campaign runs therefore hide `BroodWar.mpq` (configuration `cd="original"`: "the original StarCraft CD is in the drive"). Expansion runs keep both archives (`cd="expansion"`).
- **Windows DLL names.** `ddraw.dll` and `dsound.dll` resolve to the HLE even though the game folder holds third-party wrappers (dgVoodoo) under those names. Those wrappers are not part of the game.
- **storm.dll `DllMain`** is called with a non-NULL `lpvReserved`, as Windows does for DLLs loaded at process start. With NULL (dynamic loading) storm.dll deliberately regenerates its MPQ crypt table from a different seed, so no archive file can be found.
- **Registry.** It starts with three values under `HKCU\Software\Blizzard Entertainment\Starcraft`:
  - `intro` = 0 and `introX` = 0: the intro videos were already watched;
  - `tip` = 0: the start-of-game tips dialog, which waits for a click, is off.

  Everything else keeps the game's defaults. For example speed 4 and all sound options on. Guest writes are kept for the run.
- **Window manager.**
  - Windows are host records; messages go through a real queue, and `SendMessage`/`DispatchMessage` call guest window procedures by continuation.
  - `CreateWindowExA` sends `WM_NCCREATE` and `WM_CREATE`. A visible top-level window then gets `WM_SHOWWINDOW`, `WM_ACTIVATEAPP(1)`, `WM_NCACTIVATE`, `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_SIZE` and `WM_MOVE`. The application is therefore active (`0x66fd24` = 1): an inactive single-player window would skip frames.
  - Timers fire on the virtual clock. The screen is 640×480.
  - The key state follows the queue, as on Windows: `GetKeyState` changes when `WM_KEYDOWN`/`WM_KEYUP` are retrieved.
  - `TranslateMessage` adds `WM_CHAR` for printable keys and Enter.
  - `LoadAcceleratorsA` reads the real `RT_ACCELERATOR` resources, and `TranslateAcceleratorA` matches them and sends `WM_COMMAND`, as Windows does.
- **DirectDraw.**
  - COM objects have guest vtables whose entries are HLE stubs.
  - `SetDisplayMode` accepts 640×480×32. The primary surface is guest memory at 32 bpp, and `Lock` returns it with pitch 2560.
  - Storm's `SDrawUnlockSurface` converts the game's 8-bit frame through its palette into it. Each primary `Unlock` is a presented frame.
- **DirectSound.** Recording device by default:
  - `CreateSoundBuffer` allocates guest memory for the requested size and format;
  - play cursors advance with the virtual clock at the format's byte rate: looping buffers wrap, and one-shot buffers stop at their end;
  - `GetStatus` and `GetCurrentPosition` report that state;
  - `Play`, `Stop`, `SetVolume` and `SetPan` are logged.

  No audio is mixed.

  Configuration `sound_device=False` makes `DirectSoundCreate` fail with `DSERR_NODRIVER` instead.
- **Command line.** `"C:\StarCraft\StarCraft.exe"`, plus mission tokens for missions (below).

### HLE function list

The table is generated from the implementations' docstrings (`python -m sc_emulator apis`). "Args" is the number of 32-bit stack arguments popped (stdcall); COM methods count `this`. Interface methods not listed fail as unimplemented. Calling any unimplemented import stops the run and names it.

#### ADVAPI32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `AddAccessAllowedAce` | 4 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `AddAccessDeniedAce` | 4 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `AllocateAndInitializeSid` | 11 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `CheckTokenMembership` | 3 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `FreeSid` | 1 | NULL. |
| `GetTokenInformation` | 5 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `GetUserNameA` | 2 | "Player". |
| `InitializeAcl` | 3 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `OpenProcessToken` | 3 | Security APIs fail (FALSE); the process-access hardening step is then skipped. |
| `RegCloseKey` | 1 | Forgets the handle. |
| `RegCreateKeyExA` | 9 | Creates/opens a key. |
| `RegDeleteValueA` | 2 | Deletes a stored value. |
| `RegEnumKeyExA` | 8 | Enumerates direct subkeys in sorted order. |
| `RegFlushKey` | 1 | 0. |
| `RegOpenKeyExA` | 5 | Opens a key that exists in the in-memory registry; ERROR_FILE_NOT_FOUND otherwise. |
| `RegQueryInfoKeyA` | 12 | Counts of subkeys and values. |
| `RegQueryValueExA` | 6 | Reads a stored value (ERROR_MORE_DATA when the buffer is short). |
| `RegSetValueExA` | 6 | Stores the value in memory. |

#### CRYPT32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `CertCloseStore` | 2 | Certificate APIs fail (FALSE/NULL). |
| `CertFindCertificateInStore` | 6 | Certificate APIs fail (FALSE/NULL). |
| `CertFreeCertificateContext` | 1 | Certificate APIs fail (FALSE/NULL). |
| `CertGetNameStringW` | 6 | Certificate APIs fail (FALSE/NULL). |
| `CryptMsgClose` | 1 | Certificate APIs fail (FALSE/NULL). |
| `CryptMsgGetParam` | 5 | Certificate APIs fail (FALSE/NULL). |
| `CryptQueryObject` | 11 | Certificate APIs fail (FALSE/NULL). |

#### DDRAW.DLL

| Function | Args | Semantics |
|---|---:|---|
| `DirectDrawCreate` | 3 | Creates the IDirectDraw object (any GUID, including the emulation-only GUID 2). |
| `DirectDrawEnumerateA` | 2 | Enumerates nothing (DD_OK). |
| `IDirectDraw::AddRef` | 1 | Increments the object's reference count. |
| `IDirectDraw::CreateClipper` | 4 | Clipper object (no clipping is performed). |
| `IDirectDraw::CreatePalette` | 5 | Palette object initialised from the 256 entries. |
| `IDirectDraw::CreateSurface` | 4 | DDSCAPS_PRIMARYSURFACE -> 640x480 surface in the display depth; otherwise the requested width/height (8 bpp unless a pixel format is given). Pixels are zeroed guest memory. |
| `IDirectDraw::GetCaps` | 3 | Zeroed caps (sizes preserved). |
| `IDirectDraw::GetDisplayMode` | 2 | Current mode as DDSURFACEDESC. |
| `IDirectDraw::GetVerticalBlankStatus` | 2 | Always inside the vertical blank (TRUE). |
| `IDirectDraw::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectDraw::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectDraw::RestoreDisplayMode` | 1 | DD_OK. |
| `IDirectDraw::SetCooperativeLevel` | 3 | Records the window and flags; DD_OK. |
| `IDirectDraw::SetDisplayMode` | 4 | Accepts any mode and records it. |
| `IDirectDraw::WaitForVerticalBlank` | 3 | Returns immediately (no time is charged). |
| `IDirectDrawClipper::AddRef` | 1 | Increments the object's reference count. |
| `IDirectDrawClipper::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectDrawClipper::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectDrawClipper::SetHWnd` | 3 | Accepted. |
| `IDirectDrawPalette::AddRef` | 1 | Increments the object's reference count. |
| `IDirectDrawPalette::GetEntries` | 5 | Returns stored entries. |
| `IDirectDrawPalette::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectDrawPalette::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectDrawPalette::SetEntries` | 5 | Stores PALETTEENTRY values. |
| `IDirectDrawSurface::AddRef` | 1 | Increments the object's reference count. |
| `IDirectDrawSurface::Blt` | 6 | Colour fill (DDBLT_COLORFILL) and same-format rectangle copies are performed in guest memory. |
| `IDirectDrawSurface::BltFast` | 6 | Same-format rectangle copy. |
| `IDirectDrawSurface::Flip` | 3 | Counted; no buffer exchange (the game uses no flipping chain). |
| `IDirectDrawSurface::GetAttachedSurface` | 3 | DDERR_NOTFOUND (no attached surfaces). |
| `IDirectDrawSurface::GetBltStatus` | 2 | DD_OK. |
| `IDirectDrawSurface::GetCaps` | 2 | Creation caps. |
| `IDirectDrawSurface::GetDC` | 2 | Opaque DC. |
| `IDirectDrawSurface::GetFlipStatus` | 2 | DD_OK. |
| `IDirectDrawSurface::GetPalette` | 2 | Attached palette or DDERR_NOPALETTEATTACHED. |
| `IDirectDrawSurface::GetPixelFormat` | 2 | 8-bit palettised or 32-bit RGB. |
| `IDirectDrawSurface::GetSurfaceDesc` | 2 | Size, pitch and pixel format. |
| `IDirectDrawSurface::IsLost` | 1 | Never lost. |
| `IDirectDrawSurface::Lock` | 5 | Returns the guest pixel pointer (offset by the rectangle) and pitch. |
| `IDirectDrawSurface::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectDrawSurface::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectDrawSurface::ReleaseDC` | 2 | DD_OK. |
| `IDirectDrawSurface::Restore` | 1 | DD_OK. |
| `IDirectDrawSurface::SetClipper` | 2 | Accepted. |
| `IDirectDrawSurface::SetColorKey` | 3 | Accepted. |
| `IDirectDrawSurface::SetPalette` | 2 | Attaches the palette. |
| `IDirectDrawSurface::Unlock` | 2 | Unlocking the primary surface counts as a presented frame (observers are notified). |

#### DSOUND.DLL

| Function | Args | Semantics |
|---|---:|---|
| `DirectSoundCreate` | 3 | DSERR_NODRIVER unless a recording device is configured. |
| `DirectSoundEnumerateA` | 2 | Enumerates nothing. |
| `IDirectSound::AddRef` | 1 | Increments the object's reference count. |
| `IDirectSound::CreateSoundBuffer` | 4 | Buffer backed by guest memory of the requested size (primary buffers get 64 KiB). |
| `IDirectSound::DuplicateSoundBuffer` | 3 | Shares the source buffer's memory. |
| `IDirectSound::GetCaps` | 2 | Zeroed caps. |
| `IDirectSound::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectSound::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectSound::SetCooperativeLevel` | 3 | DS_OK. |
| `IDirectSoundBuffer::AddRef` | 1 | Increments the object's reference count. |
| `IDirectSoundBuffer::GetCaps` | 2 | DSBCAPS: flags and buffer size. |
| `IDirectSoundBuffer::GetCurrentPosition` | 3 | Play cursor from the virtual clock; the write cursor leads it by 10 ms of data. |
| `IDirectSoundBuffer::GetFormat` | 4 | The WAVEFORMATEX given at creation. |
| `IDirectSoundBuffer::GetFrequency` | 2 | Sample rate of the buffer format. |
| `IDirectSoundBuffer::GetPan` | 2 | Stored pan. |
| `IDirectSoundBuffer::GetStatus` | 2 | DSBSTATUS_PLAYING (\| DSBSTATUS_LOOPING) while the virtual play cursor runs; a one-shot buffer stops when the cursor reaches its end. |
| `IDirectSoundBuffer::GetVolume` | 2 | Stored volume. |
| `IDirectSoundBuffer::Lock` | 8 | Returns the region (wrapping at the end). |
| `IDirectSoundBuffer::Play` | 4 | Starts the virtual play cursor (DSBPLAY_LOOPING = 1 makes it wrap) and logs the play. |
| `IDirectSoundBuffer::QueryInterface` | 3 | E_NOINTERFACE (only the base interfaces exist). |
| `IDirectSoundBuffer::Release` | 1 | Decrements the reference count (objects are never freed). |
| `IDirectSoundBuffer::Restore` | 1 | DS_OK. |
| `IDirectSoundBuffer::SetCurrentPosition` | 2 | Moves the play cursor. |
| `IDirectSoundBuffer::SetFormat` | 2 | DS_OK. |
| `IDirectSoundBuffer::SetFrequency` | 2 | DS_OK. |
| `IDirectSoundBuffer::SetPan` | 2 | Stored. |
| `IDirectSoundBuffer::SetVolume` | 2 | Stored. |
| `IDirectSoundBuffer::Stop` | 1 | Freezes the play cursor; logged. |
| `IDirectSoundBuffer::Unlock` | 5 | DS_OK. |

#### GDI32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `BitBlt` | 9 | No drawing. |
| `CreateCompatibleBitmap` | 3 | Opaque 32 bpp bitmap. |
| `CreateCompatibleDC` | 1 | New memory DC handle. |
| `CreateDIBitmap` | 6 | Opaque bitmap with the header's size. |
| `CreateDIBSection` | 6 | Allocates zeroed guest pixel memory from the BITMAPINFOHEADER size. |
| `CreateFontA` | 14 | Opaque font handle. |
| `CreateFontIndirectA` | 1 | Opaque font handle. |
| `CreatePalette` | 1 | Opaque palette. |
| `CreateRectRgn` | 4 | Opaque region. |
| `DeleteDC` | 1 | TRUE. |
| `DeleteObject` | 1 | TRUE. |
| `ExtTextOutA` | 8 | No drawing. |
| `GetBitmapBits` | 3 | Zero bytes. |
| `GetCharABCWidthsA` | 4 | A=0, B=8, C=0 for every character. |
| `GetCurrentObject` | 2 | Opaque handle. |
| `GetDeviceCaps` | 2 | 640x480 display, 32 bits per pixel, 1 plane, 96 dpi, no palette device. |
| `GetDIBits` | 7 | Reports the lines; pixels are zero. |
| `GetObjectA` | 3 | BITMAP for DIB sections/bitmaps (width, height, 8 bpp); 0 otherwise. |
| `GetPaletteEntries` | 4 | Zeroed entries. |
| `GetPixel` | 3 | Black. |
| `GetRegionData` | 3 | 0 (empty). |
| `GetStockObject` | 1 | Opaque stock handle. |
| `GetSystemPaletteEntries` | 4 | Zeroed entries. |
| `GetSystemPaletteUse` | 1 | SYSPAL_STATIC. |
| `GetTextExtentExPointA` | 7 | Fixed 8-pixel advance. |
| `GetTextExtentPoint32A` | 4 | 8 pixels per character, 16 pixels high. |
| `GetTextMetricsA` | 2 | Nominal font: height 16, ascent 13, descent 3, average width 8, max width 8. |
| `MoveToEx` | 4 | No drawing. |
| `PatBlt` | 6 | No drawing. |
| `RealizePalette` | 1 | 0 entries changed. |
| `Rectangle` | 5 | No drawing. |
| `SelectObject` | 2 | Returns the previously selected object of the same kind (opaque). |
| `SelectPalette` | 3 | Opaque previous palette. |
| `SetBkColor` | 2 | Returns white. |
| `SetBkMode` | 2 | Returns OPAQUE. |
| `SetDIBitsToDevice` | 12 | No drawing; returns the line count. |
| `SetPixel` | 4 | No drawing. |
| `SetStretchBltMode` | 2 | Returns 1. |
| `SetTextAlign` | 2 | Returns 0. |
| `SetTextColor` | 2 | Returns black. |
| `StretchBlt` | 11 | No drawing. |
| `StretchDIBits` | 13 | No drawing. |
| `TextOutA` | 5 | No drawing. |

#### IMM32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `ImmAssociateContext` | 2 | NULL. |
| `ImmGetCompositionStringA` | 4 | 0 bytes. |
| `ImmGetContext` | 1 | NULL (no input method). |
| `ImmReleaseContext` | 2 | TRUE. |
| `ImmSetCompositionStringA` | 6 | FALSE. |

#### KERNEL32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `AreFileApisANSI` | 0 | TRUE. |
| `CloseHandle` | 1 | Closes files (releasing host file objects) and forgets other handles. |
| `CompareFileTime` | 2 | -1/0/1 comparison of two FILETIMEs. |
| `CompareStringW` | 6 | Ordinal comparison (NORM_IGNORECASE honoured); returns 1/2/3. |
| `CopyFileA` | 3 | Copies within the virtual file system (destination in the overlay). |
| `CopyFileW` | 3 | Wide-character CopyFileA. |
| `CreateDirectoryA` | 2 | Creates an overlay directory. |
| `CreateDirectoryW` | 2 | Wide-character CreateDirectoryA. |
| `CreateEventA` | 4 | Event object (manual/auto reset). Named events are process-global; reopening sets ERROR_ALREADY_EXISTS (so the single-instance check sees no other instance). |
| `CreateEventW` | 4 | Wide-character CreateEventA. |
| `CreateFileA` | 7 | Files under C:\StarCraft read from the game directory; writes and all other paths go to the in-memory overlay. Directories cannot be opened. |
| `CreateFileMappingA` | 6 | Read-only mapping object over an open file (contents copied into guest memory on MapViewOfFile). |
| `CreateFileW` | 7 | Wide-character CreateFileA. |
| `CreateProcessA` | 10 | Always fails: no child processes are started. |
| `CreateProcessW` | 10 | Always fails: no child processes are started. |
| `CreateThread` | 6 | Creates a cooperative guest thread (own stack, TEB and FS selector). It first runs when the creating thread blocks. CREATE_SUSPENDED (4) is honoured. |
| `CreateToolhelp32Snapshot` | 2 | Returns INVALID_HANDLE_VALUE (no process/thread/module enumeration). |
| `DecodePointer` | 1 | Identity transform. |
| `DeleteCriticalSection` | 1 | Forgets the lock record. |
| `DeleteFileA` | 1 | Deletes from the overlay (host files are only hidden). |
| `DisableThreadLibraryCalls` | 1 | Returns TRUE (thread attach notifications are never sent anyway). |
| `EncodePointer` | 1 | Identity transform (pointer encoding with a zero cookie). |
| `EnterCriticalSection` | 1 | Recursive ownership; a lock held by another thread blocks the caller (cooperative switch). |
| `EnumSystemLocalesW` | 3 | TRUE without callbacks. |
| `ExitProcess` | 1 | Ends the emulation (the run loop returns) with the given exit code. |
| `ExitThread` | 1 | Ends the calling guest thread. |
| `FileTimeToLocalFileTime` | 2 | The guest time zone is UTC: identity. |
| `FileTimeToSystemTime` | 2 | Calendar conversion (UTC). |
| `FindClose` | 1 | Releases the search handle. |
| `FindFirstFileA` | 2 | Lists the virtual directory (host directory merged with overlay), sorted by name. |
| `FindFirstFileW` | 2 | Wide-character FindFirstFileA. |
| `FindNextFileA` | 2 | Next entry or ERROR_NO_MORE_FILES. |
| `FindResourceA` | 3 | Real lookup in the image's resource directory; returns a small guest record {data address, size} used as HRSRC. |
| `FindResourceExA` | 4 | FindResourceA with a language preference. |
| `FlushFileBuffers` | 1 | TRUE. |
| `FormatMessageA` | 7 | Formats "Error <id>" (system message tables are not available). |
| `FreeEnvironmentStringsA` | 1 | Frees the block. |
| `FreeEnvironmentStringsW` | 1 | Frees the block. |
| `FreeLibrary` | 1 | No-op (images stay mapped); returns TRUE. |
| `FreeResource` | 1 | No-op. |
| `GetACP` | 0 | Code page 1252. |
| `GetCommandLineA` | 0 | The configured command line (default ``"C:\StarCraft\StarCraft.exe"``). |
| `GetCommandLineW` | 0 | Wide-character GetCommandLineA. |
| `GetCommState` | 2 | Serial ports do not exist: FALSE. |
| `GetComputerNameA` | 2 | "EMULATOR". |
| `GetConsoleCP` | 0 | 437. |
| `GetConsoleMode` | 2 | FALSE (no console). |
| `GetCPInfo` | 2 | Single-byte code page (MaxCharSize 1, default char '?', no lead bytes). |
| `GetCurrentDirectoryW` | 2 | C:\StarCraft. |
| `GetCurrentProcess` | 0 | Pseudo handle -1. |
| `GetCurrentProcessId` | 0 | Constant process id 0x1000. |
| `GetCurrentThread` | 0 | Pseudo handle -2. |
| `GetCurrentThreadId` | 0 | Deterministic thread ids 0x1000 + 4 * creation index. |
| `GetDateFormatA` | 6 | M/D/YYYY. |
| `GetDiskFreeSpaceA` | 5 | 8 sectors/cluster, 512 bytes/sector, 1 Mi free of 4 Mi clusters (4 GiB free). |
| `GetDriveTypeA` | 1 | C: is a fixed disk (3); every other drive does not exist (1), i.e. no CD-ROM drives. |
| `GetDriveTypeW` | 1 | Wide-character GetDriveTypeA. |
| `GetEnvironmentStrings` | 0 | Small fixed environment block (PATH, SystemRoot, TEMP, TMP, USERNAME, USERPROFILE). |
| `GetEnvironmentStringsW` | 0 | Wide-character GetEnvironmentStrings. |
| `GetExitCodeProcess` | 2 | STILL_ACTIVE. |
| `GetFileAttributesA` | 1 | DIRECTORY or ARCHIVE from the virtual file system; INVALID_FILE_ATTRIBUTES when absent. |
| `GetFileAttributesW` | 1 | Wide-character GetFileAttributesA. |
| `GetFileSize` | 2 | Size of the open file. |
| `GetFileType` | 1 | FILE_TYPE_DISK for files, FILE_TYPE_CHAR for the standard handles. |
| `GetFullPathNameA` | 4 | Normalises relative paths against the current directory C:\StarCraft. |
| `GetLastError` | 0 | TEB LastErrorValue. |
| `GetLocaleInfoW` | 4 | Fails (no locale database); callers fall back to defaults. |
| `GetLocalTime` | 1 | As GetSystemTime (UTC time zone). |
| `GetLogicalDriveStringsA` | 2 | Only C:\. |
| `GetModuleFileNameA` | 3 | Module paths: StarCraft.exe and game DLLs live in C:\StarCraft; Windows DLLs in C:\Windows\System32. |
| `GetModuleFileNameW` | 3 | Wide-character GetModuleFileNameA. |
| `GetModuleHandleA` | 1 | NULL -> StarCraft.exe base; loaded original images -> their base; Windows DLL names -> a stable pseudo handle (unmapped address used only as a key for GetProcAddress). |
| `GetModuleHandleExW` | 3 | Flag 4 (FROM_ADDRESS) maps an address to the containing original image; otherwise as GetModuleHandleW. Writes the handle to *out. |
| `GetModuleHandleW` | 1 | Wide-character GetModuleHandleA. |
| `GetOEMCP` | 0 | Code page 437. |
| `GetOverlappedResult` | 4 | Transfers always complete synchronously: returns the recorded byte count. |
| `GetProcAddress` | 2 | Original images: their export (by name or ordinal). Pseudo (Windows) modules: an HLE stub when the function is implemented, else NULL with ERROR_PROC_NOT_FOUND (so optional imports such as FlsAlloc fall back exactly as on an older Windows). |
| `GetProcessHeap` | 0 | Constant heap handle; all heaps share one deterministic arena. |
| `GetProcessId` | 1 | 0x1000 for the current process. |
| `GetProcessTimes` | 5 | All four FILETIMEs from the virtual clock (kernel/user time = elapsed virtual time). |
| `GetProfileIntA` | 3 | Returns the default. |
| `GetStartupInfoA` | 1 | Zeroed STARTUPINFO with SW_SHOWNORMAL. |
| `GetStartupInfoW` | 1 | Zeroed STARTUPINFO with SW_SHOWNORMAL. |
| `GetStdHandle` | 1 | Fixed pseudo handles for stdin/stdout/stderr (writes are logged). |
| `GetStringTypeA` | 5 | cp1252 GetStringTypeW. |
| `GetStringTypeW` | 4 | CT_CTYPE1 classification from Unicode character properties. |
| `GetSystemInfo` | 1 | One x86 (Pentium-class, type 586) processor, 4 KiB pages, 64 KiB granularity. |
| `GetSystemTime` | 1 | Virtual calendar time. |
| `GetSystemTimeAsFileTime` | 1 | 2000-01-01 00:00:00 UTC plus the virtual clock. |
| `GetTempFileNameA` | 4 | <path>\<prefix><counter>.tmp with a deterministic counter. |
| `GetTempPathA` | 2 | C:\Users\Player\AppData\Local\Temp\ (overlay). |
| `GetTempPathW` | 2 | Wide-character GetTempPathA. |
| `GetThreadContext` | 2 | Fails; only used by the crash reporter. |
| `GetTickCount` | 0 | Virtual milliseconds (TICK_BASE_MS + clock). Every query first charges TIME_QUERY_COST_US (50 us) to the clock, so loops that spin on the clock make progress. |
| `GetTimeFormatA` | 6 | HH:MM:SS. |
| `GetTimeZoneInformation` | 1 | UTC, no daylight saving (TIME_ZONE_ID_UNKNOWN). |
| `GetUserDefaultLangID` | 0 | 0x0409 (English, United States). |
| `GetUserDefaultLCID` | 0 | 0x0409. |
| `GetVersion` | 0 | Windows XP SP3 (5.1, build 2600). |
| `GetVersionExA` | 1 | Windows XP SP3 (5.1.2600, platform NT). |
| `GetVolumeInformationA` | 8 | Volume "SYSTEM", serial 0x12345678, NTFS. |
| `GlobalMemoryStatus` | 1 | Reports 1 GiB physical / 2 GiB page file / 2 GiB virtual, all free. |
| `HeapAlloc` | 3 | Deterministic allocation (16-byte aligned, exact-size LIFO reuse); HEAP_ZERO_MEMORY honoured. |
| `HeapCreate` | 3 | Returns a new heap handle backed by the shared arena. |
| `HeapDestroy` | 1 | No-op (blocks are not tracked per heap). |
| `HeapFree` | 3 | Returns the block to its size-class free list. |
| `HeapReAlloc` | 4 | Grows/shrinks in place when the rounded block fits, else allocate + copy + free. Newly exposed bytes are zeroed with HEAP_ZERO_MEMORY. |
| `HeapSize` | 3 | Requested size of the block. |
| `InitializeCriticalSection` | 1 | Host-side lock record keyed by the CRITICAL_SECTION address; guest fields mirrored. |
| `InitializeCriticalSectionAndSpinCount` | 2 | As InitializeCriticalSection; returns TRUE. |
| `InterlockedDecrement` | 1 | Atomic by construction. |
| `InterlockedIncrement` | 1 | Atomic by construction (single CPU, cooperative threads). |
| `IsBadReadPtr` | 2 | TRUE when any byte of the range is unmapped. |
| `IsBadStringPtrA` | 2 | TRUE when the string is not readable. |
| `IsBadWritePtr` | 2 | TRUE when any byte of the range is unmapped. |
| `IsDBCSLeadByte` | 1 | FALSE (single-byte code page). |
| `IsDebuggerPresent` | 0 | FALSE. |
| `IsProcessorFeaturePresent` | 1 | TRUE for the classic features (MMX, SSE, SSE2, RDTSC, CMPXCHG8B), FALSE otherwise. |
| `IsValidCodePage` | 1 | TRUE for 1252/437/65001. |
| `IsValidLocale` | 2 | TRUE for 0x409. |
| `LCMapStringA` | 6 | cp1252 LCMapStringW. |
| `LCMapStringW` | 6 | LCMAP_UPPERCASE / LCMAP_LOWERCASE (other mappings copy). |
| `LeaveCriticalSection` | 1 | Releases one recursion level. |
| `LoadLibraryA` | 1 | Windows DLL names return pseudo handles served by HLE (this includes ddraw.dll and dsound.dll even though the game folder holds third-party wrappers of those names). Any other DLL is loaded from the game directory as an original image: mapped, relocated, imports bound, DllMain (PROCESS_ATTACH) run on the calling thread. |
| `LoadLibraryExW` | 3 | As LoadLibraryW; flags ignored. |
| `LoadLibraryW` | 1 | Wide-character LoadLibraryA. |
| `LoadModule` | 2 | Fails. |
| `LoadResource` | 2 | Returns the resource data address inside the mapped image. |
| `LocalAlloc` | 2 | Fixed memory from the heap arena (LMEM_ZEROINIT honoured). |
| `LocalFree` | 1 | Frees a LocalAlloc block; returns NULL. |
| `LockResource` | 1 | Identity (resource memory is the image itself). |
| `lstrcpyA` | 2 | strcpy. |
| `lstrcpynA` | 3 | Copies at most n-1 characters and terminates. |
| `MapViewOfFile` | 5 | Copies the requested file range into a fresh region (writes are not written back). |
| `Module32FirstW` | 2 | Enumeration over an empty snapshot: FALSE. |
| `Module32NextW` | 2 | Enumeration over an empty snapshot: FALSE. |
| `MulDiv` | 3 | (a*b)/c rounded half away from zero; -1 on division by zero. |
| `MultiByteToWideChar` | 6 | cp1252 (or the given code page) to UTF-16; n = -1 includes the terminator. |
| `OpenProcess` | 3 | Fails (no other processes exist). |
| `OpenThread` | 3 | Fails (NULL); only used by the crash reporter. |
| `OutputDebugStringA` | 1 | Logged. |
| `OutputDebugStringW` | 1 | Logged. |
| `Process32First` | 2 | Enumeration over an empty snapshot: FALSE. |
| `Process32Next` | 2 | Enumeration over an empty snapshot: FALSE. |
| `QueryPerformanceCounter` | 1 | Virtual microseconds (frequency 1 MHz); charges TIME_QUERY_COST_US like GetTickCount. |
| `QueryPerformanceFrequency` | 1 | 1,000,000. |
| `RaiseException` | 4 | Software exceptions are dispatched through the guest SEH chain (see seh.py). |
| `ReadConsoleW` | 5 | FALSE. |
| `ReadFile` | 5 | Synchronous read. With an OVERLAPPED the offset is taken from it, the transfer completes immediately and its event is signalled. |
| `ResetEvent` | 1 | Clears the event. |
| `ResumeThread` | 1 | Decrements the suspend count; at zero a suspended thread becomes ready. |
| `RtlCaptureContext` | 1 | Leaves the record zeroed (crash reporter only). |
| `RtlUnwind` | 4 | Calls the intervening SEH handlers with EXCEPTION_UNWINDING, unlinks them and returns. |
| `SetCommState` | 2 | Serial ports do not exist: FALSE. |
| `SetCommTimeouts` | 2 | Serial ports do not exist: FALSE. |
| `SetConsoleCtrlHandler` | 2 | TRUE (no console events). |
| `SetEndOfFile` | 1 | Truncates an overlay file at the current position. |
| `SetEnvironmentVariableA` | 2 | Accepted and ignored. |
| `SetErrorMode` | 1 | Returns the previous mode (0). |
| `SetEvent` | 1 | Signals the event; waiting threads become ready (they run when the caller blocks). |
| `SetFileAttributesA` | 2 | Accepted for existing paths; attributes are not stored. |
| `SetFilePointer` | 4 | FILE_BEGIN/CURRENT/END with optional high dword. |
| `SetFilePointerEx` | 5 | 64-bit SetFilePointer. |
| `SetHandleCount` | 1 | Returns n. |
| `SetLastError` | 1 | TEB LastErrorValue. |
| `SetStdHandle` | 2 | TRUE. |
| `SetThreadPriority` | 2 | No effect on the cooperative scheduler; returns TRUE. |
| `SetUnhandledExceptionFilter` | 1 | Remembers the filter; returns the previous one. |
| `SizeofResource` | 2 | Size from the HRSRC record. |
| `Sleep` | 1 | Blocks the thread for ``ms`` of virtual time (Sleep(0) yields to other ready threads). |
| `SuspendThread` | 1 | Only the bookkeeping count (threads are cooperative); returns the previous count. |
| `SystemTimeToFileTime` | 2 | Calendar conversion (UTC). |
| `TerminateProcess` | 2 | Ends the emulation with the given exit code. |
| `Thread32First` | 2 | Enumeration over an empty snapshot: FALSE. |
| `Thread32Next` | 2 | Enumeration over an empty snapshot: FALSE. |
| `TlsAlloc` | 0 | Lowest free slot index (TEB TlsSlots, 64 slots then expansion area). |
| `TlsFree` | 1 | Releases the slot. |
| `TlsGetValue` | 1 | Reads the calling thread's slot (and sets last error 0). |
| `TlsSetValue` | 2 | Writes the calling thread's slot. |
| `UnhandledExceptionFilter` | 1 | EXCEPTION_CONTINUE_SEARCH. |
| `UnmapViewOfFile` | 1 | Releases the region. |
| `VirtualAlloc` | 4 | Reserve/commit from a 64 KiB-granular region (memory is always committed and zero). Committing inside an existing reservation returns the requested address. |
| `VirtualFree` | 3 | MEM_RELEASE returns the region; MEM_DECOMMIT zeroes the pages. |
| `VirtualLock` | 2 | TRUE. |
| `VirtualQuery` | 3 | MEMORY_BASIC_INFORMATION for images, regions and stacks (committed, RWX). |
| `VirtualUnlock` | 2 | TRUE. |
| `WaitForMultipleObjects` | 4 | Cooperative WaitForMultipleObjects (any/all). |
| `WaitForSingleObject` | 2 | Cooperative wait: returns at once when signalled (auto-reset events are consumed); otherwise the thread blocks and other threads run; timeouts are measured on the virtual clock. Non-waitable handles (files) count as signalled. |
| `WideCharToMultiByte` | 8 | UTF-16 to cp1252 (unmappable characters become '?'). |
| `WriteConsoleW` | 5 | Logged and discarded. |
| `WriteFile` | 5 | Writes into the overlay; console/std handles are logged and discarded. |

#### PSAPI.DLL

| Function | Args | Semantics |
|---|---:|---|
| `GetModuleFileNameExA` | 4 | Fails (0). |

#### SHELL32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `FindExecutableA` | 3 | Fails (SE_ERR_FNF). |
| `SHCreateDirectoryExA` | 3 | Creates the overlay directory chain. |
| `ShellExecuteA` | 6 | Fails (returns SE_ERR_FNF = 2); nothing is launched. |
| `SHGetFolderPathA` | 5 | Personal = C:\Users\Player\Documents, (Local) AppData under C:\Users\Player\AppData; other folders fail (E_FAIL). |
| `SHGetPathFromIDListA` | 2 | Copies the path string. |
| `SHGetSpecialFolderLocation` | 3 | Returns a PIDL that is really the folder path string (only SHGetPathFromIDListA reads it). |

#### TAPI32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `lineAnswer` | 3 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineClose` | 1 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineDeallocateCall` | 1 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineDrop` | 3 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineGetCallStatus` | 2 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineGetDevCaps` | 5 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineGetID` | 6 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineInitialize` | 5 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineMakeCall` | 5 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineNegotiateAPIVersion` | 6 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineOpen` | 9 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineShutdown` | 1 | Telephony is unavailable (LINEERR_NODRIVER). |
| `lineTranslateAddress` | 7 | Telephony is unavailable (LINEERR_NODRIVER). |

#### USER32.DLL

| Function | Args | Semantics |
|---|---:|---|
| `AdjustWindowRectEx` | 3 | No frame: rectangle unchanged. |
| `BeginPaint` | 2 | Fills PAINTSTRUCT with a screen DC and an empty paint rectangle. |
| `CallWindowProcA` | 5 | Calls the guest procedure (or the default procedure for HLE addresses). |
| `ClientToScreen` | 2 | Adds the window origin. |
| `ClipCursor` | 1 | TRUE (no clipping). |
| `CopyAcceleratorTableA` | 3 | Copies back the entries (count when p is NULL). |
| `CreateAcceleratorTableA` | 2 | Copies the ACCEL entries into a host-side table. |
| `CreateWindowExA` | 12 | Creates a headless window and sends, like Windows, WM_NCCREATE and WM_CREATE (with a CREATESTRUCTA). A visible top-level window then receives WM_SHOWWINDOW, WM_ACTIVATEAPP(1), WM_NCACTIVATE(1), WM_ACTIVATE(1), WM_SETFOCUS, WM_SIZE and WM_MOVE, and becomes the active, foreground and focus window. Default sizes (CW_USEDEFAULT) become 640x480. |
| `DefDlgProcA` | 4 | As DefWindowProcA. |
| `DefWindowProcA` | 4 | WM_NCCREATE -> TRUE, WM_ERASEBKGND -> 1, WM_CLOSE destroys the window, others 0. |
| `DestroyAcceleratorTable` | 1 | Forgets the table. |
| `DestroyWindow` | 1 | Sends WM_DESTROY and WM_NCDESTROY, then marks the window (and children) destroyed. |
| `DialogBoxParamA` | 5 | Win32 modal dialogs are only used for fatal start-up errors: logged, returns IDCANCEL (2). |
| `DispatchMessageA` | 1 | WM_TIMER with a callback calls it; otherwise the target window's procedure. |
| `DrawEdge` | 4 | No drawing (headless GDI). |
| `DrawFocusRect` | 2 | No drawing (headless GDI). |
| `DrawTextA` | 5 | No drawing; returns a nominal text height of 16. |
| `EnableWindow` | 2 | Sets the enabled flag; returns TRUE when it was previously disabled. |
| `EndDialog` | 2 | Destroys the dialog window. |
| `EndPaint` | 2 | TRUE. |
| `EnumWindows` | 2 | Calls the guest callback for every live top-level window, in creation order. |
| `EqualRect` | 2 | Rectangle equality. |
| `FindWindowA` | 2 | Searches the emulated windows (no other processes exist). |
| `FindWindowExA` | 4 | Searches the emulated windows. |
| `GetActiveWindow` | 0 | The active window. |
| `GetClassInfoA` | 3 | Fills WNDCLASSA for registered classes; FALSE otherwise. |
| `GetClassLongA` | 2 | GCL_WNDPROC (-24), GCL_HCURSOR (-12), GCL_HICON (-14), GCL_HBRBACKGROUND (-10). |
| `GetClassNameA` | 3 | Registered class name. |
| `GetClientRect` | 2 | (0, 0, width, height). |
| `GetCursorPos` | 1 | Virtual cursor position (starts at 320, 240). |
| `GetDC` | 1 | Constant screen DC handle. |
| `GetDesktopWindow` | 0 | Constant desktop handle. |
| `GetDlgCtrlID` | 1 | Control id. |
| `GetDlgItem` | 2 | Child with the given control id. |
| `GetDlgItemTextA` | 4 | GetWindowTextA on the control. |
| `GetFocus` | 0 | The focus window. |
| `GetForegroundWindow` | 0 | The foreground window (the game window once created). |
| `GetKeyState` | 1 | Key state as Windows keeps it: updated when WM_KEYDOWN/WM_KEYUP are retrieved from the queue (bit 15 down, sign-extended; bit 0 toggle). All keys start up. |
| `GetLastActivePopup` | 1 | The window itself. |
| `GetMessageA` | 4 | Blocks (cooperatively) until a message or timer is available; the virtual clock jumps to the next timer when every thread is idle. Returns 0 for WM_QUIT. |
| `GetParent` | 1 | Parent (child windows) or owner (top-level windows). |
| `GetPropA` | 2 | Window property lookup. |
| `GetSysColor` | 1 | Grey (0xC0C0C0). |
| `GetSystemMetrics` | 1 | SM_CXSCREEN/SM_CYSCREEN = 640x480, icon sizes 32/16, mouse present, everything else 0. |
| `GetTopWindow` | 1 | First child of ``hwnd`` (or first top-level window). |
| `GetUpdateRgn` | 2 | NULLREGION. |
| `GetWindow` | 2 | GW_CHILD (5) first child, GW_HWNDNEXT (2) next sibling, GW_OWNER (4); others NULL. |
| `GetWindowDC` | 1 | Constant screen DC handle. |
| `GetWindowLongA` | 2 | GWL_WNDPROC/STYLE/EXSTYLE/ID/HWNDPARENT/USERDATA and extra window bytes. |
| `GetWindowRect` | 2 | Window rectangle in screen coordinates. |
| `GetWindowTextA` | 3 | Returns the stored text. |
| `GetWindowTextLengthA` | 1 | Length of the stored text. |
| `GetWindowThreadProcessId` | 2 | Main thread of this process. |
| `IntersectRect` | 3 | Intersection; FALSE (and an empty rectangle) when disjoint. |
| `InvalidateRect` | 3 | TRUE (no WM_PAINT is generated: the game redraws its whole surface every frame). |
| `IsChild` | 2 | Parent-chain test. |
| `IsDialogMessageA` | 2 | FALSE (no Win32 dialog navigation). |
| `IsIconic` | 1 | FALSE. |
| `IsWindow` | 1 | TRUE for live windows. |
| `IsWindowEnabled` | 1 | Enabled flag. |
| `IsWindowVisible` | 1 | Visibility flag. |
| `KillTimer` | 2 | Removes the timer. |
| `LoadAcceleratorsA` | 2 | Real RT_ACCELERATOR (9) lookup in the module's resources; the 8-byte resource entries {fVirt, key, cmd, pad} become a host table of 6-byte ACCEL records (NULL when absent). |
| `LoadCursorA` | 2 | Opaque cursor handle. |
| `LoadIconA` | 2 | Opaque icon handle. |
| `LoadImageA` | 6 | Opaque image handle. |
| `LoadStringA` | 4 | Real RT_STRING lookup in the module's resources (block ident/16+1, entry ident%16). |
| `MessageBoxA` | 4 | Logged (text and caption are recorded); returns IDOK (1), or IDYES (6) for yes/no boxes. |
| `MsgWaitForMultipleObjects` | 5 | Waits for the handles or for queued input. |
| `OffsetRect` | 3 | Moves RECT. |
| `PeekMessageA` | 5 | Posted messages first, then due timers. An empty poll charges ``idle_quantum_us`` (1 ms) of virtual time to the clock: this is how time passes for the game's busy loops. |
| `PostMessageA` | 4 | Appends to the message queue. |
| `PostQuitMessage` | 1 | Queues WM_QUIT. |
| `PtInRect` | 3 | Point-in-rectangle test (POINT passed by value). |
| `RegisterClassA` | 1 | Records WNDCLASSA (procedure, extra bytes, icon, cursor, brush); returns an atom. |
| `RegisterClassExA` | 1 | Records WNDCLASSEXA; returns an atom. |
| `ReleaseCapture` | 0 | Clears the capture. |
| `ReleaseDC` | 2 | 1. |
| `RemovePropA` | 2 | Removes and returns a property. |
| `ScreenToClient` | 2 | Subtracts the window origin. |
| `SendDlgItemMessageA` | 5 | SendMessageA to the control. |
| `SendMessageA` | 4 | Synchronous call of the window procedure (HWND_BROADCAST is ignored). |
| `SetActiveWindow` | 1 | Sets the active window; returns the previous one. |
| `SetCapture` | 1 | Sets the capture window; returns the previous one. |
| `SetClassLongA` | 3 | Sets a class value; returns the previous one. |
| `SetCursor` | 1 | Returns the previous cursor handle. |
| `SetCursorPos` | 2 | Moves the virtual cursor. |
| `SetDlgItemTextA` | 3 | SetWindowTextA on the control. |
| `SetFocus` | 1 | Moves the focus, sending WM_KILLFOCUS/WM_SETFOCUS; returns the previous focus. |
| `SetForegroundWindow` | 1 | Sets the foreground window. |
| `SetPropA` | 3 | Window property store. |
| `SetRect` | 5 | Writes RECT. |
| `SetTimer` | 4 | Virtual-clock timer; replaces an existing (hwnd, id) timer. |
| `SetWindowLongA` | 3 | Sets the value and returns the previous one (subclassing changes the procedure). |
| `SetWindowPos` | 7 | Moves/resizes unless SWP_NOMOVE/SWP_NOSIZE; SWP_SHOWWINDOW/HIDEWINDOW honoured. |
| `SetWindowTextA` | 2 | Stores the text. |
| `ShowCursor` | 1 | Display counter. |
| `ShowWindow` | 2 | Updates visibility; returns the previous visibility. |
| `SystemParametersInfoA` | 4 | SPI_GETWORKAREA (0x30) -> 640x480; other queries leave the buffer and return TRUE. |
| `TranslateAcceleratorA` | 3 | Win32 accelerator matching: WM_KEYDOWN/WM_SYSKEYDOWN against FVIRTKEY entries and WM_CHAR against character entries, with the Shift/Ctrl/Alt state (FSHIFT 4, FCONTROL 8, FALT 0x10) from the key-state table. A match sends WM_COMMAND(0x10000 \| cmd) to ``hwnd`` synchronously and returns TRUE. |
| `TranslateMessage` | 1 | Posts WM_CHAR for WM_KEYDOWN of printable keys (US layout, no shift state). |
| `UnregisterClassA` | 2 | Forgets the class. |
| `UpdateWindow` | 1 | TRUE (painting is driven by the game itself). |
| `WaitForInputIdle` | 2 | 0. |
| `wsprintfA` | varargs | Integer/string printf formatting (no floating point, like Windows); output capped at 1024 bytes. |

#### VERSION.DLL

| Function | Args | Semantics |
|---|---:|---|
| `GetFileVersionInfoA` | 4 | Copies the RT_VERSION resource. |
| `GetFileVersionInfoSizeA` | 2 | Size of the image's real RT_VERSION resource (loaded original images only). |
| `VerQueryValueA` | 4 | "\" returns the VS_FIXEDFILEINFO (offset 40); other paths are not supported (FALSE). |

#### WINMM.DLL

| Function | Args | Semantics |
|---|---:|---|
| `timeBeginPeriod` | 1 | TIMERR_NOERROR. |
| `timeEndPeriod` | 1 | TIMERR_NOERROR. |
| `timeGetTime` | 0 | Virtual milliseconds (same clock as GetTickCount). |
| `timeKillEvent` | 1 | MMSYSERR_INVALPARAM. |
| `timeSetEvent` | 5 | Multimedia timers are not provided (NULL). |
| `waveOutClose` | 1 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutGetID` | 2 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutGetNumDevs` | 0 | 0 devices. |
| `waveOutOpen` | 6 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutPrepareHeader` | 3 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutReset` | 1 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutSetVolume` | 2 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutUnprepareHeader` | 3 | No wave output device (MMSYSERR_NODRIVER = 6). |
| `waveOutWrite` | 3 | No wave output device (MMSYSERR_NODRIVER = 6). |

#### WINTRUST.DLL

| Function | Args | Semantics |
|---|---:|---|
| `WinVerifyTrust` | 3 | TRUST_E_NOSIGNATURE: signatures are never verified (only the System Survey launcher asks). |

## How to run

From `tools/research`, with `~/koprulu-private/venv/bin/python` (Unicorn 2.1.4, pefile, capstone, a C compiler for the 40-line coverage hook):

```
python -m sc_emulator boot2                 # milestone 1: boot to the idle main menu -> emu/boot/boot.json
python -m sc_emulator mission terran-m02 terran1 --cd original --frames 3000
python -m sc_emulator mission xzerg-m63 xzerg9b --cd expansion --inject 63,0,1
python -m sc_emulator campaigns --jobs 11 --frames 3000   # all 64 missions in parallel subprocesses
python -m sc_emulator aggregate             # writes docs/research/evidence/full-game-emulation.json
python -m sc_emulator apis                  # HLE table
python -m sc_emulator chain orig-terran terran0 --cd original --frames 300          # play a campaign to its end
python -m sc_emulator chain single-m63 xzerg9b --cd expansion --inject 63,0,1 --single
python -m sc_emulator chain defeat-terran02 terran1 --cheat "game over man" --single
python -c "from sc_emulator.input_demo import run_demo; run_demo()"                 # drag-select + right-click
python -c "from sc_emulator.scenarios import wasteland; wasteland()"                # H03: Terran real victory, hotkeys, placement
python -c "from sc_emulator.scenarios import terran_sandbox; terran_sandbox()"      # H03: command card, spell, add-on, research
python -c "from sc_emulator.scenarios import worker_build; worker_build('zerg1','original','zerg-drone-pool',41,ord('B'),ord('S'),142,131)"
python -c "from sc_emulator.scenarios import zerg_no_cheats; zerg_no_cheats()"             # H07: Z1 victory, no cheat
python -c "from sc_emulator.scenarios import protoss_p1_nocheat; protoss_p1_nocheat()"     # H07: P1 victory, no cheat
python -c "import sc_emulator.scenarios as s; s.protoss_p6_nocheat(route=s.P6_ROUTE_FULL)"  # H07: P6 attempt
```

`emu/chains/<name>/` holds:

- `chain.json`: missions, events, states, profile saves and the input log;
- `trace.jsonl`: per-frame digests, now with a `mission` field;
- `coverage.json`;
- PNGs: the frame before the cheat, the end dialog and the score screen.

Per mission, `emu/missions/<name>/` holds:

- `trace.jsonl`: one digest per frame;
- `sounds.jsonl`: sound and music requests;
- `frameNNNNN_screen.png`: the game bitmap `0x6d0b88` with palette `0x6d2388`;
- `frameNNNNN_present.png`: the 32-bit surface as presented;
- `coverage.json`, `files.txt` (MPQ files requested), `summary.json` and `run.log`.

### Frame digest

The digest is taken at `0x444f76`, in RunPendingFrames right after the trigger scheduler `0x4be500` returns, once per frame-counter value. Fields:

| Field | Content |
|---|---|
| `f` | frame counter `0x52651c` |
| `rng` | game RNG state `0x67393c` |
| `u` | number of units in the active list (head `0x665ae0`) |
| `uh` | number of units in the hidden list (head `0x665af8`) |
| `h` | SHA-1 (first 16 hex digits) over, for each unit of both lists in list order: type `+0x64`, owner `+0x4c`, position `+0x28`, HP `+0x08`, order `+0x4d` and pool index |
| `cd`, `el` | trigger countdown `0x5349d4` and elapsed seconds `0x5349d8` |
| `w` | trigger wait per player: remaining ms from `0x6ca768[p]` when the wait byte `0x6ca760[p]` is set, else −1 |
| `min`, `gas` | `0x5263d0[0..7]` and `0x526400[0..7]` |
| `res` | committed trigger results `0x5349e0[0..7]` |
| `t` | virtual milliseconds |

### Sound log

Each entry records the frame, virtual time, function and arguments. The hooked entry points are:

- `0x49e4e0` PlaySoundInternal (ECX id, EDX attenuation, pan, unit);
- `0x49e740` PlaySound;
- `0x49e780` PlaySoundAtVolume;
- `0x49e810` PlayStreamedSoundFile, which carries the path: trigger and briefing speech;
- `0x46ef10` PlayMusicTrack and `0x46f1b0` StartMusicTrack (track).

## Starting missions (milestone 2 path)

The driver uses the game's own mission-warp mechanism:

1. The command line is `StarCraft.exe ophelia <token>`. `ParseCommandLine` (`0x4661b0`) passes each token to `ProcessCheatText` (`0x40dee0`). "ophelia" sets cheat bit `0x200`.
2. The warp token (`terran1`, `xzerg4c`, …) reaches `HandleMissionWarpCheat` (`0x40e030`), which records a pending mission through `0x457e10`.
3. At entry, the glue loop (`0x457510`) checks that the CD archive has the campaign menu (`0x45a6b0`) and calls `CreateCampaignMissionGame` (`0x4bb6c0`) with the mission index.
4. It opens the race's ready room. The briefing runs from the map's MBRF triggers, with portraits and speech.
5. After 2 virtual seconds the driver presses the Start button's hotkey `S` (synthetic `WM_KEYDOWN`/`WM_KEYUP`; the game's `TranslateMessage` call adds the `WM_CHAR`). The first-pass batch also posted an explicit `WM_CHAR`, which the game received twice. That queues start command `0x54` through the turn pipeline.
6. Glue result 101 sets game mode 1. `RunGameSession` → `StartGame` `0x443f40` then loads the CHK, creates units and launches triggers and AI scripts, and `RunGame` `0x445310` loops.

Warp tokens are derived from the campaign tables `0x50e0b8`/`0x50e0c4` and the warp table `0x4eba18` (`campaigns.py`). The warp cheat refuses mission 63, so that mission is started by making the guest call the same pending-mission setter `0x457e10`(ECX = 63, EDX = race 0, expansion 1) at the first instruction of the glue loop. No input is given after Start.

The UI path also works and was used while developing: main menu → Single Player → Original → new ID "Player" → Terran → prologue → tutorial ready room, all by synthetic mouse clicks and typed characters.

## Limitations (exact)

- **Speed.**
  - About 0.1 s wall per frame for one process, and about 0.15–0.18 s with 8–11 in parallel. A 20,000-frame mission took 42–55 minutes with 11 processes running.
  - Emulation runs at about 10 million translation blocks per second.
  - Each process keeps about 1.2 GB resident, mostly Unicorn's translation cache.
- **Time model.** Virtual time advances by fixed charges (1 ms per empty message poll, 50 µs per clock query), not by executed instructions.
  - The simulation advances once per frame-counter step and does not read the wall clock. This was checked statically over every `GetTickCount`, `GetLocalTime`, `GetSystemTimeAsFileTime` and `QueryPerformanceCounter` call site in the executable. The trigger elapsed-seconds counter `0x5349d8` is incremented by `TickTriggerClocks` `0x4be410` every 16 frames. Trigger waits use the game-speed table milliseconds passed by `RunTriggerScheduler` `0x4be500`.
  - Wall-clock readers are limited to frame pacing (`PollTurn` `0x444cc0`, `RunPendingFrames` `0x444e30`, `GameLoop` `0x4451f0`, `ReceiveTurns` `0x475ac0`), the score-screen/save elapsed time (`0x47a750`/`0x47a780`), UI timers, text-line expiry, sound caching and network code. The CRT seed (`time()`) is the only exception, and its value is listed under the game seed below.
  - UI timings differ from a real PC: the number of game-loop iterations and presents per frame, scroll and palette-cycle cadence, and how long briefings take in virtual time.
  - The game seed is CRT `time()` at game start, in whole virtual seconds. Any change to the pre-game event sequence can therefore change the seed (see the long runs).
- **Input.**
  - Real victories without any cheat exist for all three races: Terran Wasteland, Zerg Z1 and Protoss P1. The H03b victories in Z1 and Dark Origin used helper cheats.
  - P6 (and the other hero-escort missions) has no cheat-free victory. The scripted escort dies to the map's burrowed ambushes and Infested Terrans before reaching "open sesame!" (H07).
  - The campaign-chain endings use cheats: "there is no cow level", "game over man".
  - Scripted play uses fixed recipes, not general play:
    - Placement clicks candidate points around a base until the game accepts one.
    - Waves leave from a staging point, and defence reacts only to raiders near buildings.
    - Clicks on moving units can miss, which the scripts work around by stopping units first or retrying.
    - A recipe can need changes for each map: the P1 recipe had to power the rescued Gateway, and Ctrl+click selection keeps Fenix out of waves.
  - Every single-player input path has now been exercised at least once (H03, H03b, H04, H05, H06).
  - Ctrl+F2…F4 have no accelerator binding, so there is nothing to exercise. Ctrl+S was first run without a sound device, so `ToggleSfxMute` returned at its device test. H06b repeated it with a device: the effects volume went 50 → 0 → 50 and `StopAllSounds` ran, as specified.
  - Multiplayer-only inputs stay out of scope because the offline single-player game cannot reach them:
    - `ToggleTextbox` (P39 `0x4b3bd0`) sets message mode 1 offline and sends text with `SendTextboxChat` `0x4b3d60` only when the multiplayer byte `0x672664` ≠ 0. That function is `excluded`, so chat targets and recipient modes never apply.
    - The alliance and team helpers (P39) require the same byte; the Single Player path keeps it 0 (shared facts).
  - The 3,000- and 20,000-frame runs give no input after Start.
- **Mission start.** The mission-warp cheat skips the main-menu/campaign-menu UI and sets cheat bit `0x200` (Ophelia), which only enables warping.
  - Mission 63 is started by a guest call of `0x457e10` injected at the glue-loop entry.
  - Original-campaign runs hide `BroodWar.mpq`, so `StarCraft.mpq` is the CD archive; with both present, the warp to an original mission fails as on real hardware.
  - Warp starts use the default profile "Anonymous".
- **Campaign chains.**
  - States are sampled every 100 ms of virtual time, so very short glue/mode states can be missed; events come from code hooks and are complete.
  - Cinematics (mode 10) are skipped with Escape. Their videos start but are not watched.
- **Sound.**
  - No audio is mixed; the log records requests and buffer events, not audible output.
  - Cursor advance uses `nAvgBytesPerSec` with no hardware latency or granularity.
- **Windows behaviour not modelled.**
  - Preemptive scheduling: threads switch only at blocking calls. A spin-wait on another thread's memory without a Windows call would hang; none was observed.
  - DLL thread attach/detach notifications.
  - Real GDI rasterisation: GDI is headless and the game draws nothing through it in these paths.
  - Win32 dialog templates: `DialogBoxParamA` returns IDCANCEL and is logged. It occurs only on fatal start-up errors and never happened.
  - CPU-fault dispatch to SEH: faults stop the run and are reported. None occurred.
- **Coverage.** It covers block starts in StarCraft.exe and storm.dll only; `local.dll`, `standard.snp`, `smackw32.dll` and Storm's run-time SCode blitters are executed but not mapped.
  - "Function executed" means a translation block started at the entry, so a function entered only by fall-through would be missed.
  - The outside-body analysis depends on Ghidra's function sizes and is heuristic.
  - Statuses come from the project atlas JSON at aggregation time (2026-10-11).
