# Script files (`.kps`)

A script drives the product from a text file: `koprulu --script FILE --script-out DIR` ([ADR 0017](../adr/0017-scripted-runs.md), stages in [docs/phase-script.md](../phase-script.md)). The parse and the run are `native/kp_app/kp_script.c`. The host resolves the names a script uses and runs its inputs and observations: the product's host is `native/kp_app/sdl/kp_app_script.c` with the flow's names in `native/kp_app/kp_script_flow.c` (stage SC3, [what it has](#the-products-names-stage-sc3)), the tests' host records (`tests/c/test_script.c`).

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
| `clock fixed` or `clock real` | The run's clock: fixed (the default; 1/60 s a frame, [below](#the-fixed-clock)) or real |
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
| Observation | `shot NAME`, `state NAME`, `ui NAME` | `DIR/NAME.png`, `NAME.state`, `NAME.ui`. A name is `[A-Za-z0-9_-]`, once per command and never `failure`, in any case (macOS's and Windows's file systems ignore it) |
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

## The product's names (stage SC3)

The product's host knows these names now; the rest come with stage SC5 ([docs/phase-script.md](../phase-script.md)). Names are in any case. A value read where what it is read from is missing has none (`no game`, `no console`, `no interface`): a wait keeps waiting, `expect` and `mark` abort.

| Kind | Names |
|---|---|
| Keys | `A`–`Z`, `0`–`9`, `F1`–`F12`, `Escape`, `Return`, `Enter` (the keypad's), `Space`, `Tab`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `LShift`, `RShift`, `LCtrl`, `RCtrl`, `LAlt`, `RAlt`, `Back` (Android's), `Semicolon`, `Comma`, `Period`, `Slash`, `Minus`, `Equals`, `Grave` |
| Conditions | See [the conditions](#the-conditions) |
| Dialogs | A dialog's BIN file name without its directory and `.bin`. The game's menus and ours: `gamemenu`, `options`, `video`, `snd_dlg`, `spd_dlg`, `objctdlg`, `savegame`, `loadgame`, `quit`, `quitrepl`, `restart`, `abrtmenu`, `help`, `helpmenu`, `tips_dlg`, `ok`, `okcancel`, `wait`, `lmission`, `wmission`, `quit2mnu`, `language`. The glue's: `gluMain`, `gluLogin`, `gluCmpgn`, `gluExpCmpgn`, `gluGameMode`, `gluHist`, `gluLoad`, `gluPEdit`, `gluPOk`, `gluPOkCancel`, `gluScore`, `glurdyp`, `glurdyt`, `glurdyz`, `credits`. The console's, for `DIALOG.ID` and `ui` (never a dialog to `nodialog`; as the NAME of `dialog` or `screen` a parse error): `minimap`, `ptextbox`, `ttextbox`, `ztextbox`, `stat_f10`, `statbtnp`, `statbtnt`, `statbtnz`, `statdata`, `statlb`, `statport`, `statres` |
| Controls | See [the controls](#the-controls) |

### The values

| Value | From | Meaning |
|---|---|---|
| `tick` | the game | The game's tick |
| `selected` | the game | The local player's selection's units |
| `units:N` | the game | Player N's (0..11) active units; another N is a parse error |
| `creep.tiles`, `creep.receding` | the game | The tiles with creep; those of them receding |
| `trigger.waiting` | the game | 1 while the local player's triggers wait (a transmission) |
| `camera.x`, `camera.y` | the world | The view's centre in the world's pixels (the view above the console in a game) |
| `camera.tile.x`, `camera.tile.y` | the world | The view's top-left tile |
| `minimap.x`, `minimap.y` | the console | The tile the minimap's view box last drew at |
| `portrait.ms` | the console | The trigger portrait's time left, 0 none |
| `targeting`, `placing` | the console | 1 while an order's target is aimed; while a building is placed |
| `groups.held` | the console | The groups that hold units, a bit each (group 0 the lowest) |
| `pad.open`, `pad.count`, `pad.shown:N` | the console | The touch pad's groups opened; its buttons shown; 1 when its button N (0..11, `KP_UI_PAD_*`) is; another N is a parse error |
| `gamma` | the interface | The gamma (100 the original's) |
| `portraits` | the interface | The portraits' mode: 0 none, 1 still, 2 animated |
| `video.graphics`, `video.fog`, `video.water`, `video.font`, `video.lighting` | the interface | The video options: -1 auto, else the choice's value |
| `video.serial`, `video.commits` | the interface | The video options' changes; the video dialog's OKs kept |
| `text.vector` | the interface | 1 when the interface's text is in the vector font |
| `art.scale` | the host | The world's HD pixels a logical pixel: 2 HD2, 4 HD, 0 the original's art. None without a world |
| `art.lit`, `art.lit.pages`, `art.fx` | the host | The HD sprites drawn lit; the HD packs' lit pages; the original's art through the RGBA path (the smooth fog, the water). None without a world |
| `hd.ui` | the host | 1 with the HD interface's art |
| `cursor.shown` | the host | 1 while the cursor is drawn |
| `mouse.x`, `mouse.y` | the host | The mouse's last logical point; none before the mouse was seen |
| `music.playing`, `music.track` | the host | The music plays; its track |
| `sounds.started`, `sounds.streamed` | the host | The sounds the mixer started; streamed |

### The conditions

| Condition | Holds when | Its why when not |
|---|---|---|
| `game` | The game is in play with its console, its first view taken, no glue over it | The game's error, `the glue`, `the game's first view to come`, `no game in play` |
| `dialog [NAME]` | In a game its modeless dialog is open (NAME: that one); in the glue a popup is over the screen (NAME: that popup, or the screen NAME) | `open: <the open dialogs>` |
| `nodialog` | No modeless dialog in a game; no popup over the glue's screen | `open: …` |
| `paused` | The game's clock is paused | `the game runs`, `no game` |
| `glue` | The glue is up | `the glue: <its dialogs>[ (a ready room)][, its panels moving]`, `a game`, `no glue` |
| `still` | The glue is up and its panels stand still (none sliding, none hidden for a slide to come) | as `glue` |
| `screen [NAME]` | `still`, no popup over the screen, and the screen is NAME when it is named | as `glue` |
| `room` | The glue shows a ready room | as `glue` |
| `movie`, `nomovie` | A cinematic plays; none plays | `no movie plays`, `a movie plays` |

### The controls

A control is named in the table or as `DIALOG.ID`: a dialog's name and the control's ID in its BIN (`video.12`, `gamemenu.-3`), as `ui` lists them. A control named in the table is either any open dialog's (`ok`, `cancel`: the topmost one that has it) or one dialog's.

| Name | Dialog's ID |
|---|---|
| `ok`, `cancel` | -2, -3 (the topmost open dialog's) |
| `gamemenu.save`, `.load`, `.options`, `.help`, `.objectives`, `.end-mission`, `.return` | `gamemenu` 1, 2, 3, 4, 5, 6, -3 |
| `glumain.single` | `gluMain` 3 |
| `glugamemode.original`, `.expansion` | `gluGameMode` 6, 7 |
| `glulogin.ok`, `.cancel`, `.new`, `.delete` | `gluLogin` 4, 5, 6, 7 |
| `glucmpgn.load`, `.player` | `gluCmpgn` 5, 9 |
| `gluexpcmpgn.load`, `.player` | `gluExpCmpgn` 5, 9 |
| `gluload.ok`, `.cancel`, `.delete` | `gluLoad` 4, 5, 7 |

A glue popup's controls are placed where the popup is composited into its screen. A control on no dialog open now fails its step (`no video.12 open (open: …)`).

### What runs

A key's press goes through the product's own input path as SDL's events: the modifiers pressed first, the key, its text, the key's release, the modifiers released. Without `text` or `notext`, a letter, a digit or `Space` with no `ctrl`, `alt` or `gui` gets its character (shifted: the capital). The mouse's points become window coordinates at a logical pixel's centre.

The commands run now: `key`, `text`, `move`, `click`, `rclick`, `press`, `release`, `camera`, `pause`, `resume`, `hang`, `shot`, `state`, `ui`, the waits, `until`, `log`, `mark`, `expect`, `quit`. The points `X Y`, `world`, `control`, `center` and `view` are placed; `unit`, `beside` and `pad` fail at run time. A command without its names yet (`check`, `choose`, `menu`, `options-dialog`, `select`) is a parse error; the others that parse (`pick`, `finger`, `tap`, `touch on`, `world`, `fog`, `save`, `load`, `load-file`, `activate`, `end-placement`, `cancel-targeting`, `audit hd`) fail at run time with `not in this build yet`. A point outside the logical screen fails its step.

## The observations' files

Each is written to `DIR/NAME.ext` (an observation refused writes no file) and logged with its path: `screenshot PATH: WxH`, `state PATH: N lines`, `ui PATH: N lines`.

- **`shot NAME`** writes `NAME.png`, the window's drawable as `--screenshot` writes it.
- **`state NAME`** writes `NAME.state`: the game's state as `kp_dump` writes it with the units' details, the text the oracle's comparisons read ([docs/golden-oracle.md](../golden-oracle.md)). It fails without a game (`no game`).
- **`ui NAME`** writes `NAME.ui`: the open dialogs' tree, topmost first, the console's dialogs among them. Every point is in logical pixels on the screen, as a script's `X Y` takes it. It fails without an interface.

```
# ui: a game, the logical screen 854x480, the view 854x400 centred on 427,1672; 9 dialogs, topmost first; a control is DIALOG.ID to `control`
dialog video box 177,32 676,343 (modeless) "Video"
  video.-2 default-button at 354,317 box 303,304 406,331 "OK" key O = ok
  video.-3 button at 498,317 box 447,304 550,331 "Cancel" key C = cancel
  video.1 slider at 588,95 box 521,90 656,101
  video.10 option at 264,99 box 189,90 340,109 "Auto" checked
```

- **The first line** says where the run is (`the glue`, `a game`, `no game in play`), the logical screen's size, in a game the view's size and centre, and how many dialogs follow.
- **A dialog's line:** `dialog NAME box L,T R,B (ROLE) "TITLE"`. ROLE is one of:
  - `modeless`: the game's menu;
  - `console`: the console's dialogs;
  - `screen`: the glue's screen;
  - `popup`: one over the screen;
  - `open`: any other.

  A dialog not drawn now says `not on the screen` in place of its box.
- **A control's line:** `DIALOG.ID KIND at CX,CY box L,T R,B "TEXT" key K|hotkey \xNN [disabled] [hidden] [checked] = NAMES`.
  - `at` is the centre `control` clicks.
  - KIND is `dialog`, `default-button`, `button`, `option`, `checkbox`, `image`, `slider`, `scrollbar`, `edit`, `label`, `list`, `combo`, `flic-button` (the glue's buttons), else `typeN`.
  - The text drops the font's colour codes; a line break is `\n`, and it is cut at 160 bytes (before a UTF-8 character the cut would split) with `...`.
  - `key K` is the control's hotkey as `key` names it; `hotkey \xNN` a hotkey no key name gives.
  - `= NAMES` gives the table's names for the control: its dialog's own, and `ok` or `cancel` only on the dialog `control ok` clicks (the topmost that has the ID).
- **A list's items** follow its control: `    item K "TEXT" [selected]`.

**On an abort** the host writes `failure.png`, `failure.ui` when there is an interface, and `failure.state` when there is a game, after the step's FAILED line and before the run's last line. A stop from outside (the window closed) writes none. A script's own observation may not be named `failure`.

## The run's log and its end

The lines go to the product's log with `SPIKE ` before them.

- A failed step: `script FILE:LINE: <the line>: <why> FAILED`. For a wait the why is `no change in N ms: <what the condition saw>`.
- `log`: `script FILE:LINE: LABEL: gamma 140, camera.x n/a`.
- The last line: `script NAME: N steps, F failures`. N counts the steps that ran to their end (a failed `check` does, the step a run aborts on does not). `until`'s input counts as a step of its own once it was sent: `until nodialog key Escape` is 2 steps, or 1 when no dialog was open.

| What | The run | Exit code (desktops) |
|---|---|---|
| A parse error; `--script` without `--script-out` or the reverse; a development check's flag beside it (`--autotest`, `--play-check`, `--video-check`, `--hd-check`, `--save-check`, `--touch-check`, `--cine-check`, `--cine-play`, `--bench-frames`, `--screenshot`, `--camera`) | `script: <why>`; no window, no game | 2 |
| The game cannot start (no such mission, no data) | `script: <the start's error>` | 2 |
| A wait past its time, a failed `expect`, a step the host cannot run | Aborted: the steps left skipped; `failure.png`, `failure.ui` and `failure.state` written ([above](#the-observations-files)) | 1 |
| A failed `check` | Counted, the run goes on | 1 at the end |
| The window closed, the program ended before the script did | `script FILE:LINE: <the step's line>: <why> FAILED` for the step it was on (`script NAME: <why> FAILED` past the last), then the last line | 1 |
| No frame within `watchdog` | `script NAME: no frame in N ms: the watchdog ends the program FAILED`, from the watchdog's own thread; no summary line | 3 |
| All steps run, no failure | | 0 |

SDL's callbacks return only success or failure, so on the desktops the program calls `exit` with the code after `SDL_Quit`. On the web and the phones the result is the log's last line; there is no watchdog there.

**The run's start.** The header's line: `script NAME: N steps, files in DIR, profile DIR/pref/, clock fixed` (or `clock real`).

**The fixed clock.** With `clock fixed` the product's clock (`kp_app_now`: the game's steps, the interface's timers, the touch layer, the palette's cycles, the water) starts at 1 s and moves 1/60 s at each frame, whatever the frame took. The same script on the same build and data writes the same files byte for byte (`tests/scripts/determinism.kps`, run twice in `tools/ci.sh`), and runs faster than the real time (terran01's 914 ticks, 38 s of the game at its fastest speed, in 3.4 s on an M-series Mac). The frame limit, the frame times and the watchdog stay on the wall clock. A movie's frames follow its sound, so the fixed clock stands while one plays and the run's time (its waits' limits) goes on with the wall clock then: a movie that never ends still ends a wait. A wait's limit counts the fixed clock's frames, and they run uncapped: a wait for anything the host does in its own time (the HD art's pages, a file a thread reads) can run out its limit in a fraction of that on the wall clock, and the frames it shows would differ from run to run. Such a script takes `clock real`.

**The sandbox.** The profile is read from and written to `DIR/pref/` (created; its files as the product's own profile directory has them), so a run starts as a new player's and leaves the player's profile alone: on the desktops and the phones `SDL_GetPrefPath`, which creates the real directory, is not called. A run writes even with `options originals`, removes nothing, and two runs at once need their own `DIR`. To replay a player's report, put their `profile.kpprof` in `DIR/pref/` first.

**What the script owns.** The player's own input and the window's focus events are ignored while a script runs, the frame rate is not capped (unless `--frame-cap`) and vsync is off. A shot is the window's drawable as `--screenshot` writes it (`SPIKE screenshot PATH: WxH`).
