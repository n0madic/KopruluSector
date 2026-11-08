# Original image geometry, LO offsets, direction and list contracts

This batch executes unchanged, isolated StarCraft **1.17.0.1** functions for image bounds, LO attachment offsets, direction changes, sprite depth keys, image insertion and sprite row relocation. It extends [stock image initialization](native-image-initialization.md) and [Move with populated images](native-trigger-move-images.md). It does not launch the retail game or establish final rendered appearance.

The target executable SHA256 is `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Preferred image base is `0x400000`; every RVA below is VA minus that base. These addresses are specific to this executable and must not be replaced with published 1.16.1 addresses.

## Evidence and reproduction

- [Inspector](../../tools/research/inspect_native_image_geometry_117.py) and [instruction inventory](evidence/native-image-geometry-static.json): **33 blocks, 2,237 decoded instructions**. Resource loading/release blocks are explicitly static only.
- [Probe](../../tools/research/probe_native_image_geometry_117.py) and [measured results](evidence/native-image-geometry-probes.json): **43,354 passing cases, 43,354 complete original calls, zero prefixes, zero helper stubs, zero imports**; **986 distinct executed instruction PCs**. Stack cleanup and return to the private sentinel are checked for each call.
- [Retail manifest](evidence/retail-manifest.json), [asset provenance](evidence/native-image-assets.json) and [asset reader](../../tools/research/inspect_native_image_assets_117.py) identify the exact archive sources and SHA256 values. No GRP, LO, DAT, palette or IScript payload is copied into project evidence.

The probe requires x86_64 Python under Rosetta on this host, Unicorn 2.1.4, pefile 2024.8.26 and Capstone 5.0.6. Architecture, Python version, probe SHA256 and every imported inspector's SHA256 are recorded in the generated JSON. It asserts the four existing inspector hashes before execution. The fixture supplies private image/sprite records and free-image lists, checked raw retail image DAT columns, retail GRP/LO files and the original IScript file. Resource pointer tables are supplied directly; archive/resource loading is not executed.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_image_geometry_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-image-geometry-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_image_geometry_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-image-assets \
  --asset-manifest docs/research/evidence/native-image-assets.json \
  --out docs/research/evidence/native-image-geometry-probes.json \
  --execute-original-code
```

Paths are explicit inputs. General setup and asset extraction are described in [reproduction](reproduction.md). Omitting `--execute-original-code` prints a plan without executing guest instructions. Exhaustive stock-derived coordinate outputs are independently predicted, compared with original results, then stored as SHA256 values. This preserves assertion evidence without publishing a bulk coordinate dataset.

## Input coverage

The recorded corpus contains **28 image IDs, 23 distinct GRP paths, 2,940 GRP frames, 43 LO aliases and 19 distinct LO paths**. Sixteen LO aliases belong to the five ordinary classes; 27 aliases select three shield LO files. All serialized frames/points of these recorded LO resources are checked. This is resource-shape coverage, not a claim that every serialized frame/point is reachable in gameplay. For example, recorded Tank body GRP has 51 frames while its special LO has 170 serialized frames.

| Probe family | Complete cases | Meaning |
| --- | ---: | --- |
| All recorded GRP frames | 5,880 | All 2,940 frames, normal and mirrored |
| Clipping, hold-Y and field wrapping | 1,080 | First/last frames, flags 0/2/4/6, six camera/position profiles |
| Ordinary LO points | 5,030 | Every recorded ordinary LO frame/point, both mirror states |
| Shield LO points | 3,264 | Three distinct files, 17 direction records × 32 points, both mirror states |
| Stock direction fields | 8,176 | All 256 flingy angles and canonical quantized values for all 28 image IDs, plus explicit byte-truncation vectors |
| Packed depth key | 16,128 | All elevation bytes, seven signed Y values, three flags and three indices |
| Follow-main special LO | 3,220 | All 805 recorded special LO frames, mirror states and changed/equal offsets |
| Follow enable | 24 | Flag toggling, equal-offset idempotence and raw byte-argument boundaries |
| Follow/direction sequencing | 10 | Deliberately synthetic self-main fixture discriminates the two APIs |
| Direction idempotence/frame wrap | 12 | Unchanged direction skips updates; changed frame sum wraps to WORD |
| Sprite rows | 486 | Two map heights, nine old/new signed Y values, three positions in a nonempty source list |
| Allocation/layer policies | 24 | Three complete wrappers, empty/nonempty lists, capacities 0/1/2/4, real Marine Init/shadow VM |
| Image range search | 20 | Empty, inclusive boundaries, first match and reversed range |

These tests do not execute campaign walkthroughs, all 999 image records, malformed resource recovery, every image drawing method or the complete renderer.

## GRP frame bounds

`0x40ee70` / RVA `0x0ee70` receives ECX=image and returns through original instructions. A raw GRP has a six-byte `<3H` header `(frame_count, canvas_width, canvas_height)` and eight-byte frame records `<4B,I>` at `6 + frame*8`: `(x_offset, y_offset, width, height, data_offset)`. The payload offset is not used by this bounds helper. The pinned [PyMS GRP reader](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/GRP.py#L212-L275) provides the independent raw-resource schema.

For frame fields `fx,fy,fw,fh` and canvas dimensions `W,H`:

```text
world_x = signed_word(sprite_x + signed_byte(image_dx)
             + (W//2 - fw - fx if image_flags & 2 else fx - W//2))
world_y = old_world_y                         if image_flags & 4
          signed_word(sprite_y + signed_byte(image_dy) + fy - H//2) otherwise
```

Flag **4 preserves the existing world-Y field** at image `+0x1e`; it does not merely omit the image offset. The helper still computes screen clipping using that preserved value. Unsigned canvas division truncates toward zero; world positions are stored in signed WORD fields before camera subtraction.

Camera coordinates are DWORDs at `0x543218/0x54321c`. The image stores eight signed WORDs at `+0x1c`: world X/Y, screen X/Y, crop X/Y and visible width/height. Clipping uses a **640×400** viewport. Entirely offscreen inputs can leave nonpositive visible dimensions; this helper does not clamp them to zero. Frame validity is a caller/resource contract: the original helper performs no GRP frame-count check. The exhaustive corpus calls use legal frame indices; wrap tests are labelled private field-boundary fixtures.

## LO classes, mirroring and follow-main sequencing

| Function | ABI | Actual source of offsets |
| --- | --- | --- |
| `0x40f5d0` / RVA `0x0f5d0` | ECX=image; stack `[out_XY, kind, point]`, pops 12 | Ordinary table, current WORD frame; signed bytes, mirrored X |
| `0x40f6a0` / RVA `0x0f6a0` | ECX=image; stack `[out_XY, point]`, pops 8 | Shield table, BYTE direction; signed bytes, no mirror |
| `0x410380` / RVA `0x10380` | ECX=child image | Special LO point 0 from sprite's main image ID/frame/mirror |
| `0x40f3a0` / RVA `0x0f3a0` | ECX=image; stack `[requested]`, pops 4 | Toggle follow flag, optionally copy main-image special LO |

The five ordinary pointer tables start at **`0x596400 + 4*(kind*999 + image_id)`**, in order: attack, damage, special, landing dust, liftoff dust. Shield is a **separate table at `0x59b210 + image_id*4`**. It must not be treated as an adjacent sixth ordinary table at `0x59b20c`. Static resource loading `0x40fef0..0x41008a` reads the original DAT columns `0x515d98/0x516d38/0x517cd8/0x518c78/0x519c18` for ordinary classes and `0x514df8` for shield, through its loader callee. Static release `0x40f420..0x40f488` likewise loops five ordinary tables and then the separate shield table. Those loader/free callees are not executed here.

LO starts with two DWORDs `(frames, points)` and a DWORD frame-offset table at `+8`; each point is a signed-byte pair. Ordinary offset lookup returns signed **32-bit** X/Y. Mirroring negates X as 32-bit arithmetic, so an encoded `-128` can produce `+128`. The getter does not treat `[127,127]` as an absent point. Damage-overlay metadata preprocessing has its own sentinel contract, covered in [image initialization](native-image-initialization.md).

Shield lookup uses image `+0x0b` direction rather than `+0x1a` GRP frame. The probe deliberately supplies GRP frame `0x7777` and proves it is ignored. Mirroring does not change either shield offset.

Follow-main reads special-table `0x598338`, uses the **main image's** frame/mirror, and writes offsets into the child's **BYTE** fields `+0x0e/+0x0f`. Consequently mirrored `+128` truncates back to encoded byte `0x80`. Changed offsets set dirty flag 1; equal offsets preserve the existing dirty state. The enable API compares the current follow bit to the complete requested low byte, then writes requested bit 0 into flag `0x80`. Canonical arguments are 0/1; explicit 2/255/256/257 cases describe arithmetic boundaries, not expected normal script operands.

Sequencing is different in the two direction APIs: `0x411580` copies follow-main offsets **before** updating direction/frame; `0x40f1c0` updates direction/frame **before** its follow call. The ten self-main cases are discriminating synthetic fixtures, not a claim that normal child/main topology is self-referential.

## Direction and depth keys

`0x411580` / RVA `0x11580` receives ECX=image and stack `[flingy_angle]`, pops 4. It truncates the angle to BYTE and quantizes **`(angle+4)>>3`**. `0x40f1c0` / RVA `0x0f1c0` has the same ABI but accepts the already quantized BYTE. Canonical values 0..16 use normal direction; values 17..32 select mirrored direction `32-q`. Flingy bytes 252..255 therefore become mirrored direction 0. Both APIs require image flag 8, supplied from the stock directional DAT column.

A change writes BYTE direction `+0x0b`, mirror bit 2, dirty bit 1, and WORD frame `base_frame + direction`. The WORD sum wraps. It selects the update callback from `0x503044 + method*8`, and normal/mirrored draw callback from `0x5030d4/0x5030d8 + method*12`. Method 17 writes WORD `0x230` at `+0x30`. When direction and flip already match, these fields and frame are left alone; a direction setter cannot be used to force refresh an independently changed base frame. Draw pointers are selected, but their pixel callbacks are not invoked in this batch.

`0x4160b0` / RVA `0x160b0` receives ECX=sprite and returns this DWORD key:

```text
key = (elevation_byte << 13) | (signed_y_word if elevation_byte <= 4 else 0)
key = ((key << 5) | (sprite_flags_byte & 0x10)) modulo 2^32
key = ((key << 9) | sprite_index_word) modulo 2^32
```

The native instructions at `0x4160d4/0x4160db` load and OR the low WORD index. The [BWAPI sprite layout](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/CSprite.h) corroborates field offsets. The pinned [OpenBW depth key](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/ui/ui.h#L541-L549) has elevation/Y/turret ordering but omits this index component. This is an observed native difference, not an instruction to copy OpenBW ordering. Negative Y, elevation bytes beyond stock values and large indices are private arithmetic vectors. A complete draw-list construction/sort call and its equal-key behavior are not executed here.

## Image insertion and sprite rows

All three allocation wrappers receive ECX=sprite and stack `[image_id, byte_X_offset, byte_Y_offset, quantized_direction]`, pop 16, and return a new image or zero. They execute real image allocation, initialization, drawing-method setup and the stock Init VM:

| Wrapper | Existing nonempty image list |
| --- | --- |
| `0x4151f0` / RVA `0x151f0` | Insert before the main image |
| `0x415260` / RVA `0x15260` | Insert before the head |
| `0x4152d0` / RVA `0x152d0` | Insert after the main image |

An empty list establishes main/head/tail with the new image. Allocation capacity 0 returns zero without changing the list. Capacity 1 initializes the Marine main image successfully even though its optional shadow allocation fails. Capacity 2 creates main image 239 and stock shadow 240; the shadow is **after** the main image in next-pointer traversal. This is original `0x4101b0 → 0x411900` underlay insertion, rather than an assumed “underlay means before” rule. With an existing `[A, main, Z]` list, the new pair occupies the insertion point specified above; the original main pointer remains unchanged. The fixture clears unit IScript contexts and disables original RNG explicitly; it supplies no unit-level cloak/ownership state.

`0x416940` / RVA `0x16940` searches the image list for the first ID in inclusive `[low,high]`; it does not sort by numeric image ID. Reversed ranges return zero in the tested lists.

Sprite relocation `0x417330` / RVA `0x17330` receives ECX=sprite, stack `[x,y]`, pops 8. Signed WORD Y is divided by **32 with truncation toward zero**, then clipped to rows `0..map_height_tiles-1`, using WORD map height `0x5264b6`. Positions themselves are still stored as raw WORDs; row clipping does not clamp world coordinates. Row heads/tails are `0x5c51d0/0x5c55d0`. A row change executes original removal `0x4119a0`, then `0x411930`, which inserts **after an existing destination head**. It preserves head/middle/tail removal correctly and marks every linked image dirty. A position change within the same clipped row preserves list topology. The early equal-position return is statically visible and also covered by the prior Move fixtures; this new 486-case row family deliberately changes X to exercise dirty/list behavior.

## Defold implications and remaining dependencies

Store native frame/base-frame, BYTE direction/mirror/follow flags, signed-byte offsets, WORD position/crop fields, image-layer topology and sprite-row topology as explicit state. Convert resources with class-specific LO semantics. Direction updates and Init can create/change simulation-owned image records before presentation. An atlas entry alone cannot represent those state transitions or finite image-pool failure behavior.

This batch closes the stated geometry, attachment, insertion and packed-key contracts for complete original calls with checked retail inputs. GRP compressed pixel interpretation, palette/player-color remapping, blend/shadow/cloak rendering, full sprite draw-list construction, texture upload and final screen observation remain distinct contracts. Raw GRP frame data offsets must also be converted to the in-memory pointer representation expected by original pixel consumers; this bounds-only use does not validate the resource loader's relocation. No campaign, all-image or visual parity claim follows from the passing case count.
