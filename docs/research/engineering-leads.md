# Leads for later stages: GPU backend, start-up robustness, small checks, custom maps

Collected 2026-11-13 from outside material on other StarCraft ports. Every item is a **lead, not a finding**: it has not been reproduced here, and none of it replaces the atlas or the emulator as evidence. Each item says what to check and where it would matter. The replay and network side is in [replay-network-notes.md](replay-network-notes.md).

## For the native SDL_GPU backend (stage S9)

Drawing is GLES 3.0 / WebGL2 for now and a native SDL_GPU backend is a deferred stage ([phase S](../phaseS.md), [ADR 0012](../adr/0012-sdl3-runtime.md)). If it is started, these are reported to bite on SDL_GPU 3.4:

- A uniform block larger than 4 KiB reads as zeros on SDL's Vulkan backend. Keep each block below it and split rather than grow; a build-time check on block sizes catches it before a frame does.
- A three-component vector after a scalar sits beside it in Direct3D's layout and 16 bytes further on in SPIR-V's, moving every field after it. A build step that compares the offsets of each field between the two layouts and fails the build on any difference prevents it.
- SDL_GPU binds at most 8 storage buffers of each kind. Several parts of a frame description then have to share one buffer at offsets.
- The swapchain image is handed out only on the thread that created the window, so presenting cannot move to a drawing thread.
- Direct3D 12 in SDL 3.4.16 was reported to crash when descriptor tables were written before being sized, and the report's authors carry a local patch. Check the pinned SDL version against this when a D3D12 path is first run.
- Mouse positions reach the game in game pixels, and two presenters that letterbox differently disagree about where the game's frame is; anything drawn around the game's frame must be part of the one function that maps window to game coordinates.

**A frame check to add with a second backend:** read the resolved frame back every frame and compare it with the frame the first backend draws for the same state, so a frame that is self-consistent but wrong is caught. The emulator's frame captures are the reference for the first backend; the second is compared with the first.

## Start-up robustness and crash reports

Neither is described in the project's docs today, and both matter most on phones and on the many Windows GPU drivers:

- **A driver that hangs or fails at start.** Restart by itself after a time limit (20 s was used) with another way of drawing, down to one that needs no graphics driver, and let the player choose one by hand. The start-up log should name the graphics card and driver.
- **A start-up log per run**, and a note at the next start when the previous session ended with no word (a heap corruption or an abrupt kill reaches no handler).
- **A crash report folder per crash** with: what happened and where (module and offset), the build, system, displays and renderer, the session (time running, whether the start finished, the screen or frame, the mission, the last few missions), the call stack, the last 64 log lines of every module with times, the registers and the loaded modules. Write each part to disk as it is made, so a fault inside the reporter keeps the earlier parts; keep the last ten.
- **Symbols per build.** Keep each shipped build's symbols (the program with debug info, the `.dSYM`, the PDB) and a script that takes a report and names each frame's function, file and line.
- **Off Windows** the fault is a signal, handled on a stack of its own (about 1 MB), and the main thread keeps part of its stack back so an overflow can still write its report. Frames are offsets from the program's load address; the report should carry the build id.
- **A deliberate crash** (`av`, `abort`, uncaught exception, invalid parameter, pure virtual call) at the menus or in a mission, to test the reporter in CI.
- **Test instances.** A run that must not take the player's focus: hidden window, or a window that neither takes the focus nor the mouse; each test instance with its own data folder and its own control port, never the player's.

## Small checks against the original

Each is a place where outside material says the original does something easy to get wrong. Check each against the atlas and the emulator before relying on it.

- **`stat_txt.tbl`.** A button's string is its key (0 when the tooltip has no cost line) followed by the text, so reading each string up to its first NUL cuts the strings with a leading 0 byte (274 of them in one rebuild). Check the reader and the repack's rebuilt tables against the original's.
- **A missing `help_txt` table** makes the original treat the install as broken and close (`load_game_data`). A Remastered source that lacks it needs it made.
- **Fixed-size string buffers** (a name of 25 bytes) copied in UTF-8 must leave a character out whole when the byte limit falls inside it. Check the copy of translated names against the original's buffers.
- **Subtitles** drawn with a face that lacks a script's letters (Cyrillic, East Asian) should fall back to another face or to English. The one-font localization plan ([phase-l10n](../phase-l10n.md)) is the place to check it.
- **The AI's first-attack delay** is counted in elapsed-time ticks of 16 game frames and converted with the fastest speed's frame time, so at another speed the delay is the same number of frames, not the same time. Check against the atlas entry of the attack-delay test and the speed dependence in `kp_ai`.
- **Another language's install** holds the game types only in English's, and only a few languages have a `Local.dll`; an install lists other languages' files without holding them. A test that needs another language's files has to fetch them, and the CASC reader can read the online storage; that is a download of the original's data and so the user's decision.

## Checking an optimisation against the plain version

Spatial indexes (a grid of squares for the units-in-rectangle search, row tables for the map's contours, lookups for the local search's entries) were added to a port with a switch that runs the plain walk as well and compares the answer. The pattern fits the project's rule that an optimisation is verified by benchmarks: keep the plain version in the tree behind a check flag, and run the oracle suites with the flag on. Anything that caches the answer to a question about the game must be invalidated where the game changes it, and a snapshot restore must forget it.

## Custom maps with direct memory access (outside the current scope)

Some custom maps use Deaths conditions and Set Deaths actions to read and write the game's memory through the death table's unchecked index (`0x58A364 + 4 * (player + 12 * unit)` in the 1.16.1 address space). A map that does is recognisable by a Deaths condition or action with a player of 12 or 26 and above, or a unit above 232. If custom maps are ever loaded, this test tells such a map from an ordinary one, and the map would be refused or run with an emulated memory map; the first needs only the test. [Remastered's EUD layer](remastered.md) is the other side of the same question.

## Not verified

All items above come from third-party documentation and have not been reproduced here. Version numbers, limits (4 KiB, 8 buffers, 20 s) and counts (274 strings) are as quoted there.
