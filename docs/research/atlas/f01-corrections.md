# F01: Cross-package corrections

This pass applies the follow-ups that package agents raised against other atlas documents, then runs a sweep of reference and constant-return claims. Each item was re-checked against the capstone disassembly of the exact executable. Reference claims were also checked against a raw-image scan: every `call`, `jmp` and `jcc` rel32 target across `.text`, linear-sweep branch targets of all 5784 atlas functions, and little-endian pointer values in every section. Lines marked HIGH, VERIFY or Glossary, and lines that concern only `native-*.md`, are left to other passes.

## Follow-up items

| # | Item | Verdict | Files and entries changed |
|---|---|---|---|
| 1 | P10: GetFree (state 30) has a writer in FixCollision `0x42b3b0` at `0x42b6b5` (via `0x42b6cc`) | Reporter wrong. `0x42b6b5` is entry 7 (r = 8) of the result table `0x42b6dc`. `0x42b6cc` is the 4-entry side-direction table, and every FixCollision branch yields r ∈ 1..7. A scan of all `+0x97` writes finds no other source of 30. Wording made more precise. | `p10.md` `0x42b3b0`, `0x42bb80` |
| 2 | P10: forecasting consumes RNG through randcondjmp in sampling mode (from P04) | Applied. `0x410df7` calls `0x478200(7)` unconditionally; `0x42e6c0` and `0x42dff0` run the VM with no-op = 1. | `p10.md` `0x42c030`, `0x42dff0`, `0x42e6c0` |
| 3 | P03: `0x43f0c0` is the `rez\network.tbl` getter, not stat_txt | Applied. The same error was in P02. | `p02.md` `0x4074a0`, `0x408300`; `p03.md` `0x40d9d0`, `0x40dfa0` |
| 4 | P15→P01: `0x405a43` is called by `0x445ea0`; dirty-run width = min(run+1, 40−start) | Applied (call at `0x445ec6`; the width formula matches `0x445f80`). The row-end offset was also corrected: it adds `0x2780`, not `0x2790`. | `p01.md` `0x405a43`, `0x405aad` |
| 5 | P15→P03: `0x40b220` is called by `0x445ea0` | Applied (calls at `0x445ec1`, `0x445eed`). | `p03.md` `0x40b220`, `0x40b310`; `p03.json` `0x40b220` |
| 6 | P15→P14: `0x443b90` condition inverted | Applied. The branch polarity was right, but the label was wrong: `0x447db0` = 1 means Use Map Settings. | `p14.md` `0x443b90`, `0x443f40` |
| 7 | P16→P08: `0x422b10` is reached from `0x4487a0` | Applied. There is also a second caller, `0x4a6d30` at `0x4a6d51`. | `p08.md`, `p08.json` `0x422b10`; `p16.md`, `p16.json` `0x4487a0` |
| 8 | P16→P36: `0x4a6260` is called directly at `0x44881b` | Applied. | `p36.md`, `p36.json` `0x4a6260` |
| 9 | P20→P03: `0x408300` string 17 comes from network.tbl | Applied. The entry is in P02. | `p02.md` `0x408300` |
| 10 | P21→P19: `0x461940` does not read ECX | Applied (it overwrites ECX at `0x46194e`). | `p19.md` state table, `0x457510` |
| 11 | P22→P36: in `0x463bd0`, an out-of-range frame selects frame 0 | Applied. | `p36.md` `0x4a26c0` |
| 12 | P22→P20: the version label comes from the product-version fields | Applied (`VS_FIXEDFILEINFO` `+0x10`, `+0x16`). | `p20.md` `0x45ab00`; `p22.md` `0x465960` |
| 13 | Resolved spawn flag: `font.ccd` decodes to "sgubon", so the flag is 0 | Applied. The assembly at `0x463767`–`0x463777` returns 1 on a match, and `0x4638a0` stores (result == 0). The decoded value is taken from the recorded host decode. | `p19.md` theme records; `p19.json` `0x457510`; `p21.md` `0x4620c0`; `p21.json` `0x4620c0`; `p22.md`, `p22.json` `0x4638a0`; `p20.md` conventions |
| 14 | P23→P14: `0x46ae30` clears the text lines | Applied. | `p14.md` `0x443790` (callee list) |
| 15 | P23: `0x673f08` holds user-select slot flags | Applied (sole setter `0x478a20`, called by `0x46b5c0` for race 5). | `p21.md` `0x45dfc0`; `p21.json`; `p20.md` table; `p20.json` `0x45b5e0`, `0x45d870` |
| 16 | P24→P03: `0x46eec0` shows network.tbl `0x41`/`0x42` | Applied. | `p03.md` `0x40dfa0`, `0x40d9d0` |
| 17 | P24: `0x46c3e1` tail artifact, `0x46e8cf` padding, `0x46c990` inside `0x46c920` | Already correct in P24. The tail and padding entries are labeled; the only byte match for `0x46e8cf` (`0x4a94d4`) lies inside an instruction; `0x46c990` is referenced only by `0x46d96f` and is specified in the `0x46c920` entry. The private function index is not part of the atlas documents. | none |
| 18 | P25: rename `0x4474d0` | Applied as `AreAlliancesAllowed`. | `p15.md`, `p15.json` `0x4474d0`, `0x447600`; `p26.md` `0x477610`; `p39.md` `0x4b3bd0` |
| 19 | P25: command `0x37` length 1 vs 7 | Already documented. Table `0x4ef124` = 1; the dispatcher case `0x475db5` consumes 7. | none |
| 20 | P26→P22: `0x475040` is multiplayer-only | Applied. Its only caller paths in `0x45bfc0` are multiplayer or saved-game paths; `0x475220` never writes `+0x2c`. | `p22.md` `0x4634e0` |
| 21 | P27→P25: slot actions 0/1/2 = computer/open/closed | Applied. | `p25.md` `0x471020` |
| 22 | P27→P25: ExecBuild flag `0x2` is the addon flag | Applied. | `p25.md` `0x472d30` |
| 23 | P31→P28: `0x490d80` ignores ECX/EDX | Applied. | `p28.md` `0x481d50` |
| 24 | P34→S01: `0x49d460` is also called by `0x46b0d0` with E = 65537 | Applied. The SBig exclusion reasons were widened to cover ladder/tournament condition maps. | `s01.md` SBig section, 5 SBig entries; `s01.json` same |
| 25 | P35→P36: `0x6aab28` is `tranwire.grp` | Applied. | `p36.md`, `p36.json` `0x4a29e0` |
| 26 | P35→P27: `0x47d290` also tail-jumped from `0x4a04c0` | Applied. | `p27.md` `0x47d290` |
| 27 | P38→P31: region patrol unit at `+0x28` | Applied. `+0x5c` was the array offset of region 1. | `p31.md` globals, `0x48cee0` |
| 28 | P39→P15: `0x4b44a0` initializes the receding-creep pool | Applied. | `p15.md` `0x445a30` |
| 29 | P39/P40: `0x4b5130`, `0x4b3ad0`, `0x4ba830` documented inside other entries | Already correct. Each is described in its containing entry, and each has exactly one pointer reference (`0x4b4f57`, `0x4b3ab6`, `0x4ba3b5`). | none |
| 30 | P41→P21: `0x4bb970` records the next row and runs regardless of victory | Applied. | `p21.md` `0x461940` |
| 31 | P42→P19: `0x4c2b50` never writes `0x6ce6dc` | Applied. | `p19.md` dialog machinery |
| 32 | P44→P42: types 6/7 are scroll bars; flag `0x2000000` turns them into sliders | Applied (`0x4c8d10` tests the flag). | `p42.md` control-type table |
| 33 | P45→P19/P21: `0x6d300e` is the layout-flags byte | Applied (`0x4cd9f0`/`0x4cdf20` test `0x02`, `0x08`, `0x10`, `0x20`, `0x68`). The same mislabel was fixed in P13 and P17. | `p19.md`, `p19.json` `0x455850`; `p21.md` conventions, `0x4605c0`, `0x460fa0`; `p13.md` `0x43ee70`; `p17.md` `0x44fda0` |
| 34 | P43→P25: `0x6cf7a8` is the open combo box | Applied. | `p25.md` `0x471150` |
| 35 | P43→P14: `0x4c63b0` is the combo-box default | Applied. | `p14.md` `0x442af0`; `p19.md` dialog machinery |
| 36 | P43→P42: `0x4c3740` receives sound codes 0/1/2 | Applied (with the hooks `0x458490` and `0x443f20`). | `p42.md`, `p42.json` `0x4c3740` |
| 37 | P19→P05: cross-link the `0x6662b8` values | Applied. P22 defines the values in the main-loop table `0x467b68`. | `p05.md` `0x414e20`, `0x414ef0` |
| 38 | P11/P23/P06: addendum duplicates and missing `0x468a20`, `0x41cfe0` | Already correct. A01 has entries for both, and its P11-range addresses are `covered` jump-table labels, not duplicates. | none |
| 39 | P16: cross-check accelerator IDs against the jump tables | Already consistent. A decode of `0x448abc`/`0x448a98` and `0x448b70`/`0x448ad0` matches every P16 row. | none |

## Sweep of reference and constant-return claims

Every "unreferenced", "no caller", "never called", "dead code" and "always returns" claim was checked, along with variants such as "no references", "no call or stored pointer" and "finds no call". The following were **wrong** and are corrected:

| Entry | Claim | Evidence | Files |
|---|---|---|---|
| `0x40bbb0` | unreferenced | tail jump from `0x4178d0` (itself called by `0x40a210`, `0x40a2f0`, `0x40a720`, `0x41bf70`) | `p03.md`, `p03.json` |
| `0x415700`, `0x415b30`, `0x4165d0` | unreferenced | called by `0x445ea0` at `0x445eb2`, `0x445ef7`, `0x445ede` | `p05.md` |
| `0x415d20` | unreferenced | called by `0x436030` at `0x4360f4` | `p05.md` |
| `0x4148b0`, `0x414990`, `0x414e20` | no direct callers | direct calls from `0x4149f0` at `0x414a27`, `0x414a17`, `0x414a61` | `p05.md` |
| `0x41d300` | no code or data reference | called by `0x436cf0` at `0x436d58` | `p07.md`, `p07.json` |
| `0x4242f0` | no references | called by `0x432510` at `0x432549` | `p08.md`, `p08.json` |
| `0x4265d0` stub | unreferenced | pushed as callback at `0x42530f` in `0x425270` | `p09.md` |
| `0x4808c0` code | no call or pointer | conditional tail jump at `0x479cd6` in `0x479cc0` | `p28.md` |
| `0x484f40` code | unreachable | conditional jump at `0x4b1ffc` in `0x4b1fe0` | `p29.md` |
| `0x4b0980` code | unreachable | conditional jump at `0x48d36c` in `0x48d320` | `p38.md`, `p38.json` |
| `0x4635c0` value | always 1 (Save Replay always disabled) | spawn flag 0 on a full install (item 13) | `p21.md`, `p21.json` |

The following claims were **verified** and are left unchanged:
- No references: `0x4cff90`, `0x4e0790` (none), `0x440016` (the two .data matches are WORD coordinate pairs), `0x46e8cf`.
- Constant returns:
  - `0x42ad10`, `0x42e670`, `0x4318f0`, `0x435b50`, `0x438650`, `0x4aaaa0`, `0x4b7440`, `0x4b75d0`, `0x4b7750`, `0x4832f0` and `0x489070` return 0.
  - `0x42e6c0`, `0x4b8e70`, `0x4c0470`, `0x4c0dc0` and `0x492a90` return 1.
  - `0x47ef40` returns 23.
- Unreachable branches: `0x42bb80` (state 30), action ID 0 in `0x44fee0`, and the index 11/12 branches of `0x46aef0`.
- Already correct by the package's own record: `0x483940` (caller `0x4ac92d`) and the A01 initializer-table entries.

Single-player "always 0" statements that rest on the multiplayer byte `0x672664` or on Battle.net-only writers were not re-derived. Examples are `0x44d7d0`, `0x4777e0` and `0x4b2d90`. Storm.dll claims in S01 refer to a different binary and are outside this scan.
