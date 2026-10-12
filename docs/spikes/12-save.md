# Spike 12: binary save files on every platform

Plan: phase 0 item 12, D17. Code: `spikes/12-save/save.script`.

## Method

The spike runs four checks:
- Write 2 MB containing every byte value to `sys.get_save_file(app, "state.bin")` with `io.open(..., "wb")`, then read it back and compare.
- Write a marker file. The next launch reports whether the previous marker survived: a persistence check across process restarts. On HTML5 the second launch reuses the same browser profile.
- `sys.save` / `sys.load` of a table holding binary strings of 64 KB, 400 KB and 600 KB.
- A final `sys.save` that also flushes the HTML5 file system to IndexedDB.

## Results

| | macOS | HTML5 (headless Chrome) | Android emulator | Galaxy Z Fold7 |
|---|---|---|---|---|
| Save path | `~/Library/Application Support/<app>/` | `/data/.<app>/` (IndexedDB-backed FS) | `/data/user/0/<package>/files/` | same |
| io write 2 MB / read back | 3.2 / 3.3 ms, identical | ~0 / 8 ms, identical | 4.4 / 6.3 ms, identical | 0.5 / 0.9 ms, identical |
| Marker survives relaunch | yes | yes | yes | not run (single launch) |
| `sys.save` 64 / 400 / 600 KB string | round-trip ok | round-trip ok | round-trip ok | round-trip ok |

The documented `sys.save` limit is a 512 KB output buffer. In 1.13.2 a 600 KB string still round-trips, so the documented limit is not enforced at that size. D17 does not rely on this either way.

## Decisions

- Saves are binary files written with `io.open` at `sys.get_save_file` paths on every platform. The format is the structural snapshot prototyped in `game/simulation/snapshot.lua` (D17).
- **HTML5.** `io` writes go to the in-memory FS and reach IndexedDB on a sync. The save routine must call `sys.save` on a small settings table after writing a save file (the spike does this), so the write persists. The persisted marker proves this works.
- `sys.save` stays for settings and campaign progress only, as planned.

## Not measured

- iOS (no device).
- Browser storage quota with many saves.
- Behaviour when the app is killed mid-write: an atomic replace (write temp file, then rename) is to be tested when the save format exists (phase 3/M0E E4).
