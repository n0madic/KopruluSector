# Spike 10: data access, async loading, archive compression, Lua heap

Plan: phase 0 item 10, D3, D4, §4.1, §4.3. Code: `spikes/10-data/`.
- `bench.lua` runs from the command line and inside Defold.
- `data.script` is the Defold part.
- `make_mission.py` builds a mission-sized container.

## Method

- **Read cost.** One million u16 values (2 MB) are read sequentially and at random positions by four readers:
  - `string.byte` from a string;
  - a Lua SoA table of numbers;
  - a Defold buffer stream (`buffer.get_stream`), Defold only.

  All readers must agree on a checksum.
- **Loading.** `sys.load_buffer_async` loads two files:
  - a synthetic 3.0 MB mission container (`make_mission.py`), with sections sized like the largest stock mission and filled with structured contents: MTXM 128 KB, WALK 1 MB, TRIG 1 MB, CONT, RGNS, MASK, STRS, UNIT;
  - an 8 MB incompressible fixture.

  Then all sections are extracted to strings. The Lua heap is measured after a full GC.

## Results

| ms per 1M u16 | LuaJIT JIT (CLI) | LuaJIT `-joff` (CLI, iOS mode) | PUC 5.1 (CLI) | Defold macOS | Defold HTML5 (wasm, PUC) | Defold Android emu | Galaxy Z Fold7 (JIT off) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `string.byte` sequential | 3.9 | 20.1 | 31.3 | 4.5 | 62 | 41.3 | 36.9 |
| `string.byte` random | 5.1 | 58.2 | 38.9 | 5.5 | 81 | 196.8 | 83.6 |
| SoA table sequential | 3.6 | 10.9 | 7.9 | 3.8 | 17 | 18.2 | 13.0 |
| SoA table random | 4.7 | 32.0 | 15.9 | 5.3 | 38 | 114.9 | 45.0 |
| buffer stream sequential | n/a | n/a | n/a | 28.0 | 66 | 41.6 | 48.7 |
| buffer stream random | n/a | n/a | n/a | 30.7 | 89 | 66.4 | 80.0 |
| unpack string → SoA (1M) | 2.4 | 14.6 | 36.6 | 7.5 | 79 | 68.6 | 29.6 |
| SoA memory for 1M numbers | 8 MB | 8 MB | **16 MB** | 8 MB | **16 MB** | 8 MB | 8 MB |

Observations:
- The Android emulator's numbers look like interpreter numbers: its random access is several times slower than its sequential access.
- Defold's Android engine runs LuaJIT with the JIT off (checked with `jit.status()` on the phone), so the Android columns are interpreter numbers.
- Every buffer-stream element access is a C API call. With the JIT on (macOS) it is about 6× slower than `string.byte`. On interpreters the two are comparable: on the phone sequential streams are 1.3× slower and random access is equal; on the emulator random streams were faster. Streams win nowhere by enough to change the decisions below.

| Loading (Defold) | macOS | HTML5 | Android emu | Galaxy Z Fold7 |
|---|---:|---:|---:|---:|
| `load_async` 3.0 MB mission | 35.9 ms (first load, cold) | 5 ms | 7.4 ms | 3.9 ms |
| `load_async` 8 MB fixture | 2.6 ms | 13 ms | 17.5 ms | 14.9 ms |
| Extract 8 sections | 0.2 ms | 1 ms | 0.4 ms | 0.8 ms |
| Heap after load + extract | +5.9 MB | +5.9 MB | +5.9 MB | +5.9 MB |
| Heap with sections only | +2.95 MB | +2.96 MB | +2.95 MB | +2.95 MB |

**Archive compression.** The HTML5 archive for 3.0 MB mission + 8.4 MB random fixture + small files is 9.4 MB (`dist/spikes/10-data/web/.../archive`). So the structured mission compressed to about a third. Defold compresses custom resources itself, so the container keeps no internal compression (§4.1 confirmed).

## Decisions (feed into §4.3)

- **Byte code and strings** stay as Lua strings read with `string.byte`, as planned. Buffer streams are never the access path from Lua.
- **Hot DAT columns** are unpacked once into SoA tables. Random access is 2–4× faster than `string.byte` on interpreters. The cost is 8 bytes per value on LuaJIT and 16 on PUC/HTML5, so only hot columns are unpacked.
- **Large grids** (WALK, CONT, creep, visibility) stay packed strings. They are mutable at runtime, so a mutable grid needs a dedicated representation, decided in phase 3 with this table. On HTML5 a 1 MB grid as a table would cost 16 MB.
  - Lua side: packed rows or chunked tables.
  - Native side (D19): a buffer is the natural form.
- **Heap after a mission load.** The resident form is about the file size (sections as strings). The transient peak is twice that until the file string is dropped. Budget target for §10: *mission file size × 2 peak, × 1 resident*, plus unpacked SoA columns.
- `loader.lua` already drops nothing implicitly. The mission loader must release the container string after extraction (`c.data = nil` in the spike).

## Not measured

- iOS. The LuaJIT `-joff` column is the stand-in for the iOS interpreter.
- A mid-range Android device (the phone is a flagship).
- A real largest mission (the converter for CHK comes in phase 1). The synthetic sizes are upper estimates.
