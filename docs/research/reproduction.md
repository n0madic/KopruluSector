# Reproducing the StarCraft research

The static tools read archives and machine code. The separately flagged probe tools execute selected original x86 functions in private Unicorn memory after the user's explicit authorization for these and subsequent isolated probes. No tool launches the installer, WinMain, a complete game, Windows APIs or original DLLs. Original extracted binaries belong in scratch; project evidence consists of aggregates, selected instruction witnesses and bounded function observations.

## Tools and inputs

The investigation used innoextract 1.9, Python with pefile/capstone, the PyMS x86_64 StormLib, and these diagnostic families:

- [inspect_campaigns.py](../../tools/research/inspect_campaigns.py): five archives, explicit overlay, selected DAT/TBL/AI/IScript, CHK/TRIG/MBRF inventory.
- [inspect_ai.py](../../tools/research/inspect_ai.py): AST schema reading, lexical decoding, conservative CFG. PyMS Python is parsed without importing or executing it.
- [inspect_native_117.py](../../tools/research/inspect_native_117.py): known-hash EXE, AI table and selected blocks, RNG and derived vectors. Unknown EXE/schema hashes stop before publication.
- [inspect_ai_opcodes_117.py](../../tools/research/inspect_ai_opcodes_117.py): all 113 native wrappers, script directory/pool/list contracts; optional isolated VM/helper probes.
- [inspect_economy_frame_117.py](../../tools/research/inspect_economy_frame_117.py) and [probe_native_leaf_117.py](../../tools/research/probe_native_leaf_117.py): frame/economic/RNG static witnesses and isolated RNG, percentage, request, heap and limit cases.
- [inspect_native_triggers_117.py](../../tools/research/inspect_native_triggers_117.py) and [probe_native_triggers_117.py](../../tools/research/probe_native_triggers_117.py): trigger/campaign tables and selected native condition/action/timer cases.
- [probe_native_briefing_loader_117.py](../../tools/research/probe_native_briefing_loader_117.py): briefing/CHK loader decisions and optional original CHK section/required-presence input coverage.
- [inspect_native_combat_ai_117.py](../../tools/research/inspect_native_combat_ai_117.py) and [probe_native_combat_ai_117.py](../../tools/research/probe_native_combat_ai_117.py): targeting, spell and regional witnesses, with selected native numerical/selection cases.
- [inspect_native_workers_117.py](../../tools/research/inspect_native_workers_117.py) and [probe_native_workers_117.py](../../tools/research/probe_native_workers_117.py): worker/town/placement witnesses, native migration, region lookup and explicitly labeled selection-prefix cases.
- [inspect_native_production_117.py](../../tools/research/inspect_native_production_117.py): costs, requirements, request satisfaction, queue/refund transitions and optional native consumer-prefix probes.
- [inspect_native_training_117.py](../../tools/research/inspect_native_training_117.py): supply, actual payment, queue metadata and ordinary request consumers.
- [inspect_native_morph_selection_117.py](../../tools/research/inspect_native_morph_selection_117.py): original parent and merge-pair selection, list order and energy boundaries.
- [inspect_power_asset.py](../../tools/research/inspect_power_asset.py) and [probe_native_placement_117.py](../../tools/research/probe_native_placement_117.py): power GRP provenance, ordinary/creep/Nydus/power/occupancy validation, complete predicates and an explicitly labeled post-allocation mask fragment. Asset and execution commands are in [native-placement.md](native-placement.md).
- [probe_native_trigger_counters_117.py](../../tools/research/probe_native_trigger_counters_117.py): scalar trigger queries/setters and original campaign parameter coverage; commands are in [native-trigger-counters.md](native-trigger-counters.md).
- [probe_native_trigger_ranking_117.py](../../tools/research/probe_native_trigger_ranking_117.py): Opponents coalition and scalar most/least predicates; all 68 CHKs are audited for use.
- [inspect_native_damage_117.py](../../tools/research/inspect_native_damage_117.py) and [probe_native_damage_117.py](../../tools/research/probe_native_damage_117.py): native damage, armor/size, matrix/shields, selected attribution and producers; commands are in [native-damage.md](native-damage.md).
- [inspect_native_research_upgrade_117.py](../../tools/research/inspect_native_research_upgrade_117.py): actual research/upgrade starts, progress bits, waiting decisions and refunds; commands are in [native-research-upgrade.md](native-research-upgrade.md).
- [probe_native_map_overrides_117.py](../../tools/research/probe_native_map_overrides_117.py): original map availability/stat overrides; complete setters and explicitly labeled post-import fragments.
- [inspect_native_requirements_117.py](../../tools/research/inspect_native_requirements_117.py) and [inspect_native_dat_requirements_117.py](../../tools/research/inspect_native_dat_requirements_117.py): native unit-production and general order/research/tech-use/upgrade tables, interpreters and wrapper probes.
- [inspect_native_unit_ai_117.py](../../tools/research/inspect_native_unit_ai_117.py) and [probe_native_unit_ai_117.py](../../tools/research/probe_native_unit_ai_117.py): strength/cargo, dispatch routes and selected native order transitions.
- [inspect_native_military_ai_117.py](../../tools/research/inspect_native_military_ai_117.py) and [probe_native_military_ai_117.py](../../tools/research/probe_native_military_ai_117.py): guard recall, cohesion, transport waits and Stim decision gates; dependency script hashes are recorded.
- [probe_native_unit_properties_117.py](../../tools/research/probe_native_unit_properties_117.py): UNIT property normalization and campaign start locations, using synthetic and original CHK inputs.
- [verify_research.py](../../tools/research/verify_research.py): English/local-link/JSON/Python syntax checks and recorded probe freshness; this verifier executes no original code.

Pinned PyMS commit: bfc5d3aad0b5614a5aff72c223f8efa00afddfa4. Inputs are PyMS/FileFormats/AIBIN.py, PyMS/Data/Listfile.txt, and PyMS/FileFormats/MPQ/StormLib.dylib. The bundled dylib is x86_64, so this Mac used `arch -x86_64 /usr/bin/python3`. This executes an open-source analysis library, not the original game. Other platforms can use a compatible StormLib build.

## Commands used

Installer scratch copy: /private/tmp/koprulu-retail.jJ8D4z/. Both multipart BIN files must remain alongside Setup.exe. The copy was created with APFS cp -c. innoextract reads the package itself without executing the Windows installer.

```sh
innoextract --silent \
  --include app/StarCraft.exe --include app/patch.txt \
  --include app/Patch_rt.mpq --include app/StarDat.mpq \
  --include app/BrooDat.mpq --include app/StarCraft.mpq \
  --include app/BroodWar.mpq \
  --output-dir /private/tmp/koprulu-retail.jJ8D4z/extracted \
  /private/tmp/koprulu-retail.jJ8D4z/StarCraft_Repack_Setup.exe
```

From the KopruluSector root, with the current scratch inputs:

```sh
arch -x86_64 /usr/bin/python3 tools/research/inspect_campaigns.py \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --out /private/tmp/koprulu-campaign-recheck \
  --listfile /private/tmp/koprulu-pyms-campaign/PyMS/Data/Listfile.txt \
  --version 1.17.0.1

python3 tools/research/inspect_ai.py \
  --data /private/tmp/koprulu-campaign-recheck \
  --schema /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/AIBIN.py

python3 tools/research/inspect_ai_defense_strength.py \
  --data /private/tmp/koprulu-campaign-recheck \
  --schema /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/AIBIN.py \
  --out docs/research/evidence/ai-defense-strength.json

python3 tools/research/inspect_native_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --schema /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/AIBIN.py \
  --out /private/tmp/koprulu-native-recheck
```

MPQ --version is a report label, not version detection. The separate [PE preflight](evidence/executable-preflight.json) establishes executable version. JSON ordering is deterministic and results are compared with docs/research/evidence. A different library, locale, listfile, or archive set is a different input profile.

## Isolated original-function runtime

On this Apple Silicon host, the installed arm64 Unicorn 2.1.4 process crashed at `mem_map`, including a probe containing only an analysis-authored NOP. No original instructions were reached in that failed attempt. An isolated x86_64 Python environment under Rosetta successfully mapped memory and ran the NOP, then the allowlisted original functions. This is an observed host-library limitation and workaround, not a discovered game crash.

```sh
arch -x86_64 /usr/bin/python3 -m venv /private/tmp/koprulu-native-x86-venv
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 -m pip install \
  --no-cache-dir unicorn==2.1.4 pefile==2024.8.26 capstone==5.0.6
```

The runtime is Python 3.9.6, x86_64 process, x86-32 guest on this machine. Every execution runner checks the exact EXE SHA-256, maps a private PE image plus synthetic state and stack, and rejects instructions outside its own reviewed allowlist. It does not resolve imports. Function-prefix cases stop before unresolved operations; those operations are not replaced with a helper stub.

These commands publish the corresponding evidence. Execution flags are explicit; default probe modes only describe the plan. The user has authorized these and future probes within this same isolated scope. Full game execution remains a different action.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_leaf_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --execute-original-code --out docs/research/evidence/native-leaf-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_ai_opcodes_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --schema /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/AIBIN.py \
  --inventory docs/research/evidence/ai-inventory.json \
  --out docs/research/evidence/native-ai-opcodes-static.json \
  --execute-isolated-original-code --probe-out docs/research/evidence/native-ai-opcode-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_triggers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --execute-original-code --out docs/research/evidence/native-trigger-leaf-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_briefing_loader_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --execute-original-code --out docs/research/evidence/native-briefing-loader-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_combat_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --execute-original-code --out docs/research/evidence/native-combat-ai-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_workers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --execute-original-code --out docs/research/evidence/native-workers-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_production_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-production-static.json \
  --execute-isolated-original-code --probe-out docs/research/evidence/native-production-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_requirements_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-requirements-static.json \
  --execute-isolated-original-code --probe-out docs/research/evidence/native-requirements-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_dat_requirements_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-dat-requirements-static.json \
  --execute-isolated-original-code --probe-out docs/research/evidence/native-dat-requirements-probes.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_unit_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --execute-original-code --out docs/research/evidence/native-unit-ai-probes.json
```

The static extractors for economy/frame take an output directory. Worker/combat/trigger/production extractors take a JSON output filename. Trigger extraction additionally requires the scratch CHK directory and archive manifest. Do not interchange the two output argument conventions. Each JSON lists the cases/blocks actually covered; those counts may expand as recovery continues.

Additional connected batches have their exact input/command requirements in their reports: [building spending dispatch](native-spending-dispatch.md), [primary-order queues](native-primary-orders.md), and [trigger unit/location counts](native-trigger-unit-counts.md). Primary orders require the effective original `orders.dat`; trigger unit counts require the effective DAT/CHK inputs and pinned PyMS reader. Their passing counts retain complete-function/prefix distinctions.

[Default production](native-default-production.md), [guard lifecycle](native-guard-lifecycle.md), [guard transfer](native-guard-transfer.md), and [AI candidate placement](native-ai-placement.md) extend these paths. Guard transfer uses unchanged stock `orders.dat` flags. AI placement's stock batch requires the effective `units.dat` and pinned PyMS reader; its terrain, power and region fixtures remain synthetic. Reproduction commands and exact hashes are recorded in those reports.

[Train lifecycle](native-training-lifecycle.md) adds 539 checks, explicitly split into 534 complete returns and five prefixes. Its reports record the runner and all five extractor dependency hashes; successful normal hidden completion is not inferred from complete visible-child fixtures.

[AI removal/town migration](native-ai-removal.md) adds 113 complete original calls. [Trigger unit actions/outcomes](native-trigger-unit-actions.md) adds 6,548 complete calls, including connected Create allocation-failure paths that distinguish the auxiliary random state from the simulation LCG. [Region paths](native-region-paths.md) adds 505 complete calls with supplied graphs, actual connected distance and single-cut helpers, including heap/arena exhaustion and heuristic counterexamples. Each report records its specific remaining caller/world-state boundaries.

[Military arrival](native-military-arrival.md) adds 115 complete calls through actual MilitaryAI pools and primary orders, using stock `orders.dat`. It closes selected previous Train military-assignment prefixes and records exact source56 RNG/table geometry; its strategic-wave and positive combat dependencies remain explicit.

[Campaign attack preparation](native-campaign-attack.md) adds 80 complete original calls, including successful staging selection through native classifiers and actual `attack_prepare`, `attack_do`, and `attack_clear`. Its explicit state9 fixture isolates VM continuation; actual regional progression and military wave transfer remain separate.

[Hidden-unit completion](native-unit-creation.md) adds 543 cases: 530 complete returns and 13 explicit prefixes. It connects stock Train to placement/Show/AI/rally, generated vision and actual fog writes, and cancellation to Death issuance. Successful image/iscript allocation and later Death recycling remain separate.

[Trigger ownership](native-trigger-ownership.md) adds 1,245 complete original Give cases, including all 154 decoded campaign parameter tuples, actual accounting/list/order changes, researched-state copying and connected stale-cache sequences. Linked-unit, positive AI and iscript speed profiles remain explicit dependencies.

[Region generation](native-region-generation.md) adds 452 cases with 507 complete original-function returns and 110 explicit fragment completions. It connects VF4 flags/walk/classes to complete generated graphs and 22 original long paths. CV5/MTXM import, collision contours, dynamic terrain and authentic campaign geometry remain separate.

[Terrain assets](terrain-assets.md) validates all40 original tileset resources and1,045,504 cells across68 CHKs with hash-verified MPQ inputs. The aggregates and sample pixel hashes are static/derived; original payload is extracted only to scratch. This report's source/manifest freshness is checked separately from original-code counts.

[Campaign wave launch](native-campaign-wave.md) adds127 complete calls from actual state8/9 dispatch through readiness or timeout, military transfer/arrival/dispersion and subsequent `attack_do/clear`. Positive target scoring, periodic recruitment, transport and physical movement remain broader work.

## Validation boundaries

The MPQ tool checks complete reads. CHK parsing checks header/section bounds and 2,400-byte trigger records. AI decoding checks tables, operand bounds, known opcodes, and jump targets at instruction boundaries. CFG includes both conditional branches, call target/continuation, and spawn target/continuation; disabled/impossible trigger actions remain included. That inventory estimates requirements without running the programs.

The external listfile supplies candidate additional CHK names. Catalog coverage is checked separately against mapdata.dat/mapdata.tbl. The 68 found resources describe that union, not exhaustive unknown MPQ filenames. Default StormLib locale does not establish installation language.

The initial static/derived batch reran the project scripts and compared JSON with its first evidence. Native inspection rejection was checked against an altered EXE. The Lua RNG specification was checked against integer-derived vectors. Subsequent isolated batches include original instruction execution and record script hashes, runtime versions and limits. These are distinct evidence classes: a real original leaf is stronger than a derived arithmetic vector, but still does not establish complete gameplay, rendering, audio, campaign progression or save/load continuity.

Run `python3 tools/research/verify_research.py` after publishing a batch. It checks script syntax, English prose, local link targets, JSON parsing, target identity where recorded, probe script hashes and recorded counts/results. Its [verification report](evidence/verification.json) is an artifact-consistency result, not a new execution or a gameplay regression result. A source change requires regenerating its probe report before this check can pass.

Scratch paths are temporary. To reproduce later, extract local archive copies and change --app, --out, --stormlib, --listfile, and --schema; pinned inputs, hashes, and the analysis method remain documented in the project.

[Terrain import](native-terrain-import.md) adds405 cases,397 complete original-function returns and82 explicitly labeled fragment completions. Complete MTXM/CV5 import covers all68 authenticated CHKs; flag fragments cover all33,433 VF4 records. Height queries cover ordinary/alternate selection and VF4 bit precedence. Loader/defaults, dynamic tiles, contours and authentic full graphs remain separate.

[Strategic targeting](native-strategic-targets.md) adds82 complete no-stub cases for positive threat/life scoring, regional filters/ties, actual spatial-query target fields and global building-tier/distance selection. Upstream strength, order consumers and retarget-to-transfer composition remain separate.

[Trigger Move](native-trigger-move.md) adds475 complete no-stub cases, including all56 authored records and ordinary/flying/floating relocation, rollback quantity charging, references, vision and actual ring/index handling. Grounded buildings and positive AI/child/path profiles remain separate.

[Image initialization](native-image-initialization.md) adds242 complete assertion cases: real stock main/shadow IScript, first image updates, Goliath/Tank child and LO, creation RNG, pool failures/deferred rollback and connected Train allocation. Asset extraction provenance is checked separately; pixel rendering/resource loading, building/special profiles, populated completion and pending CUnit recycling remain separate.

[Attack consumers](native-attack-consumers.md) adds20 complete no-stub compositions for positive regional target-to-native order issue and actual state8 global target selection/state9 transfer/arrival. Recruitment, upstream enemy strength, physical attacks and other state handlers remain separate.

[Authentic campaign regions](native-campaign-regions.md) adds340 cases with544 complete function returns and340 explicit fragment completions. Complete graph/rank builders run on all68 original map inputs;272 native paths use their actual ordered graphs. Contours, dynamic terrain, full world setup and local movement remain separate.

[Enemy strength](native-enemy-strength.md) adds53 complete no-stub cases for original all-computer regional rebuild, native ownership/hostility/lookup, physical flight buckets and wrapped accumulation. The actual function4abf90 is a strength producer; cached unit-strength and recruitment/caller lifecycle remain separate.

[Grounded-building Move](native-trigger-grounded-move.md) adds2171 complete no-stub cases for selected Terran/Protoss relocation, stock physical validation, occupancy and alignment, bounds and all21 authored grounded-eligible records in two profiles. Positive creep, AI/child/path links and construction/addon cancellation remain separate.

[Contours and growth](native-contours.md) adds98 geometry cases with490 complete init/sort returns and98 explicit traversal fragments, plus12 growth cases with108 explicit fragments and no complete growth-function claim. Geometry covers all68 authentic walk grids after whole-byte comparison with recorded native producers; capacity15000 is a private fixture. Native clear/copy/publication execute across explicitly separated import boundaries.

[Initial campaign recruitment](native-campaign-recruitment.md) adds44 complete no-stub cases for actual state8 missing-roster/adoption/request paths, donor surplus/fallback gates and allocation-failure partial mutation. Other-state recipes, multi-donor/threat/garrison profiles and the outer periodic caller remain separate.

[Radius recruitment/regional sweep](native-radius-recruitment.md) adds28 complete no-stub cases for actual positive spatial callbacks, cleanup/allocation/arrival, sequential pool exhaustion and333-slot cursor/rebuild timing. Other regional states, the melee planner and physical movement/combat remain separate.

[Populated Train/recycling](native-populated-lifecycle.md) adds175 complete cases with55,478 original entry invocations, including full stock timers, populated child Show/AI/rally, hidden image/vision progression and actual pending-list return/reallocation. Repeated progress calls remain grouped in their cases. Full frame scheduling, visible Death, creep retirement and morph/building/special profiles remain separate.

[AI routes](native-ai-routes.md) adds48 complete no-stub cases for actual military cached flood, threat barriers, time/source invalidation, strategic retained-route filters and the donor ignoring a zero route return. Multi-donor/adoption/authentic-map compositions and physical movement remain separate.

[Populated Move](native-trigger-move-images.md) adds768 complete no-stub cases for actual stock image animation resumption, queued primary activation and selected Walking speed/velocity paths. AI/path/linked children and general movement remain separate.

[Donor ranking](native-donor-ranking.md) adds72 complete no-stub cases for multi-candidate ties/distance/priority, protected states and full adoption/arrival despite a zero strategic-route return. Broader regional actions/recipes and physical movement remain separate.

[All-map footprint fitting](native-contour-fit.md) adds8937 complete cases and9213 original entry returns with authenticated terrain/contour byte inputs, stock extents, point gates and rectangle rules. Native sorting/input authentication is separate from world setup and movement.

[Terrain sweep/cache](native-terrain-sweep.md) adds3241 complete no-stub cases for directional interval/corner policy and actual persistent cache hit/rebuild, with explicit synthetic unit/contour inputs. Native vertical/horizontal policy differs from pinned OpenBW; authentic/multi-record movement, invalidation, local routes and unit collision remain separate.

[All-map terrain movement queries](native-campaign-terrain-sweep.md) adds10608 complete cases and11696 original entry returns on all68 full authenticated contour arrays. Each stock source point passes original footprint validation; every prospective movement checks stored safe bounds and collision while retaining the cache. Source positions remain stationary fixtures; invalidation callers, local routes, other-unit collision and movement FSM remain separate.

[Movement bounds/unit collision](native-movement-collision.md) adds3225 complete cases and12285 original returns covering all stock map bounds, native pair policy and positive swept query→first blocker→mover gate. Endpoint tables/ties are fixtures; insertion, response, invalidation and movement FSM remain separate.

[Regional states0..7](native-region-states.md) adds183 complete no-stub cases through actual decay, reserve, support, threat and defense handlers, including positive transfer/arrival. Recipes, cached strength producers and complete world-state progression remain separate.

[Regional defense recipes](native-defense-recipes.md) adds108 complete no-stub cases for category selection, base-type credit despite failed military allocation, actual request insertion, sentinel999, filled-array scanning and full periodic recruitment compositions. Unit cache refresh executes as an explicit setup event; its automatic cadence remains separate.

[Observer/transport support](native-ai-support.md) adds101 complete no-stub cases from periodic count/region policy through actual generated requests, original prerequisite evaluation, payment/queue metadata, failure/retry and selected constructed completed-child assignment. New Train child creation, natural counters, transport loading and Observer control remain separate.

[Populated unit Death](native-unit-death.md) adds114 complete cases with4881 original entry invocations for stock visible/hidden destruction, turret image transfer, Marine corpse allocation/recycling, pool exhaustion, hallucination/self-destruct branches and full Train cancellation refunds. The additional death-image asset manifest is authenticated independently. Explicit calls to pending/Thingy consumers do not establish complete accepted-frame ordering.

[Creep/grounded Zerg Move](native-trigger-creep.md) adds4372 cases:4335 complete original paths,31 explicit import-bounded prefixes and6 pure CRT arithmetic suffixes. Positive old-area creep retention/removal, finite receding pool and both border tables execute; full new-tile stamping, CRT context acquisition and disappearance scheduling remain separate.

[Active units/gameplay IScript](native-active-iscript.md) adds434 complete cases with1871 original entry invocations, including14 populated two-object-step compositions and stock Zealot melee animation→damage. Bullet/independent-flingy lists and selected periodic consumers return through explicit empty/positive-countdown guards; full combat and accepted-frame scheduling remain separate.

[Move path/AI/turret links](native-trigger-move-links.md) adds183 complete cases for actual old-path disposal, accepted/rejected relocation, nonnull AI destination guards, linked unindexed turrets and fixed-point sprite rows. Immediate relocation updates Sprite before requesting the same destination, so the measured equal-position guard returns before strategic AI access.

[Path storage/waypoint reuse](native-path-storage.md) adds6327 complete cases and7921 original returns for all0..50 point/region lengths, finite-pool failure, packed copying/restoration/reuse, all byte retry timers and selected heading/region waypoint policy. Search outputs are explicit fixtures; path construction, walking and OS pool initialization remain separate.

[Reaction spells](native-reaction-spells.md) adds586 complete cases for stock weapon flags, actual target/readiness/permission, positive Matrix/Hallucination/Plague decisions, reservation/order-pool behavior and original cross-caster Storm→Plague shared-score sequences. Spell movement, energy debit and effects remain separate.

[Image geometry/ordering](native-image-geometry.md) adds43354 complete original calls over all2940 frames in the23 recorded stock GRPs, ordinary/shield LO consumers, layer wrappers and nonempty row insertion. Resource loading and render callbacks remain separate; pixel-buffer work is not a full rendered-game observation.

[Local paths](native-local-paths.md) adds572 complete cases and1064 original returns through local search, stock rectangle detours, parent-chain limits, empty-pool failure and long/local segment continuation using one actual finite record. All selected returned detour segments pass independent rational obstacle tests. Sprite movement, authentic campaign local routes, positive unit blockers and capacity/fallback profiles remain separate.

[Reaction spell families](native-spell-families.md) adds145 complete calls for ordinary caster decisions and positive order consumers, Nuke score/timestamp/attachment, Recall regional permission, Dark Swarm overlap and the campaign Queen's low-byte RNG branch. Subsequent spell movement, debit, effects and expiry remain separate.

[Indexed GRP blitters](native-pixel-blitters.md) adds9005 complete original calls over the recorded2940 GRP frames and declared stock PCX tables. Whole indexed bitmap buffers and guards match independent predictions; native loading, remaining effect methods, RGB composition and visual gameplay remain separate.

[Ranged projectiles](native-projectiles.md) adds245 complete cases for stock Marine/Wraith/Goliath populated projectile chains, finite-pool limits, miss thresholds and Flingy arithmetic. Selected full object steps use an explicit order gate. Shared initialization traces are disclosed; main attack-order decisions and periodic timer consumers remain separate.

[Local heap identity](native-local-heaps.md) adds452 complete cases /2576 original entry returns for exact pointer ordering, signed priority mutations, equal-cost boundaries and absent-node behavior at declared heap lengths through150. Search capacity callers remain separate.

[Selected spell effects](native-spell-effects.md) adds73 complete original calls for Matrix casting and expiry, Irradiate impact/damage/final-tick cleanup, EMP application, Recall debit/wait and generic completion signals. Original empty image/lone-sprite pools are declared; positive creation/relocation and populated overlays remain separate.

[All-map local searches](native-campaign-local-paths.md) adds816 complete cases /6471 original entry returns on all68 authentic graph/contour profiles. Recorded results include813 positive searches,3 failures, a bottom-row footprint discrepancy and a real250-node arena boundary. First segments and independent geometry diagnostics remain separate from physical movement.

[Positive path blockers](native-path-blockers.md) adds587 complete cases /635 original entry returns, including48 full local routes around actual indexed Marine/CommandCenter records. All444 returned segments avoid independently expanded stock bounds;539 direct cases discriminate the path-specific gates. Native index insertion and physical response remain separate.

[Draw effects/palette state](native-pixel-effects.md) adds2072 passing cases:2068 complete returns and4 explicit prefixes before palette upload. Complete contracts include cloak/decloak, in-place displacement, WarpIn callback/IScript signal, selected draw traversal and palette leaves/bootstrap. Prefixes and indexed buffers are distinct from submitted RGB/visual gameplay.

[Main AttackUnit/timers](native-attack-orders.md) adds234 complete cases /2099 complete top-level returns for stock acquisition normalization, target selection, range/chase, cooldown/jitter, periodic timer expiry and Hallucination conversion. Three48-step object callbacks compose real ranged attack orders, images and projectiles; stationary clear-world fixtures remain separate from blocked physical pursuit and full accepted frames.

[Waypoint production/movement17](native-path-progress.md) adds336 complete cases /624 original entry returns for real waypoint consumers, finite direct-record reuse, pool failure and state15/18/25/RNG51 transitions. Sprite advancement between consumed points is explicit; full physical movement remains required.

[Populated spell fields/Recall](native-spell-fields.md) adds53 complete cases /1190 original function invocations through stock field creation, delivery, timed-life removal and Recall's positive placement/show/order/effect chains. Multi-target relocation, target clearing and complete no-walkable rollback are included; ready-caster admission is an explicit fixture and native InitArbiter remains required.

[Health bars/selected images](native-health-bars.md) adds13358 complete cases for stock HP/shield/energy/timed meters, supplied clipping, virtual-header construction, actual health/circle attachment/release/dirty-grid consumers, stock outline frames and detected-owner blend rows. Authored body/image/type states and private indexed buffers remain distinct from complete selection management or visible RGB gameplay.

[Physical movement/Move orders](native-movement-fsm.md) adds42 complete cases /5085 original invocations for stock Marine/Zealot/Wraith physical travel with naturally maintained spatial indices and populated Walking IScript. Three actual Move6 orders execute complete object callbacks through arrival, path release and Guard2→PlayerGuard3. Clear graph/resource fixtures remain distinct from blocked travel and full mission initialization.

[All-tileset palettes/RGB preparation](native-palette-bootstrap.md) adds2346 passing cases:2301 complete returns and45 explicit prefixes. Actual stock definitions, native bootstrap/membership/rotation, SSE gamma at every supported brightness and grayscale preparation are measured. Timed fades, device submission and visible gameplay remain separate.

[Computer combat/main orders](native-computer-combat.md) adds69 complete cases /1704 original returns for real guard/military metadata, computer AttackUnit/Stim/home gates, AttackMove/Hold acquisition/queues/RNG and stock Scourge/Infested Terran suicide effects. Selected Death issuance is distinct from CUnit reclamation; full-world/object scheduling remains separate.

[Natural caster/field protection](native-spell-casting.md) adds10 complete cases /5886 original invocations for InitArbiter through actual dispatch, natural energy services, full Defiler119 delivery, natural Recall and genuine Dark Swarm ground cover/expiry/resumed damage. Most invocations are repeated timer services; they do not indicate broad algorithm coverage or complete frames.

[Physical blockers/repath/sliding](native-blocked-movement.md) adds56 complete cases /11030 original invocations:48 initially obstructed stock routes and8 later-created blockers, native index maintenance, finite path return,24 half/16 quarter queries and9 proposal restorations. Every published endpoint is independently clear; two continuous corner crossings are retained diagnostics, not hidden by a stronger collision assumption.

[Populated status applications/expiry](native-status-effects.md) adds23 complete cases /8471 original invocations for genuine Plague/Ensnare/Stasis/Acid overlays, native periodic services and natural Defiler144 delivery. Repeated timer calls and declared scheduling remain separate from complete game frames.
