# ADR 0010: The interface's rules in kp_core (kp_ui), with the clock passed in

Status: accepted (phase 9, 2026-10-24). The user chose to keep the interface's rules in `kp_core` (inline design document, 2026-10-24). Evidence: the full-game emulator's interface input log (`tools/research/sc_emulator/uilog.py`, `input.jsonl`) replayed through `kp_ui`.

## Context

Plan phase 9 builds the original's interface: the command card, selection by click and box, the status panel, the console portrait, unit responses, the channel manager, the briefing. Many of its rules read the game's state, and some draw from the game's generators:

- the unit voices choose their sound with the CRT `rand` (`ChooseYesSound 0x447130`, `ChooseWhatSound 0x447190`), the generator that also chooses the creep tile art of the simulation, so a voice drawn at another moment changes the map's look (phase 6, `ui.jsonl` "crt");
- the console portrait draws from its own generator `0x4a6e00` (P36, the state `0x50d7cc`), which lives as long as the process and is not saved (`KpUi`'s `portrait.rng`; the game's copy and the save's `PRTR` of phase 8 were never drawn from and went on 2026-11-02);
- the selection rules (`0x4bbe70`, `0x4bc2d0`, `0x4bcd00`) and the button conditions (`0x49f410`–`0x4a0490`) read the units and the requirement evaluator of `kp_core` (D16), and turn into the game's commands.

Plan D19 keeps native code to the simulation and to what profiling shows. The acceptance of phase 9 (three levels, [phase 9](../phase9.md)) compares the interface's behaviour with the original's frame by frame: the same commands, generator draws, sounds and portraits for the same input.

## Decision

1. **Where it lives.** The interface's rules live in `native/kp_core` as a layer `kp_ui` (`kp_ui.h`, `kp_ui_*.c`), next to the simulation whose state and generators they use; `kp_core.ui` exposes them to Lua (`kp_ui_lua.c`). They do not change the game except through commands, as the original's interface does, through the draws the original makes (the CRT, the portrait generator), and through the one unit field the original's interface writes: the minimap blink counter and its colour (`+0x8a`, `+0x8b`), which a talking portrait with a ping sets (`RequestUnitTalking 0x4a7200`) and the minimap's drawing counts down (`DrawLocalMinimapUnits 0x46e490`); nothing of the simulation reads them, and the state checks leave them out (phase 4). This is a deliberate exception to D19 for the interface's rules.
2. **What stays in Lua and Defold.** Layout, drawing, the GUI scenes, sound playback and the platform's input stay in Lua and Defold; they give `kp_ui` input in the coordinates of the original's dialogs and draw what `kp_ui` decides.
3. **The clock.** `kp_ui` never reads a wall clock. Every rule that reads `GetTickCount` in the original (the UI timer queue of `timer.cpp`, the pump's 100 ms idle event, the dispatcher's press slots, the cursor's animation) takes the tick as an argument. The product passes the frame's milliseconds; the oracle passes the emulator's virtual ticks from its input log.
4. **Fixed capacity.** As the simulation (ADR 0008), `kp_ui` keeps fixed-capacity arrays (`KP_UI_TIMERS` timer nodes) and allocates nothing while it runs; a full pool fails with a message instead of growing.
5. **Saves.** What of `kp_ui` a game's continuation needs is saved with the game; the rest is rebuilt when a game is loaded, as the original rebuilds its interface.

## Consequences

- The interface's rules are tested as C (the Lua suites through `kp_core.ui`, the emulator's input log replayed) and run at native speed on every platform, built by the same `tools/native/build.sh` and Defold extension as the simulation.
- The first piece is the UI timer queue (`kp_ui_timers.c`): its order of nodes, the fired queue and the search keys `SetUiTimer` only stores when its active queue is not empty, replayed against the original's timer passes.
- The interface follows the game's changes through counters the simulation keeps for it (`interface_refresh` where the original calls `RefreshStatusUi 0x4a7ed0`, in the same branches; `local_selection_serial`; `minimap_terrain`), which change nothing of the simulation.
- A rule of the interface that a recorded game reaches before Koprulu has it fails with the original's address, as the simulation's ports do (D18).
