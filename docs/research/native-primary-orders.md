# Native 1.17 primary-order issuance and queue activation

This report closes the original primary-order chain that worker, military, training and research decisions call after selecting an action. The target is the locally supplied StarCraft executable **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. The recovery uses local instruction witnesses and isolated execution of the original x86 functions with real private order pools. No retail game process or guest Windows API was executed.

The evidence is [native-primary-orders-static.json](evidence/native-primary-orders-static.json) and [native-primary-orders-probes.json](evidence/native-primary-orders-probes.json). The static report contains **28 blocks and 865 instructions**, with no undecoded tails, plus the idle-animation dispatch table and all 19 native orders.dat field descriptors. The probe batch has **535 passing cases: 524 complete returns and 11 explicit prefixes**, with no replacement helper stubs. These include 189 cases using the actual effective retail order flags and a separate 189-ID synthetic table sweep. Unit/image records, a private TBL and the doubly linked 20-byte order pool remain artificial fixtures; stock order properties are kept distinct from those fixture inputs.

The [extractor](../../tools/research/inspect_native_primary_orders_117.py) and [probe](../../tools/research/probe_native_primary_orders_117.py) enforce both the executable hash and the effective orders.dat hash. Their own hashes, the extractor dependency hash, engine version, input/output state, original call arguments, pool pointers and queue pointers are recorded in the evidence. Both tools require `--orders-dat` as well as `--exe`. The engine is Unicorn 2.1.4 with an x86-32 guest under local x86_64 Python. A completed isolated call establishes the recorded branch, not movement, attack execution, production completion or campaign parity.

## Entry contracts and native structures

Addresses are preferred VAs for this exact 1.17 image; subtract `0x400000` for the RVA. The local contracts are recovered from instruction use and original callers rather than imported 1.16.1 hook addresses.

| Entry | Local VA | ECX | Stack arguments | Return |
|---|---:|---|---|---|
| Issue without target | `0x42a2c0` | Unit | Order ID | `ret 4` |
| Issue position | `0x42a340` | Unit | Order ID, X, Y | `ret 0xc` |
| Issue FoW unit type/position | `0x42a300` | Unit | Order ID, FoW unit ID, X, Y | `ret 0x10` |
| Issue unit target | `0x42a380` | Unit | Order ID, target pointer | `ret 8` |
| Issue packed position/unit | `0x42a3e0` | Unit | Order ID, packed position, target pointer | `ret 0xc` |
| Append with tail pruning | `0x429f60` | Unit | Order ID, packed position, target, FoW unit ID, clear-others | `ret 0x14` |
| Insert after an order | `0x4295d0` | Unit | Order ID, packed position, target, FoW unit ID, anchor or null | `ret 0x14` |
| Insert before an order | `0x429e10` | Unit | Order ID, packed position, target, FoW unit ID, anchor | `ret 0x14` |
| Activate next order | `0x42a610` | Unit | None | `ret` |
| End/advance order | `0x42a5a0` | Unit | None | `ret` |
| Clear entire queue | `0x429ac0` | Unit | None | `ret` |
| Allocate order | `0x413c80` | CL = order ID | Packed position, target; DX = FoW unit ID | EAX = node or zero; `ret 8` |
| Free order | `0x413d50` | Order node | None | `ret` |

Packed position is `uint16(X) | (uint16(Y)<<16)`. Coordinate wrappers truncate their arguments to 16 bits; negative inputs preserve their two's-complement bit pattern. Unit-target issuance samples the target sprite position. At activation, a nonnull target **resamples its current sprite position**, rather than using a previously queued coordinate. The packed-position/unit wrapper therefore has two different outcomes depending on whether its target pointer is null.

An order node is 20 bytes:

| Offset | Width | Meaning |
|---:|---:|---|
| `+0` | 4 | Previous node |
| `+4` | 4 | Next node |
| `+8` | 1 | Order ID |
| `+0xa` | 2 | FoW unit ID, with 228 meaning None |
| `+0xc` | 4 | Packed position |
| `+0x10` | 4 | Unit target pointer |

The native pool's free head is `0x5aef00`, free tail `0x5aef04`, and allocated count `0x5aef08`. A unit's queue head/tail are `+0x74/+0x78`. Highlighted pending-order count is byte `+0x84`; this is not the number of queue nodes.

## Allocation, list operations and failure

The real allocator `0x413c80` reads the free head. An empty head returns zero. Otherwise it writes the order ID, FoW type, position and target into that node, removes it from the free list using original `0x4119a0`, increments the allocated count and returns the node. CL and DX establish the byte/word truncation of the payload. Complete leaf probes cover empty, one-node and 16-node pools, oversized register inputs and round-trip freeing.

Freeing `0x413d50` decrements the count and inserts the node **after the existing free-list head** through `0x411930`. An empty list instead makes the freed node both head and tail. For an initial pool `[P0, P1, P2, ...]`, allocating and freeing `P0` yields `[P1, P0, P2, ...]`; the next allocation returns `P1`. Complete probes assert this exact chain and the next allocated pointer. Freeing does not destroy or clear the payload fields. A valid caller must pass a detached allocated node; the helper does not independently enforce ownership or protect the count from invalid free calls. The probes only use valid allocated nodes.

The list helpers execute real previous/next/head/tail updates. Queue append/insertion calls use them directly. Each recorded queue/pool inspection checks both link directions, absence of cycles and exact tail consistency. Complete middle-insertion cases distinguish insertion before from insertion after an existing node.

An allocation failure in an order insertion requests stat_txt string index `0x368` via `0x4a7d70 → 0x4c13b0`, then invokes notification gate `0x46ae50` for the unit owner. Complete failure fixtures use a real artificial TBL and a unit owned by player 3 while local player is 0, so the original notification gate returns without UI/import calls. The local-owner failure fixture reaches the actual notification-effect branch at `0x46ae70` and stops there. That branch contains text/UI/time calls; no import is replaced or executed.

Allocation failure does not roll back earlier tail removals, restore a pruned queue or clear the issuer's user-action flag. The issuer proceeds to the activation function regardless of allocation success. A one-node pool containing an older noninterruptible queued order demonstrates this: allocating the new requested order fails, then activation succeeds for the **older head** and returns its node to the pool. The final active order is the older order, and no node for the requested order exists.

These entries are procedures, not a stable boolean-success API. Higher-level AI consumers that ignore their return cannot infer from the call alone that the requested order became active.

## Append, interruption and tail pruning

`0x429f60` first returns when the current main order is Death 0. Its wrappers have already set user-action flag bit 1, so this early return does not mean that the whole wrapper is a state-preserving no-op.

For an ordinary current order, it examines the queue **from the tail**. A tail node is removed when either:

- `clearOthers!=0` and its interruptibility byte at `0x51b200 + tail.order` is nonzero; or
- its order ID equals the new requested ID, including when `clearOthers==0`.

After each removal it decrements the highlighted count if the order's 16-bit descriptor at `0x51b980 + order*2` differs from `0xffff`, detaches the node and frees it. It continues toward the head until the queue is empty or the next tail satisfies neither condition. It does not search past a retained tail to remove earlier eligible nodes.

The new ordinary order is then appended. Thus a noninterruptible older queue entry can survive ahead of it. Issuing a new action does not universally clear the whole queue.

The direct `0x4295d0` insertion entry does not perform this pruning. A null anchor means append after the existing tail, or create the queue when empty. A nonnull anchor inserts immediately after it. The before-entry `0x429e10` inserts immediately before its supplied anchor and updates the queue head if necessary.

Before-insertion has a distinct AI gate: when the unit has an AI pointer and its highlighted count is **greater than 8**, return without allocating or inserting. Count 8 is allowed; count 9 is rejected. The ordinary append-after entry lacks this gate. A complete fixture with twelve unhighlighted queued nodes and highlighted count zero successfully prepends a thirteenth node, proving that the gate concerns highlighted count rather than queue length.

Highlight increments wrap as bytes. Removal/activation decrements are followed by an unconditional check that converts byte value `0xff` to zero, even if the current order has no highlight descriptor and no decrement occurred. Complete round-trip cases distinguish this from saturation or an unrestricted integer count.

### Stock data binding

The effective retail asset is `BrooDat.mpq:arr\\orders.dat`, 4,158 bytes, SHA-256 `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`, with provenance in [retail-manifest.json](evidence/retail-manifest.json). It was read from the existing private campaign extraction; the asset itself is not copied into this report.

The native descriptor array at `0x501278` has nineteen 12-byte records containing destination, entry width and count. Every count is 189. Descriptor 4 binds SubunitInheritance byte `0x51b080`; descriptor 6 binds interruptibility byte `0x51b200`; descriptor 16 binds 16-bit highlight `0x51b980`. Their corresponding source file offsets are 945, 1323 and 3213, respectively. The fixed layout was checked against the pinned [PyMS OrdersDAT schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/OrdersDAT.py).

The evidence preserves all 189 decoded rows of these three factual fields. Additional complete calls load those real values into their original destination arrays and issue each ID through `0x42a2c0`, with the same artificial unit/main-image fixture. This verifies the stock flag binding and its effects on the recovered mechanism. It does not show that every order is a legal command for that fixture's unit type; requirements and command admission occur outside the issuer.

Examples of actual stock values are:

| Order ID | Interruptible | Highlight | Subunit inheritance |
|---:|---:|---:|---:|
| Death 0 | 0 | `0xffff` | 0 |
| Guard 2 | 1 | 229 | 1 |
| AttackUnit 10 | 1 | 230 | 0 |
| AttackMove 14 | 1 | 230 | 0 |
| Cloak 109 | 1 | 252 | 0 |
| Burrowed 117 | 0 | `0xffff` | 0 |
| Unburrow 118 | 0 | 260 | 0 |
| ComputerAI 156 | 1 | `0xffff` | 1 |

## Activation and state commitment

The ordinary issue wrappers set `unit+0x93 |= 1`, call append with `clearOthers=1`, then call `0x42a610`. Activation reads the **queue head**, which may be a retained older order rather than the freshly requested order.

Before ordinary activation, two special rules apply:

1. A burrowed AI unit may insert Unburrow first, as described below.
2. Status bits `0x1000` or `0x2000` block activation when the head's order is nonzero. **Death order 0 bypasses both bits.** An empty queue also returns without changing order/state/timers.

When activation is permitted, the native function:

1. Updates the highlighted count, detaches the head node and frees it.
2. Reads the node's retained order ID, FoW type, position and target payload.
3. Resets order-wait byte `+0x85`, clears user-action bit 1 at `+0x93`, and resets movement-target update timer `+0xe3`.
4. Clears status bits `0x8`, `0x1000` and `0x08000000`, then sets `0x1000` if the new order's interruptibility byte is zero.
5. Commits the main order ID at `+0x4d`, resets order state `+0x4e=0`, and writes target pointer/position and FoW type through the appropriate native setter.
6. Clears auto-target `+0x7c` only when the unit has no AI pointer.
7. Invokes the original animation/reset chain `0x420910 → 0x40ceb0 → 0x4176e0`.
8. Performs applicable subunit order inheritance.

The node is returned to the pool before its payload is copied into the unit; freeing preserves the payload, so this ordering is safe within the isolated sequential chain. A remake with reentrant callbacks or immediate node reuse must preserve the visible contract rather than invalidating that payload early.

The target setters are `0x4179e0` for FoW type/position, `0x417a10` for position with None type 228, and `0x417a50` for a unit target. A nonnull unit target supplies its current sprite coordinates and forces FoW type 228. Null-target setters clear the unit pointer. Main-order timer byte `+0x54` and secondary-order byte `+0x4f` are **not reset by this chain**; a complete preservation fixture verifies this distinction from order-wait `+0x85`.

Native status clearing is recorded by exact masks. Historical source names for bit `0x8` and `0x08000000` differ, so reproducing these masks is stronger evidence than selecting a label from an enum.

All 189 order IDs were issued through the no-target wrapper using explicit artificial interruptibility/highlight tables, then separately using actual stock interruption/highlight/inheritance flags. The synthetic sweep gives Cloak a noninterruptible fixture to distinguish the special saved-order branch; the retail sweep uses its actual interruptibility 1. The batch also executes coordinate/FoW wrappers, target wrappers, lock/release scenarios, queue clearing and allocation failure. These prove initialization and queue transitions; selected order handlers and command requirements remain separate contracts.

## Cloak saves and restores the previous action

Requested order **109 (Cloak)** takes a special append branch. After tail pruning, the native function places the current order, current target, target position and FoW type before the existing queue head, or appends that saved order when the queue was empty. It then prepends Cloak before the saved/current head, clears status bit `0x1000`, and calls the native end/advance entry `0x42a5a0`.

This internally attempts activation once. The outer issue wrapper then attempts activation **again**. Whether the saved previous order remains queued depends on Cloak's interruptibility and any iscript lock:

| Complete fixture | Result |
|---|---|
| Two or more free nodes; Cloak interruptibility 0 | Cloak becomes active and sets `0x1000`; previous order remains queued. |
| Two or more free nodes; Cloak interruptibility 1 | Internal activation starts Cloak; outer activation immediately restores the saved previous order. |
| One free node; no existing queue | Saving the previous order consumes the node; Cloak allocation fails; the previous order is activated again. |
| No free nodes | Neither order can be allocated; current order remains, with the pending user-action flag. |
| Iscript lock `0x2000` | The special branch clears `0x1000` but retains `0x2000`; Cloak and saved order wait in the queue. |
| AI pointer; highlighted count 9 | Before-insertion rejects Cloak, even though saving the current order through append may have succeeded. |

These are explicit DAT/pool fixtures, including discriminating synthetic interruption values. **The retail Cloak flag is 1**, so the stock-flags issuer probe with main-image animation zero completes by restoring the saved previous order; its pending highlighted count returns to zero. The noninterruptible fixture is a discriminator rather than a stock-data claim. This observation concerns this primary-issuer entry, not the complete player Cloak command or its secondary-order/effect path, which is outside the batch. The operation is not equivalent to simply replacing the current order and losing the previous action.

## Burrowed AI and empty-queue completion

At `0x42a610`, a unit with a nonnull AI pointer, nonempty queue and Burrowed status `0x10` ordinarily prepends **Unburrow 118** at its sprite position. It then clears `0x1000` and invokes order-done activation. The queued requested order remains behind Unburrow.

The injection is skipped for incoming orders **0, 117, 118 and 156**, for **Spider Mine unit ID 13**, and for **Lurker ID 103** when incoming order is **Guard 2 or AttackFixedRange 11**. Complete cases execute these combinations with the real before-insertion, allocator, list operations and recursive activation. The new order state is initialized; the later Unburrow animation/handler is not executed by issuance.

`0x42a5a0` advances an existing queue after setting the user-action bit. With an empty queue, a nonnull AI pointer selects **ComputerAI 156**; without AI, it selects the unit type's ReturnToIdle order from `0x520f20`. Those orders go through the same allocation/activation chain. A lock can leave that idle/AI order pending. Complete empty-queue cases verify both paths and the no-op behavior of calling plain activation on an empty queue.

## Subunit inheritance

After the parent order is committed and animation reset returns, inheritance requires that the parent lacks the Subunit units.dat flag `0x10`, the child pointer `+0x70` is nonnull, and the child type has that flag. The parent order is mapped as follows:

| Parent order relationship | Child order source |
|---|---|
| Matches parent's ReturnToIdle order | Child type's ReturnToIdle byte at `0x520f20` |
| Matches parent's AttackUnit order | Child type's AttackUnit byte at `0x521008` |
| Matches parent's AttackMove order | Child type's AttackMove byte at `0x5210f0` |
| Other order with inheritance byte `0x51b080+order != 0` | Same order ID |
| Other order without inheritance flag | No child issue |

A target pointer is propagated through `0x42a3e0`; without a target, the child receives the FoW type and queued position through `0x42a300`. Both are actual recursive primary-order calls with their own pools, queue and lock behavior. Complete fixtures use different parent/child idle/attack bytes to distinguish the tables and also verify a unit target's coordinates.

The pinned Teippi rewrite's AttackMove branch reads the **parent** type's AttackMove order. Native 1.17 reads the **child** type after loading its unit ID. The distinct-table fixture confirms this difference. Stock parent/child data may make the values equal, but the original lookup contract should be preserved. [Teippi order activation source](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L2954)

## Animation and remaining boundaries

Animation reset `0x420910` clears unit status `0x2000`, clears sprite flag `0x80`, establishes current-unit context globals `0x543300/0x543304`, calls the real idle transition and restores both globals. The flingy helper also clears bit 8 of byte `unit+0x20`.

The sprite helper uses the main image's current animation byte `+0x16`. Complete cases use animation 0, for which the original dispatch returns without starting another iscript animation. They execute the whole reset chain and verify status/flag/context effects; no callee is stubbed.

Ten active-animation prefixes stop before original image iscript entry `0x411730`, with the active order/state/wait/movement-update fields already committed. They verify requested idle animations: input 2/5 requests 8; input 3/6 requests 9; input 7 requests 12. The evidence records the actual image pointer and stack animation argument. Executing animation initialization and bytecode can change state again, so these prefixes are not full issuer returns for animated units.

The separate local-owner allocation-failure prefix stops before text/UI notification at `0x46ae70`. This boundary is independent of the primary queue mutation; its state and pool effects up to that point are recorded.

The remaining caller work is consequently concrete:

- Connect the remaining orders.dat fields and command-admission rules to their callers. The interruption/highlight/inheritance fields are already bound to native descriptors and verified in 189 stock-data calls.
- Execute the active-image iscript transition with valid retail script/image fixtures, including `nobrkcodeend` and any order-signal effects.
- Execute local order-pool exhaustion notification through its original UI/import environment only if that environment is separately authorized and available.
- Connect committed orders to their later primary-handler FSMs, movement/attack/worker actions and AI state. Issuing an order is not completing its action.

The initial source leads were GPTP's `CUnit::orderTo/orderComputerCL/orderToIdle` and Teippi's `IssueOrder/AppendOrder/DoNextQueuedOrder`. The local 1.17 instruction graph and pool probes now independently establish the entry contracts, interruption rules and field mutations. [GPTP source](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/structures/CUnit.cpp#L535)

This chain connects the issue boundaries in [native-workers.md](native-workers.md), [native-training.md](native-training.md), [native-production.md](native-production.md), [native-military-ai.md](native-military-ai.md) and [native-combat-ai.md](native-combat-ai.md). Death-order initialization also contributes to the removal boundary described in [native-damage.md](native-damage.md); AI detachment and the later Death FSM remain separate caller contracts.
