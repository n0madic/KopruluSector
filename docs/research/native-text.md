# Native font packets, byte strings and text layout — StarCraft 1.17

**31,457 cases passed: 31,447 complete original returns and 10 explicit
pre-import prefixes.** Unchanged native code measures stock glyphs, decodes
packets into finite private indexed buffers, resolves TBL/STR offsets, wraps
lines and computes multi-line alignment. The batch observes 692 distinct
instruction addresses and checks 342,294 native writes against declared
state/buffer/stack ranges. No helper stub, external import, UI, original
process startup or full game executes.

The executable is `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Preferred VAs use ImageBase `0x400000`; subtract it for RVAs.
[Static evidence](evidence/native-text-static.json) records 15 reviewed blocks
and 1,127 instructions, jump tables, every stock font's per-byte metrics and
string inventories. [Probe evidence](evidence/native-text-probes.json) records
each input, prediction, return/stop VA, resource hash and runtime. Retail
payloads and decoded pixels remain private.

The [inspector](../../tools/research/inspect_native_text_117.py) has SHA-256
`cf7e6209d6c88de9e3f291273d1781dafd7833724a4631f0ef3eb88fbd3e30c8`;
the [probe](../../tools/research/probe_native_text_117.py) has SHA-256
`3bef35200201abd4933fa36d526fd202b38fa7dc3460a24a380fd71b83e51a5c`.
The probe pins the existing stock PCX decoder's source hash. Health-bar,
palette-bootstrap and earlier graphics publications are unchanged.

## Actual resource provenance and font equivalence boundary

Effective installed paths `font\font*.fnt` resolve to **StarDat.mpq** and
have opaque encrypted headers. Native loader `0x4cdec0` uses callback
`0x4657f0`; the callback reads the asset and invokes actual decoder
`0x4119f0`. The published wrapper shows that dependency; the decoder and
its key preparation remain unexecuted and outside this batch's code allowlist.

The same installation's **BroodWar.mpq** independently contains plaintext
`files\font\font*.fnt` with valid `FONT` headers. These genuine original
resource inputs enable the present contracts without key handling. **Matching
names do not prove that their bytes equal the decoded effective installed
fonts.** No complete encrypted-font decoder or byte-equivalence comparison
is claimed. The converter must preserve this source distinction rather than
silently treating the two sets as interchangeable.

The [asset manifest](evidence/native-text-assets.json) pins all archives and
14 private inputs. Plaintext font hashes are:

| Resource | Bytes | Maximum width × height | Plaintext SHA-256 |
|---|---:|---:|---|
| `files\font\font8.fnt` | 6,443 | 8 × 11 | `33b56e39c0bde96834b3c1ad8e8b90e91e7f518e6209dcd1124c43399907ed01` |
| `files\font\font10.fnt` | 7,718 | 10 × 13 | `10dcf7cf3ee78f29f20992d4608e4f6c9b1c8f26d502b2e70794b437033c27f9` |
| `files\font\font14.fnt` | 13,114 | 15 × 16 | `5c4e149b6cc8ff7d66be710f47d96ab990b121c79244e2cc2364dd497f980e0a` |
| `files\font\font16.fnt` | 13,315 | 17 × 19 | `fcb9c45064fc8892af16965ab6daea51204039c6af6b650e4a0da839211c21d7` |
| `files\font\font16x.fnt` | 20,106 | 25 × 19 | `6ef6841447648794faac15dfcdcdaabf8d50c8397efcf80c3ccea59f837e73d2` |

Each font covers byte codes **32–255**, with 224 offset slots. Only space32 has
offset0; all other223 slots are populated and distinct. Filenames are not
the glyph bounding-box dimensions: for example `font8` has maximum height11.

Effective `game\tFontGam.pcx` comes from Patch_rt.mpq, SHA-256
`51936e27f42f90afea1a6c7dbbaf721d7f03ddd86538221f3f142cd82cc0c97e`.
Its decoded indexed lookup area is64×3, or24 eight-byte rows. Menu
`glue\title\tFont.pcx` comes from BrooDat.mpq, SHA-256
`5f59492a1181132d43c7aaa68cc8bd6f81e0611e3428b2f15a44cfd3116a2076`;
it is40×1, or5 rows. These are palette **indices**, not RGB colors. Every one
of the29 rows consumes masks0–7 through the original glyph routine. Native
PCX loading/color-bank bootstrap and display palette submission are separate
contracts; the private fixture supplies decoded resource rows explicitly.

## FNT schema and complete native packet decoding

The plaintext header is four bytes `FONT`, followed by BYTE low code, high
code, maximum width and maximum height. Starting at offset8 are
`high-low+1` little-endian DWORD offsets relative to the file start. Each
nonzero offset points to four BYTE metrics `(width, height, xOffset,
yOffset)`, followed by compressed glyph packets. This schema is also present
in pinned [PyMS FNT.load_file](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/FNT.py#L126).

The relocation loop at `0x4cdf10`, inside native font loader `0x4cdec0` (not a separate loader; corrected after the function atlas, [P45 `0x4cdec0`](atlas/p45.md)), adds the loaded font base to **every** offset,
including0. Private inputs reproduce that relocation from the reviewed loop;
the loader/callback itself is not executed. Thus native getter `0x4cde90`
returns the font base for stock space32, while out-of-range codes return0.
Space is handled separately by layout and is never decoded as a `FONT`
header-shaped glyph. All1,280 font/byte getter cases verify these pointers.

Original packet blitter **`0x4cfdf0`** has ECX=destination pointer,
EDX=packet pointer. Globals are color-row pointer `0x6d3010`, width DWORD
`0x6d3014`, height DWORD `0x6d3018`, and `stride-width` DWORD `0x6d301c`.
It returns without stack arguments, preserving EAX/EBX/ESI/EDI/EBP and advancing
ECX/EDX. Packet high5 bits skip0–31 pixels; low3 bits select the lookup entry.

The exact zero-mask distinction matters:

- **Skip0, mask0 writes lookup[0] and advances one pixel.** It is not a
  universal transparent pixel.
- **Positive skip, mask0 skips and fetches another packet without plotting
  an extra pixel.**
- A positive skip with a nonzero mask skips, then plots one pixel if the
  skip has not reached the final glyph boundary.
- Skips can carry across rows. A skip reaching or exceeding the final
  boundary terminates without plotting its mask. Row transitions add the
  declared stride padding, including the final transition.

All **1,115 populated stock glyphs** execute against one actual game color
row and one actual menu color row, with stride padding0/5:4,460 complete
cases. Every full8,192-byte sentinel-initialized bitmap, consumed packet count
and final destination pointer match an independent host prediction. Another
90 authored packet cases distinguish zero masks, row crossing and final
overshoot. The write hook rejects any native write outside the declared
bitmap/state/stack regions. The glyph routine itself does **not** perform
rectangle clipping; callers must establish safe positions and dimensions.

## Byte widths, bearings and scalar DBCS state

Current font pointer is `0x6d3020`. Original **`0x4cdc30`**, ECX lowBYTE=code,
returns advance width in EAX. In ordinary byte-font mode:

```text
space32: floor(font.maxWidth / 2)
tab9: configured tab width
populated ordinary byte: glyph.xOffset + glyph.width + letterSpacing
unsupported/out-of-range byte: 0
```

The left bearing contributes to the advance; the right padding up to maximum
font width does not. Glyph metric bytes are unsigned. Letter spacing is BYTE
`0x6d3035`. The3,840 cases cover all256 byte codes in all5 fonts at spacing
0/1/3. Control11 enters the ordinary lookup path, which returns0 because these
stock fonts begin at32. Other control codes1–31, apart from tab9, return0
through the width dispatch. Width calculation is a different consumer from
bounded text termination described below.

When DWORD `0x6d3008` is nonzero, the routine also maintains BYTE lead state
`0x6d300c`. For current byte≥`0xa0`, previous state≥`0xb0` or exactly`0xa4`
returns the configured mode width and clears the lead. Otherwise current
byte≥`0xb0` or exactly`0xa4` stores that lead and returns mode width. A normal
fallback clears the state and uses ordinary font metrics. Early width-control
branches can retain it. The7,680 complete scalar cases cover all bytes, all
five fonts and six previous-state profiles with explicit mode width7. They
verify this state machine, **not** an OS-dependent DBCS glyph renderer or a
language installation. Locale selection and Korean rendering imports remain
unexecuted.

Native font setup **`0x4ce160`** stores the selected font, header maxima,
tab width `2*maxWidth`, line gap3, letter spacing1, flags`0x11`, default color
row, and clip bounds`[0,0,32767,32767]`. Five cases stop immediately **before
`0x4ce1c5`**, its imported language query. This is a setup prefix, not a
complete positive font-selection function.

## TBL/STR offsets, embedded NULs and the measured corpus

Original **`0x4c13b0`** has ECX=table buffer and EDX=request ID. The buffer
begins with WORD count and count little-endian WORD offsets. It uses the
request's **low16 bits**, returns NULL for ID0, resolves one-based IDs1..count,
and returns the executable's empty-string pointer`0x4ec14e` for out-of-range
IDs. A resolved pointer is `table + WORDoffset`; lookup performs no decoding,
NUL scan or copying. The caller decides how much of the following bytes to
consume. Upper request bits are not a wider STR index.

Effective stock `rez\stat_txt.tbl` has1,547 distinct entries/offsets, and
`rez\gluAll.tbl` has202. In stat_txt,274 allocated entry blocks contain
additional embedded NULs after the first substring. Retaining only the first
substring discards remaining allocated bytes; their individual downstream
field interpretations are not closed here. The corpus has no
high bytes in these **first substrings**; that is not a claim about every
binary field after NUL or about other language installations.

All **68 physical campaign CHKs** contain one STR section each, with1,024
declared IDs per section: **69,632 references and8,296 distinct offsets** in
aggregate. Those are format counts, not progression or rendered dialogue
counts. First-substring inventory observes tab9 four times, LF10 2,297 times,
CR13 2,297 times and no high bytes. This inspected English corpus therefore
uses ASCII first-substring payloads with CR/LF controls. Duplicated/unused
offsets and trailing allocation bytes remain distinct inventory facts.

The10,395 complete lookup cases cover every TBL ID, one representative ID for
every distinct offset in every campaign STR, and per-table0/out-of-range/
low16-wrap boundaries. They do not execute CHK loading or every duplicate ID
as a separate native case. All68 raw STR tables are genuine resource inputs;
the distinction from supported required-section map normalization remains
documented in the [loader report](native-briefing-loader.md).

Keep byte identity through the converter. A reversible Latin1 representation
can transport arbitrary bytes, as pinned [PyMS TBL.load_file](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/TBL.py#L105)
does; it does not prove Latin1 is the display character mapping. Preserve
count, offsets, shared offsets, allocated blocks and embedded NULs independently
of any Unicode UI adapter. This batch does not establish Russian/localized
fonts, locale selection, STRx or Remastered encodings.

## Complete line wrapping, measurement and control consumers

Original **`0x4cd100`**, ECX=text pointer, updates current X DWORD`0x6d3048`
and returns a boundary pointer. With actual wrap flag BYTE`0x50f160=1`, it
can return completely on ordinary NUL-terminated text without imports. It
accumulates a whole word, compares its final X to inclusive right bound
WORD`0x6d303c`, and on overflow restores the word's starting X and pointer.
Whitespace advances before the next word. A word that already exceeds the
available width can therefore return its unchanged starting pointer; a higher
consumer must account for progress/vertical limits. LF10 and formfeed12 return
the pointer **after** the delimiter. CR13, controls18/19 and26 do not add width
in this wrapped parser. Native measurement of control bytes is not equivalent
to stripping all bytes below32 before parsing words.

Tab advances to the next stop relative to clip-left WORD`0x6d3038`, using
signed x86`idiv` by configured tab width. The quotient truncates toward0.
For negative relative coordinates this differs from floor-division/modulo
in some host languages. The120 complete tab cases cover both parser modes,
all5 fonts, six initial X values including negatives, and two clip-left values.
The nonwrapping profile ends through a real LF; ordinary NUL/overflow in that
mode calls locale helper`0x4c7730`, which immediately imports language ID.
No such import is supplied or executed.

All **1,749 stock TBL first substrings** run through the complete wrapped
parser with font10 and finite right bound120. Inputs remain private; evidence
contains their hashes, sizes and measured boundary/X. Another105 authored
cases vary widths20/45/120, tabs, CR/LF/formfeed, alignment controls and high
glyph bytes across all5 fonts. This is a full stock TBL input corpus at the
declared measurement profile, not every font/width/UI context.

Original bounded text **`0x4cd670`** uses ECX=start and EDX=exclusive end.
With actual measurement flag BYTE`0x6d300d=1`, it completes while measuring
glyph bearing/width/spacing and bottom extent, without plotting or querying
the language. The1,270 byte cases cover all256 values except18/19 in all5
fonts; those alignment controls invoke the line parser and are separately
covered there. This bounded consumer stops before NUL0, formfeed12, and
invalid stock glyph controls11/20. LF10, CR13 and26 add no width here;
space/tab advance. A glyph extends bottom to
`max(oldBottom, currentY + yOffset + height - 1)`.

Pure color setter **`0x4ce250`** is tested on all256 input bytes. Code1
restores the previous row. Other inputs first save the current row even if
their dispatch makes no color change. Its selected rows are:

| Codes | Row pointer(s) |
|---|---|
| 2–7 | `0x6d3050`, `58`, `60`, `68`, `70`, `78` |
| 8 | `0x6d3090` |
| 14–17 | `0x6d3098`, `a0`, `a8`, `b0` |
| 21–25 | `0x6d30b8`, `c0`, `c8`, `d0`, `d8` |
| 27–30 | `0x6d30e0`, `e8`, `f0`, `f8` |
| 31 | `0x6d3108` |

The abbreviated entries in a row retain the first pointer's high bytes.
Bounded text ignores color changes during measurement, and in its actual
visible control handler ignores later color controls when current row is
`0x6d3068`. The58 complete control-only cases discriminate that gray-row
lock without rendering glyphs or reaching imports. These native state rules
refine the contextual color names in the
[PyMS TBL control catalog](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/TBL.py#L8);
the catalog's labels alone are not native execution proof.

The highlight BYTE setter **`0x4ce240`** is real. In the bounded measured
consumer, a matching glyph immediately preceded by control4 contributes its
height but **no advance**. Other occurrences advance normally. Twenty cases
test both placements in all5 fonts. Highlighted visible overlay drawing is
not executed.

Original multi-line engine **`0x4cd9f0`**, ECX=text, EDX=line-count pointer,
composes the actual parser and bounded consumer. The45 complete measured
cases cover one/multiple/empty-middle lines with left, center and right flag
profiles in all5 fonts. It advances each line by `font.maxHeight + gap`,
resets X to clip-left, and keeps measured line extents. Center flags use
separately truncated halves of available width and measured width. Right
flags store the clip-right bound as the output's right extent. The final
cursor Y is reduced by the gap; this cursor is not the last visible glyph
bottom. Output extent fields are WORDs at`0x6d3040..46`.

Pure vertical alignment **`0x4cdbf0`**, ECX=Y pointer, uses
`remaining = clipBottom-startY` and `used = currentY-startY`, then writes
`startY-used+remaining`. With flag`0x20`, it separately halves remaining and
used with truncation toward0. Fifty complete cases include negative and odd
differences. They establish arithmetic independently of window presentation.

## Visible wrapper boundary and converter consequences

Every ordinary positive visible glyph through bounded text executes an
imported language query at **`0x4cd837`**, before the pure packet blitter.
Five cases enter the unchanged wrapper with real glyphA and stop immediately
before that instruction. They verify width/height/stride preparation and an
unchanged bitmap. They are **prefixes**, distinct from the4,460 complete native
glyph-buffer results. Font setup accounts for the other5 prefixes. Neither
the imports nor language-specific OS drawing are stubbed. Main layout wrapper
`0x4cdf20` is recorded statically and remains unexecuted here.

The converter can now validate plaintext FNT headers/tables, retain
unsigned bearings and per-byte advances, decode masks independently of RGB,
preserve byte string-table structure and apply the measured wrap/alignment
rules. It should keep measurement and drawing as explicit consumers with
their own control semantics. Atlas advances must not be recomputed from
bitmap widths alone, and positive-skip mask0 must not become an extra pixel.

Remaining R07 work includes effective encrypted-font equivalence, actual
font/color-bank bootstrap, all visible-layout/clipping paths, Unicode/locale
adapters, complete menu/briefing presentation, portraits and WAV/SMK timing.
The [briefing VM](native-briefing-loader.md) and pure layout contracts do not by themselves
prove presentation timing or full visual parity.

## Reproduction

Use the x86_64 runtime documented in [reproduction.md](reproduction.md).
The existing read-only [resource extractor](../../tools/research/inspect_native_pixel_assets_117.py)
can read the14 declared paths into private scratch in one invocation; its
`--paths` argument is comma-separated MPQ paths. Preserve the exact archive
hashes and precedence from the manifest. Its arguments are `--app`,
`--stormlib`, `--scratch`, `--retail-manifest`, `--paths` and `--out`.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_text_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-text-assets \
  --asset-manifest docs/research/evidence/native-text-assets.json \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --out docs/research/evidence/native-text-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_text_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-text-assets \
  --asset-manifest docs/research/evidence/native-text-assets.json \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --out docs/research/evidence/native-text-probes.json \
  --execute-original-code
```

The code hook admits only reviewed native ranges and has a2,000,000-instruction
bound per invocation. Inputs and expected buffers are authored on the host;
unchanged original instructions generate actual outputs. The evidence's
runtime is x86_64 Python3.9.6, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26.
