# Script files (`.kps`)

A script drives the product from a text file: `koprulu --script FILE --script-out DIR` ([ADR 0017](../adr/0017-scripted-runs.md), stages in [docs/phase-script.md](../phase-script.md)). The parse and the run are `native/kp_app/kp_script.c`. The host resolves the names a script uses and runs its inputs and observations: the product's host is `native/kp_app/sdl/kp_app_script.c` with the flow's names in `native/kp_app/kp_script_flow.c` (stage SC2, [what it has](#the-products-names-stage-sc2)), the tests' host records (`tests/c/test_script.c`).

```
koprulu --script tests/scripts/smoke_pass.kps --script-out build/script/smoke_pass --headless --window 1280x720
```

The format is a list of commands, not a language: one command a line, no variables, no branches, no loops except `until`'s one repeated input.

## The file

- UTF-8; a byte order mark is skipped; lines end in LF or CRLF.
- A line has at most 511 bytes and 32 words. Words are split at spaces and tabs.
- A word has at most 63 bytes. A string is quoted (`"…"`), escapes only `\"` and `\\`, and holds at most 127 bytes.
- A word starting with `#` starts a comment, to the line's end.
- A line ending in the word `&` belongs to the same frame as the next line (see below).
- Control characters, a NUL byte, a quote inside a word or a string running into a word are errors.

An error names the file and the line (`video.kps:12: unknown key "F1O"`) and is reported before the game starts.

## The header

Directives come before the first command, in the main file only.

| Directive | Meaning |
|---|---|
| `mission NAME` | The game to start (`zerg/zerg01`), as `--mission` does; the command line's `--mission` wins |
| `options originals` | The development checks' mode: the original's options, no first-start intro |
| `video originals` | The index path's video options too (font, graphics, fog, water). It implies `options originals`, else the profile's video options would come back |
| `clock fixed` or `clock real` | The run's clock: fixed (the default; 1/60 s a frame, stage SC4) or real |
| `timeout S` | A wait's time without `within` (default 60 s) |
| `watchdog S` | The time one frame may take before the host ends the program with exit code 3 (default 60 s) |

Times are seconds with up to 3 decimals.

`include FILE` reads a file beside the script, anywhere a command may stand. Its name is `[A-Za-z0-9_.-]`, no `..`. Includes nest 4 deep and a script has at most 16 files. An included file holds commands only; an error names the included file and its line.

## Frames

The host runs the script after a frame is drawn and before it is shown.

- **Observations** (`shot`, `state`, `ui`, `log`, `mark`, `pick`, `expect`, `check`) run at once, as many as come.
- **An input or an action** runs and ends the frame's steps. The steps after it see the next frame, the one drawn after the input.
- **`&`:** an input line ending in `&` has the next line's input run in the same frame. The next line must be an input (two fingers, a box drawn in one frame).
- **A wait** ends the frame's steps until it holds. The steps after it run in the frame it holds in. `wait frames N` has the steps after it run N frames after the wait started.
- The run ends in the frame its last step runs in; `quit` ends it early.

## Commands

| Kind | Command | Meaning |
|---|---|---|
| Input | `key NAME [mod M] [text "…" \| notext]` | A key's press, its text, its release. `mod` is `shift`, `ctrl`, `alt`, `gui` joined by `+`. Without `text` or `notext` the host gives a letter key its letter |
| | `text "…"` | A text event, as a keyboard layout gives it |
| | `move P`, `click P`, `rclick P` | The mouse moved to P; the left or the right button clicked there |
| | `press P`, `release P` | The left button pressed or released at P (a box: `press`, then `release`) |
| | `finger down\|move\|up ID P` | A touch of finger ID (0..255) |
| | `tap ID P` | A finger down and up at P |
| | `touch on` | The touch layer's mode |
| | `choose NAME` | A video option's choice clicked |
| Action | `pause`, `resume` | The game's clock |
| | `camera X Y`, `camera by DX DY`, `camera max` | The view's centre on the world's X Y; moved by DX DY; at the map's bottom-right |
| | `world on\|off`, `fog on\|off` | The world's images drawn; the fog |
| | `save "NAME"`, `load "NAME"`, `load-file "NAME"` | The game saved, loaded in the game, loaded from its file (the glue's Load Saved) |
| | `menu NAME`, `options-dialog NAME` | A game menu, an options dialog opened |
| | `activate NAME` | The open menu's control activated as its click does |
| | `select NAME RADIUS [optional]` | The camera on the player's unit of the kind, a box of the radius over it; with `optional` no such unit is no failure |
| | `end-placement`, `cancel-targeting` | A building's placement, an order's target ended |
| | `audit hd` | The canvases count the draws outside the image log |
| | `hang S` | The frame takes S seconds (the watchdog's test) |
| Wait | `wait frames N` | N frames (1 or more) |
| | `wait seconds S` | S seconds of the run's clock |
| | `wait COND [within EXPR]` | Until the condition holds; `within` is its time in ms (else `timeout`) |
| | `until COND [within EXPR] [every S] INPUT` | The input at once and again every S seconds (default 0.25), until the condition holds |
| Observation | `shot NAME`, `state NAME`, `ui NAME` | `DIR/NAME.png`, `NAME.state`, `NAME.ui`. A name is `[A-Za-z0-9_-]`, once per command, never `failure` |
| | `log LABEL [VALUE…]` | A line of the label and the values (at most 12) |
| | `mark NAME` | The values the script reads as `@NAME.value` later are kept now |
| | `pick NAME` | The host picks a unit of the player's (at most 16 picks) |
| | `expect COND` | The condition holds now, or the run aborts |
| | `check NAME ["ARG"]` | One of the host's checks (a comparison of pixels, the HD coverage). A failure is counted and the run goes on |
| End | `quit` | The run ends here |

### Points

| Point | Meaning |
|---|---|
| `X Y` | Logical pixels. The logical screen's size depends on the window, so plain numbers fit a fixed `--window` only |
| `world X Y` | The world's pixels, through the camera |
| `control NAME [right]` | The open dialog's control's centre; `right`: its right end (a slider's) |
| `unit PICK [DX DY]` | A picked unit, with an offset |
| `beside PICK D` | The ground D pixels right of a picked unit, or left of it at the view's edge |
| `pad N`, `pad toggle` | The touch pad's group button, its toggle |
| `center [DX DY]` | The view's centre, with an offset |
| `view X% Y%` | A share of the logical screen (100%: its last pixel) |

### Values, expressions, conditions

- **A value** is one of the host's integers by name: `tick`, `camera.x`, `video.graphics`. Some take an argument after a colon: `units:3` (a number), `moved:u` (a pick).
- **An expression** is a number, a value, or a value a mark kept (`@start.gamma`), then optionally `+ N` or `- N` (`+N` glued works too).
- **A condition** is one or more terms joined by `and`, at most 4. A term is either:
  - one of the host's named conditions, some with an argument word (`game`, `dialog gamemenu`, `screen login`);
  - or a comparison `EXPR OP EXPR` with `==`, `!=`, `<`, `<=`, `>`, `>=`.
- A value with none now (no game yet) keeps a wait waiting. In `expect` and `mark` it aborts the run.

Unknown names (a key, a value, a control, a check...) are parse errors. A mark or a pick must come before the line that uses it.

## The product's names (stage SC2)

The product's host knows these names now; the rest come with stages SC3 and SC5 ([docs/phase-script.md](../phase-script.md)). Names are in any case.

| Kind | Names |
|---|---|
| Keys | `A`–`Z`, `0`–`9`, `F1`–`F12`, `Escape`, `Return`, `Enter` (the keypad's), `Space`, `Tab`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `LShift`, `RShift`, `LCtrl`, `RCtrl`, `LAlt`, `RAlt`, `Back` (Android's), `Semicolon`, `Comma`, `Period`, `Slash`, `Minus`, `Equals`, `Grave` |
| Values | `tick` (the game's), `camera.x`, `camera.y` (the view's centre in the world's pixels: the view above the console in a game) |
| Conditions | `game` (the game in play with its console, its first view taken, no glue over it), `dialog [NAME]` (in a game its modeless dialog; in the glue a popup over the screen, or the screen NAME), `nodialog`, `paused` |
| Dialogs | A dialog's BIN file name without its directory and `.bin`: `gamemenu`, `options`, `video`, `snd_dlg`, `spd_dlg`, `objctdlg`, `savegame`, `loadgame`, `quit`, `restart`, `abrtmenu`, `help`, `helpmenu`, `tips_dlg`, `ok`, `okcancel`, `wait`, `lmission`, `wmission`, `quit2mnu`, `gluMain`, `gluLogin`, `gluCmpgn`, `gluExpCmpgn`, `gluGameMode`, `gluHist`, `gluLoad`, `gluPEdit`, `gluPOk`, `gluPOkCancel`, `gluScore`, `glurdyp`, `glurdyt`, `glurdyz`, `credits`, `language` |
| Controls | `ok`, `cancel` (the topmost dialog's that has it); `gamemenu.save`, `.load`, `.options`, `.help`, `.objectives`, `.end-mission`, `.return` |

A key's press goes through the product's own input path as SDL's events: the modifiers pressed first, the key, its text, the key's release, the modifiers released. Without `text` or `notext`, a letter, a digit or `Space` with no `ctrl`, `alt` or `gui` gets its character (shifted: the capital). The mouse's points become window coordinates at a logical pixel's centre.

The commands run now: `key`, `text`, `move`, `click`, `rclick`, `press`, `release`, `camera`, `pause`, `resume`, `hang`, `shot`, the waits, `until`, `log`, `mark`, `expect`, `quit`. The points `X Y`, `world`, `control`, `center` and `view` are placed; `unit`, `beside` and `pad` fail at run time. A command without its names yet (`check`, `choose`, `menu`, `options-dialog`, `select`) is a parse error; the others that parse (`state`, `ui`, `pick`, `finger`, `tap`, `touch on`, `world`, `fog`, `save`, `load`, `load-file`, `activate`, `end-placement`, `cancel-targeting`, `audit hd`) fail at run time with `not in this build yet`. A point outside the logical screen fails its step.

## The run's log and its end

The lines go to the product's log with `SPIKE ` before them.

- A failed step: `script FILE:LINE: <the line>: <why> FAILED`. For a wait the why is `no change in N ms: <what the condition saw>`.
- `log`: `script FILE:LINE: LABEL: gamma 140, camera.x n/a`.
- The last line: `script NAME: N steps, F failures`.

| What | The run | Exit code (desktops) |
|---|---|---|
| A parse error; `--script` without `--script-out` or the reverse; a development check's flag beside it (`--autotest`, `--play-check`, `--video-check`, `--hd-check`, `--save-check`, `--touch-check`, `--cine-check`, `--cine-play`, `--bench-frames`, `--screenshot`, `--camera`) | `script: <why>`; no window, no game | 2 |
| The game cannot start (no such mission, no data) | `script: <the start's error>` | 2 |
| A wait past its time, a failed `expect`, a step the host cannot run | Aborted: the steps left skipped; `failure.png` written (`.ui` and `.state` come with stage SC3) | 1 |
| A failed `check` | Counted, the run goes on | 1 at the end |
| The window closed, the program ended before the script did | `script FILE:LINE: <the step's line>: <why> FAILED` for the step it was on (`script NAME: <why> FAILED` past the last), then the last line | 1 |
| No frame within `watchdog` | `script NAME: no frame in N ms: the watchdog ends the program FAILED`, from the watchdog's own thread; no summary line | 3 |
| All steps run, no failure | | 0 |

SDL's callbacks return only success or failure, so on the desktops the program calls `exit` with the code after `SDL_Quit`. On the web and the phones the result is the log's last line; there is no watchdog there.

**The run's start.** The header's line: `script NAME: N steps, files in DIR, profile DIR/pref/, clock real`. The real clock is the only one until stage SC4: `clock fixed` (the default) is parsed and the line says `clock real (fixed comes with stage SC4)`.

**The sandbox.** The profile is read from and written to `DIR/pref/` (created; its files as the product's own profile directory has them), so a run starts as a new player's and leaves the player's profile alone: on the desktops and the phones `SDL_GetPrefPath`, which creates the real directory, is not called. A run writes even with `options originals`, removes nothing, and two runs at once need their own `DIR`. To replay a player's report, put their `profile.kpprof` in `DIR/pref/` first.

**What the script owns.** The player's own input and the window's focus events are ignored while a script runs, the frame rate is not capped (unless `--frame-cap`) and vsync is off. A shot is the window's drawable as `--screenshot` writes it (`SPIKE screenshot PATH: WxH`).
