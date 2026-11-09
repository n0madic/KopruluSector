# ADR 0017: Scripted runs of the product

Status: accepted (2026-11-08). The user approved the plan with the development checks ported to scripts (2026-11-07), and chose a line format of our own with no third-party library (2026-11-08). Stages and progress: [docs/phase-script.md](../phase-script.md); the format: [docs/formats/script.md](../formats/script.md).

## Context

The product is driven by C state machines in `native/kp_app/sdl/kp_app_dev.c`: `--video-check`, `--touch-check`, `--save-check`, `--play-check` and `--hd-check`, about 1170 of its 1710 lines. A new scenario needs C and a rebuild, which is too dear for a one-off debugging run or for an agent that wants to look at a screen, act and look again. `--headless` (2026-11-07) already runs the product with a hidden window and no sound.

## Decision

1. **`koprulu --script FILE --script-out DIR`** runs a script: inputs, waits on conditions, observations (screenshots, the game's state, the interface's tree) and checks, with a window or `--headless`.
2. **A line format of our own, no embedded language.** One command a line, no variables, no branches, no loops except `until`'s one repeated input. A line is split into words and its command is looked up in a table: the parse with its errors is about 1000 lines of C, the run about 300 (SC1). A branch a check needs goes into a `check` of C, not into the format.
3. **The parse and the run have no SDL** (`native/kp_app/kp_script.c`, in `kp_app_core`, tested by `kp_test` natively, under the sanitizers and in wasm). The host resolves the names (keys, values, conditions, controls, checks), runs the inputs and the observations and gives the clock. The product's host is `native/kp_app/sdl/kp_app_script.c` over `kp_script_flow.c`.
4. **Names are checked before the game starts:** a typo in a key, a value or a control is a parse error with its file and line (exit code 2), not a failure 40 steps in.
5. **A frame's steps run in the main thread after the frame is drawn.** Observations run at once; an input ends the frame's steps (a line ending in `&` shares the frame with the next input); a wait ends them until it holds.
6. **The development checks are ported to scripts and their flags removed** in the order video, touch, save, play, HD. Pixel comparisons and the HD coverage stay in C as named checks. Each port runs beside the old flag in CI first, and a patch that breaks what the check guards must make the script fail. `--autotest`, `--cine-check` and `--bench-frames` stay flags: they sweep all missions, cinematics or frames.
7. **A run never touches the player's profile:** it reads and writes a profile of its own under `DIR/pref/`.
8. **Exit codes on desktops:** 0 passed, 1 failed, 2 refused (the script or the flags), 3 the watchdog (no frame within its time). Phones and the web report through the log's last line, `script NAME: N steps, F failures`.

### Libraries considered

The format needs one thing from an embedded language: a script suspended at a wait and resumed in a later frame, since the product runs from SDL's callbacks and the web build can block neither its thread (no Asyncify) nor start another (no threads).

| Library | Size | Suspends at a wait | Why not |
|---|---|---|---|
| Lua 5.5 | about 32,000 lines of C, a 484 KB library | Yes (coroutines, `lua_yieldk`) | Too heavy for a development tool; ADR 0012 removed Lua from the project |
| partcl, picol | about 600 and 669 lines | Not claimed; picol evaluates recursively | A file fed line by line would give the words' split only, with no wait inside `if`, `while` or `proc`, and names known only when a line runs |
| Fe, Berry | under 800 lines; a core under 40 KiB | Not claimed | A Lisp, or a full language the scripts do not need |
| Jim Tcl | | No ("Threads and coroutines are not supported") | Larger than partcl for nothing more |
| Wren | close to Lua by a third party's estimate | Yes (fibers) | Its last release is 0.4.0 of April 2021 |
| Janet, Squirrel, Duktape, QuickJS | as Lua or larger | | No lighter than Lua |

The format's commands have Tcl's shape (`key F10`, `wait dialog GameMenu`), so a move to partcl, should real branches be needed, would leave the scripts nearly as they are.

## Consequences

- New scenarios are text: no C, no rebuild. A failure leaves `failure.png`, `failure.ui` and `failure.state` beside the run's files.
- The development checks' state machines go (about 1170 lines); the parse, the names' tables and the host come instead, so the lines saved come later than the port. Done in SC6 (2026-11-09): `kp_app_dev.c` went from 1818 to 617 lines (the autotest, the benchmark, the screenshot and the `kp_dev_*` comparisons stay), the five flags with it.
- The product's clock goes through `kp_app_now(a)`: a fixed clock of 1/60 s a frame makes a run repeatable (stage SC4), the frame limiter keeps the real time.
- The checks' summary lines (`play check`, `video check`, `save check`, `touch check`, `hd check: N screens`) become the script's last line, and CI reads it and the exit code instead. The pixel lines CI reads (`check NAME after the play`, `fog check`, `palette check`) keep their text; `creep check` becomes an `expect` on the creep's values.
