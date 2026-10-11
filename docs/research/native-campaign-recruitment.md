# Native 1.17 initial campaign roster recruitment

The periodic player sweep supplies the missing link between recording a state8 attack group and filling that group's military roster. It tries to borrow eligible units, then records real production requests for unmatched entries. **44 complete original-code cases pass**, including successful guard conversion, military reassignment, unmet-demand insertion and the full selected player sweep. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`.

Evidence: [14 static witnesses, 771 instructions](evidence/native-campaign-recruitment-static.json), [44 complete original-code cases](evidence/native-campaign-recruitment-probes.json), and [inspector/probe](../../tools/research/inspect_native_campaign_recruitment_117.py). The longest call uses2519 instructions. Actual donor selectors, original AI release/allocation, movement-order issuance, primary-order pool/list/activation, costs and request-heap insertion execute without helper stubs. Private fixtures supply units, cached strengths, region graph/geometry, DAT values and free pools; stock orders.dat supplies its unchanged189 order fields. No full game, WinMain, Windows API or host file/network operation executes.

## Scheduler to requested roster

Player sweep `0x4af930(ECX=player)` visits region indices **1 through actual_count-1**. States0..3 skip this recruitment work. State8 calls `0x4ab040`; it does not route state9 through that roster branch. Other states above3 use separate required-strength/recipe routines. Two complete calls execute the actual whole sweep with one state8 staging region, a donor region with no outstanding strength requirement, and transport-request suppression flag8. They convert a guard to MilitaryAI in the staging region and issue ordinary order158 through the real issuer. The other recipe, transport and observer branches remain separate contracts.

The caller is `0x483400`, at callsite`0x48349c`, after spending/account work and guard updates, when elapsed game time is at least10. That outer helper also requires player type1 and PlayerAI flag2. Its rotating player slot and8/38 object-step cadence are independently recovered in [frame scheduling](native-frame.md); this batch invokes the sweep directly, so it does not independently measure the outer cadence.

`0x4ab040(ECX=staging_region)` obtains missing roster entries through original `0x4aa500`. Campaign mode supplies its argument0; noncampaign mode supplies argument1. The roster stores `unit_ID+1`, and output sentinel228 is skipped. Empty roster and stored marker230, producing output229, request nothing. Duplicate requested words need separate satisfaction: after converting the only matching guard, a duplicate request creates one real production entry.

There is a campaign-specific distinction before recruitment: the argument0 missing-roster scan can count an **unfinished ordinary unit** even without MilitaryAI assigned to the staging region. A supplied incomplete Mutalisk43 therefore satisfies that roster entry; no adoption or request follows. The later recruitment eligibility helper itself requires completion. These are separate gates, and combining them into one completed-unit count would change the native behavior.

For every remaining output ID<228, the roster routine invokes `0x4ae960(region,ID,[priority])`. Priority is60 in campaign mode. Outside campaign mode it is160 when `0x483570` succeeds, otherwise60. That predicate succeeds when last time `PlayerAI+0x224` is0 or **unsigned elapsed-minus-last >=30**; it is distinct from the grouping time at+`0x228`. Fifteen cases compare donor priorities59,60,100,159,160 in campaign, noncampaign never-recorded, and noncampaign recent-time fixtures.

If adoption returns0, the routine calls actual `0x482990(player,region,[ID,60,1,1])`. The resulting heap record has **kind1, priority60, requested ID, context=staging region**. Synthetic fixture costs10minerals/5gas/2supply raise AI demand by those values. It does not pay current resources or create a unit at this point. Real request accounting/heap insertion is separately recovered in [default production](native-default-production.md) and [production contracts](native-production.md).

## Two native donor selectors

`0x4ae960` reads the staging center and destination region, then tries **`0x4ac230` surplus MilitaryAI selection first**. Only if that returns null does it call **`0x4ac0d0` guard/lower-priority fallback**. The probe traces retain both calls and their original arguments, so successful military surplus cases are distinguishable from fallback cases.

Both use `0x4aaf70` as a unit gate. It rejects inactive sprite/order state, DAT property`0x10`, absent UnitAI, a different requested type except stored Tank30 matching requested5, incomplete units, orders106/150, and a unit currently threatened according to `0x41fd90`. The surplus selector additionally asks to exclude InBuilding status. Ordinary type43 must have rounded current life at least half maximum life; cases49/50/51 against maximum100 prove inclusive equality. Literal exempt-type branches include0,32,37,38,65,66, with Marine0/order92 handling separate. The reached positive fixtures have null current targets, so the threat predicate returns through its real no-target branch. Positive threat, occupied-garrison/unload and other exempt-type paths are not replaced or established by these calls.

The **surplus selector** requires MilitaryAI in another assigned region and excludes donor states7,8,9. For nonzero cached air contribution, `donor_all_air - candidate_air >= donor_needed_air` suffices. For nonzero cached ground contribution, `donor_all_ground - candidate_ground > donor_needed_ground` is required. The arithmetic promotes the stored words before subtraction; it is not unsigned-word subtraction with wrap. Six composed cases deliberately set donor priority200 to block fallback, distinguishing air equality, air shortage, strict ground surplus, ground equality, and excluded donor states7..9.

Distance is native approximate distance from sprite position to staging center, with a strict smaller comparison preserving an existing equally close selection. Candidate ranking across multiple donors is visible statically but these composed fixtures provide a single candidate. A grounded candidate also calls route helper`0x496ac0` with actual current/staging region IDs. One complete Zergling37 case supplies identical IDs1/1 while its old MilitaryAI assignment is region3; the original route entry returns1 immediately. This preserves the separation between actual position and assigned region. The route's general different-ID branch is a dependency; its return value is not tested by the following surplus-selection instructions, and its possible effects cannot be replaced by an invented boolean eligibility check.

The **fallback selector** admits a guard for a campaign staging state8 and, outside campaign mode, for attacking state8/9. A military donor must be in another assigned region; noncampaign donors in states1/2 are protected by a separate gate. For attacking/staged destinations it also avoids borrowing from the destination's target region. Source priority must be **strictly less than** the supplied threshold: equal60 or160 rejects. The cases isolate that inequality by making the military donor fail the surplus selector. Guard conversion does not use that source-priority comparison. Detailed multi-candidate ranking, InBuilding priority20 behavior and non-null target fixtures remain explicit selected-path boundaries.

## Actual adoption and allocation failure

For a guard, `0x4ae960` clears the unit's AI pointer, calls real `0x4ae760(staging,unit,force=1)`, then sets the old guard's parent pointer+`0x0c` to0. It recomputes strength for the guard unit's **actual current terrain region**. It does not call the generic `0x48dbc0` removal path or unlink that guard record itself.

For a military donor, it calls real `0x4a9a30` on the old assigned region, clears UnitAI, then calls `0x4ae760` for the staging region and recomputes old-region strength. The released MilitaryAI node enters the shared free pool and can immediately be reused, so an initially empty destination pool can still succeed on this path. Guard conversion has no such released MilitaryAI node to reuse.

Eight complete combinations distinguish campaign flag, guard/military donor and initially empty/nonempty free pool. Successful ordinary adoption assigns MilitaryAI to the staging region and issues order158 with three actual RNG calls. The ground case also changes old/new ground-member counts from1/0 to0/1.

**Allocation failure is not propagated as adoption failure.** With a guard and an empty MilitaryAI pool, the real allocator returns without allocating. UnitAI stays0, the old guard parent becomes0, no order or RNG call occurs, but `0x4ae960` still returns1. The roster caller therefore inserts **no** production fallback in that invocation. This is a concrete native partial mutation; a remake must not infer successful assignment or restore the old guard merely from the helper's returned1. The next scheduler visit and guard lifecycle are separate behavior.

Pinned [Teippi AddToRegionMilitary](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L835) corroborates the two-selector structure and old-region strength update. Its modified guard branch explicitly moves list membership and its allocator uses a different implementation. Those policies are not imported into the local1.17 contract above. [AISE region layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates stored strength and military-list fields.

## Reproduction and remaining compositions

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_campaign_recruitment_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-campaign-recruitment-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-campaign-recruitment-probes.json
```

Default mode only reads/disassembles the hash-gated EXE and validates stock orders.dat. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Probe script SHA-256: `68089f936bb380ab1953b3e145a323991e83471758bde1dde5c33a41a2527605`. Evidence records script/dependency hashes, reviewed ranges, complete returns, actual call arguments and matched state. Each entry is bounded by100,000 instructions.

This closes selected original initial-roster adoption/request paths alongside [preparation](native-campaign-attack.md), [wave launch](native-campaign-wave.md), [positive attack consumers](native-attack-consumers.md) and [enemy-strength production](native-enemy-strength.md). It does not execute movement, production completion or a campaign mission. Still separate are multi-donor ties/priority combinations, non-null threat and garrison paths, general different-region route calls, strength-recipe recruitment for states4..7/9, and positive radius guard callback`0x4ada40`. That callback is included as static evidence but is not counted as a composed executed path in these44 cases.
