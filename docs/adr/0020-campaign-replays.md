# ADR 0020: Campaign replays in 1.17's `.rep` format

Status: accepted (phase RP, 2026-11-13). The user gave the design and approved the plan with five decisions, listed below. Stages and their state: [docs/phase-replay.md](../phase-replay.md). The format: [docs/formats/rep.md](../formats/rep.md).

## Context

The original records every game's commands and writes `LastReplay.rep` when the game ends; Load Replay plays a file back with full vision, a speed control and a pause. The remake had the command executor, the random generator and the per-step dumps (`kp_dump.c`), but no recorder, no feed, no file and none of the replay's in-game interface. The leads of [replay-network-notes.md](../research/replay-network-notes.md) (the 1.17 command loss, a tail the original ignores) had not been reproduced.

## Decision

1. **Single-player campaign games, 1.17's format both ways.** Koprulu writes files the original plays and plays the original's files, frame for frame. Out of scope:
   - melee and custom maps, network games and their commands 0x57 and 0x5c (refused before the playback);
   - rewinding and seeking;
   - replay archives.
2. **The record is the original's.** The recorder, the feed and their quirks are copies of 0x478340, 0x4787e0, 0x478980, 0x478480 and 0x4769b0:
   - the twelve excluded ids;
   - the 0x100 limit;
   - the growing buffer;
   - the save's open block;
   - the last block never dispatched.

   1.17's command loss is reproduced for every file. There is no "fixed" mode: a file Koprulu writes then plays the same in the original.
3. **Save Replay after a campaign game stays disabled**, as in the original. Enabling it would change the score screen that `kp_oracle score_replay` compares with the original's captures. The file is named by exporting it (stage RP4).
4. **Koprulu's tail goes after the CHK** (`KPRX`), which the original never reads (shown on the emulator). It holds the simulation's revision, the data's hash, the carried globals the header does not hold, the briefing's objectives, and the loads.
5. **A recorded game's save keeps its record** in a `REPL` section. Its absence in an older save starts an empty record. A replay of a game with loads is marked unverified.
6. **The format lives in `kp_app_core`** (`kp_rep.c`, `kp_record.c`) and calls pklib directly. `kp_core` gains the replay's state (the flag, full vision, the speed multiplier and pause, the commands 0x10, 0x11, 0x55 and 0x56) and no format or compression.

## Consequences

- **Presentation in a replay:** the replay flag 0x67cc90 and full vision change presentation fields, as in the original:
  - the shown creep bit of the tiles;
  - the minimap's attack blink (unit +0x8a/+0x8b);
  - the images' visibility.

  The result dialog's clock (0x4be340) does not run in a replay. A session's playback of its own recording therefore equals the recording in every field only without the flag; tests use `replay_as_game` for that.
- **Load Replay starts without a briefing,** so a replay has no briefing objectives.
- **Transmissions:** a trigger's transmission waits until its sound ends, and the interface reports that end (`kp_trigger_release_wait`). A playback matches the original only with the interface's events at the original's frames: the oracle's goldens of the original's replays carry them (`tools/state_check.py --kind replays`).
- **Brood War and network names:** the name of the human in the header is the profile's; the computers' come from network.tbl as in 0x4074a0.

## Alternatives

- **Koprulu's own format.** Simpler to evolve, but the original could not play it and its files could not be compared frame for frame. Rejected.
- **Playing commands as recorded (without 1.17's loss).** It would differ from the original only on files with a frame of more than 512 command bytes after an empty one. A second mode would double the checks for no campaign file that needs it. Rejected.
- **The tail in a file beside the replay.** A file that travels alone would lose it, and the original ignores the bytes anyway. Rejected.
