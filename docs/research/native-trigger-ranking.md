# Native Opponents and scalar ranking conditions, StarCraft 1.17

The original executable passed **16,784 isolated checks** for the Opponents
condition and the six scalar most/least conditions. It complements
[native-trigger-counters.md](native-trigger-counters.md), whose resource, score,
kill and player-selector helpers are the actual callees used here.

No condition IDs `8/9/10/14/18/19/20` occur in any of the **68 physical campaign
CHKs** in the hash-gated retail manifest. These routines still matter for a
faithful original-game contract, but this batch supplies synthetic original
function evidence. It does not claim played campaign use or runtime mission
coverage for an absent record type.

## Evidence boundaries

The target executable is StarCraft 1.17.0.1, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image
base `0x400000`. All listed addresses are preferred VAs; the generated evidence
also records RVAs and each executed instruction's bytes and operands.

The [probe script](../../tools/research/probe_native_trigger_ranking_117.py)
executes complete original functions with their real callees in private Unicorn
x86-32 memory. Per-instruction bounds prohibit imports, unrelated code and the
game entry point. There are no stubs or original game/file/network execution.
The successful-run results are in
[native-trigger-ranking-probes.json](evidence/native-trigger-ranking-probes.json).
The successful run used `x86_64` under Rosetta, Python 3.9.6, Unicorn 2.1.4,
pefile 2024.8.26 and Capstone 5.0.6. The probe script SHA-256 is
`c544c63c20bea9fc0c1de9a933e0b3175cb48b7da88149f36cd0b1cf91d35c74`.
The evidence contains 860 distinct executed instruction witnesses.
Every fixture supplies explicit scalar arrays, controllers, finished states,
force memberships, alliances, Current Player and iterator mode. The probes
check that scalar arrays and alliances remain unchanged.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_ranking_117.py \
  --exe /path/to/StarCraft.exe \
  --data /path/to/extracted-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-ranking-probes.json
```

Without the execution flag, the script prints its plan. The manifest and each
input CHK are hash-checked. The evidence records the probe-script SHA-256 and
runtime versions. See [reproduction.md](reproduction.md) for extraction and
dependency setup. Campaign originals stay outside the repository.

## Scalar most/least predicates

| Condition ID | Original wrapper VA | Scalar helper VA |
| --- | --- | --- |
| 8, Most Kills | `0x4b9160` | kills `0x4b9d30` |
| 9, Highest Score | `0x4b91c0` | score `0x4ba510` |
| 10, Most Resources | `0x4b9220` | resources `0x4ba410` |
| 18, Least Kills | `0x4b94a0` | kills `0x4b9d30` |
| 19, Lowest Score | `0x4b9510` | score `0x4ba510` |
| 20, Least Resources | `0x4b9580` | resources `0x4ba410` |

Each wrapper iterates the eligible player sequence from
`0x4b3160`/`0x4b3170`, queries each player's scalar value, retains the Current
Player's value, and maintains an **unsigned** maximum or minimum. It succeeds
when Current Player is tied for that extreme. It does not require sole first or
last place.

The eligible sequence has the same controller/finished-state and alternate
four-slot behavior documented in the counters report. It ordinarily contains
only eligible slots `0..7`. These ranking functions do not append neutral slots
`8..11`, even though the scalar All Players query does. Duplicate alternate
slots are visited repeatedly; repeating a value does not change the extreme.

The raw condition selector at `+4`, amount at `+8` and comparison at `+0xe` are
ignored by these six wrappers. The probes populate them with `0xffffffff`,
`0x12345678` and 99 respectively. The unit WORD at `+0xc` selects a kill counter;
the byte at `+0x10` selects resource/score type. Current Player is read from the
native DWORD `0x6ca754`.

The maximum routines initialize both the extreme and the retained Current
Player value to 0. Minimum routines initialize both to `0xffffffff`. If Current
Player never appears in the eligible sequence, its retained value stays at that
initial value. Consequently:

- An empty eligible sequence makes all six conditions true.
- An ineligible Current Player passes a most predicate when every eligible
  queried value is zero; otherwise it fails.
- An ineligible Current Player passes a least predicate when every eligible
  queried value is `0xffffffff`; otherwise it fails.
- Invalid resource/score types query as zero. This can produce a tie even when
  the condition's type is outside the ordinary editor domain.

The native maximum uses `cmova`, for example `0x4b91a1`; the minimum uses
`cmovb`, for example `0x4b94e3`. Current-value capture is `cmove` after comparing
the iterator slot with the Current Player getter. The final `cmp; sbb eax,eax;
inc eax` implements the appropriate unsigned inequality. Since the retained
Current value normally participates in the extreme, these inequalities implement
ties; the explicit initial values explain the inactive and empty cases.

The probes include zero ties, nonzero ties, `0xffffffff` ties, ascending and
descending values, and mixed values around `0x80000000`. Combined resources,
scores and Any Units kills wrap at 32 bits before the unsigned rank comparison.
This distinguishes unsigned ranking from signed comparisons and from ranking
an unbounded host integer sum.

## Opponents

Condition ID 14 enters `0x4b9320`, calls the original helper `0x4ba060`, converts
its return through `movzx edx,al` at `0x4b9332`, and compares it with the raw
DWORD amount using comparison codes `0/1/10`. Comparison code 2 and other codes
return false. The helper's ordinary counts are small; the byte conversion is a
static contract, rather than a claim that these fixtures produce a count over
255.

For a direct owner, the helper first rejects owners at least 8 and owners whose
controller is 3 or 7. It then builds a coalition from the eligible player
sequence: the owner itself, if eligible, plus eligible players that are mutually
allied with the owner. Controllers 3 and 7 are excluded from the coalition.

For **each** coalition member, it visits eligible other players, excludes
controllers 3/7, and marks a player as an opponent unless both directional
alliance bytes with that member equal 2. An eight-byte local bitmap records the
union. The return value is the number of marked bits. Repeated iterator slots
therefore do not cause duplicate opponent counts inside one direct owner's
bitmap.

The direct contract can be expressed as:

```text
team(owner) = eligible owner and eligible mutual allies of owner,
              excluding controllers 3 and 7
opponents(owner) = union over team members of their eligible other players
                   that are not mutual allies, excluding controllers 3 and 7
return |opponents(owner)|
```

The coalition is selected relative to the owner; it does not require all team
members to be mutually allied with each other. For example, owner 0 is mutually
allied with 1 and 2, but 1 and 2 are enemies. In the fixture, owner's own
non-allied set is `{3,4,5,6,7}`. The coalition union also marks 1 and 2, and the
original Opponents helper returns 7. Treating Opponents as just the owner's
non-allied player count would return 5 and change the condition.

The machine-code witnesses expose that union directly:

- `0x4ba0a2 xorps xmm0,xmm0` and `0x4ba0a9 movq [ebp-0x10],xmm0` clear the
  eight-byte bitmap.
- The mutual-alliance tests at `0x4ba0d9` and `0x4ba0e6` select coalition members.
- The tests at `0x4ba11b` and `0x4ba128` check both opponent alliance directions.
- `0x4ba132 mov byte ptr [ebp+eax-0x10],1` marks a bitmap slot.
- `0x4ba15e..0x4ba199` count the eight marks.

The same scalar selector aggregators handle group selectors. A grouped
Opponents query **sums the separate direct-owner counts**; it does not compute
one deduplicated union across selected owners. For eight mutually hostile
ordinary players, each direct count is 7 and All Players returns 56. The four
neutral slots appended by All Players each return zero from the direct helper.
An alternate iterator can select the same owner twice and add that owner's
bitmap count twice, even though each bitmap internally deduplicates opponents.

Direct owners do not themselves undergo the active-player iterator filter
before the initial controller checks. An inactive owner can still have eligible
mutual allies whose opponents contribute to the bitmap. The sparse-controller
fixtures exercise this distinction separately from group selection.

The synthetic alliance inputs include all foes, all allies, all neutral
relations, the nontransitive coalition example, and a deterministic asymmetric
matrix generated with host `Random(117)`. This host random generator only creates
fixed inputs; no claim about the game's RNG follows from it.

## Checked cases and campaign audit

The 6,384 ranking checks cover each scalar type across six profiles, four
iterator/controller modes and seven Current Player slots. The Current slots
include ordinary eligible players, inactive players and neutral slots 8/11.
The modes are ordinary, sparse controllers with finished states, the alternate
`[5,1,5,0]` sequence and an empty eligible sequence.

| Ranking condition | Passing original calls |
| --- | --- |
| Most Kills | 1,008 |
| Highest Score | 1,512 |
| Most Resources | 672 |
| Least Kills | 1,008 |
| Lowest Score | 1,512 |
| Least Resources | 672 |

The Opponents checks comprise 2,080 complete helper calls and 8,320 complete
condition-wrapper calls. They cover the four modes, five alliance matrices,
Current slots `0/2/7/8`, and all normal direct/group selectors except None 12.
For each helper input the wrapper runs with at-least, at-most, exactly and
invalid comparison codes, cycling thresholds below/equal/above the queried
value. Every original call returns and leaves the checked input arrays intact.

The audit scanned all **1,907 raw TRIG records** from the 68 physical CHKs,
including ignored records and the four VER 57 files. Every map has a hash and
selected-condition histogram in the evidence. The histogram is empty for all
seven condition IDs; consequently there are zero retail parameter checks in
this batch. The 68 files are not claimed to be a mission play order.

## Primary-source comparison and remaining work

[BWAPI trigger declarations](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h)
identify the condition and selector names. The pinned
[OpenBW `trigger_opponent_count`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L18879)
adds each selected player's `trigger_players(p,26)` range size. That source
expression lacks the original helper's coalition bitmap union, controller 3/7
exclusions and direct-owner handling. It is a useful structural reference, but
cannot substitute for the original probes on this contract.

The original scalar producers, alliance changes, game-mode setup and finished
state transitions remain separate dependencies. Command/Bring and their
most/least variants require live-unit, cargo, completion and location traversal
contracts; they are not implemented by these scalar counters. No full trigger
scheduler, mission playback, UI leaderboard or save/load is executed here.
