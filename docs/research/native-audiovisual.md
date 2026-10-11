# Native 1.17 portrait, sound and dialog resource contracts

This batch executes the unchanged instructions of `StarCraft.exe` 1.17.0.1,
SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`,
preferred image base `0x400000`. Subtract that base from every VA below to get
its RVA. The addresses are specific to this executable.

The [native evidence](evidence/native-audiovisual-probes.json) contains **20,272
passing checks: 20,233 complete original calls and 39 partial executions**.
The partial executions are 33 duration-arithmetic fragments, three prefixes
before CRT sound randomness and three prefixes before Storm video continuation.
None is counted as a complete function. The batch visits 695 distinct original
instruction PCs and guards 112,975 guest writes. Every executed helper is the
EXE implementation; there are no substituted helpers, imports, Windows calls,
retail process launches, original movie playback or audio-device calls.

The separate [codec evidence](evidence/native-audiovisual-codec.json) checks 35
original media resources with host FFmpeg: 20 SMKs comprising 200 decoded frames
and 15 WAVs whose decoded PCM matches their RIFF data payload exactly. This is
independent codec/resource evidence, **not execution of the original video or
audio implementation**. No input media, decoded image or audio payload is
published. The published outputs are hashes, measurements and finite state
results.

The [static report](evidence/native-audiovisual-static.json) retains 42 reviewed
blocks, 1,970 instructions with VA/RVA/file offsets, DAT descriptors, resource
hashes and the actual import table. The [asset manifest](evidence/native-audiovisual-assets.json)
contains 47 privately extracted resources: three DATs, four TBLs, five BINs,
20 SMKs and 15 WAVs. Archive precedence and full archive hashes are checked by
the [generic extractor](../../tools/research/inspect_native_pixel_assets_117.py).

## Portrait resolution and campaign state

Complete resolver `0x4a65d0` takes a u16 unit type in `CX`, reads the stock
portrait array at `0x523358`, and returns the portrait in `EAX`. Its two special
cases are exact comparisons with the raw u16 state at `0x526524`:

| Base portrait | State test | Replacement word | Effective stock value |
|---:|---|---|---:|
| 38, Overmind | `state > 31` | `0x523488`, portrait array slot 152 | 35, Daggoth |
| 60, Protoss Advisor | `state >= 28` | `0x5233f4`, portrait array slot 78 | 51, Fenix Dragoon |

The 1,368 resolver checks use all 228 effective `units.dat` rows and states
0, 27, 28, 31, 32 and 65535. The replacement values above come from the actual
Patch_rt data loaded into the destinations of native DAT descriptor `0x5014f0`.
They are not guessed advisor constants. The numeric portrait names follow
pinned [GPTP PortraitId](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations/PortraitId.h).

Pinned [GPTP's 1.16.1 portrait replacement](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/interface/status_display/unit_portrait.cpp)
is a useful structural anchor, but its comments explicitly label the campaign
amount/advisor interpretations as tentative. This batch establishes the 1.17
numeric state predicate and stock array values. It does not prove the full
producer or progression meaning of `0x526524`, and does not equate either
threshold with a count of playable missions. See [campaigns](campaigns.md) for
the separate catalog/physical-map/progression distinctions.

## A separate portrait RNG and exact idle/talking choices

Portrait RNG `0x4a6e00` takes `ECX=low`, `EDX=high`, and one stack argument used
as a mask size; it returns with `ret 4`. Its three u32 states are `0x50d7cc`,
`0x50d7d0`, and `0x50d7d4`. For state `(x,y,z)`, each draw performs:

```text
x = u32(x + y)
y = u32(y + 0x71823402)
z = u32(z + x)
x = x >> 1                  // logical shift
y = y xor z
candidate = (y xor x) & (mask_size - 1)
repeat while candidate >= high - low + 1
return low + candidate
```

State writes occur after an accepted candidate. Rejected candidates still
advance the local state, which is eventually committed. The 420 complete RNG
cases check all 101 accepted idle values, rejection loops up to 16 iterations,
four range/mask profiles, overflow-bearing synthetic states and the initial
three words present in the mapped PE. Those initial words are file state,
not a claimed post-startup snapshot. This RNG is distinct from simulation RNG
and from the CRT generator used by sound choice.

Complete idle routine `0x4a6b40` uses mode `0x50d7c8 == 2` to draw **0 through
100 inclusive**, with mask size 128. Four consecutive byte arrays from native
`portdata.dat` descriptors become the cumulative weights. It compares the draw
with each cumulative total using `<=`. Thus stock weights `[85,5,5,5]` yield
86/101, 5/101, 5/101 and 5/101 of the accepted draw values. The five distinct
stock profiles are `[85,5,5,5]`, `[87,5,5,3]`, `[89,5,5,1]`, `[90,5,0,5]` and
`[100,0,0,0]`. The 505 boundary checks cover every accepted draw for all five
profiles. Another 880 checks enter the routine with all 110 stock portrait IDs.

Complete talking routine `0x4a6c50` draws 0–2 with mask size 4 in that mode.
If the draw equals previous variant byte `0x6ab3ec`, it uses `(draw + 2) % 3`.
This is a different anti-repeat rule from the sound selector. The routine
writes the resulting previous-variant byte even on the tested nonzero-handle
guard (after a real open it writes the opened variant minus one; see below). There are 3,520 checks across all 110 IDs, eight seeds and previous bytes
0, 1, 2 and 255.

These idle/talking calls run to their native returns. Their fixture supplies a
**synthetic nonzero movie-handle sentinel** and an already dirty control. The
native existing-handle check therefore skips opening a movie. The selected
variant is observed at the original PCs `0x4a6bb0` / `0x4a6c95`; no handle is
replaced by a working codec stub. This proves the tested choice arithmetic and
state changes, not that the corresponding resource is opened or shown.

The unexecuted opening path formats `portrait\` plus the TBL stem, variant
digit and `.smk`. The idle opener attempts lower variants on failure; the
talking opener has analogous fallback and ultimately stores `BL` into the
previous byte. `BL` is decremented (`add bl,0xff` at `0x4a6d0f`) after every
open attempt, including a successful one, and the loop exits on the next
handle check. After a successful open of variant `v`, `0x6ab3ec` therefore
receives **`v − 1`** (`0xff` for `v = 0`), not `v`; mode 0 without an open movie also ends at `0xff`.
The next anti-repeat comparison uses that value. Only the already-open path,
which the probes execute, stores the drawn variant itself. Corrected after the
function atlas ([P36 `0x4a6c50`](atlas/p36.md)). These are static paths here. File availability is an independent
boundary: a selected Zealot idle variant-3 name was absent from the effective
archives during extraction, whereas the 20 selected variant-0/1 files were
successfully extracted. A DAT weight does not establish that every numbered
movie exists.

## Portrait modes, timers and platform boundaries

Mode setter `0x4a6e90` clamps an unsigned requested mode to two. Five complete
checks confirm 0, 1, 2, 3 and `0xffffffff` when no portrait control is attached.
With a control and selected unit present, its refresh dependencies remain
static in this batch.

Decode gate `0x4a6700` returns zero in mode 0. In mode 1 with first-frame state
`0x6ab394 == 0`, it returns one without continuing the video. Mode 2, and mode
1 with first-frame state set, call thunk `0x4d0184`, IAT `0x4eb36c`, **Storm
ordinal 457**. The exact PE imports agree with pinned [BWAPI's Storm export
definitions](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/Storm/storm.def)
and [declarations](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/Storm/storm.h):

| Original thunk / IAT | Storm ordinal | Export identity |
|---|---:|---|
| `0x4d017e` / `0x4eb368` | 454 | `SVidPlayBegin` |
| `0x4d0184` / `0x4eb36c` | 457 | `SVidPlayContinueSingle` |
| `0x4d018a` / `0x4eb370` | 458 | `SVidPlayEnd` |

The first three decode-gate cases are complete; the three video-continuation
cases stop before the thunk. Storm itself and its codec are never executed.

Timer callback `0x4a7030`, event ID 2, executes a useful pure state transition.
Only a visible control with a nonzero movie handle enters it. When fresh flag
`0x6ab38c` is zero and fade byte `0x6ab3a1 < 80`, it adds ten to that byte.
A set fresh flag requests dirtying without the increment; fade 80 or above
does nothing. The 48 complete cases cover visibility, handle-zero/nonzero,
fresh state and fade values 0, 49, 50, 79, 80 and 255. The control already has
dirty bit one, so the real dirty helper returns before `USER32!SetRect`.
The tests do not upload or display a dirty rectangle.

Talking request `0x4a7200` is suppressed while trigger-portrait state
`0x6ab3d0` is nonzero; five duration cases execute that complete guard. Its
positive scheduling path and trigger portrait `0x4a7320` call event creator
`0x4c5960`, which allocates and reads `KERNEL32!GetTickCount`. Callback IDs
1/2/3/4 connect frame continuation, fade, talking expiry and trigger lifetime.
Their complete positive queue lifecycle, skip interaction, camera changes and
unit-display-type refresh are not closed by the guard/fade tests. See
[briefing loader](native-briefing-loader.md) for the independently recovered
UI timer node and strict deadline-expiry predicate.

## Sound choice, ownership and initialization

Sound routines `0x447130` (Yes) and `0x447190` (What/annoyed) consume the actual
`units.dat` sound ranges. Their random branches call CRT `rand` at `0x4d14d4`,
then use `sar eax,8`, signed remainder over the inclusive range, and a
previous-sound adjustment. These branches stop before the CRT call: its TLS
state is not fabricated. Static instructions show that an immediate repeat
advances to the next ID, wrapping at the range end. This should not be replaced
with the portrait anti-repeat rule.

Complete calls cover 143 fixed/absent Yes ranges, 129 fixed What ranges and all
389 valid sequential annoyed entries from stock data. The annoyed argument is
one-based: nonzero values up to the inclusive count return `first + index - 1`.
This branch does not update previous-sound global `0x66697c`. On exhaustion,
the native selector clears sequence global `0x667270` before entering its
random What fallback; one prefix tests that retained state mutation.

Full selection wrapper `0x446c30` checks quiet state, null input, replay,
ownership and the positive cooldown. All 228 stock type IDs are tested with
owners 0, 1 and 11, and replay off/on: 1,368 complete cases. Under these
fixtures, local-owner units are accepted; neutral owner 11 accepts resource
types 176/177/178/188 and critters 89/90/93/94/95/96. Other owners and replay
reset previous selection unit `0x66726c` and sequence `0x667270`. Quiet state
returns before that reset. Accepted units have the same previous pointer and
cooldown `0x667274 == 1`, so they return before choosing or playing sound.
These are predicate/state-reset contracts, not a successful selection sound.
The native accepted continuation sets cooldown to ten; decrement timing and
positive playback/talking integration remain separate.

Complete initializer `0x446800` resets six selection/silence scalars and, only
when initialization flag `0x666978` is zero, constructs three terminated u16
sound lists. Each begins with IDs 1–113, followed by the race block: Zerg list
`0x666980` 755–967, Terran list `0x666c10` 174–472 and Protoss list `0x666f50`
473–754, selected through `0x5033b8[race byte 0x5264c2]`. Corrected after the
function atlas ([P15 `0x446800`](atlas/p15.md)): the earlier order did not
match the list addresses. Two native cases check first initialization and preservation
of existing lists on the second path. Audio-device pointer `0x6aa954` is zero,
so the actual preload-list consumer `0x49e3f0` returns without loading files.

Pinned [Teippi sound.cpp](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/sound.cpp)
is comparison material, not a 1.17 oracle. It uses `MainRng` for sound choice
and updates previous sound after successful playback, whereas this target's
selector instructions call CRT and exhibit the leaf state changes above.
The source also explicitly fixes a queued-sound deletion bug; that fix must
not silently become an original-game contract.

## Cached sound duration and its original arithmetic boundary

Native cached-duration getter `0x49db30` reads u32 `0x6a61d8 + 16*sound_id`.
Complete wrapper `0x446740` returns `floor(duration / 1000) * 1000` using
unsigned arithmetic. The 5,720 cases cover all 1,144 sound IDs with values 0,
999, 1000, 1001 and `0xffffffff`. These cache values are supplied fixture state;
the device-backed producer is not claimed to have created them.

Producer `0x49da70` first asks the actual DirectSound buffer for its format
through vtable `+0x14`. Its arithmetic at `0x49daa0`–`0x49daaa` is:

```text
duration_ms = u32(buffer_bytes * 1000) / nAvgBytesPerSec
```

The product wraps before division. Thirty-three **original arithmetic
fragments**, with supplied `ESI=SoundData` and `ECX=nAvgBytesPerSec`, test the
15 genuine WAV byte lengths plus 18 overflow/rate boundaries. They stop at
`0x49daab` before the caller-frame restoration, after the real `imul`, `xor`,
`pop` and `div`. They neither call the function's entry nor obtain a format
from a device. They are included in the 39 partial checks, not the complete
calls. A zero device rate and division-fault path are outside these cases.

All 15 selected WAVs are mono, 22,050 Hz, PCM16 with 44,100 average bytes per
second. Their lengths do not overflow the product, so host resource durations
match that original arithmetic **if the device format matches the parsed
format**. That equality is conditional, not observed device behavior.

The selected-speech caller then adds sign-extended sfxdata adjustment at
`0x51dd40 + 2*sound_id` to the whole-second duration before a talking request.
Native `movsx` establishes signed consumption. Effective stock adjustments
are zero for 1,142 rows, one for one row and two for one row. Positive speech
playback, obtaining a real cached duration and successfully starting its
portrait timer were not executed as a connected chain.

## RIFF/WAVE parsing and retained failure mutations

Complete parser `0x49d830` receives `ECX=bytes`, `EDX=&remaining_size` and an
18-byte format output pointer on the stack. It requires at least 12 bytes,
subtracts 12 from the remaining count, then checks `RIFF` and `WAVE`. It does
not validate the advertised RIFF size at offset four.

Its actual chunk walker `0x49d7c0` subtracts an eight-byte header and advances
by the declared payload length. **It adds no odd-byte padding alignment.**
The parser searches forward for `fmt `, requires at least 16 bytes, copies
the six base format fields and writes the extra-format-size word as zero.
It then searches after that chunk for `data`, verifies the remaining bound,
returns the data pointer and replaces remaining size with data length.

Sixty complete checks run the 15 stock WAVs at full length, length minus one,
11 and 12. Eleven synthetic cases discriminate wrong signatures, short and
extended `fmt `, unpadded versus standard padded odd JUNK chunks, an ignored
RIFF length, data preceding `fmt `, empty data and an unsupported/zero-rate
format. The structural parser accepts the last format; it is not an audio
codec validator. Standard odd padding misaligns this native walker, while an
unpadded one-byte JUNK chunk can be skipped. Failed data lookup can retain
the copied format and a decremented size: the output mutation is checked,
not assumed atomic. Malformed pointer arithmetic, nested containers and all
audio encodings remain outside this finite set.

## Eight channels and queued sound policy

Native channel records at `0x6aa990` have stride 24, with buffer pointer `+0`,
unit-type word `+4`, flags `+6`, priority `+7`, release-on-finish byte `+8`,
sound ID `+0x0c` and playback parameters in the remaining words.

Complete selector `0x49db60` visits all eight records. Queued flag `0x08`
skips a record. A null nonqueued buffer is available, and each later available
record replaces the prior choice: **the last available index wins**. All 256
queued/null masks are checked, including all queued returning `0xffffffff`.
No COM buffer object is supplied and no status query is replaced by a stub.

The nonempty-buffer paths are static: they query DirectSound status, release
finished temporary buffers and, if no free record exists, consider playing
buffers whose priority is strictly below the incoming priority and whose
flag `0x20` is clear. The strict comparisons keep the first candidate among
equal minimum priorities. Successful preemption then calls actual buffer
Stop/Release or position reset. Those live-buffer effects are **not** proved
by the 256 complete queued/null tests.

Complete playback gate `0x49e4e0` rejects a sound with its runtime queued bit
set. All 1,144 stock sfxdata rows enter that rejection path with authentic
priority/flags plus supplied queued bit `0x08`. Device/master-volume predicate
`0x49d810` is checked in four combinations. This does not establish successful
channel admission or playback. The positive queue routine `0x49e000` sets
resource flags `0x0c`, channel queued flag and parameters, then calls
`KERNEL32!SetEvent` at IAT `0x4eb0e8`; it remains static here.

Effective stock priorities are 986 rows at one, 145 at two, 12 at zero and one
at three. The flags are retained per row in the static report. Their editing
schema is pinned [PyMS SoundsDAT](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/SoundsDAT.py),
but flag names alone do not prove all connected admission/deletion behavior.

## Native resource identity and dialog geometry

The bootstrap pointers have distinct original writers:

| Global / consumer | Original writer and input resource |
|---|---|
| `0x665de0`, getter `0x43f0c0` | `0x43f0d0` → `0x4c13e0`, filename at `0x4ec2d0` is `rez\network.tbl`; store at `0x43f0f0` |
| `0x6ab4f8`, getter `0x4a7d70` | `0x4a7eb0` → `0x4c13e0`, filename at `0x4f01ac` is `rez\stat_txt.tbl`; store at `0x4a7ec9` |
| `0x6ab388`, portrait names | `0x4a6a40` loads `arr\portdata.tbl`; its native loop resolves both 110-entry DAT ID arrays through `0x4c13b0` |
| SFX name array `0x51c270` | `0x49dac0` loads `arr\sfxdata.dat` / `arr\sfxdata.tbl` through native descriptors and the table loader |

The writers and native file-reading bootstrap are static. Their pointer
identities are not inferred merely from filenames. Complete getter calls
consume authenticated private table bytes: all 168 network strings, all
1,547 status strings and low-u16/invalid boundaries. Another 1,364 native
lookups check both 110 portrait DAT ID arrays and all 1,144 SFX associations.
Zero ID returns null; out-of-range IDs return the native empty-string pointer.
The shared [native text](native-text.md) report covers TBL/STR byte preservation,
control codes, font metrics and layout. [Font decoder](native-font-decoder.md)
establishes effective-font equivalence for the five pinned stock pairs.

The five BIN inputs are `statport.bin`, `glurdyt.bin`, `glurdyz.bin`,
`glurdyp.bin` and `glucmpgn.bin`, with 3, 22, 23, 22 and 13 linked file widgets
respectively: 83 total. The file records use the 86-byte schema in pinned
[PyMS DialogBIN](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DialogBIN.py).

Complete predicate `0x4c29a0` compares event coordinate words at `+0x0e/+0x10`
with control words `+4/+6/+8/+0x0a` using **signed inclusive** comparisons.
Complete relative-point routine `0x4c29d0` subtracts the supplied control's
origin for type zero, or its parent at `+0x32` for other types; subtraction
wraps in u16. There are 581 boundary-point cases for each routine.

These probes copy actual file bound words into declared native-layout control
fixtures. They do not execute the BIN loader. Serialized root coordinates can
be relative/unnormalized: `statport.bin` stores `[408,408,87,71]`, so that raw
root does not form a valid runtime rectangle. A converter must recover native
fixups, parent-relative positioning, responsive bounds and event dispatch
before treating these words as final screen geometry. The tests establish the
pure predicates on the declared words, not functioning original dialogs.

## SMK resource proof and its exact layer

Pinned [FFmpeg n8.0 demuxer](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavformat/smacker.c)
defines SMK2/SMK4 headers, frame-size/flag tables, optional ring frame, audio
slots, compressed trees and interval scaling. The corresponding
[decoder](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavcodec/smacker.c) implements
indexed video and palette reconstruction. Their source hashes are
`74345c3548db82fc83d49d05097168a7f6062b6bc3044b71501c80cc9ae6144d`
and `b72190b9ca14084f626de7a360e4961f8c6a896ac1ccfc4ba065077a950dd360`.

The host tool separately records installed FFmpeg 9.0.2 and both executable
hashes; it does not claim that binary was built from the pinned n8.0 files.
All 20 selected movies structurally parse and decode as 60×56 indexed video,
ten frames each, 10 fps, with no audio stream. Output hashes cover each frame's
3,360 indices and 1,024-byte FFmpeg palette representation. That palette
representation is not an observed Storm upload or original screen result.

This closes selected resource structure and independent decode viability.
Original Storm decoding, fallback/movie-handle lifetime, RGB output, rendering,
real clock cadence and audiovisual synchronization remain unexecuted.

## Reproduction and remaining connected work

The scripts are [inspector](../../tools/research/inspect_native_audiovisual_117.py),
[native probe](../../tools/research/probe_native_audiovisual_117.py) and
[separate codec probe](../../tools/research/probe_audiovisual_codec_resources.py).
Each evidence file records script/resource hashes; native evidence also records
x86_64 Python 3.9.6 and Unicorn 2.1.4. General environment and extraction details
are in [reproduction](reproduction.md). Native reproduction is:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_audiovisual_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-audiovisual-assets \
  --asset-manifest docs/research/evidence/native-audiovisual-assets.json \
  --execute-original-code \
  --out docs/research/evidence/native-audiovisual-probes.json
```

The next connected dependencies are the original BIN loader/fixups and control
callbacks, dialog theme/font/color bootstrap, skip/cancel/requeue ordering,
portrait refresh/display-type and campaign-state producers, successful Storm
movie lifetime/decoding, live DirectSound buffers/channel preemption, SFX
loading/deletion/cache lifetime, CRT selection RNG state and cooldown timing,
and positive WAV/Transmission/portrait/text composition with observed original
clock and presentation. These finite contracts do not establish complete
campaign UI parity, six-campaign playback or all R07 requirements.
