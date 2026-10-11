# Native 1.17 BIN fixups, UI geometry and theme consumers

This batch uses `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
All addresses below are VAs at image base `0x400000`; subtract that base for
the RVA. These are original 1.17 instructions, not transferred 1.16.1 addresses.

The [probe evidence](evidence/native-dialog-bin-probes.json) contains **5,258
passing checks: 5,216 complete native calls and 42 prefixes**. The prefixes are
five complete-file fixup attempts and 28 isolated image-widget attempts that
stop before the real bitmap loader, plus nine font-selection attempts that
stop before `GetUserDefaultLangID`. No prefix counts as a complete call. The
batch visits 333 original instruction PCs and guards 47,612 guest writes.
Every executed helper is unchanged EXE code; no helper or import is stubbed.
There is no retail process, Windows UI, movie, renderer or full-game execution.

The [static report](evidence/native-dialog-bin-static.json) retains 23 reviewed
blocks and 1,215 instructions with their VA/RVA/file offsets, both 15-entry
callback tables and all 30 theme-word destinations. The
[private resource manifest](evidence/native-dialog-bin-assets.json) checks
33 assets: six BINs, 15 GRPs and twelve PCXs. Payload bytes stay in private
scratch. Published measurements, resource hashes and output hashes support
reproduction without distributing retail content.

## Exact resources and supported native stages

| BIN | File widgets | Complete unchanged file fixup |
|---|---:|---|
| `rez\statport.bin` | 3 | Yes |
| `rez\glurdyt.bin` | 22 | Stops before bitmap loading |
| `rez\glurdyz.bin` | 23 | Stops before bitmap loading |
| `rez\glurdyp.bin` | 22 | Stops before bitmap loading |
| `rez\glucmpgn.bin` | 13 | Stops before bitmap loading |
| `rez\gluexpcmpgn.bin` | 13 | Stops before bitmap loading |

The 96 widgets include 28 image controls and 27 type-14 controls. Their input
SMK links contain 26 nodes; some type-14 controls have no node. The static
report preserves each linked node's offsets, flags, filename hash and position.
This is linked-file coverage, not execution of those movies or all UI screens.
The two campaign menus are separate files; their presence does not prove the
native campaign-progress producer. See [campaigns](campaigns.md).

The file loader `0x4c33a0` calls its supplied read callback, rejects a null
result, sets root flag `0x04`, and invokes the fixup helper. Its normal callback
`0x444890` opens, sizes, allocates, reads and closes through Storm imports.
That loader is static here. The complete fixup calls start with already
extracted, hash-verified file bytes at a declared private base.

Serialized records have the 86-byte layout in pinned
[PyMS DialogBIN](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DialogBIN.py).
The 30-byte SMK node layout is separately retained. That source supplies a
file schema; native pointer fixups and comparisons below come from this EXE.

## Complete BIN fixup contract

Entry `0x4c33e0` takes `ECX=first control`, `EDX=file base`, and three stack
arguments: bitmap-loader callback, source filename and source line. It returns
with `ret 12`. Its traversal mutates the buffer in this order:

1. A nonzero next-record offset at `+0` becomes a pointer relative to the file
   base. A nonzero text offset at `+0x14` similarly becomes a pointer.
2. If text exists and flag `0x200` is set, it sign-extends and transforms the
   first text byte before storing the relocated text pointer.
3. For type 14, it follows the SMK link at `+0x42`, relocates each next link
   and each filename pointer at node `+0x0a`, and adds control x/y to four
   responsive words `+0x46/+0x48/+0x4a/+0x4c`. These additions wrap in u16.
4. It installs interaction pointer `+0x2a` from `0x4f10f8 + 4*type`, and draw
   pointer `+0x2e` from `0x4f1148 + 4*type`.
5. An image control of type 5 with nonzero text invokes the supplied bitmap
   loader. This is the precise stop boundary in the image cases.
6. A negative signed bitmap value at `+0x10` is a relative reference to another
   record's bitmap field: it reads `[file_base + signed_value + 0x10]`, stores
   that bitmap value, and sets flag `0x20`. It does **not** merely mask bit 31
   from a positive file offset.
7. Type zero follows its relocated first-child link at `+0x42`. Other types
   receive parent/file-base pointer `+0x32` and follow their next link.

The entire authentic `statport.bin` chain returns from the original helper.
All 96 widgets are also tested individually by explicitly clearing only the
successor link (the root's child link for type zero). All other file bytes
remain authentic. The 68 nonimage widgets return completely; the 28 positive
image controls stop before `0x444930`, which reaches Storm bitmap loading.
These isolated controls are declared fixtures, not an intact running dialog.

Thirty synthetic cases exercise all 15 callback pairs with and without the
first-byte flag; type 5 deliberately has null text so that its complete guard
can be tested without a substituted loader. Three further cases test the
negative-relative shared-bitmap branch, which does not occur in these six
stock files. A null first-control call returns completely.

Calling this helper twice on the same fixed buffer would add the base again
to offsets that have already become pointers. The finite inputs are fresh
file bytes on every run. There is no native idempotence or malformed/cyclic
record safety claim.

## The first hotkey byte is lowercased

The exact callee is `0x4d1375`. With locale state `0x6d3220 == 0`, its complete
EXE leaf changes integer values 65–90 to value plus 32 and returns all other
tested integers unchanged. It calls no import in this branch. The fixup's
`movsx` sign-extends the source byte; only the returned low byte is stored.
The operation is **ASCII lowercase**, not uppercase or Unicode case folding.

There are 260 complete leaf cases: all 256 byte values and four signed/large
integer boundaries. Real flagged type-14 controls reach this same native
callee during the complete isolated fixups. Locale state zero is supplied
fixture state; CRT initialization is not run. The nonzero-locale branch calls
`0x4d1232` and is excluded, along with its possible locale/TLS dependencies.

## UI GRP frame arithmetic

Complete helper `0x4cf570` takes `ECX=GRP`, the requested frame in `DL`, and
five stack output pointers. Its `ret 20` fills an eight-byte bitmap descriptor,
frame x/y words and logical GRP width/height words.

The frame request is a **low-byte** value. If it is outside the header's u16
frame count, frame zero is used. A frame begins at `GRP + 6 + 8*frame`.
Offset bit 31 is cleared from the pixel offset and adds 256 to the frame's
byte width. The returned pixel pointer is relative to the GRP base; frame
origin is returned separately and is not applied to that pointer.

The 15 authentic inputs comprise `dlgs\{zerg,terran,protoss}.grp` and all
twelve `glue\Pal*\Dlg.grp` themes. They contain 2,121 physical frames.
Every possible low-byte request is executed for each file: 3,840 complete
cases, including fallback after the genuine 139/148-frame limits. Seven
synthetic cases separately discriminate u8 argument truncation and the
width-plus-256 flag. None of these 15 stock GRPs has that flag set, so the
flag proof is synthetic, not stock occurrence evidence. No GRP pixels are
blitted in this helper and no actual dialog background is observed.

## Theme metrics and the 192-byte color bank

Complete `0x4c31d0` reads a declared theme object. It copies the GRP pointer
from object `+0x10c` to `0x6ce6e0`, then 30 u16 fields at `+0x110` through
`+0x14a` to the destinations listed in the static report. An equality check
against global `0x6ce764` suppresses all writes. The helper does not update
that global; its complete producer/meaning is not inferred to be a current
theme cache. Sixty-four cases discriminate every individual word, all-word
boundaries and the equality guard using explicit synthetic theme objects.

The native constructor `0x4c2ca0` allocates and builds that object, obtaining
geometry through the same `0x4cf570` helper and storing dimensions derived
from specified UI frames. Allocation and the complete constructor remain
static. The theme-object cases do not claim that the native allocator or
constructor produced their supplied fields.

The twelve authentic `glue\Pal*\tFont.pcx` files each decode to **48 indices,
six rows of eight**, with dimensions 48×1. Original loader `0x458690` allocates
`0x900` bytes at global `0x66a280`, then asks the actual bitmap wrapper to fill
12 banks at stride `0xc0`. The reviewed EXE path has no explicit clear of that
allocation. `0x4444e0` forwards to Storm; its allocation/decode semantics are
not executed. Thus these PCXs alone do not establish the other 144 bytes in
each 192-byte bank.

The complete bank-copy leaf `0x47cb00` copies exactly 48 dwords to `0x6d3050`
with `rep movsd` under the declared clear-direction-flag fixture. The 96 cases
combine each genuine 48-byte prefix with two **explicit synthetic 144-byte
suffixes**, at four source alignments. Every byte, including the supplied
suffix, is copied. This establishes the copy size and resource-index prefix;
it does not establish zero initialization of the original suffix, an RGB
palette or a screen upload. UI selector `0x458075` chooses a bank at
`0x66a280 + 192*theme_index` before applying theme metrics; that selection and
its full surrounding bootstrap are static here.

The distinct game-font loader `0x47ca80` explicitly clears 192 bytes before
its own bitmap call. Its clear does not prove the initialization of the
separate UI banks. [Native text](native-text.md) covers indexed glyph use of
text color rows; [palette bootstrap](native-palette-bootstrap.md) covers the
separate palette/resource boundaries.

## Admission, identifiers and focus state

Complete predicate `0x4c2400` accepts only visible (`0x08`) and enabled
(`0x02` clear) controls. ID `0xffff` is rejected. Other IDs with bit 15 set
are accepted after those two guards; nonnegative IDs also need responsive
flag `0x10`. The 192 cases cover six ID boundaries and all low-five-bit
flag combinations. This is an input/control predicate, not a demonstrated
button action or permission to invoke every stored callback.

Complete lookup `0x4c2200` follows the root's child list, switching to the
supplied control's parent first when its type is nonzero. It compares only
the low u16 requested ID. Fourteen cases compose authentic statport fixup
with lookup through the root or child, including wrapped requests.

Complete binding helper `0x4c25b0` considers **signed positive IDs** on
nonroot records. For those IDs it reads slot `ID - 1` in the supplied pointer
array and replaces `+0x2a` only for a nonzero slot. Root records and IDs
0/32768/65535 are skipped. It writes the low u16 of `stack_argument >> 2` to
`0x6ce65c`; this is not a demonstrated bounds check for each pointer-array
lookup. Four authentic statport cases and 180 synthetic type/ID/slot cases
discriminate those rules. Statport's static-text control has ID `0xfff6`, so
it retains its type callback while the ID-1 button may receive an override.
The supplied callback pointers are never invoked by this binding helper.

Complete `0x4c2640` updates focus-display bit `0x1000`. A visible enabled
control gains it when it matches its parent's active pointer. A type-1
default button may also gain it with no active pointer, provided parent
flag `0x20000000` is clear. Other cases clear the bit. The 450 cases cover
all types, active pointers, parent guard and existing focus state. Six calls
to `0x4c2680` separately verify clearing mask `0x4c00`. These are finite flag
contracts; native focus acquisition, sound, event dispatch and redraw are
still connected dependencies.

## Font selection and dialog initialization boundaries

Pure selector `0x4c3660` examines `flags & 0x4c00`. Exact values `0x400`,
`0x800` and `0x4000` choose globals `0x6d3024`, `0x6d302c` and `0x6d3030`;
every other combination chooses `0x6d3028`. It tailcalls font setup
`0x4ce160`. Nine genuine-font prefixes reach the latter's real language
import at `0x4ce1c5` and stop. Width/height and selected pointer before that
boundary are checked. The import result and completed setup are not fabricated.

The two native font bootstraps bind those globals differently:

| Raw mode at `0x66ccc0` | `0x6d3024` | `0x6d3028` | `0x6d302c` | `0x6d3030` |
|---:|---|---|---|---|
| 1, `0x465ce0` (start-up and glue) | font10 | font14 | font16 | font16x |
| 2, `0x465c40` (game start) | font8 | font10 | font16 | font16x |

Corrected after the function atlas ([P22 `0x465c40`/`0x465ce0`](atlas/p22.md)):
the two rows were swapped. The string operands are `0x4ee3ec` font8 and
`0x4ee3e4` font10 in `0x465c40`, and `0x4ee3e4` font10 and `0x4ee3dc` font14 in
`0x465ce0`.

Their file-name addresses and original loads are static. The prefixes use
explicit font8/font10/font16/font16x bindings, which are the mode-2
(game-start) bindings, and authenticated plaintext font resources. The probe
label "Explicit mode1 slots" in the evidence has been corrected accordingly; the
selector results do not depend on the mode. The
separate [font decoder](native-font-decoder.md) proves equivalence of those
resources to the effective encrypted fonts for the exact pinned pairs.
The mode's complete UI producer/lifetime remains separate.

The text draw wrapper `0x4c3e70` uses the same slot choice, suppresses spacing
under flag `0x10000`, skips the leading hotkey byte under mask `0x300`, and
chooses color controls from type/state before calling layout. Those positive
draw paths remain static here because their font setup reaches the language
import. An editor flag name alone does not establish its full native effect.

Dialog initialization `0x4c2a90` also allocates, invokes callbacks and creates
a clock-backed timer. Its static coordinate instructions add root x/y to
root right/bottom after earlier initialization callbacks. For unchanged raw
statport words that arithmetic would turn `[408,408,87,71]` into
`[408,408,495,479]`; this is a conditional static calculation, not an observed
post-loader snapshot. The complete initialization, callback mutations, child
normalization and displayed rectangle are not executed.

## Reproduction and outstanding connected work

Scripts: [inspector](../../tools/research/inspect_native_dialog_bin_117.py) and
[native probe](../../tools/research/probe_native_dialog_bin_117.py). The
asset manifest records the original generic extractor, archive and StormLib
hashes. The probe records its own hash, reviewed source dependencies, both
resource manifests and x86_64 Python 3.9.6/Unicorn 2.1.4. General setup is in
[reproduction](reproduction.md). The native command is:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_dialog_bin_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-ui-bin-assets \
  --asset-manifest docs/research/evidence/native-dialog-bin-assets.json \
  --font-assets /private/tmp/koprulu-text-assets \
  --font-manifest docs/research/evidence/native-text-assets.json \
  --execute-original-code \
  --out docs/research/evidence/native-dialog-bin-probes.json
```

This narrows the BIN pointer, hotkey, geometry, callback-binding and theme
format contracts. Native allocation/read/bitmap decoding, the 144-byte UI
color-bank suffix producer, complete theme construction, nondefault locale,
language-dependent font setup, actual events/focus, callback bodies, timers,
SMK playback and dialog rendering remain separate. See
[audiovisual contracts](native-audiovisual.md) and
[briefing loader](native-briefing-loader.md) for their other closed layers.
These checks do not prove a playable briefing or complete R07/UI parity.
