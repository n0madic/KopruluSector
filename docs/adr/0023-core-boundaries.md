# ADR 0023: The native core's boundaries

Status: accepted (2026-11-15). The user approved the design inline on 2026-11-15: no mutable static state in the native core (stage A), the simulation and the interface as two libraries (stage B), and the outer layers' reads of the units through a named list of fields, with the interface's writes to a unit through the simulation's functions and out of the state hash (stage C). Stages A, B and C are built.

## Context

Two games and two interfaces have to live in one process: a rewind that shows a preview of the restored game while the played one goes on, tests that run games on threads, a viewer next to a player. Three things stood in the way.

- **One library.** `kp_core` held the simulation and the interface's rules (ADR 0010) in one directory and one library: nothing stopped the simulation from including the interface, and `kp_files.c` did (`kp_ui_pcx` for its PCX helpers, which only the interface's data, the presentation's lookups, the HD interface and the converter call).
- **Static state.** The menus (`kp_ui_menus.c`) kept the controls of their screens in twelve file-scope variables (the popups' procedures, the login, campaign, load and Load Replay screens' dialogs and lists, the episode button's result), so a second interface opening a screen overwrote the first's. The creep borders' rank tables (`kp_creep.c`) and the Ogg CRC table (`kp_webm.c`) were built on first use, a data race between threads even though the values are the same. `kp_ui_establishing_text` returned a static buffer, and the game menu filled a static array on each call.
- **The units' records open to all.** The interface read the units' fields as `g->u.<field>[n]` in 285 places, the scripts' flow (`kp_script_flow.c`) in 31 and the health bars (`kp_health_bar.c`) in 8; any field of the 161 was in reach. The interface also wrote three: the minimap blink's counter and colour (`+0x8a`, `+0x8b`, `kp_ui_minimap.c`, `kp_ui_portrait.c`) and the attack notices' silence (`+0x87`, `kp_ui_voice.c`). All three depend on who the local player is, and all three were in the state hash's units section, so a viewer's and a player's hashes would have differed for the same game.

## Decision

1. **No mutable static or global state in `native/kp_core/src`.** A table is a constant, written out or generated (`tools/native/crc32_table.py` writes the Ogg CRC's next to the CRC-32's); state belongs to `KpGame` or `KpUi`.
   - The creep borders' rank by neighbour mask is one constant table (`KP_CREEP_BORDER_RANKS`, `RANK[SHAPE[mask]]` of BuildCreepShapeTable 0x4bff60 and BuildCreepShapeRanks 0x4c0010); only masks 0..255 index it.
   - The menus' variables are fields of `KpUiMenus` (`popup_procs`, `saved_result`, `login_dialog`, `replay_list` and the rest), with the values the statics started with. `KpUiMenus.modal[].saved_result`, which nothing used, is gone. The episode button draws through `kp_ui_flc_draw_button` instead of keeping the flic button's draw function.
   - The one process-wide variable left is `kp_smk.c`'s `video_codec`, set once before any game (`kp_smk_set_video`).
   - `tools/boundaries.sh` (a step of `tools/ci.sh`) reads the sources for a static that is no function and no constant, and fails on any but `video_codec`.
2. **Two libraries** (stage B). `native/kp_core/src/core` and `src/defs` build `kp_sim`; `src/ui` builds `kp_ui`, which links `kp_sim`. `kp_sim`'s include paths do not hold `src/ui`, so an include of the interface from the simulation fails to compile; the C++11 check sees both.
   - `src/ui` holds the interface's rules (`kp_ui*.{h,c}`) and what only the interface and the layers over it use: the portraits' movies (`kp_smk`, `kp_index_map`) and the fog's view (`kp_fog_view`, presentation state, ADR 0009).
   - The shared readers stay in the simulation: `kp_kpf`, `kp_files`, `kp_text`, `kp_utf8`, `kp_wav`, `kp_grp`; `kp_draw.c` too (it reads `kp_images_internal.h`).
   - The PCX helpers moved to the interface as `kp_ui_pcx_pixels`, `kp_ui_pcx_file` and `kp_ui_pcx_prefix` (`kp_ui_data.h`).
   - `kp_present` and `kp_oracle_lib` link `kp_ui`; no target links only `kp_sim` yet (the tools reach the interface through the data, `kp_data.h`, and the session, `kp_session.h`).
3. **The units through named fields** (stage C). `KP_UNIT_VIEW_FIELDS` in `kp_game.h` lists the 53 fields that the interface, the scripts' flow and the health bars read; each gets `kp_unit_get_<field>(g, n)` (`kp_unit_get_loaded(g, n, k)` for the eight transport slots). The other fields have no getter outside the simulation.
   - The interface's writes go through four functions of the simulation (`kp_combat.c`, next to `kp_notify_attacked`, which sets the same fields): `kp_unit_set_blink` (a talking portrait's ping, RequestUnitTalking 0x4a7200), `kp_unit_blink_step` (the minimap's count-down, DrawLocalMinimapUnits 0x46e490), `kp_unit_silence_attack_notice` and `kp_units_tick_attack_notices` (the attack notices, ADR 0010).
   - These three fields (`KP_UNIT_VIEW_STATE`: `attack_notify_timer`, `last_event_timer`, `last_event_color`) are the units' view state. The save keeps them in a section of their own, `VIEW`, after `WRLD`; the state hash's total folds every section but `VIEW`, and the save's table lists `VIEW`'s hash too, which the load checks. `KP_SAVE_VERSION` is 3 (schema `0x009a8709ec54eb16`), `KP_HASH_VERSION` 2.
   - `tools/boundaries.sh` fails on `->u.` or `KpUnits` in `native/kp_core/src/ui`, `native/kp_app` and `native/kp_present`. The tests and the oracle's tools reach the records on purpose and are not checked.
   - TODO: the local selection (`SELN`: `local_selection`, `circle_colors`, the sprites' selection flags) is view state as well, but the simulation's commands read it; it stays in the hash until it moves out of `KpGame`.
   - TODO: `KpUi.game` stays a mutable pointer. A `const` one needs the rest of the local player's state out of `KpGame` (the selection, the screen's and the placement's scalars, `crt`, `events`).
4. **Caches of the tools stay per process.** `kp_runner.c`'s data roots and speech sources serve one thread; `kp_test` runs its jobs as processes. A lock comes with the first tool or test that runs games on threads (a TODO in `kp_runner.c`).

## Consequences

- `test_kp_core.creep_border_ranks_are_the_original_tables` rebuilds the two tables as the original does and compares the 256 ranks.
- `test_hud.two_interfaces_keep_their_own_screens` opens Load Replay on two interfaces whose screens are other controls; each keeps its own dialog and list.
- `test_save.view_state_is_saved_outside_the_total` changes the three view fields through the functions: the total and the other sections' hashes stay, `VIEW`'s changes, and a save and a load bring the values back.
- The tests' own statics (host callbacks' counters in `test_hud.c`, `test_kp_ui.c`, `kp_fuzz.c`) stay: the suites run as processes.
