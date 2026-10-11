# Native health bars, selected-image pools and size outline — StarCraft 1.17

**13,358 cases passed, all complete unchanged original-function returns.** The
batch observes 1,467 distinct instruction addresses, uses no helper stub or
external import, and never starts the retail process or full game. It compares
private indexed buffers and native object/list state; it does not observe a
presented image, RGB display, or full visual parity.

The target is `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Preferred VAs use ImageBase `0x400000`; subtract it for RVAs. The
[static evidence](evidence/native-health-bars-static.json) records 36 reviewed
blocks and 2,695 instructions with bytes, VA, RVA and file offset. The
[probe evidence](evidence/native-health-bars-probes.json) records every input,
expected/measured buffer hash, call entries, return boundary and runtime.

New reproducible sources are the
[inspector](../../tools/research/inspect_native_health_bars_117.py), SHA-256
`758d1fcce54f576f95ba4c9010896b0ea93a5dcc21d9b185aff1dcb65b62ff6f`, and
[probe](../../tools/research/probe_native_health_bars_117.py), SHA-256
`49b2660d84a62bc2b85cd0cb413fa8f1574d247d9a8ddbdd52cd69c00b95defd`.
They pin the previously published pixel-effects and native-list sources. Those
publications remain unchanged.

## Private resources and fixture scope

The [health-image input manifest](evidence/native-health-image-assets.json)
contains 12 effective ImagesDAT IDs, 560–570 and 583, from 11 GRP paths. Images
560 (method 11) and 583 (method 15) both reference the five-frame 44×28
`unit\thingy\blackx.grp`, SHA-256
`7abd5b8507a50fbc9b6ced0235bbb794dfc7b7b0d636c162fef4e20159fc43c1`.
The health constructor replaces that placeholder with its own virtual header.
Selection-circle images 561–570 provide the stock vertical placement geometry.

The [palette input manifest](evidence/native-health-palette-assets.json) records
MPQ precedence, archive hashes and effective resources. The 19×1
`game\thpbar.pcx` has SHA-256
`d1c6adc8fc9578070bd46819345b7df8fe582da4673c44a81c8f7cab434cb139`.
It supplies the original 19 indexed bar colors at `0x66cb5c`. Player-color
`tunit.pcx` and badlands `trans50.pcx` are declared inputs for owner-row tests.
Retail bytes remain in private scratch; only metadata and hashes are published.

UnitsDAT, SpritesDAT and ImagesDAT are loaded into the exact descriptor targets
of this executable. Their hashes, the stock IScript hash, manifests and source
hashes are recorded in probe evidence. Units, sprites and most existing images
are authored fixture state. The full method-11 unit profiles use stock DAT
properties for 14 unit IDs but a declared Marine sprite 130 throughout. That is
neither all campaign units nor a claim about each unit's actual stock graphics.
The private bitmap is 640×480, compared in full with 16-byte guards; the actual
bounds consumer clips gameplay images to 640×400.

## Original arithmetic and row drawing

`0x4648a0` calculates HP percentage and segment length. `0x464920` handles the
other meters. Percentage uses signed integer division, truncating toward zero:
`current*100/maximum`; a zero standalone maximum gives 100. Native 32-bit
multiplication semantics are preserved. Segment calculation uses **width−1 for
HP**, and **width for shields/energy/timed life**, then enforces a minimum of 3
and rounds to the nearest multiple of 3: remainder 1 down, remainder 2 up.
7,650 complete vectors cover every BYTE width 1–255, canonical fractions,
zero maximum and declared negative/overfull raw-integer edge profiles. The raw
edges are arithmetic fixtures, not asserted reachable stock gameplay states.

The five-row `0x4649a0` and seven-row `0x464ab0` consumers draw only when the
supplied visible height is exactly 5 or 7 respectively. A partially clipped
height suppresses that complete component. They do not use crop-top to choose
partial color rows. Fill becomes `clamp(fill-cropLeft,0,cropWidth)`, and segment
separators align to the original three-pixel grid after horizontal clipping.
The 3,024 row cases compare complete buffers, including incomplete-height
suppression, cropped-left alignment, blank columns and separator positions.

The native class tables select green at HP percentage at least 66, amber at
least 33, otherwise red. Their thresholds and 19-color palette indices are
preserved in static evidence at `0x5085d0`, `0x5085f4` and adjacent tables.
`0x464bc0` overlays four blue shield rows on the seven-row component. It draws
its original beveled left edge only when crop-left is zero and starts its
three-pixel separators three columns into the component. The 96 complete
vectors preserve the edge's small-width writes; the host prediction does not
silently crop those native writes to a narrower declared rectangle.

## Complete method 11: HP, shields, energy and timed life

`0x464d20` is the installed method-11 callback for both orientations. Its exact
ABI is ECX=destination X, EDX=destination Y, stack=`[frame, crop, CUnit]`, `ret 12`.
The unused placeholder frame pixels do not determine bar appearance.

HP reads fixed-point CUnit `+0x08`, rounds upward with `(hp+255)>>8`, and uses
stock UnitsDAT max HP shifted right by 8. A zero DAT maximum falls back to
current integer HP, or 1 if both are zero. Raw width comes from the original
SpritesDAT health-size byte at `0x51ee36+spriteID`, rather than the virtual
header's rounded/minimum width. Shield-enabled DAT units draw seven HP rows
and the four-row blue overlay; current shields use CUnit `+0x60 >> 8` and max
shields use the stock WORD DAT value.

The additional meter starts 6 rows below an ordinary HP bar, or 8 rows below
HP+shields. It is eligible for a non-hallucinated spellcaster, a hallucination,
or unit type 40 (Broodling). **It draws only when CUnit owner BYTE `+0x4c` equals
viewer DWORD `0x508d50`.** This owner check does not replace HP/shield colors
with team colors; those colors come from the fixed health palette/class table.
Remaining height must be positive and the five-row leaf still requires exactly
five visible rows.

Normal energy reads the integer HIGH BYTE at `+0xa3`. Maximum energy executes
real `0x434940`/`0x434a80`, including hero and upgrade rules and actual upgrade
accessor `0x49a470`: the original base-game and expansion banks are distinct.
Eighteen complete draw cases use unit IDs 1, 8, 12, 45, 46, 60, 63, 67 and 71
with both upgrade states; the maximum is 200 or 250 as the real helper decides.
Timed meters use WORD `+0x110`; real `0x434a90` gives hallucinations 1,350 and
Broodling 1,800. A hallucinated spellcaster displays this timed-life field.
These calls are isolated display consumers, not a timer cadence proof.

560 full-callback cases cover IDs 0, 1, 7, 8, 12, 40, 64, 65, 67, 68, 72, 73,
106 and 131, HP/shield fractions 0/1/33/66/100, own/other viewer, hallucination
state and two crop profiles. Every case also checks the complete CUnit remains
unchanged. Four discriminating Marine cases vary Matrix HP WORD `+0x112` and
Matrix timer BYTE `+0x114` through `(0,0)`, `(1,1)`, `(250,150)` and
`(65535,255)`: this routine's whole output is identical. **This establishes a
Matrix display non-dependency for this profile, not Matrix damage semantics.**

## Virtual health images and selected-image lists

Original `0x40f810` initializes a health pool entry. It installs image ID 560,
method 11, and a per-entry virtual GRP header at `0x592ba8 + index*14`.
`0x40f6e0` associates that image with a sprite and selected CUnit found through
sprite selected-index BYTE `+0x0b` and `0x6a6070 + index*4`.

The virtual width is the raw health size minus `(size−1)%3`, with minimum 19.
Height is 5, plus 2 for shields, plus 6 for a spellcaster/timed-life meter.
Vertical offset is half the actual selection-circle frame-0 height, plus 8,
plus stock SpritesDAT circle vertical offset; the resulting image offset is a
signed BYTE. Complete configuration calls execute original bounds and real
stock IScript 289 initialization: header offset 31,126, Init PC 31,162,
`wait 125`. The VM returns at PC 31,164 with wait BYTE 124. Four Marine positions
check the entire 64-byte image and 14-byte virtual header.

`0x40eb60` consumes the actual health free list at `0x59289c`/`0x592ba0`, links
the image at the sprite's `+0x1c` end of its image list through insert helper
`0x411970` (sprite `+0x1c` = health image, image `+4` = previous `+0x1c` image,
that image's `+0` = health image), then configures it. The probe names `+0x1c`
the "last image" and `+0x20` the "first image", and records exactly these writes.
In the BWAPI layout used by the function atlas, `+0x1c` is the image-list head
and image `+0` is the previous link, so the same writes place the health image
**before the head**, not after the tail. Corrected after the function atlas
([P03 `0x40eb60`](atlas/p03.md)); the executed result is unchanged, only the
head/tail naming differs. Four cases also run `0x415a80`, which renders the
health image into the complete private bitmap and clears its dirty bit. The
existing body image has zero visible dimensions, so this profile does not
claim populated body rendering.

Four further cases execute complete selection wrapper `0x417110`. With an
already-existing circle and flag 1, it sets health flag 8 and the selected
index, attaches the health image once, and ignores a second selection call
while health flag 8 is present. Complete `0x415a00` then clears both flags,
releases the health image through `0x40f540` to its original health pool, and
releases the existing circle through `0x40f570` to the original circle pool
`0x592ba4`/`0x594050`. Actual screen invalidation `0x4164c0`→`0x43eb50` marks the
native dirty grid for positive visible health dimensions. The pool heads/tails,
remaining image list, returned circle color, dirty grid and dirty flags all
match explicit predictions. New circle allocation through `0x40ec90` is outside
this existing-circle selection profile.

The damage/position leaf `0x416bb0` traverses existing images backwards and
marks **all** method-11 images dirty. It does not guard on sprite health flag 8,
does not allocate a health image, and does not return after the first match.
Ten discriminating cases verify these facts with empty/single/multiple lists
and flag 8 both set and clear. Actual callers include `0x41bb81`, `0x41e169`,
`0x4340a2` and `0x43a386`. These are caller anchors; full damage processing and
automatic damaged-unit visibility policy are not inferred from this dirty leaf.

## Stock method 15 and detected owner-color preparation

Forty cases compose the real bounds helper and sprite dispatcher for all five
stock image-583 frames, both mirrors, and inner/origin/right-bottom/offscreen
positions. Method 15 `0x40e9d0` draws only retained original frame edges, using
outline color `0x6d237a`, real horizontal `memset` and vertical-line helpers.
It uses the stock GRP frame header, not its pixel packets. Whole-buffer matches
include omitted clipped edges and cleared dirty flags. The prior
[pixel-effects report](native-pixel-effects.md) separately covers explicit
single-pixel/inner/retained-edge raw crops.

`0x416bd0` prepares detected-cloak owner blend rows only when the **parent main
image's low BYTE method is 5–7**. The full sprite draw wrapper passes that method
and sprite owner. Real `0x41d4f0` obtains the owner color: mode 2 chooses self
class 2, nonzero-alliance class 7 or enemy class 0, then finds the first matching
color assignment; other modes use the raw owner. The helper copies eight
256-byte rows inside the current trans50 bank, following the actual in-place
copy order. It does not change the current-color cache `0x5d6e38`.

1,944 complete cases distinguish all 18 methods, 12 owners, modes 0/1/2,
alliance bytes 0/1/2 and BYTE argument truncation. The stock `tunit` and trans50
bytes are used; the leading blend row remains the explicitly supplied fixture
byte `0x5a`. This closes owner-row preparation, not the loader's initialization
of that leading row or a multi-overlay cloaked sprite's complete visual output.

## Reference-source differences and remaining boundaries

Pinned [OpenBW health-bar source](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/ui/ui.h)
helps identify stock shapes and resources. Its shown fill helper uses rounded
virtual width for HP as well as other meters, whereas this executable's HP
helper uses raw width−1. Its partial-row clipping and timed-meter arithmetic
must not be silently substituted for the native contracts above.
Pinned [Teippi sprite source](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/sprite.cpp)
rewrites `MarkHealthBarDirty` with a health-flag guard and first-match return;
original 1.17's measured leaf has neither.

Still separate: allocation of a new selection circle, selection/control policy
that chooses which units receive these images, automatic damaged-unit display
policy, full populated multi-overlay/world traversal, palette RGB/device
submission, and visible original-versus-Defold comparison. The pool fixtures do
not claim the complete selection manager or all 12 health-pool entries initialized
by the original game. The [palette-effects batch](native-pixel-effects.md) covers
cycle/setter state and explicitly bounded upload prefixes; all-tileset bootstrap
and native RGB math are investigated in a dedicated continuation.

## Reproduction

Use the private-resource and Rosetta runtime procedure in
[reproduction.md](reproduction.md). Extract image IDs
`560,561,562,563,564,565,566,567,568,569,570,583` with the unchanged image asset
inspector, and the four paths in the health-palette manifest with the unchanged
pixel asset extractor. Keep retail payloads outside the repository. Then run:

```sh
arch -x86_64 "$NATIVE_PYTHON" tools/research/inspect_native_health_bars_117.py \
  --exe "$RETAIL_EXE" --out /private/tmp/health-bars-static.json
arch -x86_64 "$NATIVE_PYTHON" tools/research/probe_native_health_bars_117.py \
  --exe "$RETAIL_EXE" --data "$CAMPAIGN_DATA" --assets "$HEALTH_ASSETS" \
  --palette-manifest docs/research/evidence/native-health-palette-assets.json \
  --image-manifest docs/research/evidence/native-health-image-assets.json \
  --execute-original-code --out /private/tmp/health-bars-probes.json
```

Measured runtime is x86_64 Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6 and
pefile 2024.8.26. The evidence contains current probe/source/input hashes and
checks complete original returns with expected stack cleanup. Authored state
and stock resource bytes remain explicitly distinguished from full game state.
