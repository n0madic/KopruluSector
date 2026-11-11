# StarCraft archives, data, and campaign execution for Defold

Research date: 2026-10-09. The local baseline is the Windows retail executable **1.17.0.1**, confirmed by the [PE preflight](evidence/executable-preflight.json). The package also contains `patch.txt` with 1.17.0 as its latest section. The user expanded the initial 1.16.1 target to include 1.17. These findings apply to the inspected archives; BWAPI addresses for 1.16.1 require verification before use with 1.17.

## Findings verified from local data

A static, read-only probe opened five archives from a scratch copy of the installer using the x86_64 StormLib library bundled with PyMS and `SFileOpenArchive(..., 0, STREAM_FLAG_READ_ONLY, ...)`. The game executable was not run. Extracted retail bytes remain in scratch storage. The repository contains the inspection scripts, research notes, and JSON evidence with hashes and counts.

| Archive | Size in bytes | SHA-256 |
|---|---:|---|
| Patch_rt.mpq | 984571 | ea0c7d20b77a6ccb4794f1b717d08f6664ec3aa7dc1277984bb21944c49d08fd |
| BrooDat.mpq | 23847431 | 8c2c8ad5b32d4caf05d9e9d6727f46872ce6fc54f2e69ad6d6364cd1ea58c3c4 |
| StarDat.mpq | 66184580 | ff0002e5bef807f20eae3d11e87490b3cc683351036519012983d16739d2bbe2 |
| BroodWar.mpq | 566279173 | 9838b910a5eef8a3ba067da26e2d1ec06be3e912e005bb763a2868223e57fca2 |
| StarCraft.mpq | 601389682 | 6812af9a163d9791a922768bd4c7f53a83b179500280754728df94b331bec62b |

The probe selected the first archive containing each requested path, in this order: Patch_rt, BrooDat, StarDat, then the campaign archives BroodWar and StarCraft. The core-data order Patch_rt → BrooDat → StarDat is independently implemented by [OpenBW `data_files_directory` / `data_files_loader`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/data_loading.h#L1300) and [PyMS `MPQHandler.add_defaults` / `get_file_mpq`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/Utilities/MPQHandler.py#L25). The original executable's complete archive resolution, locale handling, and mod behavior still require separate investigation.

The probe found 39 physical copies of the selected files and resolved 16 effective files. The JSON records the source archive, path, size, and SHA-256 for each copy and each selected result. Examples:

| Effective file | Archive | Size in bytes | SHA-256 |
|---|---|---:|---|
| scripts/aiscript.bin | Patch_rt | 62050 | 635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e |
| scripts/bwscript.bin | BrooDat | 26458 | 15a1a5b3fb4f2cab2e7b7b507ae04d4e2ef097c77081cfeb950d9a2e4ebee382 |
| scripts/iscript.bin | Patch_rt | 40482 | cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0 |
| arr/mapdata.dat | BrooDat | 260 | 06ce3127b9c77739f32ad606637943f022e5d0344afcf1e978494449ace37967 |
| arr/mapdata.tbl | BrooDat | 1775 | ab4715c36e3af0062fffa5308953ef8c40850790144523b90ccd19529862f15b |

## Campaign catalog and physical map inventory

`arr/mapdata.dat` contains 65 records, each a u32 reference into `arr/mapdata.tbl`, which also contains 65 strings. Record 0 references the string `null`; the remaining 64 reference campaign directories. Appending `staredit/scenario.chk` gives the path to raw CHK data inside the campaign archives. These entries are not separate SCM/SCX files. The relationship is described by [PyMS `CampaignDAT`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/CampaignDAT.py) and its [mapdata.dat documentation](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/Help/Files/DAT/mapdata.dat.md).

| Campaign directory | Catalog CHK entries | All CHK entries found |
|---|---:|---:|
| terran | 11 | 13 |
| zerg | 10 | 11 |
| protoss | 10 | 11 |
| expprotoss | 8 | 8 |
| expterran | 9 | 9 |
| expzerg | 16 | 16 |
| Total | **64** | **68** |

The catalog includes the Terran tutorial, `expterran/terran05a` and `terran05b`, `expzerg/zerg04a` through `zerg04f`, and `expzerg/bonus`. Terran directory numbering has gaps: the catalog excludes `terran07` and `terran10`, but includes `terran11` and `terran12`. Menu and mission indices therefore cannot be derived from directory suffixes.

Four physically present maps are outside the catalog: `protoss/tutorial`, `terran/terran07`, `terran/terran10`, and `zerg/tutorial`. All four have the prerelease CHK format value `VER=57`. The catalog contains 31 maps with `VER=59` and 33 with `VER=205`. The **64 catalog resources do not establish 64 player-facing missions**. Campaign progression, mission variants, bonus unlock conditions, and transitions require investigation of the original glue/campaign code and native tests.

The catalog maps contain 1,869 TRIG records, 64 MBRF records, and 612 raw AI launch actions referring to 173 distinct script IDs. The complete physical inventory contains 1,907 TRIG records, 67 MBRF records, 621 AI launch actions, and 177 distinct IDs. For every map, the manifest records its CHK SHA-256, sections, condition/action histograms, AI IDs and locations, trigger/action indices, execution flags, and owners. Counts include disabled and potentially unreachable triggers. They describe the static analysis workload rather than an execution trace.

## AI binary layout and control-flow inventory

PyMS provides a schema for 114 opcode IDs, `0x00` through `0x71`. The [native AI investigation](native-ai.md) separately establishes that the inspected original 1.17 dispatcher accepts opcode IDs through `0x70`; PyMS's extra `0x71` entry is the unused `if_towns` schema entry. A tool's schema size is therefore not the number of implemented native opcodes.

`AIBIN.load_data` reads directory entries as `<4s3L>`: ID, code offset, string index, and flags. `BWBIN.load_data` reads `<4sL>`: ID and code offset. AIScript and BWScript share an instruction schema but **have different directory layouts**, despite the simplified “exact same format” statement in the help text. An AIScript code offset of 0 requests a lookup of the same ID in BWScript. See [PyMS `AIBIN.load_data`, `BWBIN.load_data`, and `parameters`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/AIBIN.py#L503).

The local AIScript directory contains 293 IDs: 137 have code in AIScript and 156 redirect to BWScript. BWScript contains 156 IDs with code. Full lexical decoding with the AST-derived schema identified 15,947 AIScript instructions using 83 opcode types and 7,769 BWScript instructions using 51 types. Every decoded target address resolved to an instruction boundary, and every map launch ID resolved. These counts describe encoded instruction intervals, not the number of VM steps executed during a game.

A conservative traversal from the 173 catalog root IDs reaches 64 opcode types. The traversal follows every conditional address target and the fallthrough path. For `call`, it includes both the target and the successor; for `multirun`, it includes the spawned code and the continuation. `goto`, `stop`, `return`, `kill_thread`, `fatal_error`, and `debug` end local fallthrough. Offset-0 directory redirects select code in the other binary; address branches inside a binary are not automatically interpreted as cross-binary jumps. This upper bound includes impossible game states, all random outcomes, and disabled trigger actions. It does not establish branch probabilities, execution frequency, or instruction semantics.

One actual example is `Ter5`, whose AIScript entry is at PC `0x1c1a`: `start_campaign`, `wait(1)`, `start_town`, `defaultbuild_off`, `default_min(0)`, `wait(1)`, `max_force(400)`, `build(1,106,150)`, and `build(3,7,130)`. Unit IDs 106 and 7 identify the Command Center and SCV. A later straight-line sequence contains `wait(6000)` at PC `0x1c8b`, `attack_add(4,32)` at `0x1c8e`, `attack_prepare` at `0x1c92`, `wait(500)` at `0x1c93`, `attack_do` at `0x1c96`, and `attack_clear` at `0x1c97`. This establishes authored strategy in the original data. It does not guarantee successful construction requests or an attack at a particular wall-clock time.

## CHK, triggers, and mission briefings

A TRIG record occupies 2,400 bytes: 16 conditions of 20 bytes, 64 actions of 32 bytes, execution flags, and owner bytes. MBRF uses the same container with a separate namespace for briefing conditions and actions. PyMS enables briefing parsing with `load_data(..., True, True)`. The runtime needs a separate briefing execution path so these actions are not interpreted as gameplay actions. See [PyMS `CHKSectionTRIG`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionTRIG.py), [`CHKSectionMBRF`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionMBRF.py), and the [TRG schema](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/TRG.py#L1321).

CHK gameplay action IDs 15 and 16 are `RunAIScript` and `RunAIScriptAtLocation`. IDs 38 and 39 are `MoveLocation` and `MoveUnit`. An AI script ID occupies four bytes in the action's extra field; it is not a localized script name. STR/TBL parsing must retain control characters, embedded NULs, and legacy encoding. The runtime's ID conversion needs to preserve byte identity independently of display names. [PyMS `TBL.py`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/TBL.py#L101).

Map-author documentation describes trigger passes for Players 1 through 8, actions in their stored order, `CurrentPlayer` binding, per-owner one-shot execution, and `PreserveTrigger`. Force and AllPlayers owners require separate running instances; switches are shared, whereas death counters belong to a player/unit pair. Bring caches can retain stale results after changes within a cycle. These are documented observations from map authors; this session did not verify their timing in the original runtime. [StarEdit trigger mechanics](https://staredit.net/wiki/index.php?title=Triggers).

A trigger `Wait` is not an independent coroutine sleep. A player has one active wait state, another wait can block, and previously processed triggers are rechecked. Native probes should cover zero and nonzero waits, two waits owned by one player, different owners, PreserveTrigger, and transmissions. AI `wait` and trigger `Wait` have different execution machinery; their time units and update rules must be established separately. [StarEdit wait blocks](https://staredit.net/wiki/index.php?title=Wait_blocks).

Format dispatch depends on `VER` and game mode. Original, hybrid, and Brood War maps use the section pairs UNIS/UNIx, UPGS/UPGx, TECS/TECx, UPGR/PUPx, and PTEC/PTEx. MRGN records pixel rectangles and ground/air elevation flags. Current documentation also describes Remastered `VER=64/206` and STRx, which require separate version handling from the 1.17 baseline. Duplicate-section behavior differs between ordinary sections and append-only TRIG/UNIT/THG2; protected custom maps may use section stacking. This probe validates the inspected linear retail layout and is not a general CHK validator. [StarEdit Scenario.chk](https://staredit.net/wiki/index.php?title=Scenario.chk).

## Limits of OpenBW as a campaign reference

At commit `4b046d5...`, `bwgame.h::test_trigger_condition` implements only Command, Bring, ElapsedTime, Opponents, and Always; its default branch raises an error. For `RunAIScript`, `execute_trigger_action` accepts only the shared-vision IDs `+Vi0` through `+Vi7`; other IDs raise an error. Full AIScript/BWScript loaders and MBRF execution were not found. `process_triggers` also uses timer 30 and step 42, while the map-author reference describes the ordinary cycle as two game seconds. That discrepancy requires measurement of the original before adopting a timer value. [OpenBW trigger conditions](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L18887), [AI action implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L19041), and [`process_triggers`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L13106).

IScript must participate in simulation. Its commands include damage, weapons, spell/order signals, and movement, in addition to visual frame changes. The OpenBW loader and opcode handlers provide useful anchors for focused comparisons. The remake needs image VM state covering PC, waits, return state, and order flags. [PyMS `IScriptBIN.py::CMD_HELP`](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/IScriptBIN.py#L87) and [OpenBW `load_iscript_bin`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L21994).

## Save-state completeness

BWAPI documents `BWGame` as a serialized structure containing frame/elapsed time, campaignIndex/nextScenario, resources/supply, unit counters, technologies/upgrades, alliances/vision, switches/locations, objectives/victory state, and selections. It is one structure rather than a complete specification of the save format. Units, bullets, orders, RNG state, AI threads, and trigger continuations also need preservation. [BWAPI `BWGame.h`](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/BWGame.h#L8).

The proposed Defold save format should be versioned and preserve stable entity IDs, the complete mutable simulation snapshot, RNG state, and order/list ordering. It should record the map hash, content-manifest hash, and campaign progression as explicit data. A required validation is to run T ticks, save and reload, continue for N ticks, and compare every subsequent tick's state hash and AI/trigger events with an uninterrupted run. Cover saves during trigger Wait, AI call/multirun/wait_build, attack grouping, transport, construction, missile flight, fog updates, and location mutations. This is a proposed acceptance criterion; original binary save compatibility has not been established.

## Extraction and conversion workflow

First record archive hashes and the effective overlay, then extract DAT/TBL and AI/IScript into scratch storage. Read campaign CHK paths from the catalog and build a typed dependency manifest covering GRP, LO, portraits, WAV, tileset files, map overrides, and raw IDs. DAT files supply parameters and references; order handlers, prerequisites, and AI task execution still require executable research. The offline converter should publish the game's data after validating temporary output. The intended runtime does not require MPQ loading.

StormLib's modern patch chain (`haPatch`, `MPQ_FILE_PATCH_FILE`, and binary patch transforms) must be distinguished from the classic Patch_rt file overlay. The probe opens physical archives independently. Public API documentation describes `dwPriority` as unused, whereas current master stores it in `TMPQArchive`; `SFileOpenFileEx` reads the supplied archive handle and its patch chain. An explicit resolver provides reproducibility without relying on an undocumented global archive search. [StormLib `SFileOpenArchive`](https://raw.githubusercontent.com/ladislav-zezula/StormLib/master/src/SFileOpenArchive.cpp) and [`SFileOpenFileEx` / `OpenPatchedFile`](https://raw.githubusercontent.com/ladislav-zezula/StormLib/master/src/SFileOpenFileEx.cpp).

An external listfile supplies candidate names rather than proof of existence or a complete inventory. The probe retains successfully read CHK files and combines those candidates with the exact mapdata catalog. Locale handling requires a separate check through StormLib's locale APIs. This probe uses the library's default locale and does not establish the installation's complete language configuration. [StormLib `SFileListFile`](https://raw.githubusercontent.com/ladislav-zezula/StormLib/master/src/SFileListFile.cpp).

## Reproducibility and project evidence

Detailed setup and commands are documented in [reproduction.md](reproduction.md).

- [inspect_campaigns.py](../../tools/research/inspect_campaigns.py) accepts `--stormlib`, `--app`, `--out`, `--listfile`, and `--version`. It opens archives read-only, writes extracted originals only to the selected scratch output, and generates a manifest.
- [inspect_ai.py](../../tools/research/inspect_ai.py) accepts `--data` and `--schema`. It reads the pinned PyMS schema through the AST, validates bounds and jump boundaries, and generates conservative control-flow inventories. Its `load_schema` and `decode` are importable by the other tools.
- [inspect_ai_defense_strength.py](../../tools/research/inspect_ai_defense_strength.py) accepts `--data`, `--schema` and `--out`. It decodes every instruction of both script files with `inspect_ai.py`'s decoder, computes the AI type strengths from the extracted `units.dat` and `weapons.dat` as `kp_strength_init` does, and exits 1 when a defense opcode names a type without strength in its list's direction ([defense recipes](native-defense-recipes.md#the-stock-scripts-name-no-zero-strength-type)).
- [retail-manifest.json](evidence/retail-manifest.json) records archive/file hashes, the map catalog, and per-map trigger/action/AI launch metadata.
- [ai-inventory.json](evidence/ai-inventory.json) records AI directory sizes, instruction counts, requirements from campaign roots, and additional legacy maps.
- [ai-defense-strength.json](evidence/ai-defense-strength.json) records the input hashes, every defense list's types and weakest entry, the zero-strength types of each direction and the whole strength table.
- Source commits: PyMS `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`; OpenBW `4b046d5f65302b10cb0a745f0fecd37ec85b20a8`; BWAPI `d727fed68558c506163048ea889131d8cbb33915`.

The evidence establishes static data and analysis coverage. Campaign execution and full gameplay semantics remain unverified. Native 1.17 probes and campaign walkthroughs are required to assess behavioral compatibility.

## Damaged archive entry

`campaign\protoss\protoss10\staredit\wav\PAB05ura.wav` in the reference install's StarCraft.mpq (997,594 bytes, 244 sectors of 4 KB) cannot be read completely. [probe_storm_read.py](../../tools/research/probe_storm_read.py) runs the installation's original `storm.dll` in the full-game emulator's machine:

| Reader | Result |
|---|---|
| Original `storm.dll` | Decodes sectors 0–218. On sector 219 it reports error `0x85100083` through `SErrDisplayError` and terminates the process |
| StormLib | Already fails on sector 218 |

The file is the Transmission of the Protoss 10 briefing (MBRF record 0, action 14). On this installation the original game therefore exits during that briefing unless Start is pressed before the line plays. No other effective file read by the converter (DAT, TBL, scripts, tilesets, PCX, GRP, FNT, BIN, WAV, SMK) fails.

**The damage is this copy's, not the release's** (issue #2, 2026-11-11). A player's StarCraft.mpq of the same size and date (601,389,682 bytes, 1998-03-27, SHA-256 `ba155a30…`) reads the file whole and plays it; the reference copy's SHA-256 is `6812af9a…`. [probe_damaged_sectors.py](../../tools/research/probe_damaged_sectors.py) compares the reference copy with Remastered's `SD/campaign/protoss/protoss10/staredit/wav/pab05ura.wav` (997,594 bytes, SHA-256 `1ebee7e4…`):
- 242 of the 244 sectors read and equal Remastered's copy byte for byte; sectors 218 and 219 do not read;
- Remastered's sectors compressed again with StormLib (ADPCM mono and Huffman, mask `0x41`) and encrypted with the file's keys equal 138 stored sectors byte for byte (215, 217, 220 and 221 among them). The other readable ones come out otherwise from some byte to their end, or at another length: Storm's 1998 encoder chose other ADPCM codes than StormLib's (sector 216 reads and decodes to Remastered's samples, yet compresses again to other bytes from its byte 863);
- sector 219 alone comes out equal again after a difference: from its byte 113 to its end (1,179 bytes). The encryption's state at a byte depends on every byte before it in the sector, so sector 219 was Remastered's sector compressed, and its first 113 bytes are broken (its mask byte reads `0x98`);
- the unreadable sector 218 (the archive's offsets `0x1d0c184f`–`0x1d0c1d4e`) and the broken head of sector 219 (to `0x1d0c1dbf`) lie within one 2,048-byte block of the disc (block 237,955): a bad read when the disc was copied;
- the copy cannot be repaired to the byte: StormLib does not make Storm's ADPCM codes, so sector 218's stored bytes cannot be made again.

The profile lists the release's file as Remastered keeps it (`defs/scr/inputs-bw-1.17.0.1.json`; until then the manifest named it `unreadable`, and every source, Remastered's and a sound disc's too, dropped the line). On the reference install it is a damaged copy: its header reads, so the mission finds it and no sound index moves (an index is the WAV string's position, [kpm format](../formats/kpm.md#wavs)), and a whole read fails, so the line plays nothing; `verify` reports it as a warning, `repack` leaves it out and says so, and `scr-manifest` keeps the manifest's entry for it (`converter/inputs.py` `damaged_entries`).
