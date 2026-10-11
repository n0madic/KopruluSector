# Native 1.17 military selection and guard adoption

Nearest available military selector`0x4af750` and the selected guard-adoption chain through actual AI removal`0x48dbc0` and primary-order issue are recovered from StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase`0x400000`. Evidence: [static witnesses](evidence/native-guard-transfer-static.json), [79 isolated original-code checks](evidence/native-guard-transfer-probes.json), [inspector/probe](../../tools/research/inspect_native_guard_transfer_117.py).

All **79 checks passed without helper stubs**, including complete Zergling/Scourge adoption with real military/free-order lists and unchanged stock order flags. Original instructions execute in private Unicorn memory under an instruction allowlist. No WinMain, complete game, Windows API or guest host-file/network operation executes. This report closes the successful-adoption dependency of [guard rebuilding](native-guard-lifecycle.md). The additional [generic-removal batch](native-ai-removal.md) supplies 113 complete checks for killed-unit, state8, Medic, worker/building and town-migration paths; higher-level lifecycle callers remain separate work.

## Nearest available military selector

`0x4af750(ECX=player, EDX=desired_ID, stack=[x,y])` obtains that player's unit-list head through`0x4256f0` and follows unit+`0x6c`. It accepts a candidate only when:

- Actual inactive predicate`0x41fe40` reports a nonnull sprite and primary order other than0.
- The current u16 unit type equals the requested ID.
- The unit's AI pointer is nonnull and its type byte+8 equals4.
- The AI region's state byte+5 differs from1,7,8,9.
- `u16(region+0x14) - u16(unit+0x13a) > u16(region+0x0c)`, using a **signed** comparison of the subtraction result.

The region fields are cached all-ground strength and required ground strength, and the unit field is cached ground strength. Actual leaf`0x4a9a20` returns the u16 value at unit+`0x13a`; it does not recalculate health, weapon or strength. The broader cache-generation and region-update mechanisms are separately recovered in [combat AI](native-combat-ai.md) and [unit AI](native-unit-ai.md).

Forty-four cases distinguish all states0..10 and strict spare-strength boundaries. In particular, equality rejects, and a negative subtraction does not become a large unsigned surplus. Two additional cached-strength boundary cases distinguish0 and65535. State2 or3 can pass this selector, even though the destination guard-region predicate has a different state3 rejection; source and destination regions must retain their separate tests.

The selector does **not** require completed status, reject a current primary target or auto-target, or test that the military AI's parent field equals the unit. Seven cases distinguish the actual live-sprite/type/AI tests from these omitted completion/target tests. They use controlled unit-list membership; no claim is made that inconsistent owners or malformed records are authentic campaign state.

For accepted candidates, actual approximate-distance helper`0x405cf0` uses candidate sprite coordinates and the requested center. It maintains the smallest distance, starting at9999999. Replacement requires a strictly smaller distance, so ties retain the **first** accepted unit in player-list order. Five two-candidate cases distinguish nearer-second, equal-distance order, axis distance and signed16 positions. This is a linear unit-list selection, distinct from the spatial search routines used by [placement](native-ai-placement.md).

## Actual AI removal for this path

`0x48dbc0(ECX=unit, EDX=was_killed)` calls these original helpers in order:

1. `0x4b0770`: remove AI region references and, for type4, release its military record.
2. `0x486af0(unit,0)`: worker/building removal helper.
3. `0x4b26f0`: guard parent/death update.
4. `0x483220`: clear/replace free-Medic reference when the current type is34.
5. Write unit+`0x134=0`.

The guard scheduler passes `was_killed=0`. Its selected military unit has ordinary mobile-unit properties, so the actual worker/building and guard helpers take their reviewed nonapplicable returns. The probe executes those original returns rather than substituting a successful removal response. The [worker report](native-workers.md) owns their broader town/pool branches.

`0x4b0770` visits all eight players. For player type1, it reads signed16 map region count through`0x481b00` and clears matching unit pointers at region offsets+`0x1c`,+`0x20`,+`0x24`,+`0x28` in region indices **1 through count-1**. It then uses actual hostility helper`0x429110`; a hostile candidate lowers that player's current-position region's required air/ground strength by the **per-type** dword values at`0x6ac530`/`0x6ac8c0`, saturating at0. These are not the removed unit's health-scaled cached strengths. Three complete cases observe pointer clearing and smaller/equal/larger strength subtraction. The own-player fixture is allied and does not lower its requirements.

For type4 military AI, this selected `was_killed=0` path calls military release`0x4a9a30(region,node)`. If unit status bit4 is clear, it decrements region u16 ground-unit count+`0x0a` with wrapping arithmetic; count0 becomes65535. It unlinks the node from region head+`0x30`, repairs reciprocal links and prepends it to the free head at military_pool+`0x4e20`, using region+`0x2c` as that pool pointer. It does not zero the released record's parent/region payload.

Nine actual-removal cases distinguish military type4, guard type1 and another type, as well as count underflow and status bit4. Type1 takes the actual guard detach helper, clears the guard parent and increments death99→100, then the enclosing remover clears the unit AI pointer. The selected military path leaves cached region strength unchanged until its independent update; only membership/count and the separately described hostile requirement adjustments occur here.

Other removal branches are outside these79 `was_killed=0` checks. Their applicable fixtures now execute actual callees in [generic AI removal and town migration](native-ai-removal.md): state8 roster removal, killed-unit cross-region transitions, Medic replacement, worker/building release, town destruction and worker/Overlord migration. Full recovery of surrounding destruction callers' list ordering is still distinct from these complete selected function paths.

## Complete scheduler-to-order adoption

For requested guard IDs37 Zergling or47 Scourge, the [periodic guard scheduler](native-guard-lifecycle.md) calls this selector after its retry and destination-region gates. With a selected unit it:

1. Runs actual`0x48dbc0(unit,0)`, releasing the previous military membership.
2. Copies guard `other_home` into home and stores guard.parent=unit.
3. Writes unit.AI=guard.
4. Issues ComputerAI order`0x9c` through actual`0x42a2c0` and skips the spending-request branch.

The guard record remains in the same native player guard list, now with a nonnull parent. The successful adoption branch does **not** update timestamp+`0x1c`; fixture timestamp77 survives. It neither adds nor pops a spending request.

Eight complete cases cover both requested IDs, status bit4 and an empty/four-slot real order pool. With available order records, the original allocator/list/activation/free chain activates ComputerAI and leaves allocated-order count0 afterward. With an exhausted pool, **AI adoption still completes but the current primary order remains1**. The routine returns normally and no spending request is emitted. A normal issuer or scheduler return therefore cannot be treated as proof that the requested primary order became active.

An additional complete engaged-unit case confirms the selector can choose a unit already holding a target. Successful order activation clears primary target+`0x5c`, retains auto-target+`0x7c` because the new guard AI pointer is present, and leaves source region cached all-ground strength unchanged. This composes the native AI transfer with the separately recovered [primary-order contract](native-primary-orders.md), rather than inferring those effects from an issuance wrapper.

Pinned [Teippi guard rebuild code](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L390) corroborates the intended dependency structure: nearest military selection, previous AI removal, guard attachment and ComputerAI issue. Its list migration and other changed guard policies are not the oracle for the local binary's mutations or timing.

## Reproduction and limits

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_guard_transfer_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-guard-transfer-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-guard-transfer-probes.json
```

Default mode only reads/disassembles the hash-gated EXE. Explicit probes also require unchanged effective orders.dat SHA-256`0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. They use its189 native interruptible/highlight/subunit-inheritance entries and private real order-list records. Main images have animation0, the fully reviewed idle path; active iscript animation and UI notification paths are not substituted or executed.

JSON records target/data/script/dependency hashes, runtime versions/architecture, VA allowlist, instruction counts and actual call traces. All79 completed fixture paths matched. The separate generic-removal batch closes its declared killed-unit/state8/town dependencies. Remaining work includes surrounding AI membership producers/consumers, periodic cache maintenance and subsequent ComputerAI/guard gameplay progression. This establishes the declared original-function contracts, not a full running-game or mission-parity trace.
