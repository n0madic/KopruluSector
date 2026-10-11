# Native 1.17 briefing interpreter and CHK loading contracts

The exact target is `StarCraft.exe` 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Addresses below are preferred virtual addresses; subtract `0x400000` for their RVAs. These addresses are specific to this executable and do not carry over to 1.16.1.

The [probe evidence](evidence/native-briefing-loader-probes.json) contains **957 successful isolated checks of original instructions**: 89 synthetic boundary cases, 612 section-leaf checks using the actual bytes of all 68 physical campaign CHKs, and 256 required-section presence checks using all 64 supported catalog CHKs. Every selected native callee is the original EXE implementation. There are no stubs, DLL imports, Windows API calls, original-game process launches or complete-map execution in this batch. One timer test deliberately executes only a decision fragment; its entry registers and terminal PCs are recorded explicitly.

The reproducible [probe script](../../tools/research/probe_native_briefing_loader_117.py) records its own SHA-256, the executable hash, Python version, x86_64 runtime architecture and Unicorn 2.1.4 version. It also records every campaign CHK hash, per-section input/output hashes, exact handler addresses and instruction counts. Original assets remain in scratch storage. The broader archive inventory and campaign progression are documented in [campaigns](campaigns.md) and [native triggers](native-triggers.md).

## Version selection and loading groups

Native version selection at `0x469700` searches three 40-byte descriptors. At `0x469770` it reads the loaded u16 version from the output structure at `+0x18`; `0x469774` compares it with each descriptor. The expansion-only descriptor additionally checks byte `0x536720`. This selection routine was inspected statically; its initialization calls an imported Storm routine and was not executed as a whole.

| CHK version | Descriptor VA / RVA | Expansion required | Header entries | Briefing entries | Melee entries | UMS entries |
|---|---|---|---:|---:|---:|---:|
| 59 | `0x4eeb80` / `0xeeb80` | No | 9 | 2 | 4 | 19 |
| 63 | `0x4eeba8` / `0xeeba8` | No | 9 | 2 | 4 | 19 |
| 205 | `0x4eebd0` / `0xeebd0` | Yes | 9 | 2 | 5 | 15 |

These four groups are **conditional loading groups**, not four unconditional sequential stages. Caller `0x469970` selects the header group for mode argument 1. For mode argument 2, it calls `0x447db0`; nonzero selects the UMS group at descriptor offset `+0x1c`, and zero selects the melee group at `+0x14`. The separate briefing caller uses descriptor offset `+0x0c` at `0x46a729`–`0x46a737`. The labels melee/UMS agree with the selected section sets; the predicate itself checks three mode bytes at offsets `+0x75`, `+0x79` and `+0x7f` of the supplied/default game configuration.

Each table row is 12 bytes: a four-byte section tag, callback pointer and required flag. The evidence retains every row. Important differences are:

- All versions require `VER `, `DIM `, `ERA `, `OWNR`, `SIDE`, `STR `, `SPRP`, `FORC` and `VCOD` in the header group. The briefing group requires `STR ` and `MBRF`.
- The melee group requires `STR `, `MTXM`, `THG2` and `UNIT`; version 205 also requires `COLR`.
- UMS versions 59/63 require base unit/upgrade/technology sections and treat five expansion override sections as optional. Version 205 requires the expansion versions instead. Version 59 selects the 64-location `MRGN` loader; versions 63/205 select the 255-location loader.

The four physical prerelease resources have version 57 and no descriptor in this native table. Their `VER ` section leaf still copies the number 57 successfully. That leaf result does not establish acceptance by the complete loader: the statically recovered preflight rejects an unknown descriptor. The 31 version-59 and 33 version-205 catalog maps have supported descriptors; all 64 passed each group's required-presence checker independently. This does not mean all four groups are executed for a campaign load.

## Required sections and duplicate handling

Required-section checker `0x4c1020` inspects the required byte at table-row offset `+8` and searches the linked chunk list for any matching tag. Optional absent sections are accepted. A missing required tag returns zero. Four synthetic cases execute this original routine for absent optional, absent required, wrong-tag and matching-tag inputs; the 256 catalog checks execute it against the real tag sets and version-specific native tables.

Dispatcher `0x4c12e0` uses **table order as its outer loop and chunk-list order as its inner loop**. At `0x4c1303` it compares the node's tag with the current table tag, calls that row's callback at `0x4c1318`, and follows the next chunk pointer at `0x4c131e`. It processes every matching duplicate. A callback returning zero immediately aborts the dispatcher; it does not undo earlier mutations.

Three original-execution cases use the real `DIM ` loader as the callback:

| Duplicate payload lengths | Dispatcher return | Dimensions after return |
|---|---:|---|
| 4, 4 | 1 | Second payload: 128 × 128 |
| 3, 4 | 0 | No valid first write |
| 4, 3 | 0 | First payload remains: 64 × 96 |

The duplicate list is a synthetic native-layout fixture. The chunk enumerator's allocation, malformed-header behavior and list construction were not executed. Consequently, the confirmed contract is the dispatcher's behavior **given that ordered list**, rather than universal acceptance of every hand-crafted CHK container. A converter should retain ordered duplicate sections until the relevant native callback's overwrite/append behavior is known. Collapsing all tags to their last payload would lose the failure behavior above.

The bounded section reader at `0x4c12b0` reads `{end_pointer, payload_pointer, tag, length}`. It computes `payload_pointer + length`, rejects an unsigned value above the supplied end pointer, and otherwise calls the original EXE memmove at `0x4d16a0`. Equality with the end pointer is accepted. A four-byte copy was tested with three, four and five readable bytes. The probe sets native CPU-feature state `0x6d319c` to select the real `rep movsb` path; it does not replace memmove. Pointer-addition overflow and malformed chunk-enumerator inputs remain outside these cases.

## Core normalization and defaults

The following handlers execute completely within the allowlist, including the actual bounded reader and native memory-copy implementation. Fixed-length tests use undersized, exact and oversized payloads; the actual campaign cases verify copied output bytes or the specified normalization.

| Section | Handler VA / RVA | Accepted payload and observed result |
|---|---|---|
| `TYPE` | `0x468a20` / `0x68a20` | Static no-op returning one; content not used by this leaf |
| `VER ` | `0x468a30` / `0x68a30` | Exactly 2 bytes; copied to output `+0x18` |
| `SIDE` | `0x468a60` / `0x68a60` | Exactly 12 bytes; copied unchanged to `0x673d51 + 36*player` |
| `OWNR` | `0x468ab0` / `0x68ab0` | Exactly 12 bytes; copied unchanged to `0x673d50 + 36*player` |
| `ERA ` | `0x468b00` / `0x68b00` | Exactly 2 bytes; expansion gate, then `value & 7` |
| `DIM ` | `0x469130` / `0x69130` | Exactly 4 bytes; width/height copied to `0x5264b4` / `0x5264b6` |
| `SPRP` | `0x4692f0` / `0x692f0` | Exactly 4 bytes; copied to the output structure's start |
| `FORC` | `0x469320` / `0x69320` | Length 0–20; clears 20 output bytes, then copies the supplied prefix |
| `MRGN`, version 59 | `0x468d50` / `0x68d50` | Exactly 1,280 bytes, 64 records; copied to `0x534f40` |
| `MRGN`, versions 63/205 | `0x468d80` / `0x68d80` | Exactly 5,100 bytes, 255 records; same destination |
| `UPRP` | `0x468db0` / `0x68db0` | Exactly 1,280 bytes; copied to `0x6c4700` |

`ERA ` first writes the raw u16 tileset value. Without expansion enabled, a value above four then returns failure **with that raw write retained**. On acceptance it masks the value with seven. Native cases test values 0, 4, 5, 7, 8 and 65535 with expansion both off and on. This combines normalization with a partial-mutation failure case.

`FORC` executes its clearing stores before its bounded copy. Native lengths 0, 1, 19 and 20 produce the supplied prefix plus zero-filled tail; length 21 is rejected without changing the output. Empty `FORC` therefore has a concrete all-zero default at this leaf. Editor-library object defaults are not a substitute for this original behavior.

`SIDE` and `OWNR` populate the map-setup player structures, not the later runtime player array at `0x6726d0`. These leaves do not resolve Random/UserSelect races, convert every controller mode, populate alliances or initialize full runtime players. `DIM ` contains no dimension-range validation in this leaf. The property reset at `0x4b6a70` was executed separately: it calls the original EXE memset at `0x4d0c60`, zeroes exactly 1,280 bytes at `0x6c4700`, and preserves guards immediately before/after that buffer.

`TRIG` at `0x468de0` and `MBRF` at `0x468e80` require payload lengths divisible by 2,400. Native probes execute early rejection for lengths 1, 2399, 2401 and 4799, before the allocation path. Successful trigger allocation and owner expansion are static evidence here; [native triggers](native-triggers.md) describes their structures and traversal.

All 68 physical CHKs provide the nine selected data-bearing tags `VER `, `SIDE`, `OWNR`, `ERA `, `DIM `, `SPRP`, `FORC`, `MRGN` and `UPRP`. The batch executes all **9 × 68 = 612** native leaf calls and checks their results against each input's hash-verified bytes. The four version-57 extras are included in this leaf coverage, but are excluded from the 64-map supported-descriptor presence coverage. This is campaign-wide **input coverage for these leaves**, not complete map loading or a campaign walkthrough.

## MBRF interpreter, ownership and continuation

MBRF uses the same 2,400-byte record format as TRIG, but a separate interpreter and action table. The native loader at `0x468e80` copies the section and calls `0x4607e0` for each record. That owner-expansion function accepts the local player from `0x508d50`, AllPlayers owner 17 and matching local-player Forces 1–4 in owner bytes 18–21. It appends copies to a separate list at `0x5082f8`. This allocation/selection path was inspected statically; the isolated VM receives constructed records directly.

Interpreter `0x44fee0` receives the raw payload pointer. It tests **only the first condition's type byte at `+0x0f` against 13**. It does not call the TRIG condition evaluator or inspect the other 15 conditions. The first condition's ignore flag does not suppress this type check. Native cases with type 0 or Always 22 finish immediately without action execution; type 13 executes even when that condition has flag `0x02`.

The VM sets execution flag `0x01` at raw offset `+0x940`, resumes action index byte `+0x95f`, and visits up to 64 actions at `+320 + index*32`. Action flag `0x02` skips an entry. Enabled type zero terminates the remaining actions. Other IDs select the ten-pointer table at `0x503908`. A zero handler result suspends with the current index; success advances. Completing the list clears only running flag `0x01` and resets the index to zero. Native cases cover initial indices 0, 63 and 64.

Unlike the normal TRIG list runner, this VM does not skip execution flag `0x08`: a native case with that flag still completes its Wait and retains flag `0x08`. A Defold briefing VM should have its own state transitions rather than inherit all TRIG disable/preserve rules.

UI list runner `0x460ea0` passes `node+8` to the VM. It advances its current-node pointer at owner/UI structure offset `+0x26` after success and leaves it unchanged after suspension. Reaching the last record clears that pointer; a mode-dependent branch then invokes further UI handling.

**Briefing timing (resolved by the function atlas, [P21 `0x460a70`/`0x460ea0`](atlas/p21.md)).** Ready-room setup `0x460a70` creates UI timer 10 on the ready-room dialog with a **100 ms** period and callback `0x460ea0`, after resetting the current node to the first record (`0x460dd0`). Each tick runs the current record's actions until one suspends; a completed record advances the pointer, so the next record starts on the next tick, not in the same tick. At the end of the list the pointer becomes 0; in multiplayer the runner then calls `0x460a20` (finish/ready), while offline nothing else happens and the player presses Start. With no MBRF record, the dialog shows control −14 "No briefing data" and, offline, disables Replay (control 20); Replay restarts the list through `0x460dd0`.

**Briefing action table `0x503908` (resolved by the function atlas, [P17](atlas/p17.md)).** The ten pointers, indexed by action ID without a bounds check in `0x44fee0`, are: 0 None `0x4b8e70` (never reached, because type 0 ends the list), 1 Wait `0x44fcd0`, 2 Play WAV `0x44fd10`, 3 Display Text Message `0x44fda0`, 4 Set Mission Objectives `0x44fdc0`, 5 Show Portrait `0x44fde0`, 6 Hide Portrait `0x44fe00`, 7 Talking Portrait `0x44fe10`, 8 Transmission `0x44fe30`, 9 Enable Skip Tutorial `0x44fec0`. Only Wait and Transmission suspend the record; every other handler returns 1 at once. Display Text renders the map string into control −10 for `+0x0c` ms (timer 1) and scrolls text taller than the control. Set Mission Objectives writes the local player's objectives entry `0x5349a4[player]`, which the mission inherits, and shows control −11. Show/Hide/Talking Portrait address slot control `15 + byte +0x10`; Talking Portrait runs for `+0x0c` ms without waiting. Transmission plays the WAV, displays the text for the **unmodified** `+0x0c`, talks in slot `+0x10` for the modified duration `d` (the unit type `+0x18` is ignored), then waits `d` through `0x4611a0`. Enable Skip Tutorial shows controls 19 and −13 only offline with a nonzero campaign mission key `0x526524`.

The action names come from pinned [BWAPI `Triggers.h`](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h); the [native trigger evidence](evidence/native-triggers-static.json) records all ten exact pointers: None, Wait, PlayWAV, DisplayTextMessage, SetMissionObjectives, ShowPortrait, HidePortrait, TalkingPortrait, Transmission and EnableSkipTutorial. The schema is also represented by pinned [PyMS `CHKSectionMBRF`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionMBRF.py). These names/schema do not establish the complete audiovisual behavior of every handler.

## Briefing waits and the UI clock

Briefing Wait at `0x44fcd0` uses one global active byte, `0x667bac`, independently of the eight player TRIG wait timers:

1. If the global is active, the handler returns zero and does not complete the action.
2. If inactive and action entry flag `0x01` is set, it clears that flag and returns one.
3. Otherwise it passes action u32 duration `+0x0c` to `0x4611a0`, sets the entry flag and global active byte, and returns zero.

The first two paths and their integration with the complete VM execute original instructions in the synthetic cases. The third path is static because it creates/schedules a UI event. `0x4611a0` passes UI owner pointer `0x66b40c`, event ID 11 and callback `0x4611c0` to timer creator `0x4c5960`. The callback calls `0x44ff60` to clear `0x667bac`, then tail-jumps to event cancellation `0x4c58f0`. Two native callback cases confirm the clear and cancellation selector with both event queues empty; successful queue removal/free was not executed.

Timer creation stores a 28-byte node with next pointer `+0`, owner `+4`, callback `+8`, start tick `+0x0c`, duration `+0x10`, u16 event ID `+0x14` and correction `+0x18`. It calls IAT slot `0x4eb2fc`, identified from the PE imports as `KERNEL32.dll!GetTickCount`. This is a UI clock, separate from simulation frame/game-second dividers. The import was inspected but never invoked.

The original expiry decision at `0x4c5a42` subtracts start and duration from the current tick, tests the signed result and branches with `jle` at `0x4c5a4e`. **Only positive lateness expires the timer; exact equality remains waiting.** This differs from the TRIG wait loop's `remaining <= delta` expiry. For lateness below 500 it updates correction using arithmetic shift: `correction += (lateness - correction) >> 5`. At 500 or above it leaves correction unchanged. Sixteen original-fragment cases cover deadline−1, equality, deadline+1, lateness 31/32/499/500, previous correction 0/32, and a 32-bit tick-wrap boundary. The fragment starts with explicitly supplied ESI=node and EBX=current_tick and stops before callback execution or queue handling; it does not measure real UI scheduling.

Briefing Transmission at `0x44fe30` shares the same active/entry flags. Its static duration arithmetic starts with action `+0x0c`: modifier 7 replaces it with `+0x14`, 8 adds `+0x14`, and 9 subtracts with a zero floor. It calls WAV, string, text and portrait helpers, then the same wait scheduler. Original normal TRIG Transmission uses a different timer path, including an indefinite single-player wait; it should not be substituted for MBRF Transmission.

## Evidence boundaries and remaining dependencies

Pinned [OpenBW `load_map_data`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L21230) is a useful independent implementation: its table-ordered chunk processing agrees with the native duplicate-dispatch shape. Its force parser consumes typed fields from a nonempty payload, whereas the native leaf accepts every length up to 20 and zero-fills the rest. Its tileset parser masks/modulos eight without the native non-expansion gate. OpenBW is therefore comparison material rather than a universal oracle for malformed inputs or campaign loader normalization.

Confirmed contracts here are the separate MBRF interpreter's tested control paths, core section lengths/copies/defaults, the property reset, required presence, duplicate dispatch and a UI timer decision fragment. Closing these contracts does not close the following dependencies:

- Native chunk enumeration and allocation, complete version preflight, `VCOD` verification, STR allocation/lifetime and malformed string behavior.
- UMS unit/weapon/upgrade/technology overrides, per-player availability, `UNIT` creation and property-validity normalization, `THG2` creation, `COLR` palette behavior, terrain/visibility loading and runtime player setup.
- Execution (rather than static recovery) of briefing owner allocation, the UI list lifecycle, timer creation/cancel/free/requeue ordering, user skip/replay, objectives/text layout, WAV scheduling, portrait animation and Transmission audiovisual completion. Their static behavior is specified in the function atlas ([P17](atlas/p17.md), [P21](atlas/p21.md)).
- Full campaign load/briefing/playthrough traces and save/restore of the connected state.

These dependencies must be investigated or validated in their own native contracts and campaign runs. The 957 checks do not establish all map-loading semantics or six-campaign gameplay parity.

Reproduction details, dependency versions and scratch extraction paths are maintained in [reproduction](reproduction.md). This batch can be rerun with the command below; the explicit execution option distinguishes a plan from original-instruction execution.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_briefing_loader_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --execute-original-code \
  --out docs/research/evidence/native-briefing-loader-probes.json
```
