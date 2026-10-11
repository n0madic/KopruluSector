# Original tileset assets and campaign terrain references

All eight original tilesets were read from the hash-verified five-archive overlay. The [asset report](evidence/terrain-assets.json) records **40 effective physical resources**: CV5, VX4, VR4, VF4 and WPE for each tileset. The [reader](../../tools/research/inspect_terrain_assets.py) verifies archive identities, full MPQ reads, resource sizes and every campaign CHK hash. Host open-source StormLib reads MPQs; no original executable or DLL executes. Retail payload is extracted to scratch outside the project.

This is **static decoding and derived pixels**, distinct from [original region/flag instructions](native-region-generation.md). Palette sample hashes prove repeatable decoding under the recorded schema, not that the original renderer has been compared with a screenshot.

## Formats and reference chain

```text
CHK MTXM tile WORD
 -> CV5 group/subtile
 -> megatile index
 -> VX4: sixteen mini-tile references -> VR4: sixteen 8x8 index grids -> WPE RGB
 -> VF4: sixteen mini-cell flag WORDs -> gameplay terrain flags and walkability
```

| Resource | Original record | Decoded storage |
|---|---|---|
| CV5 | 52 bytes | Ten metadata WORDs, then sixteen megatile WORDs at byte20 |
| VX4 | **32 bytes** | Sixteen little-endian WORD references: mini-tile index `value>>1`, horizontal flip `value&1` |
| VR4 | 64 bytes | One8×8 grid of palette-index bytes, row-major |
| VF4 | 32 bytes | Sixteen little-endian mini-cell flag WORDs, row-major4×4 |
| WPE | 1024 bytes | 256 four-byte palette entries; decoded RGB is the first three bytes |

The pinned PyMS [CV5](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/Tileset/CV5.py), [VX4](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/Tileset/VX4.py), [VR4](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/Tileset/VR4.py), [VF4](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/Tileset/VF4.py) and [palette](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/Palette.py) readers provide the schema references. The report hashes those source files. PyMS is not imported or executed by this tool.

The VX4 source also supports a separate expanded64-byte format. These original `.vx4` resources use the32-byte format. Treating them as16 dwords would change every reference after the first record. All eight original reference tables were checked against VR4 bounds.

The palette fourth byte is0 for **every entry in all eight files**. It is preserved as a raw byte in the inspection, but must not be interpreted as zero opacity when converting terrain to a visible RGB image. Original palette cycling, remaps, draw functions and rendering timing remain separate contracts.

## Complete resource checks

| ERA index | Tileset | CV5 groups | VX4/VF4 megatiles | VR4 mini-tiles | Physical CHKs |
|---:|---|---:|---:|---:|---:|
| 0 | badlands | 1,665 | 4,844 | 26,247 | 11 |
| 1 | platform | 1,513 | 3,055 | 18,787 | 9 |
| 2 | install | 1,265 | 1,431 | 7,259 | 4 |
| 3 | ashworld | 1,262 | 3,497 | 18,078 | 11 |
| 4 | jungle | 1,578 | 5,046 | 23,818 | 14 |
| 5 | desert | 1,520 | 5,810 | 32,158 | 10 |
| 6 | ice | 1,415 | 4,518 | 30,376 | 4 |
| 7 | twilight | 1,494 | 5,232 | 28,124 | 5 |

Each tileset has equal VX4 and VF4 record counts. All CV5 megatile references are inside both tables. All VX4 mini-tile references are inside VR4. The report stores exact archive/resource SHA-256, complete flag-word/bit histograms, flip counts and the three selected decoded-megatile pixel hashes per tileset. No resource has a truncated record, invalid reference or noncanonical palette length in this exact input set.

The eight tileset names/order are corroborated by pinned [OpenBW resource loading](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L22327). Locale and alternate installations remain outside the measured archive profile.

## Campaign terrain audit

All **68 physical CHKs** contain exactly one ERA, DIM and MTXM section. ERA is0..7 and each MTXM length equals `width*height*2`. The corpus includes the64 catalog resources and four additional legacy resources; it is not68 sequential player missions. [Campaign provenance](campaigns.md) retains the distinction.

The tool traverses **1,045,504 stored tile cells**. Every referenced CV5 group/subtile and its megatile is in bounds. It records per-map dimensions, CHK/MTXM hashes, unique tile/group counts, megatile histograms and CV5 metadata-word histograms. These checks validate the actual retail references, rather than merely allocating synthetic tables with large enough bounds.

No raw MTXM cell in this corpus has bit`0x8000`; the maximum raw value is26635. The group decoder still retains the original static mask`0x7ff`. High-bit behavior requires a synthetic native boundary case rather than being claimed from a campaign occurrence.

Stored terrain references alone do not include subsequent doodad state changes, tile replacements, native flags/visibility initialization or collision contours. The complete native import is a separate original function; neither this reader nor successful reference validation claims that it has run.

## Native import anchors and remaining contracts

Local1.17 static inspection identifies **`0x445720`** as a pure map-to-megatile/flag initializer. It reads map dimensions, tile WORDs at global`0x666458`, CV5 at`0x6ce650`, group count at`0x666968`, megatile flags at`0x666450`, and writes map flags through`0x666454` plus megatile WORDs through`0x54323c`.

Its instruction sequence masks group with`0x7ff`, uses the low four subtile bits, obtains CV5 megatile WORD at`group*52+20+subtile*2`, masks the CV5 second WORD with`0xd8fa` before shifting left16, and combines it with the generated megatile flags. A raw tile high bit`0x8000` also marks the output megatile's high bit and adds map flag`0x400000`. It clears the existing map flag's high WORD before ORing, preserving low visibility/exploration bits. Invalid group returns0 after earlier cells may already have been written.

Those statements are **static instruction recovery**, awaiting a dedicated original-code batch. They are not native execution claims from the MPQ audit. The function has no imported calls in its reviewed body, so all68 authentic MTXM/CV5 inputs can be checked in private memory. Native pixel-height lookup at`0x445690` also reads a nonzero alternate tile layer through`0x66696c`, otherwise the ordinary layer, then examines VF4 bits4/2. Its full input/override contract and doodad interaction remain separate.

Other remaining R04 work includes complete CV5 metadata meanings and buildability overlay, raw chunk/default/duplicate semantics, native import failure state, dynamic terrain/doodad producers, terrain collision contours, original renderer/palette comparison, and campaign graphs generated from those authentic inputs. [Completion ledger](completion-ledger.md) retains those requirements.

## Reproduction

```sh
arch -x86_64 /usr/bin/python3 tools/research/inspect_terrain_assets.py \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --manifest docs/research/evidence/retail-manifest.json \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --scratch /private/tmp/koprulu-terrain-data \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out docs/research/evidence/terrain-assets.json
```

Scratch must remain outside the project. Unknown archive/CHK identities and incomplete reads fail before publication. The report records script, manifest, schema-source and host StormLib hashes. The reader creates no game/remake build, image export or parity certificate.
