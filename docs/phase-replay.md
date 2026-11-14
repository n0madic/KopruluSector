# Campaign replays (phase RP)

Design: [ADR 0020](adr/0020-campaign-replays.md); the format: [docs/formats/rep.md](formats/rep.md). The user gave the design and approved the plan on 2026-11-13, with these decisions:
1. Save Replay after a campaign game stays disabled.
2. 1.17's command loss is always reproduced.
3. Koprulu's tail goes after the CHK.
4. A save keeps the record in a `REPL` section.
5. The format lives in `kp_app_core` and calls pklib directly.

| Stage | What | State |
|---|---|---|
| RP0 | The emulator: the overlay's files out and in (`vfs.py`), each chain game's `LastReplay.rep` kept, Load Replay through the original's menus (`replay.py`), the research reader and writer (`scdata/formats/rep.py`) | done 2026-11-13 |
| RP1 | The format: `kp_rep.{h,c}`, `test_kp_rep` | done 2026-11-13 |
| RP2 | Record and play: `kp_record.{h,c}`, the session's record, playback, speed and `REPL`, kp_core's replay mode, `LastReplay.rep` in the flow; acceptance (a)–(d) | (a)–(d) passed 2026-11-14 ((b) on 62 games); the flow's playback start is with RP3 |
| RP3 | The interface: Load Replay, the replay's card and buttons, the time panel, the vision dialog, `quitrepl.bin`, `.kps` `replay` | done 2026-11-14 |
| RP4 | Platforms: the replays' folder, IDBFS, import and export | done 2026-11-14 |

## RP0 as built

- **`tools/research/sc_emulator/vfs.py`:** the overlay's files written under the Documents folder are kept and can be put in before the start, so a chain leaves its `maps\replays\LastReplay.rep` and a run can be given a `.rep`.
- **`chain.py`:** a chain keeps each game's `LastReplay.rep`, which EndGame rewrites after every game, as `replays/<k>-m<mission>.rep`.
- **`replay.py`** (`python -m sc_emulator replay FILE OUT [--cd expansion --expansion] [--oracle]`) plays a file through the original's own glue by input: Single Player, the game mode, a new ID, the campaign menu's Load Replay (glue state 12, `gluCustm.bin`), the file picked by its first letter, Ok. It writes the frame digests (`trace.jsonl`), each buffer the dispatcher got from the feed (`dispatch.jsonl`), the end (`replay.json`), the oracle's logs and, with `--oracle`, the state dumps in the shape of `state_check.py --kind replays`.
- **`chain.py --quit`:** the chain's first game is left by the game menu's Quit Mission (F10, E, Q, Return) at the frame limit, and the chain ends at the glue. It showed that a quit game's `LastReplay.rep` is written too, with no 0x57 (replay-network-notes).
- **`tools/research/scdata/formats/rep.py`:** read and write, implode and explode in Python (`test_rep.py`).
- The emulator's findings are in [replay-network-notes.md](research/replay-network-notes.md#later-evidence-phase-rp-2026-11-13).

## RP1 as built

- **`native/kp_app/kp_rep.{h,c}`** in `kp_app_core`: the four blocks and the `KPRX` tail, each block through pklib with raw chunks where imploding does not help; the reader checks every size against the bytes it has and the data still owed, refuses a record over 64 MiB and a CHK over 16 MiB, and reads a file whose tail is damaged without it. The header's fields by `ReadReplayFile`'s accessors; `kp_rep_build_header` makes a campaign game's.
- **`tests/c/test_kp_rep.c`**:
  - `file_round_trip`, `empty_record_and_tail`, `refusals` (every cut and every damaged block);
  - `mutations` (a deterministic mutation run, under ASan and UBSan in CI);
  - `reads_the_originals_file`, `builds_the_originals_header`;
  - `record_blocks`, `feed_loses_commands_as_the_original`, `split_walks_as_the_dispatcher` (RP2's `kp_record`);
  - `split_stops_at_a_cut_last_command` (2026-11-14): a cut-off last command is never run, and a save's or load's name stops the buffer at a control code or past 0x1c bytes, as `ValidateNetString` 0x476e20 does ([notes](research/replay-network-notes.md#a-cut-off-last-command-2026-11-14)).

## RP2 as built

- **`native/kp_app/kp_record.{h,c}`:** the recorder (0x478340, 0x4787e0): the twelve ids not recorded, the 0x100 limit, the growing buffer, the block boundaries, the save's open block (0x478980). The feed (0x4769b0, 0x478480): the early read, 1.17's loss, the last block never dispatched. `kp_record_split` walks a buffer as `ExecuteTurnCommands` 0x475d30, including the name check of commands 6 and 7 (no control codes, at most 0x1c bytes with the terminator).
- **`kp_session.{h,c}`:**
  - **Recording:** `KpSessionStart.record` records the local player's commands at `KP_FRAME` before the step.
  - **Playback:** `kp_session_replay_start` starts from a file's header with no briefing (`objective_count` 0). It refuses a file whose player record with a Storm ID names a slot past the eight players (`test_kp_rep.player_slot_past_the_players_refused`): the feed would give its commands that player. It also refuses a record with a network game's 0x57 or 0x5c, or with any id the recorder never writes (save 0x06, load 0x07, restart 0x08 and the playback's own; since 2026-11-14, after a security review: a crafted file's save would write over the player's saves), wherever the dispatcher would meet one (`test_kp_rep.unrecorded_commands_refused`). A file's replay speed 0x56 may give any multiplier; the game keeps it as the original does, and the wall clock paces at most 16 and never steps under 1 ms (`KP_CLOCK_MAX_MULTIPLIER`, `test_session.clock_speed_table`). `KpSessionStart.replay` plays a file: only the live ids pass during the playback, and at the header's frame count the game pauses (`replay_ended`; the victory dialog is RP3's).
  - **Speed:** the replay's speed and pause (0x56) through the clock, up to 16×.
  - **Saves:** the `REPL` section in the save, `kp_session_replay_file`. A damaged `REPL` refuses the load before the game changes. The record is restored only after `kp_load` took the save.
- **kp_core:**
  - **The replay mode** (`w->replay`): the commands 0x10, 0x11, 0x55, 0x56.
  - **Vision:** full vision set after the map's load (StartGame 0x443f40 step 13; 0x67cc84 is 0 during the load). The replay's viewer rule for the creep's shown bit and the remembered terrain (0x4b4040, 0x4b4560, 0x40af30, 0x40b310).
  - **No result clock:** the result dialog's clock (0x4be340) does not run in a replay.
- **`kp_flow.c`:** the recorded game's header with the profile's name and the computers' names (0x4074a0), the tail, and `<pref>replays/LastReplay.rep`. The file is written once, when a game that reached its console ends. A ready room's Cancel writes none, so it does not replace the last game's file with an empty one.
- **The oracle:** `KP_REPLAY` / the golden's `"replay": 1` start the oracle's game in replay mode; `state_check.py --kind replays` compares the dumps of the original's playback (`replay.py --oracle`) with Koprulu's (`tests/golden/oracle/replays/`).
- **Acceptance:**
  - **(a)** Every golden scenario (160), play (69) and idle run (64), recorded by a session and played back, equals the recording at every step. This is `test_session.replay_scenarios_play_back`, `replay_plays_play_back` and `replay_runs_play_back`, run with `replay_as_game`: the replay's flag changes presentation fields, see ADR 0020. The suite compares the plays and runs every `CHECK_EVERY_LONG` steps; with `KP_REPLAY_EVERY=1` at every step, run once on 2026-11-14 (all four replay tests passed, 2 h 37 min; the build before the review's fixes of the same day, after which the suite's every-16-steps run passed in the full CI).
  - **(b)** The original's playback of its own files equals Koprulu's replay mode at every step: terran01, zerg01 and Brood War's protoss01 (345, 647 and 647 steps), and, since 2026-11-14, the 59 more games of the campaign chains (every mission won with the cheat, the seven hidden rows and terran02 lost), 62 in all. This is `test_scenarios.replays_equal_the_original`; the goldens hold the transmissions' interface events at the original's frames.
    - The chains rerun with each game's `LastReplay.rep` kept gave 65 files (`koprulu-private/emu/rp/chains`, `run.sh`), played by the original through Load Replay with state dumps (`play.sh`, `replay.py --oracle`).
    - 27 of the 59 first differed at step 2, in the tiles' creep-shown bit and the remembered terrain of a 21 x 14 rectangle. The pre-game objects step's remembered-terrain refresh (`RefreshRememberedTerrainOffscreen` 0x40af30) leaves the screen's tiles out, and the original's screen is already set by the map's load (`HandleStartLocationRecord` 0x46a280: the local player's start location's tile less (10, 6), in a replay the first start location's while the screen is still 0xffff); Koprulu's screen came only with the first draw, so the rectangle left out was (0, 0). A game never showed it: before the game the local player sees no tile, and the refresh passes over unseen tiles; a replay's viewer sees what any player sees. Fixed in `load_units` (`kp_start.c`); the interface's first view of a replay (`kp_ui_start_screen`) is now the same start location's.
    - Three files (orig-terran m10, orig-protoss m28, orig-zerg m15) are left out. Right after a transmission's wait ends (frames 2, 2 and 219), the original's playback in the emulator calls 0x480e00 (the end of the replay) and its frames stop, though their headers hold 354 to 360 frames; the emulator then runs the turn loop at that frame until its wall limit. TODO: find whether the original ends these replays so or the emulator's sound end does it.
  - **(c)** The same three games recorded by Koprulu give the original's record and map byte for byte, and its header except the names (`test_session.session_replay_records_as_the_original`, private files).
  - **(d)** The original played Koprulu's `terran01.rep` to its end (frame 323) through Load Replay, and its frame digests equal those of its playback of its own file at every frame.

### Differences from the plan

- **Golden size:** acceptance (b) runs on the original's files of the campaign chains (62 games, every mission once), not on the 69 plays' files. A play's game is a real win of thousands of frames (727,781 frames in all) and a playback golden is a full run of the emulator (about 2 minutes per 300 frames); the chains' games end by the cheat after about 320 frames.
- **(a) uses `replay_as_game`:** the replay's own flag changes presentation fields (the shown creep, the minimap's blink, the result clock), as in the original.
- **Our file ends earlier:** our recording of terran01 ends at frame 323, not the original's 369. A session has no interface to end a transmission's wait (`kp_trigger_release_wait`), so the triggers reach the result at another frame. The record's bytes up to there are the original's.

## RP3 as built

- **Load Replay** (glue state 12, `kp_ui_menus.c`): the campaign menus' button opens `gluCustm.bin` as `RunCreateGameScreen` 0x455550 with ECX = 1 does.
  - Play Custom's game type, subtype, speed and slot controls are hidden.
  - The list is the listed folder's under `<pref>replays/` with `iCreate.grp`'s icons (`BuildMapList` 0x45ba40): below the root "[Up One Level]" first, then the folders as "[name]" (by `_stricmp` of that text), then the files. Ok on a folder enters it and the folder label shows its last name (`OnCreateGameOk` 0x455240's 0x8000000b, `SetFolderLabel` 0x4560d0); each opening starts at the root (`InitMapList` 0x455aa0). `test_flow.load_replay_lists_refuses_and_plays_the_last_game` lists a folder's replay and accepts it, `test_hud.load_replay_enters_folders_and_goes_up` goes in and back through the screen. The entry is previewed 100 ms after its selection (0x45b970), and its header fills the information fields (0x45d250, 0x45dfc0).
  - Ok reads the file again: an unreadable one is refused with gluAll 51, a refused read with 60, and so is a map that is no catalog mission (outside V1). After a refusal the screen stays.
  - Ok then starts the replay's game at once, without a ready room (`kp_flow.c` `KP_GLUE_REPLAY`).
  - A game whose `KPRX` tail names another simulation revision, other data or saves loaded shows one of Koprulu's labels "Unverified: other version", "Unverified: other data" or "Unverified: saves loaded" (controls 40, 41, 39, `defs/ui/koprulu-dialogs.json`'s `extend`, translated in `defs/l10n/`) in the information fields (decision 4; `test_flow.load_replay_lists_refuses_and_plays_the_last_game` writes each).
- **The console of a replay:**
  - **The local race:** `kp_ui_local_race` gives Terran (0x5264c2 = 1) for `cmdbtns`, `pbrempt`, `textbox`, the music and the voices. The letter `n` is used for `nconsole.pcx`, `statbtnn.bin` and `nconover.pcx` (`LoadConsoleBitmap` 0x4a7e20, `InitPortraitDialog` 0x4a67a0).
  - **The card** (`statbtnn.bin`, sets 248/249): the buttons Slower, Faster and Pause (0x480e50…0x480f90) send 0x56, with their states and icon offsets (0x4a0da8). The buttons have no `cmdbtns` background (`DrawCommandButton` 0x4a0c00).
  - **The time panel** (`UpdateReplayInfoPanel` 0x4a5640): the progress bar, "Replay Progress", the elapsed time (0x480d70) and the speed or "Paused", refreshed each second, on a new speed and by the pause's blink (0x480fb0, 0x481040).
  - **The minimap** (`DrawMinimapUnits` 0x46d510): every player in its colour, no local pass. Ghosts are skipped on tiles none of the vision players explored, unless reveal-all is on (0x418520 mode 1).
  - **The vision dialog** (`allyfltr.bin`, `OpenAllianceDialog` 0x441b70): one box per player, applied through `kp_replay_set_vision` (0x67cc84, 0x67cc88).
- **The message line:** in a replay a line that is no cheat goes to `HandleReplayChatCommand` 0x480be0: "/replay show NAME" sets the leaderboard of table 0x4ef6a8 (kills, razings, slaughter, resources, minerals, gas, score and its five kinds; `ReplayShowLeaderboard` 0x47fd60) through `TrigActLeaderboard` 0x4b67c0 of a zeroed action (`kp_replay_leaderboard`), and "/replay hide" clears it (0x47fe10). `test_hud.replay_line_shows_and_hides_the_leaderboard`.
- **The menus:**
  - The game menu hides Save and Load (0x47b4c0).
  - The abort menu hides Restart and labels Quit "Exit Replay" (0x47b140).
  - Quit opens `quitrepl.bin` (0x47b380, 0x47bed0).
- **The end:** at the record's end the session sends a result of kind 3 (`KP_REPLAY_RESULT`). The victory dialog then shows "The replay has finished." with Exit Replay (`InitVictoryDialog` 0x4bd220: gluAll 0xb5, 0xbc).
- **The score screen of a replay:** index 1, title 0xb2, the elapsed time by 0x480d70, and the humans' names from the slots.
- **Scripts:** the action `replay "NAME"`, the condition `replay`, and the values `replay.ended`, `replay.frames` and `sim.crc` (the state's CRCs without the step's result clock, the selection, the sprites and the images: a playback has no local selection or markers).
- **Data:**
  - `defs/ui` lists `nconover.pcx`, and the input manifest has the new rows.
  - Remastered has no `rez\statbtnn.bin`: `defs/scr/texts-bw-1.17.0.1.json` now rebuilds it from Remastered's texts (byte for byte the 1.17 file). Its `SD/game/nconover.DDS` gives `nconover.pcx`. Remastered's HD `glue/create/icreate.dds.grp` is in the HD interface's placement table (`defs/scr/hd-ui-bw-1.17.0.1.json`, `kp_convert hd-ui-table`).
- **The converters' goldens** (private, recorded before stages C3 and C6 removed the Python converter) were brought up to date on 2026-11-14, field by field:
  - `tests/golden/hdui/converter.json`: the asset `glue\create\icreate.grp` (5 frames in each variant) is added, the later assets' first frames are shifted, and the counts and the sections' digests are updated. The 60 earlier assets equal Koprulu's output in every frame's geometry and SHA-256, so they are the converter's still.
  - `tests/golden/locale/converter.json`: each language's `DLGS` and `POOL` digests changed (the label 39). The tables, scripts, subtitles, missions and glyphs did not change.
- **Tests:**
  - `test_flow.load_replay_lists_refuses_and_plays_the_last_game`.
  - `test_session.session_replay_plays_the_recorded_game`: the result of kind 3, and the vision with reveal-all.
  - `test_kp_ui`'s button states.
  - `tests/scripts/replay.kps` (in `tools/ci.sh`'s scripts): terran01 recorded, Quit Mission writes `LastReplay.rep`, `replay` plays it to the marked step with the same `sim.crc`, the console is `statbtnn`, and the record's end opens `wmission` with "The replay has finished.".
- **Against the emulator** (`replay.py` with `--shots`, `--keys` and the `shot` event's vision fields, terran01; private captures `koprulu-private/emu/rp/ui-terran01`):
  - The card's buttons, the time panel, the minimap, the portrait and the end dialog equal the original's frames except the cursor.
  - With the original's key presses replayed by the script (Faster at frame 61, Slower at 122 and 142, Pause at 158 and 161), the whole 640 × 480 screen at frames 40, 100 and 200 equals the original's except the two cursors (302 pixels). At frame 200 the portrait's talking frame also differs (466 pixels): the portrait's animation runs on the interface's millisecond timers, and the emulator's clock is virtual, so its frame at a given game frame is not the same; the game's state and the time panel are equal.
  - Two console fixes came from these frames:
    - **The holes beside the minimap:** the minimap dialog's background is captured from the console (`CaptureConsoleBackground` 0x4a7c40) and drawn opaque (`DrawDialogBackground` 0x4c37f0), so `nconsole.pcx`'s zero pixels in its box (130..137 × 315..323) showed black. The original shows the game there (its zero is the game's transparent index, as `zconsole.pcx`'s zeros show in the Zerg frames). `kp_ui_canvas.c` leaves a captured dialog's pixel transparent where the console's own pixel is zero.
    - **The fluff over MENU:** `CreateFluffDialogs` 0x4a5a40 makes one plain dialog per rectangle of the race's list (0x50d7b0), and the replay's list (index 3) has (138, 367, 358, 22), which covers row 388 above the menu button (416..479) where the races' rectangles end at 387. The lists are the `FLUF` table in the generated definitions (`tools/defs_c.py`); `kp_canvas.c`'s `compose_fluff` repeats the console's art in them over the panels. `test_hud.replay_console_keeps_its_holes_and_fluff` checks both on terran01's console.

- **The HD console of a replay** (2026-11-14): Remastered's art for `nconsole.pcx` is one picture, `game/consoles/neutral/console.dds` (HD2 1706 × 350, HD 3412 × 700), not a race's three pieces.
  - The placement table takes it (`kp_hd_ui_scr_classify`'s `KP_HD_UI_SCR_NEUTRAL`, regenerated `defs/scr/hd-ui-bw-1.17.0.1.json`).
  - The layout makes it a console asset (`KP_HD_UI_CONSOLE`, its own silhouette) with one frame. The columns are centred on the 640 (x −106.5 logical, as the races' left and right pieces lie at a 853-wide screen's edges). The first row is 306: the picture's luminance gradients against `nconsole.pcx`'s agree best there (measured on HD2 at whole and half pixels; 305, the bottom edge's row, agrees less).
  - The interface marks the console as one picture (`KpUi.console_whole`), so every group (`kp_canvas.c`'s `piece_item`) and every dialog background captured from the console take frame 0.
  - Checks: `test_hd.ui_equals_the_converters` (the golden's new asset; the golden is private), `test_hud.replay_console_keeps_its_holes_and_fluff`, and `tests/scripts/hd_replay.kps` in `tools/ci.sh` over the install and the repack (the replay's screen with no HD art missing: before the fix 44,862 of its pixels had none).

### Open in RP3

- **The fluff in the races' pieces:** a replay's console has no pieces (no `CONW` entry for `n`), so every layout draws it as the original's, its fluff included, in SD and in HD. `compose_fluff` draws nothing in the races' piece layouts (TODO in `kp_canvas.c`); the races' rectangles end above their panels, so nothing is missing there today. The original's 640 × 480 frames are the only reference.
- **The neutral console's other HD art** (settled 2026-11-14): `conover.dds` stays SD by design. Remastered drew it anew, as it did the races' (64 × 64 or 64 × 69 against SD's 60 × 56; the best gradient agreement 0.48–0.72, below the table's `SAME_PICTURE`; mapped, the placement check finds 132 pictures drawn anew). `dlg.dds.grp` has no 1.17 counterpart: a replay's dialogs use `dlgs\terran.grp` through `kp_ui_local_race`. The comment above `CONSOLES` in `kp_hd_ui_scr.c` says so.

## RP4 as built

- **The replays' folder:** it is `<pref>replays/` on every platform, made by the flow when it writes `LastReplay.rep` and when Load Replay lists it. `<pref>` is SDL's preference folder on the computers and the phones, and the script's own `DIR/pref/` under `--script-out`.
- **The web:** `<pref>` is the IndexedDB mount `/prefs` (`kp_app_main.c`). The flow calls the host's `sync_files` after it writes the file (`FS.syncfs`), so a replay stays like a save does.
- **Import and Export** (Koprulu's buttons on Load Replay, IDs 42 and 43 in `defs/ui/koprulu-dialogs.json`'s `extend`, translated in `defs/l10n/`):
  - They are shown only when the platform has a file picker (the flow host's `pick_replay` and `export_replay`, `native/kp_app/sdl/kp_app_pick.c`). A scripted, hidden or checking run and iOS (SDL has no file dialog there) offer none, so the screen there is the original's and the oracles' captures stay valid.
  - **The computers and Android:** SDL's open and save dialogs with a `.rep` filter. SDL may answer on another thread; the answer waits under a lock for the next frame, which imports or writes on the main thread. Android's answer is a `content://` URI, which `SDL_LoadFile` and `SDL_SaveFile` open through the content resolver. The save dialog starts in the documents folder with the replay's name, where the platform's dialog has a name field.
  - **The web:** a file input (`accept=".rep"`) whose file goes through the memory file system to the flow, and for Export the File System Access API's save picker (the player names the file) or, without it, a download under the replay's name.
  - **Import** (`kp_flow_import_replay`): the bytes must read as a replay (`kp_rep_read`), else Load Replay's Ok popup refuses them (gluAll 63, or 60 for the expansion's). The copy goes into the listed folder. Its name is the picked file's last part: a URI's escapes are undone, and characters no file system takes become `_`. Trailing spaces and dots are dropped, and ".rep" is kept or added. A taken name gets " (2)" … " (99)". The stem is cut at a character's start until the name's legacy form is below the original's 32 bytes (the list's own limit, `LoadReplayEntryInfo` 0x45d250). The list is rebuilt with the copy selected. The flow calls `sync_files`, so on the web the copy stays.
  - **Export:** the selected file's bytes under its name (a folder's nothing). This is how a campaign replay gets a name while Save Replay stays disabled (decision 1).
  - Tests: `test_flow.load_replay_imports_and_exports` (the names from paths and URIs, the copy byte for byte, a taken name, a long name, a folder, a refused file, Export's name and bytes) and `test_hud.load_replay_imports_and_exports_through_the_host` (the buttons absent without the picker, Import and Export through the host, the copy selected, a refusal's popup).
  - **In the browser** (2026-11-14, the debug web build in the browser pane): terran01 played and quit wrote `/prefs/replays/LastReplay.rep`. After a reload of the page it was still listed. Import, with the file input given `terran01.rep`, copied it as "My terran game.rep". Export handed the save picker the selected replay's name and its bytes, equal to the imported file. A second reload kept both files.
- **On the Fold7** (2026-11-14, the debug APK):
  - The first run showed Android's names wrong. The downloads provider's URI (`…/document/msf%3A2139`) named the copy "2139.rep". SDL's Java side gives the save dialog no name (SDL drops the default location on Android) and the type `*/*` for the unknown `.rep`, so a taken name became "Phone test.rep (1)".
  - Fixed through Koprulu's lines in SDLActivity (`tools/native/android_app.sh`, template edition 5). `kpDisplayName` asks the content resolver for the document's `DISPLAY_NAME`, which `kp_app_pick.c` passes to the import in place of the URI. `kpSetFileDialogName`, set before `SDL_ShowSaveFileDialog`, becomes the intent's `EXTRA_TITLE`. A save's `*/*` becomes `application/octet-stream`.
  - Then Import, with "Phone test.rep" picked in Downloads, copied it as "Phone test.rep" and selected it. Export's dialog showed "Phone test.rep"; the provider saved it as "Phone test (1).rep", byte for byte the picked file. The imported replay played to its end (56 frames, "The replay has finished.").
- **Checks:**
  - `tools/ci.sh` runs `replay.kps` natively in the checks' scripts.
  - With `KOPRULU_WEB` and `KOPRULU_SCR` it also runs on the web in headless Chrome (`web_run replay`: 0 failures and the `replay: …/replays/LastReplay.rep` line).
  - With KOPRULU_SCR, `hd_replay.kps` plays a replay with the HD interface (see "The HD console of a replay").
  - The same game writes the same file: the scripts' play_4 runs over the bundle and over the 1.17 install compare their profile folders byte for byte (`tools/ci.sh`), `LastReplay.rep` among them. The first full run found the imploded bytes differing (pklib's work buffer not cleared, see `docs/formats/rep.md`); fixed in `kp_rep_chunk`.
  - The web build compiles `kp_rep.c` as C99 (no `strnlen`).
- **On macOS by hand** (2026-11-14, the debug bundle, a live script with the picker let in for the test):
  - Import opened the system's open panel as a sheet of the window; "Mac picked.rep" picked in another folder was copied byte for byte into `pref/replays/` and selected, with T1 Wasteland in the information fields. A cancelled panel changed nothing. Export opened the save sheet with "Mac picked" in Save As and Documents as the folder.
  - Export's Save, pressed by the user in the product bundle, wrote the file. Now and then the system's arrow stayed over the game's cursor after the sheet closed, until a click. SDL hides the system cursor on macOS by the window's cursor rectangle (an invisible cursor), not by `NSCursor hide`; the sheet sets its arrow, SDL still counts the cursor hidden (so `host_cursor` changes nothing), and AppKit looks at the rectangle again on a mouse event, not when the sheet's closing animation ends. `kp_app_pick_frame` now redraws the cursor (`SDL_SetCursor(NULL)`, which invalidates the rectangles) every frame for a second after a dialog answers. The user checked it in the rebuilt release bundle: the arrow no longer stays.
- **Open:**
  - Linux's and Windows' SDL dialogs (portal or zenity, the common item dialog) were built, not opened.
