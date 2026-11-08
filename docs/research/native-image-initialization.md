# Native 1.17 unit creation, image initialization and subunit rollback

This batch closes successful original `0x424900` calls with populated image lists, retail DAT columns, retail GRP frame headers, retail LO files and the retail IScript program. It also closes allocation failure after the parent succeeds. The earlier [unit creation report](native-unit-creation.md) starts with an already initialized hidden child; this report establishes the preceding allocation and initialization path.

The target is StarCraft **1.17.0.1**, PE32 x86, preferred image base `0x400000`, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Every address below belongs to that exact executable. Earlier 1.16.1 addresses supplied names and structural leads; they were not reused as 1.17 execution addresses.

## Evidence and limits

- [Probe](../../tools/research/probe_native_image_initialization_117.py) and [results](evidence/native-image-initialization-probes.json): **242 passing assertion cases, zero prefixes, zero helper stubs**. Every recorded execution returns through original instructions and checks stack cleanup. The repeated-creation case contains three complete calls. Fixture preparation for direction and geometry cases also executes original creation.
- [Inspector](../../tools/research/inspect_native_image_initialization_117.py) and [static witnesses](evidence/native-image-initialization-static.json): **50 blocks, 2,936 decoded instructions, zero undecoded tails**, plus the 69-entry IScript dispatch table, draw-update callbacks, removal dispatch data and 81 DAT descriptors.
- [Asset reader](../../tools/research/inspect_native_image_assets_117.py) and [provenance](evidence/native-image-assets.json): **28 image records, 23 distinct GRP paths**, their effective archive, dimensions and SHA256, plus selected LO provenance. Retail bytes remain in private scratch. Archive hashes are checked against [the retail manifest](evidence/retail-manifest.json).
- Execution uses Unicorn **2.1.4**, an x86_64 Python runtime under Rosetta, and private guest memory. No WinMain, Windows imports, full game loop, guest file access or network access executes. The host asset reader uses the pinned checkout's open-source StormLib to read archives; that is separate from guest execution.

CUnit records occupy their real pool slots starting at `0x5da3a0`, with stride `0x150`. Sprite and image pools are private linked records with strides `0x24` and `0x40`. Pools, owner/controller state, selection lists, bullets, paths and upgrades are fixtures. Owner 3 and viewer 0 keep local UI paths out of scope. Native bookkeeping, allocation, list operations, image callbacks and IScript callees execute unchanged.

Raw GRP files supply the original frame headers used by image geometry. The original resource loader, pixel decompression, draw/blit functions and final rendered pixels are **not** validated. Actual draw-update callbacks execute; the callback selected in these stock profiles is the original `0x4be950` return instruction, not a replacement stub.

## Complete call chain and caller contracts

| Native function | Calling contract | Recovered behavior |
|---|---|---|
| `0x424900` | ECX=unit type, EDX=x; stack `[y, owner]`, callee pops 8 | Consume RNG source 14; allocate parent; add its global list entry; optionally allocate/link a turret; set turret image offset from LO; return parent or zero. |
| `0x424860` | ECX=type, EDX=x; stack `[y, owner, random byte]`, pops 12 | Check count equal to 1700, map extents, free CUnit head; call initializer; increment count and remove CUnit from free pool only after success. |
| `0x425710` | ECX=CUnit; stack `[type, x, y, owner, random byte]`, pops 20 | Reset fields; initialize type/sprite; establish queues, generation, randomizer, HP/orders, owner list and live accounting. The final random argument is not forwarded to the type initializer. |
| `0x425a70` | ECX=CUnit; stack `[type, x, y, owner]`, pops 16 | Temporarily set unit IScript context `0x543300`; initialize flingy with heading 0; immediately update images; restore context; set type-derived fields, build gains and speed. |
| `0x4178e0 → 0x40c2b0` | ECX=CUnit/flingy; stack `[flingy ID, x, y, owner, heading]`, pops 20 | Copy flingy DAT movement fields, initialize integer and fixed-point coordinates, create sprite and update image directions. |
| `0x4183b0 → 0x4154a0` | Original wrappers preserve the flingy/CUnit caller | Remove a free sprite, initialize it, then insert it into its tile-row list. Failed main-image allocation returns the sprite to the free list. |
| `0x4162f0` | ECX=sprite; stack `[sprite ID, x, y, owner]`, pops 16 | Establish sprite identity, owner, visibility and position; create its DAT main image; cache GRP dimensions clamped to 255. |
| `0x4151f0` | ECX=sprite; stack `[image ID, x offset, y offset, direction]`, pops 16 | Allocate real image; establish sprite main/head/tail links; initialize image, drawing methods, Init animation and direction. |
| `0x40fc50` | ECX=image; stack `[image ID, sprite, x offset, y offset]`, pops 16 | Set GRP pointer, directional/clickable flags, signed-byte offsets, sprite backlink, draw-specific state; reset IScript/frame state. |
| `0x40fb50` | ECX=image | Select drawing callbacks and full-IScript flag; find script header; run Init immediately; compute GRP frame geometry. |
| `0x40f340` | ECX=image; stack `[IScript ID]` | Scan original file index for ID and store its header offset. Missing-header fatal/debug path is excluded; every selected stock ID exists. |
| `0x411730 → 0x410480` | Image animation: ECX=image, stack `[animation]`; VM: ECX=image, stack `[state pointer, no-op flag, distance output]`, pops 12 | Install animation PC, clear return/wait state, execute original VM until a wait/return. Normal stock Init and Walking sampling are covered. |
| `0x418b20 → 0x417860` | ECX=CUnit, then ECX=sprite | Walk the actual linked image list; call each original draw-update callback and VM once. This occurs before remaining unit type fields are finalized. |
| `0x40f5d0` | ECX=parent main image; stack `[output XY pointer, LO kind, point index]`, pops 12 | Read signed-byte LO offsets from actual frame and mirror x when flipped. Creation uses kind 2, index 0. |
| `0x4117d0` | ECX=image; stack `[x offset, y offset]` | Store offsets as bytes and mark changed image dirty. |
| `0x425cd0` | ECX=CUnit | Remove parent state through `0x425050`, release sprite/images through `0x416720`, clear sprite pointer. Return completes even though CUnit reclamation is deferred. |

`0x425a70` explicitly passes heading **0** to flingy initialization. The source-14 draw still advances the PRNG before allocation. Tank Init subsequently executes its direction opcode and sets both tested tank CUnits to heading **96**. A renderer-level random-facing choice would therefore change these initial states.

## Stock data preparation

The native DAT descriptor arrays are recovered at `0x5014f0` for units (54 columns), `0x501148` for flingy (7), `0x501408` for sprites (6), and `0x5011a8` for images (14). The probe copies each complete raw retail column to its descriptor destination and verifies that the total consumed bytes equal the retail file length. This avoids guessing packed composite fields, shortened sprite arrays or unit extents. Orders uses the already reviewed stock interruptible/highlight/subunit-inheritance columns.

Representative destinations include unit graphics `0x51faa8`, subunit ID `0x51fb90`, properties `0x521660`, HP `0x520708`, shield amount `0x520540`; flingy sprite ID `0x510ae0`, speed `0x510c88`, acceleration `0x510fd0`, halt distance `0x511178`, turn speed `0x5114c0`, movement type `0x511670`; sprite image `0x51eaa8`; image directional/clickable/full-IScript/draw-if-cloaked columns `0x5126e8/0x512ad0/0x512eb8/0x5132a0`, draw function `0x513688`, remapping `0x513a70`, IScript ID `0x513e58`.

The retail script is 40,482 bytes, SHA256 `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0`, supplied unchanged at the pointer stored in `0x54330c`. Its first DWORD gives the ID/offset index location. The decoder checkout is pinned to PyMS `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`; its [IScript opcode schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/IScriptBIN.py#L462-L489) supplies opcode names. The native dispatch and executed program counters are independently recorded.

Prepared GRP pointers occupy `0x5436f8 + image_id*4`. The five extra LO classes use `0x596400 + (kind*999 + image_id)*4`: attack, damage, special, landing dust, liftoff dust. Shield LO files have separate provenance and are not used as a sixth entry in that table. Selected turret placement reads retail `goliath.lol` and `tank.lol`.

## Successful stock results

Each of the ten main types runs with six seeds (`0`, `1`, `12345`, `0xffffffff`, `0x12345678`, `0x80000000`) and both enabled/disabled RNG: **120 complete creation cases**. All use stock arrays and populated image lists. Values below are stored fixed-point top-speed fields after original initialization, with no player upgrades; they are not measured movement speed.

| Unit ID/type | Main / shadow image IDs | Additional CUnit / main image | Stored speed | Heading | Source-3 draws |
|---|---|---|---:|---:|---:|
| 0 Marine | 239 / 240 | — | 1024 | 0 | 1 |
| 2 Vulture | 256 / 257 | — | 1707 | 0 | 0 |
| 3 Goliath | 234 / 236 | 4 / 235 | 1200; turret 1 | 0 | 0 |
| 5 Siege Tank, tank mode | 250 / 252 | 6 / 251 | 1024; turret 0 | 96 | 0 |
| 7 SCV | 247 / 248 | — | 1280 | 0 | 0 |
| 8 Wraith | 243 / 244 | — | 1707 | 0 | 1 |
| 37 Zergling | 54 / 55 | — | 1440 | 0 | 0 |
| 41 Drone | 17 / 18 | — | 1280 | 0 | 0 |
| 43 Mutalisk | 38 / 39 | — | 1707 | 0 | 1 |
| 65 Zealot | 151 / 152 | — | 1024 | 0 | 0 |

Every normal parent is returned hidden, with sprite hidden bit `0x20`, sprite visibility 0, primary/secondary order 23, five build-queue entries 228, and generation `(old_byte+1)&31`. Six extra cases prove generation wrap and truncation. HP is `floor(stock_max_hp_fixed/10)` for normal parents. A turret has **raw HP 1**, not one whole displayed HP. The shield-amount field is initialized to stock amount times 256 even for a unit whose shield-enabled DAT flag is zero; that field alone does not confer shields. The stored initial energy likewise does not confer spellcaster capability.

Goliath and Tank each consume two CUnits, two sprites and three images. The turret is linked through both CUnit `+0x70` pointers, is present in the owner's `+0x68/+0x6c` list, and has sprite turret bit `0x10` in addition to hidden bit. It does not receive a separate parent-style hidden-list insertion. Native live/supply accounting skips DAT subunit property `0x10`, while the allocated-CUnit count includes the turret.

Creation with exactly **two CUnits, two sprites and three images** succeeds for both turret-bearing types. With the tested Tank frame, its turret image offset is `[-9,-16]`; the tested Goliath frame gives `[0,0]`. These offsets come from stock LO frame records after Init and direction changes.

The repeated Marine → Zealot → Goliath case discriminates list order. Owner list, newest first: `[Goliath turret, Goliath body, Zealot, Marine]`. Hidden list: `[Marine, Goliath body, Zealot]`. Generic `0x411930` inserts after an existing head; owner insertion in `0x425710` pushes to the front. These operations must preserve their distinct ordering.

## IScript execution and RNG

An image's eight-byte IScript state starts at `+0x10`: WORD header offset, WORD program counter, WORD return address, BYTE animation and BYTE wait. Normal animation installation resets PC/return/wait and immediately invokes the VM. A positive wait byte is decremented before any opcode is read.

The new execution traces exercise opcodes **0x00, 0x03, 0x05, 0x06, 0x07, 0x09, 0x1d, 0x29 and 0x34**: frame/vertical position, waits, goto, underlay creation, follow-main, movement and direction operations. All 69 dispatch destinations are statically recovered. This is complete execution of the tested programs, not runtime validation of every opcode and its combat/UI/resource callees.

For normal wait, the VM stores `wait_argument-1` and the next PC. For normal `waitrand`, it consumes source 3 and stores:

```text
wait_byte = (minimum - 1 + ((rng_result & 255) % (maximum-minimum+1))) & 255
```

The immediate sprite image update then decrements a positive wait. Marine Init chooses 63..75; after creation, its checked main-image wait is `61 + ((second_rng_result & 255) % 13)`. This uses the low byte and modulo, unlike the native general ranged-RNG scaling described in [native-prng.md](native-prng.md).

Parent initialization consumes source 15 for its wireframe randomizer after image initialization and speed setup. A successful turret consumes another source 15; no extra wrapper source-14 draw is made for it. Thus a Marine/Wraith/Mutalisk normal call uses sources `[14,3,15]`; most single-body types use `[14,15]`; Goliath/Tank use `[14,15,15]`. All six seeds and disabled-RNG profiles verify final state, source counters and randomizer bytes. Failed optional shadows do not introduce an extra RNG draw.

`0x4101b0` creates an actual image relative to the current image, runs its Init recursively, inherits orientation and adjusts its frame. Stock underlays become real linked shadow images. Following Init, `0x417860` walks main and shadow, invokes their real update methods and VM. The shadow's `follow-main` copies frame/orientation, so omitting that first update leaves a different state.

For movement type 2, `0x43aea0 → 0x42dff0` copies main-image IScript state into a separate state record, chooses Walking animation 11, then executes **32 samples** with no-op flag 1 and a distance output. Original speed modifiers run for each sample; the signed average becomes CUnit `+0x34`. This sampling does not replace the real Init state. Other movement types use stock flingy speed, acceleration and turn helpers.

## Geometry, direction and damage-overlay metadata

Thirteen direction cases execute `0x417190 → 0x411580` over both Marine images. Quantization is `(direction_byte+4)>>3`; values above 16 use mirrored index `32-index`. Frame is `base_frame+index`; the flip bit is 2. Direction 252..255 produces index 32 before mirroring, hence mirrored index 0.

Thirty-six GRP geometry cases execute `0x40ee70` with three stock main images, both mirror states, map positions and camera clipping. For a frame header `(fx,fy,fw,fh)` in a GRP `(width,height)`:

```text
world_x = sprite_x + signed_image_offset_x
          + (width/2 - fw - fx if flipped else fx - width/2)
world_y = sprite_y + signed_image_offset_y + fy - height/2
```

Division by two is unsigned integer truncation for GRP dimensions. Results are stored in signed WORD fields. Camera subtraction and clipping use a 640×400 viewport. Fully offscreen cases can store nonpositive visible dimensions; these are not clamped to zero by this helper. Pixel drawing is outside this evidence.

A newly recovered pure preprocessing helper, `0x40fbe0`, initializes image metadata at `0x543310`. This is **twice the count of valid damage-overlay point slots among the first 22 first-frame LO points**, where `[127,127]` marks an absent point. It is not a movement flag. `0x416050` reads the main image's value, and unit initialization stores it in CUnit `+0xa7`. The complete stock-LO preprocessing case returns counts 4 for Hatchery image 70, and 6 for Nexus 179, Barracks 266 and Command Center 275. Other unprepared LO pointer slots are explicit null fixtures. Tested mobile main images have no stock damage-overlay LO and therefore use count 0.

## Failure and rollback contracts

Twenty stock main-image-pool exhaustion cases show that a missing required image rejects sprite/unit initialization, leaves the CUnit free, sets error 62, and returns the allocated sprite to its free list. The return insertion is after the remaining head, so the first two sprite nodes exchange order in a longer pool.

Fifteen optional-shadow cases show that a one-image pool is sufficient for Marine, SCV, Wraith, Zealot and Vulture. Their main image succeeds; the failed optional shadow returns zero inside the overlay helper, but creation returns the parent. A two-image pool adds the shadow. A remaker must distinguish required sprite main-image allocation from optional IScript overlay allocation.

Ten turret-failure cases cover exhausted CUnit pool, sprite pool, one/two-image pools, and exact unit cap for both Goliath and Tank. The real chain is:

```text
424900 → failed child 424860 → parent 425cd0
       → 425050 → owner/global removal 426530
       → sprite release 416720 → image release 40fe10
```

The parent disappears from owner and hidden lists, enters pending-delete list `0x665ae8/0x665aec`, clears its sprite pointer, and returns all its sprite/image records to their pools. The CUnit is **not returned to the unused pool in this call**, and allocated count `0x665b38` does not decrease. Starting with count 1699, parent allocation raises it to 1700, child allocation rejects with error 61, rollback leaves count 1700. Other tested child failures report 62. All cases verify pending-list membership, pool counts, counters and the complete return.

Removal uses real reference clearing, movement stop, hidden/owner unlinking, live-account removal, null-AI removal, selection guards and image release. The hidden list contains the parent, so its hidden-reference scan is not a substituted empty result. Bullet, active selection, path, carried-powerup and nonnull-AI branches remain fixtures or are covered separately by [ownership](native-trigger-ownership.md), [AI removal](native-ai-removal.md) and [primary orders](native-primary-orders.md). Later pending-CUnit recycling is not executed by this batch.

Image release `0x40fe10` invalidates its screen rectangle through `0x4164c0 → 0x43eb50 → 0x4d0c60`, unlinks it from the sprite, clears its GRP pointer and returns it to the free image list. The dirty-rectangle helper and memset execute original instructions; no graphics API is called.

## Connected Train allocation

Fourteen cases connect `0x439310 → 0x41b060 → 0x41bc30 → 0x424900` for the seven Terran/Protoss training types, with initial secondary states 0 and 1. The producer is an explicitly synthetic existing building with an empty producer image list; the child uses the complete populated retail image/IScript path. A successful call sets producer state 2, `current_build_unit` to the new parent, keeps queue head/cursor and secondary Train order 38, and leaves the already-paid mineral/gas stocks unchanged. Turret-bearing children include their actual subunit.

These are complete allocation calls under the stated producer fixture. Producer image construction/animation, later child show/rally with populated images, Zerg morph/egg execution, and all building allocation side effects require their own connected profiles. Existing [training lifecycle](native-training-lifecycle.md) and [hidden-child completion](native-unit-creation.md) reports describe the corresponding established stages without expanding their image-list evidence.

## Source comparison and implementation implications

[OpenBW creation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L16242-L16358) supplied a structural guide for unit initialization, source-14/source-15 consumption and turret LO placement. Its [Walking sampling](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L15415-L15450) and [wait/overlay interpreter](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L14754-L14780) provided comparison points. The native cases independently establish the behaviors reported here; they do not certify OpenBW or arbitrary other versions.

[GPTP DAT/LO declarations](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/scbwdata.h#L307-L335) and its [LO structure](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/scbwdata.h#L405-L412) supplied field/class names. The local native descriptors and instruction reads provide the version-specific addresses and widths.

For Defold, creation must be a deterministic simulation operation with explicit finite unit, sprite and image capacity. Preserve separate pool/list order, optional overlay failure, deferred CUnit reclamation, byte-sized generations/waits, raw fixed-point HP and the full RNG sequence. IScript initialization and speed sampling belong to authoritative state: even an unseen unit executes them before it is shown. Presentation objects may reflect this state, but substituting presentation-time initialization would change random consumption, frames, wireframe data and failure behavior.

The remaining exact boundaries are retail resource loading/rendering, untested unit/building-specific creation branches, other IScript opcodes and animation consumers, populated-image child completion/show, and pending-CUnit recycling. None is counted as closed by these 242 cases.
