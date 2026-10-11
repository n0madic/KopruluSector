# Native scalar trigger counters and player selectors, StarCraft 1.17

The original executable passed **4,136 isolated checks** for resource, score,
death and kill counter queries; Accumulate, Kill, Deaths and Score conditions;
Set Resources, Set Score and Set Deaths actions; and their player selectors.
Every call ran a complete original function and its actual callees. There are no
replacement callees, imports or instruction-prefix tests in this batch.

The state supplied to those functions is a controlled fixture. The campaign
checks reuse decoded parameters from the retail CHKs, rather than a snapshot of
the state reached while playing a mission. This establishes the scalar contract
and campaign input coverage, not a complete mission outcome or full trigger VM.
Execution order, waits, flags and trigger clocks are covered separately in
[native-triggers.md](native-triggers.md).

## Provenance and reproducibility

The target is `StarCraft.exe` 1.17.0.1, preferred image base `0x400000`, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
All addresses below are preferred VAs; subtract `0x400000` for RVAs. These are
local 1.17 addresses, not addresses transplanted from 1.16.1 documentation.

The reproducible artifacts are:

- [Probe script](../../tools/research/probe_native_trigger_counters_117.py),
  SHA-256 `60e000193aa747c45bf758d37a40714fb599e12da9f2733240eba410895055b6`.
- [Generated native evidence](evidence/native-trigger-counters-probes.json),
  including 1,715 executed instruction witnesses, individual inputs and outputs,
  per-map counts, and every campaign reference for each parameter tuple.
- [Retail manifest](evidence/retail-manifest.json), which gates every input CHK
  by its SHA-256. The probe also records the manifest hash it used.

The successful run used Rosetta `x86_64`, Python 3.9.6, Unicorn 2.1.4, pefile
2024.8.26 and Capstone 5.0.6. Every executed instruction must lie in the reviewed
allowlist. The original PE is copied into private emulator memory; no game entry
point, DLL import, original file access or original network access can execute.
The script compares all resource, score, individual death/kill and aggregate
death/kill arrays after each call. Writes are recorded as index, old value and
new value; the full checked scalar state also receives a SHA-256.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_counters_117.py \
  --exe /path/to/StarCraft.exe \
  --data /path/to/extracted-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-counters-probes.json
```

Without `--execute-original-code`, the script prints its plan. Input extraction
and dependency installation are documented in [reproduction.md](reproduction.md).
Retail CHKs and bytecode remain outside the project.

## Entry points and record fields

| Operation | Wrapper VA | Scalar helper VA |
| --- | --- | --- |
| Accumulate condition, ID 4 | `0x4b8f80` | resources `0x4ba410` |
| Kill condition, ID 5 | `0x4b8fd0` | kills `0x4b9d30` |
| Deaths condition, ID 15 | `0x4b9360` | deaths `0x4b99e0` |
| Score condition, ID 21 | `0x4b9020` | score `0x4ba510` |
| Set Resources action, ID 26 | `0x4b6940` | Set `0x4b82d0`, Add `0x4b6be0`, Subtract `0x4b8810` |
| Set Score action, ID 27 | `0x4b6990` | Set `0x4b8440`, Add `0x4b6cf0`, Subtract `0x4b8950` |
| Set Deaths action, ID 45 | `0x4b6550` | Set `0x4b7e80`, Add `0x4b6a90`, Subtract `0x4b8640` |

For a raw 20-byte condition, the wrapper reads player selector at `+4`, threshold
at `+8`, comparison byte at `+0xe`, and resource/score byte at `+0x10` or unit
WORD at `+0xc`. The scalar query receives `ECX=selector`, `EDX=type` and a zero
stack argument. For a raw 32-byte action, the wrapper reads selector at `+0x10`,
amount at `+0x14`, type WORD at `+0x18` and modifier byte at `+0x1b`.

The original conditions compare **unsigned 32-bit** values. Comparison `0` means
at least, `1` at most and `10` exactly. Other comparison codes return false.
For example, `0x4b8fa5` subtracts 9 after the earlier decrement, and `0x4b8faa`
compares query and threshold before `sete`. The `sbb eax,eax; inc eax` paths at
`0x4b8fb5` and `0x4b8fbc` implement the two unsigned inequalities.

Action modifiers `7/8/9` select Set/Add/Subtract. The wrappers return `1` even
for an invalid modifier; an invalid modifier does not mutate the scalar state.
Individual add operations and grouped query sums wrap at 32 bits. Subtraction
saturates each destination at zero using unsigned comparisons.

## Resources

Resource types are `0=minerals`, `1=gas`, `2=both`; other types return zero or
perform no mutation. Querying both returns the wrapped sum of current minerals
and current gas. A Both action applies the amount to each resource separately.

| Field | Per-player array VA |
| --- | --- |
| Current minerals | `0x5263d0` |
| Current gas | `0x526400` |
| Cumulative minerals gathered | `0x526460` |
| Cumulative gas gathered | `0x526430` |

Each element is a DWORD at `base + 4*player`. Set and Add affect both the current
resource field and its cumulative field. Subtract affects current resources
only. At `0x4b837e`/`0x4b8385`, Set writes current and cumulative minerals; the
gas writes are at `0x4b8391`/`0x4b8398`. The native Subtract helpers preserve the
cumulative arrays in all tested cases.

This distinction affects mission bookkeeping and end-of-mission resource totals.
An implementation that treats Set Resources as a wallet-only action would change
observable counters. The probe also tests zero amounts, overflow additions and
subtractions larger than the current balance.

## Score

The score getter uses the following components:

| Type | Meaning | Array or expression |
| --- | --- | --- |
| 0 | Total | Units + Buildings + Kills + Razings + Custom |
| 1 | Units | `0x5291b4` |
| 2 | Buildings | `0x529304` |
| 3 | Units and Buildings | Units + Buildings |
| 4 | Kills | `0x5291e4` |
| 5 | Razings | `0x529334` |
| 6 | Kills and Razings | Kills + Razings |
| 7 | Custom | `0x5295d4` |

An invalid score type returns zero and makes no action writes. Set/Add/Subtract
for a single component use the amount directly. Types `3` and `6` distribute
`((amount + 1) mod 2^32) >> 1` to **each** of their two components. Total type
`0` distributes `((amount + 3) mod 2^32) >> 2` to **each** of Units, Buildings,
Kills and Razings, while leaving Custom untouched. These rounding operations
overflow before shifting, as tested with `0xffffffff`.

Total Score query includes Custom even though Total Score mutation does not
change it. Thus, setting Total Score to 5 gives each standard component 2; if
Custom was 934, the subsequent Total query is 942. The fixture makes all five
components distinct to expose that difference. Subtraction clamps each selected
component independently, rather than subtracting from a combined total.

The Subtract Total instructions are explicit: `0x4b8b57 add eax,3` and
`0x4b8b5a shr eax,2`, followed by separate comparisons and writes for each of
the four standard arrays. Combined-pair rounding uses `lea edx,[edx+1]` and
`shr edx,1` at `0x4b8ae0`/`0x4b8ae3` and `0x4b8b11`/`0x4b8b14`.

## Death and kill counters

For unit IDs `0..227`, individual deaths are DWORDs at
`0x531644 + 4*(12*unit_id + player)`. Individual kills use
`0x52eb84 + 4*(12*unit_id + player)`. These are scalar historical counters;
the helpers do not scan live units. Set Deaths updates the death array only.

| Group ID | Query | Death aggregate base | Kill aggregate base |
| --- | --- | --- | --- |
| 229, Any Units | Men + Buildings | both rows below | both rows below |
| 230, Men | Men | `0x529154` | `0x529184` |
| 231, Buildings | Buildings | `0x5292a4` | `0x5292d4` |
| 232, Factories | Factories | `0x5293c4` | `0x5293f4` |

The Any Units query sums the Men and Buildings aggregates; it does not sum the
228 individual-unit counters. The fixture intentionally makes aggregates differ
from the individual array so this cannot pass accidentally.

Set Deaths with IDs `230/231/232` changes only that aggregate. It neither rewrites
individual counters nor updates the other aggregates. IDs `228` and `229` are
no-ops for Set/Add/Subtract. The complete original setters were executed for both
IDs. Query ID `228` falls through to the individual-array addressing code and
is outside the supported unit domain; it was not executed in this batch.

The individual queries do not bounds-check every direct selector or unit ID.
This report does not generalize their behavior into EUD compatibility. The
offline converter must distinguish ordinary campaign values from out-of-domain
inputs instead of treating arbitrary adjacent memory as a game contract.

## Player selectors

The scalar helpers share callback aggregators. For every selected player the
aggregator calls the actual scalar query or mutator recursively. Query results
are added modulo `2^32`. Mutation callbacks return zero, and the action wrapper
still returns one.

| Selector | Original scalar selection contract | Aggregator VA |
| --- | --- | --- |
| `0..11` | Direct slot, without active-player filtering | scalar helper itself |
| 13, Current Player | Slot in DWORD `0x6ca754` | getter `0x4bda70` |
| 14, Foes | Eligible other slots with current→other alliance byte 0 | `0x4b9b10` |
| 15, Allies | Eligible other slots with current→other byte 2 | `0x4b9730` |
| 16, Neutral Players | Eligible other slots with current→other byte 1 | `0x4b9f70` |
| 17, All Players | Eligible `0..7`, then `8..11` unconditionally | `0x4b9660` |
| `18..21`, Forces 1–4 | Eligible slots whose force byte is `selector-17` | `0x4b9c60` |
| `22..25` | Empty selection | scalar jump-table zero-return target |
| 26, Non-Allied Victory Players | Eligible other slots except mutual alliance 2 | `0x4b9e60` |

Allies excludes Current Player even if the self-alliance byte is 2. Foes and
Neutral Players also exclude Current. Non-Allied Victory excludes a slot only
when **both** alliance directions equal 2; a one-sided alliance still counts.
Alliance bytes are at `0x534914 + 12*from_player + to_player`.

The ordinary iterator `0x4b3160`/`0x4b3170` visits ascending slots `0..7`. It
accepts controller bytes `1,2,3,4,7` at `0x6726d0 + 36*slot`. The finished-player
helper `0x4bd590` excludes controllers 1/2 whose state byte at `0x5349e0+slot`
equals 1 or 2. Direct queries do not apply that filter. Force membership is a
one-based byte at `0x6726d2 + 36*slot`, read by the original `0x443560` helper.

When both bytes `0x672664` and `0x67294d` are nonzero, the iterator instead reads
four slot bytes at `0x5264ac`, applying the same eligibility filter. It preserves
order and **does not deduplicate**. The probe supplies `[5,1,5,0]`; queries add
slot 5 twice, and Add/Subtract actions mutate it twice. Set writes can repeat
without changing the final result. This branch must be modeled as iteration,
not as membership in a set.

All Players reads neutral slots `8..11` even when the active iterator yielded no
matching ordinary slot. In contrast, all scalar writers explicitly ignore
direct slots `>=8`. Neutral-slot state therefore contributes to an All Players
condition but is preserved by All Players actions. At `0x4b9699`, the original
All Players helper starts the unconditional loop at slot 8; at `0x4b8368`, Set
Resources rejects writer slots at least 8.

Selector 12, None, is excluded from query probes: unlike unused selectors
22–25, it is not routed through the empty-selection jump-table entry. Arbitrary
direct out-of-range values and EUD operations remain outside this contract.

## Campaign coverage

All **68 physical campaign CHKs** were hash-checked and scanned, including the
four VER 57 prerelease files. These files are not a 68-mission play order. The
64 supported catalog inputs and the four extra physical files are distinguished
in [campaigns.md](campaigns.md).

| Selected raw record | Physical-map occurrences | Distinct parameter checks |
| --- | --- | --- |
| Accumulate condition 4 | 19 | 14 |
| Kill condition 5 | 15 | 10 |
| Deaths condition 15 | 19 | 16 |
| Score condition 21 | 23 | 17 |
| Set Resources action 26 | 218 | 100 |
| Set Score action 27 | 92 | 44 |
| Set Deaths action 45 | 4 | 1 |
| **Total** | **390** | **202** |

The four VER 57 maps account for eight Set Resources actions and one Kill
condition. The remaining 64 supported maps contain 75 selected conditions and
306 selected actions. Sixty-five physical maps contain at least one selected
record. Every selected input uses a supported parameter domain; there are zero
unresolved tuples.

Raw coverage includes ignored actions: one Set Resources record in expansion
Protoss 04 and three Set Score records in expansion Protoss 01 have action
ignore flag 2. Their wrapper inputs were checked, but the normal VM would skip
those actions. There are no ignored selected conditions. Reachability,
conditions becoming true and trigger-owner execution are not inferred from
these counts.

The four Set Deaths occurrences all set player selector 6, unit ID 15, amount 0,
modifier 7, in expansion Terran 02. They occur in raw trigger indices
16/50/55/60 and action slots 11/10/15/7. All are active records. Native execution
on the fixture changes exactly the associated individual death counter.

The synthetic checks consist of 730 getters, 1,308 condition wrappers and 1,896
action wrappers. They exercise every in-domain selector, all scalar types,
equality boundaries, invalid comparison/modifier/type codes, ordinary and sparse
controllers, both iterator branches, duplicate alternate slots, zero and maximum
amounts, unsigned thresholds, wraparound and saturation. Campaign checks reuse
the ordinary fixture with Current Player 2; they validate the decoded argument
contract and do not replay the mission's owner state.

## Comparison with pinned primary sources

[BWAPI's trigger declarations](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h)
provide the condition/action IDs, flags and selector names.
[PyMS TRG.py](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/TRG.py)
provides resource and score type names. These sources supply structure and
terminology; the hash-pinned native results establish the contracts above.

The pinned [OpenBW trigger implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L18785)
is useful as a cross-check, but has material differences in these paths:

- `trigger_count_comparison` uses exactly code 2; the original wrappers here use
  exactly code 10. Original probes return false for code 2.
- `trigger_players` applies `trigger_players_pred`, whose initial
  `player_slot_active` filter rejects slots at least 8. Original scalar All
  Players queries explicitly append slots 8–11, and direct queries bypass the
  active-player filter.
- `execute_trigger_action` case 26 repeats `else if (a.num_n == 8)` for its
  subtraction branch. In that pinned source the second Add test makes native
  modifier 9 subtraction unreachable. The original helper executes modifier 9
  and clamps the current balance. No selected retail resource action uses
  modifier 9, so this difference is revealed by the synthetic original probes.

Those observations are version-pinned source comparisons, not claims about
every OpenBW revision or every trigger subsystem. Use the original witnesses
and reproducible probes when implementing or resolving conflicting accounts.

## Remaining dependencies

This batch does not execute the code that produces resource income, death/kill
statistics, build/destroy scores or end-of-game totals. Those producers must
maintain the separately observed individual and aggregate counters correctly.
It also does not close Opponents, most/least ranking conditions, Command/Bring,
location traversal, unit creation/removal or mission playback. Save/load must
preserve the scalar arrays and selector state alongside the separate trigger VM
state. Full campaign parity still requires original mission traces and gameplay
regression once those systems are integrated.
