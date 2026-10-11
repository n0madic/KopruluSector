# Native 1.17 town requests and worker allocation

This report follows concrete native helpers from the local StarCraft.exe 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. [Static witnesses](evidence/native-economy-frame-static.json), [isolated native-function probes](evidence/native-leaf-probes.json), [probe source](../../tools/research/probe_native_leaf_117.py).

The isolated probes execute original instructions in private Unicorn x86 memory. They do not launch WinMain, the game, Windows APIs, or a campaign. Their result proves the selected leaf behavior for the recorded inputs; it does not prove the complete economic loop.

## Town request layout and insertion

Native town allocation clears 400 bytes at `town+0x3c`. Complete shared pool initialization, genuine owner/area creation and empty deletion/reuse are now executed in the [Town lifecycle follow-up](native-towns.md). The index-to-pointer helper uses stride `0x1cc`, consistent with 100 four-byte request slots. Each slot contains:

| Offset within slot | Width | Native encoding |
|---|---|---|
| +0 | u8 | `(count << 3) | ((kind & 3) << 1) | (special & 1)`, truncated to a byte |
| +1 | u8 | Priority |
| +2 | u16 LE | Unit, upgrade, or technology ID |

The general insertion helper is `0x485fa0`. It returns without writing unless the town owner's player type is **1**, read from `0x6726d0 + player*36`. It scans from the first slot for `(flags_and_count & 0xf8)==0` and writes there. It does not sort by priority, merge duplicate IDs, or replace a previous desired count. Those behaviors cannot be inferred from the opcode name `build`.

Callers establish request kinds:

- Build wrapper `0x487550`: kind 0, caller-supplied count/ID/priority/special. Rejects unsigned count>30 and ID>=228.
- Technology wrapper `0x487890`: kind 2, count 1, ID in EDX and priority on the stack, special 0.
- Upgrade wrapper `0x4878b0`: kind 1, count in EDX, ID and priority on the stack, special 0.
- Native build opcode passes special 0. Its neighboring request opcode passes special 1 and priority 80; its semantic scope follows the native consumer, rather than a guessed flag name.

These fields agree with the independently recovered AISE `TownReq` layout, although AISE's modified algorithms do not establish native insertion policy. [AISE structures](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L744).

### Nominal capacity edge

The native bound check occurs **after testing the current slot's count bits**. After 100 occupied slots it reads slot index 100, which is at `town+0x1cc`, beyond the nominal request array. If that byte has nonzero count bits, the helper returns; if those bits are zero, it writes a request at index 100. This is the observed machine-code boundary behavior, not a safe array-insertion specification.

Two isolated execution cases confirm both branches using emulated adjacent bytes. A remake must determine whether authentic campaign states can reach this edge before deciding what behavioral compatibility is necessary. Pretending the helper always safely rejects the 101st request would contradict the original instructions. Count=0 is also accepted by the wrapper, but leaves the slot's occupancy bits zero.

## Demand-to-worker percentage rule

Helper `0x483030` reads PlayerAi's total mineral/gas demand from the first two dwords of `0x67d068 + player*0x4e8`. Current resources are separate arrays: minerals `0x5263d0 + player*4`, gas `0x526400 + player*4`. The native `resources_jump` handler independently uses those arrays with the corresponding operands.

It produces mineral and gas percentages in output bytes. For ordinary non-overflowing campaign values:

```text
if gas_need == 0:
    mineral_percent = 100; gas_percent = 0
else if mineral_need == 0:
    mineral_percent = 0; gas_percent = 100
else:
    mineral_deficit = signed32(mineral_need - current_minerals)
    gas_deficit = signed32(gas_need - current_gas)
    if mineral_deficit > 0 and gas_deficit <= 0:
        mineral_percent = 100; gas_percent = 0
    else if gas_deficit > 0 and mineral_deficit <= 0:
        mineral_percent = 0; gas_percent = 100
    else:
        gas_percent = floor(gas_need * 100 / (mineral_need + gas_need))
        mineral_percent = 100 - gas_percent
```

The final ratio uses **total needs**, not positive deficits. With needs 300 minerals/100 gas it returns 75/25 both at stocks 0/0 and at 299/99. Satisfied demand for only one resource can force 100% allocation to the other. When both needs are zero, the first branch chooses minerals.

The exact instruction rule also includes uint32 truncation of multiplication/addition, signed deficit tests, unsigned division, and byte truncation of the outputs. The [probe model](../../tools/research/probe_native_leaf_117.py) keeps those operations explicit; it does not replace them with floating-point arithmetic or clamp negative deficits. Twelve recorded native cases cover the main branches, equality, ratio rounding, and signed wrap. The probe set does not establish that pathological overflowing demand totals are reachable in a game.

## Gas-resource classification

`0x485e80(player, unit)` returns zero for a null unit, a unit rejected by native helper `0x41fe40`, or a type that does not match the player's gas building. It returns:

- 1 for raw Vespene Geyser unit ID 188.
- 2 for the matching racial gas building with resource word `unit+0xd0 == 0`.
- 3 for that building with a nonzero resource word.

The race test chooses IDs 110, 149, and 157, matching Refinery, Extractor, and Assimilator. `0x485de0` scans the town's three gas references at +0x30/+0x34/+0x38 and returns the requested one-based occurrence of a requested classification. Missing occurrences return null. This is a scan in slot order, rather than a nearest-resource search.

## Town worker allocation

`0x485020` returns immediately for an uninitialized town (`town+0x19 == 0`). It establishes resource-area membership, refreshes gas references when byte +0x1d requests it, fills a missing mineral reference, and updates the worker limit. These operations use native resource-area helpers; their search/geometry is a distinct dependency.

It then obtains the gas percentage above and computes a candidate gas-worker count:

```text
gas_workers = floor(town.worker_count * gas_percent / 100)
gas_capacity = 4 * resource_area.gas_count
```

When candidate gas_workers exceeds capacity, it caps the count and skips the following stock-based minimum branch. Otherwise, while the player's current gas is below 800, it raises the count to at least 1/2/3/4 when the player's completed racial worker count reaches 5/10/15/20. That worker count is read through `0x4143e0` from `0x52c0c4 + 4*(unit_id*12+player)`; it is a player-wide count, rather than the current town's count.

Worker assignment codes at WorkerAi+9 are **different from gas classification codes**. Code 1 selects the mineral path; 2/3/4 select the first/second/third class-3 nonempty gas occurrence. The allocator starts its gas assignment at code 2 when it finds either a nonempty or depleted racial gas building, otherwise at code 1. A town without a mineral reference can change its fallback assignment to code 2. It walks the town worker list, assigns the first quota to gas, and rotates the non-mineral assignment codes 2→3→4→2 after every four non-mineral workers. The actual worker decision helper falls back from a missing requested nonempty occurrence to the first other nonempty occurrence, then the first depleted occurrence. [Native worker decisions and original probes](native-workers.md) establish this consumer; treating assignment code 3 as the class-3 classifier result would incorrectly choose the second gas object.

The same pass records last-update time at WorkerAi+0xc. Workers in orders 0x21/0x60/0x1a or with status bit 0x40 refresh that timestamp. Otherwise, elapsed difference>=60 triggers order helper `0x42a2c0` with order 0x9c, sets wait byte +0xb to 10, increments reassign byte +0xa, refreshes time, and calls `0x486480` on the worker's town. Interpreting the order's complete effect still requires its handler.

The worker-allocation body is established by static data/control flow. The native execution batch covers its percentage leaf and build insertion, **not this whole allocator**. The allocator's resource-area and reassignment callees remain separate recovery targets.

## Native spending heap

`0x482040` inserts eight-byte entries at `PlayerAi+0x18`: priority u8, kind u8, ID u16, context pointer u32. The count byte is at +0x210. This is a max-heap by **unsigned priority**, rather than FIFO order or a stable sort.

For count<63 it appends at the old count, increments count, and bubbles upward while the child's priority is strictly greater than its parent's. Equal priority does not swap upward. For count==63, however, it **replaces entry 62** and bubbles it upward; it does not discard the new request. AISE's heap-push wrapper returns when its array is full, so copying that wrapper would change this original behavior. [AISE queue implementation](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L137).

`0x482310` pops the root by decrementing count, moving the previous final entry to index 0, and sifting down. When two children have equal priorities, it chooses the **right child**. It stops when the parent's priority is >= the selected child's. A one-entry pop sets count to zero; the stale underlying bytes remain. An empty pop is not an established legal caller state and must not be generalized from this routine.

The probe includes ascending priorities, repeated equal priorities, low/high replacement of the 63rd entry, and complete draining sequences. It compares the entire logical heap after each original push/pop, preserving IDs and context rather than checking the highest priority alone.

### Unit-limit predicate

The push dispatcher applies native unit-limit helper `0x4835e0` to request kinds **1, 2, 4, and 8**, based on the local switch mapping. It reads `PlayerAi+0x3f0+unit_id`:

- 0 permits the request without counting units.
- 255 rejects it without counting units.
- Other values reject when the current all-unit count is >= the limit.

The all-unit count table is `0x529604 + 4*(unit_id*12+player)`. For requested **Tank ID 5** it also adds **Siege Tank ID 30**. Requested ID 30 does not symmetrically add ID 5 in this helper. The VM wait-count helper merges the two in both directions; these are different algorithms. Eight native execution cases confirm limit zero/255, below/equal threshold, the asymmetric Tank branch, and a request kind that bypasses the limit.

### Request costs and type mapping

Town request conversion at `0x482b60` maps kind 1 to spending kind 5 (upgrade), kind 2 to 6 (technology), and ordinary unit requests by DAT property bits: worker bit 0x8 maps to 4; otherwise `(properties & 2)*2 | 3` maps to 3 or 7. Property bit 0x2 is the addon property in the reference schema, not the building property.

It accumulates requested mineral/gas/supply cost **before** pushing the queue entry. Cost helper `0x4087c0` reads unit mineral/gas u16 arrays at `0x523520`/`0x5236e8` and supply byte at `0x523e10`; property bit 0x400 doubles supply for pair-birth production. Upgrade and technology costs enter through separate helpers. Native accumulation uses uint32 addition rather than AISE's saturating additions. Queue-full replacement and unit-limit rejection therefore cannot be understood independently of demand accounting and the caller's regeneration of requests.

## Connection to the spending loop

Slow AI maintenance visits player/town slots at native intervals documented in [frame cadence](native-frame.md). The town-request consumer at `0x4865b0` decodes the same three-bit count and two-bit kind, then counts worker/building lists or global unit counts depending on the special bit. Persistent desired-town requests and the player's transient spending queue are distinct structures.

The current batch contains **445 isolated original-function cases**. This closes request encoding/insertion, nominal-boundary behavior, demand percentages, gas classification, the allocator's top-level quota/rotation logic, heap insertion/removal/full behavior, and the unit-limit predicate. It does not yet close every request satisfaction equivalence, prerequisite generation, building placement, builder selection, exact resource-area selection, or failed-production transition. Those need native consumers and unit orders, rather than additional parsing of `aiscript.bin`.
