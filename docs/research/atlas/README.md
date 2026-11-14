# StarCraft 1.17.0.1 function atlas

The atlas closes the research objective at function granularity. Every function that Ghidra 12.1.4 identifies in the exact local `StarCraft.exe` (SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`) receives a status. A remake implementer can then answer "what does the original do here?" for any reachable code path without guessing.

The atlas complements the earlier contract specifications in `docs/research/native-*.md`. Those documents remain authoritative for their executed probes. Atlas entries link to them and add the remaining behavior derived from static analysis of the same executable.

## Method and evidence level

- Input: a full Ghidra headless analysis and decompilation of the exact executable, plus capstone disassembly. Original binaries, decompiler output and analysis databases stay in a private workspace outside the project; no decompiled code is reproduced here.
- Specifications are original English descriptions of behavior: conditions, constants, integer widths and signedness, iteration order, state writes, random-number consumption, error paths and callees.
- The executable was built with Visual C++ 12 and whole-program optimization. Many internal functions take arguments in registers (`eax`, `ecx`, `edx`, `esi`, `edi`, `ebx`) outside standard conventions. Each signature is confirmed against the disassembly, and the register is named explicitly.
- Static specification is evidence level 3 in the implementation plan (§0.1). Where an earlier executed probe exists, the probe result wins on its recorded inputs. Any contradiction between the two is reported in the entry, not silently resolved.
- Wine on this host cannot run 1.17 reliably (wow64 code-segment switching under Rosetta), and the user declined a VM. The complete game is instead executed in Unicorn by the [full-game emulator](../full-game-emulation.md); its coverage is compared with this atlas. Composition questions are answered from the code paths that implement them, and the emulator confirms them at runtime.

## Module map

The linker emitted object files in alphabetical order, in two runs: the game objects (`bnetgw.cpp` … `victory.cpp`, 0x408860–0x4bec30) and an engine library (`creep.cpp` … `vidinimo_pc.cpp`, 0x4c0690–0x4cffd7), followed by the Visual C++ runtime (0x4d0000 and above). Source file names come from assertion strings. A function between two named anchors belongs to an object whose name sorts between them; the atlas records this as a bracket such as `cunitinit.cpp..cunitprotoss.cpp`.

## Entry format

Each package document lists every function in its address range, in address order:

```
### 0x00401234 `ProposedName` — one-line role
- Module: bracket or exact module · Relevance: simulation | ai | script | ui | audio | render | data | platform | network | crt
- Status: specified | covered | excluded | library
- Signature: calling convention and register/stack arguments confirmed from assembly; return value
- Behavior: precise description
- Globals/fields: addresses and struct offsets read or written
- Callers/callees: notable relationships
- Prior evidence: links to native-*.md specifications, if any
```

Status values:

- `specified`: the behavior is fully described in the entry.
- `covered`: an earlier specification fully describes it. The entry links that specification and records anything it lacks.
- `excluded`: the function cannot affect the offline single-player campaigns or their presentation, and a network design without Battle.net, modems and Storm's providers does not need it. Examples are Battle.net's screens, ladder and result reports, the modem provider and Storm's internals. The entry states the reason. The network second pass (2026-11-14, [replay and network notes](../replay-network-notes.md)) specified the formerly excluded lobby, chat, map transfer, sync and start entries; their entries end with a "Second pass" line.
- `library`: standard C runtime or Windows-glue behavior, identified by name.

No entry may remain undetermined. An entry can lack an executed probe; it cannot lack a behavior statement.

## Naming

Globals are identified by address, such as `g_6283f8`, with a descriptive name where one is established. Structure offsets follow the 1.16.1 layouts in BWAPI (`CUnit`, `CSprite`, `CImage`, `CBullet`, `AIController`, pathing). The layouts are confirmed against 1.17 code at the point of use. The machine-readable index for each package is `docs/research/evidence/atlas/<package>.json`.

## Glossary of shared values

These values recur across packages. Entries use the meanings below; package text that disagrees is wrong. Each item was re-derived from the disassembly in [F03](f03-final-fixes.md).

### Program mode word `0x6662b8`

A signed WORD that the top loop `0x4679b0` dispatches through table `0x467b68` (`0x467a8e`–`0x467a9e`). Values above 10, and negative values, exit the program. During a game the next mode is the game's exit code WORD `0x666420`, set by `EndGame` `0x444e00`(CX = code) and returned by `RunGame` `0x445310`.

| Value | Meaning | Top-loop action | Main writers |
|---|---|---|---|
| 0 | Intro cinematics | `PlayCinematic` `0x47ec60`(0), then cinematic 24 when `0x45a700` reports expansion data, else 1; then mode 4 unless a cinematic changed the mode | main menu "View Intro" (`0x45a99d`) |
| 1 | Start the prepared game | mode = 3 (`0x467ad1`) | Create Game/Play Custom (`0x45564d`), the three ready rooms (`0x461249`, `0x461489`, `0x4616c9`), lobby start (`0x474f93`), loading a save (`0x498cfb`), the `noglues` cheat (`0x4576ab`) |
| 2 | Quit the program | exit list `0x4cf730` | main menu "Exit" (`0x45a9bd`), `RequestApplicationQuit` `0x440020`, exit code 2 |
| 3 | A game is running | `RunGameSession` `0x467ba0`: mode = exit code, or 4 when the game could not start | mode 1 or 5 only |
| 4 | Glue screens | `RunGlueLoop` `0x457510`, which runs while the mode stays 4 | start-up (`0x4679b7`), exit code 4 (quit a single-player mission or replay), `AdvanceCampaignAfterGame` `0x4bb790` |
| 5 | Restart the mission | `0x66fd2c` = 1, then mode 3 (`0x467ac7`) | exit code 5 from `ExecRestartGame` `0x472e20` (single player), `0x4bb790` after a loss (`0x4bb7b3`) |
| 6 | Game ended in victory | glue loop: score screen (glue state 17) with score flag 1, mode 4 | exit code 6 from `OnVictoryDialogButton` `0x4bd3c0` |
| 7 | Game ended in defeat | glue loop: score screen with score flag 0, mode 4 | exit code 7 from `OnDefeatDialogButton` `0x4bd130`, `DefeatDialogProc` `0x4bd170` and the multiplayer branches of the quit menus `0x47bed0`/`0x47bff0` |
| 8 | Credits | `RunGameCredits` `0x414e70`; if still 8, mode 4 and glue state 0 | main menu "Show Credits" (`0x45a9ad`) |
| 9 | Campaign epilog | `RunCampaignEpilog` `0x414ef0`; if still 9, mode 4 and glue state 0 | `0x4bb790` at the end of a campaign (`0x4bb890`) |
| 10 | Campaign cinematic | `PlayPendingCinematic` `0x4bbb20`; if still 10, `AdvanceCampaignAfterGame` `0x4bb790`(won = 1) | `StartCampaignEpisode` `0x4bb5b0` (`0x4bb68e`), `0x4bb790` (`0x4bb8c9`) |

The glue screens inside mode 4 have their own state number `0x66a274` (26 states, table in [P19](p19.md) `0x457510`).

### Dialog event records and types

An event is 20 bytes: `+0x00` u32 user code, `+0x04` u32 parameter, `+0x08` u16 key, character or command, `+0x0a` u16 modifiers, `+0x0c` u16 type, `+0x0e`/`+0x10` s16 cursor x/y. The window procedure `0x43f600` ([P13](p13.md)) produces the types, and the dialog dispatcher `0x4c2bb0` ([P42](p42.md)) routes them:

| Type | Event | Type | Event |
|---|---|---|---|
| 0 | key down | 10, 11, 12 | middle button down, up, double click |
| 1 | key repeat | 13 | idle |
| 2 | key up | 14 | user event (code in `+0x00`) |
| 3 | mouse move | 15 | character |
| 4, 5, 6 | left button down, up, double click | 16 | command (WM_COMMAND; given straight to the registered handler `0x665e30`, `0x43f755`) |
| 7, 8, 9 | right button down, up, double click | 17, 18 | wheel up, wheel down (`0x43fdb8`, `0x43fde1`) |

User codes (type 14): 0 create, 1 destroy, 2 close (dialog), 3 activate (press), 4 hit test, 5 focus request, 6 focus lost, 7 re-centre, 8 timer tick (parameter = timer ID), 9 mouse enter, 10 post-load initialization, 11 value changed, 13 show, 14 hide. These are the game's own dialog events (`dlg.cpp` in [P42](p42.md)), not Storm SDlg codes. For example, the credits handler `0x4149f0` maps types 4 and 7 to "next page", 13 to the scroll tick and 14 to create/destroy (byte map `0x414a7c`).

### CUnit status bits (`+0xdc`)

[P12](p12.md) lists every bit. Three of them were disputed; [V01](v01.md) item 1 settled them:

- `0x2` grounded building. It is set at creation from units.dat flag `0x2` (`0x425b4c`) and on landing (`0x436fcb`), and cleared on liftoff (`0x435f00`, `0x437010`). No instruction sets or clears it alone.
- `0x8000` cannot use ordinary weapons: the unit is under a Disruption Web. `0x43a880` sets it each Web step on ground units (`0x43a8b0`, `0x43a8cc`), and `0x438cd0` clears stale marks (`0x438d1c`, `0x438d35`).
- `0x20000` mobility bit. It is copied from units.dat flag `0x08000000` at creation (`0x425a70`) and at transport unload (`0x4249e0`), recomputed as NOT(units.dat `0x10`) on liftoff, set when an Archon or Dark Archon finishes its summon (order 106 handler `0x42f9e0`, state 1 on animation signal `0x4`) and cleared while a unit merges into an Archon (`0x42f830`, `0x42fa70`) or sits in a grounded building (`0x437c70`). BWAPI and GPTP call it "IsBuilding"; that name is wrong (Teippi: "Reacts").

### Flag `0x40`: image versus sprite

- **Image flags** WORD at CImage `+0x0c`: `0x40` = hidden. The sprite draw loop `0x415a80` skips such images (`0x415ab4`); `0x40f890` sets the flag and the show routines clear it.
- **Sprite flags** byte at CSprite `+0x0e`: `0x01` selection circle, `0x06` count of allied selections, `0x08` selected (health bar shown), `0x20` hidden (the unit is in the hidden list `0x665af8`), `0x40` burrowed (set by `0x43d940`, cleared by `0x436cf0` and `0x43c880`; `0x417400` uses it to choose cloak draw methods), `0x80` IScript no-break section.

### Carried-item overlay images

Images 392–404 and 958–959 are the powerups and resources a unit carries, which `HideCarriedItemOverlay` `0x416290` and `ShowCarriedItemOverlay` `0x4174d0` hide and show with the carrier. By images.dat: 392 Flag, 393 KerrEgg, 394 PsiEmit, 395 DataDisk, 396 KhChunk, 397–398 OreChunk, 399–400 GasOrb, 401–402 GasSac, 403–404 GasTank, 958 Uraj, 959 Khalis ([P05](p05.md)).

### Caller lists

Ghidra's caller lists miss callers reached through jump tables, conditional tail jumps and some `rel32` calls. Every "unreferenced", "no caller" or "always returns" claim in the atlas was checked against a raw-image scan ([F01](f01-corrections.md)). Caller lists that do not say "raw-image scan" can be incomplete.
