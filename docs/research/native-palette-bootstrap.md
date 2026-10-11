# Native all-tileset palette bootstrap, RGB math and UI preparation — StarCraft 1.17

**2,346 cases passed: 2,301 complete original returns and 45 explicitly bounded
prefixes.** The batch executes unchanged native palette code and real SSE math
in private memory, observes 672 distinct instruction addresses, and uses no
helper stub or external import. RGB byte-buffer results are measured; device
presentation, visible rendering and full visual parity are not.

The target is `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Preferred VAs use ImageBase `0x400000`; subtract it for RVAs. The
[static evidence](evidence/native-palette-bootstrap-static.json) records 28
reviewed blocks and 1,320 instructions with VA/RVA/file-offset/bytes, including
explicit unexecuted boundaries. The [probe evidence](evidence/native-palette-bootstrap-probes.json)
records each input, whole-buffer prediction, return or exact stop VA, resource
hash and runtime. Original process startup and the full game never execute.

The new [inspector](../../tools/research/inspect_native_palette_bootstrap_117.py)
has SHA-256 `08f274227cc18f2bfe5cca649bab96fe6c4575e6866d8e9589ec2f9d21f33d58`;
the [probe](../../tools/research/probe_native_palette_bootstrap_117.py) has
SHA-256 `e72e61d29d71acf4a9250906e9cdaa658dddd80a6f12c9ed45948a3b60bef2a2`.
They pin the stable pixel-effects inspector. Previous publications remain
unchanged; this report refines their palette boundaries with new evidence.

## All eight native definition pointers

The selector `0x46a390` reads the tileset WORD at `0x5264bc`, obtains the native
definition through `0x5089b0 + tileset*4`, and invokes real bootstrap
`0x46a3b0`. Definitions consist of an active-mask WORD, padding, eight-byte
records `(patternPointerDWORD, speedBYTE, lowBYTE, highBYTE, countBYTE)`, and a
zero-speed terminator. Native bootstrap clears all eight runtime records at
`0x6d2f88`, initializes fields through actual `0x4cd000`, and enables records
through `0x4cd070` according to the native active mask.

| Tileset ID / resource name | Definition VA | Mask / simple ranges | Evidence source |
|---|---|---|---|
| 0 Badlands / `badlands` | `0x50894c` | 7: 1–6, 7–13, 248–254 | Initialized PE data |
| 1 Space Platform / `platform` | `0x66fd40` | 0 / empty | Zero-filled mapped PE profile |
| 2 Installation / `install` | `0x66fd40` | 0 / empty | Zero-filled mapped PE profile |
| 3 Ashworld / `ashworld` | `0x50898c` | 7: 1–4, 5–8, 9–12 | Initialized PE data |
| 4 Jungle / `jungle` | `0x50894c` | 7: 1–6, 7–13, 248–254 | Initialized PE data |
| 5 Desert / `desert` | `0x508970` | 3: 1–13, 248–254 | Initialized PE data |
| 6 Arctic / `ice` | `0x508970` | 3: 1–13, 248–254 | Initialized PE data |
| 7 Twilight / `twilight` | `0x508970` | 3: 1–13, 248–254 | Initialized PE data |

All initialized records have speed 8 and null pattern pointer/count, so these
are simple rotations. The exact initialized definition hashes, including
terminators, are:

- `0x50894c`, 36 bytes: `68a6b142a9b5817d0c109cd5e9e3cd19e87123f50c5c94cb2dc4304da21c2541`.
- `0x50898c`, 36 bytes: `0ad47a2fd8922fa1a7488c90ba6ad010e89a44482bbe310ef13c8d95bb3eb12e`.
- `0x508970`, 28 bytes: `8483df345461909cf47ffb2ae823abb70b21cbb5c4d8d5cd705253904bacbcb2`.

The [earlier palette-effects report](native-pixel-effects.md) said that other
tileset pointers were runtime BSS. This was too broad: **six tilesets use three
initialized PE definitions; only Platform and Installation point to
`0x66fd40`.** The latter twelve mapped bytes are zero, and native bootstrap and
membership return the empty results in this explicit mapped-PE profile. This
batch does not execute a producer or prove that the twelve bytes remain zero
after every original loader. Their initial zero state is not promoted to a
post-loader invariant.

Twenty-four complete bootstrap cases start the runtime cycle array with three
different marker bytes for each tileset and compare all 128 resulting bytes.
They execute the actual selector, reset, setters and enables. Original
`0x4697c0` tests whether an index lies in a definition's inclusive cycling
ranges. It compares the full signed argument, without BYTE truncation. Another
2,072 complete cases cover all eight tilesets and indices −1 through 257.

## Stock WPE rotation and native scheduler boundaries

All eight 1,024-byte stock WPE files come from the
[private palette input manifest](evidence/native-palette-bootstrap-assets.json).
It records extraction source, archive precedence and hashes; no WPE payload is
published. Unlike an authored pattern, each measured simple-cycle test uses
its own tileset's effective WPE bytes.

Fifteen complete positive leaf cases execute native `0x4cccf0` and actual
backwards overlap-safe `memmove 0x4d2360`. Inclusive low..high colors rotate
right by one DWORD. Every complete palette buffer and unchanged cycle record
matches the prediction. The [earlier batch](native-pixel-effects.md) separately
covers advanced pattern fixtures; these initialized stock definitions do not
contain advanced patterns.

Each tileset then executes seven complete `0x4ccd30` driver invocations from
its bootstrap counter 8. Active counters descend to 1 with palettes unchanged;
the driver performs native low/high passes, with limits 128 and 256. These are
isolated invocations, not a display-clock cadence proof.

On invocation eight, the two mapped-empty definitions complete. The six
positive definitions execute their low-range rotations and stop **before
`0x4cf8c0`**, because the scheduler requests actual device submission after
that pass. Five separate positive high-range cases call real scheduler
`0x4ccbc0` with limit 256 and stop at the same boundary after rotating 248–254.
These eleven scheduler cases are prefixes, not complete driver returns. The
counter reload, native palette writes and exact untouched ranges are measured;
the upload is neither bypassed nor replaced.

## Complete original RGB brightness adjustment

Native `0x4cf760` has ABI ECX=destination, EDX=source,
stack=`[numberOfPaletteEntries]`, `ret 4`. Brightness comes from `0x50f2ec`, set
by real `0x4cf930`. That option accepts unsigned 60–140 and otherwise stores
100, as established by the previous batch.

At brightness 100 the original function copies all four bytes of every entry
through actual `memmove 0x4d16a0`. At other supported values, it transforms each
of the first three bytes independently:

```text
exponent = (200 - brightness) / 100
output = trunc(256 * pow(input / 256, exponent))
```

The executable uses SSE double arithmetic and real CRT helper `0x4d5c70`, then
`cvttsd2si` and a low-BYTE store. It does not use host math as a replacement for
the native call. Host `pow` supplies a separately calculated byte prediction,
which the original output matches for the declared inputs. Destination byte 3
is preserved at non-100 brightness; identity copying instead copies source
byte 3. This is a four-byte palette-record contract, not a claim that the fourth
byte is visible alpha.

The actual SSE helper checks MXCSR mask bits and x87 exception masks before
choosing its fast path. The fixture declares MXCSR `0x1f80`, FPU control
`0x027f`, and ordinary aligned/nonoverlapping buffers. It runs the unchanged
helper and its initialized constant tables from the same PE. Its fallback at
`0x4d67c4`→`0x4e0824`, exceptional `pow` inputs and errno paths remain outside
this execution profile.

130 complete RGB cases include all 81 supported integer brightness settings
with all 256 grayscale BYTE inputs; every stock tileset WPE at brightness
60/80/100/120/140; and counts 0/1/7 at selected levels. Every case compares all
1,024 destination bytes and verifies the complete source is unchanged. The
non-identity destination begins with declared byte `0xa7`, so preservation of
its fourth bytes and unused entries is observable. This closes RGB buffer math
for the supported option/input domain, independently of palette presentation.

## Original upload, grayscale and black-fade preparation

Twenty prefix cases enter actual upload wrapper `0x4cf8c0` and stop **before
device adapter `0x4cfd60`**. The original wrapper applies the real gamma function
when required, using a stack temporary offset by first palette index. Tests
cover first/count `(0,256)`, `(1,13)`, `(248,7)` and `(0,0)`, at five brightness
settings. The measured adapter input pointer, first-index argument and RGB
payload match the expected source slice or gamma-adjusted stack slice. Temporary
byte 3 starts as explicit `0xa7`; its initialization outside this isolated call
is not claimed. Even count zero reaches the device-call boundary.

Native grayscale preparation `0x4ccf40` computes each component **with a
separate truncation**, then adds the three results:

```text
gray = floor(77*R/256) + floor(151*G/256) + floor(28*B/256)
```

Thus `(255,255,255)` gives 253, rather than truncating the weighted total once.
The routine copies source byte 3, writes the complete grayscale target palette
at `0x6d2b88` through original `memmove`, and invokes fade with argument zero.
Twelve cases use all eight WPEs and four full BYTE channel-isolation palettes.
They stop **before clock-dependent fade entry `0x4ccd60`**, after the native
conversion and target copy. The current palette remains unchanged; every target
entry and the fade argument are measured. These are positive preparation
prefixes, not full timed-fade returns.

Black-fade preparation has flag BYTE `0x50f0f0`:

- `0x4cce50` returns completely when the flag is zero. Otherwise it clears the
  flag and current palette, then requests upload. Its positive test stops
  before `0x4cf8c0`; later presentation, clearing the from-palette and fade are
  unreached.
- `0x4cceb0` returns completely when the flag is nonzero. Otherwise it sets the
  flag, clears target palette and copies current to from-palette `0x6d2788`,
  then invokes fade. Its positive test stops before `0x4ccd60`.

The two guards and two positive preparation cases check all three palette
buffers and the exact state flag. No clock or presentation callback is supplied
as a stub. Native fade `0x4ccd60` statically interpolates RGB from
`0x6d2788` to `0x6d2b88`, caps its step argument at 10, performs signed
integer division and invokes `KERNEL32!GetTickCount` before its first loop.
That interpolation/time loop is static evidence here and is not called a
complete native execution result.

## Proof boundary and remaining dependencies

The 45 prefixes are precisely 11 cycle-update, 20 upload-preparation,
12 grayscale-preparation and 2 black-fade-preparation cases. Complete returns
are counted separately. Device adapter `0x4cfd60` reaches Storm through
`0x4d0106` / IAT `0x4eb57c` (ordinal 357); no device call executes.
Clock fade and presentation entry `0x4c5360` remain unexecuted. These boundaries
are preserved in every affected case's stop VA.

Pinned [Teippi palette record declaration](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/image.h)
and [game palette reset](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/game.cpp)
provide structural reference names. Original 1.17 tables/instructions and the
measured cases above establish the version-specific contracts. Reference-source
rendering does not replace native RGB byte proof or an original visible capture.

Still separate are the post-loader empty-definition invariant for Platform and
Installation; actual fade timing/interpolation execution; device/palette
presentation; full UI rendering/composition; and a controlled visible comparison
with Defold. The palette transformations can be implemented and compared as
small CPU lookup tables/buffers before coupling them to Defold rendering. A
matching native byte buffer alone does not certify the displayed result.

## Reproduction

Follow the local architecture/runtime procedure in
[reproduction.md](reproduction.md). Use the unchanged pixel asset extractor with
all eight `tileset\<name>.wpe` paths listed in the safe manifest, retaining the
WPEs in private scratch. Then run:

```sh
arch -x86_64 "$NATIVE_PYTHON" tools/research/inspect_native_palette_bootstrap_117.py \
  --exe "$RETAIL_EXE" --out /private/tmp/palette-bootstrap-static.json
arch -x86_64 "$NATIVE_PYTHON" tools/research/probe_native_palette_bootstrap_117.py \
  --exe "$RETAIL_EXE" --assets "$PALETTE_ASSETS" \
  --asset-manifest docs/research/evidence/native-palette-bootstrap-assets.json \
  --execute-original-code --out /private/tmp/palette-bootstrap-probes.json
```

Measured runtime is x86_64 Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6 and
pefile 2024.8.26. Each complete call validates actual return and stack cleanup;
each prefix validates its exact unexecuted-callee boundary. Script, dependency,
manifest and effective input hashes are recorded in evidence.
