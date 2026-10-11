# Native 1.17 UMS map overrides and player availability

The target is `StarCraft.exe` 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. All addresses below are preferred VAs; subtract `0x400000` for RVAs. The [evidence](evidence/native-map-overrides-probes.json) retains exact instruction bytes, file offsets, input/output hashes, runtime versions and the probe script hash.

This batch passed **472 original-instruction checks**: 76 synthetic cases and 396 checks using real override sections from all 68 physical campaign CHKs. It contains **108 complete-function calls and 364 explicit post-initialization fragments**. Complete functions are the PUNI loader and four native per-player setters. Ten other wrappers call imported Storm initialization/error routines, so their execution begins after those calls, with a recorded synthetic stack frame. The original bounded section reader, EXE memmove and native setters execute as real callees. There are no stubs, imported routines, game launches or complete-map loads.

The reproducible [probe script](../../tools/research/probe_native_map_overrides_117.py) uses the effective `units.dat` only for subunit and weapon associations: SHA-256 `da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`, 228 entries. It seeds those three native arrays from the pinned PyMS DAT schema; it does not execute a native DAT loader or initialize the complete game. Output arrays begin with synthetic `0xaa` guards so unchanged defaults and adjacent entries can be distinguished from writes.

## Handler inventory and actual input coverage

| Section | Wrapper VA / RVA | Executed entry | Exact bytes | Entities | Physical campaign inputs |
|---|---|---|---:|---:|---:|
| `PUNI` | `0x4685d0` / `0x685d0` | Complete function | 5700 | 228 units × 12 players | 68 |
| `UNIS` | `0x4680d0` / `0x680d0` | Fragment `0x468120` | 4048 | 228 units, 100 weapons | 31 |
| `UNIx` | `0x467f40` / `0x67f40` | Fragment `0x467f94` | 4168 | 228 units, 130 weapons | 33 |
| `UPGS` | `0x468340` / `0x68340` | Fragment `0x468390` | 598 | 46 upgrades | 31 |
| `UPGx` | `0x468250` / `0x68250` | Fragment `0x4682a0` | 794 | 61 upgrades | 33 |
| `TECS` | `0x468500` / `0x68500` | Fragment `0x468550` | 216 | 24 technologies | 31 |
| `TECx` | `0x468430` / `0x68430` | Fragment `0x468480` | 396 | 44 technologies | 33 |
| `UPGR` | `0x468750` / `0x68750` | Fragment `0x4687a0` | 1748 | 46 upgrades × 12 players | 35 |
| `PUPx` | `0x468660` / `0x68660` | Fragment `0x4686b0` | 2318 | 61 upgrades × 12 players | 33 |
| `PTEC` | `0x468930` / `0x68930` | Fragment `0x468980` | 912 | 24 technologies × 12 players | 35 |
| `PTEx` | `0x468840` / `0x68840` | Fragment `0x468890` | 1672 | 44 technologies × 12 players | 33 |

The table sums to 396 native section checks. The four prerelease version-57 extras contribute PUNI/UPGR/PTEC inputs; they are not supported-version campaign loads. Each section is checked independently. Conditional native group selection and duplicate ordering remain as described in [briefing and loader contracts](native-briefing-loader.md); the inventory is not an instruction to apply every section unconditionally.

PUNI's complete native function calls the real stack-probing helper `0x4d2200`, checks UMS predicate `0x447db0`, requires exactly 5700 bytes, and invokes bounded copy `0x4c12b0`. Native synthetic cases verify undersize/exact/oversize inputs and rejection when the mode predicate fails. The other wrappers' UMS/exact-length/error paths are static evidence because their error/initialization branches call imported Storm thunks. Their fragments test the copied payload and output normalization after initialization, not a substituted implementation of those imports.

## Unit availability and the planar player layout

PUNI contains `available[12][228]`, `global_available[228]`, then `use_default[12][228]`. For player `p` and unit `u`, the original handler writes to `0x52655c + p*228 + u`, choosing the global value when the default flag is nonzero and the player value when zero. It preserves arbitrary byte values; native cases with default flags 0, 1, 2 and 255 distinguish selection from Boolean normalization of the selected value.

UPGR/PUPx use the following **planar** layout, where `N` is 46 or 61:

```text
max_level[12][N]
start_level[12][N]
global_max_level[N]
global_start_level[N]
use_default[12][N]
```

PTEC/PTEx have the same block shape with `available`, `researched`, `global_available` and `globally_researched`, where `N` is 24 or 44. Nonzero `use_default` selects the two corresponding global bytes. Zero selects the two player bytes. The loaders traverse all 12 players and every entity without clamping level/researched values to editor defaults.

This layout is directly visible in the native offsets and confirmed by output-array comparisons. For example, base UPGR starts its backward player pointer at local offset `0x450`, the end of the 24 player arrays. After decrementing by 46 for player 11, `0x4687fa` reads its start level; `0x468807` reads its max level 552 bytes earlier. Its global start and max offsets are 1150 and 1104. PTEC similarly reads availability from one block and researched state 288 bytes later (`0x4689da`–`0x4689e7`).

Pinned [PyMS `CHKSectionUPGR.load_data`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionUPGR.py#L30) and [`CHKSectionPTEC.load_data`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionPTEC.py#L30) instead read the paired arrays per player. Their PUPx/PTEx subclasses inherit that parsing. This source/native mismatch has material campaign effects:

| Section | Maps with differing selected cells under interleaved decoding | Differing cells |
|---|---:|---:|
| `UPGR` | 10 | 150 |
| `PUPx` | 29 | 1178 |
| `PTEC` | 14 | 68 |
| `PTEx` | 30 | 549 |

These are independently compared raw player/field cells after applying default selection, not gameplay outcomes. For Terran02 UPGR, zero-based player 1, upgrade 0 has native max=1/start=0; the interleaved interpretation yields max=3/start=3. The native fragment/setters wrote the planar result. The evidence retains bounded examples for each affected input. A remake importer should use the original planar layout, with a discriminating vector for each paired array family.

## Native setters and expansion gates

The complete setter calls identify the destination fields and the asymmetric expansion checks. The array names agree structurally with pinned [BWAPI `BWGame.h` player fields](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/BWGame.h#L120); its version-specific addresses were not reused.

| Setter | Base IDs and destination | Expansion IDs and destination | Expansion-enabled gate |
|---|---|---|---|
| `0x49a370`, current/start upgrade | 0–45: `0x534590 + 46*p + id` | 46–60: `0x53660c + 15*p + (id−46)` | Expansion current level only |
| `0x49a3c0`, max upgrade | 0–45: `0x534368 + 46*p + id` | 46–60: `0x536558 + 15*p + (id−46)` | None in this setter |
| `0x49a2c0`, tech available | 0–23: `0x534104 + 24*p + id` | 24–43: `0x536330 + 20*p + (id−24)` | None in this setter |
| `0x49a300`, tech researched | 0–23: `0x534224 + 24*p + id` | 24–43: `0x536420 + 20*p + (id−24)` | Expansion researched state only |

Thirty-two complete native cases exercise both sides of the base/expansion boundaries, the last supported IDs, player 11 and expansion disabled/enabled. Value 255 is stored as a byte without level clamping. No upper-ID/player bounds check appears in these setters; the importer should validate supported array indices. The asymmetry is a setter contract, not permission to load a version-205 map with expansion disabled.

## Unit and weapon overrides

UNIS/UNIx hold 228 default flags, followed by planar arrays for u32 hit points, u16 shields, u8 armor, u16 build time, u16 mineral cost, u16 gas cost and u16 name-string IDs. Their tails contain u16 weapon damage and upgrade-damage arrays for 100/130 weapons. The field schema agrees with pinned [PyMS `CHKSectionUNIS`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionUNIS.py#L40).

The native loops run unit IDs 227 down to zero. A nonzero unit default flag leaves its existing global stats unchanged. For a zero flag, the loader copies all seven fields as raw values, including fixed-point hit points, with no HP/cost clamp in the observed body. Global array bases are:

| Field | Native array VA | Element width |
|---|---|---:|
| Hit points | `0x520708` | 4 |
| Shields | `0x520540` | 2 |
| Armor | `0x521d90` | 1 |
| Build time | `0x5238b0` | 2 |
| Mineral cost | `0x523520` | 2 |
| Gas cost | `0x5236e8` | 2 |
| Name string ID | `0x524458` | 2 |

Weapon overrides are selected **through custom units**, not by blindly copying every weapon-tail entry. The loader reads the unit's first-subunit ID from `0x51fb90`; sentinel 228 uses the unit itself, otherwise it uses the subunit. It reads that resolved unit's ground/air weapon bytes from `0x5211d8` / `0x5213a8`, skips sentinel 130, and writes damage/upgrade damage to `0x525c80` / `0x525d88`. Thus a weapon referenced only by defaulted units remains unchanged. Synthetic custom-unit cases include unit 3 with subunit 4, direct unit 4, and final unit 227; whole-array guards distinguish copied stats/selected weapons from untouched entries.

For base UNIS, expansion weapon IDs 100–129 cannot be represented in its 100-entry tail. The probe rejects such associations in its expected-output preparation instead of inventing semantics; all actual campaign inputs in this batch avoid that unsupported custom-unit association. A future malformed-map compatibility contract must investigate the original out-of-tail reads separately.

## Upgrade and technology cost overrides

UPGS/UPGx contain default flags for 46/61 upgrades and six u16 arrays: mineral base/increase, gas base/increase, time base/increase. UPGx has one padding byte after its 61 flags. A nonzero default flag leaves the existing values unchanged; zero copies the six fields into arrays starting at `0x5248d0`, `0x524950`, `0x5249d0`, `0x524a50`, `0x524ad0` and `0x524b50`. TECS/TECx analogously have 24/44 flags and four u16 arrays for mineral, gas, research time and energy cost, copied to `0x51f760`, `0x51f7b8`, `0x51f810` and `0x51f868`. Native vectors test flags 0, 1 and 2 and compare all output bytes, retaining untouched higher/base-family entries.

This establishes the map override output contract. It does not establish upgrade purchase arithmetic, technology eligibility, research timing, refunds, spell energy consumption or existing-unit reaction to changed stats; those belong to the corresponding gameplay functions.

## Reproduction and dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_map_overrides_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --execute-original-code \
  --out docs/research/evidence/native-map-overrides-probes.json
```

The script hash-gates the EXE, every input CHK and effective units.dat, verifies the pinned PyMS checkout commit, and records complete-call versus fragment scope per check. General extraction/dependency instructions are in [reproduction](reproduction.md).

Remaining map-loader dependencies include native DAT baseline initialization/lifetime, imported wrapper initialization/error behavior, full malformed-length/container handling, UNIT property normalization and creation, runtime player conversion, STR/VCOD, terrain/visibility, colors and doodads. This batch closes the tested output-array contracts on the supplied campaign inputs; it does not execute those dependencies or establish complete map/campaign parity.
