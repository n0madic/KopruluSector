# ADR 0023: The native core's boundaries

Status: accepted (2026-11-15). The user approved the design inline on 2026-11-15: no mutable static state in the native core (stage A), the simulation and the interface as two libraries (stage B), and the outer layers' reads of the units through a named list of fields, with the interface's writes to a unit through the simulation's functions and out of the state hash (stage C). Stage A is built.

## Context

Two games and two interfaces have to live in one process: a rewind that shows a preview of the restored game while the played one goes on, tests that run games on threads, a viewer next to a player. Three things stood in the way.

- **Static state.** The menus (`kp_ui_menus.c`) kept the controls of their screens in twelve file-scope variables (the popups' procedures, the login, campaign, load and Load Replay screens' dialogs and lists, the episode button's result), so a second interface opening a screen overwrote the first's. The creep borders' rank tables (`kp_creep.c`) and the Ogg CRC table (`kp_webm.c`) were built on first use, a data race between threads even though the values are the same. `kp_ui_establishing_text` returned a static buffer, and the game menu filled a static array on each call.

## Decision

1. **No mutable static or global state in `native/kp_core/src`.** A table is a constant, written out or generated (`tools/native/crc32_table.py` writes the Ogg CRC's next to the CRC-32's); state belongs to `KpGame` or `KpUi`.
   - The creep borders' rank by neighbour mask is one constant table (`KP_CREEP_BORDER_RANKS`, `RANK[SHAPE[mask]]` of BuildCreepShapeTable 0x4bff60 and BuildCreepShapeRanks 0x4c0010); only masks 0..255 index it.
   - The menus' variables are fields of `KpUiMenus` (`popup_procs`, `saved_result`, `login_dialog`, `replay_list` and the rest), with the values the statics started with. `KpUiMenus.modal[].saved_result`, which nothing used, is gone. The episode button draws through `kp_ui_flc_draw_button` instead of keeping the flic button's draw function.
   - The one process-wide variable left is `kp_smk.c`'s `video_codec`, set once before any game (`kp_smk_set_video`).
   - `tools/boundaries.sh` (a step of `tools/ci.sh`) reads the sources for a static that is no function and no constant, and fails on any but `video_codec`.
2. **Caches of the tools stay per process.** `kp_runner.c`'s data roots and speech sources serve one thread; `kp_test` runs its jobs as processes. A lock comes with the first tool or test that runs games on threads (a TODO in `kp_runner.c`).

## Consequences

- `test_kp_core.creep_border_ranks_are_the_original_tables` rebuilds the two tables as the original does and compares the 256 ranks.
- `test_hud.two_interfaces_keep_their_own_screens` opens Load Replay on two interfaces whose screens are other controls; each keeps its own dialog and list.
- The tests' own statics (host callbacks' counters in `test_hud.c`, `test_kp_ui.c`, `kp_fuzz.c`) stay: the suites run as processes.
