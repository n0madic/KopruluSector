# Interface checklist (phase 9F)

The third level of the phase 9 acceptance ([phase 9](phase9.md)): where the panels go, their scale and the fillers are
free, so they are checked by the layout engine's geometric tests and by this checklist, not against the original.
Panel content and behaviour stay the original's and are checked by the oracles (`tools/ui_check.py`,
`tools/input_check.py`, `tools/room_check.py`, `tools/menus_check.py`).

## How it was run

Phase 9 ran it on the Defold product; the notes below about the browser pane and Defold's input describe that build.
Phase S ([phaseS.md](phaseS.md)) moved the product to SDL3 under the same oracles, and the geometric tests are in
`tests/c/test_present.c` (`layouts_fit_every_screen`, `layouts_narrow_fits_portrait_screens`,
`layouts_wide_pins_the_groups`); the `test_kp_ui.*` tests named below are in `tests/c/test_kp_ui.c` and
`tests/c/test_hud.c`.

- The product: `tools/product.sh web debug` (main/bootstrap.collection with main/product.settings: the converted data,
  `viewer.glue = 1`, `viewer.product = 1`, the HTML5 canvas stretched to the window), served from
  `dist/product/web/KopruluSector` and opened in the browser pane.
- The browser pane's instant clicks never reach the engine (Defold polls the mouse buttons once per frame): every click
  below is a slow press (`left_click_drag` over one pixel, or mouse events dispatched on the canvas with a held
  button). A hidden pane runs the page at one or two frames a second, so keys are dispatched on `window` from script
  and held for over a second; the text box's characters go in as `keypress` events.
- The geometry of every layout on 20 windows (4:3, 16:10, 16:9, 19.5:9, 21:9, 32:9, 9:16, 3:4; 640 × 480 to
  5120 × 1440) in three scale modes: `tests/lua/test_ui_layout.lua` (`layouts_fit_every_screen`,
  `narrow_layout_fits_portrait_screens`, `wide_layout_pins_the_groups`).

## Screens

| Screen | 1024 × 576 (16:9, wide) | 540 × 960 (9:16, narrow) | Checked by |
|---|---|---|---|
| Main menu, game mode popup | ✓ centred, margins widened and dimmed | ✓ centred at the 640 × 480 minimum, margins above and below | browser |
| Login, new ID popup (typed name) | ✓ | ✓ | browser |
| Campaign episodes (Brood War) | ✓ | ✓ | browser |
| Establishing text, ready room (Protoss), Start | ✓ | ✓ | browser; `room_check.py` (57 games) |
| Mission history, the original campaign's screens | — | — | `menus_check.py`, `test_kp_ui.glue_menus_start_a_campaign_mission` |
| In-game console | ✓ the groups pinned to the corners, a filler between (since 2026-10-26: three groups, the status panel centred, a filler in each gap; macOS screenshots at 1920 × 1080; the vector text at scale 3 on macOS and forced on the web, a lost WebGL context restored) | ✓ two rows: the minimap above the status panel, portrait and command card | browser; `ui_check.py` (panel pixels) |
| Selection, command card, orders | ✓ box and click selection, Move on a point right of the original's 640 columns | ✓ box (mouse events) and click selection, the status panel and card | browser; `input_check.py` (144 scenarios, 69 plays) |
| Game menu, options, sound sliders and check boxes | ✓ centred, translucent over the world | — | browser; `ui_check.py` (`game_menus`; the video options are our own dialog, not compared) |
| Save and load dialogs | — | — | `ui_check.py`, `state_check.py` (`game_save`) |
| Text box, cheats | ✓ above the console's filler, the cheats' message | — | browser; `test_ui_layout` |
| Victory and defeat dialogs, score screen | ✓ Boot Camp's victory, its score screen, the next mission's room | — | browser; `room_check.py` (result and score kinds) |

## Items

- [x] No debug text and none of the viewer's keys in the product (`viewer.product`).
- [x] A glue screen keeps the original's 640 × 480 on any window (`kp_fixed_screen`, `layout.minimum`); a game on a
  portrait window narrows to the narrow layout (502 logical pixels at least) and the logical screen changes with it.
- [x] A popup lies over its screen's text on every frame: the campaign's labels no longer show over the mission history
  (`kp_ui_present` repaints the whole screen while a popup is shown; `test_kp_ui.popups_cover_text_plotted_past_a_repaint`;
  [ADR 0011](adr/0011-interface-panels-and-layouts.md#vector-text-2026-10-26)).
- [x] The game view is the logical screen less the console's 80 rows (`ui:view`): the cursor's choice, clicks, the
  units on screen, the scroll's limits, the minimap's rectangle and the sounds' distance follow it; the original's
  640 × 400 in the oracles.
- [x] The modeless dialogs (the in-game menus) centre on the logical screen, by their own width on a narrow one.
- [x] The console art's holes keep a backdrop dialog under them (the game menu once showed only its title row).
- [ ] The text lines' layer keeps the original's 640 columns: a long line is cut on a narrow screen.
- [x] The first view of a mission is the local player's start location, not the map's corner.
- [x] The text box shows above a wide layout's filler (it lay under the filler's opaque column, typed into but unseen).
- [x] The box selection in the narrow layout (540 × 960) with mouse events; the pane's touch emulation itself (a
  touch drag) was not driven: touch controls are phase 10's.
- [x] The macOS bundle (`tools/product.sh macos debug`) builds and runs: its window shows the main menu at the
  window's size with the margins (a screen capture), the log reaches the glue's main menu.
- [x] A whole mission played through the interface by hand: Boot Camp (terran/tutorial) at 1024 × 576 from the main
  menu (Single Player, the original game, the login, the Terran episode, the establishing text, the ready room) — an
  SCV trained, three Supply Depots and a Refinery placed through the command card, "show me the money" and "operation
  cwal" typed into the text box for speed — to the victory dialog, the score screen and Wasteland's ready room. The
  oracles replay whole missions' input (`input_check.py`, 69 campaign plays, 0 differences).
- [x] The interface's own cursor replaces the system cursor (the browser's is hidden once it shows): the arrow, the
  drag, targeting and edge-scroll cursors with their animation, drawn over everything by the canvas pass
  (now `native/kp_present/kp_canvas.c` and `kp_gfx`'s canvas pass). Since phase S every platform hides the system
  cursor while the interface's is drawn (`SDL_HideCursor`).
- [x] The drag box shows while the button is down, and a right click on the ground shows the command marker (the
  world's draw list left out its hidden sprite): `test_kp_ui.canvas_draws_the_drag_box_and_the_command_marker`. The
  box ends when the window loses the focus with the button down, or with no button down at the next frame.
- [ ] A Center View trigger after the camera was scrolled to the map's right or bottom edge in a window whose size is no multiple of 8 ends too (the limits on the 8-pixel grid; covered by `test_kp_ui.camera_pan_arrives_in_a_wide_view`, not yet seen live)
- [x] A Center View trigger by the map's edge pans a wide view to its own limit and ends: the camera is free again
  and the mission's triggers go on (`test_kp_ui.camera_pan_arrives_in_a_wide_view`); the space moves the view to the
  last transmission, centred in the layout's view. Checked in the web build on Boot Camp (700 x 669): the Refinery's
  ghost and footprint showed on the Geyser while placing it, its completion panned the view to the Geyser, the trigger
  went on to its text and the gas leaderboard, and the minimap moved the camera away and back.
- [x] The campaign menu's Load Saved lists the saved games, deletes one after its question and starts the chosen
  save's mission with the save loaded; an in-game load of another mission's save starts that mission
  (`test_kp_ui.glue_load_screen_deletes_and_loads`).
- [x] The text box shows "Select Target" while targeting and "Select Location" while placing; a click on the portrait
  centres the view on its unit; the game's buttons and sliders play the UI click.
- [x] The text lines are drawn with the game view, under the drag box and the console art, as the original's; a short
  narrow screen lifts them above its console art (`test_kp_ui.canvas_draws_the_text_lines_with_the_game_view`).
- [x] The placement's preview: the building's ghost and its footprint's cells stippled green or red follow the mouse
  and go with the placement (`kp_ui_placebox.c`, `test_kp_ui.canvas_draws_the_placement_preview`). A gas building's
  ghost shows on every Geyser and a Terran building's by the neutral add-ons it could take, as the world's images
  (`kp_build_mode.c`, `test_kp_core.placement_ghosts`).
- [x] The main menu's music plays: the glue's first frame starts its track (kp_ui's music command 22), which the
  viewer once dropped because the glue's sound objects were still loading (the commands now wait for the mixer). Checked
  in the web build by the samples the engine queues to the audio device (none before the fix, RMS 0.12 after), not by
  ear.
- [x] A game started from the campaign menus plays its music: the music pack's objects once deleted with the glue's
  first scene stay loaded (`viewer.script` `destroy_scene`). Checked in the web build on Boot Camp started from the
  menus: over a minute without input the samples queued to the audio device stay at RMS 0.02-0.03 in every 5 s window
  (peaks 0.09), not by ear.
- [x] A wide layout's game view takes clicks where the console dialogs' original rectangles lie (the status panel's,
  the minimap's, the command card's): `test_kp_ui.wide_game_view_clicks_reach_the_game` at 1280 x 720.
- [x] A drag that starts on the minimap goes on in its coordinates off the panel, and a click on a text line's pixels
  starts the drag box: `test_kp_ui.wide_gestures_keep_their_panel` at 1280 x 720.
- [x] The games after the first on one interface (the glue's) end through their own result dialogs, and a Quit
  Mission or a Restart does not end the next game: `test_kp_ui.next_game_forgets_the_last_result`. In the web build at
  1280 x 720: Boot Camp quit through the menus, started again from the campaign menu and restarted through the menus;
  each next game ran on (before, it ended on its first frame or restarted without end). A later game's result dialog
  is checked by the test only.
- [x] A held key reaches the window at its press, repeats and release only (`test_kp_ui.held_keys_reach_the_window_at_their_repeats`).
  In the web build, Backspace held 1.5 s in the save dialog's name deleted 6 of its 10 characters (the press and
  Defold's repeats), not all of them.
- [x] A lone producer's click flashes its rally point (`test_kp_ui.click_on_a_producer_flashes_its_rally_point`), a
  right click on the card's buttons ends a build card, a placement and a targeting in the wide layout
  (`test_kp_ui.right_click_on_the_card_ends_the_mode`), Tab, Shift+Tab and the arrows walk a menu's focus
  (`test_kp_ui.dialog_keys_walk_the_focus`), the music's toggle and a game's end stop the music
  (`test_kp_ui.music_stops_with_its_option_and_the_game`), the window's focus loss stops a held key's scroll
  (`test_kp_ui.inactive_window_stops_the_scroll`) and Ophelia's `terran 2` goes to that mission's ready room
  (`test_kp_ui.mission_warp_goes_to_the_ready_room`).
- [x] The web build (2026-10-25, 700 x 669 classic, keys sent as a browser sends them):
  - Tab and Shift+Tab walk the game menu's focus;
  - Enter opens the message line, and `ophelia` shows "Cheat Enabled";
  - `xprotoss 2` quits the first Protoss mission into the second one's ready room;
  - a game saved through the menus, quit and loaded from the campaign menu's Load Saved starts that mission at once, in its saved state.

  `play_hud.text` now leaves control characters out (`test_kp_ui.text_reaches_the_window_as_bytes`): keys sent by script with a keypress for Enter opened and closed the message line, and a real browser has no such keypress. The browser's own actions on the accelerators are prevented: a cancelable keydown of Ctrl+R, F5 or Alt+M comes back `defaultPrevented`, while a plain letter, Cmd+R and F12 do not, and the menus still take the keys. Ctrl+R was checked live in `expzerg/zerg01`, reached through `xzerg 1`, on its one-row board ("7 Hives to Rescue"): the first row stays. Folding the other rows is checked by `test_kp_ui.leaderboard_key_folds_its_rows` only. The browser's reload on Ctrl+R is prevented (above).

