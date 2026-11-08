# Built-in StarCraft/Brood War AI: research for Defold

Reviewed on 9 October 2026. The project's local reference is **StarCraft 1.17.0.1**, as clarified by the user. Most published reverse engineering and the external function addresses listed below concern **Windows Brood War 1.16.1**. This report combines open-source code, author documentation, community symbol maps, and static evidence extracted from the local reference. The retail executable has not been run; runtime behavior in 1.17 has not been compared. Original files were not modified. The implementation plan now records the target-version clarification and links to this research.

Main findings:

- Campaign compatibility requires an AIScript interpreter, town management, a production queue, regional military AI, and ordinary unit order state machines. Executing the opcodes alone is insufficient.
- The main OpenBW repository contains detailed simulation algorithms, but the reviewed commit lacks a complete built-in ComputerAI and AIScript VM. It cannot serve as a ready-made campaign opponent implementation.
- PyMS, AISE, Teippi, and BWAPI's Broodwar.map provide useful maps of the original AI. Their limits differ: a format editor, an extension that changes behavior, partial replacements that still call the original executable, and annotated addresses without implementations.
- Campaign AI deliberately differs from melee AI. Replacing it with a modern BWAPI bot would change missions substantially, even with the same unit roster.
- Record the scripts actually invoked by each mission, then verify individual mechanisms against the local 1.17 reference. Addresses from 1.16.1 must be matched to the local machine code before use.

## 1. What the sources establish

Evidence levels used here are **original-static** for bytes or machine code from the local reference; **official-description** for Blizzard's descriptions; **community-static** for inspected research code tied to the original game; **reconstruction** for an open reimplementation's behavior; and **hypothesis** for a proposed behavior that still needs an original-game probe. Static evidence does not establish original runtime compatibility.

Blizzard describes standard melee AI as scripted construction, expansion, and conditional attacks. Campaign AI instead works with a prebuilt town, limited rebuilding, few workers, and periodic attack waves. It generally does not expand or depend on the player founding another base. Suicide AI is recommended for Installation levels. These descriptions characterize standard script classes, rather than guaranteeing the behavior of every mission-specific script. [Blizzard StarEdit FAQ](https://classic.battle.net/scc/faq/aiscripts.shtml).

In OpenBW, `ComputerAI`, `AIPatrol`, and hidden `ComputerAI` raise errors. Trigger action 15 recognizes only eight special shared-vision identifiers; other IDs produce `unknown ai script`. OpenBW remains useful for combat algorithms, while campaign AI requires separate research. [OpenBW bwgame.h, ComputerAI](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L8079), [AI trigger dispatcher](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L19067).

BWAPI is an interface for external agents. Its example bots do not represent Blizzard's built-in AI. Its `BWScriptEmulator/Build.cpp` reads parameters while leaving the construction action commented out. A component named AIScript Emulator therefore does not establish completeness. [BWAPI Build::execute](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWScriptEmulator/Build.cpp#L8).

## 2. AIScript VM and execution flow

PyMS catalogs 114 command names, 0x00–0x71, including the final `if_towns` entry documented as unused. Signatures specify parameter widths and types: `wait(word)`, `build(byte count, word unit, byte priority)`, `upgrade(byte level, word upgrade, byte priority)`, and `tech(word technology, byte priority)`. This provides a statically checkable decoding contract. The `aiscript.bin` table stores a four-byte ID, offset, string ID, and flags. The separate `bwscript.bin` table stores ID/offset pairs. A zero code offset in the first table redirects the script to the second. The two tables are not literally identical. [PyMS AIBIN labels/parameters](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L163), [AI table parser](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L503), [BW table parser](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L1863).

**Local 1.17 static confirmation:** the native dispatcher accepts only 113 opcodes, `0x00..0x70`; PyMS's extra `0x71 if_towns` entry and AISE's opcode extension at `0x71` do not pass that dispatch bound. The inspected handlers use a retry value of `wait = 30` for `wait_build`, `wait_buildstart`, `attack_do`, and `train`. The scheduler has not yet established the units of that wait field. `random_jump` branches on `(RNG & 0xff) <= byte`; `call/return` use one global return PC at preferred VA `0x6a2e7c`; and a successful `multirun` copies the source town into the new script. These are machine-code findings, not runtime results. [Native AI interpreter evidence](native-ai.md).

AISE describes the native script layout: linked-list fields, `pos`, `wait`, `player`, a trigger rectangle and its center, a town reference, and flags. PlayerAi holds shared spending requests, the desired training type, attack/nuclear timers, defense lists, and unit limits. Town holds workers/buildings, a resource area, a worker limit, the main building, an assigned SCV, a mineral reference, and up to three gas buildings. These fields guide a saveable state model; an implementation should replace pointers with stable IDs. [AISE native structs](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L703).

Teippi traverses active scripts, decrements wait, and passes a ready script to the original `ProgressAiScript`. Core dispatch remains in the original engine. Teippi is therefore not a complete standalone VM. [Teippi ProgressScripts](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L202), [1.16.1 function binding](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/offsets_funcs_1161.h#L221).

The first VM families to cover are jumps and conditions; `call`/`return`; `multirun`, `expand`, and panic threads; stop/killable behavior; time/resource/race/map conditions; town initialization; requests; attack lists; defense and guards; and special StarEdit commands. The remake must preserve scheduling order and shared PlayerAi semantics because concurrent scripts can change a shared training request or attack force. That is an implementation requirement inferred from the state structure. The exact scheduling order in 1.17 remains unproven; the global return-PC finding also rules out silently substituting an independent call stack for each script.

## 3. Economy, construction, and production

**Original-static 1.17 example:** `Ter5` was extracted from the effective local `aiscript.bin`, with entry PC `0x1c1a` and flags 3. File SHA256: `635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e`. Its opening is `start_campaign; wait(1); start_town; defaultbuild_off; default_min(0); wait(1); max_force(400)`. It then requests `build(1,106,150)` [Command Center], `build(3,7,130)` [SCV], `build(1,111,80)` [Barracks], `build(1,113,80)` [Factory], and `build(1,114,80)` [Starport]. These are concrete script requests, not proof that all objects will actually be constructed during a mission. [Local static inventory](evidence/ai-inventory.json).

According to PyMS, `start_town` assigns buildings to the main town, while `start_areatown` initializes a new town within a location. `build` specifies a desired count and priority. `wait_build`/`wait_buildstart` observe state without submitting construction requests. `train` requests production and waits; `wait_train` only waits. `define_max` limits requests. `farms_timing` and `farms_notiming` change supply-request timing. `defaultbuild_off` affects autonomous idle production. The native 1.17 handlers also contain a count-adjustment branch, so an unconditional claim that they always wait for exactly the encoded building count would exceed current evidence. [PyMS command semantics](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/PyAI/HelpContent.py#L25), [Native wait handlers](native-ai.md).

AISE implements the spending queue as a max-heap: its first request has the highest priority. Military/Guard requests carry region or guard references. The code calculates mineral/gas/supply needs, checks build limits, and requests prerequisite units for morphs/merges. In this **modified** implementation, ordinary training priority is 50 and becomes 160 with large reserves of both resources. These numbers and tie-breaking need verification against 1.17. [AISE PlayerAi requests](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L45), [heap operations](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L410).

AISE fixes must not be imported silently. Its spending hook removes unsatisfiable requests first, can serve a request using one available resource, distributes training, and handles problematic preplaced colony morphs. These are plugin changes, rather than a vanilla specification. Its overridden `train`, `wait_build`, and `wait_buildstart` add unit replacement and worker-limit handling. The retry value 30 is now independently confirmed in the relevant native 1.17 handlers, but it is a **wait-field value with unknown scheduler units**; this numerical agreement does not validate AISE's remaining semantics. [AISE spending hook](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai_spending.rs#L13), [opcode hooks](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/samase.rs#L494), [Native retry evidence](native-ai.md).

Economy research needs separate contracts for mineral/gas worker assignment; reassignment after depletion or town loss; construction and placement; supply; repairs; prerequisites; upgrade/technology costs; morphs/merges; construction recovery after an SCV dies; and full production queues. DAT tables alone do not answer these questions. Change one condition per probe and record the rejection reason, cost, selected producer, and resulting order.

Teippi's repair logic first seeks a safe unfinished Terran building, then a damaged one. A designated worker may also seek a repairable unit. This is a separate SCV AI branch, rather than the player's ordinary Repair command. [Teippi Ai_RepairSomething](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_ai.cpp#L129).

## 4. Attacks, defense, and guards

In the same **original-static** `Ter5`, PC `0x1c8b` contains `wait(6000)`, followed by `0x1c8e attack_add(4,32)` [Firebat], `0x1c92 attack_prepare`, `0x1c93 wait(500)`, `0x1c96 attack_do`, and `0x1c97 attack_clear`. The next fragment is `wait(4500); attack_add(4,0)` [Marine]; `attack_add(2,32); attack_prepare; wait(500); attack_do; attack_clear`. This connects actual campaign bytecode to a multistage attack-wave model. Bytecode alone does not determine departure time, successful production, or the selected target. PCs and parameters came from a static decoder using the pinned PyMS schema; the file hash was checked. [Decoder](../../tools/research/inspect_ai.py), [AI hash and catalog](evidence/ai-inventory.json).

Researcher documentation describes `attack_add` as assembling a bounded roster, `attack_prepare` as nonblocking preparation, and `attack_do` as waiting for the attack to begin after grouping. It reports maximum preparation periods of 120 normal seconds for melee and 180 for campaign, region state 8 for grouping, and states 2/1 for active attacks. `defensebuild_*` submits production requests, while `defenseuse_*` uses existing troops; the suffixes also depend on pathable/unpathable regions. These regional dependencies cannot be represented adequately by one global instruction to attack the nearest enemy. [SCAI reverse-engineering reference](https://www.fraudsclub.com/starcraft/scai/).

The SCAI guide contains internally inconsistent versions of the `harass_factor` formula. The exact formula is **not established here**. Strength rounding, duplicate attack-list entries, inaccessible regions, transport routes, special expansion targets, and interrupted grouping also need probes. A static description selects useful experiments but does not replace them.

Native region structures distinguish requested/local/all/enemy ground and air strength, ground/air targets, a detector, the slowest military unit, a target region, and state. Army strength therefore is not simply a unit count: it includes at least two separate measures with several scopes. Unknown flags must remain unknown until investigated. [AISE AiRegion](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781).

A guard retains its home, type, and loss history. In Teippi, replacement requests depend on region safety and the campaign flag; replacement can stop after repeated deaths. Exact timings need an original-game comparison because the code fixes some bugs. The guard order periodically tries a spell and target, then returns home; a sieged tank first changes mode. This differs from regional military AI and the player's Hold Position order. [Teippi guard needs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L333), [guard/return orders](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_ai.cpp#L14).

Small observable scenarios matter for campaign compatibility: a garrison leaves and returns; two towns maintain different requests; an incomplete attack departs after its deadline; destroying one building does or does not end a town; a reinforcement replaces a guard; and a group continues after destroying its first target. Their outcomes affect mission behavior more than the nominal count of supported opcodes.

## 5. Combat target selection and unit reactions

GPTP reconstructs auto-targeting with six priority buckets, each holding up to 16 candidates. It checks visibility, hostility, attackability, distance, minimum range, attack angle, and chase eligibility. The normal selection from the first nonempty bucket is the nearest target; AI Scourge instead selects the greatest HP plus shields. Priority distinguishes workers, enemies able to retaliate, ordinary buildings, larvae/eggs, and incomplete targets. This is a reconstruction with address comments, not an original trace verified in this study. [GPTP attack priority](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/attack_priority.cpp#L94).

OpenBW has similar bucket algorithms, but the inspected code classifies passive movable/immovable targets differently from GPTP's building/non-building distinction. A discriminating probe should place an unarmed mobile unit and a harmless building at equal distances, with stable creation order. Choosing the more plausible result without the original would conceal a real evidence gap. [OpenBW priority reconstruction](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L1841).

Teippi also has `Ai_ChooseBetterTarget`. It validates candidates and considers weapons, threat, keeping the previous target in range, and distance. An interceptor may be replaced by its parent carrier as the candidate. AI target comparison and ordinary target acquisition are therefore distinct algorithms. [Teippi target comparison](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L2719).

Hit reactions support transport unloading/return, requests for help, and changes to regional needs. Teippi explicitly notes that AskForHelp was revised for speed and bug fixes. Multiple-hit ordering and batched reactions need separate probes, especially if the simulation processes an event list. [Teippi hit reaction](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai_hit_reactions.cpp#L218).

## 6. Spell AI: code found, vanilla behavior not yet proven

The GPTP v2.41 source archive for 1.16.1 contains `AI/spellcasting.cpp`, common helpers, and selectors for Storm, Lockdown, EMP, Plague, Dark Swarm, Mind Control, Yamato, Recall, Stasis, and other spells. Dispatch considers caster type, whether it is under attack, a shared spell timer, energy, technology availability, and target reservation. Its changelog includes AI fixes, including Hallucination behavior. Exact thresholds and selector ordering are therefore **reconstruction hypotheses**. [GPTP v2.41 source archive](https://staredit.net/sc1db/file/3009/).

Each spell needs two contracts: the ability's actual effect and the AI's decision to cast it. Storm effect fixtures alone cannot prove when AI chooses Storm. Probe insufficient/exactly sufficient energy; an existing effect; unseen/invisible/undetected targets; allies inside the area; one unit versus a group; a worker inside a refinery; an idle versus damaged caster; a target reserved by another caster; an ordinary caster versus a hero; and the campaign flag versus melee. Record the first rejection branch in each case. This helps preserve the original AI's character instead of replacing it with optimized modern micro.

Teippi provides another limit on claims of completeness: it calls the original spell function rather than reimplementing it. Its 1.16.1 binding places `Ai_CastReactionSpell` at VA 0x004A13C0. This is a research anchor for locating the corresponding 1.17 code, not a valid hook address in 1.17. [Teippi spell binding](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/offsets_funcs_1161.h#L205).

## 7. Address map and missing evidence

BWAPI's Broodwar.map provides the following 1.16.1 research anchors:

| Subsystem | Symbol-map name | VA in 1.16.1 |
|---|---|---|
| VM | AIScript_RunScriptOpcodes | 0x0045B850 |
| Town construction | AI_Build | 0x004323D0 |
| Waiting | AI_WaitBuild | 0x00433210 |
| Harvesting | AI_AutoHarvest | 0x00433B90 |
| Worker | AIWorkerController | 0x00435210 |
| Town needs | AttendTownNeeds | 0x004348C0 |
| Spending | Ai_TryProgressSpendingQueue | 0x004361A0 |
| Regional attack | AI_AttackManager | 0x0043ABB0 |
| Expansion | AI_Expand | 0x0045B530 |
| Spells | AI_CastSpellBehaviour | 0x004A13C0 |

These names and addresses are community annotations, not Blizzard source code. They remain version-specific even where this study has separately located a corresponding mechanism in 1.17. [BWAPI symbol map](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/Release_Binary/Starcraft/bwapi-data/data/Broodwar.map#L1077).

Samase_scarf includes analyzers for the AIScript dispatch switch, town/player AI globals, spending, worker/military branches, spell calls, and placement. It provides methods for finding code by structure, rather than a complete algorithm specification. This study used its switch pattern as a lead for independent static investigation of local 1.17; that does not validate all of Samase_scarf's analyzers against this executable. [Samase_scarf AI analysis](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/ai.rs#L64), [Local native investigation](native-ai.md).

Outstanding questions for 1.17 include exact scheduling and wait-field units, search/heap tie-breaking, RNG stream consumption, the full strength formula, AI knowledge of fog of war, target/transport planning, worker saturation and mineral/gas switching, placement order, all spell selectors, numerical campaign-flag differences, and bugs that actually affect missions. A source port alone cannot mark these contracts verified.

## 8. Proposed implementation sequence for Defold

First connect original CHK trigger actions to script IDs. For each script, derive a bytecode control-flow graph, reachable opcode set, and spawned-script dependencies. The result should be a mission → player → location → AI thread graph. Retain the source hash, effective MPQ, and PC for each command. The local 1.17 corpus takes precedence over somebody else's vanilla script dump: a patch or modified archive may change an attack roster without changing its name.

Next implement a minimal VM with trace output and no graphics: executed opcode, PC before/after, wait, spawned/deleted threads, town ID, and request-list deltas. Start with decoding and round-trip checks, then original-game probes of waits, branches, and multirun. Add a single-town economy executor with real construction orders. Follow with regional strength, defense, and the first attack wave. Add spell decisions after ordinary orders and targeting have been checked, because spell selectors depend on health, visibility, range, and status.

Use narrow milestones: the first AI town assigns workers and completes the requested construction; the scheduler does not stall on unavailable technology; a mixed-unit wave groups and departs; destroying a guard changes the appropriate request; and defense responds with different ground/air compositions. A mission is compatible only after a walkthrough compared against the original with that AI. A successful standalone VM does not establish this.

In Lua, PlayerAi/Town/Region/Guard/Script can be records and indexed arrays, with the presentation synchronized separately. Measure time in simulation ticks rather than render-script timers. Serialize queued requests, waits, per-player timers, RNG, and shared return-PC state, or save/load can alter later waves. This is an architectural recommendation, not a prescription to reproduce native memory layout.

Distinguish original campaign quirks that affect gameplay from crashes. The first group needs an explicit compatibility decision and regression coverage. A crash can reasonably become a diagnostic or safe termination with the compatibility limitation documented. Do not automatically import AISE fixes into a vanilla profile merely because they strengthen or stabilize the AI.

### Reviewed sources and pinned snapshots

Files were read from downloaded Git repositories. Links use pinned commits rather than a moving master branch. Open-source references were downloaded to scratch directories under /private/tmp/koprulu-*.

- OpenBW: `4b046d5f65302b10cb0a745f0fecd37ec85b20a8`, bwgame.h.
- PyMS: `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`, AIBIN.py and HelpContent.py.
- AISE: `da4fed681dc09bda7a21c0051f60cdad98cc7226`, structs.rs, ai.rs, aiscript.rs, ai_spending.rs, and samase.rs.
- Teippi: `05c006c2f74ad11285c39d37135aed03d1fb8806`, ai.cpp, ai.h, unit_ai.cpp, unit.cpp, ai_hit_reactions.cpp, and offsets_funcs_1161.h.
- GPTP: `ce321f0fa83174aee741b91f1b2eaec30300773e`, attack_priority.cpp; separate v2.41 source ZIP SHA256 `05e98b6e6d9cacda68a69e9bd3a999500f2cd7fafc201d62b5413eb9244b698d`.
- BWAPI: `d727fed68558c506163048ea889131d8cbb33915`, Broodwar.map and BWScriptEmulator.
- Samase_scarf: `f1681dbac8c7d1ac184dc84918ab7dd5771061ed`, src/ai.rs.
- Blizzard StarEdit FAQ and SCAI command reference: linked above; the relevant sections were read.
