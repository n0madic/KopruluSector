# Native 1.17 guard pool and rebuild lifecycle

Guard pool/list operations, periodic rebuild entry `0x4b2320`, spending request helper `0x482a40`, attachment/detachment leaves and explicit training-word writes are recovered from StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. Evidence: [static code, timing switch and reference candidates](evidence/native-guard-lifecycle-static.json), [123 isolated original-code checks](evidence/native-guard-lifecycle-probes.json), [inspector/probe](../../tools/research/inspect_native_guard_lifecycle_117.py).

All **123 checks passed without helper stubs**, executing complete reviewed functions and actual callees for the declared fixture paths. The pool initializer case compares all1000 records byte for byte. Original instructions execute in private Unicorn memory under an instruction allowlist. No WinMain, complete game, Windows API or host file/network operation executes. Successful nearest-unit adoption remains a separately identified dependency below.

## Shared pool, creation and removal

Initializer `0x4b1a60(ECX=pool)` links **1000 records of0x20 bytes** and sets the pool free-head pointer at+`0x7d00`. It writes next/previous links, type byte+8, parent+`0x0c`, ID/home fields+`0x10..0x18` and timestamp+`0x1c`. It leaves bytes+9..`0x0b` and+`0x1a..0x1b` unchanged. The probe begins with byte`aa` and verifies the full resulting32004-byte block, including these preserved bytes. This mutation rule is not a claim that ordinary initial game memory contains those synthetic markers.

Global initializer `0x4b2640` invokes this at`0x6b6908`, writes that same pool pointer to all eight entries at`0x6be610 + player*8`, and clears their list heads at`0x6be614 + player*8`. Players therefore share the pool capacity rather than owning1000 slots each.

Precreation `0x4b22a0(ECX=player, DX=ID, stack=[x,y])` removes the first free record, fixes the next free record's previous link, and prepends the record to that player's guard list. It stores word+8=1, clearing the death byte+9 as well as setting AI type1; parent becomes null. ID and both coordinate pairs are stored as LOW16 values. Home is+`0x12`/+`0x14`; the second position, source-labeled `other_home`, is+`0x16`/+`0x18`.

It **does not reset timestamp+0x1c**. Six allocation cases cover an empty/one/two-node free list and empty/nonempty guard list; a recycled timestamp77 survives creation while deaths88 becomes0. Full-pool initialization clears timestamps, but recycling and this creator do not. Empty free head causes a normal return without creating a record; the procedure has no recovered boolean-return contract.

The native player list can contain records with nonnull parents. `0x4b21f0(ECX=player)` removes **only parentless** records, repairs reciprocal links, and prepends removed nodes to the shared free list. Attached records remain. Five mixed-list cases distinguish this operation from clearing the whole list. It resets neither timestamp nor pending request/queue contexts in this body. Its sole direct call candidate, at`0x487061`, lies on a town-start path conditional on the player's current town-head being null; the complete town-start routine remains a separate contract.

Attachment leaf `0x4b25b0(ECX=guard, EDX=unit)` copies `other_home` into home, stores the parent and writes unit+`0x134=guard`, preserving death count and timestamp. Detachment leaf `0x4b26f0(ECX=unit)` requires a nonnull AI record of type1, clears its parent, and increments death byte only while it is below100. It returns1 for that guard case and0 otherwise. Twelve native cases distinguish death99→100,100 remaining100,255 remaining255, and other AI types. This leaf neither unlinks the record nor clears the unit's own AI pointer; callers must be recovered separately before assigning those broader effects to it.

## Periodic service and cleanup order

The sole direct call candidate for `0x4b2320` is`0x483488`, inside the existing per-player slow AI service. [Frame scheduling](native-frame.md) establishes its eligibility and normal per-player intervals of64 early or304 late object steps. Guard deadlines use elapsed-game-time global`0x5349d8`, whose native timer is separately recovered in [triggers](native-triggers.md); they do not use those object-step counts directly.

The rebuild routine traverses the player's guard list, caching each next pointer before mutation. It applies cleanup before deciding whether to request a replacement:

1. With campaign flag`0x20`, death byte>=3 recycles the record, including an attached record.
2. Otherwise, an attached record is retained unless its parent has status bit`0x40000000`; that case recycles it.
3. A remaining parentless record proceeds to delay, region and replacement checks.

Eighteen native cleanup fixtures distinguish campaign/noncampaign, deaths2/3/100 and null/ordinary/hallucination parents. The recycled record retains its old parent field. In these complete fixture calls the routine also leaves an existing spending request context and the parent's unit-AI pointer unchanged. These are the routine's observed writes, not proof that all caller sequences leave stale pointers in an authentic mission.

This differs from the modified [Teippi guard scheduler](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L333): it maintains separate active/needed lists, explicitly clears hallucination parent's AI and cancels references during guard deletion. Those improvements must not be substituted for this local binary's list mutation contract.

## Retry deadlines: the source branch is reversed

Timestamp0 bypasses the retry-delay test. For a nonzero timestamp, native code computes:

```text
deaths != 0: deadline = previous_update + 300
deaths == 0: deadline = previous_update + 5 + floor(build_duration / 15)
```

The addition is u32; eligibility requires **unsigned elapsed > deadline**, so equality still waits. Forty-two native boundary cases cover both conditions, all special time additions, and equality/next-unit behavior. Six further cases establish floor division, Tank normalization, u16 time reads and wrapped deadlines.

Before reading its u16 build time from`0x5238b0 + ID*2`, stored Tank30 becomes Tank5. The five-target switch at`0x4b2538`, indexed by60 bytes at`0x4b254c`, adds these parent times to the requested time:

| Guard ID | Additional time |
|---|---|
| 44 Guardian or62 Devourer | Mutalisk43 build time. |
| 63 Dark Archon | Dark Templar61 build time. |
| 68 Archon | High Templar67 build time, once. |
| 103 Lurker | Hydralisk38 build time. |
| Other IDs | None. |

The u16 inputs are widened before summing; Guardian times65535+65535 yield131070 before division. Archon time adds one parent duration even though its spending helper emits two parent requests. Synthetic unequal Tank times confirm the normalized Tank5 read.

Teippi's scheduler uses the build-duration branch for **nonzero** deaths and300 for zero deaths, the opposite of the local instructions; it also labels the Hydralisk extra-time case differently. This report preserves the native result confirmed by isolated execution. These deadlines are in the timer's native units; dividing by15 is an actual arithmetic operation, not evidence for an assumed wall-clock second or24FPS conversion.

## Region admission, request insertion and timestamps

After delay eligibility, `other_home` selects the region, and native predicate`0x4aea70` requires state!=3, both target dwords+`0x1c`/+`0x20` zero and flag`0x20` clear. Fourteen scheduler cases show that rejection leaves timestamp, demand and heap unchanged. The detailed guard predicate also appears in [default production](native-default-production.md).

For IDs37 Zergling or47 Scourge the scheduler first tries nearest available military selector`0x4af750`. Two complete cases with an empty actual player-unit list execute its real null return and fall back to a spending request. A found-unit path instead calls AI removal`0x48dbc0`, copies positions, attaches the guard and issues ComputerAI`0x9c` through`0x42a2c0`. That successful adoption chain is separately recovered with79 additional native cases in [guard transfer](native-guard-transfer.md); it is not part of this batch's123 checks.

Ordinary fallback calls `0x482a40(ECX=player, EDX=guard)`. If PlayerAi+`0x224` is nonzero and unsigned `elapsed-field < 30`, it returns without adding cost or a request. Otherwise it normalizes requested Tank30 to5, adds cost, and pushes priority50 kind2 with context=the guard pointer. It then invokes the same morph-parent helper used by default production, with priority50 and a region context obtained from **home**, rather than the `other_home` used for admission. A native Guardian fixture puts these positions in regions4 and3 and observes parent Mutalisk context=region4.

The rebuild caller writes timestamp=current elapsed **after this request helper returns**, even if throttling or a unit-limit rejection prevented insertion. Five complete cases distinguish the30-unit throttle boundary, unsigned subtraction, limit rejection and resulting demand. As elsewhere, cost is added before a limit-rejected heap push.

There is no request-context deduplication check in this recovered fallback. Two successive eligible visits at elapsed0 both insert requests because the stored timestamp remains0; the equivalent elapsed100 visits insert only once, as the second is delayed. This is a controlled boundary fixture, not a claim that an ordinary campaign normally services this path twice at time0.

## Explicit training word and remaining dependencies

Native writer`0x482eb0(ECX=player, EDX=requested_ID)` stores `LOW16(requested_ID+1)` at PlayerAi+`0x220`. Seven native width cases include65535 becoming0. Its sole direct call candidate is`0x493bc6` in the shared [AIScript train/wait_force/do_morph handler](native-ai-opcodes.md). Word accessor`0x482be0` consumes and clears it for [default production](native-default-production.md). The `.text` immediate-reference scan finds those three explicit accesses, but does not exhaust alias accesses, block initialization, serialization or indirect callers. Thus the opcode-to-word-to-chooser mechanism is closed, while global reference completeness is not asserted.

Nearest military strength/distance and selected-unit AI transfer are now composed with actual [primary-order queue/activation](native-primary-orders.md) in [guard transfer](native-guard-transfer.md). Remaining concrete dependencies are generic killed-unit/state8 AI-removal branches, other callers that add/remove guard records outside these leaves, town-start cleanup's complete caller state, and subsequent admission/training/AI-assignment lifecycle.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_guard_lifecycle_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-guard-lifecycle-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-guard-lifecycle-probes.json
```

Without explicit execution, the inspector only reads/disassembles the exact hash-gated EXE. Evidence records target/script/dependency hashes, runtime versions/architecture, reviewed VA ranges, instruction counts and actual calls. Static-only witnesses and linear-reference scan limitations are recorded separately from the123 completed native fixture paths.
