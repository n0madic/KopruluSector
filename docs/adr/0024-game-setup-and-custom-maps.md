# ADR 0024: The game's setup, templates and custom maps

Status: accepted (2026-11-15). The user approved phase M's design inline on 2026-11-15 ([phase M](../phase-multiplayer.md)): the game's setup in the start input and the game, the templates and victory trigger files in the data, custom maps outside the catalog, the map in saves and the replay header's template and slots. Stage M1 is built (2026-11-16), the melee start and victory of stage M2 on it (2026-11-16, decisions 9–16), and the Play Custom screen of stage M4 (2026-11-16, decisions 17–22). Phase M's acceptance (stage M7, 2026-11-16) passed: [phase M](../phase-multiplayer.md#m7-acceptance).

## Context

Until phase M the native core played the campaign and nothing else.

- **The start knew no game.** `kp_start` took the slots' owners straight from the CHK (`players()`), and `KpStartInput` had no game type, no slots and no chosen races. The original builds the game from a lobby: a 32-byte template record at `0x67293d` (the game type, subtype, victory, resources, starting units, fog, allies and the rest) and twelve player records at `0x6726c8` (`StartGame` `0x443f40` and its slot steps).
- **Maps came only from the catalog.** A mission needed its MPAK entry, and the CHK loader tables had no melee group: a melee map's CHK has no triggers, briefing, settings or locations.
- **Replays were campaign games.** The header held a fixed Use Map Settings template, one slot and `KP_REP_NOT_CAMPAIGN` refused any other replay.
- **A save named its mission by its key.** A game of a map outside the catalog could not be loaded again.
- **No data for the custom games.** `templates\templates.lst`, its `.got` files and `triggers\*.trg` were not in the input manifest.

## Decision

1. **`KpGameSetup` is the game's setup** (`kp_start.h`): the template record (`KP_TEMPLATE_SIZE` bytes, the layout of `0x67293d`), the multiplayer byte (`0x672664`, 0 offline and in every replay), the map's player slots the lobby offered (the game header's `+0x25`), and per slot its Storm ID, type, race, force and name. `KpStartInput.setup` passes it (NULL: the campaign's, made from the map: the Use Map Settings template, the CHK's humans and computers). The game keeps a copy (`g->setup`). It is saved and hashed as the walk's section `SETP` (`KP_SAVE_VERSION` 4).
   - The lobby's slot types are the original's: 5 a lobby computer, 6 open, 8 closed (`kp_ids.h`). `StartGame`'s slot steps turn them into the started game's (decision 9).
   - `local_player` stays the host's: neither saved nor hashed.
2. **The CHK's groups by the game** (`kp_chk_load`, `chk.py`'s `load(groups=..., game=True)`): a campaign load reads the header, briefing and Use Map Settings groups and refuses what it does not read, as before. A game's load (`KP_CHK_MELEE`: header and melee groups; `KP_CHK_UMS`: header, briefing and Use Map Settings) is the original's `0x469970`:
   - unknown tags are skipped;
   - each handler runs per chunk: DIM, ERA, OWNR, SIDE, STR, SPRP, FORC, VCOD, UPRP and MRGN keep the last, MTXM and MASK lay the chunks over each other (the defs' `repeat`);
   - the other repeats are refused.
3. **A custom map's mission** (`kp_mission_from_map`) reads the scenario for the game's load:
   - a melee load has no triggers, briefing, settings or locations, and its fog mask is all 0xff;
   - its strings are checked as the game reads them (a melee map's SPRP and FORC);
   - its WAV strings name files of the map's own MPQ;
   - it uses every race's packs (`KP_DATA_MAP_PACKS`).

   `KpMission.load` records the load. `KpDataMap` (`kp_data.h`) opens an `.scm`/`.scx` through `kp_mpq_open`, or holds a scenario alone (a replay's, a save's). Its files read the map's MPQ first, then the data's.
4. **The templates and victory files are data** (`kp_template.{h,c}`, `KpData.templates`, `KpData.victory`):
   - `ForEachTemplateFile` `0x447740` with `NextQuotedString` `0x447950` and `ReadTemplateFile` `0x4482a0`: 97 bytes of version 3, a type below 0x81 and a subtype below 8;
   - `FindTemplateRecord` `0x447b50`;
   - the eleven `.trg` files of `SetupVictoryConditions` `0x448530`.

   They are in the input manifest and the repack. `kp_res` checks them to the byte as simulation inputs. A data set without `templates.lst` does not load, as the original's "Unable to read game templates." is fatal. The campaign's template is `Use Map Settings(1).got`'s record (`LoadUseMapSettingsTemplate` `0x447b90`), which `KP_TEMPLATE_UMS` equals (`test_data.templates_of_the_data`). Remastered keeps `templates.lst` and the `.trg` files at its root but the `.got` files only under its enUS locale (`locales/enUS/Assets/Templates`), which an install without that language has not downloaded. A missing `.got` is skipped, as the original skips one (`ReadTemplateFile` returns 0), so on such an install the type list is empty: Play Custom's Ok shows gluAll's "The selected map does not support the selected game type and options." and `--map` stops with "no game type". Both twins call those files `missing` with the name the build lists (`test_scr_source.test_every_manifest_file_is_classified` allows only those).
5. **A save of a custom map's game carries the map** (container section `MAPC`, written when `KpMission.load` is not the campaign's): the scenario as read, its load, and the setup the game started from. The loader starts the same game from them, then loads the state (`kp_save_map`, the flow's `start_saved_game`). The native save's mission name is the entry's key, `maps/<name>`.
6. **The replay header is the setup** (`kp_rep_header`, `kp_rep_setup`):
   - the template at `+0x81`, its type and subtype again at `+0x3c`;
   - the slots at `+0x39`;
   - the player records.

   A replay of campaign row 0 is a custom map's game: Load Replay plays it from its own CHK, read for its template's load (Use Map Settings or melee), with the setup from its header. A map its game does not load is refused at Ok (gluAll 51). `KP_REP_NOT_CAMPAIGN` is gone. A network game's commands (0x57, 0x5c) are still refused until stage M5.
7. **The flow plays a custom map as an entry of its own** (`KP_FLOW_CUSTOM`, `custom_entry`, `custom_map`). Its key is `maps/<name>`. It has no campaign run and no files to wait for.
8. **A game that does not start tells the player.** The load screen shows gluAll 66 and Load Replay gluAll 51 in an OK popup once the screen slid in (`kp_ui_menus_refused`). This closes ADR 0023's TODO at `KP_GLUE_LOAD`.

### Stage M2: the melee start and victory

9. **A lobby's setup is started as `StartGame` `0x443f40` starts it** (`kp_gametype.c`, `kp_start`). `KpGameSetup.started` says whose records a setup holds: 0 the lobby's, 1 a started game's (a replay's header, a save's `MAPC`, a campaign's from its map), which the start leaves. From a lobby's records the start:
   - takes the single-player custom races the lobby applied (`ApplyCampaignSlotRaces` `0x443b90`, `kp_setup_custom_races`: the lobby computers open, each race turns the last open slot into a computer);
   - flags the slots whose map race was User Select (`LoadMapHeader` `0x46b5c0`: `0x673f08`, the replay header's `+0x271`);
   - draws with the game RNG the random races (`ResolveRandomRaces` `0x478ee0`, `PickRandomRace` `0x47a030`), the slot types and starts (`SetupGameSlots` `0x479340`, `RandomizeStartSlots` `0x479a00`, Use Map Settings' `ShuffleUmsForceStarts` `0x479430`) and the colours (`LoadPlayerColorTables` `0x41d570`, `ShufflePlayerColors` `0x41d460`), then reseeds (step 7);
   - keeps the started records in `g->setup` (`started` 1): the replay header and `MAPC` hold them, with the colours and user-select flags (`MAPC`'s setup bytes grew by both);
   - sets the briefing's player to the lobby's local slot (`g->briefing_player`), and the music's rotation from the CRT generator at the first game (`LoadConsoleSkinAndPickMusic` `0x443680`, `g->music_rotation`), carried to the next game as `KpCarry.music_rotation` (the replay tail's version 2, `KP_REP_SIM_REVISION` 2).
10. **The melee units and resources.** The melee filter `0x469600` and Use Map Settings' user-select gate leave map units out (`kp_gametype_unit_excluded`). `ApplyStartingResources` `0x448420` gives the template's resources, which `InitPlayerAiData` `0x4830d0` copies into the AI's records. `CreateStartingUnitsAndView` `0x469e90` makes each slot's start units (`CreateMeleeStartUnits` `0x47e100`: the town hall `0x47df00`, four workers, a Zerg's Overlord `0x47e080`).
11. **Alliances and vision** (`InitAlliancesAndVision` `0x477610`, whole): each player allied with itself and player 11, the computers with each other when the template allows alliances (`0x4474d0`), rescuable and neutral players with everyone, Use Map Settings' forces among their humans, Top vs Bottom's sides (`0x447e70`).
12. **Victory** (`SetupVictoryConditions` `0x448530`): outside Use Map Settings the computers' melee AI scripts start (`StartCustomLevelAIScripts` `0x4486a0`; their opcodes are stage M3's), and the template's `.trg` replaces the trigger lists (`0x447f70` through `ExpandTriggerOwners` `0x4bdb00`). Greed and Slaughter pick the file by the template's value; an unknown value or a missing file is the original's error `0x5d`. Capture the Flag makes the beacons first (`CreateFlagBeacons` `0x4480f0`, `CreateFlagBeaconForPlayer` `0x448210`); the beacons' orders and the flag messages are `kp_ctf.c` (`0x421440`, `0x4213c0`, `0x4214d0`, `0x421650`, `0x4216e0`, `0x421760`).
13. **The triggers only custom maps use:** Opponents (`CountOpponents` `0x4ba060`: a coalition's distinct marks), Modify Unit Resource Amount (`0x4b5d10`) and Modify Unit Hangar Count (`0x4b5dc0` with `0x4b8050`). ADR 0023's refusals of them, of a Flag created or preplaced, and the converter's lists are gone.
14. **A map string past STR** (`kp_map_string`, `kp_mission.c` `map_string_in`, `kpm.map_string`). The original copies the last STR chunk (`LoadStringSection` `0x468ce0`, no minimum length) and reads its heap past the copy. Koprulu's copy has a zero byte past STR: a string whose offset is past STR reads empty, one STR does not end ends at STR's end. This replaces ADR 0023's `validate_strings` refusal. Protected melee maps hide their strings this way: 13 of the 40 public replays' maps end with a STR chunk one byte long, and the original plays them.
15. **A replay's chat** (command `0x5c`): the game plays sound 23 and gives `KP_EVENT_CHAT`; the interface formats the line as `DisplayChatMessage` `0x46a860` (the local player's code 7 or another's 2, usechatcolors `0x1000` by the sender's minimap colour, an observer's tag). Load Replay plays a replay holding it. The leave command `0x57` is still refused (stage M5).
16. **The oracles of custom games** ([golden oracle](../golden-oracle.md)): `state_check.py --kind custom` (the emulator's Play Custom games: a map of the install with the lobby's setup) and `--kind public` (the emulator's playback of the public replays: `kp_oracle` reads a `.rep` key's scenario and header, `maps/<file>.rep`).

### Stage M4: the Play Custom screen

17. **Play Custom is the original's screen** (glue state 11, `RunCreateGameScreen` `0x455550` on `rez\gluCustm.bin`, shared with Load Replay). The UI owns the lists, the template and the races; the flow's hooks give it the folder's entries (`list_entries`: the user's maps folder before each data folder's `Maps`, `ScanMapDirectory` `0x45c9a0`), an entry's header (`load_entry`, `LoadMapEntryInfo` `0x45d870`) and the check at Ok (`check_map`, `CreateGameFromEntry` `0x45bfc0`, refusing with gluAll 51 a map whose game does not load).
18. **The combo box** (control type 13, `ComboBoxProc` `0x4c63b0` and its draw) is ported in `kp_ui_list.c` over the list box's items. The original's offline lists are kept as they are: Melee, Free For All and Use Map Settings, Free For All by default (`FillGameTypeList` `0x456910`), and the registry's last choice is kept in the profile's `CUST` (schema 9).
19. **The lobby comes from the screen** (`ApplyCustomGameRaces` `0x455020` into `kp_setup_lobby`, decision 9). A melee template's game starts at once, any other from the ready room of the player's race.
20. **The custom mode** (`0x6ca71c`) is the flow's `KP_FLOW_CUSTOM` outside a replay. After the score screen `CreateNextScenarioGame` `0x4bb360` plays the next scenario's map from the finished map's folder, with Use Map Settings, or opens the campaign menu. A failure is network.tbl's `0x6a`, deferred to the campaign menu as the original's `FlushDeferredNetError` `0x474980` shows it.
21. **Maps come in by Import** (Koprulu's, record 44 of the screen's `extend`, shown with the platform's file picker). Load Replay's picker takes the kind of file (`KpFlowHost.pick_file`). A picked map is copied into the user's maps folder under the listed folder and read as the list reads it; a file that is no map is removed again. The web and the phones have no install maps, so this is how they get one.
22. **`--map PATH [--map-template NAME]`** plays a map at once without the glue, with the lobby Play Custom's screen offers: a melee template's player and computers all Random, another template's map slots.

## Consequences

- Campaign games, saves and replays are unchanged:
  - a campaign's setup is made from its map;
  - a campaign replay's header is byte-identical (`test_session.session_replay_records_as_the_original`);
  - the campaign oracles do not move.
- A save of version 3 or older is refused as another build's (ADR 0021: saves do not migrate).
- A custom map's game is tested end to end on a scenario read as Use Map Settings (`test_session.session_a_custom_maps_game`): setup kept, replay header, playback from its own CHK, `MAPC`, a load into the game started again. Load Replay plays a custom replay (`test_flow.load_replay_plays_a_custom_maps_replay`).
- The install's 211 maps load for melee and for Use Map Settings (`test_chk.the_install_maps_load`; 209 and 201 before stage M2 lifted the Flag and Opponents refusals).
- The emulator's Play Custom games start as the original's (the 11 recorded: melee and Free For All with two to eight players, random races, closed slots, Use Map Settings), and the Use Map Settings games play as the original's to frame 2001 (`test_scenarios.custom_games_equal_the_original`). Since stage M3 (the melee AI) the melee and Free For All games play as the original's to frame 2001 too.
- The public replays start as the original's, and their play is compared to frame 2001 (`state_check.py --kind public`, [phase M](../phase-multiplayer.md)).
- The Play Custom screen's lists and lobby equal the original's for the 13 recorded games (`test_flow.play_custom_screens_are_the_originals`), and the product plays a melee game through it and replays it (`tests/scripts/custom_melee.kps`, stage M4).
- Deferred:
  - a team game's slots, start units, beacons and strings (stage M5): built since stage M5d ([ADR 0025](0025-seats-and-network-branches.md) decision 9);
  - the leave command `0x57` in a replay (stage M5): plays since then ([ADR 0025](0025-seats-and-network-branches.md) decision 2);
  - the next scenario of a loaded save's custom game, which has no map folder (`TODO` in `kp_flow.c`'s `custom_next`).
