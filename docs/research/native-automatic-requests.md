# Native automatic Town requests and queued construction reservation

**34 complete original cases** establish selected Terran supply, gas and regional-defense request producers. One chain follows an automatic Supply Depot request through original worker placement, next-pass pending-queue reservation and complete object callbacks to a genuine finished building. Its AIScript contains no `build` instruction. This extends [connected scripted construction](native-ai-construction.md), [Town startup](native-towns.md), [economy](native-economy.md), [workers](native-workers.md) and [Terran construction](native-construction.md).

The exact executable is **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred base `0x400000`. Addresses are preferred VAs; subtract that base for RVAs. [The report](evidence/native-automatic-requests-probes.json) records **1,544 complete original calls / 16,708,586 instructions**, zero prefixes and zero helper stubs, 464 visited range starts, 1,176 reviewed ranges and80 authenticated sources. The largest call executes4,513,688 instructions under the8,000,000 bound. Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26 execute private reviewed code; no imports, WinMain, host-file/network operations or full accepted-frame/gameplay orchestration execute.

The [probe](../../tools/research/probe_native_automatic_requests_117.py) SHA256 is `4656e426c1468b37f32da498aa9a09d122c67c379a17c784e8da825aabf96335`. The [inspector](../../tools/research/inspect_native_automatic_requests_117.py) SHA256 is `5a8facfbfccb4a909e37f3be9c071de0e881c1979d63fa7592aa8e71fa6af768`; [its report](evidence/native-automatic-requests-static.json) has19 blocks /1,093 decoded instructions without undecoded tails, plus separate count/morph dispatch data. Original DAT/GRP/LO/IScript provenance reuses [construction assets](evidence/native-construction-assets.json); private payloads are not published. Instruction/write guards, complete-return checks and independent RNG validation follow the connected construction fixture.

## Native world inputs and script producers

Owner3 is Computer1, race1. Actual unit creation/completion produces the SCVs, Command Center and selected additional buildings/resources. SCV population itself updates used supply and stock buildings update provided supply; the host never patches used supply to simulate a threshold. Genuine script-pool initialization, creation and full VM startup execute `start_campaign`, `start_town`, selected configuration instructions and `wait1`. Script-pool/global bindings, flat terrain/visibility/empty contours, self-alliance, valid regional arrays and finite pools remain explicit caller inputs, as in [the connected fixture](native-ai-construction.md#evidence-and-fixture-boundaries).

Fourteen supply cases use1/3/4/5/6/7/10 genuine SCVs and either opcode50 `farms_notiming` or51 `farms_timing`. Four defense cases supply regional flag bytes. Fifteen gas cases supply actual-resource pointer slots, worker limits and resource amounts after native object creation; automatic resource discovery/limit calculation is not claimed. The last case connects automatic supply to completion. Each player pass invokes full `483400`, with next-player selector`67f7a8 = 2` as an external cadence fixture and elapsed counter0. Ten-or-later periodic recruitment is outside this composition.

## Automatic supply threshold, timing flag and priority

The last-Town tail of full `4865b0` executes supply helper `485970`. Racial IDs are selected from player race: supply42/109/156 and worker41/7/64 for Zerg/Terran/Protoss. This batch executes the Terran branch; other races remain static leads.

The tested stock Command Center provides20 half-supply units. With `farms_timing`:

| Genuine SCVs | Remaining supply | Automatic Depot entry |
|---:|---:|---|
| 1/3 | 18/14 | None |
| 4/5 | 12/10 | Priority120, kind3, ID109, Town context |
| 6/7/10 | 8/6/0 | Priority140, kind3, ID109, Town context |

`farms_notiming` sets PlayerAi word+`218` bit`0x10`; `farms_timing` clears it through the original VM. The set bit suppresses the tested request while remaining supply is nonzero, but permits the zero-supply request. This is the native consumer behavior, not a conclusion inferred from the opcode names. Generated Depot requests leave persistent Town request slots zero.

The static helper also establishes the ordinary desired-count calculation: below13 remaining, take used racial supply+13, subtract stock main-building supplied amount, divide the positive excess by stock supply-unit supplied amount, then add1. It compares that result against actual/queued count `487ba0` and emits one request when deficient. Negative remaining supply, airborne-depot overrides, non-Terran/morph counting and all desired-count/overflow profiles are not certified by this population matrix.

## Pending queue counts prevent a duplicate

The connected case starts with four SCVs, a Command Center and `[56,3,51,2,1,0,36]` bytecode. Full `483400` emits priority120 Depot109. Full `487580` chooses752,672 and issues original PlaceBuilding30 with queue`[109,228,228,228,228]`.

A second **complete `483400` before any physical building allocation** executes `487730` pending-worker reservation. The heap is empty, demand zero, available mineral/gas/supply are900/1000/12, while current minerals are still1000. Native count helper `487ba0` includes the genuine queued Depot, so automatic supply emits no duplicate. The host writes neither a pending count nor a replacement budget.

Complete `445070` callbacks then move/build with all four WorkerAi objects present. The selected slow-service countdowns start at10,000; no host movement, heading, signal, HP, timer or AI assignment advances the chain. At callback681, the Depot completes at HP128,000/time0 and attaches to the Town building list before the Command Center. Current minerals are900, six units are live, and native Terran supply is36/8/400. The next player pass has no spending entry and available supply28. Persistent script request slots remain zero throughout. These are object-phase counts, not measured seconds or complete campaign execution.

## Gas desire, available sources and expansion gates

Automatic gas helper `485ab0` runs from the per-Town request pass when the Terran Town has more than one worker. The tested one-worker/main-building profile skips it. Its initial gate returns when the gas-allocation percentage is zero **and the mineral pointer is nonnull**. With a null mineral pointer, the selected cases continue even with zero gas demand.

Native classification scans the three supplied gas slots in order. A completed own Refinery with nonzero resource amount has category3; a depleted one has category2; a raw Geyser188 has category1. Missing category1 prevents a new request in the tested profiles.

Without an operating category3 source, a referenced raw Geyser produces Refinery110 at priority90, including minerals499 and a depleted Refinery. With an operating Refinery plus a raw Geyser, expansion uses priority80 and requires **minerals >=500 and `(worker_count >= worker_limit or worker_count >=15)`**. Limit16 cases distinguish14/15 workers and minerals499/500. A two-worker/limit2 case separately proves the limit branch below15. Both completed Depots in these fixtures produce enough supply to keep this gas matrix independent of automatic supply.

A genuine MineralField176 pointer with no gas demand suppresses the request. A separate full VM `build1 Ghost1 priority80` produces native25-mineral/75-gas/2-supply demand; this keeps the gas percentage nonzero and allows the priority90 Refinery request despite that same mineral pointer. The heap then contains Refinery90 before Ghost80. Ghost production or prerequisite satisfaction is not executed by this demand-producer case.

## Regional Bunker and Turret requests

Four cases supply region1 flag bytes0/2/16/18. Full player/Town processing runs actual `4ae220` and `4b1420`. Bit2 produces Bunker125, bit`0x10` produces Missile Turret124, both at priority80 with kind3/Town context. Their stock mineral demands are100 and75; both flags yield175 total demand and preserve Bunker before Turret at the equal-priority heap boundary. Persistent Town request slots remain zero.

Regional flag creation, multiple-region selection, an existing Bunker's troop requests, air/ground threat changes and physical defense construction remain distinct. Supplying a flag is an input to its consumer, not a proof of the original threat producer.

## Defold contract and reproduction

Preserve persistent script requests separately from automatic requests, complete/queued counts, desire percentages, disposable spending heap and pending-budget reservations. Include queued construction before requesting more supply. Preserve native thresholds, priority changes, timing-bit suppression and gas-slot category/scan order. Resource discovery, worker limits and regional threats require their own original producers; these caller inputs do not close them.

Execute the inspector with the exact `--exe` and `--out`. Run the probe using the [connected construction reproduction command](native-ai-construction.md#reproduction), replacing its tool with `probe_native_automatic_requests_117.py` and output filename. `--case-group all` reproduces34 cases; `supply`, `gas`, `bunker`/`turret`, and `connected` select narrower compositions. Without `--execute-original-code`, the probe prints its plan. The unchanged image reader extracts the143 IDs in the shared manifest. R25/R26/R27 remain partial for other races, resources, strategic/periodic producers and broader world interleaving.
