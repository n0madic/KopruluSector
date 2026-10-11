# Native 1.17 campaign-special initialization

This batch closes connected allocation and initial state for Spider Mine13, Scanner Sweep33, Map Revealer101, installation trap/door types203–213 and powerups215–227. It executes original `469460`, populated image/IScript initialization, native placement, completion and selected UNIT property application. It extends [mission UNIT loading](native-mission-units.md), [image initialization](native-image-initialization.md) and [Show/vision](native-unit-creation.md); their published tools and evidence remain unchanged.

The exact target is PE32 `1.17.0.1`, preferred base `0x400000`, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Every address below is a preferred VA for this image; subtract `0x400000` for RVA. No 1.16.1 address is transferred.

## Evidence and scope

[Native probes](evidence/native-special-initialization-probes.json) contain **302 complete passing cases, zero prefixes, zero bounded fragments and zero helper stubs**. They execute 3,302,884 original instructions, visit 4,689 distinct instruction addresses and record 561,753 guarded guest writes. x86_64 Python3.9.6 under Rosetta uses Unicorn2.1.4 and pefile2024.8.26. No Windows import, process startup, full game or rendering backend executes.

The [probe](../../tools/research/probe_native_special_initialization_117.py) SHA256 is `40702b25b5471dc70a6d4bb79583261c3c082271cc7441e5c14fe4ac028fc290`. The [static inspector](../../tools/research/inspect_native_special_initialization_117.py) SHA256 is `947401210c207a0e32744c43952284abfe0ac15b58eedbe462cbd3d883a17ff3`; its [witnesses](evidence/native-special-initialization-static.json) contain ten blocks and477 decoded instructions. The native report pins the imported source dependencies, runtime, executable, raw DAT/IScript inputs and resource manifests. All source/manifest hashes were checked against the workspace after generation.

| Cases | Input | Established contract |
|---:|---|---|
| 27 | One newly allocated object for every selected type, owner3/controller2 | Actual allocation, populated Init, placement, completion, lists, default order, main image/mirroring, trap masks and powerup origin |
| 81 | Those same types, runtime controllers1/3/7 | Original default-order selection and the subsequent powerup override |
| 6 | Scanner Sweep33 with finite Thingy/sprite/image capacity | Genuine optional overlay success/failure and pool/list effects |
| 15 | Types13/33/101, five shared-vision masks, original-generated sight descriptors | Complete creation/completion with full private fog-buffer comparison |
| 173 | All matching actual UNIT records in the68 physical CHKs | Native creation then unchanged record-property application, preserving actual XY/type/owner/record bytes |

The full inventory still audits21,427 UNIT records. Of the selected types, actual records are **162 Spider Mines13, nine Map Revealers101 and two Young Chrysalises216**. There are no UNIT records for Scanner Sweep33, trap/door types203–213 or the other tested powerups in this physical set. This is an encoding inventory, not a claim that those objects never appear during play: THG2/doodad conversion and trigger/action creation are separate producers. The68 physical CHKs are not described as campaign progression.

The173 record cases consume actual record bytes and source DIM dimensions, with each record/map hash retained. Runtime controllers are explicitly prepared as2; terrain is a prepared flat, walkable region with empty contour sets. They do not execute actual MTXM loading, the complete player/profile producer or general custom UNIS/UNIx overrides. The other129 cases use synthetic scalar/controller/capacity inputs with effective stock resource bytes. The distinction is recorded per case.

## Numeric identity and list behavior

**ID33 is Scanner Sweep; Spider Mine is ID13.** The native completion branch itself compares33 and101. [Pinned BWAPI numeric names](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/include/BWAPI/UnitType.h#L28-L62) identify13/33; the newly executed stock mapping independently shows13 using sprite245/image258, and33 using sprite237/image246. Image246 references the Marine GRP, but Scanner Sweep Init requests a separate sprite380. Its visible effect is therefore not described by treating the primary placeholder image as the complete visual.

At `424e63`, original completion removes33/101 from hidden heads/tails `665af8/665afc` through `4119a0`, inserts them into special heads/tails `665b00/665b04` through `411930`, and calls unit vision `423260`. Both retain sprite hidden bit32. Neither is inserted into the ordinary active list in these calls. Mine13 follows ordinary Show and active-list insertion instead. Owner-list membership and the live allocation count still exist for all three; special-list placement does not mean no allocated CUnit.

| Type | Main image | Initial human idle | Completion list/sprite |
|---:|---:|---:|---|
| 13 Spider Mine | 258, optional shadow259 | 20 | Active; sprite visible |
| 33 Scanner Sweep | 246 plus optional separate sprite380/image546 | 140 | Special; primary sprite hidden |
| 101 Map Revealer | 582 | 23 | Special; primary sprite hidden |

The six floor/wall traps are203,209,210,211,212,213. The original predicate `429160` returns1 for precisely those IDs. During completion they call `41d4c0(unit,3)`, replace status bits`0x300` with`0x300`, store CUnit+`e4 = 0x80000000`, and clear byte+`91`. Other selected types preserve the normal initialized+`e4 = 0xffffffff`. These are actual newly allocated objects with populated stock images, extending the earlier already-visible object fixtures.

Hatch/door types204–208 do not take that trap branch. Their stock DAT initialization supplies invincibility where flagged. Right variants206/208 and right wall traps212/213 execute the original explicit-flip helper `40f280`, setting image mirroring bit2 and selecting the corresponding native draw callback. This proves image state/callback selection, not rendered pixels or door collision changes after opening.

## Default order and powerup origin

At `424eb1`, controller3 selects order161 and controller7 selects162. Controller1 selects the stock computer idle byte `520d50[type]`; other tested controllers use the stock human idle byte `520e38[type]`. Original `42a2c0` performs actual order issuance; this is not a host-written final order.

Then `424ef9` tests stock unit property`0x800`. For every powerup215–227, it copies sprite XY into CUnit+`d0/d2` and issues the stock human idle again. Consequently the powerup finishes with order97 under controllers1/2/3/7, overriding the earlier161/162 choice. Trap/door/Scanner/Map Revealer types do not take this powerup-origin branch. Later rescue, pickup, drop, flag return and order97 behavior are separate consumers.

## Scanner Sweep optional sprite allocation

Stock Scanner Sweep Init reaches IScript opcode15 at offset29256, requests sprite380 at offset`[0,0]` and elevation5, then reaches wait opcode5 at29261. The actual path is:

```text
410480 IScript -> 410320 sprite overlay -> 417b80 Thingy allocation
  -> 4154a0 sprite/image creation -> 417e50 Thingy visibility
```

`410320` adds signed image offsets and signed script offsets to the parent sprite position. `417b80` uses the real finite free Thingy list `5c7a20/24`, with16-byte objects, then native sprite/image allocation. A successful Thingy joins `5c7a28/2c`; `410320` sets its sprite elevation5. The positive case supplies one actual free Thingy, two sprites and two images, producing separate sprite380/image546 while the CUnit's own placeholder sprite remains hidden.

An empty Thingy list returns0 without consuming a sprite. If the Thingy is available but its required sprite/main-image allocation fails, original code restores it to the free list. These optional effect failures still permit Scanner Sweep CUnit creation and completion. The six capacity cases distinguish these states with original allocation/list instructions, including the successful positive consumer. They do not establish subsequent Thingy animation, expiry or all-unit exhaustion behavior.

## Generated vision during completion

The15 fog cases prepare genuine native templates through `466990` and `466bd0` before calling complete creation. They use types13/33/101, center`[768,768]`, 64×64 tiles, stock sight range, initially`0x1ffff` fog cells and owner3 shared masks0/1/8/255/256. The assertion compares every changed cell to an independently calculated coordinate/mask result. This extends initialization beyond an empty vision-descriptor fixture.

`423260` combines owner/shared masks and parasite state, then calls the real vision consumer. Flying status uses the original air record count, which omits the final nine records; the Mine ground path uses the full count. Masks1..255 clear the corresponding visibility/exploration bits;0/256 produce no changed cells in these profiles. [The earlier vision report](native-unit-creation.md#original-vision-templates-and-the-air-mask-defect) contains the general dependency/height/edge contract. This batch has flat terrain, zero parasite byte and no positive carried-Flag/hangar special-source state.

## Private resources and reproduction

[Asset provenance](evidence/native-special-unit-assets.json), SHA256 `c2a4113387481a951cb1a5d4911e35da974c0a80bf7ab3d9e45ce9c0aea185fc`, records45 image IDs,37 distinct GRP paths, LO dependencies, archive precedence and hashes. Effective bytes remain private at `/private/tmp/koprulu-special-unit-assets`. The unchanged [image reader](../../tools/research/inspect_native_image_assets_117.py), SHA256 `4e82f06230a943c7d225c751aac8db7d1ac9c7c47386a95f9915102a7c11a909`, extracts these from the user's original MPQs. Network notification resources reuse [the mission provenance](evidence/native-mission-notification-assets.json), with private bytes at `/private/tmp/koprulu-mission-unit-assets`. No GRP/LO/IScript/DAT/CHK payload is added to the project.

After extracting the image IDs listed in that manifest with the unchanged reader:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_special_initialization_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/special-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_special_initialization_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-special-unit-assets \
  --asset-manifest docs/research/evidence/native-special-unit-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/special-probes.json --execute-original-code
```

The executable-backed guest maps are private buffers plus the exact PE image. The write guard rejects code writes. Original allocation/list/IScript/placement/vision/property helpers execute unchanged; cached strength tables remain caller-prepared zero inputs. Original resource loading, actual campaign terrain/player setup, positive creep requiring excluded CRT context, THG2-to-unit composition, normal addon/Nydus links, post-load fog finalization and later special orders remain separate. Initialization contracts support R03/R37 and do not certify complete mission loading, campaign behavior or visual parity.
