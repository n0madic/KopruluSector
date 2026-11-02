# Native 1.17 regional defense recipes and strength accounting

**108 complete original-code cases pass**, closing selected recipe consumption from stored defense lists through borrowing, real production-request insertion and the periodic regional sweep. The native bookkeeping credits **type strength**, separately from current cached strength and actual successful military allocation. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`.

Evidence: [13 new static witnesses, 269 instructions](evidence/native-defense-recipes-static.json), [108 complete cases](evidence/native-defense-recipes-probes.json), and [inspector/probe](../../tools/research/inspect_native_defense_recipes_117.py). The longest call uses4431 instructions. Every reached helper executes reviewed original instructions, including recipe writers/readers, donor selection, AI release/allocation, arrival/primary orders, current-strength refresh, costs and heap insertion. Private fixtures provide objects, pools, region graphs, cached/type strengths and explicit unit DAT values; stock orders.dat provides189 unchanged order fields. No callable helper replacements, complete game, WinMain, Windows API or guest host-file/network operations execute.

## Native stored recipes and list selection

The eight native recipe arrays contain20 u16 words each. A nonzero word stores `unit_ID+1`;0 ends consumption. The original writer accepts a requested append count≤10 and unit_ID<228, fills available zero slots, and can fill all20 words. The writer boundaries are independently recovered in [AIScript opcodes](native-ai-opcodes.md). Eight present cases compose the actual corresponding writer with the regional consumer.

| Category label | Build array / reader | Existing-unit array / reader |
|---|---|---|
| gg | PlayerAI+`0x2b0`, `0x482510` | +`0x350`, `0x482d10` |
| ag | +`0x2d8`, `0x482500` | +`0x378`, `0x482d00` |
| ga | +`0x300`, `0x4824f0` | +`0x3a0`, `0x482cf0` |
| aa | +`0x328`, `0x4824e0` | +`0x3c8`, `0x482ce0` |

These labels match pinned [PyMS opcode names](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L183). The native consumer determines their selection through exact arguments and terrain tests, rather than a broad interpretation of those suffixes.

`0x4ac3c0(ECX=receiver,EDX=requirement_region,stack=[air_mode,required_strength])` normally examines literal marker`0x1ffd` on **both** receiver and requirement-region terrain records. If either is marked, mode0 uses ga and mode1 uses aa. If neither is marked, mode0 uses gg and mode1 uses ag. Sixteen complete calls initialize all four build categories with different IDs, vary both markers/mode/campaign flag, and verify exactly which ID is requested and which type-strength direction is returned.

There is an earlier requirement-state4 branch: terrain u16+4<16 returns **999** immediately; a value≥16 jumps directly to build production, **skipping existing-unit borrowing**. Eight cases distinguish area values0/15/16/17 and an empty/present existing-unit list. The field is the stored terrain word used here; fixtures do not establish every naturally generated area or every context placing a requirement region in state4.

## Existing-unit borrowing and partial success

Normal consumption first passes the chosen existing-unit array to `0x4ac530(receiver,requirement_region,[list_pointer,air_mode])`. For each nonzero ID+1 word, it obtains the receiver center and destination, tries actual surplus selector`0x4ac230`, then guard/lower-priority selector`0x4ac0d0` if necessary. Fallback threshold is the **receiver's priority byte**, rather than the roster helper's separately supplied60/160 policy. The supplied requirement-region argument does not become an alternate source for that fallback threshold.

Guard conversion clears UnitAI, invokes actual destination allocation/arrival, clears the old guard parent, and recomputes the unit's actual old-region strength. Military conversion releases the old record, clears UnitAI, allocates/arrives in the receiver and recomputes the old assigned region. Occupied status can additionally invoke unloading helper`0x437e00`; nonnull garrison unloading is not exercised here. Detailed selection and node-reuse behavior are in [initial recruitment](native-campaign-recruitment.md) and [multi-donor ranking](native-donor-ranking.md).

After a selected donor branch, the function adds **base type strength**: mode1 reads `0x6ac530[ID]` through`0x4a97e0`; mode0 reads `0x6ac8c0[ID]` through`0x4a97f0`. It does not add Unit+`0x138`/+`0x13a`, the successful allocation's actual returned value, or newly measured destination-local strength. It continues through the whole existing-unit recipe; required_strength is not a borrowing-loop termination test.

**Any nonzero borrowed total returns immediately from `0x4ac3c0`, bypassing build production for that invocation.** Eight compositions use a duplicated borrow recipe `[43,43]`, one available Mutalisk43 guard/military donor and a build recipe `[37]`, with required_strength1000. Only the first matching donor can be borrowed. The helper returns100 for air or80 for ground, although those values do not cover1000, and requests no production during that call.

A guard with an exhausted MilitaryAI pool demonstrates the same credit despite failed allocation. Its UnitAI becomes0, old guard parent becomes0, no new order or RNG occurs, but the recipe function still returns the type's100/80 and bypasses production. Two complete **periodic sweep** cases set air requirement100; both successful and exhausted-pool guard cases stop on that credited100 without a production fallback. Successful allocation issues157 and consumes three RNG calls; failed allocation issues nothing. This extends the earlier roster helper's return-value boundary into real strength accounting.

## Production recipe and campaign stop rule

Only a zero borrowed total enters build production. An empty selected build array returns999. A nonempty array emits one actual request per consumed word with arguments:

```text
482990(player, receiver, [unit_ID, receiver_priority + PlayerAI_bonus, 0, 0])
```

The bonus is PlayerAI byte+`0x223`, read by`0x4827c0`. Addition is wider than one byte at the callsite; the real heap record stores an8-bit priority. Four calls compare0+0,77+20,250+20 and255+255, verifying stored priority0,97,14,254. These are request priorities, not timestamps or unit counts.

After requesting an entry, the helper adds that ID's selected **type-strength dword**. Campaign mode stops when this cumulative value is≥required_strength. Noncampaign mode consumes the whole list regardless of that comparison. Forty complete calls vary mode, campaign flag and required values1/60/100/101/160/200/240/241/300/301 against recipe`[43,37,43]`. Fixture type strengths are air43=100/37=60 and ground43=80/37=120; fixture costs are explicitly stated in each generated setup. The real helper records kind1/context=receiver, updates AI demand and inserts requests through the actual heap; it does not pay resources or instantiate completed units here.

A nonempty recipe whose selected type strength is0 returns **0**, rather than999, after making its request. Two direct cases establish that boundary. The periodic caller's visible static loop can therefore make no progress when its requirement remains positive and this same zero-credit recipe keeps being selected. This report does not execute an unbounded sweep or claim such a configuration occurs in the shipped campaigns; the zero-strength fixture distinguishes native behavior from an invented guaranteed-positive result. The next section shows statically that the stock scripts never make one.

## The stock scripts name no zero-strength type

The recipes are filled only by the defensebuild and defenseuse opcodes `0x13`–`0x1a` (kp's `defense_add`), so the stall needs one that names a type without strength in the direction its list is credited by. The mode selects both the list and the strength: ag and aa are credited by air strength, gg and ga by ground strength (kp's `fulfil`, the list `unwalkable × 2 + mode`). [`inspect_ai_defense_strength.py`](../../tools/research/inspect_ai_defense_strength.py) decodes every instruction of `aiscript.bin` and `bwscript.bin` with `inspect_ai.py`'s decoder and the pinned PyMS schema. That covers every script, the campaigns' and the melee AI's, so it is a superset of what any game reaches. The tool then computes each type's strength as `kp_strength_init` does from the stock `units.dat` and `weapons.dat` (ComputeTypeStrength `0x4a9000`, ApplyStrengthTypeModifier `0x4a8f60`).

All 2,568 defense opcodes name a type with strength in their list's direction. The weakest entries are:

- gg: the Marine, ground strength 100;
- ag and aa: the Scourge, air strength 70;
- ga: the Wraith, ground strength 130.

The direction matters. The gg lists name eleven types without air strength (the Vulture, Siege Tank, Firebat, Zergling, Ultralisk, Guardian, Infested Terran, Dark Templar, Zealot, Reaver and Lurker). The ag and aa lists name four without ground strength (the Scourge, Valkyrie, Corsair and Devourer). A swapped mapping would therefore stall.

A full list that runs on into its neighbour (above) adds requests after entries that already gave credit, so it cannot stall either. The inputs are the effective files of [retail-manifest.json](evidence/retail-manifest.json) (`units.dat` `da2ee6f1…`, `weapons.dat` `80de1650…`, both scripts as in [ai-inventory.json](evidence/ai-inventory.json)). Evidence: [ai-defense-strength.json](evidence/ai-defense-strength.json); its command is in [reproduction.md](reproduction.md).

The tool's strength table equalled kp's `g->strength` for all 228 types. That was checked once on 2026-11-02 with a scratch harness over `libkp_oracle_lib`, which is not kept. `test_aiscript.stock_defense_types_have_strength` holds kp's table to the evidence: the types of each direction keep their strength, and the three weakest keep their values.

The original's endless loop thus needs a modified script file. kp stops the game with the address (`kp_ai_military.c` `recipe_stalled`) instead of inventing progress.

## Full arrays do not reserve a terminator

The consumer tests the next word for0, with **no20-entry bound**. The original writer can fill all20 words. One complete case uses two valid count10 writes to fill gg with twenty Mutalisk43 words, then writes a Zergling37 into adjacent ag. A noncampaign gg consumer continues into that neighboring category until its next0, producing **21 requests** and returning1720 ground type strength (20×80+120). Actual AI demand becomes220minerals/100gas/41supply under the supplied DAT costs.

This is original instruction behavior on a configuration constructible through the original append helpers. It does not assert that any particular shipped campaign reaches this full-list combination. Treating every list as an independently bounded20-element Lua sequence would erase the demonstrated native cross-array behavior; any compatibility policy should make that choice explicit. Neither the writer nor this consumer is rewritten to guarantee a reserved sentinel in the evidence.

## Periodic sweep and its independent progress variable

Original `0x4af930(player)` scans region indices1..count−1. States0..3 skip this strength-recipe work; state8 instead invokes initial attack-roster recruitment. Other states>3 use current-region requirements for states4/5/6 and stored-destination requirements for states≥7. That includes state9, which does not share state8's roster branch.

Air shortfall starts from the receiver's stored all-air u16+`0x16` and compares requirement-region needed-air+`0x0e`. It repeatedly calls`0x4ac3c0` with mode1 and remaining difference, then adds returned credit to a local progress variable. Ground shortfall uses receiver all-ground+`0x14` and needed-ground+`0x0c`; its **mode is1 when requirement-region enemy-air strength+`0x18` is nonzero, otherwise0**. Ten full sweeps vary receiver state4/5/6/7/9 and enemy-air word0/1, distinguishing gg43 from ag37 production. This mode rule is not a direct projection of the words “ground shortfall.”

The progress variable is separate from stored all-military strength and actual troop creation. Two complete no-donor sweeps with needed-ground161 and type-ground credit80 emit three requests, adding virtual credits80/80/80; stored all-ground remains0. Two further sweeps first borrow one donor for credit80, then make a later production request for credit120, covering161 without interpreting cached donor ground30 as that credit.

A small state4 terrain record with needed-ground2000 executes three complete calls with remaining differences2000,1001,2, receiving999 each time. It makes no request and leaves stored all-ground0 and needed-ground2000 unchanged. Thus999 is a **progress sentinel**, not evidence that999 strength was generated or that a defense task succeeded physically.

The selected full sweeps set PlayerAI flag8 to suppress the separate transport/observer request tail. Each still executes actual request-flag clearing/counter decrement. Transport capacity, observer request recipes and subsequent production completion remain separate compositions.

## Cached strength is a distinct producer

Two cases execute actual refresh`0x4a8f30` immediately before recipe borrowing, with Mutalisk43 maximum life100, life50/100 and type strengths air100/ground80. Refresh writes cached words50/40 or100/80. Subsequent ground borrowing returns **80 in both cases**, assigns the military node to the receiver and issues157. This composes the real cache producer with the consumer and verifies that recipe credit uses base type strength, while donor surplus/region sums read the cached values.

The exact rounded-life/maximum-life/caster/bunker/hallucination calculation, zero-form exclusions and16-bit cache writes are already established in [native unit strength](native-unit-ai.md). Its refresh-site fragments remain distinct from a proven every-tick schedule. This batch manually invokes the real refresh entry as a declared setup event; it does not prove new automatic cadence or a whole unit-object step.

## Reproduction and remaining boundaries

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_defense_recipes_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-defense-recipes-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-defense-recipes-probes.json
```

Without the execution flag the tool only validates/disassembles. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Probe script SHA-256: `8e5b0a34465b55d44a7b8fc40bd9a8eb341ff175aa9f723b99dbb195531c2a11`. Generated JSON records current dependency hashes, exact reviewed allowlists, complete returns, original call arguments/returns and checked state. Entries are bounded by100000 instructions.

The local binary determines these contracts. No modified AISE/Teippi policy supplies a callable helper or overrides allocation failure, type credit or sentinel scanning. Remaining compositions include nonnull occupied-garrison release, complex route/threat gates during bulk recipes, authentic campaign reachability of unusual/full recipes (zero-strength ones are excluded statically above), transport/observer tails, cache-refresh scheduling, later production/movement execution and campaign-wide behavior. A completed selected original recipe path is not proof of complete game simulation or remake parity.
