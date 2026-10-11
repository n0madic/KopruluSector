# Native 1.17 Computer AI nuclear scripts and return-to-base consumers

**20 complete selected cases pass**, comprising 2,423 original function invocations and 15,562,172 executed instructions. This batch connects genuine Computer 1 Town/Guard/Military records to campaign nuclear producers, authentic `NuHe` and a `TB2A` fragment, launch/impact, cancellation and Ghost death. It extends the earlier [Nuclear Strike](native-nuke.md) and [script opcode](native-ai-opcodes.md) contracts. It does not establish physical return to a base across an authentic campaign contour graph.

## Evidence and execution boundaries

The target is the exact local StarCraft.exe **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. The private runtime is x86_64 Darwin/Rosetta, Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6 and pefile 2024.8.26. There are no callable helper stubs, prefixes, Windows imports, WinMain, guest resource loading, host APIs/network or complete-game execution.

The [probe](../../tools/research/probe_native_ai_nuke_117.py) executes original instructions and actual reviewed callees. The ordinary direct-call bound is 100,000 instructions; complete `0x445070` object callbacks have a declared **8,000,000** bound. The largest observed invocation executes 922,163 instructions. [Probe evidence](evidence/native-ai-nuke-probes.json) retains exact returns/stack checks, calls, object states, stock Image VM/Bullet traces and all **112 imported-source hashes**, checked again after execution. The [static inspector](../../tools/research/inspect_native_ai_nuke_117.py) publishes [20 witnesses / 1,328 instructions](evidence/native-ai-nuke-static.json). Full static function bodies do not imply that every branch was exercised.

The new [asset manifest](evidence/native-ai-nuke-assets.json) describes **215 stock Image IDs / 186 distinct GRPs**, selected LO files and their archive provenance. Original bytes remain private. The separate [string manifest](evidence/native-hallucination-string-assets.json) authenticates network.tbl at `0x665de0` and stat_txt.tbl at `0x6ab4f8`. Unchanged effective retail DAT tables, IScript and both AI BINs are verified against their original manifest hashes.

Fixtures declare explored flat terrain, map/search rectangle `(0,0,4096,4096)`, finite object/order pools, player owner 3 with controller 1, owner 4 with controller 2 and diplomacy. Native creation/completion produces Ghost 1 and Silo 108 primitives. SCV construction and the Command Center addon producer are not exercised here; [connected AI construction](native-ai-construction.md) remains a separate contract.

The two-region graph is explicitly synthetic. Town creation produces regional state 5; the fixture does not invent that AI state. One-used-region variants retain region 2 and its native Town/base state. Neither variant supplies an authentic campaign contour graph. Auxiliary scheduler countdowns/lists are deferred or empty as declared. Full object callbacks execute actual movement, orders, timers, Images, Bullets, effects and recycling; the external frame counter is advanced explicitly. These calls do not certify accepted outer campaign frames, rendering, replay or campaign trigger execution.

All **256 native RNG source counters** are cleared and validated against the total counter and an independent integer LCG. The real Military arrival chain uses source 56. Original initializer `0x478240` clears a 0x400-byte source array, supporting this full counter extent; this batch does not modify earlier frozen probes.

## Native AI attachment and campaign missile creation

Original Town initializer `0x486a20`, Guard initializer `0x4b2640`, Military initializer `0x4a9a90`, Guard allocator `0x4b2010` and script-pool initializer `0x493330` execute. Native VM startup with declared bytecode `start_campaign; start_town; create_nuke; stop` (`56,3,107,36`) then produces the following records:

| Object | Original-produced record / relationship |
|---|---|
| Town | `0x690160`; attached Silo BuildingAI list |
| Silo 108 | BuildingAI `0x685578`, kind 3, genuine unit/Town links |
| Ghost 1 | GuardAI `0x6b6908`, kind 1, parent Ghost and home `(200,200)` |
| Region 2 | state 5 through Town assignment; region 1 remains state 0 |
| Nuclear Missile 14 | genuine completed hidden child, Silo ready DWORD 1, child `+0x80=Silo`, **AI pointer 0** |

`create_nuke` reaches `0x493450→424900→424da0→422ba0→48d320`. The Computer 1 tail genuinely calls `482520` and `487990`. **ID14 is explicitly excluded by `487990` before ordinary Guard/Military attachment.** Its null AI pointer is the original contract for this selected child, rather than a failed fixture allocation. The Silo and Ghost retain their original AI records. These script-created missiles do not charge minerals or gas; paid production remains in the earlier nuclear report.

An existing ready child prevents another allocation and preserves the original pointer/free Unit head. An exhausted real Unit pool returns with the unready Silo unchanged. `nuke_pos` with no ready missile leaves the Ghost at its original order 2. Each VM path executes real `stop` and removes its active script; no script is unlinked by host code.

The separate Military initializer case starts its 0x4e24-byte area with `a5` and checks **all 1,000 records**, stride 20: forward/backward free links, zero payloads and final free-head pointer. The reference byte array is independently constructed and compared with native output. Positive Military return uses records allocated from this genuine pool.

## Return-to-base means selection and issuance

Complete `0x4afd40` first rejects null AI. A unit already in region state 4/5 with an owned worker/building needs no return order. Otherwise it scans regional IDs from 1, accepts state 4/5 plus real `4861a0` owned Worker/Building query, and retains the strictly nearest center by original `405cf0` approximate distance. The strict comparison and first-ID tie behavior are statically recovered; multiple-base ranking is not independently probed here.

For the native Guard fixture outside its Town region, the function returns **1**, retains its Guard record and issues actual Move 6 toward the selected region center `(500,200)` through `42a340`. An empty original order pool still returns **1**, while the old order and point remain unchanged. This is not an allocation-success or path-arrival API. The one-used-base-region fixture returns **0** and retains order 2 even though the Ghost is not at the region center: base membership is checked at the regional level.

For type 4 MilitaryAI, the successful return executes actual `48dbc0` detach and `4ae760→4a9b20` reallocation/arrival into the selected region before Move issuance. The source ground count changes 1→0 and destination 0→1. The freed record is genuinely reused, points to the destination region, and the old Guard parent stays null. [Military arrival](native-military-arrival.md) and [AI removal](native-ai-removal.md) retain their broader contracts.

The complete two-region Nuke chain genuinely issues Move after release from tracking, but **physical return fails under the declared synthetic contour fixture**. At callback 317, `4afd40` requests `(500,200)`. Actual subsequent movement reaches `(43,44)` by impact callback 375 and then `(40,41)`; the Ghost dies from real collateral damage and its Guard parent/Unit AI are cleared. Missile/Bullet/effect cleanup completes. This measured failure is preserved as a named passing evidence case whose expected result is `physical_return_observed=false`. It is not evidence that original campaign navigation fails.

## Authentic `NuHe`: scenario targeting differs from reaction targeting

Effective AIScript SHA256 is `635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e`. The genuine ID resolver/launcher `493970→494d20→494d90` resolves **`NuHe` at PC46656**, whose bytes are `nuke_location 0x59; stop 0x24`. The original VM runs this complete script with a declared location rectangle `(0,0,250,350)`.

| Natural enemy fixture | Direct original `432150` contribution | Full `NuHe` output |
|---|---:|---|
| No enemy | none | point order 126 to location center `(125,175)` |
| Torrasque 48 at `(100,200)` | **0** | target-unit order 126 to that Torrasque |
| Active Missile Turret 124 | **800** | target-unit order 126 to the turret |
| Sunken Colony 146 | **800** | target-unit order 126 to the colony |

The natural Torrasque has status bit `0x20000`, which excludes its ordinary nonworker life contribution in `432150`. Keep this status bit separate from DAT Building property 1. The turret passes the real active-detector test; the colony uses the exact ID146 important-building branch. All three are stock-created/completed objects with authentic properties and images.

The low-score Torrasque is nevertheless selected because scenario selector **`432f60`** differs from the reaction selector. Candidate callback **`433620`** records an in-location enemy in shared pointer `665d34` before evaluating its neighborhood. If the search returns no candidate satisfying score≥800, `432f60` returns that shared pointer. Only when both are absent does `nuke_location` fall back to the location center. The single-candidate low-score fallback is executed; multi-candidate fallback enumeration remains an explicit boundary. In contrast, reaction callback `4335b0` requires score≥800 as recovered in [spell families](native-spell-families.md).

The script handler attaches the missile to the chosen Ghost without setting the enemy's ordinary reservation byte: target `+0x92` remains 0. Scenario issuance does not imply the reaction dispatcher's reservation/timestamp contract. These cases end after script issuance; their authentic target-unit scripts are not separately advanced to impact.

## Authentic `TB2A` fragment creates and orders a Guard Ghost

Effective BWScript SHA256 is `15a1a5b3fb4f2cab2e7b7b507ae04d4e2ef097c77081cfeb950d9a2e4ebee382`. The original bytes at **PC5215–5231** execute:

```text
5215 create_nuke
5216 create_unit(1,1974,3779)
5223 nuke_pos(800,3588)
5228 wait(40) → PC5231, wait field40
```

The whole authentic BIN is bound in private memory, and real launch allocation selects BWScript with entry PC5215. `4261f0` genuinely creates/completes Ghost 1 at `(1974,3779)`; `4b1fe0→4b2010` attaches a genuine type 1 Guard record. The owner-list insertion makes this Ghost the first eligible unit for `4932a0`; `nuke_pos` issues 126 to `(800,3588)` and stores this new Ghost in the missile connection. The original Ghost's existence is retained rather than replaced by an authored target pointer.

This is **entry into a genuine middle fragment**, not full campaign launch. The original `TB2A` entry PC5160 also contains `set_randomseed(123456789)`, `nuke_rate(8)`, `fake_nuke`, three `create_nuke` operations and `multirun(5197)`; the spawned sequence waits before PC5215. Those preceding operations and the continuation after wait40 are not executed by this case. The [campaign inventory](evidence/ai-inventory.json) establishes script/catalog reachability separately. Raw mission references launch `TB2A` in `expterran/terran05a`, and `NuHe` appears in 05a/05b; trigger condition satisfaction and complete mission execution remain distinct evidence.

## Computer 1 release, cancellation and death

Full object callbacks preserve actual facing, movement, missile state, IScript signals, marker effects and AI. The one-used-base-region paths give these observed durations from script issuance:

| Chain | Complete callbacks | Nuclear impacts | Guard result |
|---|---:|---:|---|
| Ordinary launch | 540 | 1 | collateral Ghost death removes Guard attachment |
| Cancel in ascent state1, before callback13 | 41 | 0 | Guard retained, parent remains Ghost |
| Cancel in descent state5, before callback317 | 540 | 1 | commitment survives disconnection; collateral clears Guard |
| Ghost Death in ascent | 177 | 0 | real Death issuance clears Guard |
| Ghost Death in descent | 481 | 1 | committed missile continues without Ghost connection |

Cancel runs actual selection consumer `4724f0` with declared selection pointers. Its direct result clears reciprocal connections while retaining Ghost AI. Death uses `42a4d0`: the immediate AI release and subsequent active Death disconnection are recorded separately. Selected frontend command-byte decoding is not executed here; the earlier nuclear report retains its static dispatcher witness.

When `NukeTrack129` releases a Ghost already in its base region, actual `4afd40` requires no Move. The ordinary PlayerGuard path then reaches **`41e7f0→41f6c0`**. Selected no-enemy acquisition executes real `4a9760/4a9860→4a90d0`, target comparison `4ae080`, Guard/local gate `4ae040`, home handling and ordinary acquisition. Successful ascent cancellation subsequently reaches original orders160/156 with its genuine Guard record. These are complete selected no-enemy consumers, not proof of every positive target ranking, attack issue, hangar parent or mixed-target branch contained in these functions.

## Source interpretation and remaining dependencies

Pinned [PyMS command IDs](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L253) and [operand schemas](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L431) corroborate labels and decoding. Teippi's [status constant](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.h#L39) corroborates the neutral `Reacts` name for bit20000; its [return helper usage](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L1291) is modified-source context. Their pinned local checkouts were read. The measured local 1.17 instructions establish semantics; no 1.16.1 address is transferred into this target.

Still separate: authentic cross-region contour/path arrival after NukeTrack, multiple-base distance/tie execution, multiple-enemy `NuHe` fallback ordering, full `TB2A`/mission scheduler and trigger-condition execution, frontend Nuke command admission, positive automatic acquisition after release, and cloak technology/secondary-order effects reached from approach helper `4b0210`. Full static wrappers containing UI, creep/power, other unit types or successful mixed-target branches remain bounded by their actually executed paths. The new results close the listed campaign nuclear producer/attachment/commitment consumers without treating these remaining dependencies as solved.

## Reproduction

Use the validated private x86_64 runtime. Default probe mode prints a plan; the explicit flag below enables the authorized isolated instructions. The retail app, extracted DAT/BIN files, PyMS checkout and selected private image resources must match their declared hashes.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_nuke_117.py \
  --exe "$RETAIL_APP/StarCraft.exe" \
  --out docs/research/evidence/native-ai-nuke-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ai_nuke_117.py \
  --exe "$RETAIL_APP/StarCraft.exe" \
  --data "$PRIVATE_DATA" --manifest "$PRIVATE_DATA/manifest.json" \
  --assets "$PRIVATE_AI_NUKE_ASSETS" \
  --asset-manifest docs/research/evidence/native-ai-nuke-assets.json \
  --string-manifest docs/research/evidence/native-hallucination-string-assets.json \
  --pyms "$PINNED_PYMS" --execute-original-code --profiles all \
  --out docs/research/evidence/native-ai-nuke-probes.json
```

Probe SHA256: `8372bcb13568260c61f29af59a6c13a80296fcf69bddf9b2b9ea7e0ad3e22138`. Inspector SHA256: `6e16f69fac36452c4f5d4a56f7f08226cc957012682b48543559af8695a6e98c`. New image manifest SHA256: `057d217d13f2939c6d3277b61de61ee4771a03d5e71d6358dc31ebf160467c54`.
