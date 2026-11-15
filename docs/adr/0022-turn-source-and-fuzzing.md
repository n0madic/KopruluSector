# ADR 0022: The turn source and fuzzing

Status: accepted (phase T, 2026-11-15). The user approved the design of phase T inline on 2026-11-15, with stage T3 first ([ADR 0021](0021-state-walk-and-saves.md#addition-the-dumps-coverage-stage-t3)). Stage T1, the turn source, is built; stage T2, the fuzzing, is planned and gets its section here when it is built. Stages and their state: [docs/phase-turns.md](../phase-turns.md).

## Context

Each step of the session (`kp_session_step`) gathered its own commands. They came from the interface's queue and, in a replay, from the record's feed (`FeedReplayCommands` 0x4769b0), with the Storm ID to player map, the frame count, the end and the playback's filter of the live commands kept in the session's own fields. The recorder took the local player's commands from the queue. A network game needs a third source whose frame can be not ready, and a recorder that keeps every seat's commands. The session also had to stop exposing those fields to the flow, the scripts and the tests.

## Decision

1. **One source of turns** (`native/kp_app/kp_turns.{h,c}`, `KpTurns` in `KpSession.turns`). The session asks it for each frame's commands (`kp_turns_frame`) and tells it when they ran (`kp_turns_done`).
2. **Two kinds, an enum and a switch**, not a table of functions: there are two real sources, and concrete comes before generic.
   - `KP_TURNS_LOCAL`: the commands queued for the next step, each a whole buffer of the player it was queued for (`kp_execute_command` runs one command a buffer, as the interface queues them).
   - `KP_TURNS_REPLAY`: the record's commands of the frame, each buffer walked as `ExecuteTurnCommands` 0x475d30 walks it (`kp_record_split`) and played as the player whose record has its Storm ID; then the live commands the playback lets through.
3. **The playback's filter at the submit.** `kp_turns_submit` refuses in a replay what the step used to drop: everything but the keep-alive, restart, pause and resume, the sync, the network's 0x37-0x3b, latency and the replay speed (table 0x47636c). A save's queued commands are restored through it too.
4. **The frame's status.** `KP_TURNS_READY`, `KP_TURNS_ENDED` once at the frame the record passes the header's frame count (the session opens the victory dialog and pauses the playback, as before; the live commands still come) and `KP_TURNS_NO_MEMORY` (the session fails the game). A network source adds "not ready": the step does not run and the clock keeps no missed steps (a TODO in `kp_turns.h`).
5. **The recorder watches the executed list** (`kp_turns_record`), not the queue. It records each command of a buffer whose player has a Storm ID: in a game played alone the local player, ID 0, which gives the same bytes as before. A network source fills the seats of every player; a viewer is a replay over a growing record.
6. **The session keeps the recorder** (`KpSession.rec`, a recorded game's only) and the recorded header, loads and loss. `pending`, `playing`, `replay_*` and the step's list left the session. The flow and the scripts read `kp_session_playing`, `kp_session_replay_ended` and `kp_session_replay_frames`.
7. **Nothing else changes.** The save (CMDS from the source's queue, REPL from the recorder), the replay file, `KP_SAVE_VERSION` and `KP_REP_SIM_REVISION` stay the same. All in one thread, synchronously from the session's step: the web has no threads and determinism matters.

## Consequences

- The tests that bind the behaviour to the original are unchanged and pass: the 65 replays against the original's playback at every step, every golden scenario, play and idle run recorded and played back (`test_session.replay_*_play_back`), the three records byte for byte (`session_replay_records_as_the_original`), the product scripts.
- `test_session.turns_local_queue_and_recording` and `turns_replay_feed_and_live_commands` test the source without a game: the queue's order, players and limits; the recording's seats and the split of a buffer; the feed's Storm ID map with the player kept for an unknown ID; the filter; the end given once.
- A command a playback does not let through is refused when it is submitted instead of taking a place in the queue until the step. The interface ignores the refusal, as it ignored the drop.
