# ADR 0027: Replays over the network and the viewers of a network game

Status: accepted (2026-11-21). The user approved both designs inline on 2026-11-21. The details are in
[phase N](../phase-network.md#replays-over-the-network-and-the-viewers-2026-11-21) and
[watch-udp.md](../formats/watch-udp.md).

## Context

1.17's Create Game lists replays besides maps: an entry of kind `0x80` (`CreateGameFromEntry` `0x45bfc0`) hosts a
network game that plays the replay on every seat, each seat in lockstep over Storm's turns. Koprulu's lobby refused
that entry until now (a `TODO` in `kp_netui.c`).

1.17 has no way to watch a network game that is being played. The user asked for a viewer that follows a game from
its growing record.

## Decision

1. **The hosted replay is the original's.** The host's Create Game takes a replay of a network game (one whose seed
   record is the lobby's `0x48`; a single-player replay cannot be hosted). Its header is the game's, and joiners
   receive the `.rep` by the map transfer.
   - Each seat's session is a replay (`kp_turns_net_replay`): the record feeds the commands before the turn is
     polled (`RunPendingFrames` `0x444e30`).
   - The network's turns pass the live filter `0x47636c`: keep-alive, pause, resume, the sync commands, latency, the
     replay speed. The first other command ends the buffer, as `0x476352` never advances past it.
   - A seat whose Storm ID has no record runs its speed command as record 0.
   - Koprulu's network replays now carry the lobby's seed record (`SetReplayGameSeedRecord` `0x4807f0`). Before, it
     was zero, so a hosted replay's start seed was wrong.
2. **The viewers use Koprulu's own protocol on a port of its own** (`KP_WATCH_PORT` 6120), outside Storm.
   - Nothing goes on Storm's wire, which ADR 0026's decision 7 keeps the original's.
   - The protocol is a stateless pull: "the part's bytes from an offset". An answer is never larger than its request,
     so a forged source address gets no amplification.
   - A viewer holds at most a replay's record (`KP_WATCH_RECORD_MAX`, since 2026-11-27): a server announcing more
     ends the game where it came to, so a forged server cannot make it grow its room without bound. The host's
     `--watch-port off` serves no viewer.
3. **A viewer is a replay whose record grows.** The playback is the replay feed, with an end header after the bytes
   received, and it waits past what it holds. Nothing changes in the `.rep` or save formats.
4. **The delay is the server's**, 60 seconds by default (`--watch-delay`). The server holds the record back, so a
   viewer cannot read ahead of it.
5. **No new buttons in the original's screens.** `koprulu --watch HOST[:PORT]` and the script's `watch "ADDRESS"`
   start a viewer.

## Consequences

- **The last block.** A viewer runs the game's last block, which a file's playback never runs: the end header
  follows it. A viewer equals the finished replay's playback up to that block. Every viewer agrees, whenever it
  joined.
- **The read-ahead rule.** A frame runs only once the next frame's blocks are in. 1.17's command loss happens when
  the feed reads a frame early; with the rule it reads it at the same call as a file's playback does.
- **No glue.** `--watch` runs at once without the glue, as `--map` does: a game started under a glue screen would
  keep the screen's menus (the `TODO` of the script's `replay`). At the end the playback stays at its result.
- **The web has no sockets,** so it can neither serve nor watch.
- **Loads.** A save's load in the game ends what the server publishes, since the record's frames go back.
