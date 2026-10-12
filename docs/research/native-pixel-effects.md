# Native draw effects, clipping and palette state — StarCraft 1.17

This batch executes unchanged original draw kernels, image callbacks and palette
memory operations. **2,072 cases passed: 2,068 complete original returns and four
explicit prefixes ending before palette submission.** It observes 2,901 distinct
instruction addresses. No import, helper stub, retail process entrypoint or full
game executes. These are indexed-buffer and state contracts, not rendered RGB
observations or proof of full visual parity.

The exact target is `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Addresses are preferred VAs for ImageBase `0x400000`; subtract it for RVAs.
Every instruction below has VA, RVA, file offset and bytes in the
[static evidence](evidence/native-pixel-effects-static.json). Measured cases,
expected hashes, call entries and runtime provenance are in the
[probe evidence](evidence/native-pixel-effects-probes.json).

The new files are
[inspector](../../tools/research/inspect_native_pixel_effects_117.py), SHA-256
`048d7c647846c80c7d1f753c05f140a7b9332418f7d9b28e1bb83709292b907e`, and
[probe](../../tools/research/probe_native_pixel_effects_117.py), SHA-256
`5ac1904f54f1a9b8f398dc5670f4f644242b465a212d5bf586174ae929b1e182`.
The inspector records 69 reviewed blocks and 4,774 decoded instructions, including
explicit static-only boundaries. Complete return paths through the original
`memmove` and `memset` are selected by declared CPU flags and aligned palette
inputs; unselected optimized branches are not claimed as executed.

## Inputs and proof boundary

Private stock inputs come from the existing
[GRP/LO manifest](evidence/native-image-assets.json),
[PCX/WPE manifest](evidence/native-pixel-assets.json), and the new
[effect asset manifest](evidence/native-pixel-effect-assets.json).
The latter records image IDs 154 and 210, their GRPs and MPQ precedence. Retail
payloads stay in scratch. Evidence stores input hashes, scalar metadata and
whole-buffer hashes rather than redistributed pixel resources.

Relevant effective hashes are:

| Resource | SHA-256 |
|---|---|
| `arr\images.dat` | `6b6123e2cc6771d96f914fc9bb92d34da17cc3319dc02e2db0cd10b4f3d2efdc` |
| `scripts\iscript.bin` | `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0` |
| `unit\protoss\texture.grp` | `2ccdfe076ed5c7ad790b1555a09d702019575e31f20a41c72a5ef3ce5ef9e567` |
| `unit\protoss\zealot.grp` | `16513ad29df7bb676d48cc537b9a6981109f6b7c8bc0066188b007d9ba6a52a2` |
| `tileset\badlands.wpe` | `37736f2f738384e0668d7ff8bd658e540d02138e00f660d59b75fb5c5cb5a3ea` |

The private bitmap is 640×480 with 16-byte guards on both sides. Every draw case
compares the entire guarded buffer against a host prediction. Existing
CImage/CUnit/Sprite state is authored explicitly: this batch does not allocate a
unit or image. Callback calls are counted as invocations, not as gameplay or
display frames. The original bounds helper still clips gameplay images to
640×400, which is distinct from the displacement kernel's 640×480 wrap domain.

## Original dispatch and effect kernels

The installed draw pointers come from `0x5030d4 + method*12` and its mirrored
slot at `+4`. Update pointers come from `0x503044 + method*8`.
All 18 method installations execute complete `0x4116e0`, including BYTE argument
truncation, mirror selection, dirty flag and method 17's packed `0x0230` state.

| Method | Normal / mirrored original draw entry | Contract in this batch |
|---|---|---|
| 2, 4 | `0x40e6c0` / `0x40e700` | Distortion pass, then foreground threshold pass |
| 3 | `0x4063b5` / `0x406c09` | Brightness-indexed background displacement |
| 5, 7 | `0x40e740` / `0x40e780` | Stock trans50 blend, then threshold pass |
| 6 | `0x40e7c0` / `0x40e7e0` | Stock trans50 blend wrapper |
| 8 | `0x406533` / `0x406d8e` | Raw foreground index supplies displacement |
| 12 | `0x40e800` / `0x40e880` | Stock warp texture blit, then main-image mask |
| 15 | `0x40e9d0` / same | Outline only for retained original frame edges |
| 16 | `0x40ea60` / `0x40eac0` | Stock shift remap, covered by the prior batch |
| 17 | `0x40eb20` / `0x40eb40` | ofire-bank background remap; stock WarpIn consumer |
| 11 | `0x464d20` / same | Health/shield/energy bar wrapper; static boundary here |

Methods 0/1, 9/10, 13/14 and 16 have complete selected consumers in the prior
[pixel blitter batch](native-pixel-blitters.md). Installation coverage of all
slots does not establish complete execution of every renderer consumer.

`0x445f30` builds the 256-byte brightness table at `0x666460` from four-byte
palette entries. For entry `(R,G,B,unused)` its result is
`(77*R + 151*G + 28*B + 4096) >> 13`. Stock badlands WPE and five authored
channel-isolation/grayscale palettes execute the complete unchanged helper.
The fourth byte has no effect.

Foreground threshold entries `0x406235` / `0x406a84` first apply the global
color map at `0x501021`. They compare that mapped index's brightness against
the low BYTE of the draw argument with **signed `jg`**, then write the mapped
index when the comparison succeeds. Vectors 0, 4, 8, 127, 128 and 255 distinguish
canonical transition state from raw signed-byte edge behavior. A reversed
private color map verifies that brightness follows the mapped color.

Distortion methods 3 and 8 read and write the current destination buffer in
place. Method 3 obtains displacement from the brightness of the **raw** source
index; method 8 uses the raw index itself. The normal path samples ahead and
the mirrored path samples behind. Both wrap at the native constant `0x4b000`
bytes, not at a host-configurable bitmap size. Earlier writes can change later
samples. The host model follows original packet/row traversal order, including
the mirrored right-to-left destination traversal. Explicit top/bottom wrap
cases distinguish this behavior from a snapshot lookup or a no-wrap clamp.

420 authored packet/crop cases distinguish transparent packets, opaque color
zero, repeat/literal boundaries, clipped packet entry, signed thresholds and
zero height. Another 144 cases use actual Ghost, Wraith and Zealot GRP frames,
full/cropped rectangles and both orientations. The inherited native blend
allocation layout has a leading 256-byte row; its `0x5a` fixture contents remain
explicitly supplied, not claimed as original loader initialization.

## Warp texture, outline and sprite clipping

The texture GRP is image 210, native pointer slot `0x543a40`; it has 21 frames
on a 192×192 canvas. Both method 12 wrappers save the bitmap global `0x6d0c40`,
create a temporary descriptor with cropped width/height, draw the chosen stock
texture frame at `(0,0)` into native scratch `0x59c2b0` using the same crop, then
restore the original descriptor and mask-copy to the main destination.
The original mask copy ignores opaque main-image colors. Transparent main
packets leave destination bytes unchanged but advance the texture cursor.
On the mirrored path the texture cursor advances while destination moves left.

126 complete cases cover all 21 texture frames, three main Zealot crops and both
orientations. They check the entire destination, the entire 65,536-byte scratch
fixture and bitmap-pointer restoration. Transparent texture pixels retain the
declared scratch byte `0xa5`; native scratch initialization outside this wrapper
is a separate requirement.

Method 15 obtains outline color from `0x6d237a` and writes it to `0x6d0c50`.
It draws the top edge only for crop top 0, left edge only for crop left 0,
right edge only when left+width equals frame width, and bottom edge only when
top+height equals frame height. Complete original horizontal `0x4ccb30` calls
real `memset`; vertical `0x4ccb70` writes one byte per bitmap stride. Eighteen
cases check full, inner, single-pixel and retained-edge crops.

240 further cases compose real bounds `0x40ee70`, sprite draw `0x415a80`, player
color setter `0x41d740`, dirty-rectangle predicate `0x417790` when required, and
the installed effect. The existing Ghost image profile is tested at negative,
origin, inner, bottom/right clipped and fully offscreen positions, both mirrors,
methods 0/3/5/6/8/17, hidden flag and three dirty-grid states.

`0x415a80` skips hidden flag `0x40` and nonpositive signed visible dimensions.
Otherwise dirty bit 1 forces drawing; without it the real predicate scans the
40-column 16×16 dirty grid at `0x6d06d0`. The wrapper clears dirty bit 1 after
each image, including skipped images, and follows the image next list. These
cases use one existing image and a zero sprite main-image pointer, so the
additional `0x416bd0` owner/main-image setup consumer is outside this profile.
They do not execute world traversal, depth sorting or framebuffer presentation.

## Cloak and stock WarpIn callback state

`0x40ed00` installs method 2 with parameter 0/timer 3. `0x40ed30` installs
method 4 when the old BYTE method is below 4, otherwise method 7, with parameter
8/timer 3. Parent-copy `0x40ef80` copies effect state only for parent methods
2 through 7 inclusive and selects pointers using the child's mirror flag.
Seventy-two initializer and 36 copy cases execute complete functions.

For cloak-in callbacks `0x40e570`, nonzero timer BYTE `+0x31` is decremented
without setting dirty. Timer zero increments parameter BYTE `+0x30`, reloads
timer 3, and marks dirty. New parameter at least 8 advances method 2→3 or 5→6
and invokes real `0x41d4c0` on context CUnit `0x543300`. That leaf replaces
status DWORD `+0xdc` bits `0x300` with the low argument's two bits, preserving
all other status bits.

Decloak `0x40e5f0` uses the same timer rule but decrements parameter and switches
to method 0 **when the old parameter was zero**. The resulting parameter byte
is `0xff`; it is not an early switch at old parameter 1. It clears the CUnit
status bits through the real leaf. Raw BYTE edge vectors are fixture evidence,
not a claim that stock gameplay supplies them.

The effective ImagesDAT contains **26** method-17 images, all with IScript 192.
These are the stock Protoss WarpIn images (IDs listed in the probe manifest),
including ZealotWarpIn 154. The previous batch's `fire` function label describes
its ofire-bank implementation; the actual stock consumer here is WarpIn.

Method-17 callback `0x40e650` starts from installed parameter 48/timer 2.
At timer zero it reloads 2, increments parameters below 63 and marks dirty.
At parameter at least 63 it installs animation 1 if needed and executes the
real VM `0x410480` with the real script-192 Death entry. The original header is
at script offset 12348, Death PC 12369, first commands `wait 1; end`.
The first call returns after wait, with PC 12371 and wait BYTE **0** because
the VM stores the operand minus one. The callback then ORs order-signal bit 1
at CUnit context `0x543304` offset `+0x4f`. An already-Death image skips VM
reinstallation but still sets the signal. The later `end`/image lifecycle is
not executed by this callback batch.

600 complete state vectors and 176 successive canonical callback calls check
timers, mirror pointers, parameters, status preservation and signal writes.
They are isolated callback sequences: the full unit/sprite progression and
the clock that invokes these callbacks remain covered by their own consumers.

## Palette memory and cycle scheduling

Native `0x4cd040` is a **setter**, not a getter: it copies the supplied input
buffer into target palette `0x6d2b88`, beginning at the low BYTE of ECX, for
low WORD EDX entries. Five vectors check direction and argument widths using
the unchanged `memmove 0x4d16a0` with declared CPU bit 1 selected.

Cycle records are eight 16-byte entries at `0x6d2f88`. Real `0x4cd000` sets
low/high entries, pattern pointer/count, speed and initial counter, resets
phase, and preserves unrelated fields; `0x4cd070` sets active byte 1.
The BYTE truncations and preservation are checked for every record index.
`0x4ccf00` clears all 128 bytes through the original `memset`.

Simple `0x4cccf0` rotates inclusive low..high entries of current palette
`0x6d2388` right by one DWORD using real overlap-safe `memmove 0x4d2360`.
It does not inspect active/speed itself. Forty-eight stock-WPE cases cover all
record indices and several ranges, including singleton and high palette entries.

Advanced `0x4ccc60` increments the BYTE phase, resets it when it equals pattern
count, then fills inclusive low..high from the pattern, wrapping the pattern
index at count. Sixty-four complete cases use declared patterns taken from
the first stock badlands WPE entries. These are private advanced-cycle fixtures,
not an asserted decoded stock advanced-cycle catalogue.

The badlands definition is initialized PE data at `0x50894c`, 36-byte hash
`68a6b142a9b5817d0c109cd5e9e3cd19e87123f50c5c94cb2dc4304da21c2541`.
Its active mask is 7; three simple cycles have speed 8 and ranges 1..6, 7..13,
248..254. Complete selector/bootstrap `0x46a390`→`0x46a3b0` clears and configures
these native records. Other tileset table entries point into runtime-populated
BSS; their initial zero file image is not treated as post-loader stock state.

Scheduler `0x4ccbc0` scans from the supplied record/index while speed is nonzero,
stopping at index 8 or a low palette entry above its supplied limit. Active
records decrement the BYTE counter, including 0→255. On zero it reloads speed
and calls the real simple or advanced leaf. Driver `0x4ccd30` makes a first pass
with limit 128, then resumes at the returned index with limit 256. Complete
non-expiry stock driver cases exercise both passes.

Expiry accumulates the minimum changed low and maximum changed high and calls
`0x4cf8c0` to submit that interval. **Four positive prefix cases stop before
that entry**, after the actual original cycle updates. Two exercise authored
simple/advanced scheduling, one the stock first two badlands cycles, one the
stock high-palette second pass. Submission is not stubbed and no complete
scheduler return is claimed for those four cases.

Brightness option setter `0x4cf930` accepts unsigned values 60..140 and replaces
out-of-range values with 100; 13 complete vectors check it. The RGB adjustment
`0x4cf760` is static only here. Its constants are `1/256`, `256`, and `100`,
and it calls original CRT math helper `0x4d5c70`; this batch does not substitute
host `pow` and call the result native proof. Fade `0x4ccd60` reaches imported
`KERNEL32!GetTickCount`. Device submission `0x4cf8c0`→`0x4cfd60` reaches
`0x4d0106`→IAT `0x4eb57c`, `Storm.dll` ordinal 357. Neither executes here.

## Reference-source distinctions and remaining work

Pinned Teippi names help identify the effects and
[CycleStruct fields](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/image.h).
Its rewritten [image callback](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/image.cpp)
switches decloak when `state-1==0` and gives WarpFlash a different reset counter.
Those are reference implementation behaviors; the original 1.17 instructions
and passing discriminating vectors above establish the target contract.

Pinned [OpenBW UI renderer](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/ui/ui.h)
uses a different cloak drawing approach and its raw distortion path samples
ahead with boundary checks. It also builds a full texture frame before cropped
mask drawing. These source comparisons explain why reference names and apparent
visual similarity cannot replace original algorithm probes.

Remaining renderer dependencies are explicit: positive health/shield/energy bar
method 11 and its helpers; sprite main-image/owner setup `0x416bd0`; original
PrepareDrawImage and dirty-rectangle generation composition; image-list ordering
with multiple populated overlays and world render traversal; loaders and all
tileset cycle definitions; native RGB gamma/math/fade; Storm device submission;
native scratch/leading-row initialization; a controlled visible original-versus-
Defold comparison. This batch does not claim these closed.

Later evidence (2026-10-12): method 11, its helpers and `0x416bd0` are executed in
[health bars](native-health-bars.md), gamma and the cycle definitions in
[palette bootstrap](native-palette-bootstrap.md). The world traversal, image-list
order and composition are compared with the original renderer on emulator frames
in [phase 2](../phase2.md#acceptance-item-7): 192 frames of all 64 campaign missions,
indexed buffer before the fog pass, equal pixel for pixel.

## Reproduction

Use the architecture/runtime and private asset setup in
[reproduction.md](reproduction.md). The new effect assets use the unchanged
image asset inspector with `--image-ids 154,210`; preserve its manifest and
private scratch directory. After setting your local path variables:

```sh
arch -x86_64 "$NATIVE_PYTHON" tools/research/inspect_native_pixel_effects_117.py \
  --exe "$RETAIL_EXE" --out /private/tmp/pixel-effects-static.json
arch -x86_64 "$NATIVE_PYTHON" tools/research/probe_native_pixel_effects_117.py \
  --exe "$RETAIL_EXE" --data "$CAMPAIGN_DATA" \
  --assets "$IMAGE_ASSETS" --asset-manifest docs/research/evidence/native-image-assets.json \
  --effect-assets "$EFFECT_ASSETS" --effect-manifest docs/research/evidence/native-pixel-effect-assets.json \
  --pixel-assets "$PIXEL_ASSETS" --pixel-manifest docs/research/evidence/native-pixel-assets.json \
  --execute-original-code --out /private/tmp/pixel-effects-probes.json
```

The runtime recorded in evidence is x86_64 Python 3.9.6, Unicorn 2.1.4,
Capstone 5.0.6 and pefile 2024.8.26. The probe pins its stable pixel/image
dependencies, checks every used resource hash, compares original return/state
or explicitly bounded prefix state, and records its own script hash.
