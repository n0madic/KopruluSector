# The viewers' protocol (Koprulu's own)

The host of a network game serves its replay as it grows; a viewer (`koprulu --watch HOST`) plays it as a replay
whose record's end grows. StarCraft 1.17 has no such feature, so the protocol is Koprulu's own and stays off Storm's
ports: an original peer never sees it ([ADR 0027](../adr/0027-network-game-viewers.md)). The code is
`native/kp_app/kp_watch.{h,c}`; the playback's side is `kp_record_grow` and `kp_turns_watch`.

## Datagrams

UDP on the server's port (`KP_WATCH_PORT`, 6120, or the host's `--watch-port`; `--watch-port off` serves no viewer). Every datagram starts with the magic
`KPW1` and a type byte; the numbers are little-endian.

| Type | Bytes | Fields |
|---|---|---|
| 1 REQUEST | 14 or more | `u32 game` (0: not known yet), `u8 part`, `u32 offset`, then padding to the size of the answer asked for |
| 2 DATA | 29 + n | `u32 game`, `u8 part`, `u32 offset`, `u32 size` (the part's size), `u32 frames`, `u8 final`, `u16 n`, `u32` CRC-32 of the n bytes, the n bytes (at most 1,024) |

The parts:

- **0, the start:** the replay file at the game's start (`kp_rep_write`: the header, an empty record, the CHK, Koprulu's
  tail). Its size is fixed; `frames` is 0 and `final` 1. A resumed network game is served once its save has loaded:
  the header is then the save's, and part 1 holds the restored record from its first byte. A resumed game whose save
  kept no record is not served.
- **1, the record:** the record's published bytes. `frames` says that the bytes hold every block before that frame;
  `final` that the game ended and the record is whole.

## The server

- It keeps no state per viewer. Each REQUEST of at least 29 bytes gets one DATA: the part's bytes from `offset`, as
  many as fit in a datagram no larger than the REQUEST (at most 1,024). A REQUEST past the part's end gets no bytes,
  only the size and the frames. A forged source address therefore gets no more bytes than the forger sent.
- A REQUEST naming another game gets the server's game and no bytes, which stops the viewer.
- **Publication.** After each step the session marks the record: the game's frame count C and the record's size then.
  Chat lines are recorded at the next frame, after the mark. The server publishes the record as it stood `delay`
  frames before: frames C − delay, and the bytes up to that mark. No command can change those bytes any more. The open
  block's size byte is written when the block closes; the server sets it in its copy, as the close will.
- **A paused game** steps without running a frame. Such a step marks nothing: what the pause records (its turns, chat
  lines, the resume) belongs to the next frame, which the next mark covers.
- **The end:** when the game's session is freed, the whole record is published, final. The server goes on answering
  until the next game this program hosts, or until the program ends.
- **A load in the game** takes its frame back. The server then ends the game at what it had published.
- **The delay** is `--watch-delay SECONDS` (default 60; 0 for none), in frames of the game's speed
  (`kp_watch_delay_frames`).

## The viewer

- It asks for the start, then for the record. While it knows that more bytes wait, it sends up to 8 REQUESTs of 1,024
  bytes for successive chunks; once it holds everything known, one REQUEST for 256 bytes. It asks every 100 ms, and at
  once after new bytes.
- It keeps only bytes that follow what it holds, with a valid CRC, and drops the rest; they are asked for again.
- The record's frames are taken only when the viewer holds the whole size that the same DATA announced.
- **The record's cap:** a DATA announcing a record larger than a replay holds (`KP_WATCH_RECORD_MAX`, `KP_REP_MAX_RECORD`) ends the game where it came to (`KP_WATCH_GONE`, `oversize`): the viewer's room for the record is never grown past it, whatever a forged server announces (`test_watch.a_viewer_takes_no_record_past_a_replays`; the `watch` fuzz target, ADR 0022).
- **Timeouts:** it gives up after 10 s without an answer from a server that has answered. Before any answer it waits
  5 minutes: the host may still be in its lobby. Without the start that is a failure. After the start the record
  becomes whole at what came, and the playback ends there.

## The playback

- The viewer's session is a replay (`KpSessionStart.watch`). Its record grows by `kp_session_watch_grow`, which walks
  the new bytes as a replay's start walks a record and refuses a save or a load. An entry cut before its command id
  or its count waits for the next bytes; an unknown id is refused (only a forged record holds one, and the feed's read could go on
  past it where the walk cannot follow). The feed drops every command the recorder never writes besides, so a forged
  record that the walk reads otherwise than the feed still dispatches no save or load.
- **The end header.** `kp_record_grow` appends an empty block's header of frame F after the received bytes, where F is
  the published frame count. `ReadRecordedFrame` `0x478480` takes a frame only when the next block's header follows
  it, so the header lets the feed dispatch the last block received. The next bytes replace it, and the read position
  goes back onto them.
- The header stays once the record is whole. The viewer therefore runs every block, the game's last one included,
  whenever it came. A file's playback never runs its last block.
- **The read ahead.** Frame f runs only once the blocks up to f + 1 are in the record. The feed reads the next block
  one call early. In 1.17, a frame of more than 0x200 bytes that is read early loses its commands; read at its own
  frame it would not. With the rule, the viewer reads every block when a file's playback reads it.
- **Tests:**
  - `test_watch.a_growing_record_plays_as_the_file`: delays 0, 1, 7 and 25 dispatch the file's commands, frame 20's
    loss included.
  - `test_netplay.a_network_game_is_watched_as_it_goes`: a network game watched 48 frames behind over a lossy link
    equals the finished replay's playback up to its last block.
