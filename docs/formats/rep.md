# Replays (`.rep`)

StarCraft 1.17's replay files, which Koprulu writes and reads for single-player campaign games ([ADR 0020](../adr/0020-campaign-replays.md), stages in [docs/phase-replay.md](../phase-replay.md)). A file Koprulu writes is played by the original, and the original's files are played by Koprulu, frame for frame. The original's code is P26 and P28 of the [atlas](../research/atlas/README.md): `WriteReplayFile` 0x480080, `ReadReplayFile` 0x480690, the recorder `RecordCommand` 0x478340 and the feed `FeedReplayCommands` 0x4769b0.

Code:
- `native/kp_app/kp_rep.{h,c}`: the file;
- `native/kp_app/kp_record.{h,c}`: the command record and the playback's feed;
- `native/kp_app/kp_session.c`: record and play;
- `native/kp_app/kp_flow.c`: `LastReplay.rep`.

The research reader and writer are `tools/research/scdata/formats/rep.py`.

## The file

Four blocks, then optionally Koprulu's tail:

| Block | Bytes |
|---|---|
| magic | `reRS` (u32 0x53526572) |
| header | 633 bytes (0x279), [below](#the-header) |
| command record | u32 its size; then, when above 0, the record's bytes as a block of their own |
| map | u32 the CHK's size; then the CHK as a block of its own |
| KPRX tail | `KPRX` and a block, [below](#the-kprx-tail); the original never reads past the map |

Each value or byte run is one **block** (`WriteCompressedBlock` 0x4137d0 / `ReadCompressedBlock` 0x413ae0):

```
u32 checksum   CRC-32 of the uncompressed bytes from 0xffffffff, no final inversion (0x402160): ~kp_crc32
u32 chunks     ceil(n / 8192)
per chunk:     u32 size, then size bytes
```

A chunk holds at most 8 KiB of the data. It is PKWARE DCL imploded (binary, 1 KiB dictionary, pklib in `kp_storm`), or stored raw when imploding does not make it smaller: a chunk whose size equals the data it holds is raw. The writer clears pklib's work buffer before each chunk (`kp_rep_chunk`): implode's matcher reads window bytes it never wrote, so a buffer left as `malloc` gave it made the same game's file differ between two runs (2026-11-14, the scripts' play_4 comparison); the bytes now depend on the input alone (`test_kp_rep.chunk_bytes_depend_on_the_input_alone`). The reader checks every size against the bytes it has and against the data still owed. It also checks the checksum after the whole block.

The reader refuses a record over 64 MiB or a CHK over 16 MiB (the original takes any size).

## The header

The offsets are `ReadReplayFile`'s accessors 0x480540–0x480680. The values are what a single-player campaign game writes; 1.17's own files from the emulator's campaign chains show them.

| Offset | Size | Field |
|---|---|---|
| +0x00 | 1 | Brood War (1: the expansion's game; refused without Brood War, gluAll 60) |
| +0x01 | 4 | frame count at the end of the game (`0x480670`); the playback ends there |
| +0x05 | 2 | campaign row (`0x4805d0`; 0 for a map that is not one) |
| +0x07 | 13 | the seed record: a command byte, the seed (+0x08, `0x4805b0`), 8 player bytes |
| +0x14 | 141 | the game header `0x6728d0` (below) |
| +0xa1 | 12 × 36 | the player records `0x6726c8`: `{u32 slot, s32 Storm ID, u8 type, u8 race, u8 force, char name[25]}` |
| +0x251 | 8 × 4 | the colours (`0x480630`); a campaign game's are 0..7 |
| +0x271 | 8 | the force bytes (`0x480680`), all 0 in a campaign game |

The game header's fields at their file offsets:

| Offset | Field |
|---|---|
| +0x18 | the player's name, 24 bytes with its NUL (the logged-in ID; `Anonymous`, network.tbl 72, without one) |
| +0x34, +0x36 | the map's width and height (WORD) |
| +0x38 | active players: 1 |
| +0x39 | slots: 1 (`0x4805e0`) |
| +0x3a | the game speed (`0x4805c0`) |
| +0x3c, +0x3e | 0x0a, 1: the Use Map Settings type and subtype |
| +0x44 | the tileset (WORD) |
| +0x46 | 1; the reader refuses 0 |
| +0x61 | the scenario's name (SPRP), 32 bytes, in the map's encoding |
| +0x81 | the Use Map Settings template, `0a 00 01 00 00 00 00 00 00 00 00 02 00 01 03 01 00 01 00` |

Player records:
- **The human:** type 2, Storm ID 0, the profile's name.
- **Every other slot:** Storm ID −1.
- **Computer names:** the original names computer players by `AssignDefaultComputerPlayerNames` 0x4074a0, and Koprulu does the same (`kp_ui_default_player_name`). This applies to types 1, 3, 4 and 7. The name is network.tbl entry 0x1d + race offset + player: 0x91 instead of 0x1d in Brood War, race offset zerg 8 and protoss 16. Missions 15 and 17 clear player 2's name.

## The command record

```
block: u32 frame, u8 payload size, payload { u8 Storm ID, command bytes }*
```

`RecordCommand` 0x478340 writes it:
- **Housekeeping ids are not recorded:** the twelve ids of 0x4ef2fc (0x37, 0x05–0x08, 0x10, 0x11, 0x38–0x3b, 0x55). The save and load effects are therefore not in a replay, in 1.17's either.
- **Size limit:** a command of 0x100 bytes or more is not recorded.
- **Buffer:** it starts at 50,000 bytes and grows by 10,000 whenever fewer than 0x200 are left. A command that does not fit is dropped.
- **Block boundaries:** a block is closed when the frame changes or when the next command would take its payload to 0xff bytes, which makes 84 two-byte commands per block. An empty block is removed.
- **Saving mid-frame:** saving closes the open block but keeps its frame (`SaveRecordedCommands` 0x478980). Commands of the same frame recorded after that go on without a header.

Koprulu records in `kp_session_step`: the local player's commands with Storm ID 0, at `KP_FRAME` before the step that executes them. The original dispatches a block stamped f while its frame counter is f, before the object step that makes it f + 1, and the emulator's dispatch log shows that (`replay.py`'s `dispatch.jsonl`). A buffer of several commands is split as `ExecuteTurnCommands` 0x475d30 walks it (`kp_record_split`).

### The playback's feed and 1.17's command loss

`FeedReplayCommands` 0x4769b0 runs at the start of each frame.

- **Reading ahead:** when the next recorded frame is ahead of the game, it reads it one call early into the 0x200-byte static buffers.
- **The loss:** a frame that needs more than 0x200 bytes grows a heap copy that the early return frees. The next call then dispatches the frame's count from the static buffers, which hold only what was written before the growth followed by an earlier frame's bytes. Koprulu reproduces this for every file (`kp_record_feed`); the emulator showed it on a synthetic file (`test_kp_rep.feed_loses_commands_as_the_original`).
- **The last block:** it is never dispatched, because `ReadRecordedFrame` 0x478480 returns −1 at the end of the data without storing the count. 1.17's campaign files end with their cheat's block, which their playback therefore never runs.
- **The end of the replay:** at the header's frame count the playback pauses (`SetReplayPaused` 0x480e00) and shows the victory dialog.
- **Live commands during playback:** only ids 5, 8, 0x10, 0x11, 0x37–0x3b, 0x55 and 0x56 pass. 0x56 is the replay's speed and pause: u8 paused, u32 speed, u32 multiplier, as set by `SetReplaySpeed` 0x481040.

Out of version 1: a record with the leave command 0x57 or the chat command 0x5c (a network game's) is refused before the playback starts. So is a record with any id the recorder never writes (table 0x4ef2fc: 0x05-0x08, 0x10, 0x11, 0x38-0x3b, 0x55), which no file the original wrote holds (the sync 0x37 of the table plays: its entry is 1 byte, the dispatcher's 0x37 7, so it never runs); a crafted file's save 0x06 or load 0x07 would otherwise write over or load the player's saves. The record is walked as the feed reads it and each entry as the dispatcher walks it, so a command after another inside one entry counts too (`test_kp_rep.unrecorded_commands_refused`). So is a player record with a Storm ID whose slot is no game player (0..7).

## The KPRX tail

After the map's block Koprulu writes `KPRX` and one block of a fixed payload, little-endian:

| Field | Size |
|---|---|
| version (1) | u32 |
| the simulation's revision (`KP_REP_SIM_REVISION`) | u32 |
| the data set's hash (the save identity's DAT digest) | 32 |
| has carry | u32 |
| `KpCarry`, the globals the game before left (its int64 fields in order) | 8 × n |
| cheats at the start | i64 |
| briefing objectives: count, 8 values | u32, 8 × i64 |
| saves loaded during the game | u32 |
| a load without a record (a save from before replays) | u32 |

The original ignores it: the emulator played a 1.17 file with a tail appended frame for frame as without. A file whose tail is missing or damaged is read without it. Load Replay shows a replay as unverified with one reason: another simulation revision (`sim_revision` other than this build's, "Unverified: other version"), then other data (`data_hash` other than the running data's save identity, "Unverified: other data"), then saves loaded during the game ("Unverified: saves loaded"). A file without the tail (the original's) is not marked.

## Saves

A recorded game's save has a `REPL` section beside CORE/CMDS/CAMP/PRES:

```
u32 version (1), header[633], u32 loads, u32 lost, i64 open block's frame, u32 size, the record
```

A load restores the record and its open block. A save without `REPL` (from before replays) starts an empty record and marks it lost. A damaged `REPL` (wrong version or size) refuses the whole load. The section is checked before the game is loaded and restored only after, so a refused load leaves the game and its record as they were (`test_session.session_replay_record_in_a_save`). `KP_SAVE_VERSION` did not change.

## Refusals

| `KpRepRefusal` | Message |
|---|---|
| `KP_REP_DAMAGED` | gluAll 63: not a replay, a bad block, a length past the file |
| `KP_REP_EXPANSION` | gluAll 60: a Brood War replay without Brood War |
| `KP_REP_NOT_CAMPAIGN` | outside version 1: a melee or custom map's replay (campaign row 0), a row the catalog does not have, a network game's commands |
| `KP_REP_OTHER_MAP` | the CHK is not the campaign row's map |

## Where it is written

After every game that reached its console and then ended (victory, defeat, quit, restart, exit), `<pref>replays/LastReplay.rep`, written atomically and synced on the web. A game whose ready room was cancelled, or left with the program from there, writes none and keeps the last one (`test_flow.room_cancel_leaves_no_game`). Save Replay after a campaign game stays disabled as in the original.
