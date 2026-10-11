# Trigger Move with populated stock images

This report extends [ordinary/floating Move](native-trigger-move.md) with actual stock main/shadow images and IScript execution. **768 isolated cases return from the original Move action with every assertion passing.** They connect order resumption to the image-animation consumer. They are separate from the [2,171 grounded Move cases](native-trigger-grounded-move.md), authored campaign-parameter coverage, rendered frames, and full-game execution.

The executable is StarCraft **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. All addresses below are preferred VAs; the static evidence records RVAs and instruction bytes. No 1.16.1 address is transferred to this target.

## Evidence and reproduction

- [Probe](../../tools/research/probe_native_trigger_move_images_117.py), SHA256 `d734e8379b71c74711dcb7f4b0e3a4a0672b56938bec7caac045ab56f4e3fc5f`.
- [Execution evidence](evidence/native-trigger-move-images-probes.json): **768 complete cases, 3,598 distinct executed instruction addresses**, no helper stubs or imported functions.
- [Static evidence](evidence/native-trigger-move-images-static.json): **368 reviewed blocks, 17,477 decoded instructions**, with exact source dependency hashes. This includes the existing Move/order/placement dependencies and the full VM instruction range; it does not establish every VM opcode's runtime semantics.
- [Retail manifest](evidence/retail-manifest.json), [image/GRP/LO provenance](evidence/native-image-assets.json), and [stock image initialization](native-image-initialization.md) provide the input and dependency boundaries.

The runtime is x86_64 Python under Rosetta with Unicorn 2.1.4. The report records Python, architecture, Capstone and pefile versions, target/source/data hashes and the reviewed instruction bound. Original code runs unchanged in private emulator memory. No Windows process, WinMain, full game loop, guest file/network operation or Windows import executes. Host file reads prepare inputs before guest execution.

After reproducing the retail data and image-asset extraction described in [reproduction.md](reproduction.md), run:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_move_images_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --assets /private/tmp/koprulu-image-assets \
  --asset-manifest docs/research/evidence/native-image-assets.json \
  --static-out /private/tmp/move-images-static.json \
  --out /private/tmp/move-images-probes.json \
  --execute-original-code
```

Paths are explicit CLI inputs. Omitting `--execute-original-code` prints the plan; the original execution requires the exact supported executable hash and x86_64 runtime. Retail DAT, IScript and GRP bytes stay in private scratch; project evidence contains hashes, selected decoded instructions and measured states.

## Stock inputs and private state

The probe copies the complete raw units/flingy/sprites/images DAT columns into the executable's own descriptor destinations, checking both file hashes and consumed byte lengths. Orders uses the previously reviewed interruptibility/highlight/subunit-inheritance columns. It installs the hash-verified retail IScript file at a private address and prepares original GRP frame headers and selected LO pointers from the asset manifest.

Existing CUnits, owner/global/spatial lists, flat terrain, visibility, image capacity and order capacity are private fixtures. Each existing sprite receives real images through `0x4151f0 → 0x40fc50/0x40fb50 → 0x411730 → 0x410480`, including the stock shadow created by Init. Startup runs with the selected unit as IScript context; both context pointers are then cleared before Move. The batch does not execute normal CUnit creation/admission. Goliath and Tank are explicitly body-only fixtures with null subunit pointers.

The first-wait programs used to prepare current animations are real stock entries. Every selected entry is checked nonzero before execution. Unsupported animation entries are excluded; interpreting file offset zero as an empty animation would be incorrect for this VM.

| Unit ID/type | Stock sprite / main image | Prepared current animations | Cases |
|---|---|---|---:|
| 0 Marine | 235 / 239 | GroundAttack 2, AirAttack 3, Walking 11 | 144 |
| 2 Vulture | 244 / 256 | GroundAttack 2, Walking 11 | 96 |
| 3 Goliath body | 232 / 234 | Walking 11 | 48 |
| 5 Tank body, tank mode | 240 / 250 | Walking 11 | 48 |
| 7 SCV | 239 / 247 | GroundAttack 2 | 48 |
| 8 Wraith | 238 / 243 | GroundAttack 2, AirAttack 3 | 96 |
| 37 Zergling | 159 / 54 | GroundAttack 2, Walking 11 | 96 |
| 41 Drone | 140 / 17 | Walking 11 | 48 |
| 43 Mutalisk | 151 / 38 | Walking 11 | 48 |
| 65 Zealot | 201 / 151 | GroundAttack 2, Walking 11 | 96 |

Each animation profile varies Complete status, the destination tile's walkability bit, seeds `0`, `12345`, `0xffffffff`, enabled/disabled RNG, and presence/absence of a queued Move order 6. The queued order is allocated through real `0x4295d0`, with target `[999,222]`. Source position is `[64,64]`, destination `[400,400]`; AI, path and child links are null.

These **768 consumer-state cases add no authored campaign parameter cases**. The earlier Move reports separately test the 56 stored Move records and grounded-eligible subset with their actual DIM/MRGN/owner/action parameters. Private animation states are not claimed to be a played campaign trace.

## Complete Move-to-animation chain

A successful ordinary Move of a Complete unit follows:

```text
4b5840 action → 4b7b20 callback → 4b7a80 ordinary Move
  → hide/relocate/find-fit/reattach
  → 42a5a0 queued-or-idle selection → 42a610 order activation
  → 40ceb0 flingy-to-idle → 4176e0 sprite animation selection
  → 411730 animation installation → 410480 stock IScript VM
```

`0x4176e0` reads the main image's current animation and selects a return animation. The tested native results are:

| Initial state / Move result | Main-image script state | Order queue |
|---|---|---|
| Complete, accepted, current GroundAttack 2 | Install GroundAttackToIdle 8 and execute immediately to the first wait | Resume queued order 6, or stock return-to-idle order |
| Complete, accepted, current AirAttack 3 | Install AirAttackToIdle 9 and execute immediately to the first wait | Same |
| Complete, accepted, current Walking 11 | Preserve animation, PC, return address, wait and frame | Resume queued or idle order |
| Incomplete, accepted | Preserve current script state | Preserve queued order |
| Ground unit rejected by unwalkable destination | Restore `[64,64]`; preserve current script state | Preserve queued order |
| Flyer with the same blocked ground tile | Accept `[400,400]`; apply the Complete/incomplete rules above | Same |

All cases verify final integer/fixed-point positions, sorted spatial borders and their CUnit indices, sprite row-list consistency, active/hidden list heads, trigger quantity exhaustion, action return and queue count/head. These are complete selected original paths. They do not execute later movement updates of the newly resumed order.

The callback still consumes its quantity on rollback. Hiding/reattaching and reference/cache effects are covered by the preceding Move contract. This batch specifically checks the populated animation consumer after those operations.

## VM state, frames and random consumption

`0x411730` uses the low argument byte, stores the requested animation, reads its WORD PC from `header + 8 + animation*2`, clears return address and wait, then calls the original VM. Images without the full-animation flag `0x10` reject this non-Init/non-Death request through the original guard. In the tested profiles the shadow therefore preserves its Init state while the main image changes.

An independent, deliberately limited decoder predicts only the selected idle-return programs: playframe `0x00`, set vertical offset `0x03`, wait `0x05`, waitrand `0x06`, and goto `0x07`. It fails on any other opcode or unbounded path. Every resulting main/shadow image ID, IScript state, signed offset, base/current frame, sprite backlink, GRP pointer and executed opcode/PC list is compared with the native result. This decoder predicts the newly installed idle program; it does not substitute for any guest callee or validate all Init/Walking behavior by itself.

The Marine example is an actual complete action call: GroundAttack PC `29181` becomes animation 8, PC `29235`, return address 0 and wait 0. Its return program executes playframe at offset `29230` followed by wait at `29233`. Both offsets refer to the exact retail script with SHA256 `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0`.

Wraith's return program sets vertical offset and executes waitrand. Its wait is:

```text
(minimum - 1 + ((original_rng_result & 255) % (maximum-minimum+1))) & 255
```

The actual original source-3 PRNG call advances main state and the source counter when RNG is enabled; disabled RNG leaves them unchanged. With seed `12345`, enabled RNG and a Complete accepted Wraith Move, Init plus idle return consume two source-3 draws. The checked final main RNG state is `3785644967`; main script state is `[header=29358, PC=31143, return=0, animation=8, wait=9]`. Rollback/incomplete paths consume no additional idle-return draw. This random effect belongs to simulation even when no rendered frame is produced.

Walking preparation additionally closes the actual velocity helper `0x40c220..0x40c25b`: ECX is CUnit/flingy, stack argument is signed speed, callee pops 4. It stores speed at `+0x3c`; if changed from `+0x38`, it multiplies the original direction-vector table at `0x5026b0` by speed, arithmetic-shifts by 8, and stores `+0x40/+0x44`. The original instructions execute during selected stock Walking startup. This is a consumer dependency, not a new general movement integration claim.

## Comparison and implementation consequences

The pinned [OpenBW idle selection](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L16858-L16893) provides a structural comparison: attack animations choose attack-to-idle; Walking is absent from that switch. Its [animation installer](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L15136-L15158) and the pinned [PyMS opcode schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/IScriptBIN.py#L462-L479) supply names and comparison points. The local original instructions, stock bytes and actual isolated calls independently establish the selected behavior reported here.

For Defold, trigger Move must compose with the authoritative order and IScript state machines. On an accepted Complete move, activating an order can execute an idle-return program immediately and consume random state. A generic “reset animation to standing” operation would change Walking PCs/waits, non-animated shadow state and Wraith randomness. Store script header/PC/return/wait, frame, offsets and flags in simulation state; the renderer can reflect them afterward.

Remaining boundaries are positive child/turret and nonnull-AI Move reactions, live path disposal, other animation/VM opcode consumers, populated grounded-building animation, pixel decoding/drawing, and full campaign traces. Stock graphics headers and actual geometry/dirty-rectangle helpers do not establish rendered pixel parity. These dependencies remain explicit rather than being closed by the 768 passing cases.
