# Native 1.17 positive strategic target selection

This batch closes positive paths through strategic score `0x4a9460`, regional selector `0x4a90d0`, its actual spatial-query consumers, and global wave target selector `0x4aba70`. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [native witnesses and score switch](evidence/native-strategic-targets-static.json), [82 complete original-code cases](evidence/native-strategic-targets-probes.json), and [inspector/probe](../../tools/research/inspect_native_strategic_targets_117.py). **All82 cases pass without helper stubs.** Native score, cargo resolution, life, attack eligibility, detector, terrain and spatial-query/release helpers actually execute. Inputs are explicitly synthetic objects, DAT/strength tables and spatial indexes. No WinMain, full game, Windows/host API or guest file/network operation executes.

This extends [campaign waves](native-campaign-wave.md), where regional target calls had only empty-enemy fixtures, and reuses the independently recovered [unit-strength/cargo contracts](native-unit-ai.md). Selected original functions returning completely does not establish their upstream data production, physical attacks or campaign parity.

## Strategic score and unit-specific modifiers

`0x4a9460(ECX=target, EDX=mode)` starts from native strength multiplier `0x4a9630`: mode0 reads the228-entry air-strength table, mode1 the ground-strength table. This is the **target's** strength under the requested mode, rather than a filter by its physical flight status.

IDs35,36,59,97 return0. A hallucinated target whose current rounded life is below maximum life also returns0. Critter IDs89,90,93,94,95,96 return1. Otherwise original `0x4a9520` modifies the native multiplier using the unchanged7-case switch and84-byte mapping:

| Unit ID | Modifier before the life ratio |
|---:|---|
| 1 | Current order123..129 replaces the value with1000; other orders preserve it |
| 2 | Add100 times byte Unit+`0xc0` |
| 11,42,57,69 | Add100 times actual cargo space used across all eight cargo slots |
| 30 | Add `floor(value/4)` |
| 34 | Add mode-specific Marine0 type strength |
| 84 | Add50 |
| Other IDs, including0 and IDs>84 | Preserve the value |

The result is `(modified_value << 7) / max(rounded_life,1)`, using native **u32 shift/division arithmetic**. Rounded life uses rounded-up HP plus truncated shields when the shield-enabled DAT byte is set. A zero ratio returns2 when status`0x20000` is set, otherwise1. Excluded IDs and damaged hallucinations retain their earlier0 result. This ratio favors a larger threat relative to remaining life; it is not the current-strength formula that multiplies by life fraction.

Twenty-six cases cover every modifier category, both modes, and order122/123/129/130 boundaries. Ten cover excluded IDs, critter89, zero HP, zero multiplier, status-based floors and hallucination life. Ten additional composed cases execute a real packed cargo reference through `0x438350 → 0x428b50 → 0x4255f0` and the original unit generation/validity checks. Cargo of space2 adds200 for the transport IDs. Bunker125 instead uses the actual cargo-space multiplier times Marine strength in `0x4a9630`, then the ordinary score ratio. The native array slot, packed ID, generation and space DAT byte are explicit fixture inputs.

The inspector records the score switch as data. Its mapping bytes and seven targets are not treated as x86 instructions. No unit modifier is inferred from a similarly named modified AI function.

## Regional target filtering, maximum score and ties

`0x4a90d0(ECX=player, EDX=optional_attacker, stack=[region_id,candidate_array,mode])` reads a null-terminated candidate pointer array. It chooses the **strictly greatest signed score**, initially comparing against-1. Equal scores retain the first candidate. Seven cases distinguish larger score in either list position, equality, ownership, invulnerability status`0x04000000`, invisibility status`0x300` without detection, and the player's detection bit128. All execute the actual query-release helper and restore depth and scratch offset.

Campaign mode rejects critters. Noncampaign mode permits them even when allied; four explicit critter89 cases distinguish campaign and ownership. A large artificial score with bit31 set is rejected by the signed maximum comparison against-1. This separately verifies signed ranking after unsigned score arithmetic; it is not asserted to be an ordinary campaign input.

Air and ground regional selectors differ by **strength-table mode**, not by whether a candidate flies. Four cases give types37/38 opposing air/ground strength values; mode0 selects38 and mode1 selects37, with both candidates either grounded or flying. Thus the names `air_target` and `ground_target` alone do not justify filtering the candidate list by flight status.

When an optional attacker is supplied, the native selector also runs actual `0x41da50` attack eligibility, the chase rejection helper and context-specific position constraints. Four Wraith8-attacker cases execute the score bonus: a target with active detector DAT property`0x8000`, Completed status and status`0x20000` receives **+1000**. Disabled status`0x400` or blindness byte+`0x123` suppresses this detector bonus. The attack/detection checks are actual original calls, with explicit weapon DAT inputs.

For a GuardAI attacker, an enemy in the guard's home region passes the region constraint; an enemy elsewhere requires both enemy and attacker to lie within distance192 of the home point. Four cases compare target distances128,192,193,256 while the attacker is at home. The original test uses Unit fixed-point positions+`0x2c/+0x30`, native approximate distance and `distance >> 8`, rather than the sprite coordinate alone. Equality192 passes. The fixture sets both position representations explicitly.

Two composed positive cases begin at `0x4adcf0` with an actual indexed enemy in native unit-array slot1. They execute both `0x4a9800/0x4a9820 → 0x4222d0 → 0x422330 → 0x4a90d0`, and write the same enemy into region air/ground target fields+`0x1c/+0x20`. The military list is explicitly empty, so this composition proves target fields and query release without claiming a following unit attack order.

The direct multi-candidate cases supply an already-created candidate array and matching query-depth fixture; they still execute the real release operation. The positive single-enemy compositions additionally execute the original spatial query that produces the array. The general non-Guard attacker out-of-region distance branch and some special attack-capability branches are not covered by this batch.

## Global wave target selection uses a different policy

`0x4aba70(ECX=military_region)` first gathers ground-member presence and missing-weapon booleans, then scans the global active unit list. It does **not** call the strategic score ratio above. For ordinary eligible enemy regions it tracks the nearest Building-status target separately from the nearest non-building, using the native approximate distance from the source region center. A qualifying building takes precedence over an ordinary target even when the ordinary target is closer. Strict smaller-distance comparisons retain the first target on ties.

Six positive cases discriminate building versus ordinary status, reversed global list order, weapon compatibility, and the fallback pointer. A building64 pixels from the source wins over an ordinary unit32 pixels away. Changing that same type106 object's Building status to clear makes the nearer ordinary unit win. Two equal-distance building cases select the first global-list entry in either list order.

The selector maintains a separate fallback pointer before some weapon/category rejections. An explicitly weaponless flying group can return this fallback enemy even when no ranked weapon-compatible target survives. This is a target-region choice; it does not prove that a member can subsequently damage the returned unit. The broader fallback includes special existing-region-state branches recorded in the witness but not exhaustively composed here.

The global selector's literal gates differ from the regional selector: the exercised cloaked-status`0x300` and invulnerable-status`0x04000000` Building targets can still become its result. It checks active sprite/nonzero order and hostility, but the selected global paths do not impose the regional invisibility/detection and invulnerability gates. Those later attack checks must remain separate in the remake.

Two ground-group cases execute actual completed transport counts, Ventral Sacs accessor and `0x48e980 → 0x481d00` terrain-group reachability, with movement-check flag set. A reachable enemy region is selected; a differing connectivity group yields no target. This extends the transport and terrain contracts in [campaign waves](native-campaign-wave.md) and [placement](native-placement.md). Transport counts, upgrade level and group records are fixture inputs; transport movement itself does not execute.

## Reproduction and evidence boundaries

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_strategic_targets_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-strategic-targets-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-strategic-targets-probes.json
```

Default mode only reads/disassembles the hash-gated PE. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4 under x86_64 Rosetta. JSON includes the script/dependency hashes, unchanged native switch mapping, instruction allowlist, actual helper arguments, complete returns and expected/observed effects. No callable helper is replaced by a fixture result.

Pinned [Teippi target selection](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L2437) is **modified secondary evidence**: its optimized candidate sorting and policy must not replace the native max-score scan recovered here. [AISE region structure](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates target/strength field names only.

Selected positive military ComputerAI-order consumers and actual global retarget→state9 transfer are separately composed in [attack consumers](native-attack-consumers.md). Remaining work includes initial military recruitment from the periodic strategic scheduler, upstream dynamic enemy-strength data, later movement/ComputerAI/combat execution, and the remaining transport/alternate target branches. The score and selected positive selectors are directly established here; completing their wrapper returns does not silently close those remaining consumers.
