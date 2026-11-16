# Multiplayer simulation without a network (phase M)

The user approved the design on 2026-11-15. The goal is a simulation that several seats can play in one deterministic game, before any transport exists: melee and custom maps with their start and victory rules, the original's network branches, the melee AI, and state that does not depend on which seat a process shows. The game setup, templates and custom maps will be ADR 0024; the seats, the network branches, the seat-local state, the shared hash and the turn hub ADR 0025.

The user's choices:
- **Oracles:** an emulator driver of the original's Play Custom screen (one person against computers) and public melee replays recorded before 1.21, kept in the private workspace only. The emulator runs no network game, so the branches that need the multiplayer byte `0x672664` are checked from the atlas, the disassembly and tests, not observed: the original plays a replay with the byte at 0 (the Single Player menu clears it, the Multiplayer menu sets it, [P20](research/atlas/p20.md)).
- **Melee AI:** all 20 opcodes not ported until then; `fatal_error` becomes a game failure, as the original's fatal error.
- **Seats:** a headless test of N seats in one process, and a development mode of the product, `--seats N`.

| Stage | What | State |
|---|---|---|
| M0 | Oracles: the emulator's Play Custom driver, its start hooks and recorded melee games; the public replays played by the emulator | done (2026-11-16) |
| M1 | The game's setup (template, slots, multiplayer byte) in the start input and the game; templates and victory trigger files in the data; `.scm`/`.scx` maps, the CHK's melee sections, sounds from the map; the map in saves; the replay header's template and slots; the refused map's popup | done (2026-11-16, [ADR 0024](adr/0024-game-setup-and-custom-maps.md)) |
| M2 | The melee start and victory: slots, random races and starts, colours, the melee unit filter, starting units and resources, `SetupVictoryConditions` and the `.trg` files, alliances, Opponents, Modify Resource and Hangar, Capture the Flag, the results after a custom game | done (2026-11-16; the results after a custom game moved to M4) |
| M3 | The melee AI: `StartCustomLevelAIScripts`, the non-campaign planner and town paths, all 20 opcodes | done (2026-11-16) |
| M4 | The Play Custom screen: menu state 11, the combo box, the map list and import, game types, slots | done (2026-11-16; the web's and the Fold7's Import tried at M7) |
| M5 | The network branches of `kp_core` under the multiplayer byte: triggers, network commands, pause limits, team games | done (2026-11-16; team games fixture-verified) |
| M6 | Seats: the seat-local state out of the shared hash, the turn hub (the third kind of `KpTurns`), the N-seat test, `--seats N` | done (2026-11-16; the hub has no leave, drop or chat) |
| M7 | Acceptance and documents | done (2026-11-16) |

## M0: oracles (as built)

**Play Custom in the emulator.** `tools/research/sc_emulator/custom.py` (`python -m sc_emulator custom MAP OUT --type … --race … --slots …`) drives the original's Play Custom screen. [Full-game emulation](research/full-game-emulation.md#play-custom-and-public-replays-phase-m0) describes how, and what the offline screen offers.

The game set is in the private workspace (`koprulu-private/emu/custom/games.txt`, `run.sh`). Each game runs 2001 frames, with state dumps at steps 1–101 and every 20th step to 2001, and screens at frames 1, 500 and 2000.

**Public replays.** `koprulu-private/replays-public` holds:
- the four bwscrep files recorded before 1.21: Iron Man Ladder, Ladder, Slaughter, Team CTF;
- the 36 replays of the SSCAIT 2016 archive: melee, one FFA and one Top vs Bottom against computers.

All 40 are Brood War files. The emulator plays them through Load Replay (`replay.py`, now with `--frames` and `--state`):
- each to frame 2001, with the dumps of the same steps (`koprulu-private/emu/replays-public/run.sh`);
- the six of the multiplayer game types to their end, with a dump every 500th step (`koprulu-private/emu/replays-public-full/run.sh`).

SSCAIT's bots ran under BWAPI, which needs 1.16.1, and the original's 1.17 plays those files. A replay carries no sync check, so whether 1.17 plays a 1.16.1 game exactly as it was played is not known. The emulator's playback is the oracle either way: Koprulu has to equal what 1.17 does with the same commands.

**Results.**
- **Play Custom:** 11 of the 12 games ran to frame 2001: Melee with two, four and eight players, Free For All with six and eight, random races, closed slots, and Use Map Settings on Zoo Keeper and Twilight Struggle. Use Map Settings on Bottleneck, a melee map with no computer slot, starts no game: Ok leaves the screen as it was ([full-game emulation](research/full-game-emulation.md#play-custom-and-public-replays-phase-m0)).
- **Public replays to frame 2001:** every replay that finished reached the frame limit. None was refused or stopped by a fault once the VirtualAlloc region's allocator gained its first-fit fallback (the public maps had exhausted it).
- **The unported AI opcodes the games reached:** `groundmap_jump` (every melee and FFA game, once per computer) and `race_jump` (the Brood War games). The default melee scripts reach no other unported opcode in 2001 frames. The public replays run no AI except the team game's computers (`groundmap_jump`).

**Deferred from the plan:**
- **`state_check.py --kind custom`** waited for M2 (built there).
- **The start hooks on 0x47e100 and 0x448530 and a log of the start's random calls** were not needed: the dumps at step 1 of every game equal Koprulu's start.

## M1: the game's setup and custom maps (as built)

[ADR 0024](adr/0024-game-setup-and-custom-maps.md) records the decisions. In short:

- **`KpGameSetup`** (`kp_start.h`): the template record, the multiplayer byte, the map's slots, and per slot its Storm ID, type, race, force and name. It goes in through `KpStartInput.setup` and `KpHostOpts.setup`, and the game keeps a copy. It is saved and hashed as the walk's section `SETP` (save version 4). A campaign's setup is made from its map.
- **The CHK by the game** (`kp_chk_load`, `chk.py`): a melee or Use Map Settings load skips unknown tags and runs each handler per chunk, as `0x469970`. The install's 211 maps: 209 load for melee and 201 for Use Map Settings.
- **Custom maps** (`KpDataMap`, `kp_mission_from_map`): an `.scm`/`.scx` through `kp_mpq_open`, or a scenario alone (from a replay or a save). Its files read the map's MPQ first.
- **Templates and victory files** (`kp_template.{h,c}`, `KpData.templates`, `KpData.victory`): `templates.lst`, its 30 `.got` files and the eleven `triggers\*.trg`, in the input manifest and the repack, checked to the byte. A Remastered install holds the `.got` files only under its enUS locale; without it the type list is empty ([ADR 0024](adr/0024-game-setup-and-custom-maps.md) decision 4).
- **Saves** of a custom map's game carry the map in the container section `MAPC` (`kp_save_map`).
- **Replays**: the header's template, type and slots are the setup's; Load Replay plays a custom map's replay from its own CHK (`KP_FLOW_CUSTOM`). `KP_REP_NOT_CAMPAIGN` is gone.
- **Refusals**: a game that does not start shows gluAll 66 on the load screen and gluAll 51 in Load Replay (`kp_ui_menus_refused`).

Tests: `test_session.session_a_custom_maps_game`, `test_flow.load_replay_plays_a_custom_maps_replay`, `test_data.templates_parse_as_the_original`, `test_data.templates_of_the_data`, `test_chk.the_install_maps_load`, `test_save`'s schema row of version 4, and `chk.py`'s game-load tests.

**Left for later stages:** restarting a custom game after it ends and a product flag to start a map from the command line (M4, with Play Custom).

## M2: the melee start and victory (as built)

[ADR 0024](adr/0024-game-setup-and-custom-maps.md) decisions 9–16 record it:

- **The start of a lobby's setup** (`kp_gametype.c`): the custom races, the user-select flags, the random races, the slot types and starts, the colours, then the reseed; the started records in `g->setup`. The briefing's player is the lobby's local slot; the music's rotation is the start's (`g->music_rotation`, carried in `KpCarry`, the replay tail's version 2).
- **Units, resources, alliances:** the melee filter, the start units, the starting resources (also the AI's records), `InitAlliancesAndVision` whole.
- **Victory:** the computers' melee AI scripts start (their opcodes are M3's), the template's `.trg` replaces the triggers, Capture the Flag's beacons and flags (`kp_ctf.c`).
- **Triggers:** Opponents, Modify Unit Resource Amount, Modify Unit Hangar Count; ADR 0023's refusals of them and of the Flag are gone, and `test_player_reach` takes the Flag Beacons.
- **Map strings past STR** read empty or end at STR's end (the original reads its heap): the protected maps of the public replays load.
- **A replay's chat** (0x5c) shows its line; the leave command 0x57 waits for M5.

**Comparisons:**
- **Play Custom** (`state_check.py --kind custom`, `tests/golden/oracle/custom`, `test_scenarios.custom_games_equal_the_original`): all 11 games start as the original's; Zoo Keeper and Twilight Struggle (Use Map Settings) equal at every step to 2001. The melee and Free For All games stop at their computers' first unported opcode (`groundmap_jump`, M3), so their goldens hold step 1.
- **Public replays** (`state_check.py --kind public`, goldens in `tests/golden/oracle/public`, private): 36 of the 39 the emulator finished equal at every step to 2001 (the emulator does not play the 40th, Ro8 M1G1: see M3). The three others stop where a later stage starts:
  - bwscrep's Iron Man Ladder (step 1241) and Team CTF (step 301) send the alliance command `0x0e` (M5);
  - SSCAIT's Top vs Bottom game against computers needs the non-campaign town (M3).

Tests: `test_gametype` (the custom races, the music's rotation, Opponents, Modify Resource and Hangar, Capture the Flag, the colour rows), `test_hud.chat_command_shows_its_line`, `test_data.map_strings_past_str`, `test_chk.the_install_maps_load` (211 of 211 both ways), `test_kp_rep.unrecorded_commands_refused`, `test_player_reach`.

**Moved to M4:** the results after a custom game. `CreateNextScenarioGame` `0x4bb360` runs in the custom mode `0x6ca71c`, which only the Play Custom screen sets (`PlaceLocalHumanForCustomGame` `0x4bb550`); the score's result (`ComputeGameResults` `0x477270`) is already the local player's state byte offline. The product's `--map` needs the lobby's records, which the Play Custom screen makes (`CreateGameFromMapPath` `0x45c510`).

## M3: the melee AI (as built)

- **Every opcode** has its case in the interpreter (`kp_aiscript.c`); the table `UNPORTED` and `kp_aiscript_ported` are gone, and the switch's default fails with the opcode (`0x494a98`) only for an ordinal past the table. The melee ones:
  - race_jump (`GetNearestEnemyRace` `0x486c50`: the owner of the hostile grounded building nearest the town's region centre, Terran for none), region_size (`0x4877f0`: the walk without a visited set, which counts a region each time it is reached), get_oldpeons (`TransferWorkersToTown`), groundmap_jump (`0x486410`), guard_resources (`0x493790`), rush (`0x4ad550`, kinds 0–13);
  - the waits (wait_secure `0x482470`, wait_upgrades `0x483200`, wait_bunkers `0x4b05c0`, wait_turrets `0x4b0600`) and the requests (build_bunkers `0x4ade10`, build_turrets `0x4aded0`);
  - harass_factor (`0x483830`), eval_harass (`0x4af5b0`), implode (`0x4ad430`), make_patrol (`0x4b0f10`), enter_transport and exit_transport (`0x4af520`, `0x4af6d0`);
  - debug shows its text from the binary as the script player's chat line (`kp_chat_display`, `DisplayChatMessage` `0x46a860` with its sound) and jumps; fatal_error stops the game as the original's fatal error ("Illegal AI script executed."), and `kp_aiscript_check` refuses a game whose scripts can reach it.
- **The non-campaign paths:** StartTown's territory (`0x4979b0`: the regions within depth 5 of a first town, 2 of a later one, up to the defence anchors) and route cuts (`0x4b0be0`, also start_areatown's), the weakest region near the staging choices (`0x4aa350`), and the expansion attack plan at the region scheduler's player boundary (`0x4aa8f0` with `0x4ab1d0`, `0x496320`, `0x496780`).
- **The melee start** (`StartCustomLevelAIScripts` `0x4486a0`, the campaign flag by the template) came with M2.

**Which opcodes the games reach.** The static closure (`scdata/analyze/ai.py`) of the six melee scripts (ZMCu, TMCu, PMCu, ZMCx, TMCx, PMCx) reaches race_jump, get_oldpeons, groundmap_jump, guard_resources and rush; the scripts a map's Run AI Script names reach enter_transport (EnTr), exit_transport (ExTr), make_patrol (StPt) and get_oldpeons (the SUP scripts). No stock script reaches the other twelve: wait_secure, build_bunkers, wait_bunkers, debug, fatal_error, build_turrets, wait_turrets, harass_factor, region_size, eval_harass, wait_upgrades, implode. A map cannot carry its own AI binary, so the original never runs them in a game of its own data; they are fixture-verified (`test_aiscript.melee_opcodes_follow_the_interpreter`, `more_melee_opcodes_follow_the_interpreter`, and `every_opcode_reads_its_aiop_operands` for each opcode's operands). The recorded games reach groundmap_jump (every melee and Free For All computer) and race_jump (the Brood War ones) in 2001 frames. Two longer games with three computers each add rush: `o-long-4p` (Blood Bath, Zerg, Terran and Protoss computers against an idle Terran player, who is defeated at frame 6093) reaches rush from frame 2605, and `x-long-4p` (Caldera, Terran, Protoss and Zerg computers against an idle Protoss player, to frame 10,000) reaches race_jump at frame 6881. get_oldpeons and guard_resources are in the melee scripts' closure but on branches these games did not take; they and the map scripts' opcodes are covered by the fixture tests.

**Comparisons:**
- **Play Custom:** all 11 games equal the original at every step of 1–2001 with their AI traces; their goldens hold every compared step (`tests/golden/oracle/custom`).
- **Public replays to frame 2001:** SSCAIT's Top vs Bottom game against computers now equals too: 37 of the 39 equal, the two others wait for the alliance command `0x0e` (M5). The emulator does not play the 40th (SSCAIT Ro8 M1G1): after Load Replay's Ok it spins in Storm's thread `0x15022780` (four hours without a frame, killed), though Koprulu's reader reads the file (19,485 frames). It has no golden. TODO: find what the emulator waits for there.
- **Public replays to their end** (a dump every 500th step): the 1v1 ladder game equals the original through step 66,001 of 66,504 and the Top vs Bottom team game through 26,501 of 27,576. Both stop at a network command near their end: a player leaving (`0x57`) in the ladder game, the order "nothing" (`0x1d`) in the team game. The slaughter, team CTF and iron-man ladder games stop earlier, at the alliance command `0x0e`. All three commands are M5's. The Free For All game was still being recorded when this was written; its earlier recording equalled the original through step 22,001.
- **The creep variants of a new building.** The long games and the full replays first differed in the creep tiles a Hatchery stamps later on (the ladder game at frame 4578, Blood Bath near frame 3000), while the units and the tile flags stayed equal. Every variant was one CRT draw ahead of the original's. The emulator's logger recorded the start's music pick outside the campaign (`LoadConsoleSkinAndPickMusic` `0x443680`, one `rand` right after the game's seed) as a frame-0 interface draw, and Koprulu's start makes that draw itself (`music_rotation`), so the oracle drew it twice. The logger now leaves that caller out, as it does the creep callback, and the 59 recorded logs lost that event. `tools/tests/test_emulator_hooks.py` checks both skipped callers against the executable.

Tests: `test_aiscript.melee_opcodes_follow_the_interpreter`, `more_melee_opcodes_follow_the_interpreter`, `every_opcode_reads_its_aiop_operands` (until now `ported_opcodes_are_the_interpreters`), `scripts_are_checked_before_a_game` (fatal_error in place of an unported opcode), `test_scenarios.custom_games_equal_the_original`.

## M4: the Play Custom screen (as built)

[ADR 0024](adr/0024-game-setup-and-custom-maps.md) decisions 17–22 record it.

- **The screen** is glue state 11 (`RunCreateGameScreen` `0x455550` on `rez\gluCustm.bin`, which Load Replay's state 12 shares). The campaign menus' Play Custom opens it; until now the button was inert.
  - **The list:** the listed folder in the user's maps folder (`<pref>maps/`), then in each data folder's `Maps` (`ScanMapDirectory` `0x45c9a0`, `BuildMapList` `0x45ba40`). `[replays]` below the root lists the replays folder. `.scx` files are listed only with Brood War, and Brood War's first visit enters `maps/BroodWar` (`InitMapList` `0x455aa0`).
  - **The entry's information** shows 100 ms after its selection (`PreviewMapEntry` `0x45b970`, `LoadMapEntryInfo` `0x45d870`, the host's `load_entry`). A file that cannot be read gets its gluAll text: 66 missing, 62 a name too long, 60 the expansion's, 63 another refusal.
- **The combo box** (control type 13, [P43](research/atlas/p43.md) `0x4c5b80`–`0x4c63b0`, `kp_ui_list.c`) is a closed field one row high. While the left button is held its list drops open below the field (above it when the dialog has no room); the release selects the row under the pointer (`ComboBoxProc` `0x4c63b0`). An open list is drawn after the dialog's other controls.
- **Game types and subtypes** (`FillGameTypeList` `0x456910`, `0x456a40`, `0x456830`): the templates selectable offline, which are Melee, Free For All and Use Map Settings. Without a saved choice the default is the last melee template, Free For All. The registry's "Custom Type" and "Game Subtype" are kept in the profile's `CUST` (schema 9).
- **Slots** (`RefreshComputerSlots` `0x456420`, `FillSlotTypeList` `0x456210`, `FillRaceList` `0x456170`):
  - a melee template gets the player's slot and a computer in each other slot of the map;
  - another template gets the map's computers, locked;
  - a slot offers Closed or Computer, a race Zerg, Terran, Protoss or Random (Random by default);
  - `ApplyCustomGameRaces` `0x455020` gives the lobby the races.
- **Ok** (`OnCreateGameOk` `0x455240`, `CreateGameFromEntry` `0x45bfc0`, `ValidateGameTemplate` `0x45b700`, `FinishSinglePlayerCreate` `0x4554a0`):
  - on a folder, enters it;
  - on a map, reads it again (its header, the player in the first open slot). A map whose game does not load is refused with gluAll 51;
  - outside Use Map Settings the game starts at once, else from the ready room of the player's race (Zerg 8, Terran 7, Protoss 9). The lobby is `kp_setup_lobby` (ADR 0024 decision 9).
- **After the game** (`AdvanceCampaignAfterGame` `0x4bb790` in the custom mode `0x6ca71c`, `CreateNextScenarioGame` `0x4bb360`, whatever the result):
  - without a next scenario, the campaign menu of the screen's expansion byte;
  - with one, that map in the finished map's folder, played with Use Map Settings from the ready room of the player's race;
  - a next scenario that is no map gives network.tbl's `0x6a`. It is deferred and shown over the campaign menu (`StoreDeferredNetError` `0x4748d0`, `FlushDeferredNetError` `0x474980`, `kp_ui_menus_net_error`);
  - a name that is no file of the folder (a separator, a control character, `.` or `..`) gives `0x6a` too. This is Koprulu's: the original appends any name the map's trigger gives, so a map could reach a file out of its folder (`test_flow.custom_games_end_in_the_next_scenario_or_the_campaign_menu`).
- **Import** (Koprulu's, as Load Replay's: [phase RP](phase-replay.md#rp4-as-built)): record 44 of `rez\gluCustm.bin`'s `extend`, above Ok, because the slots fill the space under the list. It is shown only with the platform's file picker.
  - The host's `pick_file` now takes the kind of file: the picker filters `.scm` and `.scx`, or `.rep`.
  - `kp_flow_import_map` copies the map into the listed folder's user base, making its folders as needed. The name comes from the picked file as for a replay, `.scm` or `.scx` kept in lower case, otherwise the screen's extension (`.scx` with Brood War).
  - The copy is read as the list reads a map. A file that is no map of the screen's is removed again (gluAll 63, or 60 for the expansion's).
  - The list then shows the copy selected. It is the only way to get maps on the web, whose repack has none.
- **`--map PATH [--map-template NAME]`** (`kp_flow_play_map`) plays a map at once, without the glue, as `--mission` does.
  - The template is found by its name in `templates.lst`, ASCII case folded. The default is Melee.
  - A melee template gets the player and a computer in each other slot, all Random, as the screen offers them; another template keeps the map's slots.
  - A name that is no template, or a file that is no map, refuses to start.

**Comparisons.**
- **Against the emulator:** the 13 custom goldens' screens equal the original's (`test_flow.play_custom_screens_are_the_originals`). Each golden is opened on the same map with the same picks, made with the mouse as the emulator's driver made them (`custom.py`'s `combos`). At Ok the type, subtype, slot and race lists' items, selections and usability equal `custom.py`'s `screen`, and the lobby's records equal `setup.json`'s.
- **In the product:** `tests/scripts/custom_melee.kps` plays a melee game through the screen from the install's maps folder, with both races Terran, then plays its LastReplay to the marked step with the same `sim.crc`. `tools/ci.sh` runs it when `--data` has a maps folder.
- **Checked by hand:** `--map` on macOS ((2)Challenger, Melee); the Import button's place in a capture with the picker forced on (centred above Ok, apart from the slots); the import on the web and the Fold7 at [M7](#m7-acceptance).

**Found for M6.** The creep's borders follow the viewer's sight. `kp_creep.c` updates `creep_border` in TERR only for tiles the viewer sees (`hidden_from_viewer`), so a game and its replay differ in `sim.crc` once a Zerg's creep spreads out of the player's sight. M6 made it the seat's state, out of the shared hash; `sim.crc` (the dump's categories) still holds it, so `custom_melee.kps` plays Terran against Terran and checks that no creep exists.

**Not done:**
- The plan's `menus_check`/`ui_check` captures of gluCustm were not made; the lists and records are compared instead.
- A saved custom game's next scenario fails with `0x6a`, because the save keeps no map folder. This is an approximation: how the original finds the folder after a load is not traced.
- A map's class is always 0 (`load_entry`'s `TODO`: no signed condition file offline).

Tests:
- `test_flow`: `play_custom_screens_are_the_originals`, `custom_games_end_in_the_next_scenario_or_the_campaign_menu`, `play_custom_imports_a_map`, `map_flag_plays_a_custom_map_without_the_glue`, `load_replay_imports_and_exports` (the picker's kind);
- `test_hud.play_custom_imports_through_the_host`;
- `test_chk.a_maps_header_has_the_lists_fields`;
- `tests/scripts/custom_melee.kps`.

## M5: the network branches (as built)

[ADR 0025](adr/0025-seats-and-network-branches.md) decisions 1–8 record it. Every branch tests the multiplayer byte `g->setup.multiplayer` (`0x672664`), 0 offline and in every replay, so the single player's games and all earlier oracles are unchanged.

- **M5a, the commands a replay plays** (`kp_network.c`): alliances `0x0e` (`0x4731d0`, with `RefreshAllianceColors` `0x41d7b0` and `ClearAlliedOrderTargets` `0x4721c0`), shared vision `0x0d` (`0x473280`, `0x447600`), the order Nothing `0x1d` (`0x472b40`) and a player leaving `0x57` (`0x472da0`, `0x477a60`, `0x4be3e0`). `kp_session` refuses only the recorder's exclusions now. A defeat is announced (`0x4bd4a0`, network.tbl `0x35`/`0x36`) and `RetirePlayer` `0x4bd7a0` is whole (Sudden Death and Capture the Flag kill the units).
- **M5b, the triggers:** Transmission waits its time, Center View sets the screen without a wait, Pause and Unpause Game do nothing, the result dialog does not pause, no skip or release, `ApplyPendingResults` ignores the cheats and frees the lists of players with results.
- **M5c, the commands:** the speed is a single player's; cheats need the template's byte (`0x447640`) and no network; 3 pauses each (`0x5349f8`, `g->pauses`, `KP_SAVE_VERSION` 5) spent by a network game's humans (`0x47cb20`, `0x47cc60`); a pause or resume cancels the cursor modes at the turn's end (`0x672985`); no save in a tournament, a network game's save only from the lowest Storm ID (`0x4771e0`), no load in a network game; the latency line `0x45`–`0x47` (`0x476d10`); the critter's death only offline (`0x446c30`).
- **M5d, team games** (`IsTeamGame` `0x4b2d90`, ADR 0025 decision 9): `SetupTeamGameSlots` `0x4b2db0` (leaders by the lowest Storm ID, RNG source `0x20`), the lobby races `0x5264a0` and leaders `0x5264ac` in `g->setup` (save version 5); the seat's record `0x508d54` apart from its local player, the command's record `0x508d48` apart from its player `0x508d44`; `CreateTeamStartingUnits` `0x47eb00` and the leaders' supply (`0x4b2c00`); `NextActivePlayer` `0x4b3170` over the leaders; the force names of defeats, Capture the Flag and chat (`0x4b2bf0`); the leaders' flag beacons; the team mates' dashed circles (`0x49c2b0`, `0x423100`, `0x4230e0`, `0x4169c0`). `kp_start` no longer refuses a multiplayer setup, only an illegal team count (error `0x5d`).

**Comparisons.**
- **Against the emulator:** `state_check.py --kind public` and `--kind public-full` play the public replays, which send `0x0e`, `0x0d`, `0x1d` and `0x57`; their states equal the original's.
- **Fixture-verified:** the network game's branches (`test_multiplayer`, after the disassembly). The emulator runs no network game.

**Not done:**
- The save's "lowest Storm ID still in the game" counts every human record: the network's player state (`0x673818`) waits for a seat's leave in the turn hub (`TODO:` in `kp_commands.c`). The latency's extra turns are the hub's since M6.
- A team game's live leave (`HandlePlayerLeftGame` `0x477a60`'s team branch: the force takes its result only when its last active human leaves, `0x4b2b50`; the live leave waits for the hub) and its score screen's team roster (`TODO:` in `kp_ui_score.c`); `NextSelectedUnit`'s clearing of a dead unit's dashed circles.
- A team game's dashed circles are the seat's since M6 (the selection circles' pool and the sprite's bits `0x06`, out of the shared hash). A network game's save loads as a single player's (its first human's record is the seat's): the lobby's load of a network save is not ported.

Tests: `test_multiplayer` (20 cases: the four commands, RetirePlayer, the defeat's message, Transmission, Center View, the pause actions, the result dialog, the trigger lists, the speed and cheats, the pause limit, saves and loads, the latency; a team game's slots, its units and supply, its triggers and its commands and dashed circles).

## M6: the seats (as built)

[ADR 0025](adr/0025-seats-and-network-branches.md) decisions 10–16 record it. Several seats of one game run in one process; each seat is a session with its own local player, and the seats' turns go through a hub that stands for the network.

- **The seat's state** (`SEAT_PART` in `kp_save.c`, `KpStateRange.seat`, `KpSnapshotField.seat`): saved as before (a save is one seat's), left out of the new shared hash `kp_shared_hash` (`KP_SHARED_HASH_VERSION` 1). It is the local selection and its circles' colours, the Psi fields shown, the screen, the placement and the order marker, the command's player between turns, the CRT generator, the remembered terrain, the creep's borders and its shown bit, the fog ghosts and the selection pools (health bars, circles with the team mates' dashed ones, placement ghosts, boxes), the VIEW section, and three found by the product's seats: the images' bounds the redraw refreshes (`img.x`, `img.y`, `RefreshAllSpriteBounds` `0x415700`), the rectangle query's marks (`spatial.seen`, `stamp`; a wrap clears them now) and the requirement evaluator's last error and offsets cache (`0x66ff60`, the lazy initializers `0x407d50`…), which the interface's box selections, attack notices and button checks write too. `kp_state_hash`, the saves and `KP_SAVE_VERSION` are unchanged.
- **The images world in a canonical order:** the sprites of the rows by a content key (their images' drawing classes: a cloak's methods 2 and 5 one class, a decloak's 4 and 7 another, a hallucination's and the plain ones 0, 3, 6, 16 none), sorted; a unit's, a lone thingy's and a Pylon's Psi-field reference hashed by its sprite's key; a Psi field's hidden flag and visibility are the seat's. Fog ghosts and the placement cursor take sprites and images from the shared pools, as the original's do, so slot numbers differ between seats.
- **The core's part:** the critter's annoyance is tested in the core (`kp_critter_annoyed`, `0x446c30` reads the multiplayer byte); after each turn the command's player is the local player again (`0x508d44 = 0x508d50` at the end of `ExecuteGameTurn` `0x4768a0`); `kp_run_frame_between_turns` runs a frame without the turn manager, a paused one as `kp_paused_frame`.
- **The turn hub** (`kp_hub.{h,c}`, `KP_TURNS_SEATS`, the third kind of `KpTurns`): the turn manager's timing (`TurnManagerStep` `0x476c00`, `SendTurns` `0x476500`, `PollTurn` `0x444cc0`, `SetSpeedTable` `0x445110`: 2 frames a turn at Fastest with the hub's 8 turns a second, 2 turns in transit), `QueueTurnCommand` `0x476c80`'s bound (a full turn sent first, a command past 16 in transit dropped), each turn's commands by the Storm IDs from 7 down (`ExecuteGameTurn` `0x4768a0`), the latency command's extra turns (`OnLatencyCommand` `0x476d10`). A seat waits while another's turn is missing. The keep-alive `0x05`, the sync command `0x37` and `ResolveDesync` `0x47a630` are not carried: the shared hash checks the seats.
- **The sessions:** `KpSessionStart`'s hub, seat and local Storm ID; `kp_session_seat_begin` runs RunGame's first turn after the hub's start; a frame between turns and a wait are the session's steps.
- **`koprulu --map PATH --seats N`** (`kp_seats.{h,c}`, `kp_flow_show_seat`): the lobby's first N − 1 computers become humans with Storm IDs 1.. (`kp_seats_setup`); the window shows seat 0, the others run without an interface (their events dropped) and follow its frame; after each frame the seats' shared hashes are compared, and the first difference stops the game with a report (the frame, the walk's sections, `kp_dump`'s categories, the snapshot's fields with their first element). Ctrl+Alt+digit or the script's `seat N` shows another seat with a console of its own and the view on its screen; the values `seat` and `local.player` read them. Only seat 0 records LastReplay.
- **Fuzzing:** the target `turns` (`kp_fuzz.c`, `tools/fuzz.sh`, a synthetic seed in `tests/fuzz/seeds/turns`) drives a hub of 1 to 8 seats with commands, first turns and frames: each turn's list is the same on every seat and by the Storm IDs from 7 down, no seat passes the bound of turns in transit or its stream's ring. 60 s ran 865 724 inputs with no finding.

**Comparisons.**
- **Seats against seats:** `test_seats` plays every scenario and every recorded custom game with each other human or computer player as a seat (the multiplayer byte set) and compares the shared hash every step; a field that differs and is not in the canonical list fails. `seat_fields_leave_the_shared_hash` changes each seat field (the hash stays) and shared fields (it moves).
- **In the product:** `tests/scripts/seats_melee.kps` plays (2)Boxer.scm with `--seats 2`: each seat orders its workers, the seat is switched twice, 202 steps with no difference; `tools/ci.sh` runs it with the install's maps folder.
- **The original's own risks, kept:** a hallucination's or a burrowed unit's drawing method differs by seat, so a cloak begun on an image of method 16 runs on one seat only; a Psi field made while the pool is exhausted depends on the seat's shown fields. Both are the original's desync risks, not Koprulu's.

**Not done:**
- The hub carries no seat's leave or drop (`SNetLeaveGame`, the other seats' `HandlePlayerLeftGame` `0x477da0` after their receive) and no chat (`SNetSendMessage`'s messages, recorded as `0x5c` by `0x475b40`): `TODO:` in `kp_hub.h` and `kp_commands.c`. The save's lowest Storm ID counts every human record until then.
- The hidden seats of `--seats` draw nothing: their images' bounds stay as the simulation leaves them (the seat's state). A seat shown gets a new interface; the one it had is not kept.
- Fold7 and the web were not tried with `--seats` (a development mode of the desktop).

Tests: `test_seats` (`custom_games_share_the_state`, `scenarios_share_the_state`, `seat_fields_leave_the_shared_hash`), `test_session` (`hub_runs_the_seats_turns`, `session_seats_share_the_state`, `seats_stop_at_a_difference`), `test_script.parse_reads_the_seat`, `test_fuzz.kept_turn_inputs`, `tests/scripts/seats_melee.kps`.

## M7: acceptance

The checks of the whole phase, run on 2026-11-16 after stage M6:

- **`tools/ci.sh` with `KOPRULU_FULL=1` and the product (`KOPRULU_APP=1`):** the 698 C tests on the host, under ASan and UBSan and in wasm under node, each with every item of the long loops; the 242 Python tests; libFuzzer's five targets for 60 s each (`rep`, `chk`, `save`, `commands`, `turns`) with no finding; the product's scripts, `custom_melee` and `seats_melee` among them. The Linux build with GCC and the sanitizers (`tools/native/linux.Dockerfile`) builds without a warning; `tools/boundaries.sh` passes.
- **Against the emulator:** the 13 Play Custom games ([compatibility.md](compatibility.md#custom-games-phase-m)) in `test_scenarios`; the public replays (`state_check.py --kind public --step all`, `--kind public-full --step all`, their goldens written again by this run): the six of the multiplayer game types equal the original at every dump to their last (the iron-man ladder through step 26,501 of 26,600, the ladder 66,501 of 66,504, the slaughter 28,501 of 28,886, the team Capture the Flag 12,001 of 12,448, the Free For All 56,501 of 56,561, the Top vs Bottom team game 27,501 of 27,576); the 39 replays the emulator played to frame 2001 equal it at every step of their dumps (the 40th, SSCAIT Ro8 M1G1, has no dumps: see M3).
- **The campaign's oracles** (initial states, scenarios, idle runs, chains, plays and their saves, replays, the interface's goldens) stay equal: the multiplayer byte is 0 in every one of them.
- **The product's Play Custom, from the map to its replay's end:**
  - macOS: `tests/scripts/custom_victory.kps` (in `tools/ci.sh`). The install's first map, Melee, Terran against a Terran computer; the state CRC marked at step 160; the cheat "there is no cow level" wins (`ApplyPendingResults` `0x4be020`), the victory dialog, the score screen, its Ok the campaign menu; Load Replay plays `LastReplay.rep`, as long as the game was (`replay.frames` the victory's step), with the same `sim.crc` at the marked step, to its end. Since the Save Replay port below, the script first saves the replay as `Custom win` from the score screen, and Load Replay lists it beside `LastReplay.rep`.
  - The web (the debug build in the browser pane): `(2)Boxer.scm` from the install imported by Import (the page's file input handed the file), Melee against a computer, the victory by the cheat, the score screen, Load Replay's `LastReplay.rep` to "The replay has finished".
  - The Fold7 (the debug APK, the inner display): the same map put in Downloads and imported through the system's document picker, Melee, the player's random Zerg against a Protoss computer, the victory by the cheat (typed by `adb shell input`), the score screen, Load Replay's `LastReplay.rep` (553 frames) to "The replay has finished".

### Save Replay after a custom game (after M7, 2026-11-16)

The score screen's Save Replay (`SaveReplayButtonProc` `0x4620c0`) is disabled only as the original disables it: after a replay (`0x66b414`), a game loaded from a save (`0x66b410`; Koprulu's session counts its loads, one session per game) or a campaign mission (WORD `0x526524`: the score data's `mission`). The spawn flag is 0 on 1.17. So the campaign's score screens, the oracle's chains among them, keep the button disabled; after a Play Custom game it opens `RunSaveReplayDialog` `0x4809d0` (`kp_ui_replay_save.c`):
- **The dialog:** `rez\savegame.bin` over the glue. Its frame is the race's `glue\pal<r><d|v>\tile.grp`, which StarCraft: Remastered lacks; each of the six is `dlgs\tile.grp` with its two pixel colours 42 and 41 replaced (Zerg defeat 160/151, Terran 146/134, Protoss 134/122; victory 127/112, 123/106, 145/134), only the RLE's pixel bytes changed, so Koprulu builds it from the data's `dlgs\tile.grp` (`kp_ui_glue_tile_grp`; equal byte for byte to the 1.17 install's six files). The glue's colour over it: translucent, effect set B at 80 % of the default `dlgs\tile.grp` frame 4's pixel (0, 0) (`0x47bd70`).
- **The list:** the replays folder's `*.rep` (`EnumerateReplayFiles`): the name to its last `.`, one of 28 characters or more left out, and `LastReplay` itself. A row's choice copies its name to the edit box (`0x481450`); the box takes the save dialogs' characters (`0x480ac0`).
- **Save** (`SaveReplayAs` `0x4802c0`): the name `LastReplay` (any case) saves nothing and succeeds; an existing file asks network.tbl 3 with the name cut to 230 pixels (`0x480900`), and its Cancel saves nothing; otherwise `LastReplay.rep` is copied to the name. A failed copy shows network.tbl `0x65` in the glue's OK box. A saved replay relabels the button (gluAll `0xb1`) and disables it.
- **Delete, Koprulu's own:** the original hides control 3 (`0x480c80`), and its deletion code (`0x481160`, the dialog's result 3) cannot run. On the user's choice (2026-11-16) Koprulu shows it, enabled while the list has a selection: network.tbl 4 asks (okcancel), the file goes (network.tbl 1 in an OK box when it cannot), and the dialog opens again. It deletes the row chosen, not the original's stale name.
- **The host:** `kp_flow`'s replays folder (`list_replays`, `replay_exists`, `copy_last_replay`, `delete_replay`); a name with a separator, a control character, `.` or `..` is refused.

Tests: `test_kp_ui` (`save_replay_is_a_custom_games`, `save_replay_dialog_names_a_replay`, `glue_tile_grp_recolours_the_frame`, `glue_tile_grps_equal_the_originals` with `KP_TEST_ORIGINAL`), `test_flow.save_replay_writes_the_replays_folder`, `tests/scripts/custom_victory.kps` (saves `Custom win`, Load Replay's list has it and `LastReplay`).

**Open after phase M** (each a `TODO:` where the code has a place for it, or a note in its stage's section):
- The network itself: a transport behind the turn hub, a seat's leave and drop, chat, the lobby's load of a network game's save (M5, M6).
- The live team-game leave (`HandlePlayerLeftGame`'s team branch), the score screen's team roster and `NextSelectedUnit`'s clearing of dashed circles (M5); team games and every network branch are fixture-verified only (the emulator runs no network game).
- `--seats` on the web and the Fold7 (M6); no capture of the original's Play Custom screen in the emulator for `menus_check` (M4).
- The emulator does not play the public replay SSCAIT Ro8 M1G1 (Storm's thread spins after Load Replay's Ok), so it has no golden (M3).
- The twelve melee opcodes no stock script reaches are fixture-verified only (M3).
- The original's own desync risks the shared hash keeps: a cloak begun on an image of drawing method 16, a Psi field made while the pool is exhausted (M6).
