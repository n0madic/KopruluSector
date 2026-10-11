# Original indexed GRP blitters and palette lookups

This batch executes complete original StarCraft **1.17.0.1** indexed-pixel functions against private, guarded bitmap buffers. It verifies GRP RLE decoding, mirroring, supplied clipping, player/selection color overrides, shift lookup, source/background blending and masked background remapping. The observations are actual writes made by original instructions to the supplied indexed buffers. They are not screenshots, RGB display observations or proof of the complete renderer.

Target executable SHA256: `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Preferred base `0x400000`; RVA is VA minus that base. Public 1.16.1 addresses are not substituted for this exact target.

## Evidence

- [Inspector](../../tools/research/inspect_native_pixel_blitters_117.py) and [static report](evidence/native-pixel-blitters-static.json): **19 blocks, 1,353 decoded instructions**, including the 18-entry normal/mirrored drawing dispatch inventory. Two resource-loader wrappers are static only.
- [Probe](../../tools/research/probe_native_pixel_blitters_117.py) and [results](evidence/native-pixel-blitters-probes.json): **9,005 complete passing original calls**, **1,034 distinct executed instruction PCs**, zero prefixes, helper stubs or imports. Each call checks original return and stack cleanup. Every drawing case compares the complete bitmap and guard bytes with an independent prediction; measured bitmap/state hashes are saved instead of retail pixels.
- [Pixel asset reader](../../tools/research/inspect_native_pixel_assets_117.py) and [provenance](evidence/native-pixel-assets.json): **13 checked retail resources** extracted only to private scratch. [Existing GRP provenance](evidence/native-image-assets.json) supplies the recorded 23 distinct GRP paths and 2,940 frames. Both manifests carry the same [retail manifest](evidence/retail-manifest.json) hash.

Probe/runtime/script/dependency hashes are in the generated evidence. Runtime is x86_64 Python 3.9.6 under Rosetta, Unicorn 2.1.4, pefile 2024.8.26 and Capstone 5.0.6. No retail process, graphics API, Windows import, native archive loading or full game loop executes.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_pixel_assets_117.py \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --scratch /private/tmp/koprulu-pixel-assets \
  --retail-manifest docs/research/evidence/retail-manifest.json \
  --out docs/research/evidence/native-pixel-assets.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_pixel_blitters_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-pixel-blitters-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_pixel_blitters_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --assets /private/tmp/koprulu-image-assets \
  --asset-manifest docs/research/evidence/native-image-assets.json \
  --pixel-assets /private/tmp/koprulu-pixel-assets \
  --pixel-manifest docs/research/evidence/native-pixel-assets.json \
  --out docs/research/evidence/native-pixel-blitters-probes.json \
  --execute-original-code
```

The asset reader checks archive hashes and effective patch-first priority before extraction. Its optional `--paths` supplies other MPQ paths; the current probe deliberately requires the recorded Badlands/profile resources. [Reproduction](reproduction.md) describes the general environment and existing GRP extraction. Omitting `--execute-original-code` prints the reviewed plan.

## Corpus and fixture boundaries

All 2,940 recorded GRP frames execute once in each orientation: **5,880 full-frame cases**. Additional **270 cases** test cropped subrectangles and zero-height early returns using first/last recorded frames. Across this corpus the independent resource reader identifies **193,262 transparent packets, 131,449 literal packets, 26,617 repeat packets and 1,219,820 nontransparent pixel positions**. No recorded opaque pixel has color index zero. These are corpus counts, not all-game asset counts.

The bitmap is a private **320×320** indexed-byte array. A different initial destination index occurs at each successive position, cycling 0..255. Sixteen guard bytes precede and follow it. The entire array and both guards must match the independent predicted result; checking only pixels expected to change would miss stray writes. Destination coordinates and supplied source crop are valid for this fixture. The native blitter does not perform general destination-buffer bounds validation.

GRP data stays raw in scratch. A private frame header supplies its original fields and converts the raw frame data offset into an absolute pointer. The original GRP loader's in-place pointer relocation is not executed. Palette PCX decoding is an analysis-authored strict 8-bit/one-plane decoder; its outputs become supplied native lookup bytes. Native PCX/file-loading calls are not replaced by this decoder because those calls are never entered.

The recorded lookup resources are `game/tunit.pcx` (128×1), `game/tminimap.pcx` (16×1), `game/tselect.pcx` (24×1), Badlands `dark.pcx` (256×32), `shift.pcx` (256×1), and seven Badlands blend PCXs: `ofire` 256×63, `gfire` 256×32, `bfire` 256×40, `bexpl` 256×63, `trans50` 256×255, `red` and `green` 256×1. Badlands WPE is a checked 1,024-byte resource; this batch does not execute RGB palette conversion/display.

## ABI and native kernels

Drawing kernels receive **ECX=destination X, EDX=destination Y**, stack `[frame_header, source_crop, drawing_parameter]`, and pop **12**. Crop is four DWORDs `(left,top,width,height)`. The bitmap descriptor pointer is `0x6d0c40`; the descriptor supplies WORD stride and a DWORD pixel pointer at `+4`. Crop and destination have already been prepared by the caller. The [BWAPI image declaration](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/CImage.h) corroborates this callback shape; the exact local instructions independently establish it.

| Contract | Normal VA / RVA | Mirrored VA / RVA |
| --- | --- | --- |
| Foreground through current 256-entry map | `0x405e1e / 0x05e1e` | `0x4071c0 / 0x071c0` |
| Source/background blend | `0x4067f6 / 0x067f6` | `0x406f03 / 0x06f03` |
| Shadow: fixed background lookup | `0x406950 / 0x06950` | `0x40730e / 0x0730e` |
| Supplied background lookup | `0x4066a1 / 0x066a1` | `0x407064 / 0x07064` |
| Player color override wrapper | `0x40e950 / 0x0e950` | `0x40e990 / 0x0e990` |
| Shift map wrapper | `0x40ea60 / 0x0ea60` | `0x40eac0 / 0x0eac0` |
| Fire row wrapper | `0x40eb20 / 0x0eb20` | `0x40eb40 / 0x0eb40` |

The dispatch table is at `0x5030d4/0x5030d8`, stride 12. Drawing methods 0 and 1 share the normal kernels. Methods 9, 10, 14, 16 and 17 select the relevant pairs above. Selection method 13 uses `0x40e900` for **both** orientation slots; that wrapper calls the normal kernel. The [pinned PyMS drawing-method schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/ImagesDAT.py#L23-L49) supplies names, while the report records actual local destinations.

## RLE and clipping semantics

A frame header's `+4` field is an absolute pointer for these native consumers. Its payload starts with WORD line offsets; each line offset is relative to the payload base. Packets decode in this order:

```text
packet & 0x80: skip packet & 0x7f destination pixels; leave them unchanged
else packet & 0x40: repeat the following color packet & 0x3f times
else: consume packet literal colors
```

Transparency is a **packet property**. A literal or repeat color value zero is still opaque to these functions. The 18 authored-resource cases contain both encoded zero colors and transparent packets, discriminate their effects, and cut through literal/repeat/transparent boundaries. Invalid zero-length packets are excluded; they are not a supported malformed-resource recovery contract. The [PyMS raw GRP reader](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/GRP.py#L212-L275) gives an independent resource schema; the native buffer writes establish the packet distinction.

Normal drawing reads source `left + x`; mirrored drawing reads source `frame_width - 1 - left - x`. Both select line `top + y`. Source crop offsets do not independently shift destination X/Y: the caller supplies the final destination of the visible crop. Mirrored writes proceed backward across each destination row but produce the predicted left-to-right flipped output. Zero-height crops return without pixel writes. These blitters consume the crop contract established separately by [image geometry](native-image-geometry.md); the probe does not claim a complete CImage bounds→scene draw pipeline.

## Color state and temporary overrides

Normal opaque pixels write **`global_map[source_index]`**, where the 256-byte map starts at `0x501021`. It is not inherently an identity map. Player color setter `0x41d740` / RVA `0x1d740` truncates ECX to BYTE, sets current color at `0x5d6e38`, and replaces global-map indices **8..15** from `0x529056 + color*8`. Twenty-four cases test 12 prepared owner rows plus high-byte truncation.

The unexecuted native loader wrapper `0x41d570` passes output capacity **`0x60` bytes** for `game/tunit.pcx` and **`0x0c` bytes** for `game/tminimap.pcx` to `0x4444e0`. The prepared fixture uses the first 96/12 decoded bytes accordingly. This static capacity evidence is distinct from executing the file/PCX loader. The pinned [OpenBW resource reader](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/ui/ui.h#L159-L174) retains all 16 rows from the 128-byte tunit file; that implementation is not proof of native loader behavior.

When selector BYTE `0x671a88` equals **2**, the native setter chooses desired class **2 for the local player**, **7 for an allied other player**, otherwise **0**. Local player is `0x508d50`; alliance bytes are indexed `0x534914 + viewer*12 + owner`. It then searches the first eight DWORD color assignments at `0x5264fc` for the **first matching class**. A missing match leaves the desired class as the lookup row. **864 complete cases** distinguish all 12×12 viewer/owner pairs, alliance 0/1, identity/reversed assignments and no-match assignments. The report does not infer the UI meaning of other selector values.

Player override wrappers save the current color, set the requested low-byte player color, call the unchanged normal/mirrored blitter, then call the real setter with the saved value. **72 cases** verify draw output and restored map/current-color state under selector 0. Restoration under selector 2 is not inferred from these cases: the original restoration call would itself apply the selector's mapping again.

Selection wrapper `0x40e900` takes the drawing parameter's low byte, temporarily copies one eight-byte row from `0x594054` into map indices **1..8**, draws normally, then restores the two saved DWORDs. Three stock rows are checked. Shift wrappers temporarily replace **all 256 map bytes** from `0x59c1b0 + parameter*256`, draw, then restore all bytes. The stock Badlands shift file supplies one row, so the two tested complete calls use parameter **0**. Wider shift-bank parameters are not claimed valid.

## Blend, shadow and fire arithmetic

Blend kernels read source color into AH and current destination color into AL. The lookup index is therefore exactly:

```text
new_destination = supplied_table[source_color * 256 + old_destination]
```

They use the supplied table directly, rather than first applying the global foreground map. All seven recorded blend resources are tested row by row against **every background index 0..255**, in both orientations: **1,848 complete cases**. This includes **1,820 stock decoded-row cases** and **28 explicit private leading-row cases**. The 255-wide and one-wide fixture calls together cover all 256 background values without inventing a 256-wide BYTE frame dimension.

The static loader at `0x466810` reserves **`(decoded_height + 1)*256` bytes**, saves the allocation base in the palette record, then decodes PCX pixels starting **256 bytes after that base** (`0x4668b4`). The reviewed wrapper does not initialize the leading row itself. Its actual allocation-library initialization behavior remains outside this execution. The probe supplies leading row **0x5a** explicitly and labels it private. The [pinned Teippi replacement](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/init.cpp#L174-L207) deliberately fills an identity row to support its decoded-GRP path; that modified behavior is not attributed to the original.

Shadow ignores opaque foreground color and maps the current destination through the 256-byte table at **`0x66ef20`**. Transparent packets leave destination untouched. Six cases use explicitly supplied Badlands dark row 18. This establishes the complete shadow consumer; selecting row 18 during original bootstrap is still an unexecuted dependency.

Fire wrappers truncate the drawing parameter to BYTE, select **`[0x508840] + row*256`**, then tail-call the supplied background-remap kernel. `0x508840` is the first field of the `ofire` palette record and holds the table *pointer*; the wrapper reads that pointer (`add eax,[0x508840]` at `0x40eb2c`) rather than indexing the record address itself. The probe fixture already stores the decoded `ofire` bank pointer at `0x508840`, so its results stand. Corrected after the function atlas ([P03 `0x40eb20`](atlas/p03.md), record layout in [P22](atlas/p22.md)). Eighteen cases test rows 1/32/63, high-byte truncation, both mirrors and three opaque foreground choices, while preserving transparent gaps. Opaque source color serves as a mask; the transformed value depends on the old destination and selected row. The probe does not simulate the frame-by-frame producer of the fire parameter.

## Defold implications and outstanding renderer work

The offline converter can decode RLE into index plus opacity data while preserving the distinction between transparent packets and opaque zero. Use the tested crop/mirror contract, keep player indices 8..15 and selection indices 1..8 distinct, and implement source/background blend tables in the same index order. A normal RGBA sprite blend would not reproduce the verified background lookup operations by itself.

Passing native buffer checks establish these isolated kernels with checked resources and explicitly supplied state. Remaining contracts include all-image/all-tileset input coverage, native archive/PCX/GRP relocation and allocation initialization, shadow-table bootstrap, drawing methods 2..8 (cloak/effect paths), 11 (HP bars), 12 (warp texture), 15 (size outline), full draw-list sorting/traversal, destination composition, palette cycling, RGB conversion, texture upload and observed final-screen output. [Image initialization](native-image-initialization.md), [image geometry](native-image-geometry.md) and [populated Move](native-trigger-move-images.md) close selected upstream consumers without converting this buffer evidence into complete visual or campaign parity.
