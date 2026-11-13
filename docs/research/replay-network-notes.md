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

**Marked `excluded` in the atlas, and needed once the network exists:** 499 of the 593 entries with relevance `network` are excluded by design: the lobby (0x478a30–0x47a250), `CreateMultiplayerGame` 0x475040, `JoinNetworkGame` 0x475610, the map transfer (0x440640–0x441020), `SendNetMessage` 0x474780, `SendInGameChat` 0x473be0, `GetSpriteVisibilityChecksum` 0x4175e0, `CaptureMinimapDrawStats` 0x47a2f0 and others. Only 94 are specified. The Storm network layer itself (`SNetSendTurn`, `SNetReceiveTurns` and the provider) is an import, so its turn scheduling, latency and join protocol are not in the executable and must be designed. Before network work starts, the excluded entries the design keeps need a second pass, as the atlas did for the campaign.

## Replays

### The 1.17 file

- A `.rep` starts with the magic `reRS` (0x53526572), then a 0x279-byte header, the command record and the map's CHK, each of the three through the compress/decompress pair above (*check* the sizes and order against `LoadRecordedCommands` and the replay loader).
- Replays recorded by 1.21 or later use another format that 1.17 cannot read, so a 1.17 player sees only older files. Public test sets exist: the 2003–2009 game-mode replays of [msikma/bwscrep](https://github.com/msikma/bwscrep) (those recorded by 1.21+ are unusable) and the SSCAIT 2016 bot-tournament archive. **Decision for the remake:** read 1.17's format only, or also the later one.
- A replay holds commands, not the computer's actions. The AI runs again during playback, so a replay against a computer player also exercises the AI, which the remake already checks against the original.
- The recorder skips twelve housekeeping command ids (the atlas, P26 §6).

### A playback bug of 1.17 (*check*)

A lead says 1.17 **loses commands** when playing some replays. `FeedReplayCommands` reads a frame one call early when the next recorded frame is ahead of the game, into a temporary buffer record over the static 0x200-byte arrays (`0x672de8` commands, `0x672fe8` lengths, `0x672be8` ids). If the frame's commands do not fit, `GrowRecordBuffer` moves the growth to the heap, and the early return frees it (atlas: "frees the buffers only if a read had to grow them"). The next call then dispatches the full count from the static arrays, which hold only the part written before the growth, followed by stale bytes of earlier frames. So a frame over 512 command bytes that follows a frame with no commands would play partly as other frames' commands. In one SSCAIT replay (Ro8\M1G2-Leta) a stale 0x08 byte would be the restart command, which ends the game at frame 19,062 of 21,593 and then crashes in `free_game` (null console theme), because a replay has no map to restart.

The atlas states the pieces but does not join them into this bug. To confirm: play that replay in the emulator (the listed set of affected replays is M1G2-Leta, M1G1-Leta, M1G3-Leta, team_game, Ro16 6-TOMA_ANDR-PVP, FFA and Ro8 M4G2-kras; M1G1-Leta reportedly does not load at all). **Decision:** reproduce the original's loss, to compare frame by frame with the oracle, or play the commands as recorded (keep the grown buffer between calls). A replay that never grows the buffer is the same either way.

### Replay-related state the original keeps (*check against `kp_save.c`*)

A 1.17 save does not hold everything a game needs to go on frame for frame. Candidates to verify against the atlas and the emulator: the random state and call count and the C runtime's `rand` state; the order of the free and used lists of units, sprites, images, thingies and bullets (a load rebuilds them in slot order); the counters between passes (trigger pass and clock, leaderboard, victory check, fog and power update, order slots, creep, AI); the AI's demands; the player tables' extra entries; cargo lists. Saving also changes the game it is taken from (the power fields come down and go up again, the collision boxes are sorted again), so a replay of a game that saved would have to repeat that effect at the frame of each save. None of this matters for campaign saves played forward, but it matters for a replay that crosses a save and for resuming a saved game on the network.

### Data a replay does not name

A 1.17 replay says nothing about which map data or rules it was recorded with. If the remake ever extends the rules or the data, a replay and a save need a record of what they were made with: a revision number of the simulation and a hash of the files the simulation reads (the eight `arr\*.dat` tables, `iscript.bin`, `aiscript.bin`, `bwscript.bin`). That hash should cover the files **as the game reads them**, not the archives' bytes. A lead says the eleven files are byte for byte equal on a 1.17 install and a Remastered one, and that only the tilesets differ (Remastered's `cv5` differ in groups 1.17 left empty: 91 on platform, 254 on desert, 227 on twilight; its `vf4` are 1.17's with more appended). If it holds, replays and rooms can cross the two sources, which the remake already accepts ([ADR 0013](../adr/0013-remastered-data-source-and-hd.md), [ADR 0018](../adr/0018-original-data-at-run-time.md)). *Check* with the repack and the CASC reader.

### A state hash

For replays and the network the remake needs a runtime hash of the simulation, not only the per-step dumps of the oracle comparison.

- Hash per domain (units, objects, players, map, sprites, AI) plus the random state and count, so a mismatch names where two runs diverged.
- Hash pointers as what they point at and records by their fields, never by memory layout, so a change of `KpGame`'s layout does not change the hash. The categories of `kp_dump.c` are the natural domains.
- Compute it at the end of the frame, after the update and the triggers; version the hash, and treat a file with another hash version as playable but unchecked.

### Snapshots and seeking

`KpGame` is fixed-capacity records with no allocation in the step, so a snapshot is close to one memory copy of the struct, and a rewind or a seek to a frame is cheap: put back the latest snapshot at or before the frame and run the recorded commands. A self-test should replay every frame after a rewind and require the same hash, and every snapshot frame byte-equal. Anything outside the simulation (the interface, camera, audio) stays out of a snapshot. Any range table of what a snapshot copies must be generated from the structs, not written once.

## Network play

### What the original does (atlas, P26–P27)

- Deterministic lockstep: only commands cross the network and every seat runs the whole game from the same commands. The turn manager, executor and per-player turn status words are in the atlas.
- Each turn also carries the sync command 0x37: a hash of the units, the resources and the random sequence, minimap counts and a hash of the sprites' visibility. A receiver checks it against its own record, and `ResolveDesync` drops the minority (with two players each side drops the other and each wins).
- **A pitfall for a remake that separates simulation from drawing (*check*):** the visibility hash is armed in the turn and taken by the original at the next draw (`PriorityAddRange`), which in the original always happens at the end of that frame. If the remake may run several frames before drawing, the hash has to be taken inside the step; otherwise a seat is dropped as out of sync as soon as units move. The atlas marks `GetSpriteVisibilityChecksum` `excluded`.
- A turn past a size bound must not be able to crash the command walker; the walker reads a cut-off last command whole (atlas: c − 5 > 0x56 stops the player, lengths are checked as in 0x475d30). The remake's executor must be checked with a truncated last command.

### Design questions the original leaves open

The executable has no transport, matchmaking or turn scheduling (those were in Storm and Battle.net), so these are the remake's to design, and the design is to be shown inline before any code:

1. **Topology.** Peer to peer as the original, one host relaying turns, or a relay process. A relay makes the step (all players' turns of a frame) in one place, which suits WebSocket-only browsers and phones.
2. **Transport.** A browser has no UDP and no raw TCP, so the web build needs WebSocket or WebRTC data channels; the phones need the same plus NAT handling.
3. **Join and leave rules.** Who may drop whom, what ends a stalled game, what a player who leaves does to the others' results, what happens when the relay or host disappears. The original's rules are in the atlas (`DropNetPlayer`, `ProcessLeaveReasons`, `WaitForLaggingPlayers`); the lost-connection result needs to be settled (two seats that each see the other leave must not both win).
4. **Start.** Who is in, map delivery, a countdown, and a common first frame. The lobby functions are `excluded` in the atlas.
5. **Compatibility check at join:** protocol, build, simulation revision and the data hash above, refused with a reason.
6. **Untrusted input.** Every frame, replay, save and map is untrusted: bounds-checked readers that fail instead of reading past the end, limits on frame and name sizes, a timeout on a connection that says nothing, no passwords in logs. A fuzz run over the `.rep` reader, the CHK reader and the command executor belongs in CI when replays land.
7. **Watching.** If a game's command record is kept growing, a viewer can play it as a replay a chosen delay behind the players; one record format then serves both.

## Verification method

- **Oracle against remake, frame by frame, on real replays.** Play the same replay in the original (here, the emulator) and in `kp_core`; log the state every frame and every field; report the first frame that differs. A replay passes when it was read, played to its end, and matched on every frame the oracle has. Use a recorded corpus with all three races, free-for-all and team games, and the public sets above; record which replays the oracle itself cannot play.
- Two runs of the same replay must be identical, and independent of the clock and of how many frames are run per step.
- A self-recorded corpus: play a game with scripted orders to a frame and keep the replay, with a fixed seed so two recordings are identical; for saves, load the save in a new process and hold it to the same log.
- Open: how fast the emulator plays a whole replay (the public replays are tens of thousands of frames each) and whether it can run one without a menu script; the campaign runs record missions, not replays.

## Not verified

The items marked *check* and the numbers quoted from outside material (affected replay names, the tileset differences, the file sizes) were not reproduced in this repository. The atlas entries cited were read in the atlas, not re-disassembled for this page.
