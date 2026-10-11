# Automatic subunit attacks and physical Scarab lifecycle

**Seven complete cases pass through 2,170 original function invocations.** A command issued only to a genuine Tank/Goliath body automatically reaches its native turret, whose stock IScript produces real damage. A genuinely paid and trained Scarab physically pursues its target, explodes and is reclaimed by the original object callback. Stopping the Reaver before projectile creation prevents damage; stopping it after creation preserves the eventual hit.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Subtract that base for RVAs. This extends [weapon effects](native-weapon-effects.md), [primary orders](native-primary-orders.md), [physical movement](native-movement-fsm.md), [active-unit ordering](native-active-iscript.md), [creation](native-image-initialization.md) and [pending recycling](native-populated-lifecycle.md). Their published scripts and evidence are unchanged.

## Evidence and inputs

- [Static extractor](../../tools/research/inspect_native_subunit_scarab_117.py), SHA256 `ace38a5c30a92de5d1a77f7bbfe9a6dd34007c2c386b5cdb2a07d2394ae45c72`, and [static evidence](evidence/native-subunit-scarab-static.json): **nine ranges / 404 decoded instructions**, without undecoded tails. The order-inheritance range is explicitly a fragment of activation, not an independently callable function.
- [Original-instruction probe](../../tools/research/probe_native_subunit_scarab_117.py), SHA256 `d9f2864ec9ba30b98be44889f64905c4561c2970504c553c5996f23a6a072410`, and [probe evidence](evidence/native-subunit-scarab-probes.json): **seven complete cases**, **2,170 checked sentinel returns**, **1,959,437 original instructions**, **440 visited range starts**, **858 reviewed ranges** and **67 authenticated source dependencies**. There are no prefixes or substituted helpers. The evidence contains each object-step timeline, raw/normalized damage arguments, IScript instructions, actual allocation/pool state and RNG validation.
- **Separate asset provenance:** [native-subunit-scarab-assets.json](evidence/native-subunit-scarab-assets.json), SHA256 `5fb4523f18bd6105aa494a6d8915005009c49c9aabf68c8b0e51f1b060f2fbc1`, covers **69 image IDs / 62 distinct GRP paths** and 73 selected LO inputs. It adds Images443,531,532,536 to the prior weapon resources. Actual Scarab movement creates Image443 dust; Tank-mode firing requires Image536. Retail payloads remain in private scratch storage.

Execution uses x86_64 through Rosetta, Unicorn2.1.4 and pefile2024.8.26, with a 250,000-instruction bound per invocation. Every selected entry returns through its sentinel with exact stack cleanup and executes only reviewed original ranges. Native RNG counters and final state are checked against an independent integer LCG. [PRNG contract](native-prng.md).

Authenticated Units/Flingy/Sprites/Images/Weapons/Orders DAT, IScript, GRP, LO and `rez\stat_txt.tbl` are supplied through private memory. Original acquisition initialization `0x425610`, creation `0x424900` and completion/show `0x424da0` execute. Every body, turret, victim and Scarab is originally allocated. Native linked lists, sprites/images and spatial borders are maintained by original callees. The stock rows used by the cases are preserved in the probe JSON.

The declared terrain is a clear synthetic 4096×4096-pixel, 128×128-tile map with two region descriptors, a uniform region1 raster, no obstacle contours and one finite 128-byte path record. Both the separate WORD rectangle at `0x543228=[0,0,4096,4096]` and the width/height globals are initialized. Visibility, owner3/4 hostility and eight valid 52-byte-stride player-region arrays are explicit fixtures. They are not an authenticated campaign-map initialization.

Each victim receives real primary **Nothing23**, with order-wait byte250 supplied explicitly. This isolates damage from victim acquisition/retaliation. The source body/child headings, movement, speed, positions and orders are not advanced or favorably overwritten by the host. Initial image readiness uses64 actual image/Bullet/Thingy steps; it is not a whole-object warmup.

The shot/travel phase calls complete original **`0x445070`**. The host supplies the external logical-frame counter `0x52651c`; all three current-object globals are required to be zero after return. AIScript/resource-area/auxiliary-Flingy lists are empty. Creep, two slow-AI/resource and deferred-update countdowns start1000, so their guarded positive-countdown paths execute. Populated AI regions, expired auxiliary services and creep production are outside these profiles. No Windows import, renderer, startup/main loop or guest host-file/network operation executes. This establishes complete selected **object callbacks**, not the outer accepted-frame, pause/readiness, command or trigger phases.

## Original caller contracts

| Entry | ABI and selected behavior |
|---|---|
| `0x42a380` | ECX=CUnit, stack=[order,target]; actual targeted primary-order issuance. Only the body/parent receives this host-issued attack command. |
| `0x42a792..0x42a842` | Fragment within `0x42a610` activation. Checks parent/child Subunit DAT properties, selects the child order and recursively issues it through actual `0x42a3e0` or `0x42a300`. |
| `0x4288f0` | ECX=body; order 21 StayInRange, which the stock vehicle body's units.dat AttackUnit column selects (corrected after the function atlas, [P09 `0x4288f0`](atlas/p09.md); earlier label "AttackUnit21"). Executes target validation, range/chase/stop decisions and Goliath body heading preparation. |
| `0x40ced0` | CL=current heading, DL=desired heading, stack=[signed limit], `ret 4`; returns the signed-byte angular difference clipped to the supplied limit in AL. The Goliath branch uses limit128 and tests absolute result against32. |
| `0x43a720` | ECX=subunit; selection-circle service, timers/status, main order, secondary order, applicable recursive child processing, then populated image update. |
| `0x43a770` | ECX=hidden unit; applicable child processing, actual movement, status/timers, hidden-primary dispatch, selected secondary services, then populated image update. |
| `0x439e20` | ECX=hidden unit; priority/throttled hidden primary-order dispatcher. The selected completed inside Scarab uses the original Nothing guard. |
| `0x420db0` | ECX=unit, EDX=x, stack=[y,target]; commit pursuit destination while preserving its target pointer, `ret 8`. The reached case has a stationary target. The separate moving-target prediction branch is not claimed exercised. |
| `0x41c910` | ECX=Reaver; main Stop7. Stops the parent, issues165 to every outside child through `0x42a2c0`, then issues parent Reaver58. |

The hidden/subunit functions execute real callees. Native turret membership remains distinct: it belongs to the owner list and links to its body through `+0x70`, but does not receive its own body-style active-list entry or spatial borders. The parent's original preparation/main sequence processes it under the appropriate context. This replaces the earlier selected-shot scope that separately issued a child command.

## Automatic body-to-turret firing

Activation checks that the parent lacks DAT Subunit flag`0x10`, its child pointer is nonnull, and the child's type has that flag. A parent order matching its type's ReturnToIdle, AttackUnit or AttackMove column is translated through the corresponding **child-type** column. Other orders require the stock Orders DAT inheritance byte. A unit target follows actual recursive `0x42a3e0`; position/FoW propagation follows `0x42a300`. [Complete primary-order mapping](native-primary-orders.md#subunit-inheritance).

These four cases issue only the stock body AttackUnit order; the units.dat AttackUnit column of these bodies is StayInRange21. Immediate native state is body100/21, child22 and the same target pointer. There is no separate child order or child-heading fixture. Subsequent complete `0x445070` calls perform targeting/turning, image/IScript firing, Bullet delivery and native damage.

| Native source → target | Body / child order | Weapon | HP loss | Selected stopping point |
|---|---|---:|---:|---|
| Siege Tank30 + turret31 → Marine0 at(360,200) | 100 / 22 | 27 | 35 | Damage callback11; Bullet reclaimed24; Marine5HP. |
| Tank5 + turret6 → Marine0 at(360,200) | 21 / 22 | 11 | 15 | Damage callback5; Bullet reclaimed25; Marine25HP. |
| Goliath3 + turret4 → Marine0 at(360,200) | 21 / 22 | 7 | 12 | Damage callback5; Bullet reclaimed20; Marine28HP. |
| Goliath3 + turret4 → Wraith8 at(320,200) | 21 / 22 | 8 | 10+10 | First missile damage18, second20; Wraith100HP. |

All damage events attribute the **body**, owner3, rather than the turret. Siege70 and Tank30 explosive damage become35/15 against the small Marine through the original damage pipeline. [Armor/size/attribution](native-damage.md).

The air case ends after the first missile pair's two damage callbacks. Both Bullet records still exist in their post-hit image phases at callback20; their reclamation is not claimed here. A developing trace showed the next genuine volley beginning before the previous missile records disappeared. A single-shot assertion that waits for zero Bullets would therefore accidentally include later damage. The [earlier projectile batch](native-projectiles.md) separately establishes selected missile image termination/recycling.

Goliath's body remains at(200,200) while the original child turns from its native initial direction toward the target. Its actual bounded-angle helper and parent/child preparation execute. Preserve the body/turret separation and image/order phase timing; issuing an independent turret command in the remake is not the recovered caller contract.

## Paid Scarab production and physical delivery

Each Scarab case starts with a native-created Reaver83 at(200,200) and Marine0 at(400,200). Actual `0x41b4b0` admission queues type85 and debits15 minerals, from1000 to985. Secondary63 is issued through actual `0x42a1e0`; `0x439310 → 0x41c630` allocates the child. Exactly106 subsequent secondary calls complete the stock105 countdown and register the genuine child through **`0x41cc80(ECX=parent, stack=[fighter], ret4)`**. The child is in the inside hangar before the shot.

Only parent ReaverAttack59 is then issued. Complete object callbacks activate launch61, choose the real inside child and move/show it25 pixels forward, to(225,200), through original launch geometry. Scarab65, target pointer, timers, speed, movement and image effects arise from original consumers. The host supplies none of those child fields.

The selected clear route reaches actual `0x48eb90/0x48ec50 → 0x491210/0x491c10`, consumes the supplied finite path record, and later releases it during native teardown. This is a genuine positive local path in the declared clear geometry; blocked, disconnected and moving-victim routes remain separate.

| Object callback | Original state after return |
|---:|---|
| 3 | Scarab shown at225, order65/state0; parent launch61. |
| 4 | Scarab state2, removal timer90, order timer7, NoCollision`0x200000`, movement12. |
| 6 | Path record`0x80c000`, movement26, current speed4096 raw1/256 units. Position still225. |
| 7–10 | Real publication at241,257,273,289:16 pixels per callback. |
| 11 | Position305; timer0; order state3; original `0x41c9c2` clears NoCollision. |
| 12–15 | Position321,337,353,369 with the same native movement. |
| 16 | Position385; inner-radius arrival selects state6, stops speed and stock SpecialState1 produces populated Bullet503/weapon82. |
| 17–18 | Real Death0, then hangar detachment, path release and pending-CUnit entry. |
| 21 | Real weapon82 impact deals100 to the40HP Marine; Reaver receives one kill. |
| 22 | Bullet recycled; victim joins pending Death processing. |
| 27 | Scarab is reclaimed by the original object callback's pending producer. |
| 38 | Marine is reclaimed; pending list empty; allocated CUnit count1 leaves only the Reaver. |

Pixel coordinates, raw fixed coordinates shifted right8 and real Sprite coordinates agree at every recorded step while a Sprite exists. Real movement also generates populated stock dust Image443. No authored sprite advance or replacement path/Bullet callee participates.

## Stop before and after projectile creation

The two interruption cases issue real parent Stop7 through `0x42a2c0`, leaving its handler and all child order transitions to original code.

When the child has reached273 at callback9, Stop is issued before callback10. `0x41c910` traverses the parent's outside-hangar chain and issues **SelfDestructing165**. The same original primary phase reaches `0x42a4d0` and selects Death0. The Reaver changes to58. There is **no Bullet or damage**, Marine remains40HP, parent kills remain0, and the Scarab is reclaimed by callback20. Final allocated count2 leaves parent and victim.

Movement does not vanish at the instant of parent issuance. Callback10 publishes289 before the parent/child primary decisions; callback11 publishes305 before the Death consumer stops/removes the child. Native phase order therefore preserves two final16-pixel movements across this interruption sequence. A remake that immediately clears child speed when parent Stop is accepted would disagree with these callbacks.

The later interruption is issued after callback16 has already created Bullet503, while the Marine still has40HP. Parent Stop again selects child165/Death and removes it from the outside hangar. The existing Bullet retains its **Reaver source**, impacts at21, kills the Marine and awards the parent one kill. Child and victim are reclaimed at27/38. Stopping the parent cannot retroactively cancel this already-created damage producer.

All three cases end with outside count0, empty pending list and a consistent finite free-CUnit count. Calls to `0x4256b0` are reached **inside** `0x445070`: one for the interrupted child, two for delivered child/victim. The probe never directly invokes the recycling helper in these cases. This closes the earlier direct-recycling boundary for the selected physical lifecycle.

## Reproduction and remaining scope

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_subunit_scarab_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-subunit-scarab-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_subunit_scarab_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-subunit-scarab-assets \
  --asset-manifest docs/research/evidence/native-subunit-scarab-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-subunit-scarab-probes.json
```

These selected compositions establish automatic stock body/turret attacks, clear physical Scarab pursuit, interruption timing, damage attribution and natural reclamation. Reaver terrain obstruction/retry, unreachable or moving targets, multiple concurrent Scarabs, computer-AI decisions, hangar upgrades, terrain-specific failed launch placement, and complete outer campaign frames remain separate. The original paid/trained producer removes the earlier need to author a child at the explosion point; it does not establish rendering or six-campaign parity.
