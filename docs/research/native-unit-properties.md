# Native 1.17 UNIT property application and start locations

The target is `StarCraft.exe` 1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. All addresses are preferred VAs; subtract `0x400000` for RVAs. The [probe evidence](evidence/native-unit-properties-probes.json) records exact instructions, RVAs/file offsets, input hashes, script hash and execution environment. The [probe script](../../tools/research/probe_native_unit_properties_117.py) executes original EXE callees without stubs or imports.

The batch passed **1750 isolated original-instruction checks**: 211 synthetic boundary/filter cases, 1215 distinct property inputs derived from the campaign data, and all 324 physical campaign start-location records. The property routine executes as a complete function on deliberately inert unit/sprite fixtures. No unit is created by the native allocator, and no map or game is executed.

The script audits **21,427 UNIT records across all 68 physical CHKs**. Of these, 20,557 match the selected property-input contract and reduce to 1215 distinct argument tuples, including map-overridden HP/shield maxima, resources and zero-count hangars. The other records comprise 324 start locations, six positive-count hangars and 540 with active special states. All 68 maps contribute eligible property inputs, covering 144 unit IDs. Deduplicated arguments are execution coverage of those scalar contracts, not 20,557 native calls or proof that every audited record is created during a game.

## UNIT wrapper and property record layout

The native UNIT section loader at `0x468f30` requires a multiple of 36 bytes. It allocates/copies the input, then visits records in order. For each record it handles start locations, checks player/controller and mode eligibility, creates the selected unit through `0x469460`, and applies its properties through `0x46a420`. It also builds relation lists for later processing and performs final unit initialization. These allocation, creation, relation and finalization paths are static evidence, not executed by this probe.

The native property function receives `ECX=unit` and `EDX=record+12`. Its property pointer starts with two u16 fields combined as a u32: valid abilities in the low half and valid properties in the high half. The layout agrees with pinned [PyMS `CHKUnit.load_data`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionUNIT.py#L40).

| Field relative to property pointer | Meaning |
|---|---|
| `+0`, u16 | Valid special states: cloak, burrow, in transit, hallucinated, invincible |
| `+2`, u16 | Valid properties: owner, HP, shields, energy, resources, hangar |
| `+4`, u8 | Owner |
| `+5`, u8 | HP percentage |
| `+6`, u8 | Shield percentage |
| `+7`, u8 | Energy percentage |
| `+8`, u32 | Resource amount in CHK |
| `+0x0c`, u16 | Hangar count in CHK |
| `+0x0e`, u16 | Requested special states |

Fixture preparation uses the effective units.dat (`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`) and decoded map HP/shield overrides. The override-copy contract was separately executed in [native map overrides](native-map-overrides.md). This preparation is host-side input construction; it does not stand in for a native DAT or complete CHK loader. Native upgrade arrays are explicitly zeroed, sprites are not visible, and unit building-status bits are clear to exclude connected UI/order branches.

For resource records, the fixture preselects the mineral animation corresponding to the requested amount. The real mineral helper therefore runs its threshold computation and unchanged-animation path, without invoking sprite mutation. This choice is recorded as a fixture limitation, rather than represented as a substituted sprite helper.

## HP, shield and energy arithmetic

Property application at `0x46a420` checks the high-half validity bits independently. Missing validity preserves the fixture's existing field. Native synthetic cases distinguish absent validity, each individual property and combined properties for percentages 0, 1, 50, 100, 101, 128 and 255, with ordinary and hero maximum-energy states.

For HP, validity `0x00020000` causes a 32-bit multiplication of the u8 percentage by the DAT/map maximum fixed-point HP, unsigned division by 100 and a minimum of **one raw fixed-point unit**, which is `1/256` HP. The actual setter `0x41bb50` then stores and caps it at the maximum. `0x46a452`–`0x46a45b` contains the minimum selection; `0x41bb5d`–`0x41bb6b` contains the store/cap. A converter must not replace that minimum with one whole HP. The probe covers normal campaign maxima; general signed/overflow behavior for artificial maxima outside this range remains separate.

For shields, the routine calculates `floor(max_shields * percentage / 100)`, caps it at maximum shields, and shifts left eight bits before writing unit offset `+0x60` when validity `0x00040000` is set. The calculation uses the shield-amount array directly; this body does not inspect the separate shield-enabled DAT flag. The isolated fixture therefore tests the array value even for unit types whose normal creation would initialize shields differently.

For energy, the original helper `0x434a80` calls maximum-energy routine `0x434940` and returns its integer value. Hero flag `0x40` selects 250; otherwise the routine checks type-specific upgrades and normally returns 200 when the fixture's upgrade levels are zero. The property calculation multiplies by the percentage, divides by 100, then **truncates the result to its low byte before the maximum cap**:

```text
energy_integer = min(max_energy, floor(max_energy * percentage / 100) & 255)
energy_fixed = energy_integer << 8
```

`0x46a4bb` uses `movzx esi,dl`, followed by the real maximum-energy call and comparison. Native cases confirm that maximum 200 with percentage 128 produces zero, because the quotient 256 truncates to byte zero; maximum 250 with percentage 128 produces 64. These high-percentage cases are synthetic boundaries, not a claim that the supplied campaigns rely on them. A generic percentage clamp would change the original behavior.

Invincibility is set only when both the valid-ability and requested-state bit `0x10` are present. The native routine ORs `0x04000000` into unit status at `+0xdc`. The selected fixture begins with status zero; this verifies setting and validity gating, not clearing a previously invincible unit.

## Resource amounts and zero-count hangars

With resource validity `0x00100000`, the complete property routine additionally checks DAT resource-container flag `0x2000`. If both are present, instruction `0x46a4f6` reads the resource value's **low u16**, despite the CHK field being u32, and `0x46a4fc` stores it to unit `+0xd0`. Native cases distinguish resource validity/container gates and values 0, 1, 249, 250, 499, 500, 749, 750, 65535, 65536 and `0xffffffff`. Accepted 65536 stores zero; `0xffffffff` stores 65535. Missing the DAT flag preserves the fixture's existing amount.

The real predicate `0x424380` recognizes mineral types 176–178. Those types invoke threshold helper `0x4247b0`, selecting animation 13 below 250 resources, 14 below 500, 15 below 750 and 20 otherwise. A matching current animation at unit `+0xd2` prevents the sprite-animation callee from running. This preselected-animation path is used by the fixture; visual changes when the current animation differs still require separate execution. All 3806 valid-resource records are included in the campaign-derived argument set.

With hangar validity `0x00200000`, the native routine takes the **low byte** of the CHK u16 count. Real helpers `0x41cc20` / `0x41cba0` identify the child unit and capacity. Carrier IDs 72/82 use Interceptor 73 and capacity 4 or 8; Reaver IDs 81/83 use Scarab 85 and capacity 5 or 10. Hero status and the relevant upgrade select higher capacity. This batch executes the zero-request paths with those real helpers and empty fixture upgrade levels; no child allocation occurs. All 58 zero-count campaign hangars are included. The six positive-count records remain creation dependencies:

| Campaign resource | Zero-based UNIT record | Parent type | Owner | Requested count |
|---|---:|---:|---:|---:|
| Expansion Zerg05 | 375 | Reaver 83 | 1 | 10 |
| Expansion Zerg05 | 455 | Carrier 72 | 1 | 8 |
| Expansion Zerg05 | 465 | Carrier 72 | 1 | 8 |
| Expansion Zerg07 | 60 | Reaver 83 | 5 | 10 |
| Protoss07 | 161 | Reaver 83 | 0 | 5 |
| Protoss07 | 255 | Reaver 83 | 0 | 5 |

The evidence retains each record's hash. The actual creation branch clamps the requested low byte to capacity before calling `0x424900`, finalizing the child and attaching it to the parent; those child-creation calls were inspected statically and not executed.

## Start-location extraction and initial camera

Helper `0x46a280` recognizes unit ID 214, Start Location. It copies the record's pixel x/y to the owner-indexed pair `output + owner*4` and returns one, preventing normal unit creation for that marker. Non-marker IDs return zero.

For the ordinary local-player path, when marker owner equals local player `0x508d50`, it writes initial camera tile coordinates:

```text
camera_x = max(0, (pixel_x >> 5) - 10)
camera_y = max(0, (pixel_y >> 5) - 6)
```

All 324 physical campaign markers execute the full original helper. The fixture fixes ordinary-mode global `0x67cc90=0` and local player zero. Matching markers update camera globals `0x5264b0`/`0x5264b2`; others preserve their initial sentinel. The evidence records each marker's source record hash and coordinates. Another static branch uses `0x67cc90` and an existing camera sentinel to choose an update; its mode semantics and player/camera setup remain dependencies.

## Native mode filtering

Pure helper `0x469600` returns zero when mode byte `0x672949` is clear. When set, it rejects records whose owner is not neutral slot 11, and permits only neutral IDs 176–178, 188, 89, 90, 93–96. Those IDs correspond to mineral fields, Vespene Geyser and selected critters under the ordinary unit-ID schema. Fifty-six native synthetic cases cover both mode-byte values, neutral/nonneutral owners, accepted IDs and adjacent/rejected IDs. The routine's predicate is confirmed; assigning its mode byte a complete UI/game-mode lifecycle still requires its callers.

The surrounding UNIT wrapper has additional filters: owner slots below eight need eligible runtime controller values, and a UMS/computer branch checks an availability bit from the unit DAT flags. Those filters and the relation/creation helpers were inspected statically, not exercised as one full loader here.

## Unexecuted dependent branches

The complete property-function calls intentionally exclude the following inputs and callees:

- Mineral sprite changes when the computed resource animation differs, and positive-count hangar child creation. Resource storage/unchanged-animation and zero-count hangar paths execute as described above.
- Active cloak, burrow, in-transit and hallucination states. They call eligibility, sprite-animation, order and status helpers. Missing requested-state/validity combinations execute the no-action paths, but the active dependent paths are outside the selected contract.
- Unit ID 40, Broodling, which applies its initial lifetime at the end of the function. There are no such excluded records in this physical campaign inventory. Its timer helper is static evidence here.
- Visible-sprite HP refresh and building damaged-overlay/order updates. The inert fixture prevents them; its real HP setter still runs.
- Native unit allocation, placement, UMS creation filters, player conversion, Nydus/addon links, finalization and save/restore.

These exclusions are explicit per map in the evidence. Full original UNIT loading and campaign behavior must validate these connected mechanisms; successful scalar normalization is not a unit-creation or campaign parity claim.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_unit_properties_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --execute-original-code \
  --out docs/research/evidence/native-unit-properties-probes.json
```

The script verifies the EXE, DAT and every CHK against recorded hashes and the PyMS checkout against the pinned commit. The broader local extraction and execution setup is documented in [reproduction](reproduction.md).
