# Native 1.17 production requests, satisfaction and costs

This report recovers the town request consumer and its actual cost, queue-counting and cancellation callees from the reviewed local StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. The [static witnesses](evidence/native-production-static.json), [168 isolated native probes](evidence/native-production-probes.json), and [reproducible inspector/probe](../../tools/research/inspect_native_production_117.py) record the evidence.

The probes execute original x86 instructions in private Unicorn memory, including the actual reviewed callees. They use **no helper stubs**. No full game, WinMain, Windows API, campaign or host network/file operation is executed by the emulator. Fixtures initialize DAT scalars, unit lists, AI records and queues; they do not prove those particular states occur in a campaign. Complete leaf calls and a declared consumer prefix are distinguished in each result. Production order execution and prerequisite evaluation remain separate dependencies.

## Persistent requests and transient spending entries

The town consumer `0x4865b0(ECX=player)` walks the town list at `0x69b51c + player*8`, following `town+0`. Each town has 100 four-byte persistent desired-count records at `town+0x3c`: flags/count byte, priority byte, ID word. Count is `flags >> 3`; kind is `(flags >> 1) & 3`; bit 0 selects a special count scope for unit requests. [Request insertion and nominal-boundary behavior](native-economy.md) are independently recovered.

For each town, the consumer first services its building queues, then scans persistent requests in array order. The **first record with `(flags & 0xf8)==0` ends that town's scan**. Later occupied records are not examined. A kind-0 request is skipped while `town+0x1e != 0`; upgrade/technology kinds bypass that battle gate.

For each positive deficit, the consumer emits one transient spending request per missing item. It does not remove, decrement or rewrite the persistent record. Nor does this function itself reset accumulated demand or clear the previous transient heap: two consecutive isolated calls regenerate the same missing request and add its cost twice. The surrounding maintenance sequence must perform the relevant reset at its own cadence; this prefix is not the entire economic loop. [Frame scheduling](native-frame.md) and [spending heap](native-economy.md) are separate contracts.

The converter `0x482b60` takes `ECX=player`, `EDX=town`, and stack arguments `[town_kind, ID, priority]`:

| Town kind / unit properties | Spending kind | Context |
|---|---|---|
| Kind 0, DAT properties bit `0x8` set | 4 | Town pointer |
| Kind 0, worker bit clear and bit `0x2` clear | 3 | Town pointer |
| Kind 0, worker bit clear and bit `0x2` set | 7 | Town pointer |
| Kind 1 | 5 | Town pointer |
| Kind 2 or 3 | 6 | Town pointer |

The bit names worker/addon are corroborated by reference schemas, but the native bit tests establish the behavior. The worker test has precedence over the addon test. The converter calls demand accumulation `0x4836d0` **before** heap insertion `0x482040`. Consequently a worker request rejected by the native unit-limit predicate can still increase mineral/gas/supply demand. One isolated case sets limit 255 and confirms nonzero demand with no heap entry. Full-heap replacement behavior is proved in the economy report, not redefined by this converter.

The fields agree with AISE's `AiTown`, `TownReq`, `PlayerAiData`, and `AiSpendingRequest` layouts. AISE is a modified AI implementation; its algorithms are corroboration of names/layout, not the native behavioral oracle. [AISE structures, pinned revision](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L751).

## What satisfies a unit request

For kind 0 with special bit 1, `0x4143c0` reads the raw player-wide all-unit count at `0x529604 + 4*(ID*12+player)`. It does not examine town membership or merge Tank modes. A native probe distinguishes this `player_need` scope from ordinary town counting.

With special bit 0, the consumer walks worker AI nodes from `town+0xc` and building AI nodes from `town+0x14`, following node+0; node+0x10 is the parent unit. An exact unit-type match contributes one without a completion/status test in this consumer. Otherwise the queued-production helper below contributes its result. The building list additionally satisfies these morph chains:

| Requested ID | Additional accepted IDs |
|---|---|
| 131 Hatchery | 132 Lair, 133 Hive |
| 132 Lair | 133 Hive |
| 141 Spire | 137 Greater Spire |
| 143 Creep Colony | 146 Sunken Colony, 144 Spore Colony |

Each additional ID is counted as an exact current type or through queued-production counting. These are asymmetric substitutions: a Hatchery does not satisfy a requested Lair. The native switch targets at `0x4869f0` and byte index table at `0x486a04` are retained as data in the static evidence. Nine isolated consumer cases cover each positive substitution, exact matches and the negative Hatchery→Lair direction.

The racial worker helper `0x486070` reads race byte `0x6726d1 + player*36`: race 1 returns SCV ID 7; race 0 returns Drone ID 41; every other value returns Probe ID 64. If a request's ID equals that result, its desired count is clamped to `town+0x1b`, the worker-limit byte. The existing worker contributes one, so desired six with limit four and one worker emits three requests. The caller performs this ID comparison for every kind; interpretation as a worker-only rule assumes normal ID/kind inputs.

### Exact queued-production helper

`0x41bde0(ECX=unit, stack=[desired_ID])` distinguishes workers, eggs/morphing buildings and ordinary producers. The build queue consists of five unit-ID words at `unit+0x98`, with head byte `unit+0xa4`; ID 228 is empty.

For ordinary nonworkers, it counts matches in all five circular positions starting at the head modulo five. It can return 0–5. For types 36/59/97, or primary orders `0x2b`/`0x2d`, it compares only the raw head-index word and returns 0/1. That special branch does not apply a modulo operation to a malformed head byte; ordinary queues do.

For workers, DAT property bit `0x8` selects a construction-specific branch:

1. If primary order is `0x1e`, `0x22`, `0x21`, or predicate `0x42f240` succeeds, select the existing target pointer at +0x5c. Otherwise, if secondary order is `0x26`, `0x3f`, or predicate `0x435990` succeeds, select the construction pointer at +0xec.
2. A nonnull selected pointer returns **zero**, preventing the queue from counting that already-instantiated target here.
3. With no selected existing target, compare only the queue head modulo five. It contributes one only for primary orders `0x1e`, `0x1f`, `0x46`, `0x1a`, or `0x19`.

The two predicates are also recovered and executed. `0x42f240` requires primary order `0x1f`, status bit `0x2` at +0xdc, nonnull +0x5c, and the target's status bit `0x1` clear. `0x435990` requires secondary order `0x25`, the same own-status bit, nonnull +0xec, and the pointed unit's status bit `0x1` clear. No additional distance, ownership or completion tests are present in these predicates.

Order names and IDs can be cross-checked against Teippi's recovered constants, but the numeric comparisons are local-1.17 evidence. [Teippi order constants, pinned revision](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h). The probe covers all 25 ordinary head/match combinations, five matches at once, special head-only branches, accepted/rejected worker orders and existing-target/status exclusions.

## Upgrade and technology satisfaction

Kind 1 starts with the current upgrade level from `0x49a470`, then scans the player's unit chain at `0x665b08 + player*4`, following `unit+0x6c`. Each eligible unit with primary order `0x4c` and current-upgrade byte `unit+0xc9 == ID` adds one. Eligibility here is the native `0x41fe40` predicate: a null sprite pointer at +0xc or primary order zero rejects the unit. It does not require town membership. Each matching active upgrade contributes one; it is not merely a boolean “an upgrade is in progress.”

Kind 2 or 3 calls `0x4863b0(town, technology_ID)`. It returns one when researched-state byte from `0x49a420` is nonzero, or when any eligible player unit has primary order `0x4b` and technology byte `unit+0xc8 == ID`; otherwise zero. Research in progress therefore satisfies the desired technology request before completion. Current researched/level arrays split at technology ID 24 and upgrade ID 46:

| State | Native address |
|---|---|
| Technology ID<24 | `0x534224 + player*24 + ID` |
| Technology ID>=24 | `0x536408 + player*20 + ID` |
| Upgrade ID<46 | `0x534590 + player*46 + ID` |
| Upgrade ID>=46 | `0x5365de + player*15 + ID` |

The higher-ID addresses are adjusted bases, so their formulas retain the original ID rather than subtracting the split. Cost probes cover both sides of the upgrade boundary; satisfaction probes distinguish sprite absence, order zero, wrong ID and the active Upgrade/Research orders.

## Priority regeneration

At entry, the consumer sets PlayerAi flag `0x400` through `0x483650(player,1)`. In campaign mode, every missing kind-1 or kind-2 item also clears that flag and adjusts priority using one pass-local decrement counter, initially zero. If decrement<=authored priority, emit `priority-decrement` and increment decrement. Otherwise emit zero and leave decrement unchanged. This counter spans requests and towns in the **same player pass**; it is not a per-upgrade or per-town counter.

For example, one desired upgrade count three followed by one technology request, all priority 100, emits 100/99/98/97. Four missing priority-zero upgrade items advance the counter only once; a following priority-100 technology emits 99. Kind 3 uses the technology satisfaction/cost path but bypasses this campaign priority adjustment and leaves flag `0x400` set. Native probes record all three distinctions, including a two-town chain.

Outside campaign mode, a kind-0 request for the racial main building uses priority **150** when signed current mineral stock is below 600. Main IDs are 106 for race 1, 131 for race 0, 154 otherwise. This replaces even an authored priority 255; it is not a minimum-priority clamp. Stocks 599/600 and a signed-negative dword distinguish the boundary. Campaign mode bypasses this replacement.

## Cost accounting and widths

`0x4836d0(ECX=player, EDX=spending_kind, stack=[ID])` reads costs and adds them to PlayerAi's first three dwords. These are demand totals, not immediate deductions from current stocks:

| Spending kind | Cost source |
|---|---|
| 5 | Upgrade wrapper `0x408820` |
| 6 | Technology helper `0x408790` |
| Other | Unit helper `0x4087c0` |

Unit minerals/gas are u16 at `0x523520 + ID*2` and `0x5236e8 + ID*2`. Supply is a byte at `0x523e10 + ID`. Property bit `0x400` doubles **only the supply byte**, with byte wrap; mineral/gas costs are not doubled. Thus synthetic supply 128 doubles to zero in this leaf. That is a width probe, not a claim about authentic unit data. Technology mineral/gas u16 arrays are `0x51f760` and `0x51f7b8`.

Upgrade price helpers `0x43b2a0` and `0x43b270` calculate base + **current researched level** × factor. Mineral base/factor u16 arrays are `0x5248d0`/`0x524950`; gas base/factor are `0x5249d0`/`0x524a50`. The arithmetic helper returns a full 32-bit result, but its caller `0x408820` explicitly retains only AX before storing each price. Demand therefore receives `(base + current_level*factor) mod 65536`. Upgrade/technology supply is zero. All demand additions wrap modulo 2³²; they do not saturate.

AISE's modern cost functions use wider/saturating arithmetic and an explicit next-level argument; they are useful comparison material but cannot replace these native widths. [AISE cost helpers](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L600).

## Queue cancellation before satisfaction

Before counting requests, the consumer traverses the town's building AI list. For each building whose owner byte matches the town owner, it calls `0x41b810` with relative queue indices **1, then 2, then 3, then 4**. Each call sees the queue modified by previous calls.

`0x41b810(unit, relative_index)` returns for index>4 or an empty selected slot. Physical index is `(head+relative_index)%5`. When the removed ID lacks DAT building bit `0x1`, `0x408460` refunds its **full u16 unit mineral/gas cost** into current-stock arrays `0x5263d0`/`0x526400`, wrapping u32. The building-bit path omits that refund. Cancellation at relative index zero with a nonnull current-build object takes a separate destruction helper `0x41a340`; the town consumer never requests that index and this batch does not recover that helper. The subsequent [populated Terran construction report](native-construction.md) executes complete selected `41a340` cancellation/refund/Death/recycling paths on an actual incomplete Supply Depot; other morph/building variants remain separate.

After removal, the function shifts queue entries towards the gap, stopping at an empty slot or the remaining queue horizon, and sets the final word to ID 228. Each shift calls actual `0x484f90`: if unit+0x134 points to an AI node whose type byte +8 is 3, copy the source training tag byte at AI+9+slot and context dword at AI+0x18+slot*4 to the destination. The newly empty slot's AI metadata is not cleared by this helper.

For a full relative queue `[A,B,C,D,E]`, cancel index 1 produces `[A,C,D,E,empty]`; cancel index 2 then removes D, producing `[A,C,E,empty,empty]`; indices 3 and 4 are already empty. The original A/C/E survive. **This is observed native behavior, not an assumption about intended cleanup.** With five identical queued items and desired count five, the consumer therefore counts three survivors and regenerates two missing requests. One prefix probe records the four native cancellation calls, surviving queue, two full refunds and regenerated spending entries together. The independent circular-slot probes verify queue/AI metadata at every head position.

## Reproduction and remaining execution boundary

Default static inspection requires Python with pefile and Capstone. It rejects any EXE with a different hash before decoding. Native execution requires the validated x86_64 Rosetta environment because the arm64 Unicorn runtime crashed during a separate NOP-only environment check:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_production_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-production-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-production-probes.json
```

The result contains the probe-script SHA-256, exact EXE hash, architecture and Python/Unicorn/pefile/Capstone versions. All **168 cases matched** their explicit contracts. The bounded allowlist rejects execution outside reviewed code. Consumer cases start at `0x4865b0` and stop before `0x48686d`, after request processing and before the last-town tail. Their fixtures avoid worker redistribution and worker-maintenance calls; those are not replaced with successful-return stubs.

The prerequisites are now independently recovered in [unit-production requirements](native-requirements.md) and [general order/technology/upgrade requirements](native-dat-requirements.md). Worker redistribution and construction service have their own [native worker report](native-workers.md). The remaining boundaries of this batch are town worker maintenance `0x485ab0`, last-town helpers `0x485970`/`0x4ae220`/`0x4b1420`, and the spending consumers that select a producer, debit resources and issue native build/train/upgrade/research orders. Teippi's reconstructed training consumer explicitly separates requirement tests, resource tests, limits, morph parents and request-pop effects; this is a guide to the dependencies to recover, not proof that its modified code matches local 1.17. [Teippi spending/training implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L1013).

This batch closes the selected request-to-demand/heap conversion, satisfaction, priority and cancellation rules. It does not establish complete economic behavior, producer selection, production-order state machines or campaign parity.
