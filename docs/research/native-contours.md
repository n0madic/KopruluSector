# Native terrain collision contours, StarCraft 1.17

Original contour geometry agrees byte for byte with the separately expressed comparison model on **30 synthetic inputs and all68 authenticated maps**. Native sorting also executes. This is **98 contour-core fragments and490 complete helper returns**, not98 complete contour-builder calls. The allocation-dependent core starts after its four initial allocations and stops before freeing the walk buffer. A separate12-case batch executes108 growth fragments across explicit allocation/free boundaries.

Target: StarCraft.exe1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. Evidence: [contour instructions/tables](evidence/native-contours-static.json), [contour cases](evidence/native-contours-probes.json), [growth instructions](evidence/native-contour-growth-static.json), [growth cases](evidence/native-contour-growth-probes.json), [contour probe](../../tools/research/probe_native_contours_117.py), [growth probe](../../tools/research/probe_native_contour_growth_117.py).

## Inputs and execution boundaries

The walk grid uses a fixed1024-byte row stride. Bit7 marks blocked cells; low bits1/2/4/8 mark blocked top/right/bottom/left directions. The producer, border rules and authentic input hashes are covered in [region generation](native-region-generation.md) and [authentic campaign regions](native-campaign-regions.md).

The contour batch reconstructs each walk grid from hash-verified CHK/CV5/VF4 inputs, then checks its **entire byte hash** against the previously recorded original producer output before execution. These are identical recorded-native inputs; the contour invocation does not rerun the producer in the same emulator instance. No original assets are copied into the project.

Original context initialization `0x488df0` returns completely. Traversal executes `0x48a88e` through stop address `0x48acc8`, including the original array clears and scanner `0x48af50`. Four complete native `qsort` calls at `0x4d4490`, using original comparator `0x48ad40`, sort the generated arrays. Imported allocation/free, shrink routine `0x48b600`, and the complete outer builder are outside this execution. Every executed basic block is checked against reviewed instruction boundaries; a dedicated instruction hook stops at the fragment boundary even when it lies inside a block.

The context has four array pointers at+0x00..0x0c, four WORD counts at+0x10..0x16 and four WORD capacities at+0x18..0x1e. Complete initialization writes `[1,-1,-1,1]` at+0x28..0x2e. Private buffers, stack and PE memory are explicit fixtures. No helper stub, Windows API, full game or guest host-file/network operation executes.

## Traversal and eight-byte records

The scanner aligns its X cursor down to a four-cell boundary and skips four-byte groups whose low direction bits are all clear. It wraps rows and stops after returning to its initial scan position. Within a candidate group, blocked or fully consumed cells are skipped. Traversal takes the first blocked direction in top/right/bottom/left order and consumes direction bits by XOR as it follows the boundary.

The original lookup tables determine endpoints at pixel coordinates `cell*8 + offset`, with offsets including **-1,0,7,8**. Map-edge records can therefore contain signed coordinate-1. Treating these WORDs as unsigned changes their sort position and later geometry. The complete native tables are recorded in the static report; they agree with the pinned [OpenBW contour construction](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L20669-L20802).

Each record is `int16 axis, int16 start, int16 end, uint8 direction, uint8 info`. For directions top/right it records the forward endpoint order; bottom/left reverse the interval endpoints. Top/bottom use Y as axis and X as interval; right/left use X as axis and Y as interval. Direction is0..3 in that order. Six low info bits encode the two corner states and the two traversal modes. With current direction `d`, initial mode `m`, and next-walk mode `n`:

```text
f0 = (d XOR (2*m) XOR ((NOT (2*d)) AND 3)) AND 3
f1 = (d XOR (2*(n XOR (d AND 1))) XOR 1) AND 3
top/right:  info = f0 OR (f1<<2) OR (m<<4) OR (n<<5)
bottom/left: info = f1 OR (f0<<2) OR (n<<4) OR (m<<5)
```

The original clears new storage before traversal, so the upper two info bits start at0 in these cases. Every unsorted record, including info bytes, and every mutated walk byte matches the comparison model. After traversal all low four walk bits are consumed. This establishes geometry for the covered inputs; it does not establish how local movement interprets corner bits.

The native comparator reads the first two coordinates as **signed16-bit values**, comparing axis then start. End, direction and info do not break ties. Verification checks complete record preservation and nondecreasing signed keys after native sorting. It does not impose stable ordering on equal keys.

## Campaign size and buffer growth

All68 authentic maps produce **543,976 records** in total; the largest single direction contains6735. The native initial capacity is5000 per direction. Three inputs exceed that initial capacity:

| Map | Top | Right | Bottom | Left |
|---|---:|---:|---:|---:|
| Brood War Terran07 | 5622 | 5729 | 5859 | 5752 |
| Brood War Terran08 | 5194 | 5078 | 4933 | 5049 |
| Brood War Zerg10 | 6392 | 6556 | 6735 | 6571 |

The published geometry batch supplies an explicit **15000-capacity fixture** to each direction. It does not execute growth during traversal. A5000-capacity exploratory invocation stopped when it reached the unreviewed growth dependency; no passing geometry report was published for that invocation.

Original growth `0x48b040` increases **all four capacities by2500**, even when only one array has reached its limit. Each new allocation requests `8*new_capacity` bytes, clears the entire capacity through original `memset`, copies only `8*count` valid bytes through original `memcpy`, frees old storage, then publishes the new pointer. Counts remain unchanged. The native allocation-size calculation sign-extends the updated capacity; the positive5000..15000 fixtures do not prove wrap/negative-capacity behavior.

The12-case growth batch covers old capacities5000/7500/10000/12500 with empty, nearly full and full arrays. Each allocation prefix stops **before** its imported allocation call. Each copy suffix receives a declared private-storage/EAX/stack fixture and stops **before** imported free. Later publication is another explicitly separated fragment. Full copied payload, cleared unused bytes, unchanged old storage, count preservation, requested byte sizes and publication instructions are checked. The original CPU copy flag2 selects its genuine REP MOVSB implementation. **No complete growth function return is claimed**, and allocator failure/free effects are not inferred from the supplied storage.

## Reproduction

Use the x86_64 Rosetta runtime described in [reproduction](reproduction.md):

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_contours_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --campaign-regions-report docs/research/evidence/native-campaign-regions-probes.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data --capacity 15000 \
  --static-out docs/research/evidence/native-contours-static.json \
  --execute-original-code --out docs/research/evidence/native-contours-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_contour_growth_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --static-out docs/research/evidence/native-contour-growth-static.json \
  --execute-original-code --out docs/research/evidence/native-contour-growth-probes.json
```

Default mode reads/disassembles the hash-gated EXE without executing it. Runtime is Python3.9.6, Unicorn2.1.4, pefile2024.8.26 and Capstone5.0.6. Limits are100,000,000 instructions per contour invocation and2,000,000 per growth fragment. Both reports record script/dependency hashes and explicit run boundaries.

Local path construction, collision/repath consumers, dynamic terrain updates, imported allocator failures and full native world initialization remain in the [completion ledger](completion-ledger.md). The geometry and growth contracts above supply inputs for that work; they do not establish unit movement or played campaigns.
