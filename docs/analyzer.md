# Dependency analyzer

Code: `converter/analyze/`. It gives each mission the resource packs it loads before it starts ([`PACK`](formats/kpm.md#pack)):
- a campaign mission's, through `converter/missions.py` `survey`, which `python3 -m converter scr-manifest` records in `defs/campaign/packs-<profile>.json` (compiled into the runtime as the profile's `MPAK`, `tools/defs_c.py`);
- a test scenario's, on its line of `generated/scenarios/index.txt` (`kp_convert scenarios`).

Since [ADR 0018](adr/0018-original-data-at-run-time.md) (stage D8d) the converter writes no per-mission profile or campaign report (`reports/profiles/`, `reports/analysis.json`); the figures below were taken from them.

## Inputs and rules

| Part | Rule | Source of truth | Verification |
|---|---|---|---|
| AI scripts | Launches of actions 15/16 resolved by `0x494d20`. The closure follows every address operand (`target`, `*_target`) and the fallthrough; `call`/`multirun` keep target and continuation. Fallthrough ends at goto, stop, return, kill_thread, fatal_error, debug. | Native operand reads of all 113 dispatcher ordinals (`defs/ai/`) | All 173 campaign roots reproduce the research inventory: per-root counts and opcode sets, 11,497 instructions, 64 opcode types, per-opcode counts |
| Triggers | TRIG/MBRF records: condition types, actions, AI launches, unit creation (11, 44), Set Next Scenario (41) | Atlas P17/P40 tables | Histograms and every AI launch equal `retail-manifest.json` on all 68 CHKs |
| IScript | From every animation slot of a header, following goto/call/conditional targets and the fallthrough, stopping at goto/end/return. Collects images, sprites, flingies, sounds, weapons, the next image (`imgulnextid`), gas overlays and the Goliath `sprol` sprite. | Atlas P04 interpreter table (`defs/iscript/`) | 1,833 entry points and 9,763 instructions as `iscript-sounds.json`; its sound ids are a subset (the analyzer also expands `playsndbtwn` ranges, which the original plays) |
| Production | A human player can make a unit that is available in the mission's `PUNI` result and has a native recipe whose `ff02` producer is reachable from the units the player starts with or that triggers create. Hardcoded transitions add larva, egg, cocoon and lurker egg. | Recipe table `0x50da88` (91 recipes, `defs/requirements/`, compared with the executable) | Synthetic closure test |
| Computer players | Produce what their AI closure names. | | |
| Resources | Units → flingy → sprite → image, subunits, infestation, construction image, weapons → flingy, selection circle images, IScript closure of every image, unit sounds and portraits. Spell weapons come from `orders.dat` for every enabled technology. | DAT layouts | |
| Packs | Pack of every GRP, sound and portrait. `effects` is always included: native code creates fixed images (selection circles, shield hit, damage overlays, spell effects). | `converter/packs.py` | |

**Upper bounds.** These are static upper bounds. They include random branches, disabled actions and units the mission makes available without ever using. An included item is not evidence that it runs. Techs and upgrades are the entries the mission enables for a human player's race; research buildings are not checked.

## M1 choice

The ranking measure is the number of distinct mechanisms a mission needs: unit types, AI opcode types, trigger condition and action types, IScript opcode types, enabled techs and upgrades.

`terran/terran01` (Wasteland) has the smallest set: 12 unit types, no AI script, 16 action and 3 condition types. Its packs are terran, zerg, neutral, effects and doodads. It is also the first candidate, because the emulator trace of a win without cheats exists. **M1 = terran/terran01** (test `converter/tests/test_analyze.py::test_m1_choice`).

## Campaign scope

Union over the 64 catalog missions (the analysis's campaign scope):

| Item | Count |
|---|---|
| Unit types | 168 |
| AI roots | 173 |
| AI opcode types | 64 |
| Trigger condition types | 13 |
| Trigger action types | 47 |
| Briefing action types | 9 |
| IScript opcode types reachable from mission images | 60 |
| Techs | 44 |
| Upgrades | 54 |
| Packs | 6 |
