# Native populated Train completion and deferred CUnit recycling (1.17)

This batch connects actual stock child creation to secondary Train progression, completion, placement, Show, completed-unit AI assignment and rally. It separately follows actual removal into the pending list, executes the complete original object-order step for the stated empty live-list profiles, and verifies CUnit reuse. It builds on [image initialization](native-image-initialization.md), [hidden-child completion](native-unit-creation.md), [training lifecycle](native-training-lifecycle.md) and [primary orders](native-primary-orders.md), without changing their published probes.

The target is the local StarCraft **1.17.0.1** PE32 image, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. All addresses below are preferred VAs for that image; subtract `0x400000` for RVAs. The evidence contains **175 passing complete cases, zero prefix cases and zero helper stubs**. The cases contain 55,478 complete original entry invocations and 8,391,833 recorded executed instructions. Execution uses Unicorn 2.1.4 in the reviewed x86_64 runtime. No Windows import, WinMain, rendering backend or whole-game loop executes.

## Evidence and caller inputs

The [static witness](evidence/native-populated-lifecycle-static.json) contains 19 reviewed blocks and 666 decoded instructions, with no undecoded tails. The [probe evidence](evidence/native-populated-lifecycle-probes.json) records the exact executable hash, current probe hash, recursively pinned source dependencies, stock DAT hashes, resource manifest hash, real callees, instruction counts, state, assertions and complete returns. The tools are [the static inspector](../../tools/research/inspect_native_populated_lifecycle_117.py) and [the execution probe](../../tools/research/probe_native_populated_lifecycle_117.py).

| Cases | Complete-call profile | Established boundary |
|---:|---|---|
| 84 | Seven stock types × human/computer × six rally relations | Actual allocation, full ordinary Train timer progression, populated child completion/Show and final AI/order/queue effects. |
| 6 | Marine/Wraith × shared-vision masks 0, 8, 9 | The same lifecycle with real hidden image updates and nonempty original-generated sight records. |
| 10 | Goliath/Tank × five actual child-allocation failures | Original parent rollback followed by the complete object-order step and CUnit return. |
| 8 | Marine/Vulture/Wraith/Zealot × zero/two queued orders | Original hidden body removal leaves its sprite pending; the next object-order step releases images/sprite, clears orders and returns CUnit. |
| 4 | Synthetic status `0x4000` × positive shared timers | Complete object-order step defers CUnit return through the actual timer-positive gate. |
| 2 | Two actually created/removed Marines × two free-pool profiles | Saved-next iteration and exact return-list order with multiple pending units. |
| 2 | Marine/Wraith with a one-record CUnit pool | Actual removal, recycling and successful reallocation of the same CUnit, preserving then incrementing generation. |
| 39 | Synthetic `randcondjmp` operands, including disabled RNG | Original VM comparison and source-7 RNG behavior; separate from stock-script provenance. |
| 20 | Synthetic `turnrand` operands | Original VM branch choice, real turn helpers and sprite/image directions; separate from stock-script provenance. |

Stock Units/Flingy/Sprites/Images DAT columns use the original native descriptor destinations. Stock `iscript.bin` and selected raw GRP/LO files retain the provenance in [native-image-assets.json](evidence/native-image-assets.json). Their preparation is described in [image initialization](native-image-initialization.md). Raw GRP headers provide real geometry; original file loading, GRP pixel decoding, and drawing are not executed. The real selected image update callback `0x4be950` is an original RET, rather than a substituted helper.

Owner 3, viewer 0, zero upgrades, controller 2 or controller 1, prepaid queue stocks, synthetic existing producer/target objects, private finite pools, one region, flat walkable terrain, empty terrain-edge arrays and explicit visibility flags are caller fixtures. The existing producer has an **empty linked image list**; the created child has real populated main/shadow images and real stock IScript state. Successful producer image construction and its animation remain a separate boundary. The ordinary Train matrix calls the secondary dispatcher directly between timer updates; it does not claim a complete game-frame schedule. The six additional cases explicitly advance hidden child images between those calls, but still omit active movement/primary-order and hidden-order dispatchers.

## Connected ordinary Train

The entry chain is:

```text
439310 secondary dispatcher
  → 41b060 Train
    → 41bc30 begin Train
      → 424900 stock child + optional subunit creation
    → 41a280 progress/finish
      → 424db0 hidden completion
        → prefinish / 426600 placement / 47dc30
        → relocation / 424de0 completion / 422f60 Show
    → 48d320 completed-unit AI
    → 41bd70 rally
    → 484f70 queue metadata clear
```

The producer begins secondary Train 38 in state 1 with its stock unit ID at queue head0 and stocks 1000/1000 already paid. The first complete call performs real allocation, writes `producer+0xec = child`, and enters state 2. Parent type3/5 also performs actual turret creation; no child or image record is substituted.

| Type | Unit | Producer | Initial native timer | Additional Train calls through completion |
|---:|---|---:|---:|---:|
| 0 | Marine | Barracks111 | 360 | 361 |
| 7 | SCV | Command Center106 | 300 | 301 |
| 8 | Wraith | Starport114 | 900 | 901 |
| 65 | Zealot | Gateway160 | 600 | 601 |
| 2 | Vulture | Factory113 | 450 | 451 |
| 3 | Goliath | Factory113 | 600 | 601 |
| 5 | Tank | Factory113 | 750 | 751 |

The timer decrements to zero before the subsequent completion call. Every intermediate call returns through original code and its stack cleanup is checked. The evidence retains the allocation and final complete traces, a histogram covering all progress callees, total executed instructions, the union of reviewed blocks, and a SHA-256 digest of per-progress timer/HP/state observations. Repeated callbacks are not represented as additional independent cases.

The completed Goliath/Tank body and turret are distinct native records. Both have completion bit 1; the turret retains its subunit sprite marker16, HP raw1 and primary order4. In the direct-dispatch seed12345 Goliath profile, the body finishes at heading 200 while the turret remains at its creation heading 0; the movement/subunit synchronization pass has not executed. Tank body/turret retain heading 96. This is another reason to keep the shortened caller composition separate from a whole accepted-frame claim.

Completion establishes full stock fixed-point HP, remaining timer 0, cleared child hidden sprite bit, spatial insertion, removal from the hidden list, the stock idle order or rally order, and producer queue sentinel 228/head 1/state 0/current-child 0. The parent's prepaid stocks remain1000/1000. This proves lifecycle composition; it does not duplicate the separate admission/payment proof.

For controller 1, the fixture reserves a real GuardAI for combat units; SCV receives the real WorkerAI allocation. For controller 2, the completed child AI pointer remains0. Same-owner unit rally issues Follow 49 with the actual target pointer. A point or different-owner target issues Move 6. No rally, self target or zero-X point retains the stock human/computer idle order. These paths execute real primary-order allocation and activation. The existing target object remains an explicit fixture, so later Follow/Move execution is not established here.

## Hidden images and nonempty vision

The selected Marine/Wraith profiles generate sight descriptors with original `0x466990` and `0x466bd0`, then run actual allocation and Train calls. While the child remains hidden, the composition sets the original unit/IScript context and executes real `0x418b20 → 0x417860 → 0x410480` before each remaining Train call. There are 361 such hidden image updates for Marine and 901 for Wraith. The last completion removes the child from the hidden list; this composition performs no extra child image update afterward.

The complete tile array is compared against the independent position/mask specification from the original-generated records. Shared mask 0 leaves fog unchanged; masks 8 and 9 clear both visibility and exploration bits for the consumed records. Sprite visibility becomes the configured mask. Ground Marine consumes the whole generated record set; air Wraith consumes the native outer-count prefix, as established separately in [hidden-child completion](native-unit-creation.md).

The stock image updates are authoritative side effects. With seed12345 and the declared invocation sequence, Marine ends at heading 104 and consumes RNG source 3 four times, source 6 once and source 7 eight times, creation sources 14/15 once each, and prefinish source 36 once: 16 calls in total. Wraith consumes source 3 102 times, creation sources 14/15 once each, prefinish source 36 once and the real air-density source 37 once: 106 calls in total. Its final heading 248 includes that prefinish update. In both stock records, graphics direction 32 selects the original random-direction branch: source 36 supplies low 5 bits, multiplied by8. Final heading therefore must not be attributed solely to the earlier hidden turns. All original RNG counters are summed and the final state is independently compared with the exact 32-bit LCG after that many calls. These are deterministic results for this composition, not an assertion that every campaign frame follows this shortened caller schedule.

### Recovered selected IScript branch arithmetic

Original VM handler `0x410daa` implements opcode 34 `turnrand` using RNG source 6. With the low two result bits equal to1 it calls left-turn `0x40bf50`; values0,2,3 call right-turn `0x40bfd0`. Thus ordinary uniform low bits select left one quarter of the time and right three quarters. The amount is the unsigned operand byte multiplied by8; the resulting heading wraps modulo256.

The real left/right helpers write heading bytes `unit+0x21`, `+0x23` and `+0x4a`, recompute signed velocity components from the native direction table `0x5026b0` and current speed `unit+0x38`, synchronize the movement target point with the sprite when needed, and call actual `0x417190` to update all image directions. Low-32-bit multiplication and arithmetic right shift8 must be preserved.

Original handler `0x410df7` implements opcode 30 `randcondjmp` with RNG source 7. It branches when **`(result & 255) <= threshold`**, including equality. For an ordinary uniform low byte, its probability is `(threshold+1)/256`; threshold 0 still admits result 0, and threshold 255 always branches. Disabled RNG returns 0 without advancing the state or source counter, so all unsigned thresholds branch in that mode.

The59 operand cases use explicitly synthetic private bytecode with a short wait on each path. Their Marine, sprite, linked images, GRP and original callees are real; only VM input operands and PC are synthetic. The runner chooses a preimage seed for the requested first native RNG result using the modular inverse of the original LCG multiplier, resets the counters after stock object creation, and verifies the branch PC, wait, headings, image mirroring, RNG state and source count. These complete VM calls discriminate the arithmetic boundaries; they are not stock-IScript extraction or rendering evidence.

## Pending-unit producer and recycler

| Entry | Caller contract | Verified effect |
|---|---|---|
| `0x425050` | ECX=CUnit; no stack arguments | Remove the actual hidden ordinary unit from ownership/global/AI/accounting references; leave a hidden sprite and pending CUnit. |
| `0x425cd0` | ECX=CUnit; no stack arguments | Abort a successfully created parent after child failure; release its sprite immediately, leave its CUnit pending. |
| `0x438ef0` | No arguments; global lists/context/timers are input | Complete original object-order step. Tested profiles have empty active/hidden/special/cloak lists after actual removal and nonempty pending list. |
| `0x4256b0` | ECX=pending CUnit; no stack arguments | Decrement allocated count, clear queue, null sprite, unlink pending list, insert into unused list. |
| `0x4b4a10` | ECX=pending unit, shared DWORD timer `0x6c46d8` | Positive timer decrements and returns 0; zero timer resets3 and tailcalls `0x4b47f0`. |

The complete object-order step begins with AI cache clear and actual selection/UI gates, handles timer redistribution and preparatory unit passes, then obtains pending head through `0x4255b0`. At `0x438fa0` it saves the next pending pointer before doing any removal and sets both original IScript/unit contexts to the current unit. A present hidden sprite is released by real `0x416720`; its image release calls execute native dirty-rectangle logic and return records to the actual private pools. The unit image updater is called afterward.

If status bit`0x4000` is clear, a null sprite reaches actual recycler `0x4256b0` at callsite `0x438fee`. With that status bit set, `0x4b4a10` must first return nonzero. The four synthetic positive-timer profiles prove that a null sprite is insufficient for recycling while this gate returns 0: the CUnit remains pending and counted, although its images and sprite are already free. They do not enter `0x4b47f0`, so the zero-timer creep producer remains explicit.

For all rollback profiles, original creation genuinely allocates and initializes the parent before the child fails. The cap1699 profile raises count to1700, rejects the child and leaves count1700 during rollback; the next complete object-order step lowers it to1699. Other profiles similarly decrement by one only when the real recycler runs. Their owner/hidden lists are actually emptied by the original rollback, rather than replaced by empty stub results.

Direct hidden removal profiles show another distinct path: `0x425050` leaves the populated hidden sprite present, and does not release the queued Move orders. The next complete step releases all images/sprites, clears the actual queue through `0x429ac0`, returns its order records, then returns the CUnit. The entire object-order function returns with both context globals0. Other active, hidden, special and cloak loops take genuine empty-list paths; building psi and UI placement gates are caller-disabled. This is a complete function return for these scenes, rather than closure of every branch inside the function.

### Exact pool order and reuse

Unused-list insertion uses original `0x411930`, placing a returned record after the existing head. With initial unused `[U2,U3]` and pending `[U0,U1]`, the complete step returns `[U2,U1,U0,U3]`. With no initially unused records, the same pending order produces `[U0,U1]`. The saved-next iteration remains valid even though the freed unit's link fields now belong to the unused list.

In a one-record CUnit pool, actual creation sets generation 1. Removal and the complete pending step preserve generation 1 and make that same record available. Reallocation returns the same address, sets generation 2 through the original initializer, and initializes new populated images. Both Marine and Wraith profiles also verify the full six-call creation RNG sequence across the two allocations. Recycling clears the unit's order queue and sprite pointer; the later initializer resets other fields. Do not assume that releasing a CUnit zeroes the entire record.

Pinned OpenBW `update_dead_unit` similarly models a separate pending/dead list, hidden sprite release and queued-order removal before object-pool return, while explicitly leaving status`0x4000` creep handling unfinished. It is a structural source lead; the local 1.17 complete calls establish the results above. [Pinned OpenBW source](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L12800-L12827)

## Reproduction and remaining boundaries

Use the resource preparation from [image initialization](native-image-initialization.md), then:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_populated_lifecycle_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest docs/research/evidence/retail-manifest.json --assets /private/tmp/koprulu-image-assets --asset-manifest docs/research/evidence/native-image-assets.json --pyms /private/tmp/koprulu-pyms-campaign --out docs/research/evidence/native-populated-lifecycle-probes.json --execute-original-code
python3 tools/research/inspect_native_populated_lifecycle_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-populated-lifecycle-static.json
```

Without `--execute-original-code`, the probe only emits its plan. Optional `--case-group` restricts a development run; the published evidence uses `all` and is not a merge of differently hashed partial runs. Raw retail resource bytes remain private scratch files; reports preserve hashes and contracts.

The remaining lifecycle boundaries are populated producer images/animations, a complete ordinary accepted-frame schedule with movement/primary/hidden-order dispatch, visible Death-animation production and sprite exhaustion during death effects, nonempty active-loop order execution in the whole object step, zero-timer status`0x4000` creep retirement, Zerg egg/morph/building-specific creation/completion and drawing/resource decoding. The measured complete compositions close the ordinary selected child completion and deferred recycling stages; they do not replace those distinct consumer obligations.
