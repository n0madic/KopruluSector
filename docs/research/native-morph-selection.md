# Native 1.17 morph-parent and merge-pair selection

The special production selectors are recovered from the reviewed StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. The [static witnesses](evidence/native-morph-selection-static.json), [148 isolated native checks](evidence/native-morph-selection-probes.json) and [reproducible inspector/probe](../../tools/research/inspect_native_morph_selection_117.py) provide the evidence.

All **148 cases passed with no helper stubs**. Each selector executes to normal return, including actual list getter, invalid-unit predicate and pair-output helper, in private Unicorn memory. Every called instruction is allowlisted. No full game, WinMain, Windows API or host file/network operation executes. Fixtures supply artificial player lists, status, AI and region state; native input memory is checked unchanged. Selection success is distinct from subsequent morph/merge legality and execution.

## Entry points and actual callees

The [training consumer](native-training.md) routes requested Guardian44/Devourer62 to the Mutalisk43 selector, Lurker103 to the Hydralisk38 selector, Archon68 to the High Templar67 pair selector, and Dark Archon63 to the Dark Templar61 pair selector. Numeric names are correlated with the pinned unit schema; the native switches, type comparisons and fields establish the behavior. [Teippi unit IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/unit.h).

| Function | Native calling contract |
|---|---|
| `0x4b0350` Hydralisk parent | ECX=player; EAX=selected unit pointer or0. |
| `0x4b03c0` Mutalisk parent | Same. |
| `0x4b0430` High Templar pair | ECX=player, EDX=first-output pointer, stack=[second-output pointer]; EAX1/0. |
| `0x4b0250` Dark Templar pair | Same. |

List getter `0x4256f0` reads `0x665b08 + player*4`. Every selector follows unit+0x6c in existing list order. Predicate `0x41fe40` returns1 if unit+0xc is null, or primary-order byte+0x4d is zero; otherwise0. The selectors reject a nonzero result. They do not themselves search a spatial grid, sort by distance, or call RNG.

The list is authoritative: these functions do not compare each candidate's owner byte against the player argument. Native owner0 fixtures placed in player7's list remain selectable. That test exposes the function boundary; it does not assert such a mismatched list occurs in normal play.

## Hydralisk and Mutalisk parents

Both parent scans have the same algorithm with only the desired type changed:

1. Reject absent/dying units through `0x41fe40`, a mismatched type, a nonzero dword+0x7c, nonzero target pointer+0x5c, or null AI pointer+0x134.
2. If AI type byte+8 is1, return this unit immediately.
3. If AI type is4, save this unit as the fallback. Read region pointer AI+0x10 and region-state byte+5. Return immediately for states0/4/5.
4. Continue through the list; if no preferred candidate was found, return the **last accepted military AI unit** saved as fallback. Return0 if none exists.

AI types1/4 correspond to guard/military layouts in pinned reference structures. Field+0x7c has differing source labels: BWAPI calls it `autoTargetUnit` and notes its broader internal use; AISE/Teippi call it `previous_attacker`. This report retains the exact offset/null check rather than assigning one universal semantic name. [BWAPI CUnit fields](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/CUnit.h#L119), [AISE unit fields](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L329).

The scan does **not** require completed status bit1, does not reject status bit0x40, and does not reject primary morph order0x2a. Those exact absent filters are tested. Later specialized requirements and queue/resource checks can still reject the selected parent; selection alone is not acceptance. A preferred military unit earlier in the list wins over a later guard unit; guard is not globally ranked above all military units. A chain of three nonpreferred military units returns the third.

The pinned Teippi reconstruction returns null when all military region states are outside0/4/5. Local1.17 instead returns its last military fallback. Replacing the native scan with that reconstructed function would change the result. [Teippi FindAvailableUnit](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L902).

## Pair filters and preferred pair construction

Both pair selectors reject absent/dying units, wrong type, incomplete status bit1, status bit0x40, the already-active merge order, null AI, and AI types other than1/4. Active merge order is0x69 for High Templar and0xb7 for Dark Templar. Unlike parent selection, these pair scans do **not** reject a nonnull target+0x5c or dword+0x7c. Dedicated native fixtures distinguish both omissions.

An accepted guard AI candidate enters the preferred pair immediately, without an energy test. Accepted military candidates are also recorded in a separate fallback pair, regardless of region-state preference. Military preferred eligibility then requires region state in **{0,4,5,8,9}**. High Templar additionally requires byte unit+0xa3>=75; Dark Templar has no energy threshold. BWAPI describes energy as a u16 at+0xa2, so this native comparison uses its high byte, rather than comparing the complete u16. [BWAPI queue/energy fields](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/CUnit.h#L140).

Preferred pair helper `0x4adb10` takes ECX=&counter, EDX=&first, stack=[&second,candidate]. Counter starts at1. For counter1 it writes first=candidate, changes counter to2 and returns0. Otherwise it writes second=candidate and returns1. The selector returns success immediately on that second write. A preferred pair can therefore mix guard/military candidates and stops at the first two preferred candidates encountered.

Output pointers are not transactional: a single preferred candidate can write the first output even though the selector later returns0. Native one-guard/no-second cases observe that partial output. Callers must check the return value before consuming a pair.

## Dark Templar fallback: first and last

For every otherwise eligible military Dark Templar, the scan stores the first encountered unit if none is saved; every later military unit replaces the second fallback. At list end, if both exist, they overwrite the caller outputs and the selector returns1. Thus a list of three nonpreferred military candidates selects the **first and third**, not the first two.

Region-state preference only controls the early preferred-pair path. Nonpreferred states still contribute to fallback, and guard candidates do not contribute to this military fallback. A partially written preferred first output can be overwritten by a complete fallback pair. Probe chains cover pure fallback, mixed guard/military and preferred early exits, with all region-state values0..10.

## High Templar fallback: ordered energy updates

The High Templar fallback maintains first/second candidate pointers and their byte-energy values, initially null and9999. For each eligible military candidate:

```text
if first is null or energy < first_energy:
    first = candidate
    first_energy = energy
else if second is null or energy < second_energy:
    second = candidate
    second_energy = energy
```

The original **does not move the previous first into second** when a new lower-energy first arrives, nor clear an existing second. This is not a general two-smallest selection algorithm. Fallback also runs before the preferred energy>=75 test, so low-energy candidates remain eligible for fallback.

Three nonpreferred military candidates with energies `[100,50,25]` repeatedly replace first; second remains null and the selector returns0. The ascending `[25,50,100]` selects25/50. `[50,100,25]` leaves first25/second100, omitting the earlier50. All six permutations of25/50/100 execute against the unchanged native code, as do preferred/guard thresholds0/74/75/255. The reported ordering is native behavior, regardless of whether a more efficient merge choice was intended.

Guard preferred pairing ignores energy completely. Military preferred pairing requires>=75, while its fallback does not. Applying one global minimum-energy requirement would therefore change both branches.

Teippi's `Merge` adds target/previous-attacker exclusions and uses a different energy-selection structure. Its reconstruction remains useful for identifying callers and conceptual roles, but it is not a replacement for the local native filters or fallback updates. [Pinned Teippi merge implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L939).

## Closed contract and subsequent execution boundary

This batch closes all four selectors, their filters, list order, preferred region-state sets, military fallbacks, energy-byte decisions, pair-output writes and actual callees. All148 probes execute complete functions to normal return and confirm unit/AI/region inputs remain unchanged. They establish selection decisions for the declared fixtures, not availability of these states during a campaign.

The training consumer subsequently checks requirements/resources/limits for a selected single parent, updates AI membership and issues a primary morph order. Pair paths issue merge orders to both selected units. Those transitions require AI migration, primary-order allocation, morph/merge order state machines, movement, cancellation and completion. The [training report](native-training.md) identifies the concrete caller addresses; these execution dependencies are not silently included in this selector completion claim.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_morph_selection_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-morph-selection-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-morph-selection-probes.json
```

Default inspection is static and hash-gated. Explicit probes require the reviewed x86_64 Unicorn runtime; output records exact target/script hashes, runtime versions/architecture, instruction counts, native call trace, input-immutability checks and no-stub status. An unexpected instruction outside the reviewed ranges aborts the probe. A green selector batch is not full-game, campaign or Defold runtime parity.
