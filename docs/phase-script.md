# Scripted runs

Design: [ADR 0017](adr/0017-scripted-runs.md); the format: [docs/formats/script.md](formats/script.md). The plan was approved on 2026-11-08 with the development checks ported to scripts and their flags removed; the user chose a line format of our own over an embedded language.

| Stage | What | State |
|---|---|---|
| SC1 | The parse and the run with no SDL: `kp_script.{h,c}`, `test_script.c` over a recording host | done 2026-11-08 |
| SC2 | The product's host: `--script`, `--script-out`, the header before SDL, the sandboxed profile, exit codes 0–3, the watchdog, `kp_app_now(a)`; `kp_script_flow.c` with `game`, `dialog`, `nodialog`, the points `X Y`, `world`, `control` and the commands `key`, `click`, `press`, `release`, `camera`, `pause`, `resume`; `tests/scripts/smoke_*.kps`, `app_script` in `tools/ci.sh` | done 2026-11-08 |
| SC3 | The other values and conditions, `state` (`kp_dump`), `ui` (the open windows' tree); a script over a real `KpFlow` in `test_flow.c` | done 2026-11-08 |
| SC4 | `clock fixed`: `determinism.kps` run twice equal byte for byte, a script with another order differs | done 2026-11-08 |
| SC5 | The checks as scripts, in the order video, touch, save, play, HD; each beside its flag in CI first, each with a patch that breaks what it guards | done 2026-11-08 |
| SC6 | The flags `--video-check`, `--touch-check`, `--save-check`, `--play-check`, `--hd-check` and their state machines removed | done 2026-11-09; the scripts first ran on the Fold7 on 2026-11-11 (below) |
| SC7 | `--script-live FIFO` on desktops | done 2026-11-09 (macOS, Linux) |

## SC1 as built

- **`native/kp_app/kp_script.{h,c}`** in `kp_app_core`, no SDL: the parse with its errors (about 1000 lines) and the run (about 300). A file is parsed into an array of steps (at most 16,384), each with its file and line; the host resolves every name while the file is parsed, through `KpScriptHost.name`, and reads an included file through `read`.
- **The run** is `kp_script_frame(run, host, now_ms)` once a frame: the observations run at once, an input ends the frame's steps, `&` chains the next input into the same frame, a wait ends them until it holds, `until`'s input is the step after it. A mark keeps only the values a later `@mark.value` reads (found while parsing). A value with none now keeps a wait waiting and aborts `expect` and `mark`. A failed `check` is counted and the run goes on; a wait past its time, a failed `expect`, an input or an observation the host cannot run abort it, and the host is told (`abort`) so it can write `failure.*`. The run takes its time from the host: which clock that is and when it stops is the host's (SC2, SC4).
- **`tests/c/test_script.c`**, 11 tests over a recording host: every command and form parsed (43 steps); the header's defaults; about 60 refused lines (unknown commands and names, a `..` or a slash in an include, a bad mark or pick, `&` on a line that is no input or before a line that is none, numbers past their range, a 512-byte line, a 64-byte word, 33 words, a NUL); includes and their errors' files and lines; the frames' order, `&`, waits holding and timing out, `until` (its input again every 0.25 s), `expect` aborting while `check` counts, marks, the host's abort. Natively, under LLVM 23.1's ASan, UBSan and LSan, in wasm under node; `kp_cxx_check` compiles both files as C++11.

### Differences from the plan

- **`select NAME RADIUS`** takes the host's names (`worker`, `main`, `group`), not a fixed `worker|main|fighter`: the names come with SC5's HD script.
- **`expect COND`** takes a full condition, named terms included, not only `A OP B`.
- **`camera X Y`** centres the view on a world point; `camera by DX DY` and `camera max` (the map's bottom-right, for `save.kps`) were added.
- **Movies and the clock:** the plan has the fixed clock and the deadlines stop while a movie plays. The run only reads the time the host gives, so the choice moves to SC2: the deadlines may instead keep the real time while the game's clock waits.

## SC2 as built

- **`native/kp_app/sdl/kp_app_script.c`**, the product's host: the keys' names (letters, digits, F1–F12 and a table), the inputs as SDL's events through `kp_input_event` (a key with its modifiers, its text and timestamps from `SDL_GetTicksNS`, as `kp_app_cine_skip` compares them; the mouse at a logical pixel's centre through the inverse of the window's scale), `shot` through `kp_dev_screenshot` (the `--screenshot` code, now with its failure reported), `hang`, the abort's `failure.png`, the run's end with `quit`. `kp_dev_frame` hands the frame to the script, after the frame is drawn and before it is shown.
- **`native/kp_app/kp_script_flow.{h,c}`** in `kp_app_core`, no SDL: the values `tick`, `camera.x`, `camera.y` (the view's centre), the conditions `game`, `dialog [NAME]`, `nodialog`, `paused`, 36 dialogs by their BIN's stem, the controls `ok`, `cancel` and the game menu's seven, the points (`X Y`, `world`, `control` placed through the canvas's places, a glue popup through its screen's, `center`, `view`), the actions `camera`, `pause`, `resume`. A failed wait says what it saw: `open: minimap, ttextbox, stat_f10, …`.
- **`kp_app_main.c`:** `--script`, `--script-out`; the script is read (`SDL_LoadFile`, then `kp_app_read` for a phone's assets) and its header applied in `SDL_AppInit` before the flags' last settings (vsync off, the index path's block for `video originals`); refusals exit with 2; a start that fails with 2; the player's input and the focus events ignored, a closed window fails the run; `SDL_AppQuit` calls `exit(code)` after `SDL_Quit` on the desktops (`KP_EXIT_CODES`).
- **The clock:** `kp_app_now(a)` replaces `kp_app_now(void)` at every call; the frame limiter, the frame's timing and `a->started` take `kp_app_wall()`. It returned the real time until SC4.
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
- **`failure.ui` and `failure.state`** came with SC3's `ui` and `state` (below).
- **The phones' autopause** is not lifted yet (a `TODO:` in `SDL_AppEvent`): a phone's script loses its game's clock to a pause it did not ask for. No script runs on a phone before SC5.

## SC3 as built

- **`kp_script_flow.c`'s values** (29): the game's (`tick`, `selected`, `units:N`, `creep.tiles`, `creep.receding`, `trigger.waiting`), the world's (`camera.x`, `camera.y`, `camera.tile.x`, `camera.tile.y`), the console's (`minimap.x`, `minimap.y`, `portrait.ms`, `targeting`, `placing`, `groups.held`, `pad.open`, `pad.count`, `pad.shown:N`) and the interface's (`gamma`, `portraits`, `video.graphics`, `.fog`, `.water`, `.font`, `.lighting`, `.serial`, `video.commits`, `text.vector`); a value whose source is missing says which (`no game`, `no console`, `no interface`). The host's own 12, read in `kp_app_script.c`: `art.scale`, `art.lit`, `art.lit.pages`, `art.fx`, `hd.ui`, `cursor.shown`, `mouse.x`, `mouse.y`, `music.playing`, `music.track`, `sounds.started`, `sounds.streamed`.
- **The conditions** `glue`, `still` (no panel sliding or waiting to slide: `hc_glue_still` moved from `kp_app_dev.c` as `kp_script_flow_still`, which `hc_ready` calls now), `screen [NAME]` (still, no popup, the screen NAME), `room`, `movie`, `nomovie` (the host's `cine_active`) beside SC2's four. A glue condition's why names the glue's dialogs, a ready room and moving panels.
- **The dialogs:** `quitrepl` and the console's twelve (`minimap`, `ptextbox`, `ttextbox`, `ztextbox`, `stat_f10`, `statbtnp`, `statbtnt`, `statbtnz`, `statdata`, `statlb`, `statport`, `statres`). The console's are left out of `dialog`, `nodialog` and the `open:` list: a failed wait in a game with no menu says `open: no dialog`.
- **The controls:** any control as `DIALOG.ID` (`video.12`, `gamemenu.-3`), and the glue's names for the main menu, the game mode, the login, both campaign menus and the load screen (14).
- **`state NAME`** writes `kp_dump(g, 1, …)`. **`ui NAME`** writes the open dialogs' tree, topmost first: each dialog's box on the screen and its role, each control's `DIALOG.ID`, kind, centre and box in logical pixels, text without colour codes, hotkey, flags and table names, a list's items ([the format](formats/script.md#the-observations-files)). The host writes both through `SDL_IOStream`, opening the file at the first line, so a refused observation (`state` in the glue: `no game`) leaves no empty file. An abort adds `failure.ui` (with an interface) and `failure.state` (with a game) to `failure.png`.
- **`until`'s steps:** its input counts as a step once it was sent, so the summary's N matches the lines that ran (`video_menu`: 19, not 18); `test_script.until_repeats_its_input` holds both counts.
- **`tests/c/test_flow.c`** runs scripts through `kp_script_frame` over a real flow, with a host of the test's own (keys, text, the mouse through `kp_glue_input` and `kp_hud_input`, `ui`, `state`):
  - **`a_script_walks_the_glues_menus`:** from the main menu, Single Player, the expansion, a new player's name typed, the login, the campaign menu, the load screen and back, out to the main menu, each screen waited for by `screen`. Its `ui` holds `"Single Player" key S = glumain.single` and the new name as a list's selected item. The profile is named `Script`.
  - **`a_script_observes_a_game`:** protoss01's state (more than 100 lines), F10's menu by its `ui` (`gamemenu.-3 default-button … "Return to Game (Esc)" key Escape = cancel, gamemenu.return`), closed by its name. Every value is read in a game; the game's values have none in the glue, while the interface's have one. Refused at parse time: `units:12` and `pad.shown:12` (a value's argument has its range in the flow's names, `KpScriptName.arg_max`), `gamemenu.x`, `nodialog.3`, `dialog minimap` and `screen statdata` (the console's dialogs are no condition's).
- **The review's fixes** (`/code-review`, 2026-11-08):
  - An abort's files are written through the host's `aborted` hook, after the step's FAILED line and before the summary, which is the log's last line again (the web's and the phones' result); `test_script` holds the order and that a stop from outside calls no hook.
  - `ui` names a hotkey as `key` takes it (`Minus`, `Tab`; `hotkey \xNN` when no key name gives it), marks `ok` and `cancel` only on the dialog `control ok` clicks, cuts a text before a UTF-8 character it would split, and reads `checked` from the low byte the drawing reads.
  - Observation names are compared in any case (`ui a` then `ui A`, `state Failure` are parse errors): macOS's and Windows's file systems ignore it.
  - The console's dialogs are a table of their own (no index kept by hand), and `text.vector` is a case of its own (an unknown value is an error, not the last one's).
- **`tests/scripts/video_menu.kps`:** the start of `--video-check` (zerg01, F10, O, V by their keys, the video dialog's tree, Escape until no dialog, the options as they were). `tools/ci.sh` checks it as the fifth script, plus `failure.ui` and `failure.state` beside `smoke_timeout`'s `failure.png`.

### Acceptance

On `build/app-script` (`--headless --window 1280x720`):

- **`video_menu.kps`:** exit code 0, `script video_menu: 19 steps, 0 failures`.
  - The options it logged: `video.*` -1, `video.serial` 0, `video.commits` 0, `gamma` 100, `portraits` 2, `art.scale` 2, `art.lit` 1, `art.fx` 0, `hd.ui` 1, `text.vector` 1.
  - `start.state` (943 lines), `video.ui` (165 lines) and both shots were written.
  - `video.ui` places every choice without reading the code: `video.12 option at 264,143 box 189,134 340,153 "HD (2x)"`, `video.13 … "HD (4x)"`, OK as `video.-2 … = ok`. Its PNG shows the same boxes.
- **The smoke scripts** again gave 0, 1, 2 and 3. `smoke_timeout`'s why is `open: no dialog`; it wrote `failure.ui` (132 lines) and `failure.state` (584 lines).
- **C suites:** 465 natively (`kp_test -j 8`), under LLVM's ASan, UBSan and LSan, and in wasm under node; `kp_cxx_check` passes.

### Differences from the plan

- **Conditions not added:**
  - `popup` is `dialog` in the glue.
  - `screen` takes the screen's BIN stem, not a state of ours. `titledlg` (2026-11-12) is the start's title screen (`kp_glue_title`): `wait screen titledlg` holds through its 1.5 s (from its first frame), and the keys, text and clicks sent then are dropped as the title drops them.
  - `art` is the values `art.*`.
  - `episode`, `room-entered` and `score` come with SC5's HD script, when its port shows what it waits for.
- **Values that wait for SC5's picks:** `selection.first`, `selection.has:NAME`, `moved:NAME`.
- **Values added beyond the plan's list:** `camera.tile.*`, `placing`, `pad.count`, `portraits`, `video.*`, `text.vector`, `hd.ui`, `art.lit.pages`, `art.fx`, `mouse.*`, `music.track`, `sounds.streamed`.

## SC4 as built

- **The fixed clock:** `kp_app_script_begin` sets `KpApp.clock_fixed` for `clock fixed`; `kp_app_clock_frame`, at the start of each frame, counts `clock_frames` unless a movie plays, and `kp_app_now` returns 1 s (the interface's 0 is "none") plus `clock_frames / 60` (`KP_FIXED_HZ`), exact at every frame. Nothing else changed: every user of the run's clock already took `kp_app_now(a)` (SC2), the session's step budget only measures the wall clock, and nothing in the game reads the time of day or an unseeded generator.
- **Movies:** the fixed clock stands while a movie plays (its frames follow its sound); the run's own time is the fixed clock plus the wall clock's time in movies, so a wait's limit goes on and a movie that never ends still ends it. Not tried: no script plays a movie yet (CI builds no cinematics by default).
- **`tests/scripts/determinism.kps`** (with `determinism_select.kps` and `determinism_watch.kps`, its includes): terran01 on the index path, the five marines boxed, sent east, then at the north-east, round the cliffs into a fight by the east edge; `state` at five points (start, selected, march, path, fight) and four frames. **`determinism_other.kps`**, its negative control, sends them south first.
- **`tools/ci.sh`** runs `determinism` twice and wants `diff -r` empty and the last state and frame written, then `determinism_other` with the same `start.state` and another `fight.state` and `fight.png`.
- **A product bug the script found:** a right click in the tick of its selection gave no order. `select_locally` changes the game's local selection at once, but `RightClickInGame` 0x47f300 reads the console's snapshot of it (`hud.selection`, `hud.status_unit`), which `kp_ui_hud_frame` rebuilt only after a game step, while the original's `UpdateGameUi` 0x445020 (`UpdateStatusUi` 0x4a7ee0) runs on each pass of `GameLoop` 0x4451f0, a step or none. At the fastest speed a player's box or group key followed by a right click within about 42 ms lost the order. `kp_hud_frame` now runs `kp_ui_hud_frame` once on a frame with no step (its refreshes are flag-driven, so more passes change nothing else); `test_hud.a_right_click_in_the_selections_tick_orders` fails without the fix.

- **The review's fixes** (`/code-review`, 2026-11-08): the clock is a frame count (no float accumulated per frame) and `kp_app_clock_frame` itself skips a movie's frames; the CI block runs after `smoke_hang` (it had taken over the label of `smoke_syntax` and `smoke_hang`) and checks the last state and frame instead of a count of files; the format says which waits need `clock real`; `test_hud`'s five copies of the view's arithmetic are `view_on`.

### Acceptance

On `build/app-script` (`--headless --window 1280x720`):

- `determinism.kps` twice: exit code 0, `27 steps, 0 failures`, all nine files equal byte for byte (`cmp`), each run 3.4–3.6 s for 914 ticks (about 11 times the real time). `determinism_other.kps`: the same `start.png`, `start.state` and `selected.state`, the six later files different. `tools/ci.sh`'s block passed on its own over this build.
- `smoke_pass` and `video_menu` with `clock fixed` in place of `clock real` (a copy each): 0 failures.
- C suites natively (`kp_test -j 8`), under LLVM's ASan and UBSan and in wasm under node; `kp_cxx_check` passes.

### Differences from the plan

- **The smoke script in both clocks:** the plan's risks name it; CI runs `smoke_pass` on the real clock and `determinism` on the fixed one, which covers the game, the input and the observations under it. The fixed-clock copies of `smoke_pass` and `video_menu` were run by hand once.
- **The order's target** is a point of the world, not a pick: picks come with SC5.

## SC5 as built

- **The comparisons as `kp_dev_*`** (`kp_app_dev.c`, declared in `kp_app.h`): `kp_dev_check_terrain`, `kp_dev_check_palette`, `kp_dev_fog_plain` with `kp_dev_check_fog`, and `kp_dev_check_hd`. The old flags call them too, so both paths compare the same way, and their log lines keep their text (`check MISSION LABEL: view …`, `fog check …`, `palette check …`, `hd check LABEL: …`).
- **The host** (`kp_app_script.c`) adds:
  - the inputs `finger`, `tap` and `touch`, through `kp_input_finger`;
  - `choose` with `check video-choice`, the old `vc_verify`: a choice that is not offered must change nothing;
  - `select`, which centres the camera on the full logical screen as the old code did, then boxes the unit;
  - `world`, `fog` and `audit hd`;
  - `check terrain|fog-plain|fog|palette|hd|save-file`. A check that compared nothing fails.
  - The run also stops with "the game stopped: …" when `session->failed` is set.
- **The flow** (`kp_script_flow.c`):
  - values: `selection.has:PICK`, `selection.first:PICK`, `moved:PICK` (world pixels since the pick; `KpScriptRun` keeps each pick's position) and `group.held:N`;
  - conditions: `episode`, `briefing` and `score`;
  - points: `unit`, `beside`, `pad N` and `pad toggle`;
  - actions: `pick` (the old `tc_own_unit`), `save`, `load`, `load-file`, `menu`, `options-dialog`, `activate`, `end-placement` and `cancel-targeting`;
  - the key `race-build`. The race table and the unit roles moved here from `kp_app_dev.c`'s `HC_*`; the old copies go with SC6.
- **Scripts:**
  - `video.kps`, `touch.kps`, `save.kps` with `save_verify.kps`;
  - `music.kps` (2026-11-12, GitHub issue #3): the music's tracks through the glue: the title's after a ready room's Cancel and a Quit Mission, the game's after the room's Start and a save loaded from the glue;
  - `play_1.kps` to `play_5.kps` with `play_tail.kps`, over the old flag's missions: `scenarios/marine_group`, `bench_200`, `bench_combat`, `terran/terran01` and `scenarios/creep_recede`;
  - `hd_glue.kps` and `hd_game.kps`, run over terran02, zerg02 and protoss02.
- **`tools/ci.sh`:**
  - a step after `determinism` runs video, touch, save and the five plays;
  - the HD loop runs `hd_glue` (or `hd_game --mission …`) after each old `--hd-check` and wants every `hd check` line clean.
  - Both steps want exit code 0 and `0 failures`. The old flags still run beside them until SC6.
- **Two check fixes the port found:**
  - `kp_dev_fog_plain` leaves out the cursor's rectangle of the plain frame. The cursor over a unit animates and changes its size between the two frames, so play_2 had 14 mismatches with no pixel darkened. The old check missed this only because its cursor stood over the ground.
  - play_4's old box lay past terran01's 64×64 map and selected nothing (`expect selected` could not hold). The script boxes five units at world (160,80)–(222,172) instead.
  - play_2's camera stood between the box and the target, 710 world pixels apart, so its clicks needed a view at least 710 wide. The Fold7's inner screen gives 656 (2026-11-11), and its first press was `-27,385 is off the screen`. The old flag sent such clicks off the view without a word. The script now centres the camera on the box, then on the target, so each input fits the narrowest logical screen, 640 wide. Every play script and `touch` passes at 640×480 and 656×900.
- **The review's fixes (2026-11-09):**
  - a pick keeps the unit's reference (`kp_unit_ref`), so a dead unit's pick, or one whose record another unit took, is `the picked unit is gone`, not another unit;
  - `check fog` refuses a plain view read at another screen size, where it would have read past it;
  - `save`, `load` and `load-file` refuse a name longer than the profile keeps (108 bytes), which they cut before;
  - `check save-file` deletes through the save dialogs' delete (`kp_ui_delete_save`), which writes the profile;
  - `select group`'s count uses the box's radius;
  - `hd_glue`'s episode text waits `room and still`, as the old flag waited.

### Comparison with the old flags

All runs on `build/app-script` with `--headless --window 1280x720`.

| Script | Result | Against the old flag |
|---|---|---|
| `video` | 0 failures | All 14 steps' values equal (choices, gamma 140, Cancel restoring, OK's one commit) |
| `touch` | 0 failures | Equal: the tap selects, the order moves, the box, group 1 held, the pan of 120, the layout's keys |
| `save` | 0 failures | Same view, minimap tile 37,52, tick 157. The portrait's time left is 4820 vs 4913 ms (the fixed clock) |
| `play_1`–`play_5` | 0 failures | Units, terrain, fog and palette all at 0 mismatches. The camera's y differs by about 40 px (centred on the screen vs the view). play_3 started 123 sounds vs 120. play_4 selects 5 vs 0 (the old box was off the map) |
| `hd_game` | All clean on terran02, zerg02 (group of 3) and protoss02 (group of 5) | Pixel counts equal to the old check, except `start`: 102908 vs 103036 |
| `hd_glue` | Clean | `episode` has 0 HD pixels vs 89641: the sandbox's new player has no progress, so no mission-history popup opens |

`tools/ci.sh`'s two SC5 blocks were run standalone over `build/app-script`, and all scripts gave 0 failures. The full `KOPRULU_APP=1 tools/ci.sh` was not run. The C suites pass natively, under LLVM's ASan and UBSan and in wasm (467 tests each), and `kp_cxx_check` passes.

### Break tests

Each break was a temporary patch, reverted afterwards (`git diff` of the product files clean after each).

| Break | Caught by |
|---|---|
| Cancel keeps the new gamma | `video`'s expect against `@start` |
| OK gives no video commit | `video`'s `video.commits` expect |
| The two-finger pan is turned off | `touch`'s pan expect |
| `text_vk = 0` (a layout's letter is no hotkey) | `touch`'s targeting expect |
| The load keeps the old view | `save`'s camera expect |
| `restore_releases` turned off (the trigger's wait stays) | `save`'s trigger wait times out |
| `.mcrc` corrupted in a copy of the data | `play_*`: 76 terrain mismatches |
| The fog table shifted (`dark + 1`) | `play_*`: 117497 fog mismatches |
| The palette's upload shifted | `play_*`: 409440 palette mismatches |
| `kp_ui_image_put` skipped | `hd_game`/`hd_glue`: all 19 hd checks fail |
| No HD blit while a menu is open | `hd_game`: the 13 menu checks fail |

### Differences from the plan

- **`select fighter 48 optional`** is `select group 48`: the player's box-selectable unit with the most such units around, whatever types the mission gives (zerg02 has no Zerglings, and its group is three units). With it no script used `optional` (no such unit is no failure), and it was removed after SC6 (2026-11-09): a script that walks missions lacking a unit can bring it back with a run that reaches it.
- **The glue in the sandbox:** the new player has no profile, so the login screen opens its new-name popup by itself. `hd_glue` types a name there, which also covers typing a name.
- **The old flags' summary lines** (`PLAY_OK`, `CREEP_OK`, …) are now expects and `check` lines. CI reads the script's summary and the check lines.
- **Not done in SC5:** the phones' shipping and log reading (done in SC6, below); no script has run on the Fold7.

## SC6 as built

- **Removed:** the flags `--video-check`, `--touch-check`, `--save-check`, `--play-check N` with `KP_APP_PLAY_CHECKS` and their `KpAppFlags` fields; their state machines and the `Dev` fields in `kp_app_dev.c` (1818 lines to 617: the autotest, the frame benchmark, the screenshot, `--cine-check`'s call and the `kp_dev_*` comparisons stay, the fog's plain view now `fog_plain`); their branches in `kp_flow_dev_checks`, `kp_flow_init`, `final_flags` (the index path: `--autotest` or a script's `video originals`), `SDL_AppEvent` (the desk's input is ignored under `--autotest` and `--script`) and `refused_flag`. The duplicated race table and unit roles (`HC_*`, `tc_own_unit`) went with them: the script flow's are the only ones.
- **`test_flow.c`:** the tests that set `hd_check`, `save_check` or `video_check` to get the checks' mode set `originals` (a script's `options originals`); `--play-check`'s range test went with the flag.
- **Shipping:** `tools/product.sh` copies `tests/scripts/*.kps` to the debug bundle's `generated/scripts/` (a phone's assets, the web's preloaded `/data`); `kp_app_script.c` reads a script from the file system, else the data (`kp_app_read`).
- **`tools/native/android_app.sh launch`** with `--script` reads the log until the script's summary, its refusal or its watchdog's line (a play script's `palette check` comes before its end); without, until the autotest's, the benchmark's or the cine check's line.
- **`tools/ci.sh`:** the `KOPRULU_APP` step runs the autotest, the smoke scripts, determinism, `video`, `touch`, `save`, `play_1`–`play_5` (each wanting its terrain, fog and palette lines clean), each language's menu and, with the HD art, `hd_game` on terran01 in that language (`SCRIPT_RUN=locale_LOC`), then `hd_glue` and `hd_game` on each race (`SCRIPT_RUN=hd_game_MISSION`, so the runs keep their files apart). `app_script` takes `SCRIPT_RUN` for a second run of one script. Without the HD art a language gets its menu and terran01's first screen (`--mission terran/terran01 --play --screenshot`: the removed `--hd-check` went that far without the HD art, then stopped at its first HD check). The `KOPRULU_ANDROID` step runs `play_1`–`play_5` and `touch` from the APK's assets into the app's external files (`phone_script`; their directory made by `adb shell mkdir -p` first, as the touch check's was).
- **The web's** `shell.html` names a script as its example of `?args=`.
- **Review fixes (2026-11-09):**
  - `app_script` itself wants the summary with 0 failures from a run that should pass (each caller grepped it; the language runs and `determinism_other` did not).
  - CI's file tests are one a line: `[ -s A ] && [ -s B ]` under `set -e` failed only on the last.
  - The host's mid-run lines carry the run's name (`script NAME: cannot write …`): `android_app.sh launch` takes `script: ` for a refusal and stopped reading there.
  - An unknown flag is logged (`koprulu: --play-check: no such flag, or no value after it: ignored`): the removed flags started a plain game with nothing said.
  - `kp_dev_fog_plain` keeps neither buffer when one fails, so a later `check fog` says there is no plain view instead of comparing without the covered mask.
- **Verified (2026-11-09):** the `KOPRULU_APP` step's commands over `build/app-script` (autotest 64 missions 0 mismatches; every script 0 failures, the four languages' terran01 walks among them; the player's profile unchanged); the C suites natively, under ASan and UBSan and in wasm. The web's debug build in the browser pane: `smoke_pass` (19 steps) and `play_1` (32 steps, terrain, fog and palette 0 mismatches) at 0 failures, read from the preloaded `generated/scripts/`. The iOS Simulator (iPhone 18 Pro, iOS 27): `play_1` and `touch` at 0 failures from the app's data, their files in the app's Documents; `touch` on the HD2 art took about 6 minutes there. The full `KOPRULU_FULL=1 KOPRULU_APP=1 tools/ci.sh` with `KOPRULU_ORIGINAL` and `KOPRULU_SCR` passed in 29 minutes (the autotest's 128 composed views 0 differ, every script 0 failures, the four languages' walks among them).

## SC7 as built

- **`kp_script.{h,c}`:**
  - `kp_script_live_begin` gives the live lines a file of their own, `live`.
  - `kp_script_live_line` parses one line onto the steps. A refused line's steps, marks, picks, includes and the mark values its expressions asked for are undone.
  - `KpScript.runnable` keeps a line ending in `&` from running before its input's line.
  - A live run waits at its last step instead of ending (`kp_script_idle`).
  - An abort there counts the failure, writes the failure's files and skips the steps received.
  - Each live line's last step answers it (`done`). A step knows the live line that brought it (`KpScriptStep.live_line`), so an `include` line is answered after its file's steps, and an include of no steps at once.
  - A mark records the values it took (`mark_taken`): a later live line asking it for another fails instead of reading a 0.
- **The host (`sdl/kp_app_script.c`):**
  - `--script-live FIFO` (with `--script-out`, `--script` optional) on macOS and Linux; other platforms refuse it (a `TODO:` names Windows' named pipes and the phones and the web).
  - The FIFO is made when missing and opened `O_RDWR`, so writers may come and go and the open waits for none.
  - A reader thread polls it (100 ms, its stop's latency) and queues lines (256, allocated by a live run only) under a mutex; each frame parses the queued lines before the run's steps. The lines of one read are queued under one lock, so a frame takes a writer's lines together (one `printf` of a few lines, the CI's batches) and a failure skips the lines sent with it whatever the threads' timing.
  - A failed `poll` or `read` of the FIFO ends the run with a failure (`the live lines cannot be read`) instead of leaving it waiting for lines that cannot come.
  - `kp_flow_scripted` stands for "a script drives the run" where `--script` was tested alone: vsync, the throttle, the profile's writes.
- **The fixed clock stands while the run waits for a line** (`KpApp.clock_hold`), so the game moves only in a line's waits and an agent's thinking time costs no ticks. This was not in the plan. Without it, the uncapped frames would run the game about 11 times faster than real time between two lines. An idle frame sleeps 10 ms.
- **`tools/ci.sh`:** `tests/scripts/live.kps` with 10 lines sent through the FIFO, each batch after the answer of the one before (`live_send`, `live_answer`). It checks:
  - the game held across half a second;
  - the misspelt key refused;
  - the failed `expect`, and the lines after it answered;
  - `9 steps, 1 failures` and exit code 1.
  
  It takes about 2 s. **Break test:** with the clock running while idle, the held tick was 14 instead of 0 and the step failed.
- **Tests:** `test_script.live_lines_run_as_they_come` (a wait's lines in the frame it ends in, `until`'s answer after its two steps, a `&` line waiting for its input's line, `include` answered after its steps and an empty one at once, `quit`) and `live_failures_go_on` (refused lines, an expect's failure skipping the step received with it, a failed check answered by its FAILED line alone, a mark asked for a value it did not keep, a refused line's pick and mark value undone).
- **Review (2026-11-09, `/code-review`):** fixed an `include` line never answered, the reader's lines of one write queued one lock at a time (the CI's skip check could lose its race), a read error leaving the run idle for ever, an overlong line with a `\r` at its 512th byte passing truncated, the line numbers after an out-of-memory refusal, the queue allocated by every run, `live_answer` waiting 60 s after the app had ended. Verified after the fixes: the C suites (469) natively and under ASan and UBSan, `test_script` in wasm, the CI block on macOS (2.2 s) and on Linux under Xvfb (1.3 s), `smoke_pass` and `smoke_timeout` as before. Left: a failure that skips a pending `&` line leaves its next line bound to be an input (the parse does not see the run).
- **Verified (2026-11-09):** the C suites (469) natively, under LLVM's ASan and UBSan, and in wasm. On the product (`build/app-script`): the CI block in its own directories; `smoke_pass`, `smoke_timeout`, `determinism`, `video` and `save` as before; the refusals (no `--script-out`, a plain file for the FIFO) with exit code 2. Linux: the product built in `koprulu-linux-build` (`tools/native/linux_app.sh`'s image, into `build/app-linux-script`; no warning under `-std=c99`, `_POSIX_C_SOURCE` for the FIFO's calls) passed the same CI block under Xvfb.

### Differences from the plan

- **Failures:** the plan had none for a live run. A failure ends a file's run, but a live run goes on, since its next line is the agent's choice.
- **Not done:** Windows (no POSIX FIFO), the phones and the web (`TODO:` in `kp_app_script.c`).

## Open

- The Fold7 (2026-11-11, inner screen, debug APK, the `KOPRULU_ANDROID` step's commands): `play_1`–`play_5` and `touch` at 0 failures, from the APK's assets into the app's external files, each with terrain, fog and palette at 0 mismatches. The first run found play_2's camera too far from its clicks for the 656-wide view (fixed above). Windows (`exit(code)`) is not run yet; Linux ran `determinism.kps` in Docker (phase D, D8b); a window closed during a run (exit code 1) is not tried.
- From `--headless` (2026-11-07): not tried on a sleeping display, a locked screen or over SSH; Linux and Windows not run with the flag.
