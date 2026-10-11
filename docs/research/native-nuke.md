# Native 1.17 Nuclear Strike production, launch, impact and cancellation

30 complete selected cases pass, with no prefixes or callable helper stubs. This batch closes genuine paid Nuclear Missile 14 production, the Silo 108 ready connection, Ghost 1 orders 123–129, populated stock launch/impact and selected cancellation/death/pool boundaries. Earlier [BW casts](native-bw-casts.md), [AI spell selection](native-spell-families.md), [training](native-training-lifecycle.md) and [weapon effects](native-weapon-effects.md) retain their separate scopes.

## Evidence and fixture boundaries

The exact executable is local **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred base 0x400000. Original x86 instructions and actual reviewed callees execute in isolated private Unicorn memory. Every direct invocation has a 100,000-instruction bound and exact return/stack checks. There are no callable helper stubs, Windows imports, WinMain, resource-loader execution, host APIs, network or complete-game execution. The private runtime is x86_64 Darwin/Rosetta, Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6, pefile 2024.8.26.

The [probe evidence](evidence/native-nuke-probes.json) records actual calls, state changes, Image VM PCs, Bullet creation/impact, exact DAT/asset inputs, all imported-source hashes, source freshness checks and independent integer-LCG validation. The [static witnesses](evidence/native-nuke-static.json) include operand-level instructions, dispatch tables, launch-state destinations and CancelNuke command mapping. The [private asset manifest](evidence/native-nuke-assets.json) records selected stock GRP/LO/archive provenance without publishing retail bytes. Authentic network.tbl is bound to 0x665de0 and stat_txt.tbl to 0x6ab4f8, using the separate [string manifest](evidence/native-hallucination-string-assets.json).

The world is an explicit flat/explored/visible terrain fixture, with self-alliance bytes, human controllers 2, finite Unit/Sprite/Image/Thingy pools, resources, supply and availability. Native creation/completion creates the Silo primitive; this does not prove SCV construction, Command Center addon geometry or frontend missile-training command admission. The inherited [construction](native-construction.md) allowlist supplies unchanged reviewed creation/movement/object-step dependencies.

Production uses actual **secondary 0x439310 → full primary 0x439420** services on the Silo. Launch uses complete **0x445070 object callbacks**, including movement, timers, primary/secondary orders, unit Images, Bullets, LoneThingys and recycling. The external frame counter is advanced explicitly, while auxiliary AI/script/resource/vision lists/countdowns are declared empty or deferred. This is complete execution of the selected object-step function, not the accepted outer campaign frame/replay/render loop.

Independent branches clone an exact byte snapshot of the genuine native-produced ready missile or Bullet. The producing case and snapshot hash are retained. No missile connection, order state, heading, IScript signal, damage or kill count is invented in these positive chains. Selected zero pools, supply/availability, selection pointers and the Invulnerable test status are declared fixture inputs. Cloning the state does not add fictitious original invocations to the totals.

Audio is explicitly disabled (`0x6d3140=0`), so real 0x446940 returns through its guard. The authentic launch-warning string lookup runs, but real 0x46ae50 returns through declared unequal window indices 0x508d48/0x508d54. Display/playback and Windows imports are outside this evidence. The Ghost's post-track AI return helper 0x4afd40 executes its original **null-AI return**; populated AI return-to-base is a separate path.

## Paid production and Silo readiness

Stock Nuclear Missile 14 costs **200 minerals/200 gas**, build time **1500**. Actual can-make 0x4b32a0 succeeds for the genuine Silo and stock requirement recipe. A discriminating case still fails paid 0x41b4b0 with provided Terran supply 200 but global race cap 0: resources stay 1000/1000 and all five queue slots stay 228. Requirement success does not imply paid queue success.

With declared cap 400, original 0x41b4b0 pays 200/200 and admits unit 14. Secondary order **Train 38**, issued through 0x42a1e0, runs full 0x439310 and genuinely allocates the child. It begins with build time 1500. Primary Silo order 124 runs **0x4356c0**, which observes currentBuildUnit at +0xec, writes Silo+0xd0=child/child+0x80=Silo and state 1; it does not pay for or create the missile itself.

After **1501 subsequent secondary/primary services**—1502 secondary calls including the initial creation—the child is completed, the ready dword Silo+0xd4 becomes 1, Silo state returns 0, resources are 800/800 and the missile retains its genuine related-Silo link. Its ordinary hidden wait order is 23. These are progress services; the production loop does not independently certify 1502 accepted game frames.

A separate original **AIScript helper 0x493450** creates/completes/hides unit 14 for an unready Silo without charging resources. Its positive and exhausted-Unit-pool branches retain actual allocation and link writes. These cases use controller 2 and execute the real 0x48d320 controller guard; the Computer 1 child-AI assignment tail is not claimed by them. The helper's genuine campaign caller and its broader AI assignment remain separately identified in [AI opcodes](native-ai-opcodes.md).

## Order dispatch and targeting

| ID | Native role | Dispatch / handler |
|---:|---|---|
|123|NukeWait selected no-op|stage 3 case 99|
|124|Silo production observer|fast case 24 →0x4356c0|
|125|missile launch states 0–6|slow case 63 →0x436540|
|126|point/unit routing|slow case 64 →0x4366f0|
|127|unit-target coordinate tracking|slow case 65 →0x41eba0|
|128|Ghost movement, aim and Silo consumption|slow case 66 →0x41ea40|
|129|Ghost channel and marker lifecycle|first special case 2 →0x436720|

Order 129 is handled in the first special stage, before the ordinary disabled-unit gate. Order 126 selects 127 when its target-unit pointer is present, otherwise 128. Order 127 copies the target Sprite position into the Ghost order point and calls 128. A null unit target executes original stop/resume; 123 remains unchanged in its selected no-op case. These low-level issuance cases do not prove UI/technology permission for arbitrary orders.

Ground 0x41ea40 uses native sight×32 and original approximate point-range testing. Outside sight it requests movement through the recent-movement throttle 0x438b80→0x43a0d0. Inside range it stops, then compares current facing with original 0x40ba10 direction toward the next target waypoint; the direct integer difference must be between −1 and 1. The main (200,200)→(400,240) fixture genuinely turns from 200 to 71 through **11 object callbacks**. Heading is never overwritten.

Ready-selector 0x436f60 traverses the real owner list for the first Silo 108 with nonzero ready dword. Ground consumes its stored missile, clears both Silo+0xd0/+0xd4, issues missile 125, sets the reciprocal Ghost/missile+0x80 links, turns its Images through 0x415f00, shows/enables it, issues Ghost order 129 and sets buttons 237. NukePaint point and unit profiles subsequently execute the full launch/impact/cleanup chain, including real movement toward the initially out-of-sight Silo target.

## Populated launch and stock VM delivery

Original 0x436540 checks **state<5 with null related unit** before its seven-state switch and issues Death in that case.

| State | Original contract |
|---:|---|
|0|move upward toward x=current Sprite x, y=stock missile top extent; order timer 90, state 1|
|1|when timer≤45 or native arrival succeeds, play guarded warning/lookup and set 2|
|2|when timer 0 or arrival succeeds, hide/disable, retain Ghost connection, stop and set 3|
|3|when accelerating bit 2 clears, force WarpIn animation 21 and set 4|
|4|wait for real IScript signal 2, clear it, relocate to x=max(targetX,left extent), y=max(targetY−320,top extent), turn to 128, request descent/show, set 5|
|5|native point test within 10 of move target starts SpecialState1 animation 13, target=self, state 6|
|6|wait for real signal 1, clear it, call actual removal 0x425700|

For the main target (400,240), missile top extent 14 determines the upward endpoint and relocation (400,14). After the explicit initial state 0 dispatcher call, object callbacks show warning state 2 at 45, state 4 at 54, real signal/relocation state 5 at 304 and descent arrival state 6 at 367. No wait countdown, signal or position is patched. Other target coordinates have different native motion durations.

Ghost order 129 allocates populated target-dot **Thingy/Sprite 231, Image 233**, at the missile target point, with elevation GhostSprite+1 and native visibility. It uses state 6 while tracking. It resumes its genuine default order, restores buttons and requests marker Image Death when the connection is null or missile state 5. Gameplay release therefore precedes missile impact; visual removal can finish later.

Stock missile Image 316 executes its actual SpecialState1 program. Native **`castspell` opcode 39 at PC 26448**, with order 125's stock weapon 31, provides the delivery. It reaches 0x420030, genuinely initializes the Bullet with **source=missile, target=missile, heading 128**, and stock Bullet Image 318. Main impact 0x4090e0 occurs at (400,240), weapon 31/hit type 5, through real radial query 0x443110→0x409590. Marker, smoke 422, explosion and missile/target body deaths use populated native pools.

The main complete chain reclaims the missile, enemy Marine, Bullet and remaining Thingys after **537 object callbacks** following the initial state 0 call. The enemy kill increments the **missile's kill byte**, reaching 1 before source recycling; it is not a fabricated Ghost counter. Friendly Ghost collateral also receives genuine Death and recycling. The Silo remains alive and subsequently consumes original building burn damage; final HP must not be mistaken for the immediate impact loss.

## Damage formula, radii and target gates

Weapon 31's raw DAT damage 600 is not its computed Nuke amount. Original 0x409010 checks hit type 5 and uses

`max(500, floor(2 × (stock max HP + applicable stock max shields) / 3)) ×256`.

Only if stock maximum HP is 0 does its separate native fallback use rounded-up current HP, with a minimum 1. That fallback is statically recovered; the genuine target cases test ordinary positive maxima. Shields are added only for a shield-enabled type, regardless of unused DAT shield values on Terran rows.

Native-created Command Center 106 (1500 HP) and Nexus 154 (750 HP+750 shields) give 1000 damage; Torrasque 48 (800 HP) gives 533. Actual 100 HP direct damage before a second amount calculation leaves each computed Nuke amount unchanged. The amount depends on maximum life rather than current life.

The radial callback excludes its attributed source before weapon-target legality, then permits same-owner/allied-owner collateral for hit type 5. Invulnerable status fixtures keep HP. Ground-mode inner/medium/outer radii are **128/192/256**, with divisors 1/2/4, original rectangle-bound approximate-distance tests and native armor/shield consumers. Neither center-distance nor floating Euclidean circles reproduce this contract.

Six direct complete callback cases use an exact snapshot of the genuine stock Bullet and freshly native-created Battlecruisers, without intervening object motion. With left extent 37, target x-offsets 165/166, 229/230, 293/294 discriminate adjacent boundaries. Their surviving raw HP is **768 / 64768 / 64768 / 96768 / 96768 / 128000**, respectively (3/253/253/378/378/500 displayed whole HP). The selected callbacks execute real on-hit reactions and preserve their state changes; they are not six stationary whole-frame battles. Building/ground retreat-search and populated GuardAI branches of these reactions remain separately bounded.

## Cancellation, source death and finite pools

The exact 1.17 command decoder maps **byte 0x2e**, bias 5, index table 0x4764a8, index 38, destination 0x476266; that branch calls **0x4724f0**. This mapping is static evidence, separate from full command-decoder execution.

Complete 0x4724f0 executes actual selection reset 0x49cb10/iteration 0x49cb20 and accepts Ghost/hero IDs 1, 16, 100, 99, 104. The selected-unit pointer table is an explicit self-owner 3 fixture. Helper 0x435a40 checks that its related object is type 14 and clears both genuine connections. No orders or state bytes are authored by the test.

| Intervention | Native outcome |
|---|---|
|Cancel command at launch state 1|connections clear immediately; missile receives Death; no weapon 31 impact; all visuals reclaimed|
|Cancel at state 5|connections clear, but descent/stock Bullet/impact continue|
|Actual Ghost Death at state 1|Death initially leaves the link; actual death cleanup subsequently disconnects, suppressing impact|
|Actual Ghost Death at state 5|source cleanup occurs, but missile continues to impact|

The fixtures demonstrate an original **state 5 commitment boundary**. It is enforced by the missile's own `<5` dependency gate, not by a host cancellation model. The command handler itself does not require Ghost order 129; UI offer/admission and delayed network-command arrival remain separate.

With declared empty Thingy free pool, native marker/smoke allocation failures complete, yet populated missile/Bullet still cause damage. This is an allocation-failure contract, not positive marker production. With empty Bullet free pool, genuine launch/movement/stock VM proceed and the missile is reclaimed, but no damage Bullet/impact appears. Spent Silo readiness and paid resources are not restored by these tested cancellations/failures.

## Reproduction and remaining dependencies

The published run executes **6,684 complete original invocations and 22,159,898 original instructions**, with a largest direct call of **28,538 instructions**. The static inspector adds **23 reviewed ranges / 849 instructions**. The resource manifest selects **189 Image IDs / 160 distinct GRP files**. Recursive dependency provenance pins **100 unchanged Python sources**.

- [Probe](../../tools/research/probe_native_nuke_117.py) SHA256: `02ced1bba1513f637955301a107fbf5775ebb81539e7959ce2b65764ef97a32c`.
- [Inspector](../../tools/research/inspect_native_nuke_117.py) SHA256: `3e4f7295c88b71ff1d1e4b530e81c7fed84351a6837164646f0a9504447b1515`.
- Image asset manifest SHA256: `b87daecf3e060abf6a7ad0f37acf6f7208e24c2313cba5c5922b2c7a0ecd9e09`.
- Authentic string manifest SHA256: `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`.

The inspector takes `--exe` and `--out`. The probe defaults to a plan. Execution requires the exact private EXE/data/manifest, selected private image resources, authenticated asset/string manifests, pinned PyMS and explicit **`--execute-original-code`**. `--profiles` supports discriminating reruns; the published evidence uses `all`. Each imported implementation remains unchanged and is identified by SHA256.

This batch closes selected complete native nuclear production, targeting, channel/launch, populated delivery, effects, cancellation and cleanup contracts. Remaining distinct contracts include SCV construction/addon admission, Computer 1/populated AI child assignment and return-to-base, tech/UI command admission and the complete command decoder, selection construction, multiple simultaneous Silos/Ghosts, hostile dynamic target movement/disappearance and difficult terrain, loaded/hangar targets and remaining splash/status permutations, true audio/UI/rendering, and accepted outer-frame/campaign execution. These are not silently promoted from wrapper/source knowledge. The separate strategic [AI spell selection](native-spell-families.md) and [opcode](native-ai-opcodes.md) evidence must be composed with these original consumers before claiming a full campaign AI Nuke scenario.

The pinned [GPTP Nuke orders](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/spells/nuke_orders.cpp#L39) and [cancel reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/recv_commands/CMDRECV_Cancel.cpp#L20), and [Teippi Nuke orders](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/nuke.cpp#L18)/[amount reconstruction](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/bullet.cpp#L474), were read from their verified pinned local checkouts. They are corroborating modified/reconstructed source, not original 1.17 runtime evidence; their addresses and replacement behaviors are not transferred. The exact retail instructions, stock inputs and bounded native outcomes above are the deciding evidence.
