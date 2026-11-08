# Scripted runs

Design: [ADR 0017](adr/0017-scripted-runs.md); the format: [docs/formats/script.md](formats/script.md). The plan was approved on 2026-11-08 with the development checks ported to scripts and their flags removed; the user chose a line format of our own over an embedded language.

| Stage | What | State |
|---|---|---|
| SC1 | The parse and the run with no SDL: `kp_script.{h,c}`, `test_script.c` over a recording host | done 2026-11-08 |
| SC2 | The product's host: `--script`, `--script-out`, the header before SDL, the sandboxed profile, exit codes 0–3, the watchdog, `kp_app_now(a)`; `kp_script_flow.c` with `game`, `dialog`, `nodialog`, the points `X Y`, `world`, `control` and the commands `key`, `click`, `press`, `release`, `camera`, `pause`, `resume`; `tests/scripts/smoke_*.kps`, `app_script` in `tools/ci.sh` | done 2026-11-08 |
| SC3 | The other values and conditions, `state` (`kp_dump`), `ui` (the open windows' tree); a script over a real `KpFlow` in `test_flow.c` | |
| SC4 | `clock fixed`: `determinism.kps` run twice equal byte for byte, a script with another order differs | |
| SC5 | The checks as scripts, in the order video, touch, save, play, HD; each beside its flag in CI first, each with a patch that breaks what it guards | |
| SC6 | The flags `--video-check`, `--touch-check`, `--save-check`, `--play-check`, `--hd-check` and their state machines removed | |
| SC7 | `--script-live FIFO` on desktops | |

## SC1 as built

- **`native/kp_app/kp_script.{h,c}`** in `kp_app_core`, no SDL: the parse with its errors (about 1000 lines) and the run (about 300). A file is parsed into an array of steps (at most 16,384), each with its file and line; the host resolves every name while the file is parsed, through `KpScriptHost.name`, and reads an included file through `read`.
- **The run** is `kp_script_frame(run, host, now_ms)` once a frame: the observations run at once, an input ends the frame's steps, `&` chains the next input into the same frame, a wait ends them until it holds, `until`'s input is the step after it. A mark keeps only the values a later `@mark.value` reads (found while parsing). A value with none now keeps a wait waiting and aborts `expect` and `mark`. A failed `check` is counted and the run goes on; a wait past its time, a failed `expect`, an input or an observation the host cannot run abort it, and the host is told (`abort`) so it can write `failure.*`. The run takes its time from the host: which clock that is and when it stops is the host's (SC2, SC4).
- **`tests/c/test_script.c`**, 11 tests over a recording host: every command and form parsed (43 steps); the header's defaults; about 60 refused lines (unknown commands and names, a `..` or a slash in an include, a bad mark or pick, `&` on a line that is no input or before a line that is none, numbers past their range, a 512-byte line, a 64-byte word, 33 words, a NUL); includes and their errors' files and lines; the frames' order, `&`, waits holding and timing out, `until` (its input again every 0.25 s), `expect` aborting while `check` counts, marks, the host's abort. Natively, under LLVM 23.1's ASan, UBSan and LSan, in wasm under node; `kp_cxx_check` compiles both files as C++11.

### Differences from the plan

- **`select NAME RADIUS [optional]`** takes the host's names (`worker`, `main`, `group`), not a fixed `worker|main|fighter`: the names come with SC5's HD script.
- **`expect COND`** takes a full condition, named terms included, not only `A OP B`.
- **`camera X Y`** centres the view on a world point; `camera by DX DY` and `camera max` (the map's bottom-right, for `save.kps`) were added.
- **Movies and the clock:** the plan has the fixed clock and the deadlines stop while a movie plays. The run only reads the time the host gives, so the choice moves to SC2: the deadlines may instead keep the real time while the game's clock waits.

## SC2 as built

- **`native/kp_app/sdl/kp_app_script.c`**, the product's host: the keys' names (letters, digits, F1–F12 and a table), the inputs as SDL's events through `kp_input_event` (a key with its modifiers, its text and timestamps from `SDL_GetTicksNS`, as `kp_app_cine_skip` compares them; the mouse at a logical pixel's centre through the inverse of the window's scale), `shot` through `kp_dev_screenshot` (the `--screenshot` code, now with its failure reported), `hang`, the abort's `failure.png`, the run's end with `quit`. `kp_dev_frame` hands the frame to the script, after the frame is drawn and before it is shown.
- **`native/kp_app/kp_script_flow.{h,c}`** in `kp_app_core`, no SDL: the values `tick`, `camera.x`, `camera.y` (the view's centre), the conditions `game`, `dialog [NAME]`, `nodialog`, `paused`, 36 dialogs by their BIN's stem, the controls `ok`, `cancel` and the game menu's seven, the points (`X Y`, `world`, `control` placed through the canvas's places, a glue popup through its screen's, `center`, `view`), the actions `camera`, `pause`, `resume`. A failed wait says what it saw: `open: minimap, ttextbox, stat_f10, …`.
- **`kp_app_main.c`:** `--script`, `--script-out`; the script is read (`SDL_LoadFile`, then `kp_app_read` for a phone's assets) and its header applied in `SDL_AppInit` before the flags' last settings (vsync off, the index path's block for `video originals`); refusals exit with 2; a start that fails with 2; the player's input and the focus events ignored, a closed window fails the run; `SDL_AppQuit` calls `exit(code)` after `SDL_Quit` on the desktops (`KP_EXIT_CODES`).
- **The clock:** `kp_app_now(a)` replaces `kp_app_now(void)` at every call; the frame limiter, the frame's timing and `a->started` take `kp_app_wall()`. It returns the real time until SC4 (a `TODO:` in `kp_app_flow.c`).
- **The sandbox:** `a->pref` is `DIR/pref/`, created; `SDL_GetPrefPath` is not called when a script runs; `kp_app_save_profile` writes there even in the checks' mode. `kp_flow_frame_cap` gives a script no throttle.
- **The watchdog:** a thread of its own (1 s ticks on a condition variable) reads the last frame's time (an atomic), and past the script's `watchdog` writes its line and calls `_Exit(3)`; the run's end wakes and joins it before the script is freed (an SDL timer was dropped: `SDL_RemoveTimer` does not wait for a callback that is running). Not on the web or the phones (`KP_SCRIPT_WATCHDOG`).
- **`tests/scripts/`:** `smoke_pass.kps` (terran01: the camera, a mark and `expect`, the pause, `wait tick > @start.tick + 8`, F10's game menu and Return to Game clicked at its control), `smoke_timeout.kps`, `smoke_syntax.kps` (`key F1O`), `smoke_hang.kps`.
- **`tools/ci.sh`:** `app_script NAME CODE APP [FLAGS]` beside `app_check`, its files in `build/script/NAME` (emptied first); with `KOPRULU_APP` the step runs the four smoke scripts and `--autotest` beside a script, and checks each exit code (0, 1, 2, 3, 2), the summary or failure lines, `menu.png`, `failure.png` and the run's `pref/`.
- **`test_flow.script_names_hold_over_the_flow`:** the names over a real flow: `--mission`'s game until `game` holds, the camera's three forms and the clamp, the pause holding the tick, F10's menu named first in `open:`, Return to Game's point clicked through `kp_hud_input` closing it, a point past the screen refused; the glue's main menu is `the glue` and `dialog gluMain` with no popup.
- **`kp_script_line`** leaves a line's comment out: the FAILED line quotes `wait dialog gamemenu`, not its `# …`.

### The review's fixes (2026-11-08)

- **A dialog named as a command:** a condition's argument refused every command's name, so `wait dialog quit` and `wait dialog wait` were parse errors; only an input's command is reserved now (until's input follows its condition). `test_script.a_dialog_may_be_named_as_a_command`.
- **The watchdog's end:** a timer's callback could still be running when the script was freed; the watchdog is a joined thread now (above).
- **A refused start** (no such mission, `--script-out` not made) logged a step 1 `FAILED` and a summary after `script: <why>`; the refusal's line is the only one now.
- **`view 100% 100%`** was one past the screen's last pixel and always failed; 100% is the last pixel now. `test_flow.script_names_hold_over_the_flow` places the `world`, `center` and `view` points too.
- **`kp_script_line`** read `"a\\"` as a string not yet closed and kept the comment after it; it follows the parse's escapes now.
- **`tools/ci.sh`** runs the `--autotest` refusal before the passing run, which keeps its files in `build/script/smoke_pass`.
- The host's mouse points come from `kp_input_window_point` in `kp_app_input.c`, beside the mapping they invert; key names compare with `SDL_strcasecmp`.

### Acceptance

Run in `build/app-script` (`--headless --window 1280x720`, from the source tree's `generated/`):

| Script | Exit code | Lines |
|---|---|---|
| `smoke_pass.kps` | 0 | `script smoke_pass: 19 steps, 0 failures`; `start.png`, `menu.png`, `end.png` at 2560×1440 |
| `smoke_timeout.kps` | 1 | `smoke_timeout.kps:7: wait dialog gamemenu: no change in 2001 ms: open: minimap, ttextbox, stat_f10, … FAILED` (the time the wait took), `failure.png` |
| `smoke_syntax.kps` | 2 | `script: smoke_syntax.kps:5: unknown key "F1O"` |
| `smoke_hang.kps` | 3 | `script smoke_hang: no frame in 2824 ms: the watchdog ends the program FAILED` |
| without `--script-out`; beside `--autotest` | 2 | `--script needs --script-out DIR`; `--autotest cannot run with --script` |

- **The shots and `--screenshot`:** the same code writes both, the same format (2560×1440, RGBA, 8 bits). terran01's first frames by `shot start` and by `--mission terran/terran01 --play --after 2 --screenshot` are equal in 3,685,039 of 3,686,400 pixels and in all of the console's band; the 1,361 others are the animations of the real time between the two frames. A comparison byte for byte waits for SC4's fixed clock.
- **The player's profile:** `~/Library/Application Support/KopruluSector/KopruluSector/profile.kpprof` kept its SHA-1 and its time through the runs, and its directory was not changed by them.
- **A race found by the runs:** the frame's mark is the ticks `| 1` (0 is "no frame yet"), 1 ms ahead of `SDL_GetTicks` on an even millisecond, and the watchdog's unsigned `now - last` read that as 2³²−1 ms: `smoke_timeout` once ended with code 3. The difference is now signed 32 bits; six runs after the fix gave 1 and 3, and the CI step's lines passed twice in a row on `build/app-script`'s binary (not yet through `tools/ci.sh` with `KOPRULU_APP`, which builds `dist/product`).
- **C suites:** 463 natively (`kp_test -j 8`), under LLVM's ASan, UBSan and LSan, in wasm under node (after the review's fixes; the four smoke scripts' codes again 0, 1, 2, 3 and 2 on `build/app-script`).

### Differences from the plan

- **Dialogs are named by their BIN's stem** (`gamemenu`, `gluMain`), in any case, not by the plan's `GameMenu`; a script's names are the files' and need no table of ours for each.
- **Movies and the clock:** still the real clock (the choice of SC1's note waits for SC4).
- **`failure.ui` and `failure.state`** come with SC3's `ui` and `state`; an abort writes `failure.png` only.
- **The phones' autopause** is not lifted yet (a `TODO:` in `SDL_AppEvent`): a phone's script loses its game's clock to a pause it did not ask for. No script runs on a phone before SC5.

## Open

- The in-game `open:` list holds the console's own dialogs (`minimap`, `ttextbox`, `stat_*`), which have no names in the dialogs' table: a script cannot name them, and a failure's why is long.
- The web build with a script (IDBFS kept, no exit code, no watchdog) and Linux and Windows (`exit(code)`) are not run yet; a window closed during a run (exit code 1) is not tried.
- From `--headless` (2026-11-07): not tried on a sleeping display, a locked screen or over SSH; Linux and Windows not run with the flag.
