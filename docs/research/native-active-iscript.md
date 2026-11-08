# Native 1.17 active-unit ordering and gameplay IScript

This batch connects stock populated unit creation to the complete active-unit dispatcher and selected complete object steps. It also executes stock Zealot attack animation through the original IScript VM and melee damage pipeline. It extends [visible Death](native-unit-death.md), [image initialization](native-image-initialization.md), [populated lifecycle](native-populated-lifecycle.md) and [damage](native-damage.md). Their published tools remain unchanged.

Target: **StarCraft.exe 1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. All addresses below are preferred virtual addresses in this exact executable.

## Evidence and reproducibility

The [probe](../../tools/research/probe_native_active_iscript_117.py) produced **434 complete cases**, zero prefix cases and no helper substitutions. The recorded cases contain 1,871 complete entry invocations and 6,703,697 original instructions. There are 612 distinct reviewed ranges in the combined allowlist; 247 range starts were visited. An allowed range alone is not evidence that its branches were executed. [Native probe results](evidence/native-active-iscript-probes.json).

The [static inspector](../../tools/research/inspect_native_active_iscript_117.py) records 36 new blocks, 2,113 decoded instructions, the 36-entry movement dispatch table, energy-capacity dispatch and native DAT descriptors. Every new block ends on a decoded instruction boundary, with no undecoded tail. [Static witnesses](evidence/native-active-iscript-static.json).

| Cases | Composition |
|---:|---|
| 7 | Stock creation, completion/Show, actual Death issuance and complete `0x438ef0` active-unit step. |
| 7 | The same first active step followed through complete unit/Thingy updates until every CUnit, sprite, image and Thingy returns. |
| 14 | Seven types × visibility 0/255, with two complete `0x445070` object steps per case. |
| 160 | Synthetic `sigorder`/`orderdone` operands, separate unit/flingy contexts and the VM no-op flag, using real stock-created objects. |
| 11 | Stock Zealot attack animation: nine attack/armor upgrade combinations, one hallucinated-attacker status fixture and one invincible-target status fixture. |
| 165 | Goliath turret heading clamp at signed angular boundaries and three values of its unused second argument. |
| 42 | Exact direction-table velocity arithmetic, including low-32-bit multiplication overflow. |
| 28 | Four cooldown values, six shield values, fourteen Wraith energy/capacity values and four timed-life values. |

Probe SHA-256: `b13cac75ee8b434af23b85b7ad7e7888bda8a6ddc23abf9df876fdef2e687908`.

Inspector SHA-256: `884e6466d4659b1e58bfc876f66132da090470acbf1d6308ed5933788980ddad`.

The recorded engine is x86_64 Unicorn 2.1.4 with pefile 2024.8.26, run through the isolated Rosetta Python environment. Execution starts at an explicitly selected original function and stops only when that function returns to the private sentinel. Every executed instruction must lie in the reviewed original ranges. WinMain, Windows imports, host filesystem/network calls and replacement helpers are excluded. An individual entry has a 100,000-instruction limit.

The probe pins all 43 local source dependencies by SHA-256. Stock Units, Flingy, Sprites, Images, Weapons and Orders DAT bytes are written to the native descriptor destinations. Stock IScript SHA-256 is `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0`. GRPs and LO offsets use the unchanged [Death asset manifest](evidence/native-unit-death-assets.json): 22 image IDs and 19 distinct GRP files. All asset paths and byte hashes remain independently recorded; raw retail assets stay in private scratch files.

Reproduction command, using the existing privately extracted data:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_active_iscript_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-death-assets \
  --asset-manifest docs/research/evidence/native-unit-death-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out docs/research/evidence/native-active-iscript-probes.json \
  --execute-original-code
```

Omitting the execution flag prints the declared plan. The PyMS checkout is pinned to `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`; the executable hash is checked before any execution.

## Complete object-step composition

The accepted unpaused frame caller and the separate trigger scheduler are statically established in [native frame order](native-frame.md). This batch invokes the object step `0x445070` itself; it does not execute the outer readiness/pause/window loop or its following trigger call.

The two complete object steps execute these real calls in this order:

| Sequence | Original entry | Selected behavior |
|---:|---|---|
| 1 | `0x4781c0`, ECX=1 | Enable the original RNG. |
| 2 | `0x4b4010` | Decrement a positive creep scheduling countdown. |
| 3 | `0x48dd90` | Call script, regional and resource-area schedulers, then tail-call slow AI. |
| 4 | `0x6663e4` counter | Decrement the periodic counter; set byte `0x6663ec` according to whether it reaches zero. |
| 5 | `0x438ef0` | Complete unit step, including active Death preparation, issuance and populated-image update. |
| 6 | `0x40a5f0` | Complete empty bullet-list return and context cleanup. |
| 7 | `0x40c910` | Complete empty independent-flingy-list return. |
| 8 | `0x418990` | Complete Thingy update, with the genuine current list. |
| 9 | `0x4781c0`, ECX=0 | Disable the original RNG. |

The fixture uses controller 2 for owner 3, inactive other controllers, an empty script list at `0x6a2e70`, zero resource-area count at `0x6a5d68`, no bullets at `0x540750`, and no independent flingys at `0x5432f0`. The terrain has one synthetic region. The slow-AI counters `0x69b568` and `0x69b564`, creep countdown `0x6c46d4` and periodic counter `0x6663e4` each start at 100 and end at 98. The two steps therefore do not enter positive resource-area work, expired slow-AI work, creep recalculation or the periodic `0x40af30 → 0x46c180` refresh. These are native guarded returns, with their conditions recorded as fixture inputs.

The regional cursor starts at player 0 / region 1. `0x4b0d70` performs 333 cursor visits in each object step even with these inactive controllers: after two steps, player remains 0 and region is **667**. The cursor does not wrap in this profile. This composes the actual scheduler rather than treating all AI as one disabled helper. [Regional cursor recovery](native-radius-recruitment.md).

`0x496c80` reads the actual resource-area count and returns at `0x496e0d` when it is zero. Its complete function is statically recorded here; positive-area unit scans and terrain distance work were not executed by these cases.

The RNG flag starts disabled immediately before the first object step and is disabled after each complete return. All fifty source counters are recorded, and their sum is checked against an independent exact-integer 32-bit LCG from seed 12345. Unit, flingy and bullet context globals `0x543300`, `0x543304`, `0x543308` are all zero after the two steps. [Native RNG arithmetic](native-prng.md).

## Unit phases and the Death transition

The complete `0x438ef0` cases close the active-unit phase that the previous pending-recycling batch did not enter. Its relevant order is:

1. Global/UI/AI guards, the 150/300 timer-redistribution counters and `0x438cd0` prepass.
2. **Pending units first:** save next pointer, establish both unit contexts, update surviving images, conditionally recycle a CUnit whose sprite has disappeared, then visibility handling.
3. **Active movement preparation:** establish context and call `0x43a5f0` for each active parent.
4. Visibility/detection phases.
5. **Active main phase:** save the next pointer, establish contexts, selection timer, `0x43a320` status/cooldowns, `0x439420` primary order, `0x439310` secondary order, applicable subunit orders, then `0x418b20` populated-image update.
6. Special/hidden/other linked-list phases and final context cleanup.

The concrete list heads, original list operations and pool contracts are preserved from [unit creation](native-unit-creation.md), [image initialization](native-image-initialization.md) and [pending recycling](native-populated-lifecycle.md). A Goliath/Tank turret belongs to the owner list and links to its parent through `+0x70`; it is not an independently inserted parent in the hidden/active list. The parent preparation recursively handles it.

For a newly issued visible Death, the pending phase has already finished when `0x439420 → 0x41e630` removes the active parent. Animation installation immediately executes the stock Death VM. The active phase subsequently calls `0x418b20` on that same parent, consuming the first wait/update in the same logical unit step. Its CUnit cannot be recycled by the already completed pending phase; later steps process it there.

The full object cases set all four bytes `+0x54..+0x57` to 3 before Death. After **two** complete object steps they are **2**, and traces contain exactly one parent `0x43a320` invocation. The first step decrements them before Death; the second handles the parent as pending and does not repeat status regeneration. A Wraith beginning at energy 0 ends at raw energy **8**. A Zealot beginning ten raw shield units below maximum ends three below maximum, having regenerated **7** once. These facts depend on phase/list membership, not a separate global “dead unit timer” approximation.

The complete first unit step followed by complete unit/Thingy updates gives this timeline. Step 1 is the active step that executes Death; these numbers are logical update counts, not wall-clock measurements:

| Stock type | CUnits returned by step | All separate effects returned by step |
|---|---:|---:|
| Marine 0 | 17 | 165 |
| SCV 7 | 18 | 18 |
| Wraith 8 | 18 | 18 |
| Zealot 65 | 14 | 14 |
| Vulture 2 | 18 | 18 |
| Goliath 3, including turret 4 | 18 | 18 |
| Tank 5, including turret 6 | 20 | 20 |

Marine's corpse is a separate Thingy. The other tested explosions are images attached to the dying parent, so there is no additional 165-step corpse lifetime for them. The detailed stock programs and finite-pool failures remain in [visible Death](native-unit-death.md). All four private CUnits, eight sprites, thirty-two images and eight Thingys return at the end of each new lifecycle case.

## Movement preparation required by active Death

`0x42ad30` dispatches byte `CUnit+0x97` through 36 destinations at `0x42af44`; it repeats dispatch while a selected handler returns nonzero EAX. The table is statically recovered in full. The complete active cases execute only these initialization/stationary paths:

| Selected path | Concrete functions | Established behavior |
|---|---|---|
| Ground parent state 0 → 9 | `0x42bcc0 → 0x42b030` | With target/waypoint/sprite positions equal, set current speed zero, reset waypoint from position, and finish in state 10. |
| Wraith state 0 → 7 | `0x42bcc0 → 0x42b7f0` | Real air waypoint guard, flingy preparation, singleton-density return, prepared-position commit and density-cell update. |
| Subunit state 0 → 2 | `0x42bcc0 → 0x42c610` | Real current-position target guard, turn/movement flags, walking-animation guard and applicable heading/image update. |
| Parent/subunit synchronization | `0x43a5f0 → 0x43a800`, `0x40f5d0`, `0x40cc00`, `0x4117d0` | Apply body heading delta, actual stock LO offset, fixed position and image offset; recurse into the turret under its own context, then restore parent context. |
| Goliath turret 4 | `0x435a30 → 0x40ba90` | Clamp turret heading to the native ±32-heading envelope around the parent. |

The Wraith uses an actual density of one after creation. Multi-unit displacement, moving-ground path construction, terrain collision and state-10 continuation are separate paths. The table also identifies state 17's case `0x42ae04 → 0x42c400`; this batch does not execute that path. [Terrain and pair collision](native-movement-collision.md).

The 165 complete `0x40ba90` cases prove the byte-wrap clamp, including signed delta −128. Let `d = sign8(center-heading)`:

```text
d < -32: heading = (center + 32) & 255
d >  32: heading = (center - 32) & 255
otherwise: preserve heading
```

The second stack argument takes 0, 64 and 255 with identical results; the original function does not read it. Its compiled envelope is ±32 and it returns with `ret 8`. The wrapper passes the parent's heading and nominal argument 64. No caller address from another StarCraft version is used here.

`0x40cb90`, ECX=flingy and one stack speed argument, changes **current** speed at `+0x38` and recomputes vector `+0x40/+0x44`; it preserves next speed at `+0x3c`. For native signed direction-table component `v` at `0x5026b0 + heading*8`, the exact arithmetic is:

```text
velocity_component = sign32((v * speed) & 0xffffffff) >> 8
```

This is low-32-bit multiplication followed by an arithmetic shift. The 42 cases include cardinal and boundary headings, zero, ordinary fixed speeds, `0xffffffff` and `0x7fffffff`. They distinguish this native behavior from an unbounded double-precision multiplication. The setter's equal-current-speed early return is visible statically; the arithmetic cases deliberately choose a different old current speed.

The other recorded preparation functions are `0x40bcd0` prepare, `0x40bdb0` commit, `0x40cf50` target direction, `0x40d020` prepared position, `0x40d110` walking-animation gate, `0x40d1a0` heading step, `0x40d260` speed step, `0x40d450` velocity-heading step, `0x42aa60` air waypoint clamp, `0x42ab10` commit wrapper and `0x4815d0/0x481880` density handling. Their executed stationary branches are recorded; their complete static bodies do not establish every moving-unit branch.

## Cooldowns, shields, energy and timed life

Complete `0x43a320` tests establish independent saturating decrements for order timer `+0x54`, ground cooldown `+0x55`, air cooldown `+0x56` and spell cooldown `+0x57`. Inputs 0/1/2/255 yield 0/0/1/254. The function clears `+0x107` and, for the completed visible Marine fixture, increments status-effect timer `+0x122` from zero to one. Names are corroborated by the pinned [Teippi unit layout](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.h#L141); local reads/writes establish the offsets independently.

For a stock shield-enabled Zealot, shield regeneration is **7 raw units per status invocation**, capped to `stock_shield_amount*256`. A value already equal to maximum is preserved. The above-cap fixture is also clamped down; six boundary values execute the original comparison and signed cap operation. These are raw fixed-point values, so the rate is 7/256 displayed shield per applicable step.

The selected ordinary Wraith path `0x438e20 → 0x434a50 → 0x434940 → 0x49a470` adds **8 raw energy** and caps it to 200×256, or 250×256 with stock upgrade ID 22 present. Fourteen input/capacity combinations prove both capacities and the near-cap arithmetic. Static entry gates require Spellcaster DAT, Completed status and no Hallucination; the cloak-consumption branch and Dark Archon special-capacity branch are not exercised by these energy cases.

Completed-unit timed life is a WORD at `+0x110`. Zero stays zero. A nonzero value decrements; transition 1 → 0 executes actual `0x42a4d0` Death issuance and activates primary order 0 through the original order pool. Values 0/1/2/65535 discriminate absence, expiry, a surviving decrement and WORD range. This case closes Death issuance from the status timer, without implying that an arbitrary timer value has a stock gameplay producer.

## IScript order signals and no-op execution

The local VM dispatch table at `0x4111e4` maps **opcode 0x24** to `0x410cfc` and **opcode 0x41** to `0x410d16`:

```text
sigorder byte:  [global_543304 + 0x4f] |= byte
orderdone byte: [global_543304 + 0x4f] &= ~byte
```

The destination is the generic current-flingy context `0x543304`, rather than the current-unit context `0x543300`. The 160 operand cases intentionally point those globals to different actual stock-created Marines. The current unit's signal remains 0xa5 while the other Marine receives the exact byte-mask result. With the VM no-op flag set, operands/PC still advance and `wait 2` installs wait byte 1, but the signal is unchanged.

The short bytecode in these cases is **synthetic** and fully recorded. It tests original instruction semantics and context contracts, not the presence of a particular signal-producing program in retail campaigns. The pinned [OpenBW signal cases](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L14931) corroborate names and purpose; they are not proof of native parity.

## Stock melee attack producer

The eleven positive-animation cases create and Show a stock Zealot 65 and Marine 0 through original `0x424900/0x424da0`, set the real order target and contexts, then invoke `0x417220` with animation 2 and force flag 1. Subsequent calls use actual `0x418b20`, populated main/shadow images and the unchanged stock IScript bytes. The fixture starts the animation directly; AttackUnit target acquisition, range admission and attack cooldown scheduling are outside this composition.

Zealot main image 151 uses IScript ID 163, header 11804. Ground attack entry is PC11987. Opcode **0x1c** (`attackmelee`) at the stock program's two hit points calls actual `0x41fc70`, followed by its sound selector. The observed path is:

```text
0x417220 / 0x418b20
  → 0x410480 stock VM
  → opcode 0x1c at native handler 0x410b64
  → 0x41fc70 ground-weapon damage producer
  → 0x49a470 attacker weapon upgrade
  → 0x41dfc0 weapon damage
  → 0x41e150 surviving HP damage
  → actual target reaction and strength refresh
```

Ground weapon 64 has stock base damage 8, bonus 1 and damage-upgrade ID 13. Marine's stock armor is zero, with armor-upgrade ID 0. For these normal-damage, unshielded targets, each hit is `max(128, (8+attack_upgrade-armor_upgrade)*256)` raw HP. Nine combinations of upgrade levels 0/1/3 execute the original DAT/upgrade lookup and downstream damage calls. The base case is:

| Image update after animation installation | Marine HP | Main-image PC / wait | NoBrk status |
|---:|---:|---|---|
| 1 | 40 | 12003 / 0 | clear |
| 2 | 40 | 12009 / 0 | set |
| 3 | 40 | 12014 / 0 | set |
| 4 | 32 | 12023 / 0 | set |
| 5 | 32 | 12028 / 0 | set |
| 6 | 24 | 12037 / 0 | set |
| 7 | 24 | 12042 / 0 | set |
| 8 | 24 | 12047 / 10 | clear |

`nobrkcodestart` sets unit status `0x2000` and sprite flag `0x80`. `nobrkcodeend` executes real `0x429cc0`, clears both, and returns on the tested empty-queue path. Its queued/user-action branch calls order completion and activation; that branch is statically recorded but not executed in these attack cases.

At update 8 the stock program reaches `ignorerest`, whose current-target branch sets wait 10 and preserves its opcode PC for retry. The trace follows sixteen image updates and does not claim a complete repeat-attack/order cycle. A remake must preserve the distinction between animation progress and the primary-order consumer that changes or repeats it.

Every surviving ordinary hit calls `0x4a8f30` twice: once from surviving HP damage and once from the weapon-damage tail. The recorded entry contexts establish this producer composition. The cache call at `0x430a8f` belongs to Hallucination conversion `0x430a00`, not a periodic update. Its full conversion consumer and any periodic cache cadence remain separate requirements. [Strength/cache semantics](native-unit-ai.md).

Late fixture application of attacker Hallucination `0x40000000` or target Invincible `0x04000000` suppresses HP loss through actual native branches; it does not validate the original producer/accounting of those statuses. The animation and sound-selection operations continue. Original RNG source 4 is called **three times**: the opening random sound and each of the two melee sound lists. The native sound backend guard `0x6d3140 == 0` returns without playback. Sound choice remains simulation-relevant even in this silent fixture.

The target is an actual Marine belonging to the same human-controlled owner and already has the attacker in its reaction reference. This avoids unexamined enemy notification/neighbor/AI branches; the damage calculation still uses actual original callees. See [damage reactions and attribution](native-damage.md) for separately covered profiles. The pinned [OpenBW melee opcode](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L14883) helped identify the producer; its implementation is not substituted for the native function.

## Remaining boundaries and implementation contracts

These complete original calls establish the selected active-unit/object/image order and a real stock gameplay damage producer. They do not establish a full running campaign frame, rendering/audio output, loaded/transformed/cargo Death, positive creep scheduler recalculation, expired slow-AI work, the periodic FOW refresh, moving-ground path states, projectile creation/impact or all VM opcodes. Positive resource-area work likewise remains separate from the empty-count closure.

The next discriminating compositions are an admitted AttackUnit order through animation/cooldown repeat, `attackwith/useweapon` through actual bullet allocation and impact, interrupted NoBrk animation with a populated order queue, expired status-effect timer, and periodic completed-unit cache refresh. Each requires original producer state and real callees, with exact stop boundaries if a dependency remains unclosed.

For Defold, the established contracts require fixed logical-step phase order, distinct active/pending/hidden and owner-list membership, saved-next traversal where the native consumer uses it, immediate VM execution during animation installation, correct current-unit versus current-flingy contexts, byte/WORD wrap and exact low-32-bit arithmetic. Regeneration/cooldown updates follow the original applicable phase; an image wait must not be deferred simply because its animation was installed earlier in that same phase. Isolated green cases are evidence for these contracts, not campaign-completion or rendered-engine parity.
