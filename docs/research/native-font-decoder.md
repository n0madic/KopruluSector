# Effective encrypted-font equivalence and native crypto leaves — StarCraft 1.17

The five effective encrypted **StarDat.mpq `font\*.fnt`** resources decode
to the corresponding **BroodWar.mpq `files\font\*.fnt`** plaintext bytes
**plus exactly one trailing NUL**, byte for byte. This is now a measured
resource equivalence for these exact inputs. It closes the corpus-identity
boundary recorded in the unchanged [text report](native-text.md).

The proof uses **396 complete original-function public-vector cases and five
explicit host composition checks**, all passing. There are no prefixes,
helper stubs, external imports, process startup, original UI or full-game
execution. The full original decoder/key wrappers are **not executed**.
The host reproduces deterministic format orchestration; unchanged complete
native hash/IDEA leaves produce actual intermediate transformations. Every
private native intermediate also matches an independently calculated host
prediction, without publishing key-derived bytes or digests.

The target is `StarCraft.exe` 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
VAs use ImageBase `0x400000`; subtract it for RVAs.
[Static evidence](evidence/native-font-decoder-static.json) records20 blocks,
1,254 instructions, VA/RVA/file offset and original bytes.
[Probe evidence](evidence/native-font-decoder-probes.json) records401 checks,
698 observed instruction addresses,4,297 native entry visits,166,807 checked
native writes and the five file comparisons. Its count fields distinguish
396 complete native cases from5 `host_composition_cases`; they are neither
complete decoder calls nor prefixes.

The new [inspector](../../tools/research/inspect_native_font_decoder_117.py)
has SHA-256 `3d1b022f56dc010df257fc4fb9d16d21ef23f7854e90a3ae6365ccd683d75a79`.
The [probe](../../tools/research/probe_native_font_decoder_117.py), SHA-256
`638d23f7a3ac88c9efc72d670b220e5d4805210b0c10aaa9b34d2c6fb4c61631`, pins that
inspector and records its own exact SHA-256 in the evidence. Earlier published
scripts, reports and resource manifests remain unchanged.

## Exact input/output equivalence

The [private-input manifest](evidence/native-font-decoder-assets.json) records
11 resources: one encrypted key carrier, five effective encrypted fonts and
five plaintext font references. Archive hashes and precedence are checked by
the existing read-only extractor. No resource payload is redistributed.

| Font | Encrypted bytes | Decoded bytes | Plaintext reference bytes | Comparison |
|---|---:|---:|---:|---|
| font8 | 6,472 | 6,444 | 6,443 | Exact reference + NUL |
| font10 | 7,752 | 7,719 | 7,718 | Exact reference + NUL |
| font14 | 13,128 | 13,115 | 13,114 | Exact reference + NUL |
| font16 | 13,384 | 13,316 | 13,315 | Exact reference + NUL |
| font16x | 20,168 | 20,107 | 20,106 | Exact reference + NUL |

Hashes of the complete decoded outputs, including that NUL, are:

- font8: `e99d4ef2b5519a3a10bc93f1ccdd733e9b9b18c610a283f8e6f6a35f6f762695`.
- font10: `d3a6ef19afa5592c6198bcde4b6e48b0823b81bed666cd262bbd4d6c1567edd9`.
- font14: `d111cf01a9f4ac58c1743b3c5d33ea030f0fbce53b35fff383c2deff2530fa80`.
- font16: `100376d3ebb77ec0c2938e98f29bf0fa68b2b9c613dd79ef531f272703f9a27e`.
- font16x: `28967e4a334b41e348192991a6d4ace52e66040ceb6772c356a4eccddc31e8d9`.

Every footer checksum matches the **actual original native hash state**.
The footer comparison itself is performed on the host following the recorded
native instructions; it is not reported as native execution of the decoder's
comparison branch. Equality is a direct byte comparison against a separate
original file resource, in addition to output hashes. It applies to the five
pinned pairs, not automatically to other installations or languages.

The effective-font glyph data therefore matches the inputs used by the earlier
31,457-case text batch. The one extra trailing NUL does not alter any FNT
offset, glyph metric or packet payload. This resolves that batch's specific
source-corpus uncertainty; it does not turn its visible-wrapper prefixes into
complete renderer or UI results.

## Why full native decoder execution is excluded

Original decoder **`0x4119f0`**, ECX=buffer, EDX=encrypted size,
stack=`[keyCString]`, `ret4`, calls key preparation **`0x411c40`** on accepted
nonnull inputs. Key preparation invokes CRT `srand 0x4d14f5` and
`rand 0x4d14d4`. Both call thread-context helper`0x4d744c`→`0x4d7464`, whose
first external import is at **`0x4d7466`**, through IAT`0x4eb17c`.
Later `time 0x4d11e1` imports through IAT`0x4eb2a8` at **`0x4d11ea`**.
The fixed-key producer **`0x448f40`** has the same CRT/time dependencies.
Their instructions and exact import prefixes are published statically.

No mapped CRT thread context, mocked TLS, substitute clock or import-return
stub is supplied. Decoder`0x4119f0`, preparation`0x411c40`, fixed-key producer
`0x448f40`, acquisition/validation`0x4635d0`, CRT rand/srand and the imported
prefixes are all outside the execution allowlist. Isolating the deterministic
resource transform does not establish the original wrappers' complete process
side effects, allocation behavior or key-validation interface.

The time result does not feed the resource key bytes: in `0x448f40` it is used
to reseed CRT **after** the fixed bytes have been generated; `0x411c40` similarly
generates its112 fixed bytes before calling time and reseeding. The next
resource preparation again starts with its explicit fixed seed. This static
data flow permits a deterministic offline resource transform without executing
the clock/TLS interfaces. The host composition models the key bytes used by
the resource transform, not the surrounding CRT state side effects.

## Deterministic preparation and encrypted container contract

CRT rand's recorded recurrence is
`state = (state*0x343fd + 0x269ec3) mod2^32`, returning
`(state>>16)&0x7fff`. Resource preparation takes the return's low byte.
This is the **CRT generator**, separate from gameplay RNG.

The fixed-key producer seeds`0x150b`, takes19 nonzero low bytes, skipping
zero draws, and appends a NUL for a20-byte C-string buffer. Native acquisition
`0x4635d0` uses that fixed key to decode the encrypted `font\font.gid`
carrier. This batch composes the carrier decode privately and uses its first
C-string payload for the subsequent font resources. It publishes only the
encrypted carrier's source hash and a successful checksum-match fact. No
decoded key, key hash, private hash state or private IDEA schedule is emitted.
Owner resources are not inputs to this batch.

Preparation recorded in `0x411c40` performs these deterministic steps:

1. Generate112 CRT low bytes using seed`0x4fa7`.
2. Repeat the nonempty key C-string to fill64 bytes. Initialize hash context0
   and transform that block, obtaining20 bytes of five little-endian words.
3. XOR each of the112 generated bytes with the digest byte at`index%20`.
4. Set up three IDEA contexts from the first three16-byte segments of that
   mixed buffer. Decoder mode0 uses inverse schedules.
5. Reinitialize three hash contexts and transform the same64 mixed bytes
   at offsets48..111 into each.

The valid encrypted container has64-byte blocks followed by an8-byte footer.
`size>8` and `(size-8)%64==0` are required. For each zero-based block index:

```text
mask = current 20-byte hash state snapshot
if blockIndex % 8 == 0:
    transform all eight 8-byte chunks with IDEA context0
else:
    take the encoded block directly
decodedByte[j] = (transformedByte[j] - mask[(63-j)%20]) mod256
if blockIndex % 16 == 0:
    update hash context0 with the full decoded64-byte block
```

The footer requires byte4=0 and its first DWORD to equal the first DWORD of
the final hash state. Logical decoded length is
`blockCount*64 + footerByte5 -64`. Original`0x4119f0` then clears crypto
contexts on the success path. The host performs that format comparison and
length arithmetic, while complete original helpers generate the hash and
IDEA results. It also invokes the actual context-clear routines.

There are951 encoded64-byte font blocks in the five files. They require976
IDEA eight-byte transforms and62 incremental decoded-block hash updates.
The private carrier and public vector tests add their own calls; complete
native entry counts are recorded separately in the evidence.

This composition validates the accepted resource path. It is **not** a
drop-in implementation of every failure-side-effect path of`0x4119f0`:
for example the native wrapper prepares keys before rejecting invalid
container geometry. Malformed-size cleanup ordering and key-interface
validation are not executed here. Valid inputs are pinned by file hashes.

## Actual pure native hash and IDEA contracts

Hash context records at **`0x6a60a8 + context*0x5c`** contain five state
DWORDs, counters and a64-byte block area. Complete helpers are:

| VA | Contract |
|---|---|
| `0x49d0b0` | ECX=context; initialize five words and counters |
| `0x49d010` | ECX=context, EDX=output; snapshot20 bytes |
| `0x49d050` | ECX=context, EDX=64-byte input, stack=[optional digest output]; ret4 |
| `0x49d0f0` | Actual block-transform core called by the wrapper |
| `0x49cff0` | Clear all276 bytes through real native memset |

The state constants are`67452301/efcdab89/98badcfe/10325476/c3d2e1f0`.
The transform reads16 little-endian DWORDs and expands to80 by XOR of words
at distances3/8/14/16, with **no extra schedule rotation**. Its four20-round
groups use the recorded boolean functions/constants. The apparent rotations
use `sar` plus `shl`/`or`, not unsigned rotate instructions:

```text
rotate(value,bits) = ((value<<bits) | (signed32(value)>>(32-bits))) mod2^32
```

Consequently a stock SHA library is not a substitute. The independent host
reference uses these exact arithmetic semantics. Twelve complete public hash
cases cover zero/FF/ramp/reverse-ramp blocks in all three contexts with
sequential state updates. Every private key and font-block hash call is also
checked in memory against that reference; its outputs are not published.

IDEA context schedules at **`0x66cb70 + context*0x68`** hold52 WORDs.
The native implementation reads key/block words little-endian, combines
addition/XOR with multiplication modulo65537 where WORD0 represents65536,
performs eight six-subkey rounds, and finishes with four output subkeys.
Complete helpers are:

| VA | Contract |
|---|---|
| `0x4652c0` | Expand16-byte key into52 little-endian subkey WORDs |
| `0x465190` | Build inverse schedule through actual modular inverse helper |
| `0x465400` | Multiplicative inverse for the WORD representation |
| `0x465380` | ECX=context, EDX=mode, stack=[16-byte key]; ret4; mode0 inverse, nonzero forward |
| `0x465360` | ECX=context, EDX=8-byte input, stack=[8-byte output]; ret4 |
| `0x464fb0` | Actual eight-round block transform with supplied schedule |
| `0x465340` | Clear all312 schedule bytes through real native memset |

The264 complete inverse cases cover0..255 and eight word boundaries. Twenty-four
schedule cases cover three contexts, forward/inverse modes and four public
keys, including zero and extreme WORDs. Ninety-six complete block cases use
those schedules and four public inputs. The host independently computes
schedule expansion/inversion and all block outputs. Private resource calls
receive the same comparisons without intermediate publication.

Actual clear helpers execute **`memset 0x4d0c60`** with the declared ordinary
REP profile, not a host replacement. Each of the six private carrier/font
compositions ends with both native context clears and verifies that all312
IDEA and276 hash bytes are zero.

## Reproduction and remaining UI scope

Use the x86_64 runtime in [reproduction.md](reproduction.md). Read the11 exact
paths from the manifest with the existing
[read-only extractor](../../tools/research/inspect_native_pixel_assets_117.py),
placing retail bytes only in private scratch. Then run:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_font_decoder_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-font-decoder-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_font_decoder_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-font-decoder-assets \
  --asset-manifest docs/research/evidence/native-font-decoder-assets.json \
  --out docs/research/evidence/native-font-decoder-probes.json \
  --execute-original-code
```

The hook admits only reviewed complete native crypto helpers, with a1,000,000-
instruction bound per invocation. Native writes are restricted to private
buffers/stack and the two declared crypto-context arrays. Runtime is x86_64
Python3.9.6, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26.

The converter can use the proved plaintext font corpus for this
installation or perform the deterministic offline transform privately. Keys
must remain converter-local and absent from published manifests/assets.
This report closes exact font-resource identity and transform mechanics;
visible text/clipping, font/color bootstrap, locale adapters, dialog/briefing
presentation, portrait selection, WAV/SMK decoding and timing remain separate
R07 contracts. Full original decoder process behavior remains outside the
execution scope.
