# Replays and network play: what the original game shows

Replays and network play were outside the first version of the remake and are now planned. This page collects what can be extracted from the original 1.17.0.1 game for them, what the [atlas](atlas/README.md) already specifies, and what is still open. Items marked *check* are leads from outside material that are **not yet reproduced** on the original; each is to be confirmed with the [full-game emulator](full-game-emulation.md) or the disassembly before the remake relies on it. Everything else is in the atlas.

## What the atlas already specifies

| Area | Atlas entries |
|---|---|
| Command recorder and replay feed ([P26](atlas/p26.md)) | `RecordCommand` 0x478340, `ReadRecordedFrame` 0x478480, `GrowRecordBuffer` 0x4785c0, `LoadRecordedCommands` 0x478640, `SaveRecordedCommands` 0x478980, `FeedReplayCommands` 0x4769b0, `BeginReplayPlayback` 0x4789f0 ([P27](atlas/p27.md)) |
| Turns and lockstep ([P26](atlas/p26.md)) | `ReceiveTurns` 0x475ac0, `ExecuteTurnCommands` 0x475d30, `SendTurns` 0x476500, `ExecuteGameTurn` 0x4768a0, `TurnManagerStep` 0x476c00, `QueueTurnCommand` 0x476c80, `WaitForLaggingPlayers` 0x47a810 ([P27](atlas/p27.md)) |
| The original's sync check ([P27](atlas/p27.md)) | `QueueSyncCommand` 0x47a300, `ComputeSyncValue` 0x47a360, `CheckSyncCommand` 0x47a510, `CheckSyncVisibilityHash` 0x47a570, `ResolveDesync` 0x47a630 |
| Compressed blocks of the `.rep` ([P05](atlas/p05.md)) | `WriteCompressedBlock` 0x4137d0, `ReadCompressedBlock` 0x413ae0 |

`kp_core` already has the command executor of 0x475d30 (`kp_commands.c`), the random generator, saves (`kp_save.c`) and per-step dumps with CRC categories (`kp_dump.c`). What is missing is the replay reader and writer, the turn manager, the network provider and a runtime state hash.

**The atlas's network second pass (2026-11-14).** Of the 593 entries with relevance `network`, 499 were `excluded` while the remake had no network play. A second pass specified from the disassembly the 277 a network design needs, in four groups, and each entry was then checked against the disassembly by an independent reviewer:
- the sync check, game start, multiplayer saves, latency and leaving (40), among them `GetSpriteVisibilityChecksum` 0x4175e0 and the checksum 0x417540–0x417610 ([P05](atlas/p05.md)), `RestoreMultiplayerSaveSlots` 0x4790b0 ([P27](atlas/p27.md)) and the leave and drop path;
- the lobby protocol (76): the lobby messages 0x3c–0x48 and 0x5b with their builders and handlers ([P25](atlas/p25.md), [P26](atlas/p26.md)), creating, listing and joining a game (`CreateMultiplayerGame` 0x475040, `JoinNetworkGame` 0x475610, the game status string a listed game advertises), the slot, race and force rules ([P41](atlas/p41.md), [P39](atlas/p39.md)) and the template a joiner installs (`ReceiveTemplateRecord` 0x448000, [P15](atlas/p15.md));
- chat and map transfer (68): the chat room's direct messages, in-game chat (`SendInGameChat` 0x473be0: one direct message per recipient, outside the turn stream; each receiver records replay command 0x5c), the slash commands, the host's ban list, `SendNetMessage` 0x474780, and the file transfer of seven packet types with 128-byte chunks, eight in flight, a CRC-32 check and only `.scx`, `.scm` and `.rep` accepted (the "Map transfer" section of [P14](atlas/p14.md));
- the lobby, join-list and connection screens (93, [P17](atlas/p17.md)–[P19](atlas/p19.md)).

Nineteen of them now carry another relevance: `simulation` where every seat's state depends on them (the start info that sets the seed, the save's slots, the team-game slots, the template record), `ui` for the lobby's drawing, `platform` for a list helper offline code also uses. The 222 entries that stay `excluded` are Battle.net (its screens, gateways, ladder map checks, result reports and server templates: 148), the modem and AppleTalk provider (37), Storm's own internals (24), start-up static initializers (11), the log line of a created game and the selection of a Storm provider (2); a design without Battle.net, modems and Storm's providers needs none of them. The Storm network layer itself (`SNetSendTurn`, `SNetReceiveTurns` and the provider) is an import, so its turn scheduling, latency and transport are not in the executable and must be designed.

## Replays

### The 1.17 file

- A `.rep` starts with the magic `reRS` (0x53526572), then a 0x279-byte header, the command record and the map's CHK, each of the three through the compress/decompress pair above (sizes and order confirmed by phase RP, [below](#later-evidence-phase-rp-2026-11-13)).
- Replays recorded by 1.21 or later use another format that 1.17 cannot read, so a 1.17 player sees only older files. Public test sets exist: the 2003–2009 game-mode replays of [msikma/bwscrep](https://github.com/msikma/bwscrep) (those recorded by 1.21+ are unusable) and the SSCAIT 2016 bot-tournament archive. **Decision for the remake:** read 1.17's format only, or also the later one.
- A replay holds commands, not the computer's actions. The AI runs again during playback, so a replay against a computer player also exercises the AI, which the remake already checks against the original.
- The recorder skips twelve housekeeping command ids (the atlas, P26 §6).

### A playback bug of 1.17 (confirmed, [below](#later-evidence-phase-rp-2026-11-13))

A lead says 1.17 **loses commands** when playing some replays. `FeedReplayCommands` reads a frame one call early when the next recorded frame is ahead of the game, into a temporary buffer record over the static 0x200-byte arrays (`0x672de8` commands, `0x672fe8` lengths, `0x672be8` ids). If the frame's commands do not fit, `GrowRecordBuffer` moves the growth to the heap, and the early return frees it (atlas: "frees the buffers only if a read had to grow them"). The next call then dispatches the full count from the static arrays, which hold only the part written before the growth, followed by stale bytes of earlier frames. So a frame over 512 command bytes that follows a frame with no commands would play partly as other frames' commands. In one SSCAIT replay (Ro8\M1G2-Leta) a stale 0x08 byte would be the restart command, which ends the game at frame 19,062 of 21,593 and then crashes in `free_game` (null console theme), because a replay has no map to restart.

The atlas states the pieces but does not join them into this bug. To confirm: play that replay in the emulator (the listed set of affected replays is M1G2-Leta, M1G1-Leta, M1G3-Leta, team_game, Ro16 6-TOMA_ANDR-PVP, FFA and Ro8 M4G2-kras; M1G1-Leta reportedly does not load at all). **Decision:** reproduce the original's loss, to compare frame by frame with the oracle, or play the commands as recorded (keep the grown buffer between calls). A replay that never grows the buffer is the same either way.

### State a 1.17 save leaves out (confirmed, [below](#a-save-and-its-load-2026-11-14))

A 1.17 save does not hold everything a game needs to go on frame for frame, and a single-player load does not even try: it starts the random generator from the clock again. What a load loses is listed below. Saving also changes the game it is taken from (the Pylons are relinked, the units go through the spatial index again, dead references are dropped, the AI pools' free records are cleared), which the remake already repeats in `kp_save_effects.c`. None of this matters for campaign saves played forward. It matters for a replay that crosses a save or a load, and for resuming a saved game on the network: there the remake's own save, which keeps the whole state, is the one to use.

### Data a replay does not name

A 1.17 replay says nothing about which map data or rules it was recorded with. If the remake ever extends the rules or the data, a replay and a save need a record of what they were made with: a revision number of the simulation and a hash of the files the simulation reads (the eight `arr\*.dat` tables, `iscript.bin`, `aiscript.bin`, `bwscript.bin`). That hash should cover the files **as the game reads them**, not the archives' bytes. The eleven files are byte for byte equal on a 1.17 install and a Remastered one, and only the tilesets differ, in a way the remake's reader already undoes (confirmed, [below](#the-rule-files-of-117-and-remastered-2026-11-14)). So replays and rooms can cross the two sources, which the remake already accepts ([ADR 0013](../adr/0013-remastered-data-source-and-hd.md), [ADR 0018](../adr/0018-original-data-at-run-time.md)).

### A state hash

For replays and the network the remake needs a runtime hash of the simulation, not only the per-step dumps of the oracle comparison.

- Hash per domain (units, objects, players, map, sprites, AI) plus the random state and count, so a mismatch names where two runs diverged.
- Hash pointers as what they point at and records by their fields, never by memory layout, so a change of `KpGame`'s layout does not change the hash. The categories of `kp_dump.c` are the natural domains.
- Compute it at the end of the frame, after the update and the triggers; version the hash, and treat a file with another hash version as playable but unchecked.

### Snapshots and seeking

`KpGame` is fixed-capacity records with no allocation in the step, so a snapshot is close to one memory copy of the struct, and a rewind or a seek to a frame is cheap: put back the latest snapshot at or before the frame and run the recorded commands. A self-test should replay every frame after a rewind and require the same hash, and every snapshot frame byte-equal. Anything outside the simulation (the interface, camera, audio) stays out of a snapshot. Any range table of what a snapshot copies must be generated from the structs, not written once.

### Later evidence (phase RP, 2026-11-13)

Shown on the emulator by stage RP0 and RP2 of [phase RP](../phase-replay.md); the file is in [rep.md](../formats/rep.md).
- **The file:** the order, sizes and checksums above hold. Three 1.17 campaign files (terran01, zerg01, Brood War's protoss01) read and write back through `rep.py` and `kp_rep.c`.
- **When it is written:** `EndGame` writes `LastReplay.rep` after every campaign game, won or lost (a chain's victories and terran02's loss). A single-player game records no chat 0x5c, and a victory's exit no leave 0x57.
- **Quit Mission** (2026-11-14, `chain terran1 --frames 300 --quit`: F10, E, Q, Return in the game): the game ends with exit code 4 at frame 302 and `EndGame` writes `LastReplay.rep` too, its record empty, so no leave 0x57 either. Koprulu's quit at the same tick on the 1.17 data (`--script`) writes the same record and CHK byte for byte; the header differs in the frame count (the quit's tick, 301 or 303), the seed and the speed only.
- **Koprulu's tail:** the original ignores bytes after the CHK. A 1.17 file with a tail appended plays frame for frame as without.
- **The loss:** 1.17 loses commands as described above, shown on a synthetic file (a frame of more than 0x200 command bytes after an empty one). The dispatch log shows the stale bytes the feed hands on.
- **The last block** is never dispatched. 1.17's campaign files end with the block of their cheat, so their playback has no result. The original's playback of terran01 equals its live game at every frame to 338, and from frame 339 differs only in the results the cheat set.
- **The dispatch's timing:** a block stamped f is dispatched while the frame counter is f, before the object step that makes it f + 1.
- **Vision:** full vision is set at StartGame's step 13, after the map's load. During the load 0x67cc84 is 0, so the creep's shown bit and the remembered terrain start hidden.
- **Transmissions:** a transmission's wait is ended by the interface (`ReleaseLocalTriggerWait` 0x4be3a0), so a playback equals the original only with those events at the original's frames.
- **No result clock:** the result dialog's clock 0x4be340 does not run in a replay.
- **Both ways:** Koprulu's playback equals the original's on the three files at every step. The original played Koprulu's terran01 file to its end, with frame digests equal to those of its own file.
- **The first screen** (2026-11-14, the 59 chain games): the map's load sets the screen's top-left tile (0x5264b0) from a start location (`HandleStartLocationRecord` 0x46a280: the local player's, in a replay the first one while the screen is 0xffff), and the pre-game objects step's remembered-terrain refresh (0x40af30) leaves that screen's 21 x 14 tiles out. It shows only in a replay, whose viewer sees what any player sees; in a game the local player sees nothing before the game. Koprulu now sets it in `load_units`.
- **Early end in the emulator** (*check*): three chain files (terran m10, protoss m28, zerg m15) end their playback right after a transmission's wait ends (frames 2, 2, 219): 0x480e00 runs and the frames stop, with 354 to 360 frames in their headers. Not yet known whether the original does so or the emulator's sound end causes it.

### The rule files of 1.17 and Remastered (2026-11-14)

Method: `kp_convert extract` read each file as the game reads it from the 1.17 install (Patch_rt, then BrooDat, then StarDat) and from a StarCraft: Remastered install (CASC), and the SHA-256 digests were compared. `kp_convert verify` on the Remastered install logged every file's status, and the raw CASC tilesets were read with `tools/research/scdata/casc/source.py`.
- **Equal byte for byte:** the eight tables `units.dat` (19,876 bytes), `weapons.dat` (5,460), `flingy.dat` (3,135), `sprites.dat` (3,229), `images.dat` (37,962), `upgrades.dat` (1,281), `techdata.dat` (836), `orders.dat` (4,158), and the three scripts `iscript.bin` (40,482), `aiscript.bin` (62,050), `bwscript.bin` (26,458). The same holds for the other files the simulation reads: `images.tbl`, all 928 GRP and LO files it names (frame bounds and overlay offsets), `mapdata.dat`, `sfxdata.dat`, `portdata.dat` and the eight tilesets' `wpe`.
- **Tilesets:** Remastered's `cv5`, `vf4`, `vr4` and `vx4` (stored as `vx4ex`, 32-bit references whose values fit 16 bits) are 1.17's files with records appended. Install is not grown. Three `cv5` also fill groups that 1.17 left empty: platform 91 (groups 933–1023), desert 254 (770–1023), twilight 227 (797–1023); every other group of the eight files is equal. Record counts, 1.17 → Remastered:

  | Tileset | `cv5` groups | `vf4`/`vx4` megatiles | `vr4` minitiles |
  |---|---|---|---|
  | badlands | 1665 → 1979 | 4844 → 6172 | 26247 → 47495 |
  | platform | 1513 → 2046 | 3055 → 5711 | 18787 → 61283 |
  | install | 1265 | 1431 | 7259 |
  | ashworld | 1262 → 1418 | 3497 → 4161 | 18078 → 28702 |
  | jungle | 1578 → 2046 | 5046 → 7038 | 23818 → 55690 |
  | desert | 1520 → 2046 | 5810 → 9130 | 32158 → 85278 |
  | ice | 1415 → 2039 | 4518 → 7174 | 30376 → 72872 |
  | twilight | 1494 → 2047 | 5232 → 8552 | 28124 → 81244 |

  Truncating to 1.17's size and clearing the filled groups gives 1.17's bytes; the remake's reader already does this (`kp_res.c`: `CASC_GROWN`, `CV5_FILLED`), and its result matches the 1.17 manifest's digest.
- **For the design:** a data hash taken over the files as the remake reads them is the same from either source, so a check at join can use it. The appended records serve only maps made with Remastered's added tiles, which 1.17's data cannot show.

### A save and its load (2026-11-14)

Method: the emulator ran the test scenario `save_effects` (terran01's map, eleven Marines against four Mutalisks, the game saved through the menus at frame 77) twice to frame 460. Run A played on after the save. Run B loaded the same save through the menus at frame 128 and played on. Each of B's dumps after the load was compared with A's dump of the same state over the remake's dump model (`tools/native_state.py`). The scenario specs and the comparison script stayed outside the repository.
- **Two uncounted steps.** After the load the original runs the two uncounted object steps of `PrimeGameLoop` 0x445520, as at a game start. The loaded game is then two steps ahead of the frame counter it saved: B's dump with counter c matches A's with c + 2.
- **What the load restores** equal to the game that was saved: the units, the sprites and their images, the orders and paths, the AI's towns, workers, buildings and scripts, the triggers, the tiles, the creep, the resources and the vision.
- **What it does not restore:**
  - the random generator: a single-player load seeds it again from the clock (`StartGame` 0x443f40 step 3), so state, call count and per-source counts start over. The save's header holds the state (`+4`, written by 0x49a8e0), but only lobby code reads it back (`GetSavedRngState` 0x499860);
  - the step's own counters, which start from their initial values: the AI's cycle, period, rounds and player cursors; the trigger pass clock; the leaderboard, result and seconds clocks; the order and secondary spread counters; the hit slot; the creep tile countdown; the recede timer; the turn counter;
  - the order of the free lists: the unit and bullet free lists, and the active bullet list, come back in slot order, so new objects take other indices;
  - the selections: the local player's selection and selection row are empty after the load, with their selection circles;
  - the leaderboard's entries.
- **After the load** the reseeded generator makes the two games drift apart. 300 steps on, the units, sprites, AI regions and the generator still differ.
- **How 1.17 resumes a network game** (the atlas's second pass, [P27](atlas/p27.md) 0x4790b0, [P33](atlas/p33.md) 0x499860): every seat loads its own copy of the same save, found by the save's id. The random state in the save's header is only a token: a joiner sends it in its join request (message 0x40) and the host refuses one that differs. The generator then starts from the seed of the start message 0x48 (the host's clock) on every seat, and `RestoreMultiplayerSaveSlots` puts the players back into their saved slots without a random draw. So the seats agree with one another, though none continues the game that was saved.
- **For the design:** lockstep needs every seat to restore the same state, not the state of the uninterrupted game; 1.17's way meets that. A replay that crosses a save, or a resume that should equal the game before it, needs more: the remake's own save keeps the whole state, and `test_save.c` loads saves made in the middle of every scenario and requires the per-step CRCs of the uninterrupted original. Either way every seat should check the same state hash at the first frame after the resume.

### A cut-off last command (2026-11-14)

From the disassembly of `ExecuteTurnCommands` 0x475d30: for each command the dispatcher subtracts the command's length from the bytes left (`sub esi, len`) and stops the whole buffer when the result is negative (`js`), before it calls the handler. So a cut-off last command is never run, and nothing past the buffer is read. Selection commands 9–0x0b check their first two bytes, then 2·n more. Save and load (6, 7) take their name from `ValidateNetString` 0x476e20 with no formatting and no control codes allowed: no terminator within min(0x1c, bytes left), or any byte 1–31, stops the buffer. Bytes from 0x80 pass.
- The remake's walker `kp_record_split` (`native/kp_app/kp_record.c`) did the same, except that it let control codes through in a save's name; it now stops at them as the original does. `test_kp_rep.split_stops_at_a_cut_last_command` covers a whole and a cut right click, a lone command id, a tab in a save's name, a name with bytes from 0x80, a name with no terminator, and names at the length bound.
- `kp_execute_command` in `kp_core` fails the game on a read past a command's size. That cannot happen behind `kp_record_split`, which hands on only whole commands. A network turn must go through the same walker.

## Network play

### What the original does (atlas, P26–P27)

- Deterministic lockstep: only commands cross the network and every seat runs the whole game from the same commands. The turn manager, executor and per-player turn status words are in the atlas.
- Each sent turn also carries the 7-byte sync command 0x37, built from a ring of 16 records (`AdvanceSyncRing` 0x47a490). A record holds a hash that alternates between the units and the resources (schedule {1, 2}, `ComputeSyncValue` 0x47a360, mixed with the vision masks and the random sequence), a minimap bit, the sprite visibility checksum byte and the first row of the sender's view. A receiver checks it against its own record with the same ring index, and `ResolveDesync` drops the minority (with two players each side drops the other and each wins).
- **A pitfall for a remake that separates simulation from drawing (confirmed from the disassembly by the atlas's second pass, [P05](atlas/p05.md) 0x417540–0x417610):** `PollTurn` 0x444cc0 sets the request flag at a turn boundary, and the checksum is computed in the draw path (`PresentFrame` 0x4c5360 → `PrepareSpriteDrawRows` 0x445de0 → `BuildSpriteDrawList` 0x416a00 → `ComputeSpriteVisibilityChecksum` 0x417540). In the original exactly one frame lies between the two on every seat, because the request also ends `RunPendingFrames` after that frame. The checksum covers the visibility bytes of the flingy sprites of non-building units: per row, the parity of each player's view, and a hash of 14 rows from the top of the sender's camera, masked to the sender's player. A remake whose step is independent of drawing has to take both inside the step, right after the first frame that follows a turn boundary, with the camera row supplied by the interface; otherwise a seat that runs two frames before its draw is dropped as out of sync. Extra draws without a request change nothing. Not observed at run time: the emulator runs no network game.
- A cut-off last command is never run. `ExecuteTurnCommands` 0x475d30 subtracts a command's length from what is left of the buffer before it calls the handler, and a negative result ends the whole buffer; the lobby's walker 0x476650 checks lengths the same way. The remake's walker does the same (confirmed, [below](#a-cut-off-last-command-2026-11-14)).

### Design questions the original leaves open

The executable has no transport, matchmaking or turn scheduling (those were in Storm and Battle.net), so these are the remake's to design, and the design is to be shown inline before any code:

1. **Topology.** Peer to peer as the original, one host relaying turns, or a relay process. A relay makes the step (all players' turns of a frame) in one place, which suits WebSocket-only browsers and phones.
2. **Transport.** A browser has no UDP and no raw TCP, so the web build needs WebSocket or WebRTC data channels; the phones need the same plus NAT handling.
3. **Join and leave rules.** Who may drop whom, what ends a stalled game, what a player who leaves does to the others' results, what happens when the relay or host disappears. The original's rules are in the atlas (`DropNetPlayer`, `ProcessLeaveReasons`, `WaitForLaggingPlayers`); the lost-connection result needs to be settled (two seats that each see the other leave must not both win).
4. **Start.** Who is in, map delivery, a countdown, and a common first frame. The original's version is in the atlas since the second pass: the lobby's messages travel in the same turn stream as the game's and run on every seat in the same turn, the joiner's handshake ends with its first slot update (message 0x3e), the host's Start needs every peer to hold the map (`AllPeersHaveFile` 0x440ba0) and queues 0x3c; from it a counter of 24 executed lobby turns runs on every seat (`LobbyCountdownTick` 0x470930), the host builds the start info 0x48 at L + 2 turns before its end (L the turns in transit), and every seat runs 0x48, which carries the seed (the host's `time()`) and the saved-slot table, before the counter reaches 0, so all seats leave the lobby on the same turn with the same seed. The turn stream is not reset between the lobby and the game.
5. **Compatibility check at join:** protocol, build, simulation revision and the data hash above, refused with a reason.
6. **Untrusted input.** Every frame, replay, save and map is untrusted: bounds-checked readers that fail instead of reading past the end, limits on frame and name sizes, a timeout on a connection that says nothing, no passwords in logs. A fuzz run over the `.rep` reader, the CHK reader and the command executor belongs in CI when replays land.
7. **Watching.** If a game's command record is kept growing, a viewer can play it as a replay a chosen delay behind the players; one record format then serves both.

## Verification method

- **Oracle against remake, frame by frame, on real replays.** Play the same replay in the original (here, the emulator) and in `kp_core`; log the state every frame and every field; report the first frame that differs. A replay passes when it was read, played to its end, and matched on every frame the oracle has. Use a recorded corpus with all three races, free-for-all and team games, and the public sets above; record which replays the oracle itself cannot play.
- Two runs of the same replay must be identical, and independent of the clock and of how many frames are run per step.
- A self-recorded corpus: play a game with scripted orders to a frame and keep the replay, with a fixed seed so two recordings are identical; for saves, load the save in a new process and hold it to the same log.
- Open: how fast the emulator plays a whole replay (the public replays are tens of thousands of frames each) and whether it can run one without a menu script; the campaign runs record missions, not replays.

## Not verified

No item marked *check* is left. The visibility hash's timing is confirmed from the disassembly only, because the emulator runs no network game. The affected replay names quoted from outside material are not reproduced. The tileset differences, the rule files' equality, what a save leaves out and the walker on a cut-off command were reproduced on 2026-11-14 (above). The atlas entries cited elsewhere were read in the atlas, not disassembled again for this page.
