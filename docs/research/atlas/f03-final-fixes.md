# F03: Final consistency and fix pass

This pass closes the two lists of cross-package issues that accumulated while the 50 atlas packages were reviewed: the older follow-up list (75 items, numbered O1–O75) and the reviewers' list (22 items, R1–R22). Every item was re-derived from the capstone disassembly of the exact executable (SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`), not taken from the item text. Reference claims were checked with a raw-image scan (`call`/`jmp`/`jcc` and immediate operands of a linear sweep over every atlas function, plus little-endian pointer values). The earlier passes [F01](f01-corrections.md), [F02](f02-native-reconciliation.md), [V01](v01.md) and [E01](e01.md) had already closed many items; for those the current text was checked against the disassembly again. The [full-game emulation oracle](../full-game-emulation.md) outputs were used read-only as runtime evidence; only its aggregation step was rerun.

Atlas JSON objects that changed keep their `review` field; the change is appended to its notes as "F03: …".

Outcome: 25 fixed, 71 already correct, 1 not a defect, 0 open. Older list: 12 fixed, 63 already correct. Reviewers' list: 13 fixed, 8 already correct, 1 not a defect.

## Older follow-up list

| # | Item | Verified fact | Files changed | Status |
|---|---|---|---|---|
| O1 | P10: state 30 (GetFree) has a writer in `0x42b3b0` | `0x42b6b5` is the store of case `0x42b6b4`, entry 7 of table `0x42b6dc`, chosen only when EDX = 8 before `dec edx` (`0x42b60d`); every producer yields 1..7 (`0x42b448`, `0x42b4a1`–`0x42b4aa`, `0x42b4b2`–`0x42b4ba`, `0x42b522`, `0x42b551`, `0x42b5c8`–`0x42b5cf`, `0x42b5ff`, `0x42b606`). State 30 is dead code. | already correct in `p10.md` `0x42b3b0`, `0x42bb80`; [V01](v01.md) item 4 | already-correct |
| O2 | P10: forecasting consumes RNG source 7 | The randcondjmp case `0x410df7`–`0x410e08` of `0x410480` calls `0x478200`(7) with no test of the sampling flag. | already correct in `p10.md` (`0x42dff0`, `0x42e6c0`) | already-correct |
| O3 | Protoss construction: order 82 named HarvestGas | Order 82 → `0x4237a0` WaitForGas, 83 → `0x4235c0` HarvestGas (index map `0x439d5c`, table `0x439bcc`, cases `0x43980d`/`0x439815`). | already correct in `native-protoss-construction.md` ([F02](f02-native-reconciliation.md) item 11) | already-correct |
| O4 | P03: `0x43f0c0` reads stat_txt | `0x43f0c0` loads `[0x665de0]` and tail-jumps to `0x4c13b0`; `0x43f0d0` stores the `rez\network.tbl` table (`0x4ec2d0`) there at `0x43f0f0`. | already correct in `p02.md`, `p03.md`, `p13.md`; no entry labels it stat_txt | already-correct |
| O5 | Glossary: images 392–404, 958–959 | images.dat GRP column: 392 Flag … 403–404 GasTank, 958 Uraj, 959 Khalis; `0x416290` compares with `0x188`..`0x194` and `0x3be`..`0x3bf`. | already correct in `p05.md` (image ID ranges); also in the README glossary | already-correct |
| O6 | Glossary: mode word `0x6662b8` values | Table `0x467b68` (11 cases, `0x467a8e`–`0x467a9e`) and every writer; values 0–10 as listed in the README. | `README.md` (new glossary section) | fixed |
| O7 | Glossary: dialog event types | Credits handler byte map `0x414a7c` confirms 4/7 page, 13 idle, 14 user. Window procedure: type 16 is WM_COMMAND (`0x43f755`), 17/18 wheel (`0x43fdb8`, `0x43fde1`); P42 and V01 called 16–18 "system char and wheel". | `README.md`; `p42.md` event list; `v01.md` item 7 | fixed |
| O8 | Glossary: sprite flag `0x40` | Image flags `+0x0c` bit `0x40` = hidden (`0x415ab4`, `0x40f89e`); sprite flags `+0x0e` bit `0x40` = burrowed (set `0x43d955`, cleared `0x436d82`, `0x43c8bb`; read by `0x417400`). V01 conflated them. | `README.md`; `v01.md` item 1 | fixed |
| O9 | Missing function `0x41cfe0` | Hangar unlink, own entry `UnlinkHangarChild`. | already correct in `a01.md`/`a01.json` | already-correct |
| O10 | HIGH: status bit `0x20000` | Writers re-scanned (`0x4249f9`, `0x425b8d`, `0x435f6f`, `0x437037`, `0x42f9f6`, `0x42f9a8`, `0x42fc00`, `0x437d26`). `0x42f9f6` belongs to order 106 (summon completion), not a meld cancel as V01 said. `p11.md` `0x4319d0` still said the combat-AI document uses "IsBuilding". | `p11.md` `0x4319d0`; `v01.md` item 1 (writer list); `README.md`; native documents already fixed by [F02](f02-native-reconciliation.md) items 14–17 | fixed |
| O11 | P11: callbacks and addon handlers not duplicated | No address has JSON objects in two packages (scan of all package JSON). | already correct | already-correct |
| O12 | P12: second `0x4af270` call in `0x439420` | `0x43948c`–`0x43949a`: no `0x20000` and `0x8000` set → `0x4af270`, then dispatch continues. | already correct in `native-unit-ai.md` | already-correct |
| O13 | P14: game seed source (R09) | `0x443fe9` `_time64(NULL)`, stored by `0x4781f0`; replay seed `0x444004`; `0x478240` and `srand` at `0x444030`/`0x44403b` and `0x4440a2`/`0x4440ad`. | already correct in `native-prng.md`, ledger R09 | already-correct |
| O14 | P15→P01: `0x405a43` called by `0x445ea0` | Call at `0x445ec6`. | already correct in `p01.md` | already-correct |
| O15 | P15→P03: `0x40b220` called by `0x445ea0` | Calls at `0x445ec1`, `0x445eed`. The general "unreferenced" recheck was done by [F01](f01-corrections.md). | already correct in `p03.md` | already-correct |
| O16 | P15→P14: `0x443b90` condition | `0x447db0` returns 1 when header bytes `+0x75`, `+0x79`, `+0x7f` are all 0 (Use Map Settings). | already correct in `p14.md` | already-correct |
| O17 | P15: race sound preload order | `0x446800` fills `0x666980` 755–967, `0x666c10` 174–472, `0x666f50` 473–754; `0x5033b8` = those three. | already correct in `native-audiovisual.md` | already-correct |
| O18 | P16: accelerator IDs vs jump tables | Spot check: command 40036 → tail jump `0x448850` to `0x422b10`, 40053 → call `0x44881b` to `0x4a6260`; full decode in F01 item 39. | already correct in `p16.md` | already-correct |
| O19 | P16→P08: `0x422b10` reached from `0x4487a0` | Jump at `0x448850`; second caller `0x4a6d51`. | already correct in `p08.md` | already-correct |
| O20 | P16→P36: `0x4a6260` called at `0x44881b` | Direct call confirmed. | already correct in `p36.md` | already-correct |
| O21 | P19 glue table answers the `0x6662b8` glossary | Glue states are `0x66a274` (26 states); the mode word is separate. | `README.md` mode table links both | fixed |
| O22 | HIGH: `0x463710` / spawn flag | `0x46399d` stores `(0x463710()==0)`; `0x463710` returns 1 on a match (`0x463771`–`0x463777`); host decode gives "sgubon", flag 0. | already correct in `p19.md`, `p20.md`, `p22.md`, `v01.md` item 2 | already-correct |
| O23 | P20→P03: `0x408300` string 17 | `mov ecx, 0x11` before `0x43f0c0` at `0x408325`. | already correct in `p02.md` | already-correct |
| O24 | P21→P19: `0x461940` ignores ECX | ECX overwritten at `0x46194e`. | already correct in `p19.md` | already-correct |
| O25 | P21: `0x4635c0` always 1 | `mov eax,[0x5085c8]; ret`; 0 on this install. `0x4620c0` disables Save Replay only for replay, loaded save, campaign key `0x526524` ≠ 0 or spawn (`0x46211a`–`0x46213d`). | already correct in `p21.md` | already-correct |
| O26 | P22: font.ccd value still to decode | Decoded (item below); flag 0. | already correct (see the RESOLVED item) | already-correct |
| O27 | P22: dialog BIN font modes swapped | `0x465c40` loads font8 → `0x6d3024`, font10 → `0x6d3028`; `0x465ce0` font10/font14. | already correct in `native-dialog-bin.md` (F02 item 18) | already-correct |
| O28 | P22→P36: `0x463bd0` out-of-range frame | `cmp dx, ax; sbb; and` at `0x463bdf`–`0x463be8`: index ≥ count → frame 0. | `p36.md` already correct; stale contradiction note in `p22.md` `0x463bd0` marked resolved | fixed |
| O29 | P22→P20: version label | `0x4659dc`–`0x4659fe`: `VS_FIXEDFILEINFO` `+0x10` high/low word and `+0x16`. | already correct in `p20.md`, `p22.md` | already-correct |
| O30 | P22: remap row 0 never initialized | `0x466810` allocates `(h+1)·256` with SMemAlloc flags 0 (pushed at `0x46688d`) and decodes into `+0x100`. V01 described it as a 3-argument call. | `p22.md` already correct; `v01.md` item 6 wording | fixed |
| O31 | RESOLVED: font.ccd decodes to "sgubon" | As above; `0x4638a0` also sets `0x66cac4`/`0x66cacc`. | already correct in `p19.md`, `p20.md`, `p22.md` | already-correct |
| O32 | P23→P14: `0x46ae30` clears text lines | `memset(0x66ff00, 0, 0xb12)`, then `0x46a7f0`. | already correct in `p14.md` | already-correct |
| O33 | P23: `0x673f08` label | Sole setter `0x478a28` in `0x478a20`, called by `0x46b5c0` (`0x46b66b`–`0x46b678`) for race 5. | `native-mission-units.md` summary table row; `p27.md` `0x478a20` prior-evidence note | fixed |
| O34 | P23: TYPE callback `0x468a20` | `mov eax,1; ret 0xc`. | already correct in `a01.md` | already-correct |
| O35 | P24→P03: `0x46eec0` strings | `0x43f0c0`(`0x41`) at `0x46eeca`, (`0x42`) at `0x46eeed`. | already correct in `p03.md` | already-correct |
| O36 | P24: `0x46c3e1`, `0x46e8cf`, `0x46c990` | `0x46c3e1` is the `je` target in `0x46c3c0`; `0x46e8cf` is padding; `0x46c990` has an E01 entry. | already correct in `p24.md`, `e01.md` | already-correct |
| O37 | P25: rename `0x4474d0`; command `0x37` length | `0x4474d0` returns 1 iff `0x67294c` = 1 and `0x67294f` = 0; table `0x4ef124` = 1, dispatcher `0x475db5` consumes 7. | already correct in `p15.md`, `p25.md`, `p26.md` | already-correct |
| O38 | P26→P22: `0x475040` multiplayer-only | `0x475220` never writes header `+0x2c`. | already correct in `p22.md` `0x4634e0` | already-correct |
| O39 | Verify P28 claims | `0x4832f0` tests addresses and always returns 0 (`0x4832fc`–`0x483324`); `0x4824b0` loops `0x4824c4`–`0x4824d4` until a zero WORD with no bound; `0x482260` returns at `0x48227a`–`0x482291` for IDs 44, 103, 62, 68, 63. | already correct in `p28.md` | already-correct |
| O40 | P27→P25: slot actions | 0 → `0x478d30` (type 5), 1 → `0x479760` (6), 2 → `0x478c50` (8). | already correct in `p25.md` `0x471020` | already-correct |
| O41 | P27→P25: ExecBuild addon flag | `0x47d5f0` sends special flag `0x2` to `0x47d620`, else `0x47d730`. | already correct in `p25.md` `0x472d30` | already-correct |
| O42 | Verify P30: `0x489070` returns 0 | Single `xor eax,eax; ret 4` at `0x4890d7`; goal-record writes `0x4890b0`–`0x4890d1` are live. | already correct in `p30.md`, `v01.md` item 3 | already-correct |
| O43 | P29: worker donor gate | Recipient count re-read per donor (`0x48651a`). | already correct in `native-workers.md` | already-correct |
| O44 | P29: addon consumer cancels the queue | `0x41b5c0` cancels or refunds every slot. | already correct in `native-spending-dispatch.md` | already-correct |
| O45 | P29: order `0x6a` label | Order 106 → `0x42f9e0` (summon completion). | already correct in `native-towns.md` | already-correct |
| O46 | Verify P29: `0x486bf0` status test | `0x486c10`–`0x486c22` yields 1 or 2, so `je` is never taken. | already correct in `p29.md` | already-correct |
| O47 | P31→P28: `0x490d80` ignores ECX/EDX | ECX is first written at `0x490db7` (stack argument); the map size comes from `0x5264b6`/`0x5264b4` (`0x490dc3`, `0x490dce`). | already correct in `p28.md` `0x481d50` | already-correct |
| O48 | P32: create_unit "idle order" | `0x434900` cloak tech compared with 44. | already correct in `native-ai-opcodes.md` and its JSON | already-correct |
| O49 | P32: ledger R25/R13 | Whitelist and A* limits recorded. | already correct in the ledger | already-correct |
| O50 | P34→S01: `0x49d460` also called by `0x46b0d0` | Exponent 65537. Campaign missions never reach `0x46b0d0`: `0x469de0` returns at `0x469de6`–`0x469e09` when `0x526524` ≠ 0. | `s01.md` SBig paragraph (code proof and file check added) | fixed |
| O51 | P35→P36: `0x6aab28` is tranwire.grp | `0x4a1f10`: string `0x4f0000` → `0x6aab28`, `0x4f001c` grpwire → `0x6aab2c`. | `p36.md` already correct; stale contradiction note in `p35.md` `0x4a1f10` marked resolved | fixed |
| O52 | P35→P27: `0x47d290` second caller | Tail jump at `0x4a04d0`. | already correct in `p27.md` | already-correct |
| O53 | P35: ledger R34/R35 | Recorded. | already correct in the ledger | already-correct |
| O54 | P37: "bit8" wording | `test al, 8` at `0x4a9cf1`. | already correct in `native-military-arrival.md` | already-correct |
| O55 | Glossary: status bits `0x8000`, `0x2` | `0x8000`: set `0x43a8b0`/`0x43a8cc`, cleared `0x438d1c`/`0x438d35`. `0x2`: whole-word writes only (`0x425b4c`, `0x436fcb`, `0x435f00`, `0x437010`). | `README.md` glossary | fixed |
| O56 | P38→P31: patrol unit at `+0x28` | `0x4b0b20` compares `[rec+0x28]` over `0x34`-byte records. | already correct in `p31.md` | already-correct |
| O57 | P39→P15: `0x4b44a0` creep pool | As stated. | already correct in `p15.md` `0x445a30` | already-correct |
| O58 | P39: `0x4b5130`, `0x4b3ad0` | `0x4b5130` is not in the Ghidra list; it is described in `p39.md` `0x4b4f50` (sole reference `0x4b4f57`). `0x4b3ad0` has an E01 entry. | already correct | already-correct |
| O59 | P40: unit types 203–213 | `0x425ca0`: traps and doors; Flag is 215. | already correct in `native-trigger-move.md`, `native-trigger-unit-actions.md` | already-correct |
| O60 | P40: Transmission completion (R30/R32) | Recorded. | already correct in `native-triggers.md`, ledger R30 | already-correct |
| O61 | Missing `0x4ba830` | E01 entry. | already correct in `e01.md` | already-correct |
| O62 | P41→P21: `0x4bb970` | Called at `0x461aff` for every result except 7. | already correct in `p21.md` `0x461940` | already-correct |
| O63 | P41: ledger R32 | Recorded. | already correct in the ledger | already-correct |
| O64 | P42→P19: `0x4c2b50` never writes `0x6ce6dc` | Raw scan of `0x6ce6dc` writers finds none in `0x4c2b50`. | already correct in `p19.md` | already-correct |
| O65 | Verify P42 `0x4c3fb0` null clip | The only draw-procedure call (control `+0x2e`, load `0x4c5310`, `call eax` `0x4c5313` in `0x4c51c0`) passes `&[ebp-0x14]`. V01 cited `0x4c4b28`, which is a `SetRect` call in `0x4c4ae0`. | `v01.md` item 8 evidence; verdict unchanged | fixed |
| O66 | P44→P42: scroll bars and sliders | `0x4c8d1b` tests `0x2000000`. | already correct in `p42.md` | already-correct |
| O67 | VERIFY: present path without a mask | `0x4caf1e` exits without a buffer; black-screen full copy `0x4caf64`; mask path `0x4cb039`–`0x4cb068`. | already correct in `v01.md` item 5, `p44.md` | already-correct |
| O68 | P44/P22: blend vs remap row 0 | Remap row 0 unused (ledger R06, GRP scan); blend tables are fully built. | already correct in ledger R06, `p22.md`, `p44.md` | already-correct |
| O69 | P45: `0x4cdf10` | Relocation loop inside `0x4cdec0`. | already correct in `native-text.md` | already-correct |
| O70 | P45→P19/P21: `0x6d300e` layout flags | Read by `0x4c3e74`, `0x4cda55`, `0x4cdc08`, `0x4ce004`. | already correct in `p19.md`, `p21.md`, `p13.md`, `p17.md` | already-correct |
| O71 | P43→P25: `0x6cf7a8` open combo box | As stated. | already correct in `p25.md` | already-correct |
| O72 | P43→P14: `0x4c63b0` combo-box default | `0x4c63b0` is `ComboBoxProc` ([P43](p43.md)). | already correct in `p14.md`, `p19.md` | already-correct |
| O73 | P43→P42: `0x4c3740` sound codes | As stated. | already correct in `p42.md` | already-correct |
| O74 | A01→P26: league event handle | `0x4023d0` calls `CreateEventA` at `0x4023db` and stores `0x672968`. | already correct in `p26.md`, `a01.md` | already-correct |
| O75 | A01→C01: `0x4d3a22` | Labeled `ctrlevent_capture$fin`. | already correct in `c01.md` | already-correct |

## Reviewers' follow-up list

| # | Item | Verified fact | Files changed | Status |
|---|---|---|---|---|
| R1 | V01 item 1: sprite flag `0x40` | See the glossary item above. | `v01.md` item 1 rewritten; `README.md` | fixed |
| R2 | P05 header: 13th array, writers, LO table, `0x4175e0`, rename | `0x5295d4` writers `0x4b6d97`, `0x4b84e7`, `0x4b8a02`/`0x4b8a16`; LO `0x598338` readers include `0x411597`. | already correct in `p05.md` | already-correct |
| R3 | P05: `0x415190` team games only | Callers gate on `0x4b2d90` (`0x416e47`, `0x41d847`) or `0x49c2b0` (`0x49cc1d`); `0x424bd0` loops over a count that stays 0 offline. | `p05.md`, `p05.json` `0x415190`: status `excluded` | fixed |
| R4 | P05: `0x417220` caller count | Raw scan: 71 calling functions; the entry says 71. | already correct in `p05.md`; README notes that caller lists can be incomplete | already-correct |
| R5 | P15: exclusion of `0x447220`/`0x448000` vs Storm | `standard.snp` (all offline providers) reads interface `+0x48` only (`0x1d0075b7`); `battle.snp` reads `+0x50` (`0x19023206`). Storm queues user event 1 only from `0x15023c50` (control message 9), dispatched from received packets (`0x15023e40`). | `p15.md`, `p15.json` `0x447220`, `0x448000` | fixed |
| R6 | [done] V01 `0x463710` polarity | Rechecked with the spawn-flag items. | already correct in `v01.md` | already-correct |
| R7 | P26: command length table vs dispatcher | Table `0x4ef048` governs skipping (`0x4720c0`), the dispatcher `0x475d30` governs execution; both differences are stated. | already correct in `p25.md`, `p26.md`; no other document describes the command stream | already-correct |
| R8 | P26: `0x4746a0` runs `0x477da0` with RNG enabled | `0x4746c3`–`0x4746d1`; `0x477da0` returns at once offline (`0x477da7`). No document claims the RNG is off there. | none | not-a-defect |
| R9 | P25: `0x472f50` name check never rejects | `0x408300` gets limit = SStrLen (`0x472f8b`), so its first test fails and it returns 1; `0x498d20` repeats the check with 260. | already correct in `p25.md`, `p33.md` | already-correct |
| R10 | E01 parent errors | P37, A01, P39 and P36 were already corrected. P12 introduction still placed `0x4372c0`/`0x43a880` "in" unrelated entries; V01 still named `0x432950` as the Ensnare reader. | `p12.md` introduction and state-2 text; `v01.md` item 1 | fixed |
| R11 | Rerun the oracle aggregation | Rerun of `sc_emulator aggregate` on the existing outputs: 2770 of 6018 entries executed, none without an entry, 68 continuation groups, no unlisted function starts. | `full-game-emulation.md` coverage section; `evidence/full-game-emulation.json` | fixed |
| R12 | P28: Save Replay permanently disabled | `0x46211a`–`0x46213d` (see the P21 item). | already correct in `p21.md`, `p28.md` | already-correct |
| R13 | [done] P35 `0x49ed30` | Rechecked: no document claims a defect. | already correct | already-correct |
| R14 | P36: `0x4be880` needs an entry | Exit callback registered at `0x4be796`; CL ≠ 0 returns; three `SRegSaveValue` calls (ordinal 426). | `e01.md`, `e01.json` `0x4be880` | fixed |
| R15 | E01 vs corrected P36 chaining | `0x4a5990`, `0x4a5bd0`, `0x4a63e0`, `0x4a68e0`, `0x4a77d0` agree. `0x4a6d30`: codes 1 and 2 also chain (`0x4a6d56`–`0x4a6d67`). | `e01.md`, `e01.json` `0x4a6d30` | fixed |
| R16 | P41: `0x4be880` and the unsplit scan | Scan of every atlas body for 16-byte-aligned starts after `int3` padding that have a code or pointer reference: 89 starts; 33 already had their own objects. The other 56 (not in the Ghidra list) now have E01 entries too (see the appendix). | `e01.md` `0x4be880` and the 56 unsplit entries; `evidence/atlas/packages.json` | fixed |
| R17 | V01 item 8: call site | As in the P42 null-clip item. | `v01.md` item 8 | fixed |
| R18 | 57 mention-only functions | `0x405c70` already had an A01 object. 29 game functions → E01; `0x4d3fc0` and 26 static destructors → C01 `library`. Every function of the Ghidra list now has its own JSON object. | `e01.md`/`e01.json`, `c01.md`/`c01.json`, `evidence/atlas/packages.json` | fixed |
| R19 | C01: creep variant | `0x4c0c56`–`0x4c0c87`: `rand()%100 ≥ 4` → `rand()%6`, else `rand()%7 + 6`; stored + `0x10`. | already correct in `c01.md`, `p42.md` `0x4c0c40`, `native-trigger-creep.md` | already-correct |
| R20 | S01: SBig and campaign condition files | Code proof (`0x469de6`) plus a file check: none of the nine `.cnd` names exists at the root or in any of the 64 campaign directories of the five archives. `SBigNew`/`SBigDel` also run at start-up and exit (`0x402460`, `0x402480`, `0x4ea8a0`, `0x4ea8b0`). | `s01.md` SBig paragraph, `SBigNew`, `SBigDel`; `s01.json` ordinals 624, 606 | fixed |
| R21 | S01: ordinal 342 NULL name | Sole call `0x44036b` passes `[ebp-0x114]` and flag 0; the case is unreachable. | `s01.md`, `s01.json` ordinal 342 | fixed |
| R22 | S01: internal Storm helpers spot-checked | The oracle runs this storm.dll and standard.snp: 94 imports executed across the 65 runs, including SBmp 321/323/325, SNet 101–148 and the pump thread; all 64 missions reached their target frames. The PNG encoder (342) runs only on PrintScreen. | `s01.md` runtime cross-check paragraph | fixed |

## Required work beyond the lists

| Task | Result | Files |
|---|---|---|
| Own entries for `0x4be880` and the mention-only functions | 30 E01 entries: `0x4be880` (`specified`), `0x40f420` and `0x419170` (exit callbacks, `specified`), and 27 `covered` entries whose parent describes them completely (`0x417f00`, `0x418820`, `0x4302c0`, `0x4308d0`, `0x432090`, `0x4321e0`, `0x43279b`, `0x433460`, `0x4336b0`, `0x435b50`, `0x4383d0`, `0x438560`, `0x4a3160`, `0x4a3540`, `0x4a3840`, `0x4a3fd0`, `0x4a5160`, `0x4a5180`, `0x4a51f0`, `0x4a5230`, `0x4a52a0`, `0x4a53a0`, `0x4a53f0`, `0x4a5880`, `0x4a5e60`, `0x4a5f00`, `0x4a6760`). Each parent text was checked against the disassembly. `0x433460` is the Mind Control target filter of `0x432eb0`. `0x405c70` already had an A01 object. | [E01](e01.md), `evidence/atlas/e01.json`, `evidence/atlas/packages.json` |
| CRT addenda and static destructors | 27 C01 `library` rows and objects: `__ftol3` `0x4d3fc0` (with `__ftoui3`/`__ftoul3` at `0x4d3fcf`/`0x4d3fde`, no game callers) and the 26 static destructors `0x4ea150`–`0x4eaad0`, each with the initializer that registers it. The rows of `0x4ea700` and `0x4ea840` also describe the unsplit destructors `0x4ea760` (`CloseHandle`) and `0x4ea8a0`/`0x4ea8b0` (`SBigDel`). | [C01](c01.md), `evidence/atlas/c01.json`, `evidence/atlas/packages.json` |
| Every function of the Ghidra list has its own object | Checked after the edits: all 5784 starts have a JSON object in some package. The 56 further starts found by the unsplit scan also have E01 objects, so no function start is described only inside another entry. | [E01](e01.md) |
| E01 vs the corrected P36 default-procedure chaining | `0x4a5990`, `0x4a5bd0`, `0x4a63e0`, `0x4a68e0`, `0x4a77d0` agree; `0x4a6d30` corrected (R15). | [E01](e01.md) |
| Central glossary | New section "Glossary of shared values" in the atlas README: mode word `0x6662b8` (values 0–10 with writers), dialog event record, types 0–18 and user codes, status bits `0x2`/`0x8000`/`0x20000`, image versus sprite flag `0x40`, carried-item overlay images, caller-list caveat. | [README](README.md) |

## Verifier results

- `tools/research/verify_atlas.py`: 50 of 50 packages, 6075 objects (3591 specified, 1016 covered, 616 excluded, 852 library), all reviewed, 0 errors.
- `tools/research/verify_research.py --root . --out docs/research/evidence/verification.json`: passed; 221 scripts parsed, 145 English documents, 2448 local links, 333 evidence files, 133 fresh original probes (227603 recorded checks). The rerun also refreshes the evidence hashes that [F02](f02-native-reconciliation.md) left stale.

## Appendix: unsplit-function scan

Method: every atlas function body (from its start to the next Ghidra start) was swept for addresses that are 16-byte aligned, preceded by an `int3` padding byte, do not begin with `int3`, and are the target of a `call`, `jmp`, `jcc`, `push` or `mov` immediate in the linear-sweep disassembly of all atlas functions or appear as a little-endian pointer anywhere in the image. These are function starts that Ghidra merged into the preceding body. The scan found 89 such starts; 33 already had their own objects (E01 and A01). The other 56 received E01 entries in this pass, each with its parent, signature, behavior and references re-derived from the disassembly:

`0x418830`, `0x4265d0`, `0x430920`, `0x432250`, `0x432410`, `0x4326e0`, `0x436990`, `0x436a60`, `0x437850`, `0x43b070`, `0x43b500`, `0x43ed10`, `0x43f0a0`, `0x440530`, `0x447f70`, `0x448780`, `0x448d20`, `0x448dc0`, `0x453770`, `0x453e80`, `0x4570d0`, `0x457990`, `0x458be0`, `0x458cd0`, `0x45f240`, `0x45fc00`, `0x460520`, `0x462c10`, `0x463ed0`, `0x465890`, `0x4658a0`, `0x474ef0`, `0x47a1a0`, `0x47acb0`, `0x47b680`, `0x47b700`, `0x47bf70`, `0x47c0b0`, `0x47d080`, `0x47d3a0`, `0x4811a0`, `0x49b420`, `0x49ed20`, `0x49f060`, `0x4a4af0`, `0x4a5410`, `0x4a54e0`, `0x4a55e0`, `0x4b5130`, `0x4be5c0`, `0x4c23e0`, `0x4c51a0`, `0x4cb6f0`, `0x4ea760`, `0x4ea8a0`, `0x4ea8b0`.

Statuses: 49 `covered`, 4 `excluded` (`0x453770`, `0x45f240`, `0x474ef0`, `0x47a1a0`: lobby and modem code) and 3 `library` (`0x4ea760`, `0x4ea8a0`, `0x4ea8b0`: static destructors). Starts reached only by fall-through or short jumps without padding (function tails) are not functions and were not counted.

## Appendix: F04 tail-jump scan

Method: the unsplit-function scan above counted only `call` targets and pointers. This pass collected every padding-delimited start in `.text` (an address preceded by at least two `0xcc` bytes that does not itself begin with `0xcc`), dropped starts that decode inside multi-byte constants or jump tables, and matched the rest against rel32 `jmp`/`jcc`/`call` targets and pointers. Starts that already had a JSON object were skipped. Each remaining address was then read in the disassembly: a separate function has its own prologue or register-argument entry, its own epilogue, and does not use a neighbour's frame. A rerun of the raw rel32 match after the edits leaves only the two internal CRT blocks below.

| Address | Reference | Decision | Where |
|---|---|---|---|
| `0x451680` | `je` at `0x4bb351` in `ShowLobbyCountdown` `0x4bb340` | separate function (own `push ebx`/`esi` entry, tail jump `0x4c2820`), merged by Ghidra into `0x451640`; `excluded` (lobby) | [E01](e01.md); [P18](p18.md) wording fixed |
| `0x4517a0` | `je` at `0x4bb331` in `HideLobbyCountdown` `0x4bb320` | separate function (own `push esi`, tail jump `0x4c22f0`), merged into `0x451790`; `excluded` (lobby) | [E01](e01.md); [P18](p18.md) wording fixed |
| `0x4808c0` | `je` at `0x479cd6` in `LoadUserSelectFlags` `0x479cc0` | separate function (8-byte copy to `0x67cf11`, own `ret`), merged into `0x4808b0`; `covered` | [E01](e01.md); [P28](p28.md) wording fixed |
| `0x4a15a0` | `je` at `0x4a1942` in `ComputeCommandCardModes` `0x4a18d0` | separate function (own `ebp` frame, three epilogues writing `0x6aaaf4` = 245/246/247), placed after the jump table `0x4a1588` of `0x4a1240`; `covered` by [P35](p35.md) steps 3–4 | [E01](e01.md); [P35](p35.md) "continued in the code block" reworded |
| `0x4b0980` | `je` at `0x48d36c` in `AssignCompletedUnitAi` `0x48d320` | separate function (own `push ebx/esi/edi` and `ret`), merged into `0x4b0960`; `covered` | [E01](e01.md); [P38](p38.md) "Embedded code" reworded |
| `0x4e3940` | none | separate CRT function `_trandisp1` inside Ghidra's `_fptrap` range; its two-argument twin `_trandisp2` `0x4e39a7` follows without padding and was added with it; both `library`, unreferenced | [C01](c01.md); [A01](a01.md) `0x4e392c` cross-reference |
| `0x4d1cb0` | `jne` at `0x4d1bd8` (and the `jmp` over the padding at `0x4d1cab`) | internal block of `memcpy` `0x4d16a0`: source-alignment copy that uses the pushed registers of the SSE2 path and jumps back to `0x4d1bde` | [C01](c01.md) `memcpy` row |
| `0x4d2970` | `jne` at `0x4d2898` (and `jmp` at `0x4d296b`) | internal block of `memmove` `0x4d2360`, identical to `0x4d1cb0`; jumps back to `0x4d289e` | [C01](c01.md) `memmove` row |
| `0x4e14a9` | short `je` at `0x4e1486` in `_alloc_osfhnd` | confirmed internal: no prologue, uses `_alloc_osfhnd`'s frame (`[ebp-0x24]`, EDI, EBX) and jumps back to `0x4e1492`; it only follows the funclet `0x4e14a0` in memory | [C01](c01.md) `0x4e14a0` row |

Result: 5 E01 entries (179 in E01: 7 specified, 163 covered, 6 excluded, 3 library), 2 C01 `library` entries (741 in C01), 3 internal blocks recorded in their containing entries.
