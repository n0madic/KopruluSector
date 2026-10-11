# Native 1.17 regional enemy-strength rebuild

The original function `0x4abf90` rebuilds regional enemy strength; it does not recruit military units. Its complete selected paths execute with all reached original callees in **53 passing cases**, without helper stubs. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [seven static witnesses, 158 instructions](evidence/native-enemy-strength-static.json), [53 complete original-code cases](evidence/native-enemy-strength-probes.json), and [inspector/probe](../../tools/research/inspect_native_enemy_strength_117.py). The longest complete call uses1015 instructions. Fixtures supply private terrain, units, player records, diplomacy and eight regional arrays. No complete game, WinMain, Windows API, host-file operation or network operation executes. The cases establish native behavior on these inputs, separately from authentic campaign execution.

## Clearing and accumulation

The function has no player argument. It returns immediately unless global dword `0x6b1a94` is **zero**. Three complete calls distinguish nonzero values1,7 and`0xffffffff`, leaving all arrays unchanged and making no helper calls. The global's value is a native eligibility condition; interpreting every writer and its scheduling purpose remains a lifecycle dependency.

It reads terrain region count through actual `0x481b00`, which sign-extends the word at the terrain structure referenced by `0x69b584`. For **all eight regional arrays** at `0x6b1a74[player]`, it clears the combined dword at region+`0x18` for indices **1 through count-1**. This clears both enemy-air u16+`0x18` and enemy-ground u16+`0x1a`. Region0 is preserved. Clearing is independent of player type: even arrays whose player type will reject contributions are reset. Counts0,1,2,4 distinguish empty clearing, the first cleared index, and the exclusive upper boundary. Invalid negative counts are not a supported fixture or a recovered terrain-loader outcome.

It then scans the actual global active-unit chain: `0x425600 → [0x665ae0]`, next pointer Unit+4. A unit is excluded by `0x41fe40` when its sprite pointer is null or its primary order byte is0. For a surviving unit, its contribution is:

```text
value = max(u16 Unit+0x138 cached_air_strength,
            u16 Unit+0x13a cached_ground_strength)
```

A zero value skips terrain lookup and player comparisons. Otherwise `0x481ba0 → 0x481b10` resolves the unit's **actual sprite position** to a terrain-region ID, including the original split-cell mask path. It does not use MilitaryAI assignment, a stored order destination, or a UnitAI region pointer. Two cases supply encoded split cell`0x2000` and distinguish both mask outcomes through the real pixel-region helper.

For each player0..7, a contribution requires player-record type byte `0x6726d0 + player*36` equal to **1**, native Unit owner byte+`0x4c` different from that player, and actual hostility `0x429110` true. The hostility helper accepts exactly diplomacy byte0 at `0x534914 + player*12 + comparison_owner`. For native owner11, the comparison owner comes from sprite+`0x0a`; the earlier same-owner exclusion still compares the original Unit owner. Eight player-type cases and fourteen diplomacy/owner11 cases distinguish these conditions. An own native-owner unit is excluded even when the supplied diplomacy byte says hostile. An owner11 unit with sprite owner equal to the computer player can contribute when that comparison diplomacy byte is0.

**Physical flying status**, Unit+`0xdc` bit4, selects the accumulator: flying adds `value` to enemy-air+`0x18`; grounded adds the same `value` to enemy-ground+`0x1a`. This does not add each cached component to its matching mode. Eight cases distinguish the larger component, equality, one zero component and flying category. Therefore a flying unit with only nonzero cached ground strength still contributes that maximum to the enemy-air field.

Both additions use native **u16 wrapping arithmetic**. Multiple units can wrap the accumulator to0. Three two-unit cases distinguish ordinary cleared regions and region0's retained baseline. A unit resolving to region0 can add to its old value even though that region was excluded from the clearing loop. The function itself does not bound the resolved ID against region count; valid indexed fixture IDs are supplied, and general terrain validity is separately recovered in [terrain import](native-terrain-import.md).

## Filters, producers and consumers

Seven cases deliberately vary completion, cloaking, invincibility, hallucination, InBuilding status, worker properties and building properties. None supplies an additional exclusion in this producer. Its reached predicate is the sprite/order check described above, its arithmetic input is already cached strength, and its player filter is type/ownership/diplomacy. There is no visibility/detection check, UnitAI-class requirement, or completed-unit requirement here. This observation does not imply that every active object has nonzero strength or that later targeting can attack an invisible, invincible or unfinished unit.

A mixed three-unit case records all eight arrays after contributions from different owners, regions and physical categories, including a player-specific ally exclusion. The probes record exact original call traces and regional memory-write instruction addresses; the native clear is a dword write while individual contributions are word additions.

The caches' upstream per-unit maintenance is a separate recovered mechanism in [unit AI and combat strength](native-unit-ai.md). The regional consumer `0x4adbd0` is recovered in [campaign wave launch](native-campaign-wave.md): it raises needed-air from enemy-air and needed-ground from enemy-ground, uses a1.5 multiplier outside campaign mode, and then applies a nonzero max-force cap. This report closes the regional threat producer on the selected native paths, without replacing the upstream cache calculation or the later army/attack actions.

Pinned [AISE AiRegion layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates the enemy-air/enemy-ground field ordering. [Teippi's hit-reaction strength update](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai_hit_reactions.cpp#L472) writes these fields through its own strength helpers. That modified hit-reaction implementation is secondary context; its policy is not substituted for the local1.17 function recovered here.

## Reproduction and remaining lifecycle work

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_enemy_strength_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-enemy-strength-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-enemy-strength-probes.json
```

Default mode only reads/disassembles the hash-gated executable. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 under Rosetta. Script SHA-256: `5fe2f3c80a062d21fd27c1d58d4928f354371b3fdfd2976e47c462989470517c`. There are no imported research-script dependencies and no DAT file requirement in this batch. JSON records the script hash, runtime versions, reviewed instruction ranges, exact fixture inputs, complete returns and observed arrays. Each entry has a100,000-instruction bound.

The remaining strategic composition work is the writer/caller lifecycle of`0x6b1a94`, periodic regional scheduling, initial recruitment into a prepared army, positive radius recruitment, and later regional states and movement. `0x4abf90` itself is **not an initial recruitment helper**. [Positive attack consumers](native-attack-consumers.md) independently compose regional selection with ComputerAI issuance and global fallback with an actual state9 transfer. Those results and these complete producer calls do not constitute campaign walkthrough or full-game parity.
