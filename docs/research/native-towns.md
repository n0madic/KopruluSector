# Original Town pools, creation and owner/area assignment

**35 complete cases pass through 478 original invocations.** Original initialization creates the shared100-Town,1000-WorkerAI and1000-BuildingAI pools. Complete Town allocation, exhaustion, empty deletion/reuse, start_town and start_areatown execute their actual callees. Connected cases assign genuinely created SCVs, Marines, Command Centers and Supply Depots. Owner-list creation and spatial-area creation have different admission and center-selection rules.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Addresses below are preferred VAs. This follows [worker records](native-workers.md), [AI removal/migration](native-ai-removal.md), [GuardAI lifecycle](native-guard-lifecycle.md), [economy requests](native-economy.md) and [populated construction](native-construction.md). Those frozen tools/reports are unchanged.

## Evidence and scope

[Static inspector](../../tools/research/inspect_native_towns_117.py), SHA256 `1ab41cf4d587ef7b61014e266f114a6bfce1a2aaa8cba7032a8748a93cbc152f`, produces [17 ranges /637 decoded instructions](evidence/native-towns-static.json), without undecoded tails. The area-center type switch remains separately decoded data.

[Original-instruction probe](../../tools/research/probe_native_towns_117.py), SHA256 `eb22c714e4cd2fd55106d122f2fc36a07fb8ce16e913b54fb90c8e7d5d8ea4ed`, produces [35 complete cases](evidence/native-towns-probes.json), **3,961,303 original instructions**, **173 visited range starts**, **994 reviewed ranges** and **70 authenticated source dependencies**. All478 calls return to the exact sentinel with native stack cleanup. There are no prefixes, helper substitutions or executed imports. Maximum invocation71,827 stays below250,000. RNG counters/total/final seed match an independent integer LCG.

The runtime is Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26. Original DAT/IScript/GRP/LO inputs use the unchanged [construction image manifest](evidence/native-construction-assets.json), SHA256 `85085b5d52a740a9fb1510303b918562c009a7a692a254dcbb2630b0a86cdd71`, covering143Image IDs/122GRP paths. Private retail payloads remain outside the repository.

Units use actual `424900 → 424da0` allocation/completion/Show on a declared clear4096×4096-pixel128×128-tile map, uniform region/group1, empty contours and empty creep. Initial unit/Sprite/Image/order/path pools are explicit finite inputs. Original `486a20` produces Town/WorkerAI/BuildingAI pools; `4b2640` produces GuardAI pools. Race1/controller2/owner3 and eight valid regional arrays are explicit inputs. Actual `482ee0` sets campaign flag`0x20`; it bypasses the noncampaign territory/strategic-target initialization in start_town. The request-reuse case explicitly changes controller to1 before actual build insertion, because that producer requires computer type1.

These are complete selected original functions and native-created objects. They do not execute the whole AIScript start opcode, computer initialization/frontends, accepted frame/object callbacks, worker maintenance, resource-area generation, map loading or a campaign. Busy-order cases issue actual orders; incomplete-bit cases explicitly clear that tested bit. Those inputs are recorded rather than attributed to a natural construction producer. No WinMain, Windows API, guest host-file or network operation executes.

## Pool initialization and capacity

`486a20` calls WorkerAI initializer`483da0` at`67f7b0`, BuildingAI initializer`483bd0` at`685578`, and Town initializer`483ca0` at`690160`. It then sets all eight `(pool,active_head)` pairs at`69b518+8*player` to `(690160,0)` and clears`685574`. The pools are shared across players, not100Towns for each player.

| Pool | Stride | Slots | Free-head offset | Native initializer |
|---|---:|---:|---:|---|
| Town |460 (`0x1cc`)|100|`b3b0`|`483ca0`|
| WorkerAI |24|1000|`5dc0`|`483da0`|
| BuildingAI |44|1000|`abe0`|`483bd0`|

Each native initializer constructs its doubly linked free list. A complete case fills all bytes with`0xa5` first, executes whole`486a20`, and compares all three complete pool byte arrays. Every Town preserves the fixed WorkerAI/BuildingAI pool pointers at+8/+10, clears worker/building heads at+c/+14, and zeroes bytes+18..+1cb. BuildingAI initializer deliberately leaves padding bytes+e/+f unchanged; their observed165/165 is not an assumed zero field.

`485770` uses ECX=player, EDX=x, stack=[y], `ret4`. It pops the shared free head, clears the next free node's previous link, and prepends the Town to that player's active list with previous0. It writes owner byte+18, WORD x/y at+20/+22, resets initialization/count bytes+19/+1a, byte+1b, WORD+1c/+1d, main/builder/resource pointers+24..+38 and **400request bytes at+3c** through original memset`4d0c60`. It preserves the fixed pool pointers and existing empty worker/building heads established by the lifecycle.

A complete sequence alternates owners3/1 for100successful allocations, then returns0 on the101st call. Each owner has50active Towns and the common free head is0. No host write fakes allocation/exhaustion. Wide coordinates truncate to low16 bits, including negative y; owner remains a byte.

Town index decoder`4877d0` returns0 for index0, otherwise `(index*460+0x68ff94) mod2^32`. Six complete leaf cases include indices1,100,101,65536 and`0xffffffff`. This decoder does not enforce the100-slot bound and does not dereference the resulting pointer.

A native build request is inserted into a newly allocated empty Town using`487550`. Actual`4856a0` deletes it through its original empty-town/script/resource guards, returns it at the free-list head, and a subsequent`485770` reuses the same address. All400request bytes become0; the fixed WorkerAI/BuildingAI pointers survive. This composes empty deletion with allocation, without claiming the populated migration paths anew.

## Owner-list start_town

`487030` uses ECX=player, EDX=center_x, stack=[center_y,area_argument], `ret8`.

1. `4256f0` reads the player's actual owned-unit head. Null returns0 before Town allocation or parentless Guard cleanup.
2. If that player's active Town head is0, `4b21f0` removes its parentless Guards. This happens **before** allocating the Town.
3. `485770` allocates the Town at the caller's low16 center. Failure returns0.
4. Town+1c receives the low byte of the full area argument; +1d receives `(full_argument !=0)`. Thus argument256 produces area0 with refresh flag1. It is not equivalent to argument0.
5. The native owner chain follows CUnit+6c. `41fe40` rejects missing Sprite or primary Death0. `4b14d0` rejects incomplete units and current primary order 106 or ResetCollision150. Order 106 is CompletingArchonSummon (dispatched to Archon meld handler `42f9e0`), not a Train order; corrected after the function atlas ([P29](atlas/p29.md)). The probe case IDs keep the earlier `train106` name. A nonnull unit+134 AI pointer also skips assignment. Remaining units call actual`484cd0(Town,unit)`.
6. Campaign flag`4831e0` bypasses`4979b0/4b0be0`. The noncampaign path seeds territory with5for the first Town,2otherwise, then performs strategic-target processing; these callees remain outside this batch's connected coverage.

A nonempty owner chain is sufficient to allocate a Town even if every candidate is rejected. The genuine SCV admission matrix covers far-away, incomplete, order106, ResetCollision150, Death and already assigned AI. The far SCV at2000,512 is accepted into a Town centered512,512: this owner-list path does not impose a radius. The other five variants leave the new Town's worker count0; an existing actual WorkerAI stays associated with its old Town.

A selected SCV receives actual WorkerAI type2, assignment1, native update timestamp, cleared assignment metadata, unit reference+10 and Town reference+14. Its CUnit+134 becomes the genuine pool node and the Town's worker count increases. Native worker list insertion is original code.

Four complete Guard-ordering cases distinguish no owned units, first Town, second Town and naturally exhausted100-Town pool. No-unit and second-Town paths retain a precreated parentless Guard. First-Town success removes it; first-Town **allocation failure also removes it**, leaving no new Town or WorkerAI. A rewrite that allocates first and cleans Guards only on success would change this original failure behavior.

## Building assignment and regional effects

Native-created SCV7 plus Command Center106 or Supply Depot109 are assigned by complete start_town. Each receives one genuine BuildingAI type3 from the shared pool; Town+19 becomes initialized1 and worker count1. The Command Center becomes Town+24 main; the Supply Depot does not. The input runtime race byte selects main106 for Terran,131for Zerg and154otherwise through`485ef0`.

`484cd0` classifies workers by DAT property8; ordinary buildings except Geyser188 and the selected35/36/42 special types enter its separate building branch. Actual building record allocation/initialization and `4b00a0` regional assignment run. In the declared campaign state, complete Command Center assignment reaches region state6 and the Supply Depot reaches5, with original state/strength mutations. No region state is assigned by the host to stand in for that producer.

Hatchery-family/Overlord/Larva/Egg assignment, military conversion, noncampaign strategic initialization, matching race gas references and their resource-area refresh still require their own connected variants.

## Spatial start_areatown and center precedence

`486e80` uses ECX=player, EDX=pointer to four DWORD rectangle coordinates, stack=[area_argument], `ret4`. It copies their low WORD values into a native rectangle, then calls actual`422460` filtered spatial query with original owner-equality callback`485760`. The filter compares the unit's owner byte with full EDX player. Empty result returns0 after original query disposal`4222b0`; no Town is allocated.

A nonempty result allocates a Town initially at0,0. It writes the same area/refresh bytes as start_town, then visits the native query list. **The only pre-assignment check here is unit+134==0.** It does not call the owner-list path's Death/Completed/busy-order predicates. A genuinely Death-issued SCV still present in spatial storage and an explicitly incomplete SCV both receive actual WorkerAI in these selected complete area calls. Later Death cleanup/maintenance is outside the cases. An already assigned SCV leaves the new Town empty and does not influence its selected center.

The positive overlap case has SCV center391,512 while the area's left edge is400: its native footprint intersects the rectangle and is admitted. An SCV at1000,512 is excluded. Owner4 is excluded from an owner3 query. The host does not author a candidate-list result or bypass spatial endpoints.

The exact native type switch covers IDs7..154:

- Worker IDs7/41/64 select their Sprite center only when no center candidate exists.
- Main building IDs106/131/132/133/154 select their Sprite center unconditionally, overriding an earlier candidate.
- Other IDs do not select a candidate.

Consequently a Marine-only query allocates an empty Town at the rectangle midpoint600,600. A SCV-only query selects512,512. SCV+Command Center selects the building700,600; SCV+Supply Depot retains the worker512,512. The main pointer is independently assigned by the building consumer. If there is no eligible candidate, midpoint uses original signed32 subtraction/division-by2 truncating toward zero, then lowWORD addition/truncation; these positive examples use ordinary bounds.

Query traversal order determines which main building is last and which worker is first. Multi-main/multiworker exhaustive ordering, wrap/negative rectangle inputs and full mission spatial state remain separate.

## Reproduction and remaining composition

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_towns_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/towns-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_towns_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-construction-assets \
  --asset-manifest docs/research/evidence/native-construction-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/towns-probes.json \
  --case-group all --execute-original-code
```

See [reproduction prerequisites](reproduction.md) for original extraction and the pinned runtime. Without the explicit execution flag, the tool prints its plan only.

The Defold simulation needs a distinct Town ownership/list lifetime, persistent requests and resource references, AI attachment eligibility by caller, finite pool failure behavior and native region side effects. The next connected contracts are full AIScript Town startup, resource-area generation/allocation, request satisfaction→spending→builder/placement→paid completion, recovery/migration and authoritative frame cadence. These35cases do not establish that whole AI economy or certify campaign parity.
