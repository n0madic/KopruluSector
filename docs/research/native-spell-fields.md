# Populated Dark Swarm delivery, lifetime and Recall relocation

This batch closes selected positive effect paths that remained open in [spell effects](native-spell-effects.md): Dark Swarm with a real allocated unit and image, delivery by its original stock projectile, timed-life removal, and Recall with actual target relocation and populated effect sprites. The target is local StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Every address below is a preferred VA in that image, with ImageBase `0x400000`.

The recorded run passes **53 cases**, with **1,190 completed original function invocations**, **1,257,442 original instructions**, and no prefix stops, helper stubs or guest Windows imports. The largest single invocation executes **322,299 instructions**. Three dedicated static witnesses contain **174 instructions**; the composed run visits 249 reviewed code ranges from this and unchanged earlier batches. These are bounded private-memory contracts, rather than a full game or campaign execution.

## Evidence and fixture boundaries

- [Static inspector](../../tools/research/inspect_native_spell_fields_117.py), SHA-256 `359a6db9f61b5c8e2b1aea68475d9ad1811b225a93ff54be7e837d9308802616`.
- [Bounded probe](../../tools/research/probe_native_spell_fields_117.py), SHA-256 `a207c381ee893aac1dacf998437f92e10339b7b093d5cb62c2ce75994646bf3d`.
- [Static witnesses](evidence/native-spell-fields-static.json), [execution evidence](evidence/native-spell-fields-probes.json), and [new stock asset manifest](evidence/native-spell-fields-assets.json).

The runtime is x86_64 Darwin under Rosetta, Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6 and pefile 2024.8.26. The probe records its own hash, 51 dependency-script hashes, asset-manifest hash, every input DAT/TBL/IScript hash, instruction bounds, original entry counts and an independently checked integer RNG recurrence. Its default mode prints a plan. Execution requires `--execute-original-code`.

The guest receives unchanged stock `units.dat`, `flingy.dat`, `sprites.dat`, `images.dat`, `weapons.dat`, selected `orders.dat` columns, tech energy costs, `stat_txt.tbl` and `iscript.bin`. Main DAT columns are bound through their original EXE descriptor triples. Pinned [PyMS schemas](https://github.com/poiuyqwert/PyMS/tree/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT) decode the stock rows. Raw retail resources remain in private scratch directories.

Default pools contain four native CUnit records, eight sprites, 32 images, eight lone-sprite records and 32 primary orders. The projectile case initializes the original 100-record bullet pool. Original allocators, free lists, spatial indexes, placement, image initialization, IScript, order activation and removal all execute; allocation is never replaced by a fixture callback. The synthetic map has 4,096-pixel sides and 128-tile sides, flat walkable flags, declared region records and no terrain contours. The rollback case explicitly changes those dimensions to 512 pixels / 16 tiles and clears walkability. Sound is disabled through the original backend guard. There is no renderer, import loader, host filesystem/network access from the guest, WinMain or full game loop.

Recall issuance uses an explicit ready-caster fixture: it clears the `0x1000` gate left by freshly completed InitArbiter order 130 before calling the actual position issuer. The original InitArbiter handler and frontend command admission are outside this batch. An additional case retains the unmodified freshly created Arbiter and proves that issuance queues Recall 137 while the active order remains 130. This distinction prevents a returned issuer from being mistaken for accepted activation.

Nine selected image IDs use seven distinct stock GRP paths and nine declared LO records:

| Object | Stock binding |
|---|---|
| Defiler 46 | Flingy 4, sprite 138, images 13/14, main IScript 9 |
| Arbiter 71 | Flingy 37, sprite 188, images 130/131, main IScript 146 |
| Marine 0 | Flingy 78, sprite 235, images 239/240, main IScript 78 |
| Dark Swarm 202 | Flingy 125, sprite 286, image 337, IScript 299 |
| Recall effect | Lone-sprite ID 379, image 391, IScript 305 |
| Dark Swarm weapon 59 | Flingy 172, sprite 370, image 503, IScript 292 |

The image/LO extraction uses the existing exact-input extractor and stock archive provenance. Asset-manifest SHA-256 is `0ffc62cea8e75df1f4a38b2bfdb846c65f576bcb407c15acb59dc1080fbf38ac`. Populated image lists, nonnull GRP pointers, frame/IScript state and genuine recycling are observed; this does not establish pixel rendering.

## Dark Swarm creation and removal

`43dc10(ECX=x, EDX=y, stack=[caster_owner])` clips a packed WORD position through original `41a4b0` for type 202, then calls `424900` with **owner 11**. On success it sets status bit `0x200000`, sprite elevation byte `+0x0d=11`, completes the unit through `424da0`, and establishes its timed-life WORD at `CUnit+0x110`.

The static timer branch writes 900 when the returned field is zero or greater than 900; an existing nonzero value at most 900 would be preserved. The executed original creation path initializes that field to zero, so the positive runtime evidence establishes the resulting **900**, without pretending to test an injected shorter value between allocation and completion. The reconstructed [Teippi DarkSwarm function](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/tech.cpp#L394) always assigns its named timer constant; that modified source is corroboration, rather than the local timer oracle.

The allocated stock field has type 202, owner 11, live/completed neutral-player counters of one, populated Image337 and GRP, elevation 11, timed life 900 and status `0x04340001`. The caster-owner argument 0, 3 or 11 does not change the successful field's owner. That argument is used for the error notification on creation failure, a separate branch.

Stock type-202 extents are left/up 80 and right/down 79. Eighteen cases check both lower and upper clipping boundaries for three caster owners. With this fixture, X clips to `[80,4016]` and Y to `[80,3984]`: the native position clip reserves an additional **32 bottom pixels**. For example, request `(4095,4095)` produces `(4016,3984)`. This is the actual generic clipping contract, not a visual approximation of the field circle.

Starting from the unmodified created value, **900 actual `43a320` invocations** reduce timed life to zero and tail-call Death issuance `42a4d0`. Subsequent complete `438ef0` active-unit steps and `418990` lone-sprite/image steps return the field to its real unit pool. In this selected fixture, removal finishes after five active-unit/lone-sprite step pairs. Live/completed type-202 counters and pending-delete list become zero, and all four units, 32 images and eight sprites are free again. The selected timer and active-unit calls are recorded individually; 900 service invocations are not presented as a 900-frame campaign run. See the original [active-unit and timer contracts](native-active-iscript.md) and [unit death](native-unit-death.md).

## Dark Swarm's actual projectile delivery

An original allocated/completed Defiler46 supplies a stock weapon59 projectile. The projectile constructor is invoked directly; the generic spell-order's movement, readiness, energy debit and caster animation are not substituted or claimed executed by this case.

Weapon59's unchanged DAT row has behavior5, explosion type13, range288 and graphics172. Its genuine image503 executes stock IScript292. Six completed `40a5f0` bullet steps, interleaved with `418990`, reach real **`4090e0 → 43dc10 → 424900/424da0`** and create the populated field at `(400,400)`. The projectile sprite remains at `(200,200)`: effect placement uses the stored requested point, not the projectile sprite's coordinates. The impact record observes null unit context and genuine bullet/flingy context, with the stock Defiler source pointer retained.

One further bullet/lone-sprite step pair returns that delivering projectile to the original 100-record bullet pool and releases image503 and its sprite. The Defiler and field remain, the field still has life900, and the final free image/sprite counts are 29/6. This closes positive stock delivery and cleanup while preserving the distinction from a full Defiler cast-order execution. [Teippi's effect switch](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/bullet.cpp#L1118) corroborates the requested-point dispatch; original local execution supplies the version-specific proof.

## Recall order, candidate filters and positive relocation

The selected ready fixture executes the original position issuer `42a340(Arbiter,137,600,600)` and activates Recall. `42fc40` reads tech21's cost WORD at `51f892`, shifts it left eight bits, tests through `434b50` and debits through `435300`. The unchanged stock cost is **150×256**. Starting at 200×256, the positive path allocates an actual Recall effect, enters state1 and sets main-order timer byte `+0x54=22`.

Twenty-two complete `43a320` calls reduce that timer to zero. They also regenerate eight raw energy units each for the completed Arbiter, so completion energy is **50×256 + 22×8 = 12,976**. A subsequent `42fc40` invokes the actual 64-pixel area enumeration `442b20` with callback `4302c0`. The positive chain performs placement, movement, Show, another populated Recall effect and actual end/advance `42a5a0`. The final caster and Marine orders are Guard2, and the caster queue is empty.

Ten energy cases distinguish zero, one raw unit below the cost, the exact cost, one raw unit above it and 200×256, with The Gathering bit `0x800` either clear or set. A failed energy check allocates no effect and resumes Guard2 through the real notification/string helpers using stock TBL and a nonlocal owner. With The Gathering, both insufficiency checking and debit are bypassed: even zero energy enters state1/timer22 and creates the effect. These are original selected branches; local-owner UI display is excluded.

Callback **`4302c0(ECX=target, EDX=caster)`** excludes:

1. Sprite hidden bit `0x20`.
2. A different owner.
3. DAT building property bit1.
4. Invincible status `0x04000000`.
5. Burrowed status `0x10`.
6. Larva35, Egg36 and LurkerEgg97.
7. Hallucination status `0x40000000`.
8. The caster itself.

Ten complete rejection cases apply each guard independently to declared stock-created records with explicit field/type overrides where needed. They preserve position, create no Recall effect and leave the recalled-something flag zero. This callback returns **EAX=0 for both rejection and successful relocation**, to continue enumeration. Its return value is not a success flag. Two positive callback cases distinguish a populated effect pool from genuine exhaustion: relocation and shared flag1 succeed even when no effect sprite can be allocated. The [GPTP Recall reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/spells/recall_spell.cpp#L97) corroborates these filters; its 1.16.1 addresses are not transferred to 1.17.

## Placement, rollback and competing targets

**`4304d0(ECX=target, stack=[caster])`** saves the target's old sprite position, provisionally moves it to the caster through `43a270`, and calls actual placement search `47dc30`. When search fails, it restores the old position through `43a270` and returns before changing the recalled-something flag or allocating the destination effect.

The no-walkable 512-pixel fixture is a complete failure path: a Marine previously moved by the original relocator to `(400,400)` is restored there, remains on Guard2, gets no destination effect, preserves initial shared flag55 and leaves native placement error60. Its single invocation needs 322,299 instructions, so this one case has an explicit two-million instruction ceiling instead of the normal 100,000. It never calls a replacement placement helper.

On successful visible-target placement, the function clears references through `4269b0`, bullet references through `40a5a0`, movement state through `41a210`, detaches the sprite through `415f30`, and removes old regional/spatial membership through `42c7e0`. It clears `CUnit+0x5c`, commits the found position through `43a270`, reattaches through `430430`, and issues the unit's return-to-idle order unless type59 is Cocoon. It requests lone sprite379 and sets its elevation to target elevation plus one when allocation succeeds, then sets shared flag **`665b68=1`**. These are procedures; no successful EAX contract is inferred.

With a visible Marine at `(600,600)` and an Arbiter occupying `(300,300)`, the actual fit result is **`(304,328)`**, rather than the requested center. The earlier [placement-ring contract](native-unit-creation.md) explains native obstacle handling and scan order. A second complete composition recalls three Marines initially at `(580,600)`, `(600,600)` and `(620,600)`. Their recorded destinations are **`(304,328)`, `(320,304)` and `(320,280)`**, with pairwise nonoverlapping stock extents. All three targets end with null unit target pointers, resume Guard2 and receive populated destination effects; four live effects include the caster's initial effect. The actual iterator and scratch lifetime run while earlier targets move and are reindexed.

Another complete case first issues an actual AttackUnit10 order to the Marine through `42a380`, targeting an allocated enemy-owner Marine at `(800,800)`. Positive Recall changes that order to Guard2, clears the previously nonnull target pointer, moves the recalled Marine to `(304,328)` and preserves the enemy's `(800,800)` position. This distinguishes a real target clear from merely inspecting an already-zero fixture field.

A separate direct-helper case first executes genuine `430390(target,1)` Hide, then `4304d0`. It takes the hidden-target reattach branch, ends at `(300,300)`, clears the hidden sprite flag, removes the target from the hidden list and creates the destination effect. The public Recall callback excludes hidden targets, so this is an internal-helper contract, rather than a newly claimed Recall eligibility rule. Mode0 of `430390`, used by trigger Move, detaches a visible sprite without setting the hidden flag; this batch deliberately uses mode1. See [trigger Move](native-trigger-move.md).

## Recall effect expiry and remaining boundaries

Both stock image391 effects execute actual IScript305 and expire after **30 `418990` calls** in the selected two-unit fixture. Every step and free-pool count is recorded. The original lone-sprite list becomes empty, all eight lone-sprite records return to their pool, and free image/sprite counts become 28/6 while both actual units remain alive. This proves populated effect creation and its selected native expiry path, without a renderer or a fabricated pool-success return.

This batch leaves these dependencies explicit:

- The original InitArbiter130 handler and frontend command/research admission before the ready Recall entry.
- Recall's type59 no-idle branch and positive special cloak-state processing for types1/16/100/99/104; the ordinary Marine positive path does not open `435a40` for those types.
- Full generic Dark Swarm119 caster movement/readiness/energy/animation into projectile creation. This batch starts its delivery at the original stock projectile constructor.
- Dark Swarm's protection against attacks, interactions with spell damage and authentic-map spell placement; a created field and a selected projectile delivery are not those consumers.
- Full campaign/player interaction, audio hardware and rendered pixels. The selected effect/timer contracts do not close every spell FSM.

Earlier static/empty-pool results remain unchanged. This publication supplies the new positive consumers and exact fixture limits for their reuse in the Defold simulation specification.

## Reproduction

The asset manifest can be regenerated from the private local archives with the existing extractor, using image IDs `13,14,130,131,239,240,337,391,503`. The declared archive/GRP/LO hashes must match before running the probe. The x86_64 StormLib build also requires the Rosetta Python runtime when invoking that extractor.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_spell_fields_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-spell-fields-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_spell_fields_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest /private/tmp/koprulu-campaign-data/manifest.json \
  --assets /private/tmp/koprulu-spell-field-assets \
  --asset-manifest docs/research/evidence/native-spell-fields-assets.json \
  --pyms /private/tmp/koprulu-pyms \
  --out docs/research/evidence/native-spell-fields-probes.json \
  --execute-original-code
```

Omitting the execution flag prints the bounded plan only. Unknown input hashes, a different PyMS commit, unreviewed original instruction addresses, instruction exhaustion or incorrect stack cleanup fail the run. No failed attempt is counted as a completed case in the published execution evidence.
