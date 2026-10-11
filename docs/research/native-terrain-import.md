# Native terrain import and pixel elevation, StarCraft 1.17

This contract applies to the supplied executable with numerical version `1.17.0.1` and SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. It connects the [original tileset asset audit](terrain-assets.md) to the flag/class inputs used by [region generation](native-region-generation.md). Original payload remains in scratch outside the project.

The [probe tool](../../tools/research/probe_native_terrain_import_117.py), [instruction witnesses](evidence/native-terrain-import-static.json), and [execution evidence](evidence/native-terrain-import-probes.json) distinguish complete original functions from allocation-dependent fragments. The complete original game and campaign loader were not executed. There are no helper stubs or Windows imports in these probes.

## Inputs and native storage

| Address | Meaning established by these routines |
|---|---|
| `0x5264b4`, `0x5264b6` | Map width and height, unsigned WORDs |
| `0x666458` | Ordinary tile WORD array, packed row-major at map width |
| `0x66696c` | Alternate tile WORD array used by the elevation query |
| `0x6ce650`, `0x666968` | CV5 payload pointer and DWORD record count |
| `0x66644c`, `0x666448` | VF4 payload pointer and WORD megatile count |
| `0x666450` | Generated DWORD flag array, indexed by megatile |
| `0x54323c` | Output megatile WORD array, packed row-major |
| `0x666454` | Mutable DWORD tile flags, packed row-major |
| `0x69b584` | Region object, whose class raster has a fixed 256-WORD row stride |

CV5 records occupy 52 bytes. Their sixteen megatile references start at byte20. The flags consumed by the importer are the WORD at byte2. These are offsets demonstrated by original instructions, independent of names used by external editors.

## Generated megatile flags

The original suffix `0x445562` runs after allocation in function `0x445540`. It reads all sixteen VF4 WORDs per megatile, counts the low walk/elevation bits, and writes one DWORD. The allocation call itself is outside the execution scope.

The bit formula is already specified in [region generation](native-region-generation.md). This batch executes the suffix on **all33,433 original VF4 records**, across all eight tilesets, and compares every output DWORD to that formula. The original CV5 and VF4 inputs are hash-checked against the asset report. No authored substitute for a megatile record is used in these eight cases.

## Complete MTXM/CV5 import

Function `0x445720` completes without external calls. It visits exactly `width * height` cells in row-major order. For each tile WORD `raw`:

1. Compute `group = (raw >> 4) & 0x7ff` and `subtile = raw & 15`.
2. If `group >= cv5_record_count`, return0 immediately, before writing this cell. Previously processed cells remain changed; later cells retain their old values.
3. Copy the CV5 megatile WORD at `group * 52 + 20 + subtile * 2` into the output megatile array.
4. Clear the high WORD of this cell's DWORD flags, preserving the existing low WORD.
5. Read `cv5_flags = CV5[group].word_at_byte2`, retain `cv5_flags & 0xd8fa`, and shift it left16.
6. OR that value and `megatile_flags[megatile & 0x7fff]` into the cell flags.
7. If `raw & 0x8000`, set bit15 in the output megatile WORD and OR `0x00400000` into the cell flags.

Return1 after processing all cells. A zero width or height also returns1. The routine does not validate the selected CV5 megatile reference against VF4 count; the separate asset audit establishes valid references for the supplied data.

Equivalent implementation contract:

```lua
-- All reads use unsigned little-endian storage; bit operations use 32 bits.
for i = 0, width * height - 1 do
    local raw = tile_words[i]
    local group = band(rshift(raw, 4), 0x7ff)
    if group >= cv5_count then return false end
    local mega = cv5[group].megatiles[band(raw, 15)]
    megatile_ids[i] = mega
    tile_flags[i] = bor(
        band(tile_flags[i], 0xffff),
        lshift(band(cv5[group].flags_at_byte2, 0xd8fa), 16),
        megatile_flags[band(mega, 0x7fff)])
    if band(raw, 0x8000) ~= 0 then
        megatile_ids[i] = bor(mega, 0x8000)
        tile_flags[i] = bor(tile_flags[i], 0x00400000)
    end
end
return true
```

This is specification pseudocode, not a tested Defold implementation. The importer preserves whichever low flag WORD the caller supplied; these probes do not establish the initial fog/exploration state produced by the complete map loader.

## Authentic campaign inputs

The batch executes the complete importer on the exact `MTXM` payload of **all68 audited CHKs**, containing **1,045,504 cells**, with the original CV5/VF4 selected by `ERA`. Every output flag and megatile WORD is compared with the independent record/bit formula. Evidence records per-map hashes of the original CHK and native outputs.

The low flag WORD is explicitly initialized to `0xffff` as a fixture. The subsequent class suffix at `0x490e9f` consumes the native flags and fills the original fixed-stride raster; every class is compared with the existing class formula. This suffix starts after walk-grid initialization and is labeled a fragment. It does not execute the region builder in this batch.

All supplied MTXM cells have bit15 clear. Consequently, the high-bit contract comes from synthetic distinguishing cases, not an observed authored campaign use. There are64 catalog maps and four additional legacy CHKs; the count68 is not a number of sequential missions.

Six synthetic map cases additionally distinguish the retained low WORD, cleared old high WORD, CV5 mask, high tile bit, and failure at cell0,1,31,63. On failure, whole-buffer comparisons verify the unchanged rejected cell and suffix. The importer is not transactional.

## Complete pixel elevation query

Function `0x445690` takes signed pixel coordinates in `ECX=x`, `EDX=y` and returns0,1,2. Its tile coordinate division truncates toward zero. It reads the alternate tile WORD for that cell, falling back to the ordinary array only when the alternate WORD equals0. Zero therefore cannot represent a distinct replacement for an ordinary nonzero tile.

The query selects the CV5 group using `(raw >> 4) & 0x7ff`, and the CV5 subtile using `raw & 15`. It then reads the VF4 WORD at:

```text
megatile * 16 + ((y >> 3) & 3) * 4 + ((x >> 3) & 3)
```

The arithmetic shifts in this minitile expression are signed. If VF4 bit2 (`0x4`) is set, return2. Otherwise return bit1 (`(vf4_word >> 1) & 1`). Walkability bit0 and very-high bit3 do not change this return value. Bit2 takes precedence when bit1 is also set. Other high VF4 bits are ignored.

Unlike the map importer, this query does not mask the CV5 megatile WORD with `0x7fff` before indexing VF4. Original inputs have in-range CV5 references; a remake must not infer another normalization from the importer's separate behavior. There is no coordinate/group/reference bounds check inside this helper. The observed callers' validity contract remains separate from arithmetic outside the valid map.

The batch includes256 authentic-input height queries:32 on one representative map per tileset, spanning all sixteen minitile positions and ordinary/alternate selection. Another64 synthetic calls exercise every low VF4 bit combination, ignored high bits, raw tile bit15 masking, and the alternate zero sentinel. These are complete native returns, not height estimates from megatile classes.

## Evidence and limits

The batch records **405 cases**, **397 complete original-function returns**, and **82 explicit fragment completions**. Cases may compose several invocations; case and invocation counts are distinct. Private x86 memory uses the known original image, an exact instruction-address/size allowlist, real return/stack checks, and a five-million-instruction limit per invocation.

The following remain separate: CHK dispatch/allocation and native defaults; alternate-array producers; dynamic doodad/tile mutations; walk-grid and contour producers on authentic maps; complete original region graphs and local movement; terrain rendering/palette effects; and live campaign setup. This batch establishes the import/elevation formulas and their supplied-data outputs without closing those consumers or the complete terrain domain.

## Reproduction

After reproducing the [terrain asset extraction](terrain-assets.md), execute with the authorized private x86 runtime:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_terrain_import_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --static-out docs/research/evidence/native-terrain-import-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-terrain-import-probes.json
```

Without `--execute-original-code`, only the executable identity and reviewed instruction witnesses are inspected. Unknown executable or terrain/CHK hashes fail before the associated guest execution.
